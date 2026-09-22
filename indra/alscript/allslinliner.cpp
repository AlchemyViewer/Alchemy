/**
 * @file allslinliner.cpp
 * @brief A user function called once put where it is called, in the text.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "allslinliner.h"

#include "allslservice.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>
#include <tailslide/visitor.hh>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <vector>

using namespace Tailslide;

namespace
{
    // A place in the text, zero-based; an end is past its last byte.
    struct Pos
    {
        S32 line   = 0;
        S32 column = 0;
        bool operator<(const Pos& o) const { return line < o.line || (line == o.line && column < o.column); }
        bool operator==(const Pos& o) const { return line == o.line && column == o.column; }
    };

    Pos beginOf(LSLASTNode* node)
    {
        const Tailslide::YYLTYPE* loc = node->getLoc();
        return Pos{ std::max(0, loc->first_line - 1), std::max(0, loc->first_column - 1) };
    }

    // Tailslide's last column is one-based and one past the last byte.
    Pos endOf(LSLASTNode* node)
    {
        const Tailslide::YYLTYPE* loc = node->getLoc();
        return Pos{ std::max(0, loc->last_line - 1), std::max(0, loc->last_column - 1) };
    }

    typedef std::vector<std::string> Lines;

    Lines splitLines(std::string_view text)
    {
        Lines  lines;
        size_t start = 0;
        while (true)
        {
            const size_t nl = text.find('\n', start);
            if (nl == std::string_view::npos)
            {
                lines.emplace_back(text.substr(start));
                break;
            }
            lines.emplace_back(text.substr(start, nl - start));
            start = nl + 1;
        }
        return lines;
    }

    std::string slice(const Lines& lines, Pos from, Pos to)
    {
        std::string out;
        for (S32 line = from.line; line <= to.line && line < static_cast<S32>(lines.size()); ++line)
        {
            const std::string& text  = lines[static_cast<size_t>(line)];
            const S32          begin = line == from.line ? std::min(from.column, static_cast<S32>(text.size())) : 0;
            const S32          end   = line == to.line ? std::min(to.column, static_cast<S32>(text.size())) : static_cast<S32>(text.size());
            if (line > from.line)
            {
                out += '\n';
            }
            if (end > begin)
            {
                out.append(text, static_cast<size_t>(begin), static_cast<size_t>(end - begin));
            }
        }
        return out;
    }

    const char* typeWord(LSLIType type)
    {
        switch (type)
        {
            case LST_INTEGER:       return "integer";
            case LST_FLOATINGPOINT: return "float";
            case LST_STRING:        return "string";
            case LST_KEY:           return "key";
            case LST_VECTOR:        return "vector";
            case LST_QUATERNION:    return "rotation";
            case LST_LIST:          return "list";
            default:                return nullptr;
        }
    }

    // Every node of a subtree, in order.
    struct Gather : public ASTVisitor
    {
        std::vector<LSLASTNode*> nodes;
        bool                     visit(LSLASTNode* node) override
        {
            nodes.push_back(node);
            return true;
        }
    };

    std::vector<LSLASTNode*> nodesOf(LSLASTNode* root)
    {
        Gather gather;
        root->visit(&gather);
        return gather.nodes;
    }

    bool isInside(LSLASTNode* node, LSLASTNode* ancestor)
    {
        for (LSLASTNode* n = node; n; n = n->getParent())
        {
            if (n == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    // What one edit of the text says: a stretch replaced by pieces, each
    // piece text with where it came from, so that the map can say.
    struct Piece
    {
        std::string text;  // one line's worth, no newline
        Pos         from;  // where in the source, for the map
        bool        verbatim = true;
    };
    typedef std::vector<Piece> PieceLine;  // a line of the output
    typedef std::vector<PieceLine> Block;  // several lines

    struct Edit
    {
        Pos   begin, end;  // what is replaced, in the source
        Block with;
    };

    struct Rename
    {
        Pos         begin, end;
        std::string name;
    };

    // The text between two places with renames applied, as lines of
    // pieces: verbatim around each renamed name, the name itself from
    // where the old one was.
    Block renamedText(const Lines& lines, Pos from, Pos to, const std::vector<Rename>& renames)
    {
        Block block;
        for (S32 line = from.line; line <= to.line && line < static_cast<S32>(lines.size()); ++line)
        {
            const std::string& text  = lines[static_cast<size_t>(line)];
            const S32          begin = line == from.line ? std::min(from.column, static_cast<S32>(text.size())) : 0;
            const S32          end   = line == to.line ? std::min(to.column, static_cast<S32>(text.size())) : static_cast<S32>(text.size());
            PieceLine          out;
            S32                at = begin;
            for (const Rename& r : renames)
            {
                if (r.begin.line != line || r.begin.column < at || r.end.column > end)
                {
                    continue;
                }
                if (r.begin.column > at)
                {
                    out.push_back(Piece{ text.substr(static_cast<size_t>(at), static_cast<size_t>(r.begin.column - at)), Pos{ line, at }, true });
                }
                out.push_back(Piece{ r.name, r.begin, false });
                at = r.end.column;
            }
            if (end > at)
            {
                out.push_back(Piece{ text.substr(static_cast<size_t>(at), static_cast<size_t>(end - at)), Pos{ line, at }, true });
            }
            block.push_back(std::move(out));
        }
        if (block.empty())
        {
            block.push_back(PieceLine());
        }
        return block;
    }

    // The source with the edits made -- each replacing its stretch with
    // its block, and whole lines dropped where asked -- and the map from
    // the result back to it.
    struct Weave
    {
        const Lines&      lines;
        std::vector<Edit> edits;
        std::set<S32>     droppedLines;

        explicit Weave(const Lines& source) : lines(source) {}

        void run(std::string& text, ALSourceMap& map)
        {
            std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
            map = ALSourceMap();
            map.addFile(std::string(), std::string());
            text.clear();
            S32    outLine   = 0;
            S32    outColumn = 0;
            auto   emit      = [&](const Piece& piece) {
                if (piece.text.empty())
                {
                    return;
                }
                ALSourceMap::Segment s;
                s.outLine   = outLine;
                s.outColumn = outColumn;
                s.length    = static_cast<S32>(piece.text.size());
                s.line      = piece.from.line;
                s.column    = piece.from.column;
                s.verbatim  = piece.verbatim;
                map.add(s);
                text += piece.text;
                outColumn += s.length;
            };
            auto newline = [&]() {
                text += '\n';
                ++outLine;
                outColumn = 0;
            };
            size_t edit = 0;
            for (S32 line = 0; line < static_cast<S32>(lines.size()); ++line)
            {
                if (droppedLines.count(line))
                {
                    while (edit < edits.size() && edits[edit].begin.line == line)
                    {
                        ++edit;
                    }
                    continue;
                }
                const std::string& source = lines[static_cast<size_t>(line)];
                S32                at     = 0;
                while (edit < edits.size() && edits[edit].begin.line == line)
                {
                    const Edit& e = edits[edit++];
                    if (e.begin.column > at)
                    {
                        emit(Piece{ source.substr(static_cast<size_t>(at), static_cast<size_t>(e.begin.column - at)), Pos{ line, at }, true });
                    }
                    for (size_t k = 0; k < e.with.size(); ++k)
                    {
                        if (k > 0)
                        {
                            newline();
                        }
                        for (const Piece& piece : e.with[k])
                        {
                            emit(piece);
                        }
                    }
                    // The rest of the line the edit ends on, which may be
                    // a later line.
                    if (e.end.line != line)
                    {
                        line = e.end.line;
                    }
                    at = e.end.column;
                }
                const std::string& tail = lines[static_cast<size_t>(line)];
                if (at < static_cast<S32>(tail.size()))
                {
                    emit(Piece{ tail.substr(static_cast<size_t>(at)), Pos{ line, at }, true });
                }
                if (line + 1 < static_cast<S32>(lines.size()))
                {
                    newline();
                }
            }
            map.finish();
        }
    };

    // The names visible from a node: every symbol table up from it, and
    // the builtins.
    std::set<std::string> visibleFrom(LSLASTNode* node)
    {
        std::set<std::string> names;
        for (LSLASTNode* n = node; n; n = n->getParent())
        {
            if (LSLSymbolTable* table = n->getSymbolTable())
            {
                for (auto& entry : table->getMap())
                {
                    names.insert(entry.first);
                }
            }
        }
        return names;
    }

    bool isBuiltinOrKeyword(const std::string& name, ScriptContext& context)
    {
        static const char* const keywords[] = { "default", "state", "event", "jump", "return", "if", "else", "for", "do", "while", "print", "integer",
                                                "float", "string", "key", "vector", "rotation", "quaternion", "list", "TRUE", "FALSE", nullptr };
        for (const char* const* k = keywords; *k; ++k)
        {
            if (name == *k)
            {
                return true;
            }
        }
        return context.builtins && context.builtins->lookup(name.c_str(), SYM_ANY);
    }

    ALScriptProblem noteAt(LSLASTNode* at, const std::string& message)
    {
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Note;
        p.source   = ALScriptProblem::Source::Optimizer;
        const Pos b = beginOf(at);
        const Pos e = endOf(at);
        p.line      = b.line;
        p.column    = b.column;
        p.endLine   = e.line;
        p.endColumn = e.column;
        p.message   = message;
        return p;
    }

    // One round: the first function that can go in place goes. Whether
    // one did.
    bool inlineOne(std::string& text, ALSourceMap& map, ALScriptProblem& note, const std::vector<std::string>& marked)
    {
        ScopedScriptParser parser(nullptr);
        LSLScript*         script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script || parser.logger.getErrors())
        {
            return false;
        }
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        if (parser.logger.getErrors())
        {
            return false;
        }
        const Lines lines = splitLines(text);

        // The functions, and every call of each.
        std::vector<LSLGlobalFunction*>                         functions;
        std::map<LSLSymbol*, std::vector<LSLFunctionExpression*>> calls;
        for (LSLASTNode* node : nodesOf(script))
        {
            if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                functions.push_back(static_cast<LSLGlobalFunction*>(node));
            }
            else if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                auto*      call = static_cast<LSLFunctionExpression*>(node);
                LSLSymbol* sym  = call->getIdentifier()->getSymbol();
                if (sym && sym->getSubType() != SYM_BUILTIN)
                {
                    calls[sym].push_back(call);
                }
            }
        }
        for (LSLGlobalFunction* function : functions)
        {
            LSLSymbol* sym = function->getSymbol();
            if (!sym)
            {
                continue;
            }
            const auto found = calls.find(sym);
            if (found == calls.end() || found->second.empty())
            {
                continue;
            }
            // The one call, or the first of several where the function is
            // marked or is small enough to go everywhere; the function
            // goes with its last call.
            const bool   is_marked = std::find(marked.begin(), marked.end(), sym->getName()) != marked.end();
            const size_t callCount = found->second.size();
            LSLFunctionExpression* call = found->second.front();
            const bool   last      = callCount == 1;
            if (isInside(call, function))
            {
                continue;
            }
            // The definition must have its lines to itself, since they go.
            const Pos fbegin = beginOf(function);
            const Pos fend   = endOf(function);
            {
                const std::string& first = lines[static_cast<size_t>(fbegin.line)];
                const std::string& last  = lines[static_cast<size_t>(fend.line)];
                const bool before = first.find_first_not_of(" \t", 0) < static_cast<size_t>(fbegin.column);
                const bool after  = last.find_first_not_of(" \t", static_cast<size_t>(fend.column)) != std::string::npos;
                if (before || after)
                {
                    continue;
                }
            }
            LSLStatement* body = function->getStatements();
            if (!body || body->getNodeSubType() != NODE_COMPOUND_STATEMENT)
            {
                continue;
            }
            const bool returnsNothing = sym->getType() == nullptr || sym->getType()->getIType() == LST_NULL;

            // The parameters, in order, with their symbols.
            std::vector<LSLIdentifier*> params;
            if (LSLFunctionDec* dec = function->getArguments())
            {
                for (LSLASTNode* p : *dec)
                {
                    params.push_back(static_cast<LSLIdentifier*>(p));
                }
            }
            std::vector<LSLExpression*> args;
            if (LSLASTNodeList<LSLExpression>* list = call->getArguments())
            {
                for (LSLExpression* a : *list)
                {
                    args.push_back(a);
                }
            }
            if (args.size() != params.size())
            {
                continue;
            }

            // The call's statement, where the call is the statement.
            LSLASTNode* statement = call->getParent();
            const bool  bare      = statement && statement->getNodeType() == NODE_STATEMENT && statement->getNodeSubType() == NODE_EXPRESSION_STATEMENT &&
                               static_cast<LSLExpressionStatement*>(statement)->getExpr() == call;

            const std::vector<LSLASTNode*> bodyNodes = nodesOf(body);

            if (returnsNothing && bare)
            {
                if (!last && !is_marked)
                {
                    continue;
                }
                // A block in place of the statement: each parameter a
                // local set to its argument, then the body.
                bool plain = true;
                for (LSLASTNode* n : bodyNodes)
                {
                    if (n->getNodeType() == NODE_STATEMENT)
                    {
                        const LSLNodeSubType kind = n->getNodeSubType();
                        if (kind == NODE_RETURN_STATEMENT || kind == NODE_LABEL || kind == NODE_JUMP_STATEMENT || kind == NODE_STATE_STATEMENT)
                        {
                            plain = false;
                        }
                    }
                }
                if (!plain)
                {
                    continue;
                }
                // The names the body declares, and what each is called in
                // the block: its own name unless that is visible where the
                // call is.
                std::set<LSLSymbol*>              declared;
                std::map<LSLSymbol*, std::string> renamed;
                for (LSLIdentifier* p : params)
                {
                    declared.insert(p->getSymbol());
                }
                for (LSLASTNode* n : bodyNodes)
                {
                    if (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_DECLARATION)
                    {
                        declared.insert(static_cast<LSLDeclaration*>(n)->getIdentifier()->getSymbol());
                    }
                }
                const std::set<std::string> visible = visibleFrom(statement);
                std::set<std::string>       taken   = visible;
                for (LSLSymbol* d : declared)
                {
                    if (d)
                    {
                        taken.insert(d->getName());
                    }
                }
                for (LSLSymbol* d : declared)
                {
                    if (!d)
                    {
                        continue;
                    }
                    if (!visible.count(d->getName()))
                    {
                        continue;
                    }
                    std::string fresh;
                    for (int n = 1;; ++n)
                    {
                        fresh = std::string(d->getName()) + "_" + std::to_string(n);
                        if (!taken.count(fresh) && !isBuiltinOrKeyword(fresh, parser.context))
                        {
                            break;
                        }
                    }
                    taken.insert(fresh);
                    renamed[d] = fresh;
                }
                std::vector<Rename> renames;
                for (LSLASTNode* n : nodesOf(function))
                {
                    if (n->getNodeType() == NODE_IDENTIFIER)
                    {
                        auto*      id = static_cast<LSLIdentifier*>(n);
                        const auto r  = renamed.find(id->getSymbol());
                        if (r != renamed.end() && id->getSymbol())
                        {
                            renames.push_back(Rename{ beginOf(id), endOf(id), r->second });
                        }
                    }
                }
                std::sort(renames.begin(), renames.end(), [](const Rename& a, const Rename& b) { return a.begin < b.begin; });

                // The block: an opening brace at the call, the parameters
                // as locals, the body's own lines between its braces, a
                // closing brace.
                const Pos bbegin = beginOf(body);
                const Pos bend   = endOf(body);
                const Pos inner_begin{ bbegin.line, bbegin.column + 1 };
                const Pos inner_end{ bend.line, std::max(0, bend.column - 1) };
                Block     block;
                block.push_back(PieceLine{ Piece{ "{", beginOf(statement), false } });
                for (size_t i = 0; i < params.size(); ++i)
                {
                    LSLSymbol*  psym = params[i]->getSymbol();
                    const char* type = psym && psym->getType() ? typeWord(psym->getType()->getIType()) : nullptr;
                    if (!type)
                    {
                        block.clear();
                        break;
                    }
                    const auto        r    = renamed.find(psym);
                    const std::string name = r == renamed.end() ? psym->getName() : r->second;
                    // The argument as written, with the body's renames
                    // not applying to it: it is the caller's text.
                    const std::string arg = slice(lines, beginOf(args[i]), endOf(args[i]));
                    block.push_back(PieceLine{ Piece{ std::string(type) + " " + name + " = ", beginOf(params[i]), false },
                                               Piece{ arg, beginOf(args[i]), true }, Piece{ ";", beginOf(params[i]), false } });
                }
                if (block.empty())
                {
                    continue;
                }
                for (PieceLine& line : renamedText(lines, inner_begin, inner_end, renames))
                {
                    block.push_back(std::move(line));
                }
                block.push_back(PieceLine{ Piece{ "}", endOf(statement), false } });

                Weave weave(lines);
                weave.edits.push_back(Edit{ beginOf(statement), endOf(statement), std::move(block) });
                if (last)
                {
                    for (S32 line = fbegin.line; line <= fend.line; ++line)
                    {
                        weave.droppedLines.insert(line);
                    }
                }
                note = noteAt(statement, std::string("put the function ") + sym->getName() + (last ? " in place of its one call" : " in place of a call"));
                weave.run(text, map);
                return true;
            }

            if (!returnsNothing && !bare)
            {
                // The expression in place of the call: the body must be one
                // return of an expression that changes nothing, over
                // arguments that are constants or names.
                LSLASTNode* only = body->getChild(0);
                if (!only || only->getNext() || only->getNodeSubType() != NODE_RETURN_STATEMENT)
                {
                    continue;
                }
                LSLExpression* expr = static_cast<LSLReturnStatement*>(only)->getExpr();
                if (!expr)
                {
                    continue;
                }
                // Several calls: only where marked, or small enough that
                // the expression costs about what the call did.
                if (!last && !is_marked && nodesOf(expr).size() > 8)
                {
                    continue;
                }
                bool simple = true;
                for (LSLASTNode* n : nodesOf(expr))
                {
                    if (n->getNodeType() == NODE_EXPRESSION && operation_mutates(static_cast<LSLExpression*>(n)->getOperation()))
                    {
                        simple = false;
                    }
                }
                std::map<LSLSymbol*, int> uses;
                for (LSLASTNode* n : nodesOf(expr))
                {
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        ++uses[static_cast<LSLLValueExpression*>(n)->getIdentifier()->getSymbol()];
                    }
                }
                std::vector<std::string> argText(args.size());
                for (size_t i = 0; i < args.size() && simple; ++i)
                {
                    const LSLNodeSubType kind = args[i]->getNodeSubType();
                    const bool constant       = kind == NODE_CONSTANT_EXPRESSION;
                    const bool name           = kind == NODE_LVALUE_EXPRESSION;
                    if (!constant && !name)
                    {
                        simple = false;
                        break;
                    }
                    if (!constant && uses[params[i]->getSymbol()] > 1)
                    {
                        simple = false;
                        break;
                    }
                    argText[i] = slice(lines, beginOf(args[i]), endOf(args[i]));
                    if (!constant && static_cast<LSLLValueExpression*>(args[i])->getMember())
                    {
                        argText[i] = "(" + argText[i] + ")";
                    }
                }
                if (!simple)
                {
                    continue;
                }
                // The expression's text with each parameter's name replaced
                // by its argument.
                std::vector<Rename> renames;
                for (LSLASTNode* n : nodesOf(expr))
                {
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        LSLIdentifier* id = static_cast<LSLLValueExpression*>(n)->getIdentifier();
                        for (size_t i = 0; i < params.size(); ++i)
                        {
                            if (id->getSymbol() && id->getSymbol() == params[i]->getSymbol())
                            {
                                renames.push_back(Rename{ beginOf(id), endOf(id), argText[i] });
                            }
                        }
                    }
                }
                std::sort(renames.begin(), renames.end(), [](const Rename& a, const Rename& b) { return a.begin < b.begin; });
                Block block = renamedText(lines, beginOf(expr), endOf(expr), renames);
                block.front().insert(block.front().begin(), Piece{ "(", beginOf(call), false });
                block.back().push_back(Piece{ ")", endOf(call), false });

                Weave weave(lines);
                weave.edits.push_back(Edit{ beginOf(call), endOf(call), std::move(block) });
                if (last)
                {
                    for (S32 line = fbegin.line; line <= fend.line; ++line)
                    {
                        weave.droppedLines.insert(line);
                    }
                }
                note = noteAt(call, std::string("put what the function ") + sym->getName() + (last ? " returns in place of its one call" : " returns in place of a call"));
                weave.run(text, map);
                return true;
            }
        }
        return false;
    }
} // namespace

// static
ALLSLInliner::Result ALLSLInliner::run(std::string_view source, const std::vector<std::string>& marked)
{
    Result result;
    result.text = std::string(source);
    // The map of the text as it is: each line its own.
    {
        result.map.addFile(std::string(), std::string());
        const Lines lines = splitLines(source);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            ALSourceMap::Segment s;
            s.outLine = static_cast<S32>(i);
            s.line    = static_cast<S32>(i);
            s.length  = static_cast<S32>(lines[i].size());
            result.map.add(s);
        }
        result.map.finish();
    }
    if (!ALLSLService::builtinsLoaded())
    {
        return result;
    }
    // Round after round, each over the text the last made, its map over
    // the last's, until nothing more can go.
    for (int round = 0; round < 256; ++round)
    {
        ALSourceMap     step;
        ALScriptProblem note;
        if (!inlineOne(result.text, step, note, marked))
        {
            break;
        }
        // The note is about this round's text; the source's place is
        // what the map so far says of it.
        const ALSourceMap::Loc from = result.map.toSource(note.line, note.column);
        const ALSourceMap::Loc to   = result.map.toSource(note.endLine, note.endColumn);
        if (from.found())
        {
            note.line   = from.line;
            note.column = from.column;
        }
        if (to.found())
        {
            note.endLine   = to.line;
            note.endColumn = to.column;
        }
        result.notes.push_back(std::move(note));
        result.map = step.composed(result.map);
        ++result.inlined;
    }
    return result;
}

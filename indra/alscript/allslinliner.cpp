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
#include "allsltraits.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>
#include <tailslide/visitor.hh>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
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

    // What one call's inlining comes to: the edit at the call, whether
    // the function goes with it, and the note.
    struct Plan
    {
        Edit            edit;
        // Temporaries for the arguments, put before the call's statement.
        std::optional<Edit> before;
        ALScriptProblem note;
        S32             callLine = 0;
    };

    // The fresh names a round has given out, so that two blocks in one
    // function do not share a label.
    typedef std::set<std::string> Names;

    std::string freshName(const std::string& base, const Names& taken, Names& used, ScriptContext& context)
    {
        for (int n = 1;; ++n)
        {
            const std::string name = base + "_" + std::to_string(n);
            if (!taken.count(name) && !used.count(name) && !isBuiltinOrKeyword(name, context))
            {
                used.insert(name);
                return name;
            }
        }
    }

    // The plan for one call of a function, or none where the shape is
    // not one that can go in place.
    bool plan(const Lines& lines, ScriptContext& context, LSLGlobalFunction* function, LSLFunctionExpression* call, bool last, bool is_marked, Names& used, Plan& out)
    {
        LSLSymbol* sym = function->getSymbol();
        LSLStatement* body = function->getStatements();
        if (!sym || !body || body->getNodeSubType() != NODE_COMPOUND_STATEMENT || isInside(call, function))
        {
            return false;
        }
        const bool returnsNothing = sym->getType() == nullptr || sym->getType()->getIType() == LST_NULL;

        // The parameters, in order, with their symbols; the arguments.
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
            return false;
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
                return false;
            }
            // A block in place of the statement: each parameter a local
            // set to its argument, then the body. A return in the body is
            // a jump to a label after the block; the body's own labels
            // get fresh names, since a label is one to a function; a
            // state change cannot go.
            std::vector<LSLASTNode*> returns;
            std::set<LSLSymbol*>     labels;
            for (LSLASTNode* n : bodyNodes)
            {
                if (n->getNodeType() != NODE_STATEMENT)
                {
                    continue;
                }
                switch (n->getNodeSubType())
                {
                    case NODE_STATE_STATEMENT: return false;
                    case NODE_RETURN_STATEMENT: returns.push_back(n); break;
                    case NODE_LABEL: labels.insert(static_cast<LSLLabel*>(n)->getIdentifier()->getSymbol()); break;
                    default: break;
                }
            }
            // The names the body declares, and what each is called in the
            // block: its own name unless that is visible where the call is.
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
            const Names visible = visibleFrom(statement);
            Names       taken   = visible;
            for (LSLSymbol* d : declared)
            {
                if (d)
                {
                    taken.insert(d->getName());
                }
            }
            for (LSLSymbol* d : declared)
            {
                if (d && visible.count(d->getName()))
                {
                    renamed[d] = freshName(d->getName(), taken, used, context);
                }
            }
            for (LSLSymbol* l : labels)
            {
                if (l)
                {
                    renamed[l] = freshName(l->getName(), taken, used, context);
                }
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
            // A return that is the body's last statement is the end of the
            // block anyway; the rest are jumps to a label after it.
            LSLASTNode* lastStatement = nullptr;
            for (LSLASTNode* child = body->getChild(0); child; child = child->getNext())
            {
                if (child->getNodeType() == NODE_STATEMENT)
                {
                    lastStatement = child;
                }
            }
            // The label a return jumps to: the one already after the
            // call's statement, where the next statement is a label --
            // a loop's continue label, say -- since two labels at one
            // place are one too many; else a fresh one after the block.
            std::string after;
            bool        afterIsOwn = false;
            if (LSLASTNode* next = statement->getNext(); next && next->getNodeType() == NODE_STATEMENT && next->getNodeSubType() == NODE_LABEL &&
                                                          statement->getParent() && statement->getParent()->getNodeSubType() == NODE_COMPOUND_STATEMENT)
            {
                if (LSLSymbol* label = static_cast<LSLLabel*>(next)->getIdentifier()->getSymbol())
                {
                    after = label->getName();
                }
            }
            for (LSLASTNode* r : returns)
            {
                if (r == lastStatement)
                {
                    renames.push_back(Rename{ beginOf(r), endOf(r), std::string() });
                    continue;
                }
                if (after.empty())
                {
                    after      = freshName("_ret", taken, used, context);
                    afterIsOwn = true;
                }
                renames.push_back(Rename{ beginOf(r), endOf(r), "jump " + after + ";" });
            }
            std::sort(renames.begin(), renames.end(), [](const Rename& a, const Rename& b) { return a.begin < b.begin; });

            // The block: an opening brace at the call, the parameters as
            // locals, the body's own lines between its braces, a closing
            // brace, and the label a return jumps to.
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
                    return false;
                }
                const auto        r    = renamed.find(psym);
                const std::string name = r == renamed.end() ? psym->getName() : r->second;
                // The argument as written, with the body's renames not
                // applying to it: it is the caller's text.
                const std::string arg = slice(lines, beginOf(args[i]), endOf(args[i]));
                block.push_back(PieceLine{ Piece{ std::string(type) + " " + name + " = ", beginOf(params[i]), false },
                                           Piece{ arg, beginOf(args[i]), true }, Piece{ ";", beginOf(params[i]), false } });
            }
            for (PieceLine& line : renamedText(lines, inner_begin, inner_end, renames))
            {
                block.push_back(std::move(line));
            }
            PieceLine closing{ Piece{ "}", endOf(statement), false } };
            if (afterIsOwn)
            {
                closing.push_back(Piece{ "@" + after + ";", endOf(statement), false });
            }
            block.push_back(std::move(closing));
            out.edit     = Edit{ beginOf(statement), endOf(statement), std::move(block) };
            out.note     = noteAt(statement, std::string("put the function ") + sym->getName() + (last ? " in place of its one call" : " in place of a call"));
            out.callLine = beginOf(statement).line;
            return true;
        }

        if (!returnsNothing && !bare)
        {
            // The expression in place of the call: the body must be one
            // return of an expression that changes nothing, over
            // arguments that are constants, names, or expressions that
            // change nothing themselves -- a pure library call, say.
            LSLASTNode* only = body->getChild(0);
            if (!only || only->getNext() || only->getNodeSubType() != NODE_RETURN_STATEMENT)
            {
                return false;
            }
            LSLExpression* expr = static_cast<LSLReturnStatement*>(only)->getExpr();
            if (!expr)
            {
                return false;
            }
            // Several calls: only where marked, or small enough that the
            // expression costs about what the call did.
            if (!last && !is_marked && nodesOf(expr).size() > 8)
            {
                return false;
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
            // As written, for a temporary to be set to.
            std::vector<std::string> argRaw(args.size());
            std::vector<size_t>      wantTemp;
            for (size_t i = 0; i < args.size() && simple; ++i)
            {
                const LSLNodeSubType kind     = args[i]->getNodeSubType();
                const bool           constant = kind == NODE_CONSTANT_EXPRESSION;
                const bool           name     = kind == NODE_LVALUE_EXPRESSION;
                // Anything else must change nothing, since it may be read
                // at another time than the call would have, or not at all.
                if (!constant && !name && !ALLSLTraits::sideEffectFree(args[i]))
                {
                    simple = false;
                    break;
                }
                argRaw[i]  = slice(lines, beginOf(args[i]), endOf(args[i]));
                argText[i] = argRaw[i];
                if (!constant && (!name || static_cast<LSLLValueExpression*>(args[i])->getMember()))
                {
                    argText[i] = "(" + argText[i] + ")";
                }
                if (!constant && uses[params[i]->getSymbol()] > 1)
                {
                    wantTemp.push_back(i);
                }
            }
            if (!simple)
            {
                return false;
            }
            // A name the expression would read more than once is read once
            // into a temporary before the call's statement, where the
            // statement is one a declaration can stand before -- in a
            // block: an expression, a declaration, a return, an `if` with
            // the call in its condition, or a `for` with the call in its
            // first part, the parts evaluated first -- and nothing else
            // evaluated before the call would have read the name changes
            // anything: no other call, and no assignment but the
            // statement's own at its root. A `while` condition is read
            // every time round, so a temporary above it would go stale.
            std::optional<Edit> before;
            if (!wantTemp.empty())
            {
                LSLASTNode* holder = call->getParent();
                while (holder && holder->getNodeType() != NODE_STATEMENT)
                {
                    holder = holder->getParent();
                }
                if (!holder || !holder->getParent() || holder->getParent()->getNodeSubType() != NODE_COMPOUND_STATEMENT)
                {
                    return false;
                }
                const LSLNodeSubType shape = holder->getNodeSubType();
                // What is evaluated up to the call, and the expression
                // whose own assignment at the root is allowed.
                LSLASTNode* scanned = holder;
                LSLASTNode* root    = nullptr;
                switch (shape)
                {
                    case NODE_EXPRESSION_STATEMENT: root = static_cast<LSLExpressionStatement*>(holder)->getExpr(); break;
                    case NODE_DECLARATION:
                    case NODE_RETURN_STATEMENT: break;
                    case NODE_IF_STATEMENT: scanned = static_cast<LSLIfStatement*>(holder)->getCheckExpr(); break;
                    case NODE_FOR_STATEMENT:
                        // The first part's expression the call is in may
                        // assign at its root, as a statement's may; the
                        // ones after it run after the call and are not
                        // looked at.
                        scanned = static_cast<LSLForStatement*>(holder)->getInitExprs();
                        for (LSLASTNode* init = scanned ? scanned->getChild(0) : nullptr; init; init = init->getNext())
                        {
                            if (isInside(call, init))
                            {
                                root = init;
                                break;
                            }
                        }
                        break;
                    default: return false;
                }
                if (!scanned || !isInside(call, scanned))
                {
                    return false;
                }
                std::vector<LSLASTNode*> evaluated;
                if (shape == NODE_FOR_STATEMENT)
                {
                    for (LSLASTNode* init = scanned->getChild(0); init; init = init->getNext())
                    {
                        for (LSLASTNode* n : nodesOf(init))
                        {
                            evaluated.push_back(n);
                        }
                        if (init == root)
                        {
                            break;
                        }
                    }
                }
                else
                {
                    evaluated = nodesOf(scanned);
                }
                for (LSLASTNode* n : evaluated)
                {
                    if (n->getNodeType() != NODE_EXPRESSION || isInside(n, call))
                    {
                        // The call's own arguments are what is being moved.
                        continue;
                    }
                    if (n->getNodeSubType() == NODE_FUNCTION_EXPRESSION && n != call &&
                        static_cast<LSLFunctionExpression*>(n)->getIdentifier()->getSymbol() != sym)
                    {
                        // Another call of this same function changes nothing
                        // either; any other call is its own business.
                        return false;
                    }
                    if (n != root && operation_mutates(static_cast<LSLExpression*>(n)->getOperation()))
                    {
                        return false;
                    }
                }
                const Names visible = visibleFrom(holder);
                Names       taken   = visible;
                Block       decls;
                for (const size_t i : wantTemp)
                {
                    LSLSymbol*  psym = params[i]->getSymbol();
                    const char* type = psym && psym->getType() ? typeWord(psym->getType()->getIType()) : nullptr;
                    if (!type)
                    {
                        return false;
                    }
                    const std::string temp = freshName("_t", taken, used, context);
                    decls.push_back(PieceLine{ Piece{ std::string(type) + " " + temp + " = ", beginOf(args[i]), false },
                                               Piece{ argRaw[i], beginOf(args[i]), true }, Piece{ ";", beginOf(args[i]), false } });
                    argText[i] = temp;
                }
                // The statement's own indentation before it, on the line
                // the temporaries leave it on.
                const Pos          at     = beginOf(holder);
                const std::string& line   = lines[static_cast<size_t>(at.line)];
                const size_t       indent = line.find_first_not_of(" \t");
                decls.push_back(PieceLine{ Piece{ line.substr(0, indent == std::string::npos ? 0 : std::min(indent, static_cast<size_t>(at.column))), at, false } });
                before = Edit{ at, at, std::move(decls) };
            }
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
            out.edit     = Edit{ beginOf(call), endOf(call), std::move(block) };
            out.before   = std::move(before);
            out.note     = noteAt(call, std::string("put what the function ") + sym->getName() + (last ? " returns in place of its one call" : " returns in place of a call"));
            out.callLine = beginOf(call).line;
            return true;
        }
        return false;
    }

    // One round: every call that can go in place and does not cross
    // another's edit goes, and a function goes with the last of its
    // calls. How many went.
    S32 inlineRound(std::string& text, ALSourceMap& map, ALScriptProblems& notes, const std::vector<std::string>& marked)
    {
        ScopedScriptParser parser(nullptr);
        LSLScript*         script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script || parser.logger.getErrors())
        {
            return 0;
        }
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        if (parser.logger.getErrors())
        {
            return 0;
        }
        const Lines lines = splitLines(text);

        // The functions, and every call of each; and what each function
        // calls, since one that reaches itself -- through others or not --
        // can never go in place: each copy would carry another call of it.
        std::vector<LSLGlobalFunction*>                           functions;
        std::map<LSLSymbol*, std::vector<LSLFunctionExpression*>> calls;
        std::map<LSLSymbol*, std::set<LSLSymbol*>>                callees;
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
                    LSLASTNode* holder = call->getParent();
                    while (holder && holder->getNodeType() != NODE_GLOBAL_FUNCTION)
                    {
                        holder = holder->getParent();
                    }
                    if (holder && static_cast<LSLGlobalFunction*>(holder)->getSymbol())
                    {
                        callees[static_cast<LSLGlobalFunction*>(holder)->getSymbol()].insert(sym);
                    }
                }
            }
        }
        auto recursive = [&](LSLSymbol* sym) {
            std::set<LSLSymbol*>    seen;
            std::vector<LSLSymbol*> stack(callees[sym].begin(), callees[sym].end());
            while (!stack.empty())
            {
                LSLSymbol* at = stack.back();
                stack.pop_back();
                if (at == sym)
                {
                    return true;
                }
                if (seen.insert(at).second)
                {
                    for (LSLSymbol* next : callees[at])
                    {
                        stack.push_back(next);
                    }
                }
            }
            return false;
        };
        // What is taken this round: the stretches edited and the lines
        // dropped, which no other edit may cross.
        std::vector<std::pair<Pos, Pos>> edited;
        std::set<S32>                    dropped;
        Names                            used;
        Weave                            weave(lines);
        std::vector<ALScriptProblem>     said;
        S32                              went = 0;
        auto                             clashes = [&](Pos b, Pos e) {
            for (S32 line = b.line; line <= e.line; ++line)
            {
                if (dropped.count(line))
                {
                    return true;
                }
            }
            for (const auto& [ob, oe] : edited)
            {
                if (!(e < ob || oe < b))
                {
                    return true;
                }
            }
            return false;
        };
        for (LSLGlobalFunction* function : functions)
        {
            LSLSymbol* sym = function->getSymbol();
            if (!sym)
            {
                continue;
            }
            const auto found = calls.find(sym);
            if (found == calls.end() || found->second.empty() || recursive(sym))
            {
                continue;
            }
            // The definition must have its lines to itself, since they go.
            const Pos fbegin = beginOf(function);
            const Pos fend   = endOf(function);
            {
                const std::string& first  = lines[static_cast<size_t>(fbegin.line)];
                const std::string& last   = lines[static_cast<size_t>(fend.line)];
                const bool         before = first.find_first_not_of(" \t", 0) < static_cast<size_t>(fbegin.column);
                const bool         after  = last.find_first_not_of(" \t", static_cast<size_t>(fend.column)) != std::string::npos;
                if (before || after || clashes(fbegin, fend))
                {
                    continue;
                }
            }
            const bool   is_marked = std::find(marked.begin(), marked.end(), sym->getName()) != marked.end();
            const size_t count     = found->second.size();
            // Every call of it planned; the function goes when all of them
            // do. A plan that crosses another's edit waits for the next
            // round, and so does the function.
            std::vector<Plan> plans;
            bool              all = true;
            for (LSLFunctionExpression* call : found->second)
            {
                Plan one;
                if (plan(lines, parser.context, function, call, count == 1, is_marked, used, one) && !clashes(one.edit.begin, one.edit.end) &&
                    !(one.edit.begin.line >= fbegin.line && one.edit.end.line <= fend.line) &&
                    !(one.before && clashes(one.before->begin, one.before->end)))
                {
                    if (one.before)
                    {
                        edited.emplace_back(one.before->begin, one.before->end);
                    }
                    edited.emplace_back(one.edit.begin, one.edit.end);
                    plans.push_back(std::move(one));
                }
                else
                {
                    all = false;
                }
            }
            if (plans.empty())
            {
                continue;
            }
            for (Plan& one : plans)
            {
                said.push_back(std::move(one.note));
                if (one.before)
                {
                    weave.edits.push_back(std::move(*one.before));
                }
                weave.edits.push_back(std::move(one.edit));
                ++went;
            }
            if (all)
            {
                for (S32 line = fbegin.line; line <= fend.line; ++line)
                {
                    dropped.insert(line);
                    weave.droppedLines.insert(line);
                }
            }
        }
        if (weave.edits.empty())
        {
            return 0;
        }
        for (ALScriptProblem& note : said)
        {
            notes.push_back(std::move(note));
        }
        weave.run(text, map);
        return went;
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
    for (int round = 0; round < 64; ++round)
    {
        ALSourceMap      step;
        ALScriptProblems said;
        const S32        went = inlineRound(result.text, step, said, marked);
        if (went == 0)
        {
            break;
        }
        // The notes are about this round's text; the source's places are
        // what the map so far says of them.
        for (ALScriptProblem& note : said)
        {
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
        }
        result.map = step.composed(result.map);
        result.inlined += went;
    }
    return result;
}

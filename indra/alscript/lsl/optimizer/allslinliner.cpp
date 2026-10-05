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

#include "alscriptengine.h"

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "allsleffects.h"
#include "allslservice.h"
#include "allsltraits.h"
#include "alscriptlexicon.h"

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
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> Names;

    Names visibleFrom(LSLASTNode* node)
    {
        Names names;
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

    // Whether every variable a stretch of the function reads that the
    // function does not declare -- a global -- is the same variable where
    // the call is. A local of the caller's may have the global's name, and
    // the text put in place would then read or write the local: a body is
    // put in place only where each such name means there what it means in
    // the function.
    bool meansTheSameAt(LSLASTNode* stretch, const std::set<LSLSymbol*>& own, LSLASTNode* at)
    {
        for (LSLASTNode* n : nodesOf(stretch))
        {
            if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_LVALUE_EXPRESSION)
            {
                continue;
            }
            LSLIdentifier* id  = static_cast<LSLLValueExpression*>(n)->getIdentifier();
            LSLSymbol*     sym = id ? id->getSymbol() : nullptr;
            if (!sym || own.count(sym) || sym->getSubType() == SYM_BUILTIN)
            {
                continue;
            }
            // The first scope up from the call that has the name has the
            // variable the text there would mean.
            LSLSymbol* there = nullptr;
            for (LSLASTNode* up = at; up && !there; up = up->getParent())
            {
                if (LSLSymbolTable* table = up->getSymbolTable())
                {
                    there = table->lookup(sym->getName(), SYM_VARIABLE);
                }
            }
            if (there != sym)
            {
                return false;
            }
        }
        return true;
    }

    bool isBuiltinOrKeyword(const std::string& name, ScriptContext& context)
    {
        if (ALScriptLexicon::lslWord(name) & (ALScriptLexicon::LSL_KEYWORD | ALScriptLexicon::LSL_CONSTANT))
        {
            return true;
        }
        return context.builtins && context.builtins->lookup(name.c_str(), SYM_ANY);
    }

    ALScriptProblem noteAt(LSLASTNode* at, const char* key, std::string_view text, std::vector<std::string> args)
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
        p.message   = ALScriptProblem::fill(text, args);
        p.key       = key;
        p.args      = std::move(args);
        return p;
    }

    // What one call's inlining comes to: the edit at the call, whether
    // the function goes with it, and the note.
    struct Plan
    {
        Edit                edit;
        // What goes before the call's statement: temporaries for the
        // arguments, or the body setting the call's value.
        std::optional<Edit> before;
        // The brace that closes those put around a statement that stood
        // alone as a branch or a loop's body.
        std::optional<Edit> after;
        // Anything else the plan changes: a do loop's body, opened and
        // closed round the block that ends it.
        std::vector<Edit>   more;
        ALScriptProblem     note;
        S32                 callLine = 0;
    };

    // The fresh names a round has given out, so that two blocks in one
    // function do not share a label.

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

    // Text added to the end of a block's last line, a new line started, a
    // block's lines joined on: what a block is built of.
    void addText(Block& block, const std::string& text, Pos at)
    {
        if (block.empty())
        {
            block.emplace_back();
        }
        block.back().push_back(Piece{ text, at, false });
    }
    void addLine(Block& block) { block.emplace_back(); }
    void addBlock(Block& block, Block more)
    {
        if (more.empty())
        {
            return;
        }
        if (block.empty())
        {
            block = std::move(more);
            return;
        }
        PieceLine& last = block.back();
        last.insert(last.end(), more.front().begin(), more.front().end());
        block.insert(block.end(), std::make_move_iterator(more.begin() + 1), std::make_move_iterator(more.end()));
    }

    // What becomes of a body's returns where it is put in place.
    enum class Returns
    {
        // A function returning nothing: a return is the end of the block.
        Void,
        // A value nobody takes: a return keeps its expression only where
        // that does something.
        Discard,
        // A value set to a variable: a return is `target = e;`.
        Into,
        // A value returned on from the function the call was returned
        // from: the returns stay as they are.
        Keep
    };

    // The statement a node stands in.
    LSLASTNode* statementOf(LSLASTNode* node)
    {
        while (node && node->getNodeType() != NODE_STATEMENT)
        {
            node = node->getParent();
        }
        return node;
    }

    // Where a statement ends, its `;` with it: a declaration's place ends
    // before the `;`, which a block put in its place would leave behind.
    Pos endWithSemicolon(const Lines& lines, LSLASTNode* statement)
    {
        Pos                e    = endOf(statement);
        const std::string& line = lines[static_cast<size_t>(e.line)];
        if (e.column > 0 && static_cast<size_t>(e.column) < line.size() && line[static_cast<size_t>(e.column)] == ';' &&
            line[static_cast<size_t>(e.column - 1)] != ';')
        {
            ++e.column;
        }
        return e;
    }

    // Every name in the function or the event a node is in. A label is one
    // to a function -- a jump goes to the last label of its name in it --
    // and a local declared before a block is in scope for all that
    // follows it, a later block's own declarations included, so a fresh
    // name must be none of these, whatever block they stand in.
    Names namesAround(LSLASTNode* node)
    {
        LSLASTNode* callable = node;
        while (callable && callable->getNodeType() != NODE_GLOBAL_FUNCTION && callable->getNodeType() != NODE_EVENT_HANDLER)
        {
            callable = callable->getParent();
        }
        Names names;
        if (callable)
        {
            for (LSLASTNode* n : nodesOf(callable))
            {
                if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                {
                    names.insert(static_cast<LSLIdentifier*>(n)->getName());
                }
            }
        }
        return names;
    }

    // Whether any of the expressions reads or writes a variable.
    bool mentions(const std::vector<LSLExpression*>& exprs, LSLSymbol* variable)
    {
        for (LSLExpression* e : exprs)
        {
            for (LSLASTNode* n : nodesOf(e))
            {
                if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION &&
                    static_cast<LSLLValueExpression*>(n)->getSymbol() == variable)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // The function's body as a block, standing where `where` stands or
    // just before it: an opening brace; each parameter a local of the
    // block set to its argument, in order -- a call's own order: LSL takes
    // an operator's operands right to left, but a call's arguments left to
    // right, in LSO and Mono alike, so an argument that changes something
    // changes it when the call would have -- then the body's statements,
    // and a closing brace. A name the body declares that is visible at
    // `where`, or is `target`, gets a fresh one, since LSL has no
    // shadowing; so does each of its labels, a label being one to a
    // function. A return is as `returns` asks; one that is not the body's
    // last jumps to a label that ends the block -- the one already after
    // `where`, where the block takes its place and the next statement is a
    // label, a loop's continue label say, since two labels at one place
    // are one too many. False where the body cannot go: it changes state,
    // or reads a global that a local at `where` hides.
    bool blockOf(const Lines& lines, ScriptContext& context, LSLGlobalFunction* function, const std::vector<LSLIdentifier*>& params,
                 const std::vector<LSLExpression*>& args, LSLASTNode* where, bool inPlace, Returns returns, const std::string& target, Names& used,
                 Block& block)
    {
        LSLStatement*                  body      = function->getStatements();
        const std::vector<LSLASTNode*> bodyNodes = nodesOf(body);
        std::vector<LSLASTNode*>       found;
        std::set<LSLSymbol*>           labels;
        for (LSLASTNode* n : bodyNodes)
        {
            if (n->getNodeType() != NODE_STATEMENT)
            {
                continue;
            }
            switch (n->getNodeSubType())
            {
                case NODE_STATE_STATEMENT: return false;
                case NODE_RETURN_STATEMENT: found.push_back(n); break;
                case NODE_LABEL: labels.insert(static_cast<LSLLabel*>(n)->getIdentifier()->getSymbol()); break;
                default: break;
            }
        }
        // The names the body declares, and what each is called in the
        // block: its own name unless that is visible where the block goes.
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
        // What the body reads of the script's own must be what the same
        // names are where the block goes.
        if (!meansTheSameAt(body, declared, where))
        {
            return false;
        }
        const Names visible = visibleFrom(where);
        Names       taken   = visible;
        for (const std::string& name : namesAround(where))
        {
            taken.insert(name);
        }
        if (!target.empty())
        {
            taken.insert(target);
        }
        for (LSLSymbol* d : declared)
        {
            if (d)
            {
                taken.insert(d->getName());
            }
        }
        for (LSLSymbol* d : declared)
        {
            if (d && (visible.count(d->getName()) || d->getName() == target))
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
        // block anyway; the rest are jumps to a label at its end.
        LSLASTNode* lastStatement = nullptr;
        for (LSLASTNode* child = body->getChild(0); child; child = child->getNext())
        {
            if (child->getNodeType() == NODE_STATEMENT)
            {
                lastStatement = child;
            }
        }
        // The label a return jumps to. Inside the block, not after it: the
        // block may be the whole body of a loop or an if written without
        // braces, where a label after it would stand after the loop, or
        // between the if and its else.
        std::string after;
        bool        afterIsOwn = false;
        if (LSLASTNode* next = where->getNext(); inPlace && next && next->getNodeType() == NODE_STATEMENT && next->getNodeSubType() == NODE_LABEL &&
                                                 where->getParent() && where->getParent()->getNodeSubType() == NODE_COMPOUND_STATEMENT)
        {
            if (LSLSymbol* label = static_cast<LSLLabel*>(next)->getIdentifier()->getSymbol())
            {
                after = label->getName();
            }
        }
        const auto jumpTo = [&]() {
            if (after.empty())
            {
                after      = freshName("_ret", taken, used, context);
                afterIsOwn = true;
            }
            return after;
        };
        for (LSLASTNode* r : found)
        {
            if (returns == Returns::Keep)
            {
                continue;
            }
            const bool     lastOne = r == lastStatement;
            LSLExpression* value   = static_cast<LSLReturnStatement*>(r)->getExpr();
            // A value nobody takes goes where taking it changes nothing.
            if (!value || returns == Returns::Void || (returns == Returns::Discard && ALLSLTraits::changesNothing(value)))
            {
                renames.push_back(Rename{ beginOf(r), endOf(r), lastOne ? std::string() : "jump " + jumpTo() + ";" });
                continue;
            }
            // `return e;` as `target = e;`, or `e;`: the word and the `;`
            // replaced, the expression between them the body's own, its
            // names renamed with the rest.
            const Pos rb = beginOf(r);
            const Pos re = endOf(r);
            if (lines[static_cast<size_t>(rb.line)].compare(static_cast<size_t>(rb.column), 6, "return") != 0 || re.column < 1 ||
                lines[static_cast<size_t>(re.line)][static_cast<size_t>(re.column - 1)] != ';')
            {
                return false;
            }
            const std::string head = returns == Returns::Into ? target + " =" : std::string();
            if (lastOne)
            {
                renames.push_back(Rename{ rb, Pos{ rb.line, rb.column + 6 }, head });
            }
            else
            {
                renames.push_back(Rename{ rb, Pos{ rb.line, rb.column + 6 }, "{ " + head });
                renames.push_back(Rename{ Pos{ re.line, re.column - 1 }, re, "; jump " + jumpTo() + "; }" });
            }
        }
        std::sort(renames.begin(), renames.end(), [](const Rename& a, const Rename& b) { return a.begin < b.begin; });

        const Pos bbegin = beginOf(body);
        const Pos bend   = endOf(body);
        const Pos inner_begin{ bbegin.line, bbegin.column + 1 };
        const Pos inner_end{ bend.line, std::max(0, bend.column - 1) };
        block.push_back(PieceLine{ Piece{ "{", beginOf(where), false } });
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
            block.push_back(PieceLine{ Piece{ std::string(type) + " " + name + " = ", beginOf(params[i]), false }, Piece{ arg, beginOf(args[i]), true },
                                       Piece{ ";", beginOf(params[i]), false } });
        }
        for (PieceLine& line : renamedText(lines, inner_begin, inner_end, renames))
        {
            block.push_back(std::move(line));
        }
        if (afterIsOwn)
        {
            block.push_back(PieceLine{ Piece{ "@" + after + ";", endOf(where), false } });
        }
        block.push_back(PieceLine{ Piece{ "}", endOf(where), false } });
        return true;
    }

    // The expression in place of the call: the body must be one return of
    // an expression that changes nothing, over arguments that are
    // constants, names, or expressions that change nothing themselves -- a
    // pure library call, say.
    bool expressionOf(const Lines& lines, ScriptContext& context, LSLGlobalFunction* function, LSLFunctionExpression* call, LSLSymbol* sym,
                      const std::vector<LSLIdentifier*>& params, const std::vector<LSLExpression*>& args, bool last, bool is_marked, Names& used,
                      Plan& out)
    {
        LSLStatement* body = function->getStatements();
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
        // What the expression reads that is not a parameter must be
        // what the same name is where the call is.
        std::set<LSLSymbol*> own;
        for (LSLIdentifier* p : params)
        {
            own.insert(p->getSymbol());
        }
        if (!meansTheSameAt(expr, own, call))
        {
            return false;
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
            Names taken = visibleFrom(holder);
            for (const std::string& name : namesAround(holder))
            {
                taken.insert(name);
            }
            Block decls;
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
        out.note     = noteAt(call, last ? "InlinerPutExpressionOnce" : "InlinerPutExpression",
                              last ? "put what the function [1] returns in place of its one call" : "put what the function [1] returns in place of a call", { sym->getName() });
        out.callLine = beginOf(call).line;
        return true;
    }

    // A value put where the call is from a block before its statement,
    // the call's value set by the body: into the variable the statement
    // declares or sets with it, where that is all the statement does; into
    // the function's own return, where the statement returns it from a
    // function of the same type; else into a fresh local declared above
    // the block, the call becoming its name. The statement may be one that
    // runs once where it stands -- an expression, a declaration, a return,
    // an if's condition, a for's first part -- and the call may be run
    // before the rest of it only where nothing that ran before it there
    // would see a difference (ALLSLEffects). A statement standing alone as
    // a branch or a loop's body gets braces, for the block to stand in.
    bool hoisted(const Lines& lines, ScriptContext& context, const ALLSLEffects& effects, LSLGlobalFunction* function, LSLFunctionExpression* call,
                 LSLSymbol* sym, const std::vector<LSLIdentifier*>& params, const std::vector<LSLExpression*>& args, Names& used, Plan& out)
    {
        LSLASTNode* holder = statementOf(call);
        if (!holder || !holder->getParent())
        {
            return false;
        }
        const LSLNodeSubType shape = holder->getNodeSubType();
        switch (shape)
        {
            case NODE_EXPRESSION_STATEMENT:
            case NODE_DECLARATION:
            case NODE_RETURN_STATEMENT:
                break;
            case NODE_IF_STATEMENT:
                if (!isInside(call, static_cast<LSLIfStatement*>(holder)->getCheckExpr()))
                {
                    return false;
                }
                break;
            case NODE_FOR_STATEMENT:
                if (!isInside(call, static_cast<LSLForStatement*>(holder)->getInitExprs()))
                {
                    return false;
                }
                break;
            case NODE_DO_STATEMENT:
                // Read every time round, after the body: the block goes at
                // the body's end.
                if (!isInside(call, static_cast<LSLDoStatement*>(holder)->getCheckExpr()))
                {
                    return false;
                }
                break;
            default:
                // A while's or a for's condition is read every time round
                // before the body: the loop is written as an if first.
                return false;
        }
        LSLASTNode* parent  = holder->getParent();
        const bool  inBlock = parent->getNodeType() == NODE_STATEMENT && parent->getNodeSubType() == NODE_COMPOUND_STATEMENT;
        if (!inBlock && (shape == NODE_DECLARATION || parent->getNodeType() != NODE_STATEMENT))
        {
            return false;
        }
        if (!effects.mayRunFirst(holder, call))
        {
            return false;
        }
        const LSLIType type     = sym->getType() ? sym->getType()->getIType() : LST_NULL;
        const char*    typeName = typeWord(type);
        if (!typeName)
        {
            return false;
        }
        // The call as the statement's whole value, in parentheses or not.
        LSLASTNode* whole = call;
        while (whole->getParent() && whole->getParent()->getNodeType() == NODE_EXPRESSION && whole->getParent()->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            whole = whole->getParent();
        }
        Returns     returns  = Returns::Into;
        std::string target;
        std::string declare;
        bool        replaces = false;
        if (inBlock && shape == NODE_DECLARATION && static_cast<LSLDeclaration*>(holder)->getInitializer() == whole)
        {
            LSLSymbol* declared = static_cast<LSLDeclaration*>(holder)->getSymbol();
            if (declared && declared->getType() && declared->getType()->getIType() == type && !mentions(args, declared))
            {
                target   = declared->getName();
                declare  = std::string(typeName) + " " + target + ";";
                replaces = true;
            }
        }
        else if (shape == NODE_EXPRESSION_STATEMENT)
        {
            auto* expr = static_cast<LSLExpressionStatement*>(holder)->getExpr();
            if (expr && expr->getNodeSubType() == NODE_BINARY_EXPRESSION && expr->getOperation() == '=' && expr->getChild(1) == whole &&
                expr->getChild(0)->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(expr->getChild(0))->getMember())
            {
                LSLSymbol* set = static_cast<LSLLValueExpression*>(expr->getChild(0))->getSymbol();
                if (set && set->getType() && set->getType()->getIType() == type)
                {
                    target   = set->getName();
                    replaces = true;
                }
            }
        }
        else if (shape == NODE_RETURN_STATEMENT && static_cast<LSLReturnStatement*>(holder)->getExpr() == whole)
        {
            LSLASTNode* up = holder;
            while (up && up->getNodeType() != NODE_GLOBAL_FUNCTION && up->getNodeType() != NODE_EVENT_HANDLER)
            {
                up = up->getParent();
            }
            LSLSymbol* returning = up && up->getNodeType() == NODE_GLOBAL_FUNCTION ? static_cast<LSLGlobalFunction*>(up)->getSymbol() : nullptr;
            if (returning && returning->getType() && returning->getType()->getIType() == type)
            {
                returns  = Returns::Keep;
                replaces = true;
            }
        }
        if (!replaces)
        {
            Names taken = visibleFrom(holder);
            for (const std::string& name : namesAround(holder))
            {
                taken.insert(name);
            }
            target = freshName("_r", taken, used, context);
        }
        Block block;
        if (!blockOf(lines, context, function, params, args, holder, replaces, returns, target, used, block))
        {
            return false;
        }
        if (replaces)
        {
            if (!declare.empty())
            {
                block.insert(block.begin(), PieceLine{ Piece{ declare, beginOf(holder), false } });
            }
            out.edit = Edit{ beginOf(holder), endWithSemicolon(lines, holder), std::move(block) };
            return true;
        }
        // The statement's own indentation before it, on the line what goes
        // before it leaves it on.
        const Pos          at     = beginOf(holder);
        const std::string& line   = lines[static_cast<size_t>(at.line)];
        const size_t       indent = line.find_first_not_of(" \t");
        const PieceLine    indented{ Piece{ line.substr(0, indent == std::string::npos ? 0 : std::min(indent, static_cast<size_t>(at.column))), at, false } };
        const std::string  local    = std::string(typeName) + " " + target + ";";
        if (shape == NODE_DO_STATEMENT)
        {
            // The value's local before the loop; the block at the end of
            // each time round, after the loop's own body -- in braces with
            // it, so that the body's names are not the block's -- and
            // before the condition is read.
            LSLStatement* loopBody = static_cast<LSLDoStatement*>(holder)->getBody();
            Block         opening;
            if (!inBlock)
            {
                opening.push_back(PieceLine{ Piece{ "{", at, false } });
            }
            opening.push_back(PieceLine{ Piece{ local, beginOf(call), false } });
            opening.push_back(indented);
            out.before = Edit{ at, at, std::move(opening) };
            out.more.push_back(Edit{ beginOf(loopBody), beginOf(loopBody), Block{ PieceLine{ Piece{ "{ ", beginOf(loopBody), false } } } });
            block.insert(block.begin(), PieceLine());
            block.push_back(PieceLine{ Piece{ "}", endOf(loopBody), false } });
            out.more.push_back(Edit{ endOf(loopBody), endOf(loopBody), std::move(block) });
            out.edit = Edit{ beginOf(call), endOf(call), Block{ PieceLine{ Piece{ target, beginOf(call), false } } } };
            if (!inBlock)
            {
                const Pos end = endWithSemicolon(lines, holder);
                out.after     = Edit{ end, end, Block{ PieceLine{ Piece{ " }", end, false } } } };
            }
            return true;
        }
        block.insert(block.begin(), PieceLine{ Piece{ local, beginOf(call), false } });
        if (!inBlock)
        {
            block.insert(block.begin(), PieceLine{ Piece{ "{", beginOf(holder), false } });
        }
        block.push_back(indented);
        out.before = Edit{ at, at, std::move(block) };
        out.edit   = Edit{ beginOf(call), endOf(call), Block{ PieceLine{ Piece{ target, beginOf(call), false } } } };
        if (!inBlock)
        {
            out.after = Edit{ endOf(holder), endOf(holder), Block{ PieceLine{ Piece{ " }", endOf(holder), false } } } };
        }
        return true;
    }

    // The plan for one call of a function, or none where the shape is
    // not one that can go in place. A call the function is the statement
    // of goes as a block in its place; a value, as its expression where
    // the body is one, else from a block before its statement. Only a
    // function called once, or marked, goes as a block.
    bool plan(const Lines& lines, ScriptContext& context, const ALLSLEffects& effects, LSLGlobalFunction* function, LSLFunctionExpression* call, bool last,
              bool is_marked, Names& used, Plan& out)
    {
        LSLSymbol*    sym  = function->getSymbol();
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
        const bool  eligible  = last || is_marked;
        const auto  noted     = [&](LSLASTNode* at) {
            out.note     = noteAt(at, last ? "InlinerPutFunctionOnce" : "InlinerPutFunction",
                                  last ? "put the function [1] in place of its one call" : "put the function [1] in place of a call", { sym->getName() });
            out.callLine = beginOf(at).line;
        };
        if (bare)
        {
            if (!eligible)
            {
                return false;
            }
            Block block;
            if (!blockOf(lines, context, function, params, args, statement, true, returnsNothing ? Returns::Void : Returns::Discard, std::string(), used,
                         block))
            {
                return false;
            }
            out.edit = Edit{ beginOf(statement), endOf(statement), std::move(block) };
            noted(statement);
            return true;
        }
        if (returnsNothing)
        {
            return false;
        }
        if (expressionOf(lines, context, function, call, sym, params, args, last, is_marked, used, out))
        {
            return true;
        }
        if (!eligible || !hoisted(lines, context, effects, function, call, sym, params, args, used, out))
        {
            return false;
        }
        noted(call);
        return true;
    }

    // One round: every call that can go in place and does not cross
    // another's edit goes, and a function goes with the last of its
    // calls. How many went.
    // How many nodes a script is: what one walk of it visits.
    struct CountNodes : public ASTVisitor
    {
        size_t count = 0;
        bool   visit(LSLASTNode*) override
        {
            ++count;
            return true;
        }
    };

    // A round's walks of the script: the parse, the symbols, the types, the
    // references, and the inliner's own look through it.
    constexpr size_t WALKS_A_ROUND = 5;

    S32 inlineRound(std::string& text, ALSourceMap& map, ALScriptProblems& notes, const ALLSLInliner::Asked& asked, size_t& walked)
    {
        ScopedScriptParser parser(nullptr);
        LSLScript*         script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script || parser.logger.getErrors())
        {
            return 0;
        }
        CountNodes nodes;
        script->visit(&nodes);
        walked = nodes.count * WALKS_A_ROUND;
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        if (parser.logger.getErrors())
        {
            return 0;
        }
        // What is constant, for which calls of a const function go.
        if (!asked.constant.empty())
        {
            script->propagateValues();
        }
        const Lines        lines = splitLines(text);
        const ALLSLEffects effects(script);

        // The functions, and every call of each; and what each function
        // calls, since one that reaches itself -- through others or not --
        // can never go in place: each copy would carry another call of it.
        std::vector<LSLGlobalFunction*>                                            functions;
        boost::unordered_flat_map<LSLSymbol*, std::vector<LSLFunctionExpression*>> calls;
        boost::unordered_flat_map<LSLSymbol*, boost::unordered_flat_set<LSLSymbol*>> callees;
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
        // Whether a function reaches itself through calls, answered once
        // per function and kept: the graph is the same all round, and
        // walking it afresh for each was the round's own cost. Reading
        // it never writes it, so a function with no callees is not made
        // an entry of.
        boost::unordered_flat_map<LSLSymbol*, bool> reaches;
        const auto                                  callsOf = [&](LSLSymbol* sym) -> const boost::unordered_flat_set<LSLSymbol*>* {
            const auto found = callees.find(sym);
            return found == callees.end() ? nullptr : &found->second;
        };
        auto recursive = [&](LSLSymbol* sym) {
            if (const auto known = reaches.find(sym); known != reaches.end())
            {
                return known->second;
            }
            bool                                 itself = false;
            boost::unordered_flat_set<LSLSymbol*> seen;
            std::vector<LSLSymbol*>              stack;
            if (const auto* first = callsOf(sym))
            {
                stack.assign(first->begin(), first->end());
            }
            while (!stack.empty())
            {
                LSLSymbol* at = stack.back();
                stack.pop_back();
                if (at == sym)
                {
                    itself = true;
                    break;
                }
                if (seen.insert(at).second)
                {
                    if (const auto* next = callsOf(at))
                    {
                        stack.insert(stack.end(), next->begin(), next->end());
                    }
                }
            }
            reaches[sym] = itself;
            return itself;
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
        // A while or a for whose condition, or whose step, calls a function
        // that goes as a block is written first as the label, the if and
        // the jump back it compiles to -- `@top; if (c) { body jump top; }`
        // -- so that the call stands in an if's condition, or as a
        // statement, and goes in place from there in the rounds after. A
        // call its function's expression can take goes as it stands, and
        // its loop is left alone.
        boost::unordered_flat_map<LSLSymbol*, LSLGlobalFunction*> definitions;
        for (LSLGlobalFunction* function : functions)
        {
            if (function->getSymbol())
            {
                definitions[function->getSymbol()] = function;
            }
        }
        for (LSLASTNode* node : nodesOf(script))
        {
            const bool isWhile = node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_WHILE_STATEMENT;
            const bool isFor   = node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_FOR_STATEMENT;
            if (!isWhile && !isFor)
            {
                continue;
            }
            auto*         loop  = static_cast<LSLStatement*>(node);
            LSLASTNode*   check = isFor ? static_cast<LSLASTNode*>(static_cast<LSLForStatement*>(loop)->getCheckExpr())
                                        : static_cast<LSLASTNode*>(static_cast<LSLWhileStatement*>(loop)->getCheckExpr());
            LSLASTNode*   steps = isFor ? static_cast<LSLForStatement*>(loop)->getIncrExprs() : nullptr;
            LSLStatement* body  = isFor ? static_cast<LSLForStatement*>(loop)->getBody() : static_cast<LSLWhileStatement*>(loop)->getBody();
            LSLASTNode*   parent = loop->getParent();
            if (!check || !body || !parent || parent->getNodeType() != NODE_STATEMENT)
            {
                continue;
            }
            bool wanted = false;
            for (LSLASTNode* part : { check, steps })
            {
                for (LSLASTNode* n : part ? nodesOf(part) : std::vector<LSLASTNode*>())
                {
                    if (wanted || n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                    {
                        continue;
                    }
                    auto*      call = static_cast<LSLFunctionExpression*>(n);
                    LSLSymbol* sym  = call->getIdentifier()->getSymbol();
                    const auto def  = sym ? definitions.find(sym) : definitions.end();
                    if (def == definitions.end() || recursive(sym))
                    {
                        continue;
                    }
                    const bool is_marked = std::find(asked.marked.begin(), asked.marked.end(), sym->getName()) != asked.marked.end();
                    const bool once      = calls[sym].size() == 1 && asked.others;
                    if (!is_marked && !once)
                    {
                        continue;
                    }
                    // Whether it goes as it stands: planned on a copy of the
                    // names given out, and not kept.
                    Names scratch = used;
                    Plan  dry;
                    wanted        = !plan(lines, parser.context, effects, def->second, call, once, is_marked, scratch, dry);
                }
            }
            if (!wanted)
            {
                continue;
            }
            const bool  inBlock = parent->getNodeSubType() == NODE_COMPOUND_STATEMENT;
            const Pos   lb      = beginOf(loop);
            const Pos   le      = endOf(loop);
            const Pos   bb      = beginOf(body);
            Names       taken   = visibleFrom(loop);
            for (const std::string& name : namesAround(loop))
            {
                taken.insert(name);
            }
            const std::string  label  = freshName("_loop", taken, used, parser.context);
            const std::string& first  = lines[static_cast<size_t>(lb.line)];
            const size_t       indent = first.find_first_not_of(" \t");
            const std::string  pad    = first.substr(0, indent == std::string::npos ? 0 : std::min(indent, static_cast<size_t>(lb.column)));
            // In front of the body: the for's first parts as statements, the
            // label, and the if over the condition as written.
            Block head;
            if (!inBlock)
            {
                addText(head, "{ ", lb);
            }
            if (isFor)
            {
                if (LSLASTNode* inits = static_cast<LSLForStatement*>(loop)->getInitExprs())
                {
                    for (LSLASTNode* init = inits->getChild(0); init; init = init->getNext())
                    {
                        addBlock(head, renamedText(lines, beginOf(init), endOf(init), {}));
                        addText(head, ";", endOf(init));
                        addLine(head);
                        addText(head, pad, lb);
                    }
                }
            }
            addText(head, "@" + label + ";", lb);
            addLine(head);
            addText(head, pad + "if (", lb);
            addBlock(head, renamedText(lines, beginOf(check), endOf(check), {}));
            addText(head, ") ", endOf(check));
            // After it: the for's steps as statements, and the jump back.
            Block tail;
            addText(tail, "", le);
            if (steps)
            {
                for (LSLASTNode* step = steps->getChild(0); step; step = step->getNext())
                {
                    addLine(tail);
                    addBlock(tail, renamedText(lines, beginOf(step), endOf(step), {}));
                    addText(tail, ";", endOf(step));
                }
            }
            addLine(tail);
            addText(tail, "jump " + label + "; }", le);
            if (!inBlock)
            {
                addText(tail, " }", le);
            }
            Edit header{ lb, bb, std::move(head) };
            Edit opening{ bb, bb, Block{ PieceLine{ Piece{ "{ ", bb, false } } } };
            Edit closing{ le, le, std::move(tail) };
            if (clashes(header.begin, header.end) || clashes(opening.begin, opening.end) || clashes(closing.begin, closing.end))
            {
                continue;
            }
            edited.emplace_back(header.begin, header.end);
            edited.emplace_back(opening.begin, opening.end);
            edited.emplace_back(closing.begin, closing.end);
            weave.edits.push_back(std::move(header));
            weave.edits.push_back(std::move(opening));
            weave.edits.push_back(std::move(closing));
            said.push_back(noteAt(loop, "InlinerLoopAsJumps", "wrote the loop as a label, an if and a jump back, for a call in its condition to go in place", {}));
            ++went;
        }
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
            const bool is_marked = std::find(asked.marked.begin(), asked.marked.end(), sym->getName()) != asked.marked.end();
            const bool is_const  = std::find(asked.constant.begin(), asked.constant.end(), sym->getName()) != asked.constant.end();
            if (!asked.others && !is_marked && !is_const)
            {
                continue;
            }
            const size_t count     = found->second.size();
            // Every call of it planned; the function goes when all of them
            // do. A plan that crosses another's edit waits for the next
            // round, and so does the function.
            std::vector<Plan> plans;
            bool              all = true;
            for (LSLFunctionExpression* call : found->second)
            {
                Plan one;
                const auto moreClash = [&](const Plan& p) {
                    return std::any_of(p.more.begin(), p.more.end(), [&](const Edit& e) { return clashes(e.begin, e.end); });
                };
                // A const function's call goes where it can be worked out:
                // every argument a constant.
                const bool worked = !is_marked && is_const && [call] {
                    LSLASTNodeList<LSLExpression>* list = call->getArguments();
                    return !list || std::all_of(list->begin(), list->end(), [](LSLExpression* a) { return a->getConstantValue() != nullptr; });
                }();
                if (!is_marked && !worked && !asked.others)
                {
                    all = false;
                    continue;
                }
                if (plan(lines, parser.context, effects, function, call, count == 1 && asked.others, is_marked || worked, used, one) &&
                    !clashes(one.edit.begin, one.edit.end) &&
                    !(one.edit.begin.line >= fbegin.line && one.edit.end.line <= fend.line) &&
                    !(one.before && clashes(one.before->begin, one.before->end)) && !(one.after && clashes(one.after->begin, one.after->end)) &&
                    !moreClash(one))
                {
                    for (const Edit& e : one.more)
                    {
                        edited.emplace_back(e.begin, e.end);
                    }
                    if (one.before)
                    {
                        edited.emplace_back(one.before->begin, one.before->end);
                    }
                    if (one.after)
                    {
                        edited.emplace_back(one.after->begin, one.after->end);
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
                if (one.after)
                {
                    weave.edits.push_back(std::move(*one.after));
                }
                for (Edit& e : one.more)
                {
                    weave.edits.push_back(std::move(e));
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
        std::string made;
        ALSourceMap step;
        weave.run(made, step);
        // What the round made must be a script that checks as the one it
        // was made from did; else the round is not kept, and the text is
        // what the last round left.
        {
            ScopedScriptParser check(nullptr);
            LSLScript*         again = check.parseLSLBytes(made.data(), static_cast<int>(made.size()));
            if (again && !check.logger.getErrors())
            {
                again->collectSymbols();
                again->determineTypes();
            }
            walked += nodes.count * 3;
            if (!again || check.logger.getErrors())
            {
                return 0;
            }
        }
        for (ALScriptProblem& note : said)
        {
            notes.push_back(std::move(note));
        }
        text = std::move(made);
        map  = std::move(step);
        return went;
    }
} // namespace

// static
ALLSLInliner::Result ALLSLInliner::run(std::string_view source, const std::vector<std::string>& marked, size_t budget)
{
    Asked asked;
    asked.marked = marked;
    return run(source, asked, budget);
}

// static
ALLSLInliner::Result ALLSLInliner::run(std::string_view source, const Asked& asked, size_t budget)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Result result;
    result.text = std::string(source);
    // The map of the text as it is: each line its own.
    result.map = ALSourceMap::identity(source);
    if (!ALLSLService::builtinsLoaded())
    {
        return result;
    }
    // Round after round, each over the text the last made, its map over
    // the last's, until nothing more can go.
    size_t last = 0;
    for (int round = 0; round < 64; ++round)
    {
        // Each round a whole parse and its walks: none begun that would go
        // past the budget as the last did, what was done so far standing.
        if (result.visited + last > budget)
        {
            result.stoppedEarly = true;
            break;
        }
        ALSourceMap      step;
        ALScriptProblems said;
        size_t           walked = 0;
        const S32        went   = inlineRound(result.text, step, said, asked, walked);
        result.visited += walked;
        last = walked;
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

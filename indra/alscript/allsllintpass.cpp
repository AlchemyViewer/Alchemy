/**
 * @file allsllintpass.cpp
 * @brief The studio's own LSL lints, beside Tailslide's, from ALScriptLintPass's table.
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

#include "allsllintpass.h"

#include "allsleffects.h"
#include "allsltraits.h"
#include "alscriptfixes.h"
#include "alscriptlintpass.h"

#include <tailslide/tailslide.hh>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <optional>

using namespace Tailslide;

namespace
{
    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    bool isNull(LSLASTNode* node)
    {
        return !node || node->getNodeType() == NODE_NULL;
    }

    // Every node under one, and it, in the tree's order.
    void walk(LSLASTNode* node, const std::function<void(LSLASTNode*)>& each)
    {
        if (isNull(node))
        {
            return;
        }
        each(node);
        for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            walk(child, each);
        }
    }

    const char* typeName(LSLIType type)
    {
        switch (type)
        {
            case LST_INTEGER: return "integer";
            case LST_FLOATINGPOINT: return "float";
            case LST_STRING: return "string";
            case LST_KEY: return "key";
            case LST_VECTOR: return "vector";
            case LST_QUATERNION: return "rotation";
            case LST_LIST: return "list";
            default: return nullptr;
        }
    }

    // Said as the rule says, or as `severity` says where one of the rule's
    // findings is more or less than the others.
    ALScriptProblem& problem(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args, const char* name,
                             ALScriptProblems& out, std::optional<ALScriptProblem::Severity> severity = std::nullopt)
    {
        const ALScriptLintPass::Rule* rule = ALScriptLintPass::rule(name);
        const YYLTYPE*                loc  = at->getLoc();
        ALScriptProblem               p;
        p.severity  = severity.value_or(rule->severity);
        p.source    = ALScriptProblem::Source::Lint;
        p.line      = zeroBased(loc->first_line);
        p.column    = zeroBased(loc->first_column);
        p.endLine   = zeroBased(loc->last_line);
        p.endColumn = zeroBased(loc->last_column);
        p.code      = name;
        p.key       = key;
        p.message   = ALScriptProblem::fill(english, args);
        p.args      = std::move(args);
        out.push_back(std::move(p));
        return out.back();
    }

    // The script's text, by the places Tailslide gives: lines and columns
    // from 1.
    class Text
    {
    public:
        explicit Text(std::string_view source) : mSource(source)
        {
            mStarts.push_back(0);
            for (size_t at = source.find('\n'); at != std::string_view::npos; at = source.find('\n', at + 1))
            {
                mStarts.push_back(at + 1);
            }
        }

        std::string_view source() const { return mSource; }

        // Where a node begins, as an offset; npos where it is past the text.
        size_t begin(LSLASTNode* node) const
        {
            const YYLTYPE* loc = node->getLoc();
            if (loc->first_line < 1 || static_cast<size_t>(loc->first_line) > mStarts.size())
            {
                return std::string_view::npos;
            }
            const size_t at = mStarts[loc->first_line - 1] + static_cast<size_t>(std::max(0, loc->first_column - 1));
            return at <= mSource.size() ? at : std::string_view::npos;
        }

        // An offset as an edit's place: its line and column from 0.
        std::pair<S32, S32> place(size_t offset) const
        {
            const auto line = std::upper_bound(mStarts.begin(), mStarts.end(), offset) - mStarts.begin() - 1;
            return { static_cast<S32>(line), static_cast<S32>(offset - mStarts[line]) };
        }

        ALScriptEdit edit(size_t from, size_t to, std::string with) const
        {
            const auto [line, column]         = place(from);
            const auto [end_line, end_column] = place(to);
            return ALScriptEdit(line, column, end_line, end_column, std::move(with));
        }

        // The ) that closes the ( at `open`, past strings and comments;
        // npos where there is none.
        size_t closing(size_t open) const
        {
            int depth = 0;
            for (size_t at = open; at < mSource.size(); ++at)
            {
                const char c = mSource[at];
                if (c == '"')
                {
                    for (++at; at < mSource.size() && mSource[at] != '"'; ++at)
                    {
                        at += mSource[at] == '\\';
                    }
                }
                else if (c == '/' && at + 1 < mSource.size() && mSource[at + 1] == '/')
                {
                    at = std::min(mSource.find('\n', at), mSource.size());
                }
                else if (c == '/' && at + 1 < mSource.size() && mSource[at + 1] == '*')
                {
                    const size_t end = mSource.find("*/", at + 2);
                    at               = end == std::string_view::npos ? mSource.size() : end + 1;
                }
                else if (c == '(')
                {
                    ++depth;
                }
                else if (c == ')' && --depth == 0)
                {
                    return at;
                }
            }
            return std::string_view::npos;
        }

    private:
        std::string_view    mSource;
        std::vector<size_t> mStarts;
    };

    // SlSleepingCall: llSetPos and its kin, which make the script sleep
    // where a Fast call does not -- a warning in a loop or the timer event,
    // where the sleep adds up; a note elsewhere. Fixed as the Fast call,
    // not safe: a script may pace itself by the sleep.
    void sleepingCalls(const Text& text, LSLScript* script, ALScriptProblems& out)
    {
        walk(script, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return;
            }
            auto*       call   = static_cast<LSLFunctionExpression*>(node);
            LSLSymbol*  symbol = call->getIdentifier()->getSymbol();
            const char* name   = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getName() : nullptr;
            const ALScriptLintPass::Sleepless* sleepless = name ? ALScriptLintPass::sleepless(name) : nullptr;
            const ALLSLTraits::Trait*          row       = name ? ALLSLTraits::of(name) : nullptr;
            if (!sleepless || !row || row->monoSleep <= 0)
            {
                return;
            }
            bool often = false;
            for (LSLASTNode* up = node->getParent(); up && !often; up = up->getParent())
            {
                const LSLNodeSubType kind = up->getNodeSubType();
                often = kind == NODE_FOR_STATEMENT || kind == NODE_WHILE_STATEMENT || kind == NODE_DO_STATEMENT ||
                        (up->getNodeType() == NODE_EVENT_HANDLER &&
                         std::string_view(static_cast<LSLEventHandler*>(up)->getIdentifier()->getName()) == "timer");
            }
            const std::string   seconds  = llformat("%g", row->monoSleep);
            const auto          severity = often ? ALScriptProblem::Severity::Warning : ALScriptProblem::Severity::Note;
            if (!sleepless->fast)
            {
                problem(node, often ? "LSLSlSleepingTextureOften" : "LSLSlSleepingTexture",
                        often ? "[1] makes the script sleep [2] s each time it runs here, in a loop or the timer event; [3] sets a face's "
                                "texture with no sleep, given its repeats, offsets and rotation too"
                              : "[1] makes the script sleep [2] s each call; [3] sets a face's texture with no sleep, given its repeats, offsets "
                                "and rotation too",
                        { name, seconds, sleepless->rule }, "SlSleepingCall", out, severity);
                return;
            }
            ALScriptProblem& said = problem(node, often ? "LSLSlSleepingCallOften" : "LSLSlSleepingCall",
                                            often ? "[1] makes the script sleep [2] s each time it runs here, in a loop or the timer event; [3] "
                                                    "does the same without the sleep"
                                                  : "[1] makes the script sleep [2] s each call; [3] does the same without the sleep",
                                            { name, seconds, sleepless->fast }, "SlSleepingCall", out, severity);
            // The call's name, its brackets and each argument, in the text.
            const std::string_view source = text.source();
            const size_t           at     = text.begin(call->getIdentifier());
            const size_t           named  = at == std::string_view::npos ? at : at + std::strlen(name);
            if (named == std::string_view::npos || source.substr(at, named - at) != name)
            {
                return;
            }
            const size_t open  = source.find_first_not_of(" \t\r\n", named);
            const size_t close = open != std::string_view::npos && source[open] == '(' ? text.closing(open) : std::string_view::npos;
            if (close == std::string_view::npos)
            {
                return;
            }
            std::vector<LSLASTNode*> args;
            for (LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr; arg; arg = arg->getNext())
            {
                args.push_back(arg);
            }
            std::vector<std::string> given;
            std::vector<size_t>      begins;
            std::vector<size_t>      ends;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const size_t begin = text.begin(args[i]);
                size_t       end   = i + 1 < args.size() ? text.begin(args[i + 1]) : close;
                if (begin == std::string_view::npos || end == std::string_view::npos || end <= begin)
                {
                    return;
                }
                // Back over the comma before the next, and the blanks.
                if (i + 1 < args.size())
                {
                    end = source.rfind(',', end);
                    if (end == std::string_view::npos || end < begin)
                    {
                        return;
                    }
                }
                while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1])))
                {
                    --end;
                }
                begins.push_back(begin);
                ends.push_back(end);
                given.emplace_back(source.substr(begin, end - begin));
            }
            const std::optional<std::string> written = ALScriptLintPass::sleeplessArgs(*sleepless, given, false);
            if (!written || args.empty())
            {
                return;
            }
            std::vector<ALScriptEdit> edits = { text.edit(at, named, sleepless->fast) };
            if (const auto kept = ALScriptLintPass::around(*sleepless, false))
            {
                edits.push_back(text.edit(begins.front(), begins.front(), kept->first));
                edits.push_back(text.edit(ends.back(), ends.back(), kept->second));
            }
            else if (std::all_of(args.begin(), args.end(), [](LSLASTNode* arg) { return ALLSLTraits::sideEffectFree(arg); }))
            {
                // Its arguments in another order, which only what changes
                // nothing may be read in.
                edits.push_back(text.edit(begins.front(), ends.back(), *written));
            }
            else
            {
                return;
            }
            ALScriptFix fix = ALScriptFixes::titled("ScriptFixWriteIt", "Write it [1]", { std::string(sleepless->fast) + "(" + *written + ")" });
            fix.preferred   = true;
            fix.edits       = std::move(edits);
            said.fixes.push_back(std::move(fix));
        });
    }

    // SlLoopInvariantCall: a call in a loop's check to a function the
    // definitions call pure, whose arguments nothing in the loop changes --
    // llGetListLength(l) where the loop leaves l be -- worked out again on
    // every turn when once before it would do.
    void loopInvariantCalls(LSLScript* script, const ALLSLEffects& effects, ALScriptProblems& out)
    {
        walk(script, [&](LSLASTNode* node) {
            LSLExpression* check = nullptr;
            switch (node->getNodeSubType())
            {
                case NODE_FOR_STATEMENT: check = static_cast<LSLForStatement*>(node)->getCheckExpr(); break;
                case NODE_WHILE_STATEMENT: check = static_cast<LSLWhileStatement*>(node)->getCheckExpr(); break;
                case NODE_DO_STATEMENT: check = static_cast<LSLDoStatement*>(node)->getCheckExpr(); break;
                default: return;
            }
            if (isNull(check))
            {
                return;
            }
            const ALLSLEffects::Writes changed = effects.of(node);
            walk(check, [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    return;
                }
                LSLSymbol* symbol = static_cast<LSLFunctionExpression*>(inner)->getIdentifier()->getSymbol();
                if (!symbol || symbol->getSubType() != SYM_BUILTIN || !ALLSLTraits::pure(symbol->getName()) ||
                    !ALLSLTraits::sideEffectFree(inner))
                {
                    return;
                }
                bool steady = true;
                walk(inner, [&](LSLASTNode* read) {
                    if (read->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        steady = steady && !changed.writes(static_cast<LSLLValueExpression*>(read)->getIdentifier()->getSymbol());
                    }
                });
                // One inside another is said with the outer.
                LSLASTNode* up = inner->getParent();
                while (up != check && up && up->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    up = up->getParent();
                }
                if (!steady || up != check)
                {
                    return;
                }
                // For the fix, where the loop stands in a block: the local's
                // type, and where the loop begins, before which it goes.
                std::vector<std::string> args{ symbol->getName() };
                const char*              type = typeName(inner->getIType());
                if (type && node->getParent() && node->getParent()->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                {
                    const YYLTYPE* loop = node->getLoc();
                    args.emplace_back(type);
                    args.push_back(std::to_string(zeroBased(loop->first_line)));
                    args.push_back(std::to_string(zeroBased(loop->first_column)));
                }
                problem(inner, "LSLSlLoopInvariantCall",
                        "[1] is worked out again on every turn of the loop, though nothing in the loop changes what it is given: a "
                        "local set before the loop works it out once",
                        std::move(args), "SlLoopInvariantCall", out);
            });
        });
    }
}

// static
void ALLSLLintPass::check(std::string_view source, LSLScript* script, ALScriptProblems& out)
{
    if (!script)
    {
        return;
    }
    const ALLSLEffects effects(script);
    loopInvariantCalls(script, effects, out);
    sleepingCalls(Text(source), script, out);
}

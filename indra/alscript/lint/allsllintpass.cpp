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
#include "alscriptlexicon.h"
#include "alscriptlintpass.h"

#include <fmt/format.h>
#include <tailslide/tailslide.hh>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
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

        // The ) or ] that closes the ( or [ at `open`, past strings and
        // comments; npos where there is none.
        size_t closing(size_t open) const
        {
            if (open >= mSource.size() || (mSource[open] != '(' && mSource[open] != '['))
            {
                return std::string_view::npos;
            }
            const char opens  = mSource[open];
            const char closes = opens == '(' ? ')' : ']';
            int        depth  = 0;
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
                else if (c == opens)
                {
                    ++depth;
                }
                else if (c == closes && --depth == 0)
                {
                    return at;
                }
            }
            return std::string_view::npos;
        }

        // Whether nothing but blanks comes before an offset on its line.
        bool startsLine(size_t offset) const
        {
            const size_t line = static_cast<size_t>(place(offset).first);
            return mSource.substr(mStarts[line], offset - mStarts[line]).find_first_not_of(" \t") == std::string_view::npos;
        }

        // A name the text has nowhere, from `base`.
        std::string fresh(const std::string& base) const
        {
            const auto taken = [&](const std::string& name) {
                for (size_t at = mSource.find(name); at != std::string_view::npos; at = mSource.find(name, at + 1))
                {
                    const bool before = at > 0 && ALScriptLexicon::isNameByte(mSource[at - 1]);
                    const bool after  = at + name.size() < mSource.size() && ALScriptLexicon::isNameByte(mSource[at + name.size()]);
                    if (!before && !after)
                    {
                        return true;
                    }
                }
                return false;
            };
            std::string name = base;
            for (int n = 2; taken(name); ++n)
            {
                name = base + std::to_string(n);
            }
            return name;
        }

    private:
        std::string_view    mSource;
        std::vector<size_t> mStarts;
    };

    // A call's pieces in the text: its name, its brackets, and each of its
    // arguments' stretches and words.
    struct CallText
    {
        size_t                   at    = 0;
        size_t                   named = 0;
        size_t                   close = 0;
        std::vector<size_t>      begins;
        std::vector<size_t>      ends;
        std::vector<std::string> given;
    };

    std::optional<CallText> callText(const Text& text, LSLFunctionExpression* call, const char* name)
    {
        const std::string_view source = text.source();
        CallText               out;
        out.at    = text.begin(call->getIdentifier());
        out.named = out.at == std::string_view::npos ? out.at : out.at + std::strlen(name);
        if (out.named == std::string_view::npos || source.substr(out.at, out.named - out.at) != name)
        {
            return std::nullopt;
        }
        const size_t open = source.find_first_not_of(" \t\r\n", out.named);
        out.close         = open == std::string_view::npos ? open : text.closing(open);
        if (out.close == std::string_view::npos)
        {
            return std::nullopt;
        }
        std::vector<LSLASTNode*> args;
        for (LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr; arg; arg = arg->getNext())
        {
            args.push_back(arg);
        }
        for (size_t i = 0; i < args.size(); ++i)
        {
            const size_t begin = text.begin(args[i]);
            size_t       end   = i + 1 < args.size() ? text.begin(args[i + 1]) : out.close;
            if (begin == std::string_view::npos || end == std::string_view::npos || end <= begin)
            {
                return std::nullopt;
            }
            // Back over the comma before the next, and the blanks.
            if (i + 1 < args.size())
            {
                end = source.rfind(',', end);
                if (end == std::string_view::npos || end < begin)
                {
                    return std::nullopt;
                }
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1])))
            {
                --end;
            }
            out.begins.push_back(begin);
            out.ends.push_back(end);
            out.given.emplace_back(source.substr(begin, end - begin));
        }
        return out;
    }

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
            const std::string   seconds  = fmt::format("{:g}", row->monoSleep);
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
            const std::optional<CallText> parts = callText(text, call, name);
            if (!parts)
            {
                return;
            }
            std::vector<LSLASTNode*> args;
            for (LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr; arg; arg = arg->getNext())
            {
                args.push_back(arg);
            }
            const std::vector<std::string>& given  = parts->given;
            const std::vector<size_t>&      begins = parts->begins;
            const std::vector<size_t>&      ends   = parts->ends;
            const size_t                    at     = parts->at;
            const size_t                    named  = parts->named;
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

    // SlMergeablePrimParams: prim-params calls one after another, each with
    // its rules written out as a list, calling the same function, which one
    // call with all their rules does -- PRIM_LINK_TARGET between where the
    // link changes. After the first, only where all a call is given changes
    // nothing and reads nothing the calls before it could change. A note;
    // fixed as the one call, not safe.
    void mergeablePrimParams(const Text& text, LSLScript* script, ALScriptProblems& out)
    {
        struct PrimStat
        {
            LSLFunctionExpression*     call;
            const char*                name;
            int                        link;
            CallText                   parts;
            ALScriptLintPass::PrimCall prim;
        };
        const std::string_view source = text.source();
        const auto primStat = [&](LSLASTNode* stat, bool later) -> std::optional<PrimStat> {
            if (stat->getNodeSubType() != NODE_EXPRESSION_STATEMENT)
            {
                return std::nullopt;
            }
            LSLASTNode* expr = static_cast<LSLExpressionStatement*>(stat)->getExpr();
            if (isNull(expr) || expr->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return std::nullopt;
            }
            auto*       call   = static_cast<LSLFunctionExpression*>(expr);
            LSLSymbol*  symbol = call->getIdentifier()->getSymbol();
            const char* name   = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getName() : nullptr;
            const ALScriptLintPass::PrimParams* params = name ? ALScriptLintPass::primParams(name) : nullptr;
            if (!params)
            {
                return std::nullopt;
            }
            std::vector<LSLASTNode*> args;
            for (LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr; arg; arg = arg->getNext())
            {
                args.push_back(arg);
            }
            if (args.size() != static_cast<size_t>(params->rules + 1) || args[params->rules]->getNodeSubType() != NODE_LIST_EXPRESSION ||
                (later && !std::all_of(args.begin(), args.end(), [](LSLASTNode* arg) { return ALLSLTraits::sideEffectFree(arg); })))
            {
                return std::nullopt;
            }
            std::optional<CallText> parts = callText(text, call, name);
            if (!parts)
            {
                return std::nullopt;
            }
            // What is inside the rules' brackets.
            const size_t open  = text.begin(args[params->rules]);
            const size_t close = open == std::string_view::npos ? open : text.closing(open);
            if (close == std::string_view::npos)
            {
                return std::nullopt;
            }
            std::string_view inner = source.substr(open + 1, close - open - 1);
            const size_t     from  = inner.find_first_not_of(" \t\r\n");
            inner                  = from == std::string_view::npos ? std::string_view() : inner.substr(from, inner.find_last_not_of(" \t\r\n") - from + 1);
            PrimStat out{ call, name, params->link, std::move(*parts), {} };
            out.prim.link    = params->link < 0 ? std::string("LINK_THIS") : out.parts.given[params->link];
            out.prim.rules   = std::string(inner);
            out.prim.targets = inner.find("PRIM_LINK_TARGET") != std::string_view::npos;
            return out;
        };
        walk(script, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_COMPOUND_STATEMENT)
            {
                return;
            }
            for (LSLASTNode* stat = node->getChild(0); stat;)
            {
                std::optional<PrimStat> first = primStat(stat, false);
                LSLASTNode*             next  = stat->getNext();
                if (!first)
                {
                    stat = next;
                    continue;
                }
                std::vector<PrimStat> run;
                run.push_back(std::move(*first));
                for (; next; next = next->getNext())
                {
                    std::optional<PrimStat> more = primStat(next, true);
                    if (!more || std::string_view(more->name) != run.front().name)
                    {
                        break;
                    }
                    run.push_back(std::move(*more));
                }
                stat = next;
                if (run.size() < 2)
                {
                    continue;
                }
                std::vector<ALScriptLintPass::PrimCall> calls;
                for (const PrimStat& each : run)
                {
                    calls.push_back(each.prim);
                }
                const PrimStat&   head = run.front();
                const std::string now  = std::string(head.name) + "(" + (head.link < 0 ? std::string() : head.prim.link + ", ") +
                                        ALScriptLintPass::mergedRules(calls, false) + ")";
                ALScriptProblem&  said = problem(head.call, "LSLSlMergeablePrimParams",
                                                 "These [1] calls to [2] could be one, with all their rules, and PRIM_LINK_TARGET where the link changes",
                                                 { std::to_string(run.size()), head.name }, "SlMergeablePrimParams", out);
                // Said over the whole run; the last call's ; kept.
                const auto [end_line, end_column] = text.place(run.back().parts.close + 1);
                said.endLine                      = end_line;
                said.endColumn                    = end_column;
                ALScriptFix fix = ALScriptFixes::titled("ScriptFixWriteIt", "Write it [1]", { now });
                fix.preferred   = true;
                fix.edits.push_back(text.edit(head.parts.at, run.back().parts.close + 1, now));
                said.fixes.push_back(std::move(fix));
            }
        });
    }

    // SlCostlyListen, SlFastTimer, SlFastSensor: what the script sets going
    // that costs the region more than it needs -- a listen that hears all
    // chat, a timer under a tenth of a second, a sensor sweeping more than
    // once a second. Notes: each may be what the script is for.
    void costlyEvents(LSLScript* script, ALScriptProblems& out)
    {
        const auto number = [](LSLASTNode* node) -> std::optional<double> {
            LSLConstant* cv = isNull(node) ? nullptr : node->getConstantValue();
            if (cv && cv->getIType() == LST_INTEGER)
            {
                return static_cast<LSLIntegerConstant*>(cv)->getValue();
            }
            if (cv && cv->getIType() == LST_FLOATINGPOINT)
            {
                return static_cast<LSLFloatConstant*>(cv)->getValue();
            }
            return std::nullopt;
        };
        // "", or NULL_KEY.
        const auto nothing = [](LSLASTNode* node) {
            LSLConstant* cv = isNull(node) ? nullptr : node->getConstantValue();
            if (!cv || (cv->getIType() != LST_STRING && cv->getIType() != LST_KEY))
            {
                return false;
            }
            const std::string_view value = static_cast<LSLStringConstant*>(cv)->getValue();
            return value.empty() || value == "00000000-0000-0000-0000-000000000000";
        };
        walk(script, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return;
            }
            auto*                  call   = static_cast<LSLFunctionExpression*>(node);
            LSLSymbol*             symbol = call->getIdentifier()->getSymbol();
            const std::string_view name   = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getName() : "";
            std::vector<LSLASTNode*> args;
            for (LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr; arg; arg = arg->getNext())
            {
                args.push_back(arg);
            }
            if (name == "llListen" && args.size() == 4 && number(args[0]) == 0.0 && nothing(args[1]) && nothing(args[2]))
            {
                problem(node, "LSLSlCostlyListen",
                        "This listens on channel 0 for anyone, so the script wakes for every line of chat nearby. A name, a key, or "
                        "another channel hears less",
                        {}, "SlCostlyListen", out);
            }
            const std::optional<double> every = name == "llSetTimerEvent" && args.size() == 1 ? number(args[0]) : std::nullopt;
            if (every && *every > 0 && *every < 0.1)
            {
                problem(node, "LSLSlFastTimer",
                        "A timer every [1] s, under a tenth of a second, fires every few of the region's 45 frames a second, and the "
                        "time it takes is time other scripts there wait for",
                        { fmt::format("{:g}", *every) }, "SlFastTimer", out);
            }
            const std::optional<double> rate = name == "llSensorRepeat" && args.size() == 6 ? number(args[5]) : std::nullopt;
            if (rate && *rate > 0 && *rate < 1)
            {
                problem(node, "LSLSlFastSensor",
                        "A sensor sweeping every [1] s, under a second, searches round the object that often: once a second or less "
                        "is plenty for most",
                        { fmt::format("{:g}", *rate) }, "SlFastSensor", out);
            }
        });
    }

    // SlStringBuild: s += x, or s = s + x, in a loop, s a string from
    // outside it: all of s copied each time round. Said once for each
    // string. A note, and no fix: LSL has no better way; SLua's is a table
    // of the pieces, joined once.
    void stringBuilds(LSLScript* script, ALScriptProblems& out)
    {
        std::vector<LSLSymbol*> said;
        walk(script, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_BINARY_EXPRESSION || node->getIType() != LST_STRING)
            {
                return;
            }
            auto*             b   = static_cast<LSLBinaryExpression*>(node);
            const LSLOperator op  = b->getOperation();
            LSLASTNode*       lhs = b->getLHS();
            if (isNull(lhs) || lhs->getNodeSubType() != NODE_LVALUE_EXPRESSION)
            {
                return;
            }
            LSLSymbol* symbol = static_cast<LSLLValueExpression*>(lhs)->getIdentifier()->getSymbol();
            bool       append = op == OP_ADD_ASSIGN;
            if (op == OP_ASSIGN && !isNull(b->getRHS()) && b->getRHS()->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                auto*       sum  = static_cast<LSLBinaryExpression*>(b->getRHS());
                LSLASTNode* left = sum->getLHS();
                append           = sum->getOperation() == OP_PLUS && !isNull(left) && left->getNodeSubType() == NODE_LVALUE_EXPRESSION &&
                         static_cast<LSLLValueExpression*>(left)->getIdentifier()->getSymbol() == symbol;
            }
            if (!append || !symbol || std::find(said.begin(), said.end(), symbol) != said.end())
            {
                return;
            }
            // In a loop that s is not declared in, within its function.
            bool looped = false;
            for (LSLASTNode* up = node->getParent(); up && !looped; up = up->getParent())
            {
                if (up->getNodeType() == NODE_GLOBAL_FUNCTION || up->getNodeType() == NODE_EVENT_HANDLER)
                {
                    break;
                }
                const LSLNodeSubType kind = up->getNodeSubType();
                if (kind == NODE_FOR_STATEMENT || kind == NODE_WHILE_STATEMENT || kind == NODE_DO_STATEMENT)
                {
                    bool declared = false;
                    walk(up, [&](LSLASTNode* inner) {
                        declared = declared || (inner->getNodeSubType() == NODE_DECLARATION &&
                                                static_cast<LSLIdentifier*>(inner->getChild(0))->getSymbol() == symbol);
                    });
                    looped = !declared;
                }
            }
            if (!looped)
            {
                return;
            }
            said.push_back(symbol);
            problem(node, "LSLSlStringBuild",
                    "[1] is joined to with + in a loop, which copies all of it each time round. In SLua the pieces put in a table and "
                    "joined once, with table.concat, are quicker",
                    { symbol->getName() }, "SlStringBuild", out);
        });
    }

    // SlRepeatedCall: the same call made more than once in one handler or
    // function, whose answer cannot have changed between -- llGetOwner()
    // and its kin, which answer the same throughout an event, and a pure
    // function given only what the body never changes. A note, fixed as a
    // local set before the body's first statement that calls it, which
    // each call then reads; where what it is given is declared by then.
    // Not safe: a jump past the local would read it unset.
    void repeatedCalls(const Text& text, LSLScript* script, const ALLSLEffects& effects, ALScriptProblems& out)
    {
        static constexpr std::string_view RESERVED[] = { "key",     "list", "string", "integer", "float", "vector", "rotation", "quaternion",
                                                         "state",   "default", "jump", "return", "if",    "else",   "for",      "do",
                                                         "while",   "print",   "event" };
        walk(script, [&](LSLASTNode* node) {
            if (node->getNodeType() != NODE_EVENT_HANDLER && node->getNodeType() != NODE_GLOBAL_FUNCTION)
            {
                return;
            }
            LSLASTNode* body = node->getChild(2);
            if (isNull(body) || body->getNodeSubType() != NODE_COMPOUND_STATEMENT)
            {
                return;
            }
            const ALLSLEffects::Writes written = effects.of(body);
            // The body's own statement a node is in.
            const auto topOf = [&](LSLASTNode* inner) {
                while (inner && inner->getParent() != body)
                {
                    inner = inner->getParent();
                }
                return inner;
            };
            // Whether a local is declared by one of the body's own statements
            // before `top`.
            const auto declaredBefore = [&](LSLSymbol* symbol, LSLASTNode* top) {
                for (LSLASTNode* stat = body->getChild(0); stat && stat != top; stat = stat->getNext())
                {
                    if (stat->getNodeSubType() == NODE_DECLARATION && static_cast<LSLIdentifier*>(stat->getChild(0))->getSymbol() == symbol)
                    {
                        return true;
                    }
                }
                return false;
            };
            struct Made
            {
                LSLFunctionExpression* call;
                CallText               parts;
            };
            std::vector<std::pair<std::string, std::vector<Made>>> groups;
            std::vector<std::pair<std::string, std::vector<LSLSymbol*>>> locals;
            walk(body, [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    return;
                }
                auto*       call   = static_cast<LSLFunctionExpression*>(inner);
                LSLSymbol*  symbol = call->getIdentifier()->getSymbol();
                const char* name   = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getName() : nullptr;
                if (!name)
                {
                    return;
                }
                const bool has_args = call->getArguments() && call->getArguments()->getChild(0);
                const bool steady   = ALScriptLintPass::steadyName(name) && !has_args;
                if (!steady && !(ALLSLTraits::pure(name) && has_args && ALLSLTraits::sideEffectFree(call)))
                {
                    return;
                }
                // What a pure one reads: nothing the body changes, and some
                // variable -- a call of constants the optimizer folds.
                std::vector<LSLSymbol*> own;
                bool                    reads = false;
                bool                    still = true;
                walk(call, [&](LSLASTNode* read) {
                    if (read->getNodeSubType() != NODE_LVALUE_EXPRESSION)
                    {
                        return;
                    }
                    LSLSymbol* variable = static_cast<LSLLValueExpression*>(read)->getIdentifier()->getSymbol();
                    reads               = true;
                    still               = still && variable && !written.writes(variable);
                    if (variable && variable->getSubType() == SYM_LOCAL)
                    {
                        own.push_back(variable);
                    }
                });
                if (!steady && (!reads || !still))
                {
                    return;
                }
                std::optional<CallText> parts = callText(text, call, name);
                if (!parts)
                {
                    return;
                }
                std::string key = std::string(name) + "(";
                for (size_t i = 0; i < parts->given.size(); ++i)
                {
                    key += (i ? ", " : "") + parts->given[i];
                }
                key += ")";
                auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
                if (group == groups.end())
                {
                    groups.push_back({ key, {} });
                    locals.push_back({ key, own });
                    group = groups.end() - 1;
                }
                group->second.push_back({ call, std::move(*parts) });
            });
            for (size_t g = 0; g < groups.size(); ++g)
            {
                auto& [key, made] = groups[g];
                if (made.size() < 2)
                {
                    continue;
                }
                std::sort(made.begin(), made.end(), [](const Made& a, const Made& b) { return a.parts.at < b.parts.at; });
                const char*      name = static_cast<LSLFunctionExpression*>(made.front().call)->getIdentifier()->getSymbol()->getName();
                ALScriptProblem& said = problem(made.front().call, "LSLSlRepeatedCall",
                                                "[1] is called [2] times here, and answers the same each time: a local set once holds it",
                                                { key, std::to_string(made.size()) }, "SlRepeatedCall", out);
                LSLASTNode*      top  = topOf(made.front().call);
                const char*      type = typeName(made.front().call->getIType());
                const size_t     at   = top ? text.begin(top) : std::string_view::npos;
                bool             ok   = type && at != std::string_view::npos && text.startsLine(at);
                for (LSLSymbol* local : locals[g].second)
                {
                    ok = ok && declaredBefore(local, top);
                }
                if (!ok)
                {
                    continue;
                }
                // Its name: the steady one's own, a length's, else the
                // function's without ll.
                const std::string_view lsl  = name;
                std::string            base = ALScriptLintPass::steadyName(lsl) ? ALScriptLintPass::steadyName(lsl)
                                              : lsl == "llGetListLength" || lsl == "llStringLength" ? std::string("length")
                                                                                                    : std::string(lsl.substr(2));
                base[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(base[0])));
                if (std::find(std::begin(RESERVED), std::end(RESERVED), base) != std::end(RESERVED))
                {
                    base += "Value";
                }
                const std::string         local  = text.fresh(base);
                const auto [line, column]        = text.place(at);
                const std::string         indent = std::string(text.source().substr(at - static_cast<size_t>(column), static_cast<size_t>(column)));
                const std::string         call   = std::string(text.source().substr(made.front().parts.at, made.front().parts.close + 1 - made.front().parts.at));
                std::vector<ALScriptEdit> edits  = { text.edit(at, at, std::string(type) + " " + local + " = " + call + ";\n" + indent) };
                for (const Made& each : made)
                {
                    edits.push_back(text.edit(each.parts.at, each.parts.close + 1, local));
                }
                ALScriptFix fix = ALScriptFixes::titled("ScriptFixKeepCall", "Keep [1] in a local, [2]", { key, local });
                fix.preferred   = true;
                fix.edits       = std::move(edits);
                said.fixes.push_back(std::move(fix));
            }
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

    // SlIntegerPast32Bits: a whole number written past 0xFFFFFFFF, which the
    // grid's compilers read as -1 -- their 32-bit hosts' strtoul stops
    // there -- and as 1 with a minus before it. Read from the text, past
    // strings, comments and floats, since Tailslide's tree keeps only what
    // it read. A warning: no script means what it says.
    void integersPast32Bits(const Text& text, ALScriptProblems& out)
    {
        const std::string_view source = text.source();
        for (size_t at = 0; at < source.size();)
        {
            const char c = source[at];
            if (c == '"')
            {
                for (++at; at < source.size() && source[at] != '"'; ++at)
                {
                    at += source[at] == '\\';
                }
                ++at;
                continue;
            }
            if (c == '/' && at + 1 < source.size() && source[at + 1] == '/')
            {
                at = std::min(source.find('\n', at), source.size());
                continue;
            }
            if (c == '/' && at + 1 < source.size() && source[at + 1] == '*')
            {
                const size_t end = source.find("*/", at + 2);
                at               = end == std::string_view::npos ? source.size() : end + 2;
                continue;
            }
            const bool starts = std::isdigit(static_cast<unsigned char>(c)) &&
                                (at == 0 || !(ALScriptLexicon::isNameByte(source[at - 1]) || source[at - 1] == '.'));
            if (!starts)
            {
                ++at;
                continue;
            }
            const bool hex = c == '0' && at + 2 < source.size() && (source[at + 1] == 'x' || source[at + 1] == 'X') &&
                             std::isxdigit(static_cast<unsigned char>(source[at + 2]));
            size_t     end = hex ? at + 2 : at;
            while (end < source.size() && (hex ? std::isxdigit(static_cast<unsigned char>(source[end])) : std::isdigit(static_cast<unsigned char>(source[end]))))
            {
                ++end;
            }
            // A float, or a name that starts with digits: not a whole number.
            if (end < source.size() && (source[end] == '.' || ALScriptLexicon::isNameByte(source[end])))
            {
                while (end < source.size() && (source[end] == '.' || ALScriptLexicon::isNameByte(source[end])))
                {
                    ++end;
                }
                at = end;
                continue;
            }
            const std::string written(source.substr(at, end - at));
            errno                          = 0;
            const unsigned long long value = std::strtoull(written.c_str(), nullptr, hex ? 16 : 10);
            if (errno == ERANGE || value > 0xFFFFFFFFull)
            {
                // A minus before it that is not taking it from something:
                // -1 made 1.
                size_t minus = at;
                while (minus > 0 && (source[minus - 1] == ' ' || source[minus - 1] == '\t'))
                {
                    --minus;
                }
                size_t before = minus > 0 && source[minus - 1] == '-' ? minus - 1 : std::string_view::npos;
                if (before != std::string_view::npos)
                {
                    size_t prior = before;
                    while (prior > 0 && std::isspace(static_cast<unsigned char>(source[prior - 1])))
                    {
                        --prior;
                    }
                    const char last = prior > 0 ? source[prior - 1] : '\0';
                    if (ALScriptLexicon::isNameByte(last) || last == ')' || last == ']' || last == '.')
                    {
                        before = std::string_view::npos;
                    }
                }
                const size_t from              = before != std::string_view::npos ? before : at;
                const auto [line, column]      = text.place(from);
                const auto [end_line, end_col] = text.place(end);
                ALScriptProblem p;
                p.severity  = ALScriptLintPass::rule("SlIntegerPast32Bits")->severity;
                p.source    = ALScriptProblem::Source::Lint;
                p.line      = line;
                p.column    = column;
                p.endLine   = end_line;
                p.endColumn = end_col;
                p.code      = "SlIntegerPast32Bits";
                p.key       = "LSLSlIntegerPast32Bits";
                p.args      = { std::string(source.substr(from, end - from)), before != std::string_view::npos ? "1" : "-1" };
                p.message   = ALScriptProblem::fill("LSL reads [1] as [2]: past 0xFFFFFFFF, the most its 32 bits hold, a number stops there, which is -1",
                                                    p.args);
                out.push_back(std::move(p));
            }
            at = end;
        }
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
    const Text text(source);
    sleepingCalls(text, script, out);
    mergeablePrimParams(text, script, out);
    costlyEvents(script, out);
    stringBuilds(script, out);
    repeatedCalls(text, script, effects, out);
    integersPast32Bits(text, out);
}

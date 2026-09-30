/**
 * @file alscriptlintpass.cpp
 * @brief The studio's own lints, beside Luau's and Tailslide's: one table of them, and the pass over SLua.
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

#include "alscriptlintpass.h"

#include "Luau/Ast.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"

#include <algorithm>

namespace
{
    using Severity = ALScriptProblem::Severity;

    // --- the table ------------------------------------------------------------------

    const std::vector<ALScriptLintPass::Rule> RULES = {
        // x = x + 1, where Luau has x += 1.
        { "SlCompoundAssign", true, Severity::Note, true, true, nullptr },
    };

    // --- the pass -------------------------------------------------------------------

    // The same place written twice: a local, a global, or a field of one by
    // its name -- nothing that runs anything to be found.
    bool same(Luau::AstExpr* a, Luau::AstExpr* b)
    {
        if (const auto* la = a->as<Luau::AstExprLocal>())
        {
            const auto* lb = b->as<Luau::AstExprLocal>();
            return lb && la->local == lb->local;
        }
        if (const auto* ga = a->as<Luau::AstExprGlobal>())
        {
            const auto* gb = b->as<Luau::AstExprGlobal>();
            return gb && ga->name == gb->name;
        }
        if (const auto* ia = a->as<Luau::AstExprIndexName>())
        {
            const auto* ib = b->as<Luau::AstExprIndexName>();
            return ib && ia->index == ib->index && ia->op == ib->op && same(ia->expr, ib->expr);
        }
        return false;
    }

    // The operator Luau has a compound assignment of, as written; null for
    // one it has not.
    const char* compound(Luau::AstExprBinary::Op op)
    {
        switch (op)
        {
            case Luau::AstExprBinary::Add: return "+";
            case Luau::AstExprBinary::Sub: return "-";
            case Luau::AstExprBinary::Mul: return "*";
            case Luau::AstExprBinary::Div: return "/";
            case Luau::AstExprBinary::FloorDiv: return "//";
            case Luau::AstExprBinary::Mod: return "%";
            case Luau::AstExprBinary::Pow: return "^";
            case Luau::AstExprBinary::Concat: return "..";
            default: return nullptr;
        }
    }

    class Pass final : public Luau::AstVisitor
    {
    public:
        Pass(std::string_view source, const Luau::Module* checked, uint64_t enabled, uint64_t fatal, bool all_errors, ALScriptProblems& out)
            : mSource(source), mChecked(checked), mEnabled(enabled), mFatal(fatal), mAllErrors(all_errors), mOut(out)
        {
            mStarts.push_back(0);
            for (size_t at = source.find('\n'); at != std::string_view::npos; at = source.find('\n', at + 1))
            {
                mStarts.push_back(at + 1);
            }
        }

        bool visit(Luau::AstStatAssign* node) override
        {
            if (on("SlCompoundAssign") && node->vars.size == 1 && node->values.size == 1)
            {
                const auto* sum = node->values.data[0]->as<Luau::AstExprBinary>();
                const char* op  = sum ? compound(sum->op) : nullptr;
                if (op && same(node->vars.data[0], sum->left))
                {
                    problem(node->location, "LuauLintSlCompoundAssign", "Luau's [2]= says this once: [1] [2]= ...",
                            { text(node->vars.data[0]->location), op }, "SlCompoundAssign");
                }
            }
            return true;
        }

    private:
        bool on(std::string_view name) const { return (mEnabled & ALScriptLintPass::bit(name)) != 0; }

        // What the script says at a place, from its text.
        std::string text(const Luau::Location& where) const
        {
            const auto offset = [&](const Luau::Position& p) {
                return p.line < mStarts.size() ? std::min(mSource.size(), mStarts[p.line] + p.column) : mSource.size();
            };
            const size_t from = offset(where.begin);
            return std::string(mSource.substr(from, offset(where.end) - from));
        }

        void problem(const Luau::Location& where, const char* key, const char* english, std::vector<std::string> args, const char* name)
        {
            const ALScriptLintPass::Rule* rule = ALScriptLintPass::rule(name);
            ALScriptProblem               p;
            p.severity  = mAllErrors || (mFatal & ALScriptLintPass::bit(name)) ? Severity::Error : rule->severity;
            p.source    = ALScriptProblem::Source::Lint;
            p.line      = static_cast<S32>(where.begin.line);
            p.column    = static_cast<S32>(where.begin.column);
            p.endLine   = static_cast<S32>(where.end.line);
            p.endColumn = static_cast<S32>(where.end.column);
            p.code      = name;
            p.key       = key;
            p.message   = ALScriptProblem::fill(english, args);
            p.args      = std::move(args);
            mOut.push_back(std::move(p));
        }

        std::string_view    mSource;
        std::vector<size_t> mStarts;
        const Luau::Module* mChecked;
        uint64_t            mEnabled;
        uint64_t            mFatal;
        bool                mAllErrors;
        ALScriptProblems&   mOut;
    };
}

// static
const std::vector<ALScriptLintPass::Rule>& ALScriptLintPass::rules()
{
    return RULES;
}

// static
const ALScriptLintPass::Rule* ALScriptLintPass::rule(std::string_view name)
{
    const auto found = std::find_if(RULES.begin(), RULES.end(), [name](const Rule& r) { return name == r.name; });
    return found == RULES.end() ? nullptr : &*found;
}

// static
uint64_t ALScriptLintPass::bit(std::string_view name)
{
    const Rule* found = rule(name);
    return found ? 1ull << (found - RULES.data()) : 0;
}

// static
uint64_t ALScriptLintPass::defaults()
{
    uint64_t out = 0;
    for (const Rule& r : RULES)
    {
        out |= r.on ? bit(r.name) : 0;
    }
    return out;
}

// static
uint64_t ALScriptLintPass::nolint(const std::vector<Luau::HotComment>& hotcomments)
{
    uint64_t off = 0;
    for (const Luau::HotComment& comment : hotcomments)
    {
        if (!comment.header)
        {
            continue;
        }
        const std::string_view content = comment.content;
        const size_t           space   = content.find_first_of(" \t");
        if (content.substr(0, space) != "nolint")
        {
            continue;
        }
        const size_t name = space == std::string_view::npos ? std::string_view::npos : content.find_first_not_of(" \t", space);
        off |= name == std::string_view::npos ? ~0ull : bit(content.substr(name));
    }
    return off;
}

// static
void ALScriptLintPass::check(std::string_view source, const Luau::SourceModule& module, const Luau::Module* checked, uint64_t enabled,
                             uint64_t fatal, bool all_errors, ALScriptProblems& out)
{
    enabled &= ~nolint(module.hotcomments);
    if (!enabled || !module.root)
    {
        return;
    }
    Pass pass(source, checked, enabled, fatal, all_errors, out);
    module.root->visit(&pass);
}

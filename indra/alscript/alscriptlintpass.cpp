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
#include "Luau/Type.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace
{
    using Severity = ALScriptProblem::Severity;

    // --- the table ------------------------------------------------------------------

    const std::vector<ALScriptLintPass::Rule> RULES = {
        // x = x + 1, where Luau has x += 1.
        { "SlCompoundAssign", true, Severity::Note, true, true, nullptr },
        // LSL: llGetListLength(l) in a loop's check, l unchanged in it.
        { "SlLoopInvariantCall", false, Severity::Note, true, true, nullptr },
        // if n then, where n is a number: true at 0 as at anything.
        { "SlNumberTruth", true, Severity::Warning, true, true, nullptr },
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

    // What each local is given, where it is declared and at each
    // assignment, for the old solver's nonstrict mode, which types an
    // unannotated local any: a local given only numbers is one.
    class Locals final : public Luau::AstVisitor
    {
    public:
        boost::unordered_flat_map<Luau::AstLocal*, std::vector<Luau::AstExpr*>> given;
        // Given something that is not an expression's alone: a parameter, a
        // generic for's, one of several a call's values go to.
        boost::unordered_flat_set<Luau::AstLocal*> unknown;
        boost::unordered_flat_set<Luau::AstLocal*> counters;

        bool visit(Luau::AstStatLocal* node) override
        {
            for (size_t i = 0; i < node->vars.size; ++i)
            {
                // The last value's call may give several, to the rest.
                const bool has   = i < node->values.size;
                const bool alone = has && (i + 1 < node->values.size || !node->values.data[i]->is<Luau::AstExprCall>() || node->vars.size == 1);
                if (alone)
                {
                    given[node->vars.data[i]].push_back(node->values.data[i]);
                }
                else if (node->values.size > 0)
                {
                    unknown.insert(node->vars.data[i]);
                }
            }
            return true;
        }
        bool visit(Luau::AstStatAssign* node) override
        {
            for (size_t i = 0; i < node->vars.size; ++i)
            {
                if (auto* local = node->vars.data[i]->as<Luau::AstExprLocal>())
                {
                    if (i < node->values.size && (i + 1 < node->values.size || node->vars.size == 1))
                    {
                        given[local->local].push_back(node->values.data[i]);
                    }
                    else
                    {
                        unknown.insert(local->local);
                    }
                }
            }
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            // A number stays one but for .., which makes a string.
            if (auto* local = node->var->as<Luau::AstExprLocal>(); local && node->op == Luau::AstExprBinary::Concat)
            {
                unknown.insert(local->local);
            }
            return true;
        }
        bool visit(Luau::AstStatFor* node) override
        {
            counters.insert(node->var);
            return true;
        }
        bool visit(Luau::AstStatForIn* node) override
        {
            for (Luau::AstLocal* var : node->vars)
            {
                unknown.insert(var);
            }
            return true;
        }
        bool visit(Luau::AstExprFunction* node) override
        {
            for (Luau::AstLocal* arg : node->args)
            {
                unknown.insert(arg);
            }
            return true;
        }
    };

    class Pass final : public Luau::AstVisitor
    {
    public:
        Pass(std::string_view source, const Luau::Module* checked, const Locals& locals, uint64_t enabled, uint64_t fatal, bool all_errors,
             ALScriptProblems& out)
            : mSource(source), mChecked(checked), mLocals(locals), mEnabled(enabled), mFatal(fatal), mAllErrors(all_errors), mOut(out)
        {
            mStarts.push_back(0);
            for (size_t at = source.find('\n'); at != std::string_view::npos; at = source.find('\n', at + 1))
            {
                mStarts.push_back(at + 1);
            }
        }

        // --- SlNumberTruth: a number asked whether it is true ------------------

        bool visit(Luau::AstStatIf* node) override
        {
            truth(node->condition);
            return true;
        }
        bool visit(Luau::AstStatWhile* node) override
        {
            truth(node->condition);
            return true;
        }
        bool visit(Luau::AstStatRepeat* node) override
        {
            truth(node->condition);
            return true;
        }
        bool visit(Luau::AstExprIfElse* node) override
        {
            truth(node->condition);
            return true;
        }
        bool visit(Luau::AstExprUnary* node) override
        {
            if (node->op != Luau::AstExprUnary::Op::Not || !on("SlNumberTruth"))
            {
                return true;
            }
            if (number(node->expr))
            {
                const std::string text = this->text(node->expr->location);
                problem(node->location, "LuauLintSlNumberTruthNot",
                        "not [1] is always false: [1] is a number, and Luau counts every number true, 0 too. [1] == 0 asks whether it is 0",
                        { text }, "SlNumberTruth");
            }
            else
            {
                truth(node->expr);
            }
            return true;
        }

        // --- SlCompoundAssign ------------------------------------------------

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

        // Whether the check found an expression a number, and nothing else:
        // not number?, whose nil is false. A local the check says is any --
        // unannotated, in the old solver's nonstrict mode -- by what it is
        // given.
        bool number(Luau::AstExpr* e)
        {
            const Luau::TypeId* type = mChecked ? mChecked->astTypes.find(e) : nullptr;
            if (!type)
            {
                return false;
            }
            const Luau::TypeId followed = Luau::follow(*type);
            if (const auto* prim = Luau::get<Luau::PrimitiveType>(followed))
            {
                return prim->type == Luau::PrimitiveType::Number;
            }
            auto* local = e->as<Luau::AstExprLocal>();
            return local && Luau::get<Luau::AnyType>(followed) && numberLocal(local->local);
        }

        bool numberLocal(Luau::AstLocal* local)
        {
            if (const auto known = mNumberLocals.find(local); known != mNumberLocals.end())
            {
                return known->second;
            }
            // Taken as none while its own are asked about, which a loop of
            // locals given each other cannot then make one.
            mNumberLocals[local] = false;
            bool is = mLocals.counters.contains(local);
            if (!is && !mLocals.unknown.contains(local) && !local->annotation)
            {
                const auto given = mLocals.given.find(local);
                is = given != mLocals.given.end() && !given->second.empty();
                for (size_t i = 0; is && i < given->second.size(); ++i)
                {
                    is = number(given->second[i]);
                }
            }
            mNumberLocals[local] = is;
            return is;
        }

        // What a condition asks the truth of: itself, or each side of an
        // and or an or, inside brackets or not.
        void truth(Luau::AstExpr* e)
        {
            if (!on("SlNumberTruth"))
            {
                return;
            }
            if (auto* group = e->as<Luau::AstExprGroup>())
            {
                truth(group->expr);
                return;
            }
            auto* both = e->as<Luau::AstExprBinary>();
            if (both && (both->op == Luau::AstExprBinary::And || both->op == Luau::AstExprBinary::Or))
            {
                truth(both->left);
                truth(both->right);
                return;
            }
            if (number(e))
            {
                // An if-then-else inside brackets, the comparison binding
                // tighter.
                problem(e->location, "LuauLintSlNumberTruth",
                        "[1] is a number, and Luau counts every number true, 0 too: [1] ~= 0 asks whether it is not 0",
                        { text(e->location), e->is<Luau::AstExprIfElse>() ? "(" : "" }, "SlNumberTruth");
            }
        }

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
        const Locals&       mLocals;
        boost::unordered_flat_map<Luau::AstLocal*, bool> mNumberLocals;
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
    Locals locals;
    if (enabled & bit("SlNumberTruth"))
    {
        module.root->visit(&locals);
    }
    Pass pass(source, checked, locals, enabled, fatal, all_errors, out);
    module.root->visit(&pass);
}

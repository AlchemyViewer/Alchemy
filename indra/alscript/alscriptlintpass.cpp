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

#include "allsltraits.h"

#include "Luau/Ast.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Type.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <optional>

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
        // ll.ListFindList(l, x) == -1, where it answers nil; llcompat's
        // against nil, where it answers -1.
        { "SlNilSentinel", true, Severity::Error, true, true, nullptr },
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
        // Declared with no value, so nil until given one.
        boost::unordered_flat_set<Luau::AstLocal*> bare;
        // Given x op= y: a number still, but not the one it was given.
        boost::unordered_flat_set<Luau::AstLocal*> compounded;

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
                else
                {
                    bare.insert(node->vars.data[i]);
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
            if (auto* local = node->var->as<Luau::AstExprLocal>())
            {
                compounded.insert(local->local);
                if (node->op == Luau::AstExprBinary::Concat)
                {
                    unknown.insert(local->local);
                }
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

        // --- SlNilSentinel: a find's nothing asked as the other answers it ----

        bool visit(Luau::AstExprBinary* node) override
        {
            if (on("SlNilSentinel"))
            {
                sentinel(node);
            }
            return true;
        }

    private:
        bool on(std::string_view name) const { return (mEnabled & ALScriptLintPass::bit(name)) != 0; }

        bool number(Luau::AstExpr* e) { return primitive(e, Luau::PrimitiveType::Number); }

        // Whether the check found an expression of one primitive type, and
        // nothing else: not number?, whose nil is false; a string's
        // literal, typed as itself, is a string. A local the check says is
        // any -- unannotated, in the old solver's nonstrict mode -- by what
        // it is given.
        bool primitive(Luau::AstExpr* e, Luau::PrimitiveType::Type want)
        {
            const Luau::TypeId* type = mChecked ? mChecked->astTypes.find(e) : nullptr;
            if (!type)
            {
                return false;
            }
            const Luau::TypeId followed = Luau::follow(*type);
            if (const auto* prim = Luau::get<Luau::PrimitiveType>(followed))
            {
                return prim->type == want;
            }
            if (const auto* single = Luau::get<Luau::SingletonType>(followed))
            {
                return want == Luau::PrimitiveType::String && Luau::get<Luau::StringSingleton>(single);
            }
            auto* local = e->as<Luau::AstExprLocal>();
            return local && Luau::get<Luau::AnyType>(followed) && primitiveLocal(local->local, want);
        }

        bool primitiveLocal(Luau::AstLocal* local, Luau::PrimitiveType::Type want)
        {
            const auto asked = std::make_pair(local, want);
            if (const auto known = mPrimitiveLocals.find(asked); known != mPrimitiveLocals.end())
            {
                return known->second;
            }
            // Taken as none while its own are asked about, which a loop of
            // locals given each other cannot then make one.
            mPrimitiveLocals[asked] = false;
            bool is = want == Luau::PrimitiveType::Number && mLocals.counters.contains(local);
            if (!is && !mLocals.unknown.contains(local) && !mLocals.counters.contains(local) && !local->annotation)
            {
                const auto given = mLocals.given.find(local);
                is = given != mLocals.given.end() && !given->second.empty();
                for (size_t i = 0; is && i < given->second.size(); ++i)
                {
                    is = primitive(given->second[i], want);
                }
            }
            mPrimitiveLocals[asked] = is;
            return is;
        }

        // What a find answers where it finds nothing: SLua's own answer
        // nil -- ll's, table.find, string.find -- and llcompat's -1, as
        // LSL's did.
        struct Find
        {
            std::string function;
            bool        nil;
        };

        // The find an expression is, inside brackets or not: a call to
        // one, or a local given only calls to finds that answer alike --
        // and, for one that answers -1, never nil before it is given one.
        std::optional<Find> find(Luau::AstExpr* e)
        {
            while (auto* group = e->as<Luau::AstExprGroup>())
            {
                e = group->expr;
            }
            if (auto* local = e->as<Luau::AstExprLocal>())
            {
                return findLocal(local->local);
            }
            auto* call   = e->as<Luau::AstExprCall>();
            auto* callee = call ? call->func->as<Luau::AstExprIndexName>() : nullptr;
            if (!callee)
            {
                return std::nullopt;
            }
            const std::string_view name = callee->index.value;
            const auto*            lib  = callee->expr->as<Luau::AstExprGlobal>();
            const std::string_view from = lib && callee->op == '.' ? lib->name.value : "";
            if (from == "ll" || from == "llcompat")
            {
                // LSL's that answer an index or -1, and whose ll answers one
                // from 1 or nil: not llFindNotecardTextSync, whose indexes
                // are in the list it answers.
                const std::string lsl = "ll" + std::string(name);
                const auto*       row = ALLSLTraits::of(lsl.c_str());
                if (row && (row->slua & ALLSLTraits::SluaIndexResult) && ALLSLTraits::atLeastMinusOne(lsl.c_str()))
                {
                    return Find{ std::string(from) + "." + std::string(name), from == "ll" };
                }
                return std::nullopt;
            }
            if (name == "find" && (from == "table" || from == "string" || (callee->op == ':' && primitive(callee->expr, Luau::PrimitiveType::String))))
            {
                return Find{ from == "table" ? "table.find" : "string.find", true };
            }
            return std::nullopt;
        }

        std::optional<Find> findLocal(Luau::AstLocal* local)
        {
            if (const auto known = mFindLocals.find(local); known != mFindLocals.end())
            {
                return known->second;
            }
            mFindLocals[local] = std::nullopt;
            const auto          given = mLocals.given.find(local);
            std::optional<Find> out;
            if (given != mLocals.given.end() && !mLocals.unknown.contains(local) && !mLocals.counters.contains(local) &&
                !mLocals.compounded.contains(local))
            {
                for (Luau::AstExpr* value : given->second)
                {
                    const std::optional<Find> each = find(value);
                    if (!each || (out && out->nil != each->nil))
                    {
                        out.reset();
                        break;
                    }
                    out = out ? out : each;
                }
                if (out && !out->nil && mLocals.bare.contains(local))
                {
                    out.reset();
                }
            }
            mFindLocals[local] = out;
            return out;
        }

        // What the scripter wrote the find as, short: a local's name, or a
        // call's function and (...).
        std::string subject(Luau::AstExpr* e) const
        {
            while (auto* group = e->as<Luau::AstExprGroup>())
            {
                e = group->expr;
            }
            if (const auto* local = e->as<Luau::AstExprLocal>())
            {
                return local->local->name.value;
            }
            if (const auto* call = e->as<Luau::AstExprCall>())
            {
                return text(call->func->location) + "(...)";
            }
            return text(e->location);
        }

        static bool constant(Luau::AstExpr* e, double value)
        {
            while (auto* group = e->as<Luau::AstExprGroup>())
            {
                e = group->expr;
            }
            if (const auto* negated = e->as<Luau::AstExprUnary>(); negated && negated->op == Luau::AstExprUnary::Op::Minus)
            {
                return value != 0 && constant(negated->expr, -value);
            }
            const auto* number = e->as<Luau::AstExprConstantNumber>();
            return number && number->value == value;
        }

        static bool isNil(Luau::AstExpr* e)
        {
            while (auto* group = e->as<Luau::AstExprGroup>())
            {
                e = group->expr;
            }
            return e->is<Luau::AstExprConstantNil>();
        }

        // A find compared with what it never answers: SLua's with -1 --
        // equal, not equal, or LSL's order against 0 or -1, an error at
        // nil -- and llcompat's with nil. The args after the words say
        // where the find is, for the fix.
        void sentinel(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            Op op = node->op;
            if (op != Op::CompareEq && op != Op::CompareNe && op != Op::CompareLt && op != Op::CompareLe && op != Op::CompareGt &&
                op != Op::CompareGe)
            {
                return;
            }
            Luau::AstExpr*      side  = node->left;
            Luau::AstExpr*      other = node->right;
            std::optional<Find> found = find(side);
            if (!found)
            {
                found = find(other);
                if (!found)
                {
                    return;
                }
                std::swap(side, other);
                // As though the find were on the left.
                switch (op)
                {
                    case Op::CompareLt: op = Op::CompareGt; break;
                    case Op::CompareGt: op = Op::CompareLt; break;
                    case Op::CompareLe: op = Op::CompareGe; break;
                    case Op::CompareGe: op = Op::CompareLe; break;
                    default: break;
                }
            }
            // Whether it asks if the find found nothing, else something;
            // and whether it asks it by order, else by equality.
            bool none  = false;
            bool order = false;
            bool asks  = false;
            if (found->nil)
            {
                const bool minus_one = constant(other, -1);
                const bool zero      = constant(other, 0);
                if ((op == Op::CompareEq || op == Op::CompareNe) && minus_one)
                {
                    asks = true;
                    none = op == Op::CompareEq;
                }
                else if ((op == Op::CompareLt && zero) || (op == Op::CompareLe && minus_one) || (op == Op::CompareGe && zero) ||
                         (op == Op::CompareGt && minus_one))
                {
                    asks  = true;
                    order = true;
                    none  = op == Op::CompareLt || op == Op::CompareLe;
                }
            }
            else if ((op == Op::CompareEq || op == Op::CompareNe) && isNil(other))
            {
                asks = true;
                none = op == Op::CompareEq;
            }
            if (!asks)
            {
                return;
            }
            const char*           answers = found->nil ? "nil" : "-1";
            const Luau::Location& at      = side->location;
            std::vector<std::string> args = { subject(side),
                                              found->function,
                                              answers,
                                              found->nil ? "-1" : "nil",
                                              std::string(none ? "== " : "~= ") + answers,
                                              std::to_string(at.begin.line),
                                              std::to_string(at.begin.column),
                                              std::to_string(at.end.line),
                                              std::to_string(at.end.column) };
            if (order)
            {
                problem(node->location, "LuauLintSlNilSentinelOrder",
                        "Comparing [1] with a number is an error where [2] finds nothing: it answers nil there, not -1. [1] [5] asks the same",
                        std::move(args), "SlNilSentinel");
            }
            else if (none)
            {
                problem(node->location, "LuauLintSlNilSentinel",
                        "[1] == [4] never holds: [2] answers [3] where it finds nothing, not [4]. [1] == [3] asks whether it found nothing",
                        std::move(args), "SlNilSentinel");
            }
            else
            {
                problem(node->location, "LuauLintSlNilSentinelAlways",
                        "[1] ~= [4] always holds: [2] answers [3] where it finds nothing, not [4]. [1] ~= [3] asks whether it found something",
                        std::move(args), "SlNilSentinel");
            }
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
        boost::unordered_flat_map<std::pair<Luau::AstLocal*, Luau::PrimitiveType::Type>, bool> mPrimitiveLocals;
        boost::unordered_flat_map<Luau::AstLocal*, std::optional<Find>>                      mFindLocals;
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
    if (enabled & (bit("SlNumberTruth") | bit("SlNilSentinel")))
    {
        module.root->visit(&locals);
    }
    Pass pass(source, checked, locals, enabled, fatal, all_errors, out);
    module.root->visit(&pass);
}

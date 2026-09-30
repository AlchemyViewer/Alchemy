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

#include "alscriptfixes.h"
#include "allsltraits.h"

#include "Luau/Ast.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>
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
        // t == {}: a table built there equals no other.
        { "SlTableCompare", true, Severity::Error, true, true, nullptr },
        // t[0], for i = 0, #t - 1, string.sub(s, 0, n), ll.X(s, 0): LSL
        // counted from 0.
        { "SlZeroIndex", true, Severity::Warning, true, true, nullptr },
        // llcompat.X where ll.X means the same: its fix is ll's.
        { "SlCompatCall", true, Severity::Note, true, true, nullptr },
        // b == 1, where b is a boolean: LSL's truths were numbers.
        { "SlBooleanNumber", true, Severity::Error, true, true, nullptr },
        // x = 0 or function f() making a global; a function f() in a nested
        // scope, lute's global_function_in_scope.
        { "SlGlobalAssign", true, Severity::Warning, true, true, nullptr },
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

    bool comparison(Luau::AstExprBinary::Op op)
    {
        using Op = Luau::AstExprBinary::Op;
        return op == Op::CompareEq || op == Op::CompareNe || op == Op::CompareLt || op == Op::CompareLe || op == Op::CompareGt ||
               op == Op::CompareGe;
    }

    // The comparison that asks the same with its sides the other way round.
    Luau::AstExprBinary::Op mirrored(Luau::AstExprBinary::Op op)
    {
        using Op = Luau::AstExprBinary::Op;
        switch (op)
        {
            case Op::CompareLt: return Op::CompareGt;
            case Op::CompareGt: return Op::CompareLt;
            case Op::CompareLe: return Op::CompareGe;
            case Op::CompareGe: return Op::CompareLe;
            default: return op;
        }
    }

    Luau::AstExpr* unbracketed(Luau::AstExpr* e)
    {
        while (auto* group = e->as<Luau::AstExprGroup>())
        {
            e = group->expr;
        }
        return e;
    }

    // A number written as one, bracketed or negated or not.
    std::optional<double> literal(Luau::AstExpr* e)
    {
        e = unbracketed(e);
        if (const auto* negated = e->as<Luau::AstExprUnary>(); negated && negated->op == Luau::AstExprUnary::Op::Minus)
        {
            const std::optional<double> inner = literal(negated->expr);
            return inner ? std::optional<double>(-*inner) : std::nullopt;
        }
        const auto* number = e->as<Luau::AstExprConstantNumber>();
        return number ? std::optional<double>(number->value) : std::nullopt;
    }

    bool constant(Luau::AstExpr* e, double value)
    {
        const std::optional<double> number = literal(e);
        return number && *number == value;
    }

    bool isNil(Luau::AstExpr* e) { return unbracketed(e)->is<Luau::AstExprConstantNil>(); }

    // A number as a script writes it, where it is a whole one.
    std::optional<std::string> whole(double value)
    {
        if (value != std::floor(value) || std::fabs(value) > 1e15)
        {
            return std::nullopt;
        }
        return std::to_string(static_cast<long long>(value));
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
        // Indexes written to, and tables given something at 0, by an
        // assignment or where they are built: a table keyed by number, 0
        // among them, as a channel may be.
        boost::unordered_flat_set<const Luau::AstExprIndexExpr*> written;
        boost::unordered_flat_set<Luau::AstLocal*>                zeroKeyed;

        void writes(Luau::AstExpr* var)
        {
            if (const auto* index = var->as<Luau::AstExprIndexExpr>())
            {
                written.insert(index);
                if (const auto* table = index->expr->as<Luau::AstExprLocal>(); table && constant(index->index, 0))
                {
                    zeroKeyed.insert(table->local);
                }
            }
        }

        void builds(Luau::AstLocal* local, Luau::AstExpr* value)
        {
            if (const auto* table = unbracketed(value)->as<Luau::AstExprTable>())
            {
                for (const Luau::AstExprTable::Item& item : table->items)
                {
                    if (item.kind == Luau::AstExprTable::Item::Kind::General && constant(item.key, 0))
                    {
                        zeroKeyed.insert(local);
                    }
                }
            }
        }

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
                    builds(node->vars.data[i], node->values.data[i]);
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
                writes(node->vars.data[i]);
                if (auto* local = node->vars.data[i]->as<Luau::AstExprLocal>())
                {
                    if (i < node->values.size && (i + 1 < node->values.size || node->vars.size == 1))
                    {
                        given[local->local].push_back(node->values.data[i]);
                        builds(local->local, node->values.data[i]);
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
            writes(node->var);
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

    // Where a local is read in a stretch of the tree: every time, and each
    // time it is what a table is indexed by, the first such kept.
    class Uses final : public Luau::AstVisitor
    {
    public:
        explicit Uses(Luau::AstLocal* local) : mLocal(local) {}

        size_t                     all     = 0;
        size_t                     indexes = 0;
        const Luau::AstExprIndexExpr* first  = nullptr;

        bool visit(Luau::AstExprLocal* node) override
        {
            all += node->local == mLocal;
            return true;
        }
        bool visit(Luau::AstExprIndexExpr* node) override
        {
            const auto* index = node->index->as<Luau::AstExprLocal>();
            if (index && index->local == mLocal)
            {
                ++indexes;
                first = first ? first : node;
            }
            return true;
        }

    private:
        Luau::AstLocal* mLocal;
    };

    // Every place each global is named, in the order of the text, and
    // whether the script names _G, through which any global may be read.
    class Globals final : public Luau::AstVisitor
    {
    public:
        boost::unordered_flat_map<std::string, std::vector<Luau::Location>> named;
        bool                                                                 viaG = false;

        bool visit(Luau::AstExprGlobal* node) override
        {
            named[node->name.value].push_back(node->location);
            viaG = viaG || std::string_view(node->name.value) == "_G";
            return true;
        }
    };

    // What the pass asks the check an expression is.
    enum class Kind : U8
    {
        Number,
        String,
        Boolean,
        Table,
        List,
        Vector,
    };

    class Pass final : public Luau::AstVisitor
    {
    public:
        Pass(std::string_view source, const Luau::AstStatBlock* root, const Luau::Module* checked, const Locals& locals, const Globals& globals,
             uint64_t enabled, uint64_t fatal, bool all_errors, ALScriptProblems& out)
            : mSource(source), mRoot(root), mChecked(checked), mLocals(locals), mGlobals(globals), mEnabled(enabled), mFatal(fatal),
              mAllErrors(all_errors), mOut(out)
        {
            mStarts.push_back(0);
            for (size_t at = source.find('\n'); at != std::string_view::npos; at = source.find('\n', at + 1))
            {
                mStarts.push_back(at + 1);
            }
            // The names the script is given, which are not its to make:
            // those of every scope above its own.
            if (checked)
            {
                if (const Luau::ScopePtr own = checked->getModuleScope())
                {
                    for (Luau::ScopePtr scope = own->parent; scope; scope = scope->parent)
                    {
                        for (const auto& [symbol, binding] : scope->bindings)
                        {
                            mGiven.insert(symbol.c_str());
                        }
                    }
                }
            }
        }

        // --- SlGlobalAssign: a global made where a local would do ------------

        bool visit(Luau::AstStatBlock* node) override
        {
            if (on("SlGlobalAssign"))
            {
                for (Luau::AstStat* stat : node->body)
                {
                    node == mRoot ? topGlobal(stat) : nestedFunction(node, stat);
                }
            }
            return true;
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
            if (on("SlTableCompare"))
            {
                tableCompare(node);
            }
            if (on("SlZeroIndex"))
            {
                zeroFound(node);
            }
            if (on("SlCompatCall"))
            {
                compatCompare(node);
            }
            if (on("SlBooleanNumber"))
            {
                booleanNumber(node);
            }
            return true;
        }

        // --- SlZeroIndex: counted from 0, as LSL counted -------------------

        bool visit(Luau::AstExprIndexExpr* node) override
        {
            const auto* table = node->expr->as<Luau::AstExprLocal>();
            if (on("SlZeroIndex") && constant(node->index, 0) && !mLocals.written.contains(node) &&
                !(table && mLocals.zeroKeyed.contains(table->local)) && is(node->expr, Kind::List))
            {
                const std::string list  = bracketed(node->expr);
                ALScriptProblem&  said  = problem(node->location, "LuauLintSlZeroIndex",
                                                  "[1][0] is nothing: Luau's lists count from 1, where LSL's counted from 0. [1][1] is the first",
                                                  { list }, "SlZeroIndex");
                offer(said, list + "[1]", { edit(node->index->location, "1") }, false);
            }
            return true;
        }

        bool visit(Luau::AstStatFor* node) override
        {
            if (on("SlZeroIndex"))
            {
                zeroLoop(node);
            }
            return true;
        }

        bool visit(Luau::AstExprCall* node) override
        {
            if (on("SlZeroIndex"))
            {
                zeroSub(node);
                zeroArg(node);
            }
            if (on("SlCompatCall"))
            {
                compatCall(node, mStatements.contains(node));
            }
            return true;
        }

        // A call whose answer nobody reads.
        bool visit(Luau::AstStatExpr* node) override
        {
            if (auto* call = node->expr->as<Luau::AstExprCall>())
            {
                mStatements.insert(call);
            }
            return true;
        }

    private:
        bool on(std::string_view name) const { return (mEnabled & ALScriptLintPass::bit(name)) != 0; }

        bool number(Luau::AstExpr* e) { return is(e, Kind::Number); }

        // Whether a type is of a kind, and nothing else: not number?, whose
        // nil is false. A string's or a boolean's literal, typed as itself,
        // is one; a list is a table with number keys and no fields.
        static bool of(Luau::TypeId type, Kind want)
        {
            const Luau::TypeId followed = Luau::follow(type);
            if (const auto* prim = Luau::get<Luau::PrimitiveType>(followed))
            {
                return (want == Kind::Number && prim->type == Luau::PrimitiveType::Number) ||
                       (want == Kind::String && prim->type == Luau::PrimitiveType::String) ||
                       (want == Kind::Boolean && prim->type == Luau::PrimitiveType::Boolean);
            }
            if (const auto* single = Luau::get<Luau::SingletonType>(followed))
            {
                return (want == Kind::String && Luau::get<Luau::StringSingleton>(single)) ||
                       (want == Kind::Boolean && Luau::get<Luau::BooleanSingleton>(single));
            }
            if (const auto* table = Luau::get<Luau::TableType>(followed))
            {
                return want == Kind::Table ||
                       (want == Kind::List && table->props.empty() && table->indexer && of(table->indexer->indexType, Kind::Number));
            }
            const auto* extern_type = Luau::get<Luau::ExternType>(followed);
            return want == Kind::Vector && extern_type && extern_type->name == "vector";
        }

        // Whether the check found an expression of a kind. A local the
        // check says is any -- unannotated, in the old solver's nonstrict
        // mode -- by what it is given.
        bool is(Luau::AstExpr* e, Kind want)
        {
            const Luau::TypeId* type = mChecked ? mChecked->astTypes.find(e) : nullptr;
            if (!type)
            {
                return false;
            }
            if (of(*type, want))
            {
                return true;
            }
            auto* local = e->as<Luau::AstExprLocal>();
            return local && Luau::get<Luau::AnyType>(Luau::follow(*type)) && localIs(local->local, want);
        }

        bool localIs(Luau::AstLocal* local, Kind want)
        {
            const auto asked = std::make_pair(local, want);
            if (const auto known = mLocalKinds.find(asked); known != mLocalKinds.end())
            {
                return known->second;
            }
            // Taken as none while its own are asked about, which a loop of
            // locals given each other cannot then make one.
            mLocalKinds[asked] = false;
            bool yes = want == Kind::Number && mLocals.counters.contains(local);
            if (!yes && !mLocals.unknown.contains(local) && !mLocals.counters.contains(local) && !local->annotation)
            {
                const auto given = mLocals.given.find(local);
                yes = given != mLocals.given.end() && !given->second.empty();
                for (size_t i = 0; yes && i < given->second.size(); ++i)
                {
                    yes = is(given->second[i], want);
                }
            }
            mLocalKinds[asked] = yes;
            return yes;
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
            e = unbracketed(e);
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
            if (name == "find" && (from == "table" || from == "string" || (callee->op == ':' && is(callee->expr, Kind::String))))
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
            e = unbracketed(e);
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



        // A find compared with what it never answers: SLua's with -1 --
        // equal, not equal, or LSL's order against 0 or -1, an error at
        // nil -- and llcompat's with nil. The args after the words say
        // where the find is, for the fix.
        void sentinel(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            Op op = node->op;
            if (!comparison(op))
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
                op = mirrored(op);
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

        // A table built where it is compared, which nothing equals: an
        // empty one asked of as LSL asked `l == []`, fixed as whether the
        // table is empty -- by its length where it is a list -- or with
        // items, which LSL compared by their count alone.
        void tableCompare(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            if (node->op != Op::CompareEq && node->op != Op::CompareNe)
            {
                return;
            }
            Luau::AstExpr*            other = node->left;
            const Luau::AstExprTable* built = unbracketed(node->right)->as<Luau::AstExprTable>();
            if (!built)
            {
                other = node->right;
                built = unbracketed(node->left)->as<Luau::AstExprTable>();
            }
            if (!built)
            {
                return;
            }
            other                     = unbracketed(other);
            const std::string subject = text(other->location);
            const bool        table   = !is(other, Kind::Number) && !is(other, Kind::String) && !is(other, Kind::Boolean) && !is(other, Kind::Vector);
            if (built->items.size != 0 || !table)
            {
                problem(node->location, "LuauLintSlTableCompareItems",
                        "[1] can never equal a table built where it is compared: a new table is equal to no other. LSL compared lists by "
                        "their lengths alone, which #[1] gives",
                        { subject }, "SlTableCompare");
                return;
            }
            const bool        equal = node->op == Op::CompareEq;
            const std::string asked = is(other, Kind::List) ? "#" + bracketed(other) + (equal ? " == 0" : " > 0")
                                                           : "next(" + subject + (equal ? ") == nil" : ") ~= nil");
            ALScriptProblem&  said  = equal ? problem(node->location, "LuauLintSlTableCompare",
                                                      "[1] == {} is always false: {} is a new table, equal to no other. [2] asks whether [1] is empty",
                                                      { subject, asked }, "SlTableCompare")
                                            : problem(node->location, "LuauLintSlTableCompareAlways",
                                                      "[1] ~= {} is always true: {} is a new table, equal to no other. [2] asks whether [1] has anything in it",
                                                      { subject, asked }, "SlTableCompare");
            offer(said, asked, { edit(node->location, asked) }, false);
        }



        // A counting loop from 0 to a length less 1, whose counter indexes
        // a table: LSL's walk of a list, which in Luau reads nothing at 0
        // and never the last. A counter read otherwise too -- given to
        // llcompat, which counts from 0 -- may be right as it is: fixed
        // only where every read of it indexes.
        void zeroLoop(Luau::AstStatFor* node)
        {
            const auto* less   = unbracketed(node->to)->as<Luau::AstExprBinary>();
            const auto* length = less && less->op == Luau::AstExprBinary::Sub && constant(less->right, 1)
                                   ? unbracketed(less->left)->as<Luau::AstExprUnary>()
                                   : nullptr;
            if (!constant(node->from, 0) || (node->step && !constant(node->step, 1)) || !length || length->op != Luau::AstExprUnary::Op::Len)
            {
                return;
            }
            Uses uses(node->var);
            node->body->visit(&uses);
            if (!uses.first)
            {
                return;
            }
            const std::string count = text(length->location);
            const std::string var   = node->var->name.value;
            ALScriptProblem&  said  = problem(node->location, "LuauLintSlZeroIndexLoop",
                                              "This loop counts from 0 to [1] - 1, as LSL's lists did, but Luau's count from 1: [2] reads nothing at "
                                              "0 and never the last. for [3] = 1, [1] counts as Luau does",
                                              { count, text(uses.first->location), var }, "SlZeroIndex");
            if (uses.all == uses.indexes)
            {
                offer(said, "for " + var + " = 1, " + count, { edit(node->from->location, "1"), edit(node->to->location, count) }, false);
            }
        }

        // string.sub(s, 0, n), or s:sub(0, n): Luau's takes 0 as 1, and so
        // ends a character sooner than LSL's llGetSubString(s, 0, n) did.
        // Fixed where the end's place is plain: a number, or a length less
        // one or more.
        void zeroSub(Luau::AstExprCall* node)
        {
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            if (!callee || std::string_view(callee->index.value) != "sub")
            {
                return;
            }
            const auto*  lib    = callee->expr->as<Luau::AstExprGlobal>();
            const bool   method = callee->op == ':' && is(callee->expr, Kind::String);
            const size_t first  = method ? 0 : 1;
            if ((!method && !(callee->op == '.' && lib && std::string_view(lib->name.value) == "string")) || node->args.size != first + 2 ||
                !constant(node->args.data[first], 0))
            {
                return;
            }
            Luau::AstExpr*             end = node->args.data[first + 1];
            std::optional<std::string> after;
            if (const std::optional<double> at = literal(end))
            {
                after = *at < 0 ? std::optional<std::string>(text(end->location)) : whole(*at + 1);
            }
            else if (const auto* less = unbracketed(end)->as<Luau::AstExprBinary>(); less && less->op == Luau::AstExprBinary::Sub)
            {
                const std::optional<double>      by    = literal(less->right);
                const std::optional<std::string> fewer = by && *by > 1 ? whole(*by - 1) : std::nullopt;
                if (by && *by == 1)
                {
                    after = text(less->left->location);
                }
                else if (fewer)
                {
                    after = text(less->left->location) + " - " + *fewer;
                }
            }
            const std::string function = text(node->func->location);
            ALScriptProblem&  said     = problem(node->location, "LuauLintSlZeroIndexSub",
                                                 "[1] counts from 1 and takes 0 as 1, so it ends a character sooner than LSL's llGetSubString with the "
                                                 "same numbers",
                                                 { function }, "SlZeroIndex");
            if (after)
            {
                std::string now = function + "(";
                for (size_t i = 0; i < node->args.size; ++i)
                {
                    now += (i ? ", " : "") + (i == first ? std::string("1") : i == first + 1 ? *after : text(node->args.data[i]->location));
                }
                offer(said, now + ")", { edit(node->args.data[first]->location, "1"), edit(end->location, *after) }, false);
            }
        }

        // An index of ll's given as 0, which SLua's ll counts from 1. Where
        // every index the call is given is a number, those from 0 up are
        // moved by one, as the converter moves them; one from the end stays.
        void zeroArg(Luau::AstExprCall* node)
        {
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            const auto* lib    = callee ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (!lib || callee->op != '.' || std::string_view(lib->name.value) != "ll")
            {
                return;
            }
            const std::string          lsl = "ll" + std::string(callee->index.value);
            const ALLSLTraits::Trait*  row = ALLSLTraits::of(lsl.c_str());
            if (!row || !(row->slua & ALLSLTraits::SluaIndexArgs))
            {
                return;
            }
            bool                      zero  = false;
            bool                      plain = true;
            std::vector<ALScriptEdit> edits;
            std::vector<std::string>  written;
            for (size_t i = 0; i < node->args.size; ++i)
            {
                Luau::AstExpr* arg = node->args.data[i];
                std::string    now = text(arg->location);
                if (i < 16 && (row->sluaIndexArgs & (1u << i)))
                {
                    const std::optional<double>      at    = literal(arg);
                    const std::optional<std::string> moved = at && *at >= 0 ? whole(*at + 1) : std::nullopt;
                    zero  = zero || (at && *at == 0);
                    plain = plain && at && (*at < 0 || moved);
                    if (moved)
                    {
                        now = *moved;
                        edits.push_back(edit(arg->location, now));
                    }
                }
                written.push_back(std::move(now));
            }
            if (!zero)
            {
                return;
            }
            const std::string function = text(node->func->location);
            ALScriptProblem&  said = problem(node->location, "LuauLintSlZeroIndexArg", "[1] counts from 1 in SLua, as Luau does: its first is 1, not 0",
                                             { function }, "SlZeroIndex");
            if (plain)
            {
                std::string now = function + "(";
                for (size_t i = 0; i < written.size(); ++i)
                {
                    now += (i ? ", " : "") + written[i];
                }
                offer(said, now + ")", std::move(edits), false);
            }
        }

        // A find compared with 0, which SLua's never answers: LSL's first.
        void zeroFound(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            if (node->op != Op::CompareEq && node->op != Op::CompareNe)
            {
                return;
            }
            Luau::AstExpr*      side  = node->left;
            Luau::AstExpr*      zero  = node->right;
            std::optional<Find> found = constant(zero, 0) ? find(side) : std::nullopt;
            if (!found)
            {
                std::swap(side, zero);
                found = constant(zero, 0) ? find(side) : std::nullopt;
            }
            if (!found || !found->nil)
            {
                return;
            }
            const std::string subject = this->subject(side);
            ALScriptProblem&  said    = problem(node->location, "LuauLintSlZeroIndexFound",
                                                "[1] is never 0: [2] answers from 1, where LSL's answered from 0, so its first is 1",
                                                { subject, found->function }, "SlZeroIndex");
            offer(said, subject + (node->op == Op::CompareEq ? " == 1" : " ~= 1"), { edit(zero->location, "1") }, false);
        }

        // --- SlCompatCall: llcompat's where ll's means the same --------------

        // What ll's row says of an llcompat call, and the call written as
        // ll's: llcompat put as ll, each index from 0 up moved by one, as
        // the assistant moves them. Nothing where ll lacks the function or
        // deprecates it, gives booleans in a list, or an index is not a
        // number.
        struct Compat
        {
            const ALLSLTraits::Trait* row = nullptr;
            // Its name without ll, and as LSL's: Say, llSay.
            std::string               name;
            std::string               lsl;
            std::string               written;
            std::vector<ALScriptEdit> edits;
        };

        std::optional<Compat> compat(Luau::AstExpr* e)
        {
            auto*       call   = unbracketed(e)->as<Luau::AstExprCall>();
            const auto* callee = call ? call->func->as<Luau::AstExprIndexName>() : nullptr;
            const auto* lib    = callee ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (!lib || callee->op != '.' || std::string_view(lib->name.value) != "llcompat")
            {
                return std::nullopt;
            }
            Compat out;
            out.name = callee->index.value;
            out.lsl  = "ll" + out.name;
            out.row  = ALLSLTraits::of(out.lsl.c_str());
            constexpr U8 lacking = ALLSLTraits::SluaRemoved | ALLSLTraits::SluaAbsent | ALLSLTraits::SluaDeprecated | ALLSLTraits::SluaBoolList;
            if (!out.row || (out.row->slua & lacking))
            {
                return std::nullopt;
            }
            out.written = "ll." + out.name + "(";
            out.edits.push_back(edit(lib->location, "ll"));
            for (size_t i = 0; i < call->args.size; ++i)
            {
                Luau::AstExpr* arg = call->args.data[i];
                std::string    now = text(arg->location);
                if ((out.row->slua & ALLSLTraits::SluaIndexArgs) && i < 16 && (out.row->sluaIndexArgs & (1u << i)))
                {
                    const std::optional<double> at = literal(arg);
                    if (!at)
                    {
                        return std::nullopt;
                    }
                    if (*at >= 0)
                    {
                        const std::optional<std::string> moved = whole(*at + 1);
                        if (!moved)
                        {
                            return std::nullopt;
                        }
                        now = *moved;
                        out.edits.push_back(edit(arg->location, now));
                    }
                }
                out.written += (i ? ", " : "") + now;
            }
            out.written += ")";
            return out;
        }

        // The call as its fix names it: whole where an index moved, else
        // its function and (...).
        static std::string shown(const Compat& found)
        {
            return found.edits.size() > 1 ? found.written : "ll." + found.name + "(...)";
        }

        void compatSaid(const Luau::Location& where, const Compat& found, const std::string& now, std::vector<ALScriptEdit> edits)
        {
            ALScriptProblem& said = problem(where, "LuauLintSlCompatCall", "[1] is LSL's, kept for scripts moved from it; ll's means the same here: [2]",
                                            { "llcompat." + found.name, now }, "SlCompatCall");
            offer(said, now, std::move(edits), true);
        }

        // A call ll's answers as LSL's did, or whose answer nobody reads. A
        // boolean's or a find's answer is read otherwise, and is asked of
        // where it is compared.
        void compatCall(Luau::AstExprCall* node, bool unread)
        {
            std::optional<Compat> found = compat(node);
            if (!found || (!unread && (found->row->slua & (ALLSLTraits::SluaBool | ALLSLTraits::SluaIndexResult))))
            {
                return;
            }
            compatSaid(node->location, *found, shown(*found), std::move(found->edits));
        }

        // A boolean of llcompat's compared with 1 or 0, which ll's answers
        // as true or false; a find's with -1 or 0, which ll's answers as nil
        // or from 1.
        void compatCompare(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            if (!comparison(node->op))
            {
                return;
            }
            Luau::AstExpr*        side  = node->left;
            Luau::AstExpr*        other = node->right;
            std::optional<Compat> found = compat(side);
            Op                    op    = node->op;
            if (!found)
            {
                std::swap(side, other);
                found = compat(side);
                op    = mirrored(op);
            }
            if (!found)
            {
                return;
            }
            const std::optional<double> value = literal(other);
            const bool                  equal = op == Op::CompareEq || op == Op::CompareNe;
            std::string                 ask;
            bool                        negate = false;
            if ((found->row->slua & ALLSLTraits::SluaBool) && equal && value && (*value == 0 || *value == 1))
            {
                // == 1 and ~= 0 ask whether it is true.
                negate = (*value == 1) != (op == Op::CompareEq);
            }
            else if ((found->row->slua & ALLSLTraits::SluaIndexResult) && ALLSLTraits::atLeastMinusOne(found->lsl.c_str()) && value)
            {
                if (equal && (*value == -1 || *value == 0))
                {
                    ask = std::string(op == Op::CompareEq ? " == " : " ~= ") + (*value == -1 ? "nil" : "1");
                }
                else if ((op == Op::CompareLt && *value == 0) || (op == Op::CompareLe && *value == -1))
                {
                    ask = " == nil";
                }
                else if ((op == Op::CompareGe && *value == 0) || (op == Op::CompareGt && *value == -1))
                {
                    ask = " ~= nil";
                }
                else
                {
                    return;
                }
            }
            else
            {
                return;
            }
            // The comparison taken out round the call, which stays where it
            // is, llcompat's edits inside it.
            const Luau::Location& at    = node->location;
            const Luau::Location& call  = side->location;
            std::vector<ALScriptEdit> edits;
            const bool                left = side == node->left;
            if (left)
            {
                if (negate)
                {
                    edits.push_back(edit(Luau::Location(call.begin, call.begin), "not "));
                }
                edits.insert(edits.end(), found->edits.begin(), found->edits.end());
                edits.push_back(edit(Luau::Location(call.end, at.end), ask));
            }
            else
            {
                edits.push_back(edit(Luau::Location(at.begin, call.begin), negate ? "not " : ""));
                edits.insert(edits.end(), found->edits.begin(), found->edits.end());
                if (!ask.empty())
                {
                    edits.push_back(edit(Luau::Location(at.end, at.end), ask));
                }
            }
            compatSaid(node->location, *found, (negate ? "not " : "") + shown(*found) + ask, std::move(edits));
        }

        // --- SlBooleanNumber: a truth compared with a number -----------------

        // A boolean compared with a number, which it never equals: LSL's
        // TRUE and FALSE were 1 and 0. Or an if-then-else of 1 and 0 made
        // only to be compared with one of them, which asks no more than
        // its condition.
        void booleanNumber(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            if (node->op != Op::CompareEq && node->op != Op::CompareNe)
            {
                return;
            }
            Luau::AstExpr*        side  = node->left;
            Luau::AstExpr*        other = node->right;
            std::optional<double> value = literal(other);
            if (!value)
            {
                std::swap(side, other);
                value = literal(other);
            }
            if (!value)
            {
                return;
            }
            const bool equal = node->op == Op::CompareEq;
            if (auto* choice = unbracketed(side)->as<Luau::AstExprIfElse>())
            {
                const std::optional<double> yes = literal(choice->trueExpr);
                const std::optional<double> no  = literal(choice->falseExpr);
                if (!yes || !no || *yes == *no || (*yes != 0 && *yes != 1) || (*no != 0 && *no != 1) || (*value != 0 && *value != 1))
                {
                    return;
                }
                // Whether it holds where the condition does.
                const bool        with = (*yes == *value) == equal;
                const std::string now  = with ? text(choice->condition->location) : "not " + bracketed(choice->condition);
                ALScriptProblem&  said = problem(node->location, "LuauLintSlBooleanNumberChoice",
                                                 "[1] asks no more than [2]: its 1 or 0 is made only to be compared", { text(node->location), now },
                                                 "SlBooleanNumber", Severity::Note);
                offer(said, now, { edit(node->location, now) }, false);
                return;
            }
            if (!is(side, Kind::Boolean))
            {
                return;
            }
            const std::string subject = text(side->location);
            ALScriptProblem&  said    = problem(node->location, equal ? "LuauLintSlBooleanNumber" : "LuauLintSlBooleanNumberAlways",
                                                equal ? "[1] == [2] never holds: [1] is true or false, never a number as LSL's truths were"
                                                      : "[1] ~= [2] always holds: [1] is true or false, never a number as LSL's truths were",
                                                { subject, text(other->location) }, "SlBooleanNumber");
            if (*value == 0 || *value == 1)
            {
                // == 1 and ~= 0 ask whether it is true.
                const std::string now = (*value == 1) == equal ? subject : "not " + bracketed(side);
                offer(said, now, { edit(node->location, now) }, false);
            }
        }

        // A global's name the script makes: not one it is given, nor _G.
        bool made(const Luau::AstExprGlobal* global) const
        {
            return global && !mGiven.contains(global->name.value) && std::string_view(global->name.value) != "_G";
        }

        const std::vector<Luau::Location>& named(const Luau::AstExprGlobal* global) const
        {
            return mGlobals.named.at(global->name.value);
        }

        // Whether nothing but blanks comes before a place on its line, so
        // that a line may be put in before it.
        bool startsLine(const Luau::Position& at) const
        {
            if (at.line >= mStarts.size())
            {
                return false;
            }
            const std::string_view before = mSource.substr(mStarts[at.line], std::min<size_t>(at.column, mSource.size() - mStarts[at.line]));
            return before.find_first_not_of(" \t") == std::string_view::npos;
        }

        // x = ..., or function f(), at the top of the script, the first to
        // make the global: a note, since the script may mean it, fixed as a
        // local -- in place where it names the global first, else declared
        // before the statement that names it first, the global's nil being
        // the local's until it is given something.
        void topGlobal(Luau::AstStat* stat)
        {
            std::vector<const Luau::AstExprGlobal*> made_here;
            const bool                              function = stat->is<Luau::AstStatFunction>();
            if (const auto* assign = stat->as<Luau::AstStatAssign>())
            {
                for (Luau::AstExpr* var : assign->vars)
                {
                    const auto* global = var->as<Luau::AstExprGlobal>();
                    if (!made(global))
                    {
                        return;
                    }
                    made_here.push_back(global);
                }
            }
            else if (const auto* named_function = stat->as<Luau::AstStatFunction>())
            {
                const auto* global = named_function->name->as<Luau::AstExprGlobal>();
                if (!made(global))
                {
                    return;
                }
                made_here.push_back(global);
            }
            std::string names;
            bool        first = true;
            for (const Luau::AstExprGlobal* global : made_here)
            {
                // Said once, where the top of the script first gives it.
                if (!mTopMade.insert(global->name.value).second)
                {
                    return;
                }
                first = first && named(global).front().begin == global->location.begin;
                names += (names.empty() ? "" : ", ") + std::string(global->name.value);
            }
            if (names.empty())
            {
                return;
            }
            ALScriptProblem& said = function ? problem(stat->location, "LuauLintSlGlobalFunction",
                                                       "function [1] makes a global. local function [1] is quicker for Luau to call", { names },
                                                       "SlGlobalAssign", Severity::Note)
                                             : problem(stat->location, "LuauLintSlGlobalAssign",
                                                       "This makes [1] a global. A local is quicker for Luau to read", { names }, "SlGlobalAssign",
                                                       Severity::Note);
            if (mGlobals.viaG)
            {
                return;
            }
            if (first)
            {
                offer(said, "local " + std::string(function ? "function " : "") + names,
                      { edit(Luau::Location(stat->location.begin, stat->location.begin), "local ") }, false);
                return;
            }
            // A local of each, before the first statement of the script's
            // top that names it.
            if (made_here.size() != 1)
            {
                return;
            }
            const Luau::Position first_named = named(made_here.front()).front().begin;
            for (Luau::AstStat* top : mRoot->body)
            {
                if (top->location.end < first_named)
                {
                    continue;
                }
                if (startsLine(top->location.begin))
                {
                    const std::string indent(top->location.begin.column, ' ');
                    offerTitled(said, "ScriptFixDeclareFirst", "Declare local [1] before it is first named", { names },
                                { edit(Luau::Location(top->location.begin, top->location.begin), "local " + names + "\n" + indent) }, false);
                }
                return;
            }
        }

        // function f() in a block of its own, which makes a global only when
        // the block runs: a local of the block's where the block alone names
        // it, after it.
        void nestedFunction(Luau::AstStatBlock* block, Luau::AstStat* stat)
        {
            const auto* named_function = stat->as<Luau::AstStatFunction>();
            const auto* global         = named_function ? named_function->name->as<Luau::AstExprGlobal>() : nullptr;
            if (!made(global))
            {
                return;
            }
            const std::string name = global->name.value;
            ALScriptProblem&  said = problem(stat->location, "LuauLintSlGlobalFunctionInScope",
                                             "function [1] here makes a global, and only once this runs. local function [1] keeps it to this block",
                                             { name }, "SlGlobalAssign");
            bool within = !mGlobals.viaG;
            for (const Luau::Location& at : named(global))
            {
                within = within && !(at.begin < stat->location.begin) && !(block->location.end < at.end);
            }
            if (within)
            {
                offer(said, "local function " + name, { edit(Luau::Location(stat->location.begin, stat->location.begin), "local ") }, false);
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

        // Said as the rule says, or as `severity` says where one of the
        // rule's findings is more or less than the others.
        ALScriptProblem& problem(const Luau::Location& where, const char* key, const char* english, std::vector<std::string> args, const char* name,
                                 std::optional<Severity> severity = std::nullopt)
        {
            const ALScriptLintPass::Rule* rule = ALScriptLintPass::rule(name);
            ALScriptProblem               p;
            p.severity  = mAllErrors || (mFatal & ALScriptLintPass::bit(name)) ? Severity::Error : severity.value_or(rule->severity);
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
            return mOut.back();
        }

        // The fix for a problem just said, which writes it as `now`: its
        // edits made over the text the pass read, and safe only where it
        // cannot change what the script does (YD5).
        static void offer(ALScriptProblem& problem, const std::string& now, std::vector<ALScriptEdit> edits, bool safe)
        {
            offerTitled(problem, "ScriptFixWriteIt", "Write it [1]", { now }, std::move(edits), safe);
        }

        static void offerTitled(ALScriptProblem& problem, const char* key, const char* english, std::vector<std::string> args,
                                std::vector<ALScriptEdit> edits, bool safe)
        {
            ALScriptFix fix = ALScriptFixes::titled(key, english, std::move(args));
            fix.preferred   = true;
            fix.safe        = safe;
            fix.edits       = std::move(edits);
            problem.fixes.push_back(std::move(fix));
        }

        static ALScriptEdit edit(const Luau::Location& where, std::string with)
        {
            return ALScriptEdit(static_cast<S32>(where.begin.line), static_cast<S32>(where.begin.column), static_cast<S32>(where.end.line),
                                static_cast<S32>(where.end.column), std::move(with));
        }


        // An expression's text, bracketed where an operator put before it
        // would take less of it.
        std::string bracketed(Luau::AstExpr* e) const
        {
            const bool loose = e->is<Luau::AstExprBinary>() || e->is<Luau::AstExprUnary>() || e->is<Luau::AstExprIfElse>() ||
                               e->is<Luau::AstExprTypeAssertion>();
            return loose ? "(" + text(e->location) + ")" : text(e->location);
        }

        std::string_view                                                  mSource;
        std::vector<size_t>                                               mStarts;
        const Luau::AstStatBlock*                                         mRoot;
        const Luau::Module*                                               mChecked;
        const Locals&                                                     mLocals;
        const Globals&                                                    mGlobals;
        boost::unordered_flat_set<std::string>                            mGiven;
        boost::unordered_flat_set<std::string>                            mTopMade;
        boost::unordered_flat_map<std::pair<Luau::AstLocal*, Kind>, bool> mLocalKinds;
        boost::unordered_flat_map<Luau::AstLocal*, std::optional<Find>>   mFindLocals;
        boost::unordered_flat_set<const Luau::AstExprCall*>               mStatements;
        uint64_t                                                          mEnabled;
        uint64_t                                                          mFatal;
        bool                                                              mAllErrors;
        ALScriptProblems&                                                 mOut;
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
    module.root->visit(&locals);
    Globals globals;
    module.root->visit(&globals);
    // In the order of the text, which a statement's parts are not always
    // visited in.
    for (auto& [name, places] : globals.named)
    {
        std::sort(places.begin(), places.end(), [](const Luau::Location& a, const Luau::Location& b) { return a.begin < b.begin; });
    }
    Pass pass(source, module.root, checked, locals, globals, enabled, fatal, all_errors, out);
    module.root->visit(&pass);
}

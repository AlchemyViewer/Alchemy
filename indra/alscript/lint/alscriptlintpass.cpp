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
#include "alscriptlexicon.h"

#include "Luau/Ast.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"
#include "Luau/TypePack.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    using Severity = ALScriptProblem::Severity;
    using Rule     = ALScriptLintPass::Rule;

    // --- the table ------------------------------------------------------------------

    const std::vector<Rule> RULES = {
        // x = x + 1, where Luau has x += 1.
        { "SlCompoundAssign", Rule::SLua, Severity::Note, true, true, nullptr, true },
        // LSL: llGetListLength(l) in a loop's check, l unchanged in it.
        { "SlLoopInvariantCall", Rule::LSL, Severity::Note, true, true, nullptr },
        // if n then, where n is a number: true at 0 as at anything. So too
        // a string, a uuid, a vector, a quaternion, a list and an integer,
        // which LSL counted false when empty, NULL_KEY or zero.
        { "SlNumberTruth", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // ll.ListFindList(l, x) == -1, where it answers nil; llcompat's
        // against nil, where it answers -1.
        { "SlNilSentinel", Rule::SLua, Severity::Error, true, true, nullptr, true },
        // t == {}: a table built there equals no other.
        { "SlTableCompare", Rule::SLua, Severity::Error, true, true, nullptr, true },
        // t[0], for i = 0, #t - 1, string.sub(s, 0, n), ll.X(s, 0): LSL
        // counted from 0.
        { "SlZeroIndex", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // llcompat.X where ll.X means the same: its fix is ll's.
        { "SlCompatCall", Rule::SLua, Severity::Note, true, true, nullptr, true },
        // b == 1, where b is a boolean: LSL's truths were numbers.
        { "SlBooleanNumber", Rule::SLua, Severity::Error, true, true, nullptr, true },
        // x = 0 making a global at the top; a function f() in a nested scope,
        // lute's global_function_in_scope. A function f() at the top is
        // SlGlobalFunction's.
        { "SlGlobalAssign", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // if (x) then: LSL's brackets, which Luau's if needs none of.
        { "SlParenCondition", Rule::SLua, Severity::Note, true, true, "parenthese_conditions", true },
        // a = b followed by b = a, which is no swap.
        { "SlAlmostSwapped", Rule::SLua, Severity::Warning, true, true, "almost_swapped", true },
        // string.upper(s) alone, ll.DeleteSubList(l, 1, 1) alone: an answer
        // thrown away from what does nothing else.
        { "SlMustUse", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // for k, v in pairs(t), where Luau's for walks t itself.
        { "SlGeneralizedFor", Rule::SLua, Severity::Note, true, true, nullptr, true },
        // An if or a loop whose block is empty.
        { "SlEmptyBlock", Rule::SLua, Severity::Warning, true, true, "empty_if empty_loop", true },
        // t[n / 2]: Luau's / makes a fraction, which a list has nothing at.
        { "SlIndexDivision", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // a * b of vectors where a number is wanted, LSL's dot product; a % b,
        // the cross product as in LSL, a note naming vector.cross.
        { "SlVectorProduct", Rule::SLua, Severity::Warning, true, true, nullptr, true },
        // llSetPos, llSetPrimitiveParams: a call that sleeps, which a Fast
        // one does without. A warning in a loop or a timer.
        { "SlSleepingCall", Rule::Both, Severity::Note, true, true, nullptr },
        // Prim-params calls one after another, which one call could do.
        { "SlMergeablePrimParams", Rule::Both, Severity::Note, true, true, nullptr },
        // llListen(0, "", NULL_KEY, ""): every line of chat nearby.
        { "SlCostlyListen", Rule::Both, Severity::Note, true, false, nullptr },
        // A timer under 0.1 s.
        { "SlFastTimer", Rule::Both, Severity::Note, true, false, nullptr },
        // A sensor repeat under 1 s.
        { "SlFastSensor", Rule::Both, Severity::Note, true, false, nullptr },
        // s ..= x in a loop: a new string each time round.
        { "SlStringBuild", Rule::Both, Severity::Note, true, true, nullptr },
        // llGetOwner() asked again and again in one handler.
        { "SlRepeatedCall", Rule::Both, Severity::Note, true, true, nullptr },
        // function f() at the top making a global: a rule of its own, which a
        // script moved from LSL turns off while its functions still call
        // those written after them, which a local function cannot be.
        { "SlGlobalFunction", Rule::SLua, Severity::Note, true, true, nullptr, true },
        // for i = 1, n do ... i = j ... end: LSL's for went on from what i was
        // set to, Luau's makes i afresh each time round.
        { "SlForIndexAssign", Rule::SLua, Severity::Warning, true, false, nullptr, true },
        // integer x = 4294967296: past 0xFFFFFFFF, which the grid's compilers
        // read as -1.
        { "SlIntegerPast32Bits", Rule::LSL, Severity::Warning, true, false, nullptr },
    };

    const ALScriptLintPass::PrimParams PRIM_PARAMS[] = {
        { "llSetLinkPrimitiveParams", 0, 1 },
        { "llSetLinkPrimitiveParamsFast", 0, 1 },
        { "llSetPrimitiveParams", -1, 0 },
    };

    // The sleeping calls with a sleepless way of doing the same, the
    // arguments by their places: $1, $2. A list is written [ ], which SLua
    // writes { }. Where nothing does quite the same -- a texture's calls,
    // whose rule sets its repeats, offsets and rotation with it -- the rule
    // alone, and no call.
    const ALScriptLintPass::Sleepless SLEEPLESS[] = {
        { "llGetPrimitiveParams", "llGetLinkPrimitiveParams", "LINK_THIS, $1", nullptr },
        { "llOffsetTexture", nullptr, nullptr, "PRIM_TEXTURE" },
        { "llRotateTexture", nullptr, nullptr, "PRIM_TEXTURE" },
        { "llScaleTexture", nullptr, nullptr, "PRIM_TEXTURE" },
        { "llSetLinkPrimitiveParams", "llSetLinkPrimitiveParamsFast", "$1, $2", nullptr },
        { "llSetLinkRenderMaterial", "llSetLinkPrimitiveParamsFast", "$1, [PRIM_RENDER_MATERIAL, $3, $2]", nullptr },
        { "llSetLinkTexture", nullptr, nullptr, "PRIM_TEXTURE" },
        { "llSetLocalRot", "llSetLinkPrimitiveParamsFast", "LINK_THIS, [PRIM_ROT_LOCAL, $1]", nullptr },
        { "llSetPos", "llSetLinkPrimitiveParamsFast", "LINK_THIS, [PRIM_POSITION, $1]", nullptr },
        { "llSetPrimitiveParams", "llSetLinkPrimitiveParamsFast", "LINK_THIS, $1", nullptr },
        { "llSetRenderMaterial", "llSetLinkPrimitiveParamsFast", "LINK_THIS, [PRIM_RENDER_MATERIAL, $2, $1]", nullptr },
        { "llSetRot", "llSetLinkPrimitiveParamsFast", "LINK_THIS, [PRIM_ROTATION, $1]", nullptr },
        { "llSetTexture", nullptr, nullptr, "PRIM_TEXTURE" },
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

    // What Luau's library, and SLua's, answer without changing anything: a
    // global function, or a library's -- all of math's but its random
    // numbers, and of string's, table's and the rest those that only
    // answer. SLua's types made by calling their names, vector(1, 2, 3)
    // and uuid(s), among the globals; all of integer's, whose answers are
    // new integers; and of buffer's those that read it or make one, not
    // those that write, copy into or fill one.
    bool onlyAnswers(std::string_view library, std::string_view name)
    {
        using Names = std::initializer_list<std::string_view>;
        const auto among = [&](Names names) { return std::find(names.begin(), names.end(), name) != names.end(); };
        if (library.empty())
        {
            return among({ "tostring", "tonumber", "type", "typeof", "rawequal", "rawlen", "rawget", "select", "touuid", "tovector", "toquaternion",
                           "torotation", "vector", "quaternion", "rotation", "uuid" });
        }
        if (library == "math")
        {
            return name != "random" && name != "randomseed";
        }
        if (library == "vector" || library == "quaternion" || library == "rotation" || library == "uuid" || library == "integer" ||
            library == "bit32" || library == "utf8" || library == "llbase64")
        {
            return true;
        }
        if (library == "string")
        {
            return among({ "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep", "reverse", "split", "sub", "upper",
                           "pack", "packsize", "unpack" });
        }
        if (library == "table")
        {
            return among({ "concat", "find", "clone", "pack", "unpack", "create", "maxn", "isfrozen" });
        }
        if (library == "buffer")
        {
            return name.substr(0, 4) == "read" || among({ "create", "fromstring", "tostring", "len" });
        }
        return library == "lljson" && among({ "encode", "decode", "slencode", "sldecode" });
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

    // Where a local is given a value in a stretch of the tree, plainly or
    // by x += y: each place it is set.
    class Sets final : public Luau::AstVisitor
    {
    public:
        explicit Sets(Luau::AstLocal* local) : mLocal(local) {}

        std::vector<Luau::Location> at;

        bool visit(Luau::AstStatAssign* node) override
        {
            for (Luau::AstExpr* var : node->vars)
            {
                set(var);
            }
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            set(node->var);
            return true;
        }

    private:
        void set(Luau::AstExpr* var)
        {
            if (const auto* local = var->as<Luau::AstExprLocal>(); local && local->local == mLocal)
            {
                at.push_back(var->location);
            }
        }

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

    // Every global the script sets, by name: where it is first set, and
    // whether it is ever set outside every function -- at the top, or in a
    // block of the top's.
    class GlobalSets final : public Luau::AstVisitor
    {
    public:
        struct Set
        {
            Luau::Location first;
            bool           atTop = false;
        };
        boost::unordered_flat_map<std::string, Set> sets;

        bool visit(Luau::AstStatAssign* node) override
        {
            for (Luau::AstExpr* var : node->vars)
            {
                took(var);
            }
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            took(node->var);
            return true;
        }
        // function f() at the top makes f there; one inside a function is
        // FunctionInScope's to say.
        bool visit(Luau::AstStatFunction* node) override
        {
            if (mDepth == 0)
            {
                took(node->name);
            }
            return true;
        }
        // Inside a function, however deep in the top's blocks it stands.
        bool visit(Luau::AstExprFunction* node) override
        {
            ++mDepth;
            node->body->visit(this);
            --mDepth;
            return false;
        }

    private:
        void took(Luau::AstExpr* var)
        {
            const auto* global = var->as<Luau::AstExprGlobal>();
            if (!global)
            {
                return;
            }
            const auto [it, added] = sets.try_emplace(global->name.value, Set{ global->location, false });
            if (!added && global->location.begin < it->second.first.begin)
            {
                it->second.first = global->location;
            }
            it->second.atTop = it->second.atTop || mDepth == 0;
        }
        S32 mDepth = 0;
    };

    // What a loop's body does to strings, not looking into the functions
    // it makes: each append, s ..= x or s = s .. x, what it appends and to
    // what; every read of each local; and whether it returns.
    class Appends final : public Luau::AstVisitor
    {
    public:
        struct Append
        {
            Luau::AstStat* stat;
            Luau::AstExpr* var;
            Luau::AstExpr* piece;
        };
        std::vector<Append>                                   appends;
        boost::unordered_flat_map<Luau::AstLocal*, size_t> reads;
        bool                                                  returns = false;

        bool visit(Luau::AstExprFunction*) override { return false; }
        bool visit(Luau::AstStatReturn*) override
        {
            returns = true;
            return true;
        }
        bool visit(Luau::AstExprLocal* node) override
        {
            ++reads[node->local];
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            if (node->op == Luau::AstExprBinary::Concat)
            {
                appends.push_back({ node, node->var, node->value });
            }
            return true;
        }
        bool visit(Luau::AstStatAssign* node) override
        {
            const auto* join = node->vars.size == 1 && node->values.size == 1 ? node->values.data[0]->as<Luau::AstExprBinary>() : nullptr;
            if (join && join->op == Luau::AstExprBinary::Concat && same(node->vars.data[0], join->left))
            {
                appends.push_back({ node, node->vars.data[0], join->right });
            }
            return true;
        }
    };

    // The calls a function's body makes that answer the same throughout
    // an event -- ll.GetOwner() -- not looking into the functions it makes.
    class SteadyCalls final : public Luau::AstVisitor
    {
    public:
        std::vector<Luau::AstExprCall*> calls;

        bool visit(Luau::AstExprFunction*) override { return false; }
        bool visit(Luau::AstExprCall* node) override
        {
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            const auto* lib    = callee && callee->op == '.' ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            const std::string_view from = lib ? lib->name.value : "";
            if ((from == "ll" || from == "llcompat") && node->args.size == 0 &&
                ALScriptLintPass::steadyName("ll" + std::string(callee->index.value)))
            {
                calls.push_back(node);
            }
            return true;
        }
    };

    // What the pass asks the check an expression is. An integer is SLua's
    // own, written 5i, and no number.
    enum class Kind : U8
    {
        Number,
        Integer,
        String,
        Boolean,
        Table,
        List,
        Vector,
        Quaternion,
        Uuid,
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

        // --- SlGeneralizedFor: pairs and ipairs where for walks a table -------

        // for k, v in pairs(t), ipairs(t), or next, t: Luau's for walks a
        // table given as it is. Not safe: ipairs stops at a list's first
        // gap, and a table's own __iter is what for uses, pairs not.
        bool visit(Luau::AstStatForIn* node) override
        {
            mOften.push_back(node->body->location);
            emptyLoop(node->body, Luau::Location(node->location.begin, node->body->location.begin),
                      Luau::Location(node->body->location.begin, node->location.end), "for");
            if (!on("SlGeneralizedFor") || node->values.size == 0)
            {
                return true;
            }
            Luau::AstExpr* first  = node->values.data[0];
            Luau::AstExpr* walked = nullptr;
            std::string    how;
            if (const auto* call = first->as<Luau::AstExprCall>(); call && node->values.size == 1 && call->args.size == 1)
            {
                const auto* global = call->func->as<Luau::AstExprGlobal>();
                how                = global ? global->name.value : "";
                walked             = how == "pairs" || how == "ipairs" ? call->args.data[0] : nullptr;
            }
            else if (const auto* global = first->as<Luau::AstExprGlobal>();
                     global && node->values.size == 2 && std::string_view(global->name.value) == "next")
            {
                how    = "next";
                walked = node->values.data[1];
            }
            if (!walked)
            {
                return true;
            }
            const Luau::Location values(first->location.begin, node->values.data[node->values.size - 1]->location.end);
            const std::string    table = text(walked->location);
            ALScriptProblem&     said  = how == "ipairs"
                                             ? problem(values, "LuauLintSlGeneralizedForList",
                                                       "Luau's for walks a table given as it is: in [1] walks a list as in [2] does, where the list has no gaps",
                                                       { table, text(values) }, "SlGeneralizedFor")
                                             : problem(values, "LuauLintSlGeneralizedFor", "Luau's for walks a table given as it is: in [1] says what in [2] says",
                                                       { table, text(values) }, "SlGeneralizedFor");
            offer(said, "in " + table, { edit(values, table) }, false);
            return true;
        }

        // --- SlGlobalAssign: a global made where a local would do ------------

        bool visit(Luau::AstStatBlock* node) override
        {
            if (on("SlGlobalAssign") || on("SlGlobalFunction"))
            {
                for (Luau::AstStat* stat : node->body)
                {
                    if (node == mRoot)
                    {
                        topGlobal(stat);
                    }
                    else if (on("SlGlobalAssign"))
                    {
                        nestedFunction(node, stat);
                    }
                }
                if (node == mRoot && on("SlGlobalAssign"))
                {
                    globalsSetInFunctions();
                }
            }
            if (on("SlAlmostSwapped"))
            {
                for (size_t i = 0; i + 1 < node->body.size; ++i)
                {
                    almostSwapped(node->body.data[i], node->body.data[i + 1]);
                }
            }
            if (on("SlMergeablePrimParams"))
            {
                mergeablePrimParams(node);
            }
            if (on("SlStringBuild"))
            {
                for (size_t i = 0; i < node->body.size; ++i)
                {
                    stringBuild(node, i);
                }
            }
            if (on("SlRepeatedCall") && node == mRoot)
            {
                repeatedCalls(node);
            }
            return true;
        }

        bool visit(Luau::AstExprFunction* node) override
        {
            if (on("SlRepeatedCall") && node->body)
            {
                repeatedCalls(node->body);
            }
            return true;
        }

        // --- SlRepeatedCall: the same answer asked for again ----------------

        // ll.GetOwner() and its kin called more than once in one function's
        // body, which answer the same each time: a note, fixed as a local
        // set before the first statement of the body that calls it, which
        // each call then reads. Not safe: an event may run over several of
        // the region's frames, and the owner change between them.
        void repeatedCalls(Luau::AstStatBlock* body)
        {
            SteadyCalls steady;
            for (Luau::AstStat* stat : body->body)
            {
                stat->visit(&steady);
            }
            std::vector<std::pair<std::string, std::vector<Luau::AstExprCall*>>> groups;
            for (Luau::AstExprCall* call : steady.calls)
            {
                const std::string function = text(call->func->location);
                auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == function; });
                if (group == groups.end())
                {
                    groups.push_back({ function, {} });
                    group = groups.end() - 1;
                }
                group->second.push_back(call);
            }
            for (auto& [function, calls] : groups)
            {
                if (calls.size() < 2)
                {
                    continue;
                }
                std::sort(calls.begin(), calls.end(), [](Luau::AstExprCall* a, Luau::AstExprCall* b) { return a->location.begin < b->location.begin; });
                ALScriptProblem& said = problem(calls.front()->location, "LuauLintSlRepeatedCall",
                                                "[1] is called [2] times here, and answers the same each time: a local set once holds it",
                                                { function + "()", std::to_string(calls.size()) }, "SlRepeatedCall");
                // The body's statement that calls it first, before which the
                // local goes.
                const auto top = std::find_if(body->body.begin(), body->body.end(), [&](Luau::AstStat* stat) {
                    return !(calls.front()->location.begin < stat->location.begin) && !(stat->location.end < calls.front()->location.end);
                });
                const auto* callee = calls.front()->func->as<Luau::AstExprIndexName>();
                if (top == body->body.end() || !startsLine((*top)->location.begin))
                {
                    continue;
                }
                const std::string         name   = freshName(ALScriptLintPass::steadyName("ll" + std::string(callee->index.value)));
                const Luau::Position      at     = (*top)->location.begin;
                const std::string         indent = text(Luau::Location(Luau::Position(at.line, 0), at));
                std::vector<ALScriptEdit> edits  = { edit(Luau::Location(at, at), "local " + name + " = " + function + "()\n" + indent) };
                for (Luau::AstExprCall* call : calls)
                {
                    edits.push_back(edit(call->location, name));
                }
                offerTitled(said, "ScriptFixKeepCall", "Keep [1] in a local, [2]", { function + "()", name }, std::move(edits), false);
            }
        }

        // --- SlStringBuild: a string grown in a loop -------------------------

        // A loop's body, where the statement is a loop.
        static Luau::AstStatBlock* loopBody(Luau::AstStat* stat)
        {
            if (auto* loop = stat->as<Luau::AstStatFor>())
            {
                return loop->body;
            }
            if (auto* loop = stat->as<Luau::AstStatForIn>())
            {
                return loop->body;
            }
            if (auto* loop = stat->as<Luau::AstStatWhile>())
            {
                return loop->body;
            }
            if (auto* loop = stat->as<Luau::AstStatRepeat>())
            {
                return loop->body;
            }
            return nullptr;
        }

        // A name the text has nowhere, from `base`.
        std::string freshName(const std::string& base) const
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

        // s ..= x in a loop, s from outside it: a new string each time round,
        // which a table of the pieces, joined once, is not. Said once for
        // each string, at the outermost loop. Fixed as the converter writes
        // it, where s is a local declared in the same block before the loop,
        // read in it only by its appends, and the loop never returns: a
        // table before the loop, table.insert for each append, and s joined
        // to table.concat of it after. Not safe: an error part way round
        // leaves s without what the table holds.
        void stringBuild(Luau::AstStatBlock* block, size_t index)
        {
            Luau::AstStat*      loop = block->body.data[index];
            Luau::AstStatBlock* body = loopBody(loop);
            if (!body)
            {
                return;
            }
            Appends appends;
            body->visit(&appends);
            for (const Appends::Append& each : appends.appends)
            {
                const auto*       local  = each.var->as<Luau::AstExprLocal>();
                const auto*       global = each.var->as<Luau::AstExprGlobal>();
                const bool        inside = local && !(local->local->location.begin < loop->location.begin);
                const std::string name   = local ? local->local->name.value : global ? global->name.value : "";
                if (name.empty() || inside || !mBuilt.insert(local ? static_cast<const void*>(local->local) : global->name.value).second)
                {
                    continue;
                }
                ALScriptProblem& said = problem(each.stat->location, "LuauLintSlStringBuild",
                                                "[1] is joined to with .. in a loop, which makes a new string each time round. SLua's way is to put "
                                                "the pieces in a table and join them once, with table.concat",
                                                { name }, "SlStringBuild");
                // A local, declared in this block before the loop, and read
                // in it by its appends alone.
                if (!local)
                {
                    continue;
                }
                bool here = false;
                for (size_t i = 0; i < index; ++i)
                {
                    const auto* declared = block->body.data[i]->as<Luau::AstStatLocal>();
                    here = here || (declared && std::find(declared->vars.begin(), declared->vars.end(), local->local) != declared->vars.end());
                }
                size_t mine = 0;
                for (const Appends::Append& other : appends.appends)
                {
                    const auto* target = other.var->as<Luau::AstExprLocal>();
                    mine += target && target->local == local->local ? (other.stat->is<Luau::AstStatAssign>() ? 2 : 1) : 0;
                }
                const auto reads = appends.reads.find(local->local);
                if (!here || appends.returns || reads == appends.reads.end() || reads->second != mine || !startsLine(loop->location.begin))
                {
                    continue;
                }
                const std::string         parts  = freshName(name + "Parts");
                const std::string         indent = text(Luau::Location(Luau::Position(loop->location.begin.line, 0), loop->location.begin));
                std::vector<ALScriptEdit> edits  = { edit(Luau::Location(loop->location.begin, loop->location.begin), "local " + parts + " = {}\n" + indent) };
                for (const Appends::Append& other : appends.appends)
                {
                    const auto* target = other.var->as<Luau::AstExprLocal>();
                    if (target && target->local == local->local)
                    {
                        // A number as .. would write it, so that the table
                        // is one of strings, which table.concat takes.
                        const std::string piece = is(other.piece, Kind::Number) ? "tostring(" + text(other.piece->location) + ")"
                                                                                : text(other.piece->location);
                        edits.push_back(edit(other.stat->location, "table.insert(" + parts + ", " + piece + ")"));
                    }
                }
                edits.push_back(edit(Luau::Location(loop->location.end, loop->location.end), "\n" + indent + name + " ..= table.concat(" + parts + ")"));
                offerTitled(said, "ScriptFixStringParts", "Put [1]'s pieces in a table, joined once after the loop", { name }, std::move(edits), false);
            }
        }

        // --- SlMergeablePrimParams: prim-params calls one call could make ----

        struct PrimStat
        {
            Luau::AstExprCall*         call;
            std::string                function;
            int                        link;
            ALScriptLintPass::PrimCall parts;
        };

        // A statement that is a call setting prim params with a list of
        // rules written out; one after the first of a run only where all
        // it is given is settled, so that nothing it reads could see what
        // the calls before it did.
        std::optional<PrimStat> primStat(Luau::AstStat* stat, bool later)
        {
            auto*       expr   = stat->as<Luau::AstStatExpr>();
            auto*       call   = expr ? expr->expr->as<Luau::AstExprCall>() : nullptr;
            const auto* callee = call ? call->func->as<Luau::AstExprIndexName>() : nullptr;
            const auto* lib    = callee && callee->op == '.' ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            const std::string_view from = lib ? lib->name.value : "";
            if (from != "ll" && from != "llcompat")
            {
                return std::nullopt;
            }
            const ALScriptLintPass::PrimParams* params = ALScriptLintPass::primParams("ll" + std::string(callee->index.value));
            if (!params || call->args.size != static_cast<size_t>(params->rules + 1) ||
                (later && !std::all_of(call->args.begin(), call->args.end(), [](Luau::AstExpr* arg) { return settled(arg); })))
            {
                return std::nullopt;
            }
            const auto* rules = unbracketed(call->args.data[params->rules])->as<Luau::AstExprTable>();
            if (!rules)
            {
                return std::nullopt;
            }
            PrimStat out{ call, std::string(from) + "." + std::string(callee->index.value), params->link, {} };
            for (const Luau::AstExprTable::Item& item : rules->items)
            {
                if (item.kind != Luau::AstExprTable::Item::Kind::List)
                {
                    return std::nullopt;
                }
                const auto* global = item.value->as<Luau::AstExprGlobal>();
                out.parts.targets  = out.parts.targets || (global && std::string_view(global->name.value) == "PRIM_LINK_TARGET");
            }
            out.parts.link  = params->link < 0 ? "LINK_THIS" : text(call->args.data[params->link]->location);
            out.parts.rules = rules->items.size == 0
                                  ? std::string()
                                  : text(Luau::Location(rules->items.data[0].value->location.begin, rules->items.data[rules->items.size - 1].value->location.end));
            return out;
        }

        // Each run of such statements, one after another, calling the same
        // function: one call with all their rules sets the same. A note;
        // fixed as that call, not safe, since it is one change to the prim
        // where there were several.
        void mergeablePrimParams(Luau::AstStatBlock* block)
        {
            for (size_t i = 0; i < block->body.size;)
            {
                const std::optional<PrimStat> first = primStat(block->body.data[i], false);
                size_t                        next  = i + 1;
                if (!first)
                {
                    i = next;
                    continue;
                }
                std::vector<PrimStat> run = { *first };
                for (; next < block->body.size; ++next)
                {
                    std::optional<PrimStat> more = primStat(block->body.data[next], true);
                    if (!more || more->function != first->function)
                    {
                        break;
                    }
                    run.push_back(std::move(*more));
                }
                i = next;
                if (run.size() < 2)
                {
                    continue;
                }
                std::vector<ALScriptLintPass::PrimCall> calls;
                for (const PrimStat& each : run)
                {
                    calls.push_back(each.parts);
                }
                const std::string    now = first->function + "(" + (first->link < 0 ? std::string() : first->parts.link + ", ") +
                                        ALScriptLintPass::mergedRules(calls, true) + ")";
                const Luau::Location all(run.front().call->location.begin, run.back().call->location.end);
                ALScriptProblem&     said = problem(all, "LuauLintSlMergeablePrimParams",
                                                    "These [1] calls to [2] could be one, with all their rules, and PRIM_LINK_TARGET where the link changes",
                                                    { std::to_string(run.size()), first->function }, "SlMergeablePrimParams");
                offer(said, now, { edit(all, now) }, false);
            }
        }

        // --- SlAlmostSwapped: a swap written a step at a time ----------------

        // a = b, then b = a: both are b's after, which a swap was meant.
        void almostSwapped(Luau::AstStat* first, Luau::AstStat* second)
        {
            const auto* one = first->as<Luau::AstStatAssign>();
            const auto* two = second->as<Luau::AstStatAssign>();
            if (!one || !two || one->vars.size != 1 || one->values.size != 1 || two->vars.size != 1 || two->values.size != 1)
            {
                return;
            }
            Luau::AstExpr* a = one->vars.data[0];
            Luau::AstExpr* b = one->values.data[0];
            if (same(a, b) || !same(a, two->values.data[0]) || !same(b, two->vars.data[0]))
            {
                return;
            }
            const std::string was_a = text(a->location);
            const std::string was_b = text(b->location);
            const std::string now   = was_a + ", " + was_b + " = " + was_b + ", " + was_a;
            const Luau::Location both(first->location.begin, second->location.end);
            ALScriptProblem&     said = problem(both, "LuauLintSlAlmostSwapped", "[1] = [2] and then [2] = [1] leave both [2]: a swap is [3]",
                                                { was_a, was_b, now }, "SlAlmostSwapped");
            offer(said, now, { edit(both, now) }, false);
        }

        // --- SlNumberTruth: a number asked whether it is true ------------------

        bool visit(Luau::AstStatIf* node) override
        {
            truth(node->condition);
            bracketedCondition(node->condition, node->location);
            emptyIf(node);
            return true;
        }
        bool visit(Luau::AstStatWhile* node) override
        {
            truth(node->condition);
            bracketedCondition(node->condition, node->location);
            mOften.push_back(node->body->location);
            emptyLoop(node->body, Luau::Location(node->location.begin, node->body->location.begin),
                      Luau::Location(node->body->location.begin, node->location.end), "while");
            return true;
        }
        bool visit(Luau::AstStatRepeat* node) override
        {
            truth(node->condition);
            bracketedCondition(node->condition, std::nullopt);
            mOften.push_back(node->body->location);
            emptyLoop(node->body, node->location, Luau::Location(node->body->location.begin, node->condition->location.begin), "repeat");
            return true;
        }
        bool visit(Luau::AstExprIfElse* node) override
        {
            truth(node->condition);
            bracketedCondition(node->condition, node->location);
            return true;
        }
        bool visit(Luau::AstExprUnary* node) override
        {
            if (node->op != Luau::AstExprUnary::Op::Not || !on("SlNumberTruth"))
            {
                return true;
            }
            if (!alwaysFalse(node))
            {
                truth(node->expr);
            }
            return true;
        }

        // --- SlCompoundAssign ------------------------------------------------

        bool visit(Luau::AstStatAssign* node) override
        {
            for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
            {
                timerFunction(node->vars.data[i], node->values.data[i]);
            }
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
            if (on("SlVectorProduct"))
            {
                vectorProducts(node);
            }
            return true;
        }

        // --- SlZeroIndex: counted from 0, as LSL counted -------------------

        bool visit(Luau::AstExprIndexExpr* node) override
        {
            if (on("SlIndexDivision") && is(node->expr, Kind::List))
            {
                indexDivision(node->index);
            }
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
            if (on("SlForIndexAssign"))
            {
                forIndexSet(node);
            }
            mOften.push_back(node->body->location);
            emptyLoop(node->body, Luau::Location(node->location.begin, node->body->location.begin),
                      Luau::Location(node->body->location.begin, node->location.end), "for");
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
            timerCallbacks(node);
            if (on("SlSleepingCall"))
            {
                sleepingCall(node);
            }
            costlyEvents(node);
            // What math's functions are given is a number.
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            const auto* lib    = callee ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (on("SlVectorProduct") && lib && std::string_view(lib->name.value) == "math")
            {
                for (Luau::AstExpr* arg : node->args)
                {
                    dotProduct(arg);
                }
            }
            return true;
        }

        // A call whose answer nobody reads.
        bool visit(Luau::AstStatExpr* node) override
        {
            if (auto* call = node->expr->as<Luau::AstExprCall>())
            {
                mStatements.insert(call);
                if (on("SlMustUse"))
                {
                    mustUse(call);
                }
            }
            return true;
        }

        // --- SlMustUse: an answer thrown away ---------------------------------

        // The kind an expression is, of those an answer may be given back as.
        std::optional<Kind> kindOf(Luau::AstExpr* e)
        {
            for (Kind kind : { Kind::Number, Kind::Integer, Kind::String, Kind::Boolean, Kind::List, Kind::Table, Kind::Vector, Kind::Quaternion, Kind::Uuid })
            {
                if (is(e, kind))
                {
                    return kind;
                }
            }
            return std::nullopt;
        }

        // The kind a call answers first. A call whose answer is unread is
        // checked as a pack of answers, where the check keeps one; else its
        // function's type says.
        std::optional<Kind> answerKind(Luau::AstExprCall* call)
        {
            if (const std::optional<Kind> kind = kindOf(call))
            {
                return kind;
            }
            std::optional<Luau::TypeId> answer;
            if (const Luau::TypePackId* pack = mChecked ? mChecked->astTypePacks.find(call) : nullptr)
            {
                answer = Luau::first(*pack);
            }
            const Luau::TypeId* callee = mChecked && !answer ? mChecked->astTypes.find(call->func) : nullptr;
            if (const auto* function = callee ? Luau::get<Luau::FunctionType>(Luau::follow(*callee)) : nullptr)
            {
                answer = Luau::first(function->retTypes);
            }
            if (!answer)
            {
                // A string's own, on what the old solver's nonstrict mode
                // calls any: those that answer a string.
                static constexpr std::initializer_list<std::string_view> TEXT = { "format", "gsub", "lower", "rep", "reverse", "sub", "upper" };
                const auto* callee = call->func->as<Luau::AstExprIndexName>();
                const auto* lib    = callee ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
                const bool  own    = callee && ((callee->op == ':' && is(callee->expr, Kind::String)) ||
                                            (callee->op == '.' && lib && std::string_view(lib->name.value) == "string"));
                const bool  text   = own && std::find(TEXT.begin(), TEXT.end(), std::string_view(callee->index.value)) != TEXT.end();
                return text ? std::optional<Kind>(Kind::String) : std::nullopt;
            }
            for (Kind kind : { Kind::Number, Kind::Integer, Kind::String, Kind::Boolean, Kind::List, Kind::Table, Kind::Vector, Kind::Quaternion, Kind::Uuid })
            {
                if (of(*answer, kind))
                {
                    return kind;
                }
            }
            return std::nullopt;
        }

        // A call that does nothing but answer, its answer unread: LSL's
        // llDeleteSubList(l, 0, 0) alone, which a scripter meant to change
        // l. Fixed where the first thing it is given is a variable of the
        // kind it answers: the answer given back to it.
        void mustUse(Luau::AstExprCall* call)
        {
            std::string    function;
            Luau::AstExpr* given = call->args.size ? call->args.data[0] : nullptr;
            if (const auto* global = call->func->as<Luau::AstExprGlobal>())
            {
                if (!onlyAnswers("", global->name.value))
                {
                    return;
                }
                function = global->name.value;
            }
            else if (const auto* callee = call->func->as<Luau::AstExprIndexName>())
            {
                const auto*            lib  = callee->expr->as<Luau::AstExprGlobal>();
                const std::string_view from = lib && callee->op == '.' ? lib->name.value : "";
                const std::string_view name = callee->index.value;
                if (from == "ll" || from == "llcompat")
                {
                    const ALLSLTraits::Trait* row = ALLSLTraits::of(("ll" + std::string(name)).c_str());
                    if (!row || !(row->pure || row->mustUse))
                    {
                        return;
                    }
                }
                else if (callee->op == ':' && is(callee->expr, Kind::String) && onlyAnswers("string", name))
                {
                    given = callee->expr;
                }
                else if (from.empty() || !onlyAnswers(from, name))
                {
                    return;
                }
                function = text(callee->location);
            }
            else
            {
                return;
            }
            ALScriptProblem& said = problem(call->location, "LuauLintSlMustUse", "[1] changes nothing and only answers, and nothing reads its answer here",
                                            { function }, "SlMustUse");
            const std::optional<Kind> answer = given && same(given, given) ? answerKind(call) : std::nullopt;
            if (answer && kindOf(given) == answer)
            {
                const std::string back = text(given->location) + " = ";
                offer(said, back + function + "(...)", { edit(Luau::Location(call->location.begin, call->location.begin), back) }, false);
            }
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
                       (want == Kind::Integer && prim->type == Luau::PrimitiveType::Integer) ||
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
            return extern_type && ((want == Kind::Vector && extern_type->name == "vector") || (want == Kind::Quaternion && extern_type->name == "quaternion") ||
                                   (want == Kind::Uuid && extern_type->name == "uuid"));
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
            // Named before the edits are moved out, which shown counts: a
            // call's arguments are made in no order the language sets.
            const std::string now = shown(*found);
            compatSaid(node->location, *found, now, std::move(found->edits));
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
            if (!on(function ? "SlGlobalFunction" : "SlGlobalAssign"))
            {
                return;
            }
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
                                                       "SlGlobalFunction", Severity::Note)
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

        // A global the script first sets inside a function, never at its top,
        // and names in more than one of its top's statements: a note, fixed
        // by a local of the script's before the first of them, which every
        // function there still shares. Where one function alone names it,
        // Luau's own GlobalUsedAsLocal says so.
        void globalsSetInFunctions()
        {
            if (mGlobals.viaG)
            {
                return;
            }
            GlobalSets sets;
            const_cast<Luau::AstStatBlock*>(mRoot)->visit(&sets);
            std::vector<std::pair<std::string, GlobalSets::Set>> ordered(sets.sets.begin(), sets.sets.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second.first.begin < b.second.first.begin; });
            for (const auto& [name, set] : ordered)
            {
                if (set.atTop || mTopMade.contains(name) || mGiven.contains(name) || name == "_G")
                {
                    continue;
                }
                const auto named_at = mGlobals.named.find(name);
                if (named_at == mGlobals.named.end())
                {
                    continue;
                }
                // The statements of the top that name it, in order.
                std::vector<Luau::AstStat*> tops;
                for (const Luau::Location& at : named_at->second)
                {
                    for (Luau::AstStat* top : mRoot->body)
                    {
                        if (top->location.encloses(at))
                        {
                            if (tops.empty() || tops.back() != top)
                            {
                                tops.push_back(top);
                            }
                            break;
                        }
                    }
                }
                if (tops.size() < 2)
                {
                    continue;
                }
                ALScriptProblem& said = problem(set.first, "LuauLintSlGlobalInFunction",
                                                "[1] is made a global inside a function. A local of the script's, declared before the first "
                                                "function that names it, is shared the same and quicker for Luau to read",
                                                { name }, "SlGlobalAssign", Severity::Note);
                if (startsLine(tops.front()->location.begin))
                {
                    const std::string indent(tops.front()->location.begin.column, ' ');
                    offerTitled(said, "ScriptFixDeclareFirst", "Declare local [1] before it is first named", { name },
                                { edit(Luau::Location(tops.front()->location.begin, tops.front()->location.begin), "local " + name + "\n" + indent) },
                                false);
                }
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

        // --- SlForIndexAssign: a numeric for's variable set inside it -------

        // LSL's for went on from whatever its variable was set to in the
        // body -- i = start to go back, i++ to skip one. Luau's numeric for
        // makes the variable afresh from its own count each time round, so
        // what the body gives it lasts to the end of that time round only.
        // No fix: what the loop should do instead is a while loop's to say.
        void forIndexSet(Luau::AstStatFor* node)
        {
            Sets sets(node->var);
            node->body->visit(&sets);
            for (const Luau::Location& at : sets.at)
            {
                problem(at, "LuauLintSlForIndexAssign",
                        "[1] is set here, but the for gives [1] its own next value each time round, whatever it was set to: LSL's for went on "
                        "from it. A while loop does",
                        { std::string(node->var->name.value) }, "SlForIndexAssign");
            }
        }

        // --- SlParenCondition: LSL's brackets round a condition -------------

        // A condition bracketed whole, as LSL's had to be: the brackets taken
        // out, a blank left where one would otherwise run into a word. The
        // statement's first word -- if, elseif, while -- says whose it is;
        // a repeat's is until's.
        void bracketedCondition(Luau::AstExpr* condition, std::optional<Luau::Location> statement)
        {
            const auto* group = condition->as<Luau::AstExprGroup>();
            if (!on("SlParenCondition") || !group)
            {
                return;
            }
            std::string keyword = "until";
            if (statement)
            {
                const std::string head = text(Luau::Location(statement->begin, condition->location.begin));
                keyword                = head.substr(0, head.find_first_not_of("abcdefghijklmnopqrstuvwxyz"));
            }
            const Luau::Location& at     = condition->location;
            const auto            letter = [&](const Luau::Position& p) {
                const std::optional<size_t> offset = offsetOf(p);
                return offset && *offset < mSource.size() && ALScriptLexicon::isNameByte(mSource[*offset]);
            };
            const Luau::Position before(at.begin.line, at.begin.column > 0 ? at.begin.column - 1 : 0);
            const Luau::Position close(at.end.line, at.end.column - 1);
            const bool           spaced_before = at.begin.column > 0 && letter(before);
            const bool           spaced_after  = letter(at.end);
            ALScriptProblem&     said          = problem(at, "LuauLintSlParenCondition",
                                                         "Luau's [1] needs no brackets round its condition, where LSL's did", { keyword }, "SlParenCondition");
            offerTitled(said, "ScriptFixUnbracket", "Take out the brackets", {},
                        { edit(Luau::Location(at.begin, Luau::Position(at.begin.line, at.begin.column + 1)), spaced_before ? " " : ""),
                          edit(Luau::Location(close, at.end), spaced_after ? " " : "") },
                        true);
        }

        // --- SlEmptyBlock: an if or a loop that does nothing -----------------

        // A block with nothing in it, not even a comment, which would say it
        // is meant: `stretch` is where a comment in it would be.
        bool empty(const Luau::AstStatBlock* block, const Luau::Location& stretch) const
        {
            return block->body.size == 0 && text(stretch).find("--") == std::string::npos;
        }

        // An if, elseif or else whose block is empty. Nothing in its place
        // but an else: the condition turned round, if not c then. An empty
        // else taken out, and its line where it stands alone.
        void emptyIf(Luau::AstStatIf* node)
        {
            if (!on("SlEmptyBlock") || !node->thenLocation)
            {
                return;
            }
            const std::string head    = text(Luau::Location(node->location.begin, node->condition->location.begin));
            const std::string keyword = head.substr(0, head.find_first_not_of("abcdefghijklmnopqrstuvwxyz"));
            auto*             plain   = node->elsebody ? node->elsebody->as<Luau::AstStatBlock>() : nullptr;
            const Luau::Position then_end = node->elseLocation ? node->elseLocation->begin
                                          : node->elsebody   ? node->elsebody->location.begin
                                                             : node->location.end;
            if (empty(node->thenbody, Luau::Location(node->thenLocation->end, then_end)))
            {
                ALScriptProblem& said = problem(Luau::Location(node->location.begin, node->thenLocation->end), "LuauLintSlEmptyBlock",
                                                "This [1]'s block is empty: it does nothing", { keyword }, "SlEmptyBlock");
                if (keyword == "if" && plain && node->elseLocation && !empty(plain, Luau::Location(node->elseLocation->end, node->location.end)))
                {
                    const std::string turned = "not " + bracketed(node->condition);
                    offer(said, "if " + turned + " then",
                          { edit(node->condition->location, turned), edit(Luau::Location(node->thenLocation->end, node->elseLocation->end), "") }, false);
                }
            }
            if (plain && node->elseLocation && empty(plain, Luau::Location(node->elseLocation->end, node->location.end)))
            {
                ALScriptProblem& said = problem(*node->elseLocation, "LuauLintSlEmptyBlock", "This [1]'s block is empty: it does nothing", { "else" },
                                                "SlEmptyBlock");
                // Its line with it, where it has one of its own.
                Luau::Position from = node->elseLocation->begin;
                Luau::Position to   = node->elseLocation->end;
                const std::string_view line   = mSource.substr(mStarts[from.line], (from.line + 1 < mStarts.size() ? mStarts[from.line + 1] : mSource.size()) - mStarts[from.line]);
                const bool             alone  = line.substr(0, from.column).find_first_not_of(" \t") == std::string_view::npos &&
                                                line.substr(to.column).find_first_not_of(" \t\r\n") == std::string_view::npos && from.line + 1 < mStarts.size();
                if (alone)
                {
                    from = Luau::Position(from.line, 0);
                    to   = Luau::Position(from.line + 1, 0);
                }
                else
                {
                    while (from.column > 0 && (line[from.column - 1] == ' ' || line[from.column - 1] == '\t'))
                    {
                        --from.column;
                    }
                }
                offerTitled(said, "ScriptFixRemoveElse", "Take out the empty else", {}, { edit(Luau::Location(from, to), "") }, true);
            }
        }

        // A loop whose block is empty, which spins and does nothing.
        void emptyLoop(const Luau::AstStatBlock* body, const Luau::Location& head, const Luau::Location& stretch, const char* keyword)
        {
            if (on("SlEmptyBlock") && empty(body, stretch))
            {
                problem(head, "LuauLintSlEmptyBlock", "This [1]'s block is empty: it does nothing", { keyword }, "SlEmptyBlock");
            }
        }

        // --- SlIndexDivision: a list indexed at a fraction --------------------

        // Each / in what indexes a list, through the sums it is part of:
        // Luau's / makes a float, where LSL's integers divided whole, and a
        // list has nothing at 2.5. Fixed as //, which rounds down.
        void indexDivision(Luau::AstExpr* index)
        {
            auto* e = unbracketed(index)->as<Luau::AstExprBinary>();
            if (!e)
            {
                return;
            }
            using Op = Luau::AstExprBinary::Op;
            if (e->op == Op::Add || e->op == Op::Sub || e->op == Op::Mul)
            {
                indexDivision(e->left);
                indexDivision(e->right);
                return;
            }
            if (e->op != Op::Div)
            {
                return;
            }
            // The / between its sides, in the text.
            const Luau::Location        between(e->left->location.end, e->right->location.begin);
            const std::optional<size_t> from  = offsetOf(between.begin);
            const std::optional<size_t> to    = offsetOf(between.end);
            const size_t                slash = from && to ? mSource.substr(*from, *to - *from).find('/') : std::string_view::npos;
            if (slash == std::string_view::npos)
            {
                return;
            }
            Luau::Position at = between.begin;
            for (size_t i = *from; i < *from + slash; ++i)
            {
                at = mSource[i] == '\n' ? Luau::Position(at.line + 1, 0) : Luau::Position(at.line, at.column + 1);
            }
            const std::string was  = text(e->location);
            const std::string now  = text(Luau::Location(e->location.begin, at)) + "/" + text(Luau::Location(at, e->location.end));
            ALScriptProblem&  said = problem(e->location, "LuauLintSlIndexDivision",
                                             "[1] may be a fraction, which a list has nothing at: Luau's / divides as floats do, where LSL's "
                                             "integers divided whole. [2] rounds down",
                                             { was, now }, "SlIndexDivision");
            offer(said, now, { edit(Luau::Location(at, at), "/") }, false);
        }

        // --- SlVectorProduct: LSL's products of two vectors -------------------

        // a * b of two vectors, where a number is wanted of it: LSL's was
        // their dot product, SLua's multiplies each part and gives a vector.
        void dotProduct(Luau::AstExpr* e)
        {
            auto* product = unbracketed(e)->as<Luau::AstExprBinary>();
            if (!product || product->op != Luau::AstExprBinary::Mul || !is(product->left, Kind::Vector) || !is(product->right, Kind::Vector))
            {
                return;
            }
            const std::string a    = text(product->left->location);
            const std::string b    = text(product->right->location);
            const std::string now  = "vector.dot(" + a + ", " + b + ")";
            ALScriptProblem&  said = problem(product->location, "LuauLintSlVectorProduct",
                                             "[1] multiplies each part of two vectors, and gives a vector: LSL's * of two vectors was their dot "
                                             "product, a number. vector.dot([2], [3]) is LSL's",
                                             { text(product->location), a, b }, "SlVectorProduct");
            offer(said, now, { edit(product->location, now) }, false);
        }

        // A product of two vectors where the other side of an operator is a
        // number, which a vector can be neither compared with nor added to;
        // and a % b of two vectors, the cross product in SLua as in LSL (the
        // VM's __mod), which vector.cross says plainly: a note.
        void vectorProducts(Luau::AstExprBinary* node)
        {
            using Op = Luau::AstExprBinary::Op;
            if (node->op == Op::Mod && is(node->left, Kind::Vector) && is(node->right, Kind::Vector))
            {
                const std::string a    = text(node->left->location);
                const std::string b    = text(node->right->location);
                const std::string now  = "vector.cross(" + a + ", " + b + ")";
                ALScriptProblem&  said = problem(node->location, "LuauLintSlVectorCross",
                                                 "[1] is the cross product of two vectors, as LSL's % was. vector.cross([2], [3]) says so "
                                                 "plainly",
                                                 { text(node->location), a, b }, "SlVectorProduct", Severity::Note);
                offer(said, now, { edit(node->location, now) }, false);
                return;
            }
            const bool numeric = node->op == Op::Add || node->op == Op::Sub || node->op == Op::Pow || comparison(node->op);
            if (!numeric)
            {
                return;
            }
            const auto number = [&](Luau::AstExpr* e) { return literal(e).has_value() || is(e, Kind::Number); };
            if (number(node->right))
            {
                dotProduct(node->left);
            }
            if (number(node->left))
            {
                dotProduct(node->right);
            }
        }

        // --- SlSleepingCall: a call that sleeps, which a Fast one does not ----

        // A table's timer = function, as the converter writes a state's.
        bool visit(Luau::AstExprTable* node) override
        {
            for (const Luau::AstExprTable::Item& item : node->items)
            {
                const auto* key = item.key ? item.key->as<Luau::AstExprConstantString>() : nullptr;
                if (item.kind == Luau::AstExprTable::Item::Kind::Record && key && std::string_view(key->value.data, key->value.size) == "timer" &&
                    item.value->is<Luau::AstExprFunction>())
                {
                    mOften.push_back(item.value->location);
                }
            }
            return true;
        }

        // LLEvents.timer = function, or the converter's timerHandler, which
        // its LLTimers helper calls.
        void timerFunction(Luau::AstExpr* var, Luau::AstExpr* value)
        {
            if (!value->is<Luau::AstExprFunction>())
            {
                return;
            }
            const auto* field  = var->as<Luau::AstExprIndexName>();
            const auto* events = field ? field->expr->as<Luau::AstExprGlobal>() : nullptr;
            const auto* local  = var->as<Luau::AstExprLocal>();
            const auto* global = var->as<Luau::AstExprGlobal>();
            const std::string_view name = local ? local->local->name.value : global ? global->name.value : "";
            if ((events && std::string_view(events->name.value) == "LLEvents" && std::string_view(field->index.value) == "timer") ||
                name == "timerHandler")
            {
                mOften.push_back(value->location);
            }
        }

        // What LLTimers:every and :once are given to call, and LLEvents:on
        // and :once for "timer".
        void timerCallbacks(Luau::AstExprCall* node)
        {
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            const auto* lib    = callee && callee->op == ':' ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (!lib)
            {
                return;
            }
            const std::string_view from = lib->name.value;
            const std::string_view how  = callee->index.value;
            const auto*            what = node->args.size ? node->args.data[0]->as<Luau::AstExprConstantString>() : nullptr;
            const bool timers = from == "LLTimers" && (how == "every" || how == "once");
            const bool events = from == "LLEvents" && (how == "on" || how == "once") && what &&
                                std::string_view(what->value.data, what->value.size) == "timer";
            for (Luau::AstExpr* arg : node->args)
            {
                if ((timers || events) && arg->is<Luau::AstExprFunction>())
                {
                    mOften.push_back(arg->location);
                }
            }
        }

        // Whether a place is in a loop's body or a timer's function.
        bool often(const Luau::Location& at) const
        {
            return std::any_of(mOften.begin(), mOften.end(),
                               [&](const Luau::Location& in) { return !(at.begin < in.begin) && !(in.end < at.end); });
        }

        // What changes nothing and runs nothing to be read: a constant, a
        // variable, a field of one, their sums, a vector or a quaternion or
        // a table made of them -- what may be read in another order.
        static bool settled(Luau::AstExpr* e)
        {
            e = unbracketed(e);
            if (e->is<Luau::AstExprConstantNumber>() || e->is<Luau::AstExprConstantString>() || e->is<Luau::AstExprConstantBool>() ||
                e->is<Luau::AstExprConstantNil>() || e->is<Luau::AstExprLocal>() || e->is<Luau::AstExprGlobal>())
            {
                return true;
            }
            if (const auto* field = e->as<Luau::AstExprIndexName>())
            {
                return settled(field->expr);
            }
            if (const auto* unary = e->as<Luau::AstExprUnary>())
            {
                return settled(unary->expr);
            }
            if (const auto* binary = e->as<Luau::AstExprBinary>())
            {
                return settled(binary->left) && settled(binary->right);
            }
            if (const auto* table = e->as<Luau::AstExprTable>())
            {
                return std::all_of(table->items.begin(), table->items.end(), [](const Luau::AstExprTable::Item& item) {
                    return (!item.key || settled(item.key)) && settled(item.value);
                });
            }
            const auto* call   = e->as<Luau::AstExprCall>();
            const auto* global = call ? call->func->as<Luau::AstExprGlobal>() : nullptr;
            const std::string_view made = global ? global->name.value : "";
            return (made == "vector" || made == "quaternion" || made == "rotation") &&
                   std::all_of(call->args.begin(), call->args.end(), [](Luau::AstExpr* arg) { return settled(arg); });
        }

        // ll.SetPos and its kin, which sleep where a Fast call does not: a
        // warning in a loop or a timer, where the sleep adds up; a note
        // elsewhere. Fixed as the Fast call, not safe: a script may pace
        // itself by the sleep.
        void sleepingCall(Luau::AstExprCall* node)
        {
            const auto* callee = node->func->as<Luau::AstExprIndexName>();
            const auto* lib    = callee && callee->op == '.' ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            const std::string_view from = lib ? lib->name.value : "";
            if (from != "ll" && from != "llcompat")
            {
                return;
            }
            const std::string                  lsl  = "ll" + std::string(callee->index.value);
            const ALScriptLintPass::Sleepless* call = ALScriptLintPass::sleepless(lsl);
            const ALLSLTraits::Trait*          row  = ALLSLTraits::of(lsl.c_str());
            if (!call || !row || row->monoSleep <= 0)
            {
                return;
            }
            const bool        repeated = often(node->location);
            const std::string function = std::string(from) + "." + std::string(callee->index.value);
            const std::string seconds  = llformat("%g", row->monoSleep);
            if (!call->fast)
            {
                problem(node->location, repeated ? "LuauLintSlSleepingTextureOften" : "LuauLintSlSleepingTexture",
                        repeated ? "[1] makes the script sleep [2] s each time it runs here, in a loop or a timer; [3] sets a face's texture "
                                   "with no sleep, given its repeats, offsets and rotation too"
                                 : "[1] makes the script sleep [2] s each call; [3] sets a face's texture with no sleep, given its repeats, "
                                   "offsets and rotation too",
                        { function, seconds, call->rule }, "SlSleepingCall", repeated ? Severity::Warning : Severity::Note);
                return;
            }
            const std::string fast = std::string(from) + "." + std::string(call->fast + 2);
            ALScriptProblem&  said = problem(node->location, repeated ? "LuauLintSlSleepingCallOften" : "LuauLintSlSleepingCall",
                                             repeated ? "[1] makes the script sleep [2] s each time it runs here, in a loop or a timer; [3] does "
                                                        "the same without the sleep"
                                                      : "[1] makes the script sleep [2] s each call; [3] does the same without the sleep",
                                             { function, seconds, fast }, "SlSleepingCall", repeated ? Severity::Warning : Severity::Note);
            if (node->args.size == 0)
            {
                return;
            }
            std::vector<std::string> given;
            for (Luau::AstExpr* arg : node->args)
            {
                given.push_back(text(arg->location));
            }
            const std::optional<std::string> written = ALScriptLintPass::sleeplessArgs(*call, given, true);
            if (!written)
            {
                return;
            }
            std::vector<ALScriptEdit> edits = { edit(callee->indexLocation, call->fast + 2) };
            const Luau::Location      first = node->args.data[0]->location;
            const Luau::Location      last  = node->args.data[node->args.size - 1]->location;
            if (const auto kept = ALScriptLintPass::around(*call, true))
            {
                edits.push_back(edit(Luau::Location(first.begin, first.begin), kept->first));
                edits.push_back(edit(Luau::Location(last.end, last.end), kept->second));
            }
            else if (std::all_of(node->args.begin(), node->args.end(), [](Luau::AstExpr* arg) { return settled(arg); }))
            {
                // Its arguments in another order, which only what runs
                // nothing may be read in.
                edits.push_back(edit(Luau::Location(first.begin, last.end), *written));
            }
            else
            {
                return;
            }
            offer(said, fast + "(" + *written + ")", std::move(edits), false);
        }

        // --- SlCostlyListen, SlFastTimer, SlFastSensor ------------------------

        // A number as written, or PUBLIC_CHANNEL.
        static std::optional<double> numberOf(Luau::AstExpr* e)
        {
            const auto* global = unbracketed(e)->as<Luau::AstExprGlobal>();
            return global && std::string_view(global->name.value) == "PUBLIC_CHANNEL" ? std::optional<double>(0) : literal(e);
        }

        // "" as written, or NULL_KEY.
        static bool nothing(Luau::AstExpr* e)
        {
            e                  = unbracketed(e);
            const auto* text   = e->as<Luau::AstExprConstantString>();
            const auto* global = e->as<Luau::AstExprGlobal>();
            return (text && text->value.size == 0) || (global && std::string_view(global->name.value) == "NULL_KEY");
        }

        // What the script sets going that costs the region more than it
        // needs: a listen that hears all chat, a timer under a tenth of a
        // second, a sensor sweeping more than once a second. Notes: each
        // may be what the script is for.
        void costlyEvents(Luau::AstExprCall* node)
        {
            const auto*            callee = node->func->as<Luau::AstExprIndexName>();
            const auto*            lib    = callee ? callee->expr->as<Luau::AstExprGlobal>() : nullptr;
            const std::string_view from   = lib ? lib->name.value : "";
            const std::string_view name   = callee ? callee->index.value : "";
            const bool             ll     = callee && callee->op == '.' && (from == "ll" || from == "llcompat");
            const auto             arg    = [&](size_t i) { return i < node->args.size ? node->args.data[i] : nullptr; };
            if (on("SlCostlyListen") && ll && name == "Listen" && node->args.size == 4 && numberOf(arg(0)) == 0.0 && nothing(arg(1)) &&
                nothing(arg(2)))
            {
                problem(node->location, "LuauLintSlCostlyListen",
                        "This listens on channel 0 for anyone, so the script wakes for every line of chat nearby. A name, a key, or "
                        "another channel hears less",
                        {}, "SlCostlyListen");
            }
            const bool timer = (ll && name == "SetTimerEvent") || (callee && callee->op == ':' && from == "LLTimers" && name == "every");
            const std::optional<double> every = timer && arg(0) ? literal(arg(0)) : std::nullopt;
            if (on("SlFastTimer") && every && *every > 0 && *every < 0.1)
            {
                problem(node->location, "LuauLintSlFastTimer",
                        "A timer every [1] s, under a tenth of a second, fires every few of the region's 45 frames a second, and the "
                        "time it takes is time other scripts there wait for",
                        { text(arg(0)->location) }, "SlFastTimer");
            }
            const std::optional<double> rate = ll && name == "SensorRepeat" && arg(5) ? literal(arg(5)) : std::nullopt;
            if (on("SlFastSensor") && rate && *rate > 0 && *rate < 1)
            {
                problem(node->location, "LuauLintSlFastSensor",
                        "A sensor sweeping every [1] s, under a second, searches round the object that often: once a second or less "
                        "is plenty for most",
                        { text(arg(5)->location) }, "SlFastSensor");
            }
        }

        // The kind of an expression SLua counts true whatever it holds,
        // where LSL counted it false at nought, empty or zero: a number, a
        // string, a key, a vector, a rotation or a list -- and SLua's own
        // integer. None where it may be nil though its type says not. A
        // local inside brackets as itself, which nonstrict mode types any.
        std::optional<Kind> alwaysTrue(Luau::AstExpr* e)
        {
            for (Kind kind : { Kind::Number, Kind::Integer, Kind::String, Kind::Uuid, Kind::Vector, Kind::Quaternion, Kind::List })
            {
                if (is(unbracketed(e), kind))
                {
                    return unsure(e) ? std::nullopt : std::optional<Kind>(kind);
                }
            }
            return std::nullopt;
        }

        // Whether what an expression reads may be nil though its type says
        // not: a table read by a key -- t[k], or t.k where t has no field k
        // but takes any key -- which the check types as the table's values
        // though a key with none reads nil; a local declared with no value;
        // or a local given either. if t[k] then asks whether k has one.
        bool unsure(Luau::AstExpr* e)
        {
            e = unbracketed(e);
            if (e->is<Luau::AstExprIndexExpr>())
            {
                return true;
            }
            if (const auto* field = e->as<Luau::AstExprIndexName>())
            {
                const Luau::TypeId*    type  = mChecked ? mChecked->astTypes.find(field->expr) : nullptr;
                const Luau::TableType* table = type ? Luau::get<Luau::TableType>(Luau::follow(*type)) : nullptr;
                return table && table->indexer && table->props.find(field->index.value) == table->props.end();
            }
            auto* local = e->as<Luau::AstExprLocal>();
            if (!local)
            {
                return false;
            }
            if (const auto known = mUnsure.find(local->local); known != mUnsure.end())
            {
                return known->second;
            }
            // Taken as sure while its own are asked about, as localIs does.
            mUnsure[local->local] = false;
            bool       yes   = mLocals.bare.contains(local->local);
            const auto given = mLocals.given.find(local->local);
            for (size_t i = 0; !yes && given != mLocals.given.end() && i < given->second.size(); ++i)
            {
                yes = unsure(given->second[i]);
            }
            mUnsure[local->local] = yes;
            return yes;
        }

        // What a condition asks the truth of: itself, or each side of an
        // and or an or, inside brackets or not. Each kind is fixed as LSL
        // asked it, which is what the converter writes.
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
            const std::optional<Kind> kind = alwaysTrue(e);
            if (!kind)
            {
                return;
            }
            // An if-then-else inside brackets, the comparison binding
            // tighter.
            const std::string was  = text(e->location);
            const std::string side = e->is<Luau::AstExprIfElse>() ? "(" + was + ")" : was;
            ALScriptProblem*  said = nullptr;
            std::string       now;
            switch (*kind)
            {
                case Kind::Number:
                    // Fixed by its key, as before the rest were said.
                    problem(e->location, "LuauLintSlNumberTruth",
                            "[1] is a number, and Luau counts every number true, 0 too: [1] ~= 0 asks whether it is not 0",
                            { was, e->is<Luau::AstExprIfElse>() ? "(" : "" }, "SlNumberTruth");
                    return;
                case Kind::Integer:
                    now  = side + " ~= 0i";
                    said = &problem(e->location, "LuauLintSlIntegerTruth",
                                    "[1] is an integer, and SLua counts every integer true, 0i too: [2] asks whether it is not 0", { was, now },
                                    "SlNumberTruth");
                    break;
                case Kind::String:
                    now  = side + " ~= \"\"";
                    said = &problem(e->location, "LuauLintSlStringTruth",
                                    "[1] is a string, and SLua counts every string true, \"\" too: [2] asks whether it is not empty", { was, now },
                                    "SlNumberTruth");
                    break;
                case Kind::Uuid:
                    now  = bracketed(e) + ".istruthy";
                    said = &problem(e->location, "LuauLintSlUuidTruth",
                                    "[1] is a uuid, and SLua counts every uuid true, NULL_KEY too: [2] asks whether it is not NULL_KEY",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::Vector:
                    now  = side + " ~= ZERO_VECTOR";
                    said = &problem(e->location, "LuauLintSlVectorTruth",
                                    "[1] is a vector, and SLua counts every vector true, ZERO_VECTOR too: [2] asks whether it is not zero",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::Quaternion:
                    now  = side + " ~= ZERO_ROTATION";
                    said = &problem(e->location, "LuauLintSlQuaternionTruth",
                                    "[1] is a quaternion, and SLua counts every quaternion true, ZERO_ROTATION too: [2] asks whether it is "
                                    "not ZERO_ROTATION",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::List:
                    now  = "#" + bracketed(e) + " > 0";
                    said = &problem(e->location, "LuauLintSlListTruth",
                                    "[1] is a list, and SLua counts every table true, an empty one too: [2] asks whether it has anything in it",
                                    { was, now }, "SlNumberTruth");
                    break;
                default: return;
            }
            // Not safe: the script did otherwise, whatever it meant.
            offer(*said, now, { edit(e->location, now) }, false);
        }

        // not x, where SLua counts x true whatever it holds: always false.
        // Fixed as LSL's !x asked. Whether it was said.
        bool alwaysFalse(Luau::AstExprUnary* node)
        {
            Luau::AstExpr*            inner = node->expr;
            const std::optional<Kind> kind  = alwaysTrue(inner);
            if (!kind)
            {
                return false;
            }
            const std::string was  = text(inner->location);
            const std::string side = inner->is<Luau::AstExprIfElse>() ? "(" + was + ")" : was;
            ALScriptProblem*  said = nullptr;
            std::string       now;
            switch (*kind)
            {
                case Kind::Number:
                    // Fixed by its key, as before the rest were said.
                    problem(node->location, "LuauLintSlNumberTruthNot",
                            "not [1] is always false: [1] is a number, and Luau counts every number true, 0 too. [1] == 0 asks whether it is 0",
                            { was }, "SlNumberTruth");
                    return true;
                case Kind::Integer:
                    now  = side + " == 0i";
                    said = &problem(node->location, "LuauLintSlIntegerTruthNot",
                                    "not [1] is always false: [1] is an integer, and SLua counts every integer true, 0i too. [2] asks whether "
                                    "it is 0",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::String:
                    now  = side + " == \"\"";
                    said = &problem(node->location, "LuauLintSlStringTruthNot",
                                    "not [1] is always false: [1] is a string, and SLua counts every string true, \"\" too. [2] asks whether "
                                    "it is empty",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::Uuid:
                    now  = "not " + bracketed(inner) + ".istruthy";
                    said = &problem(node->location, "LuauLintSlUuidTruthNot",
                                    "not [1] is always false: [1] is a uuid, and SLua counts every uuid true, NULL_KEY too. [2] asks whether "
                                    "it is NULL_KEY",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::Vector:
                    now  = side + " == ZERO_VECTOR";
                    said = &problem(node->location, "LuauLintSlVectorTruthNot",
                                    "not [1] is always false: [1] is a vector, and SLua counts every vector true, ZERO_VECTOR too. [2] asks "
                                    "whether it is zero",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::Quaternion:
                    now  = side + " == ZERO_ROTATION";
                    said = &problem(node->location, "LuauLintSlQuaternionTruthNot",
                                    "not [1] is always false: [1] is a quaternion, and SLua counts every quaternion true, ZERO_ROTATION too. "
                                    "[2] asks whether it is ZERO_ROTATION",
                                    { was, now }, "SlNumberTruth");
                    break;
                case Kind::List:
                    now  = "#" + bracketed(inner) + " == 0";
                    said = &problem(node->location, "LuauLintSlListTruthNot",
                                    "not [1] is always false: [1] is a list, and SLua counts every table true, an empty one too. [2] asks "
                                    "whether it is empty",
                                    { was, now }, "SlNumberTruth");
                    break;
                default: return false;
            }
            offer(*said, now, { edit(node->location, now) }, false);
            return true;
        }

        std::optional<size_t> offsetOf(const Luau::Position& p) const
        {
            if (p.line >= mStarts.size())
            {
                return std::nullopt;
            }
            return std::min(mSource.size(), mStarts[p.line] + p.column);
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
        boost::unordered_flat_map<Luau::AstLocal*, bool>                  mUnsure;
        boost::unordered_flat_map<Luau::AstLocal*, std::optional<Find>>   mFindLocals;
        boost::unordered_flat_set<const Luau::AstExprCall*>               mStatements;
        // Loops' bodies, and timers' functions: where a call runs often.
        std::vector<Luau::Location>                                       mOften;
        // Each string SlStringBuild has said, by its local or its global's
        // name.
        boost::unordered_flat_set<const void*>                            mBuilt;
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
const ALScriptLintPass::Sleepless* ALScriptLintPass::sleepless(std::string_view lsl)
{
    const auto found = std::find_if(std::begin(SLEEPLESS), std::end(SLEEPLESS), [lsl](const Sleepless& s) { return lsl == s.lsl; });
    return found == std::end(SLEEPLESS) ? nullptr : &*found;
}

// static
std::optional<std::string> ALScriptLintPass::sleeplessArgs(const Sleepless& call, const std::vector<std::string>& args, bool lua)
{
    if (!call.args)
    {
        return std::nullopt;
    }
    std::string out;
    for (const char* at = call.args; *at; ++at)
    {
        if (*at == '$' && at[1] >= '1' && at[1] <= '9')
        {
            const size_t n = static_cast<size_t>(at[1] - '1');
            if (n >= args.size())
            {
                return std::nullopt;
            }
            out += args[n];
            ++at;
        }
        else
        {
            out += lua && *at == '[' ? '{' : lua && *at == ']' ? '}' : *at;
        }
    }
    return out;
}

// static
std::optional<std::pair<std::string, std::string>> ALScriptLintPass::around(const Sleepless& call, bool lua)
{
    // "…$1, $2, …, $n…": every argument once, in its place, as written.
    if (!call.args)
    {
        return std::nullopt;
    }
    const std::string_view args  = call.args;
    const size_t           first = args.find("$1");
    if (first == std::string_view::npos || args.substr(0, first).find('$') != std::string_view::npos)
    {
        return std::nullopt;
    }
    size_t end = first + 2;
    for (char n = '2'; n <= '9' && args.substr(end, 4) == std::string(", $") + n; ++n)
    {
        end += 4;
    }
    if (args.find('$', end) != std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto bracketed = [lua](std::string_view piece) {
        std::string out(piece);
        for (char& c : out)
        {
            c = lua && c == '[' ? '{' : lua && c == ']' ? '}' : c;
        }
        return out;
    };
    return std::make_pair(bracketed(args.substr(0, first)), bracketed(args.substr(end)));
}

// static
const char* ALScriptLintPass::steadyName(std::string_view lsl)
{
    static constexpr std::pair<std::string_view, const char*> STEADY[] = {
        { "llGetCreator", "creator" },
        { "llGetKey", "primKey" },
        { "llGetOwner", "owner" },
        { "llGetScriptName", "scriptName" },
    };
    for (const auto& [name, local] : STEADY)
    {
        if (name == lsl)
        {
            return local;
        }
    }
    return nullptr;
}

// static
const ALScriptLintPass::PrimParams* ALScriptLintPass::primParams(std::string_view lsl)
{
    const auto found = std::find_if(std::begin(PRIM_PARAMS), std::end(PRIM_PARAMS), [lsl](const PrimParams& p) { return lsl == p.lsl; });
    return found == std::end(PRIM_PARAMS) ? nullptr : &*found;
}

// static
std::string ALScriptLintPass::mergedRules(const std::vector<PrimCall>& calls, bool lua)
{
    std::string items;
    const auto  add = [&](const std::string& piece) {
        if (!piece.empty())
        {
            items += (items.empty() ? "" : ", ") + piece;
        }
    };
    // Whose the rules so far are: the first call's link, until a rule sends
    // them elsewhere.
    std::string link  = calls.empty() ? std::string() : calls.front().link;
    bool        known = true;
    for (size_t i = 0; i < calls.size(); ++i)
    {
        if (i > 0 && (!known || calls[i].link != link))
        {
            add("PRIM_LINK_TARGET, " + calls[i].link);
            link = calls[i].link;
        }
        add(calls[i].rules);
        known = !calls[i].targets;
    }
    return lua ? "{" + items + "}" : "[" + items + "]";
}

// static
bool ALScriptLintPass::migration(const ALScriptProblem& problem)
{
    if (problem.key == "SluaNote")
    {
        return true;
    }
    if (problem.source == ALScriptProblem::Source::Lint)
    {
        const Rule* own = rule(problem.code);
        if (own && own->migration)
        {
            return true;
        }
    }
    return problem.key.rfind("LuauLintDeprecatedMemberUse", 0) == 0 && !problem.args.empty() && problem.args[0].rfind("llcompat.", 0) == 0;
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

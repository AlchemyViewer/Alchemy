/**
 * @file allsltoslua.cpp
 * @brief An LSL script written again as SLua, where the two languages differ noted rather than guessed.
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

#include "allsltoslua.h"

#include "alscriptengine.h"
#include "allslservice.h"
#include "allsltraits.h"
#include "allslvalues.h"

#include <tailslide/tailslide.hh>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string_view>

using namespace Tailslide;

namespace
{
    // Luau's precedence, loosest first: an if-expression holds everything to
    // its right, so it is bracketed wherever it is an operand.
    enum Prec : int
    {
        IF_EXPR = 0,
        OR      = 1,
        AND     = 2,
        COMPARE = 3,
        CONCAT  = 4,
        ADD     = 5,
        MUL     = 6,
        UNARY   = 7,
        POWER   = 8,
        PRIMARY = 9
    };

    // An expression as written: its text, how tightly it binds, and whether
    // it is a Luau boolean where LSL has 1 or 0.
    struct Expr
    {
        std::string text;
        int         prec    = PRIMARY;
        bool        boolean = false;
    };

    std::string bracketed(const Expr& e, int at_least)
    {
        return e.prec < at_least ? "(" + e.text + ")" : e.text;
    }

    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    // What Luau reserves, or SLua has for its own, that an LSL name may be.
    bool reservedName(std::string_view name)
    {
        static const boost::unordered_flat_set<std::string_view> RESERVED = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not", "or",
            "repeat", "return", "then", "true", "until", "while", "continue", "export", "type", "typeof",
            // SLua's own and Luau's libraries, which a local of the name would hide.
            "ll", "llcompat", "LLEvents", "LLTimers", "vector", "quaternion", "rotation", "uuid", "bit32", "string", "table",
            "math", "utf8", "buffer", "coroutine", "os", "debug", "print", "tostring", "tonumber", "tovector", "toquaternion",
            "torotation", "touuid", "ipairs", "pairs", "next", "select", "error", "assert", "pcall", "xpcall", "unpack",
            "rawget", "rawset", "rawequal", "rawlen", "setmetatable", "getmetatable", "require", "lljson", "llbase64",
            // What the text written here defines of its own.
            "states", "currentState", "setState", "joinLists", "lslInteger", "lslFloat", "detected", "setTimer", "timerHandle",
            "timerHandler",
        };
        return RESERVED.contains(name);
    }

    // SLua's events that hand their handler the detected table in place of
    // LSL's count.
    bool detectedEvent(std::string_view name)
    {
        static const boost::unordered_flat_set<std::string_view> DETECTED = {
            "collision", "collision_end", "collision_start", "final_damage", "on_damage", "sensor", "touch", "touch_end", "touch_start",
        };
        return DETECTED.contains(name);
    }

    bool isNull(LSLASTNode* node)
    {
        return !node || node->getNodeType() == NODE_NULL;
    }

    std::string luaString(std::string_view text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (u < 0x20 || u == 0x7f)
                    {
                        out += llformat("\\%03d", u);
                    }
                    else
                    {
                        out += c;
                    }
                    break;
            }
        }
        return out + "\"";
    }

    std::string number(double v)
    {
        if (std::isnan(v))
        {
            return "(0 / 0)";
        }
        if (std::isinf(v))
        {
            return v > 0 ? "math.huge" : "-math.huge";
        }
        // A single's shortest text: what LSL's float was.
        return ALLSLValues::floatText(v, false);
    }

    // What a variable of an LSL type holds before anything is put in it.
    std::string defaultOf(LSLIType type)
    {
        switch (type)
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return "0";
            case LST_STRING: return "\"\"";
            case LST_KEY: return "uuid(\"\")";
            case LST_VECTOR: return "ZERO_VECTOR";
            case LST_QUATERNION: return "ZERO_ROTATION";
            case LST_LIST: return "{}";
            default: return "nil";
        }
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

    class Writer
    {
    public:
        Writer(LSLScript* script, const ALLSLToSLua::Options& options) : mScript(script), mOptions(options) {}

        std::string write();
        ALScriptProblems& notes() { return mNotes; }

    private:
        // --- where the two languages differ -----------------------------------------

        // Said over the statement being written, and at the LSL's place;
        // once a statement.
        void note(LSLASTNode* at, const std::string& key, const std::string& said);
        // The same, once in the script.
        void noteOnce(LSLASTNode* at, const std::string& key, const std::string& said);

        // --- names ------------------------------------------------------------------

        std::string nameOf(LSLIdentifier* id);
        std::string nameOf(const char* lsl);
        std::string stateKey(const std::string& state) const;

        // --- the text ---------------------------------------------------------------

        void line(const std::string& text);
        std::string indent() const { return std::string(static_cast<size_t>(mDepth) * 4, ' '); }

        // --- expressions ------------------------------------------------------------

        Expr expr(LSLExpression* e);
        // As a number where LSL has one, a boolean made 1 or 0.
        Expr value(LSLExpression* e);
        // As a boolean, as LSL reads a value in a condition.
        Expr condition(LSLExpression* e);
        // As a value of another LSL type, where LSL converts on its own.
        Expr coerced(LSLExpression* e, LSLIType to);
        Expr constant(LSLConstant* c);
        Expr lvalue(LSLLValueExpression* e);
        Expr call(LSLFunctionExpression* e);
        Expr binary(LSLBinaryExpression* e);
        Expr unary(LSLUnaryExpression* e);
        Expr typecast(LSLTypecastExpression* e);
        // An assignment or a step inside an expression, which SLua makes a
        // statement: done in a function called on the spot.
        Expr sideEffect(LSLExpression* e);
        std::string args(LSLASTNode* list, LSLParamList* params);
        // What SLua has in a library call's stead, where it means the same;
        // nothing where it has nothing.
        std::optional<Expr> idiom(LSLFunctionExpression* e, const std::string& lsl);
        // What was detected, from the handler's own table.
        std::optional<Expr> detected(LSLFunctionExpression* e, const std::string& lsl);
        // A call to a function answering an index or -1, which ll answers
        // from one or nil, read against nil where it is only asked whether
        // it found: `found` says whether the test is of finding.
        std::optional<Expr> foundTest(LSLExpression* call, bool found);
        // A call whose answer is an index or -1, that ll can make with the
        // arguments it has.
        LSLFunctionExpression* findCall(LSLExpression* e);
        // Whether each of a call's index arguments is a whole number
        // written out, which ll takes moved on by one.
        bool constantIndexes(LSLFunctionExpression* e, U16 indexes);
        // The call's arguments for ll: its index arguments moved on by one.
        std::string llArgs(LSLFunctionExpression* e, U16 indexes);
        // `: type` for an LSL type, where types are written.
        std::string typed(LSLIType type) const;

        // --- statements -------------------------------------------------------------

        void statement(LSLASTNode* s, bool last);
        void block(LSLASTNode* s);
        // An expression standing as a statement: an assignment, a step, a
        // call.
        void effect(LSLExpression* e);
        void assign(LSLLValueExpression* target, LSLOperator op, LSLExpression* rhs);
        void stateChange(LSLStateStatement* s, bool last);

        // --- loops as Luau counts -------------------------------------------------

        // A for that counts one variable from a start to a limit by a
        // constant step: its variable, where it starts, the check's limit
        // and comparison, and the step. None for any other shape.
        struct Counting
        {
            LSLSymbol*     var   = nullptr;
            LSLIdentifier* id    = nullptr;
            LSLExpression* from  = nullptr;
            LSLExpression* limit = nullptr;
            LSLOperator    check = OP_NONE;
            int            step  = 0;
        };
        std::optional<Counting> counting(LSLForStatement* f) const;
        // Whether an expression reads the same on every turn of a loop:
        // nothing it reads is set in the loop, and it calls only what the
        // definitions call pure.
        bool steadyIn(LSLExpression* e, LSLASTNode* loop) const;
        // Before a function's or a handler's body is written: its for
        // loops that Luau's numeric for says exactly, and the variables
        // that only such loops use, whose declarations go.
        void prepareBody(LSLASTNode* body);
        void numericFor(LSLForStatement* f, const Counting& c);

        // --- truth values ---------------------------------------------------------

        // The integers the script uses only as truth values, and its
        // functions that answer only one, which are written as Luau's
        // booleans: never stepped or added to, and read only as a condition,
        // an operand of !, && or ||, returned by such a function, copied into
        // another such, or -- where only ever TRUE or FALSE -- compared with
        // one of them.
        void findBooleans();
        // Whether an expression is only ever TRUE or FALSE, given those
        // taken to be so far.
        bool zeroOrOne(LSLExpression* e) const;
        // Whether a read of a variable or a call is one a truth value stands
        // for, given those taken as booleans so far.
        bool readAsTruth(LSLASTNode* read) const;
        // What a truth value is set to: true and false for whole numbers,
        // else the value as a condition.
        std::string truthOf(LSLExpression* e);
        // A truth value compared with TRUE or FALSE: itself, or not it.
        std::optional<Expr> truthAsked(LSLBinaryExpression* e);
        bool        boolean(LSLSymbol* var) const { return var && mBooleans.contains(var); }

        // --- the script -------------------------------------------------------------

        void globals();
        void functions();
        void states();
        void singleState(LSLState* state);
        void multiState();
        // What a script of several states has before anything that could
        // change state: the tables and setState; and the timer on LLTimers,
        // before anything that could set it.
        void statesPreamble();
        void timersPreamble();
        std::string handlerParams(LSLEventHandler* handler, std::string& lead);
        void handlerBody(LSLEventHandler* handler);
        void helpers(std::string& out);

        LSLScript*                                       mScript;
        const ALLSLToSLua::Options&                      mOptions;
        std::string                                      mText;
        int                                              mDepth = 0;
        std::vector<std::string>                         mPending;
        boost::unordered_flat_set<std::string>           mOnce;
        ALScriptProblems                                 mNotes;
        boost::unordered_flat_map<LSLSymbol*, std::string> mNames;
        boost::unordered_flat_set<std::string>           mTaken;
        // What the text needs of its own, defined once over it.
        bool mJoinLists  = false;
        bool mLslInteger = false;
        bool mLslFloat   = false;
        bool mManyStates = false;
        // llSetTimerEvent's timer on LLTimers, where the script sets one.
        bool mTimers = false;
        // Inside a handler of an event SLua hands what was detected, where
        // the detected table is read.
        bool mInDetected = false;
        // The loops being written, innermost last: a for's steps, which a
        // jump to its end runs before `continue`.
        std::vector<LSLASTNode*> mLoops;
        // The labels a jump reaches that SLua has nothing for.
        boost::unordered_flat_set<LSLSymbol*> mUnstructured;
        // Whether a `return` from the handler being written ends the
        // script's setup: state_entry of a script of one state, written at
        // the top level.
        bool mTopLevel = false;
        // Lines made for an expression's function, whose notes go over the
        // statement it stands in.
        bool mInline = false;
        // The for loops of the body being written that are Luau's numeric
        // for, and the variables used by nothing else.
        boost::unordered_flat_map<LSLForStatement*, Counting> mNumeric;
        boost::unordered_flat_set<LSLSymbol*>                 mLoopOnly;
        boost::unordered_flat_set<LSLSymbol*>                 mBooleans;
        boost::unordered_flat_set<LSLSymbol*>                 mZeroOne;
        // The function of the script's own being written.
        LSLSymbol* mFunction = nullptr;
    };

    void Writer::note(LSLASTNode* at, const std::string& key, const std::string& said)
    {
        if (std::find(mPending.begin(), mPending.end(), said) != mPending.end())
        {
            return;
        }
        mPending.push_back(said);
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Note;
        p.source   = ALScriptProblem::Source::Assistant;
        p.key      = key;
        p.message  = said;
        if (at)
        {
            p.line      = zeroBased(at->getLoc()->first_line);
            p.column    = zeroBased(at->getLoc()->first_column);
            p.endLine   = zeroBased(at->getLoc()->last_line);
            p.endColumn = std::max(0, at->getLoc()->last_column);
        }
        mNotes.push_back(std::move(p));
    }

    void Writer::noteOnce(LSLASTNode* at, const std::string& key, const std::string& said)
    {
        if (mOnce.insert(key).second)
        {
            note(at, key, said);
        }
    }

    std::string Writer::nameOf(LSLIdentifier* id)
    {
        LSLSymbol* symbol = id ? id->getSymbol() : nullptr;
        if (symbol)
        {
            if (const auto found = mNames.find(symbol); found != mNames.end())
            {
                return found->second;
            }
        }
        std::string name = nameOf(id ? id->getName() : "");
        if (symbol)
        {
            mNames.emplace(symbol, name);
        }
        return name;
    }

    std::string Writer::nameOf(const char* lsl)
    {
        // A name Luau or SLua holds for its own, with a mark after it.
        std::string name = lsl ? lsl : "";
        while (reservedName(name))
        {
            name += "_";
        }
        return name;
    }

    std::string Writer::stateKey(const std::string& state) const
    {
        return reservedName(state) ? "[" + luaString(state) + "]" : "." + state;
    }

    void Writer::line(const std::string& text)
    {
        // What was noted of it, over it -- over the statement a line made
        // inside an expression stands in, where it is one.
        if (!mInline)
        {
            for (const std::string& said : mPending)
            {
                if (mOptions.comments)
                {
                    mText += indent() + "-- LSL: " + said + "\n";
                }
            }
            mPending.clear();
        }
        mText += text.empty() ? std::string("\n") : indent() + text + "\n";
    }

    // --- expressions ------------------------------------------------------------------

    Expr Writer::constant(LSLConstant* c)
    {
        switch (c->getNodeSubType())
        {
            case NODE_INTEGER_CONSTANT:
            {
                const int v = static_cast<LSLIntegerConstant*>(c)->getValue();
                return { std::to_string(v), v < 0 ? UNARY : PRIMARY };
            }
            case NODE_FLOAT_CONSTANT:
            {
                const double v = static_cast<LSLFloatConstant*>(c)->getValue();
                return { number(v), std::signbit(v) ? UNARY : PRIMARY };
            }
            case NODE_KEY_CONSTANT:
                return { "uuid(" + luaString(static_cast<LSLKeyConstant*>(c)->getValue()) + ")" };
            case NODE_STRING_CONSTANT:
                return { luaString(static_cast<LSLStringConstant*>(c)->getValue()) };
            case NODE_VECTOR_CONSTANT:
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(c)->getValue();
                return { "vector(" + number(v->x) + ", " + number(v->y) + ", " + number(v->z) + ")" };
            }
            case NODE_QUATERNION_CONSTANT:
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(c)->getValue();
                return { "quaternion(" + number(q->x) + ", " + number(q->y) + ", " + number(q->z) + ", " + number(q->s) + ")" };
            }
            case NODE_LIST_CONSTANT:
            {
                std::string out;
                for (LSLConstant* item = static_cast<LSLListConstant*>(c)->getValue(); item; item = static_cast<LSLConstant*>(item->getNext()))
                {
                    out += (out.empty() ? "" : ", ") + constant(item).text;
                }
                return { "{" + out + "}" };
            }
            default:
                return { "nil" };
        }
    }

    Expr Writer::lvalue(LSLLValueExpression* e)
    {
        LSLIdentifier* id     = e->getIdentifier();
        LSLSymbol*     symbol = id->getSymbol();
        std::string    name;
        if (symbol && symbol->getSubType() == SYM_BUILTIN)
        {
            // LSL's constants are SLua's globals by the same names, but
            // TRUE and FALSE, which SLua has as true and false.
            const std::string_view lsl = id->getName();
            if (lsl == "TRUE" || lsl == "FALSE")
            {
                return { lsl == "TRUE" ? "1" : "0" };
            }
            name = id->getName();
        }
        else
        {
            name = nameOf(id);
        }
        LSLASTNode* member = e->getMember();
        if (!isNull(member))
        {
            name += std::string(".") + static_cast<LSLIdentifier*>(member)->getName();
        }
        return { name, PRIMARY, isNull(member) && boolean(symbol) };
    }

    Expr Writer::value(LSLExpression* e)
    {
        Expr out = expr(e);
        if (out.boolean)
        {
            return { "if " + out.text + " then 1 else 0", IF_EXPR };
        }
        return out;
    }

    Expr Writer::condition(LSLExpression* e)
    {
        // What stands for true or false outright.
        LSLExpression* inner = e;
        while (inner && inner->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            inner = static_cast<LSLParenthesisExpression*>(inner)->getChildExpr();
        }
        if (inner && inner->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
        {
            return condition(static_cast<LSLBoolConversionExpression*>(inner)->getChildExpr());
        }
        if (inner && inner->getNodeSubType() == NODE_LVALUE_EXPRESSION)
        {
            LSLIdentifier* id = static_cast<LSLLValueExpression*>(inner)->getIdentifier();
            if (id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN &&
                (std::string_view(id->getName()) == "TRUE" || std::string_view(id->getName()) == "FALSE"))
            {
                return { std::string_view(id->getName()) == "TRUE" ? "true" : "false", PRIMARY, true };
            }
        }
        // ~ of a find, as LSL asks whether it found: -1 is all bits.
        if (inner && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BIT_NOT)
        {
            if (std::optional<Expr> test = foundTest(static_cast<LSLUnaryExpression*>(inner)->getChildExpr(), true))
            {
                return *test;
            }
        }
        Expr out = expr(e);
        if (out.boolean)
        {
            return out;
        }
        // LSL's truth: a number not nought, a string not empty, a key that
        // is one and not the null key, a vector or a rotation not zero, a list
        // with something in it. In Luau everything but nil and false is true.
        switch (e->getIType())
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return { bracketed(out, COMPARE + 1) + " ~= 0", COMPARE, true };
            case LST_STRING: return { bracketed(out, COMPARE + 1) + " ~= \"\"", COMPARE, true };
            case LST_KEY: return { bracketed(out, PRIMARY) + ".istruthy", PRIMARY, true };
            case LST_VECTOR: return { bracketed(out, COMPARE + 1) + " ~= ZERO_VECTOR", COMPARE, true };
            case LST_QUATERNION: return { bracketed(out, COMPARE + 1) + " ~= ZERO_ROTATION", COMPARE, true };
            case LST_LIST: return { "#" + bracketed(out, UNARY) + " > 0", COMPARE, true };
            default: return { out.text, out.prec, true };
        }
    }

    Expr Writer::coerced(LSLExpression* e, LSLIType to)
    {
        Expr           out  = value(e);
        const LSLIType from = e->getIType();
        if (to == LST_KEY && from == LST_STRING)
        {
            return { "uuid(" + out.text + ")" };
        }
        if (to == LST_STRING && from == LST_KEY)
        {
            return { "tostring(" + out.text + ")" };
        }
        return out;
    }

    std::string Writer::args(LSLASTNode* list, LSLParamList* params)
    {
        std::string out;
        LSLASTNode* param = params ? params->getChild(0) : nullptr;
        for (LSLASTNode* arg = isNull(list) ? nullptr : list->getChild(0); arg; arg = arg->getNext())
        {
            const LSLIType to = param ? param->getIType() : static_cast<LSLExpression*>(arg)->getIType();
            out += (out.empty() ? "" : ", ") + coerced(static_cast<LSLExpression*>(arg), to).text;
            param = param ? param->getNext() : nullptr;
        }
        return out;
    }

    // A whole number written out, and what it is: a constant, or one with a
    // minus before it.
    bool wholeNumber(LSLExpression* e, int& v)
    {
        while (e && e->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            e = static_cast<LSLParenthesisExpression*>(e)->getChildExpr();
        }
        if (!e)
        {
            return false;
        }
        if (e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            v = static_cast<LSLIntegerConstant*>(e->getChild(0))->getValue();
            return true;
        }
        if (e->getNodeSubType() == NODE_UNARY_EXPRESSION && e->getOperation() == OP_MINUS &&
            wholeNumber(static_cast<LSLUnaryExpression*>(e)->getChildExpr(), v))
        {
            v = -v;
            return true;
        }
        return false;
    }

    LSLExpression* argumentAt(LSLFunctionExpression* e, int at)
    {
        LSLASTNode* arg = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0);
        for (int i = 0; arg && i < at; ++i)
        {
            arg = arg->getNext();
        }
        return static_cast<LSLExpression*>(arg);
    }

    std::string Writer::typed(LSLIType type) const
    {
        if (!mOptions.types)
        {
            return std::string();
        }
        switch (type)
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return ": number";
            case LST_STRING: return ": string";
            case LST_KEY: return ": uuid";
            case LST_VECTOR: return ": vector";
            case LST_QUATERNION: return ": quaternion";
            case LST_LIST: return ": { any }";
            default: return std::string();
        }
    }

    bool Writer::constantIndexes(LSLFunctionExpression* e, U16 indexes)
    {
        for (int i = 0; i < 16; ++i)
        {
            int v = 0;
            if ((indexes & (1 << i)) && !wholeNumber(argumentAt(e, i), v))
            {
                return false;
            }
        }
        return true;
    }

    std::string Writer::llArgs(LSLFunctionExpression* e, U16 indexes)
    {
        // An index from nought moved on by one; one from the end, which
        // counts back from -1 either way, as it is.
        LSLSymbol*    symbol = e->getIdentifier()->getSymbol();
        LSLParamList* params = symbol ? symbol->getFunctionDecl() : nullptr;
        LSLASTNode*   param  = params ? params->getChild(0) : nullptr;
        std::string   out;
        int           at = 0;
        for (LSLASTNode* arg = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0); arg; arg = arg->getNext(), ++at)
        {
            int         v = 0;
            std::string one;
            if ((indexes & (1 << at)) && wholeNumber(static_cast<LSLExpression*>(arg), v))
            {
                one = std::to_string(v >= 0 ? v + 1 : v);
            }
            else
            {
                const LSLIType to = param ? param->getIType() : static_cast<LSLExpression*>(arg)->getIType();
                one               = coerced(static_cast<LSLExpression*>(arg), to).text;
            }
            out += (out.empty() ? "" : ", ") + one;
            param = param ? param->getNext() : nullptr;
        }
        return out;
    }

    std::optional<Expr> Writer::detected(LSLFunctionExpression* e, const std::string& lsl)
    {
        // Each llDetected* and the detected event's method that says the
        // same; the group as a boolean, as SLua has it.
        static const boost::unordered_flat_map<std::string_view, std::string_view> METHODS = {
            { "llDetectedKey", "getKey" },           { "llDetectedName", "getName" },
            { "llDetectedOwner", "getOwner" },       { "llDetectedGroup", "getGroup" },
            { "llDetectedPos", "getPos" },           { "llDetectedRot", "getRot" },
            { "llDetectedVel", "getVel" },           { "llDetectedLinkNumber", "getLinkNumber" },
            { "llDetectedGrab", "getGrab" },         { "llDetectedTouchFace", "getTouchFace" },
            { "llDetectedTouchPos", "getTouchPos" }, { "llDetectedTouchNormal", "getTouchNormal" },
            { "llDetectedTouchBinormal", "getTouchBinormal" }, { "llDetectedTouchST", "getTouchST" },
            { "llDetectedTouchUV", "getTouchUV" },   { "llDetectedType", "getType" },
            { "llDetectedRezzer", "getRezzer" },     { "llDetectedDamage", "getDamage" },
        };
        const auto found = METHODS.find(lsl);
        LSLExpression* index = argumentAt(e, 0);
        if (found == METHODS.end() || !index)
        {
            return std::nullopt;
        }
        noteOnce(e, "SluaDetectedTable", "detected[n] is what LSL read with llDetected*(n - 1): SLua counts it from 1, and one past its "
                                         "end is an error, where LSL answered nothing.");
        int v = 0;
        const std::string at = wholeNumber(index, v) ? std::to_string(v + 1) : bracketed(value(index), ADD + 1) + " + 1";
        return Expr{ "detected[" + at + "]:" + std::string(found->second) + "()", PRIMARY, found->second == "getGroup" };
    }

    std::optional<Expr> Writer::idiom(LSLFunctionExpression* e, const std::string& lsl)
    {
        const auto arg = [&](int at) { return value(argumentAt(e, at)); };
        if (lsl == "llPow")
        {
            // Luau's ^ binds tighter than a minus before it.
            return Expr{ bracketed(arg(0), POWER + 1) + " ^ " + bracketed(arg(1), UNARY), POWER };
        }
        if (lsl == "llVecMag" || lsl == "llVecNorm")
        {
            return Expr{ std::string(lsl == "llVecMag" ? "vector.magnitude(" : "vector.normalize(") + arg(0).text + ")" };
        }
        if (lsl == "llVecDist")
        {
            return Expr{ "vector.magnitude(" + bracketed(arg(0), ADD) + " - " + bracketed(arg(1), ADD + 1) + ")" };
        }
        if (lsl == "llRot2Fwd" || lsl == "llRot2Left" || lsl == "llRot2Up")
        {
            const char* fn = lsl == "llRot2Fwd" ? "tofwd" : lsl == "llRot2Left" ? "toleft" : "toup";
            return Expr{ std::string("quaternion.") + fn + "(" + arg(0).text + ")" };
        }
        if (lsl == "llGetUnixTime")
        {
            return Expr{ "os.time()" };
        }
        if (lsl == "llOwnerSay")
        {
            return Expr{ "print(" + coerced(argumentAt(e, 0), LST_STRING).text + ")" };
        }
        // Luau's math where it answers what LSL's did: not llRound, which
        // rounds a half up where math.round rounds it away from nought.
        static const boost::unordered_flat_map<std::string_view, std::string_view> MATH = {
            { "llAbs", "abs" },   { "llFabs", "abs" },   { "llAcos", "acos" }, { "llAsin", "asin" },   { "llAtan2", "atan2" },
            { "llCeil", "ceil" }, { "llCos", "cos" },    { "llFloor", "floor" }, { "llSin", "sin" },   { "llTan", "tan" },
            { "llSqrt", "sqrt" }, { "llLog", "log" },    { "llLog10", "log10" },
        };
        if (const auto found = MATH.find(lsl); found != MATH.end())
        {
            if (lsl == "llSqrt" || lsl == "llLog" || lsl == "llLog10")
            {
                noteOnce(e, "SluaMathDomain", "math.sqrt, math.log and math.log10 answer nan or -inf out of their range, where LSL "
                                              "answered 0 or stopped with a math error.");
            }
            std::string out;
            for (LSLASTNode* a = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0); a; a = a->getNext())
            {
                out += (out.empty() ? "" : ", ") + value(static_cast<LSLExpression*>(a)).text;
            }
            return Expr{ "math." + std::string(found->second) + "(" + out + ")" };
        }
        return std::nullopt;
    }

    LSLFunctionExpression* Writer::findCall(LSLExpression* e)
    {
        while (e && e->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            e = static_cast<LSLParenthesisExpression*>(e)->getChildExpr();
        }
        if (!mOptions.sluaCalls || !e || e->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            return nullptr;
        }
        auto*      call   = static_cast<LSLFunctionExpression*>(e);
        LSLSymbol* symbol = call->getIdentifier()->getSymbol();
        if (!symbol || symbol->getSubType() != SYM_BUILTIN)
        {
            return nullptr;
        }
        const ALLSLTraits::Trait* trait = ALLSLTraits::of(call->getIdentifier()->getName());
        if (!trait || !(trait->slua & ALLSLTraits::SluaIndexResult) || (trait->slua & (ALLSLTraits::SluaRemoved | ALLSLTraits::SluaBool)))
        {
            return nullptr;
        }
        return !(trait->slua & ALLSLTraits::SluaIndexArgs) || constantIndexes(call, trait->sluaIndexArgs) ? call : nullptr;
    }

    std::optional<Expr> Writer::foundTest(LSLExpression* e, bool found)
    {
        LSLFunctionExpression* call = findCall(e);
        if (!call)
        {
            return std::nullopt;
        }
        const std::string lsl   = call->getIdentifier()->getName();
        LSLExpression*    among = argumentAt(call, 1);
        std::string       asked;
        // One thing looked for in a list: table.find, which answers as
        // ll.ListFindList does.
        if (mOptions.idioms && lsl == "llListFindList" && among && among->getNodeSubType() == NODE_LIST_EXPRESSION && among->getChild(0) &&
            !among->getChild(0)->getNext())
        {
            asked = "table.find(" + value(argumentAt(call, 0)).text + ", " + value(static_cast<LSLExpression*>(among->getChild(0))).text + ")";
        }
        else
        {
            const ALLSLTraits::Trait* trait = ALLSLTraits::of(lsl.c_str());
            asked = "ll." + lsl.substr(2) + "(" + llArgs(call, trait ? trait->sluaIndexArgs : 0) + ")";
        }
        return Expr{ asked + (found ? " ~= nil" : " == nil"), COMPARE, true };
    }

    Expr Writer::call(LSLFunctionExpression* e)
    {
        LSLIdentifier* id     = e->getIdentifier();
        LSLSymbol*     symbol = id->getSymbol();
        LSLParamList*  params = symbol ? symbol->getFunctionDecl() : nullptr;
        if (!symbol || symbol->getSubType() != SYM_BUILTIN)
        {
            return { nameOf(id) + "(" + args(e->getArguments(), params) + ")", PRIMARY, boolean(symbol) };
        }
        const std::string lsl  = id->getName();
        const std::string bare = lsl.rfind("ll", 0) == 0 ? lsl.substr(2) : lsl;
        // A list's length is its length.
        if (lsl == "llGetListLength")
        {
            return { "#" + bracketed(value(static_cast<LSLExpression*>(e->getArguments()->getChild(0))), UNARY), UNARY };
        }
        if (lsl == "llSetTimerEvent" && mTimers)
        {
            return { "setTimer(" + args(e->getArguments(), params) + ")" };
        }
        if (mOptions.detectedTable && mInDetected)
        {
            if (std::optional<Expr> read = detected(e, lsl))
            {
                return *read;
            }
        }
        if (mOptions.idioms)
        {
            if (std::optional<Expr> said = idiom(e, lsl))
            {
                return *said;
            }
        }
        const ALLSLTraits::Trait* trait   = ALLSLTraits::of(lsl.c_str());
        const U8                  slua    = trait ? trait->slua : 0;
        const U16                 indexes = trait ? trait->sluaIndexArgs : 0;
        // SLua's ll where it means the same: a boolean answer, which a
        // condition reads as it is and a number takes as 1 or 0; index
        // arguments written out, moved on by one.
        if (slua == 0 || (mOptions.sluaCalls && !(slua & (ALLSLTraits::SluaRemoved | ALLSLTraits::SluaIndexResult)) &&
                          (!(slua & ALLSLTraits::SluaIndexArgs) || constantIndexes(e, indexes))))
        {
            if (trait && trait->sluaUse)
            {
                noteOnce(e, "SluaUse" + lsl, "SLua would use " + std::string(trait->sluaUse) + " for " + lsl + ".");
            }
            const std::string called = (slua & ALLSLTraits::SluaIndexArgs) ? llArgs(e, indexes) : args(e->getArguments(), params);
            return { "ll." + bare + "(" + called + ")", PRIMARY, (slua & ALLSLTraits::SluaBool) != 0 };
        }
        if (slua & ALLSLTraits::SluaRemoved)
        {
            noteOnce(e, "SluaCompatOnly" + lsl,
                     lsl == "llSetTimerEvent" ? "SLua's ll has no SetTimerEvent; llcompat's is LSL's. SLua's own timers are "
                                                "LLTimers:every(seconds, callback) and LLTimers:once, several at a time."
                                              : "SLua's ll has no " + bare + "; llcompat." + bare + " is LSL's.");
        }
        else if (lsl.rfind("llDetected", 0) == 0)
        {
            noteOnce(e, "SluaDetected", "llcompat.Detected* count from 0, as LSL's did. SLua's own way is the handler's detected table: "
                                        "detected[n + 1]:getKey(), :getName(), :getPos() and the rest.");
        }
        else if (slua & ALLSLTraits::SluaIndexResult)
        {
            noteOnce(e, "SluaIndex" + lsl, "llcompat." + bare + " counts from 0 and says -1 for none, as LSL did; ll." + bare +
                                               " counts from 1 and says nil.");
        }
        else if (slua & ALLSLTraits::SluaIndexArgs)
        {
            noteOnce(e, "SluaIndex" + lsl, "llcompat." + bare + " takes indexes from 0, as LSL did; ll." + bare + " takes them from 1.");
        }
        else if (slua & ALLSLTraits::SluaBool)
        {
            noteOnce(e, "SluaBool" + lsl, "llcompat." + bare + " answers 1 or 0, as LSL did; ll." + bare + " answers true or false.");
        }
        if (trait && trait->sluaUse)
        {
            noteOnce(e, "SluaUse" + lsl, "SLua would use " + std::string(trait->sluaUse) + " for " + lsl + ".");
        }
        return { "llcompat." + bare + "(" + args(e->getArguments(), params) + ")" };
    }

    Expr Writer::binary(LSLBinaryExpression* e)
    {
        const LSLOperator op  = e->getOperation();
        LSLExpression*    lhs = e->getLHS();
        LSLExpression*    rhs = e->getRHS();
        const LSLIType    lt  = lhs->getIType();
        const LSLIType    rt  = rhs->getIType();
        const auto        infix = [&](const char* word, int prec, bool boolean = false, bool right_assoc = false) -> Expr {
            const Expr a = value(lhs);
            const Expr b = value(rhs);
            return { bracketed(a, right_assoc ? prec + 1 : prec) + " " + word + " " + bracketed(b, right_assoc ? prec : prec + 1), prec, boolean };
        };
        // A find's answer asked only whether it found: against nil, as ll
        // answers, where LSL's -1 said nothing was found.
        {
            int         v    = 0;
            const bool  left = findCall(lhs) != nullptr;
            const bool  right = !left && findCall(rhs) != nullptr;
            LSLExpression* call  = left ? lhs : rhs;
            LSLExpression* other = left ? rhs : lhs;
            if ((left || right) && wholeNumber(other, v))
            {
                // Each test by what it says of the find, with the call on
                // its left: found, or not.
                std::optional<bool> found;
                LSLOperator         as = op;
                if (right)
                {
                    as = op == OP_LESS ? OP_GREATER : op == OP_GREATER ? OP_LESS : op == OP_LEQ ? OP_GEQ : op == OP_GEQ ? OP_LEQ : op;
                }
                if ((as == OP_EQ && v == -1) || (as == OP_LESS && v == 0) || (as == OP_LEQ && v == -1))
                {
                    found = false;
                }
                else if ((as == OP_NEQ && v == -1) || (as == OP_GEQ && v == 0) || (as == OP_GREATER && v == -1))
                {
                    found = true;
                }
                if (found)
                {
                    if (std::optional<Expr> test = foundTest(call, *found))
                    {
                        return *test;
                    }
                }
            }
        }
        const auto bit = [&](const char* fn) -> Expr {
            noteOnce(e, "SluaBit32", "bit32 answers 0 to 4294967295; LSL's integers were signed, from -2147483648.");
            return { std::string("bit32.") + fn + "(" + value(lhs).text + ", " + value(rhs).text + ")" };
        };
        switch (op)
        {
            case OP_ASSIGN:
            case OP_ADD_ASSIGN:
            case OP_SUB_ASSIGN:
            case OP_MUL_ASSIGN:
            case OP_DIV_ASSIGN:
            case OP_MOD_ASSIGN:
                return sideEffect(e);
            case OP_PLUS:
                if (lt == LST_LIST || rt == LST_LIST)
                {
                    mJoinLists = true;
                    const Expr a = value(lhs);
                    const Expr b = value(rhs);
                    return { "joinLists(" + (lt == LST_LIST ? a.text : "{" + a.text + "}") + ", " + (rt == LST_LIST ? b.text : "{" + b.text + "}") + ")" };
                }
                if (lt == LST_STRING || lt == LST_KEY || rt == LST_STRING || rt == LST_KEY)
                {
                    const Expr a = coerced(lhs, LST_STRING);
                    const Expr b = coerced(rhs, LST_STRING);
                    return { bracketed(a, CONCAT + 1) + " .. " + bracketed(b, CONCAT), CONCAT };
                }
                return infix("+", ADD);
            case OP_MINUS: return infix("-", ADD);
            case OP_MUL:
                if (lt == LST_VECTOR && rt == LST_VECTOR)
                {
                    return { "vector.dot(" + value(lhs).text + ", " + value(rhs).text + ")" };
                }
                return infix("*", MUL);
            case OP_DIV:
                if (lt == LST_INTEGER && rt == LST_INTEGER)
                {
                    note(e, "SluaIntegerDivision", "// rounds down, and LSL's integer / rounded toward zero: they differ where the answer is negative.");
                    return infix("//", MUL);
                }
                return infix("/", MUL);
            case OP_MOD:
                if (lt == LST_VECTOR && rt == LST_VECTOR)
                {
                    return { "vector.cross(" + value(lhs).text + ", " + value(rhs).text + ")" };
                }
                note(e, "SluaModulo", "Luau's % takes the divisor's sign, and LSL's took the dividend's: they differ where one is negative.");
                return infix("%", MUL);
            case OP_EQ:
            case OP_NEQ:
            {
                if (std::optional<Expr> asked = truthAsked(e))
                {
                    return *asked;
                }
                const bool eq = op == OP_EQ;
                if (lt == LST_LIST && rt == LST_LIST)
                {
                    // LSL compares two lists by their lengths alone; != says
                    // how much longer the left one is.
                    note(e, "SluaListCompare", "LSL compares lists by their lengths alone, and != answers the difference of them.");
                    const Expr a = value(lhs);
                    const Expr b = value(rhs);
                    return eq ? Expr{ "#" + bracketed(a, UNARY) + " == #" + bracketed(b, UNARY), COMPARE, true }
                              : Expr{ "#" + bracketed(a, UNARY) + " - #" + bracketed(b, UNARY), ADD };
                }
                if ((lt == LST_KEY && rt == LST_STRING) || (lt == LST_STRING && rt == LST_KEY))
                {
                    // A key against a string, as LSL compares them: as text.
                    const Expr a = coerced(lhs, LST_STRING);
                    const Expr b = coerced(rhs, LST_STRING);
                    return { bracketed(a, COMPARE) + (eq ? " == " : " ~= ") + bracketed(b, COMPARE + 1), COMPARE, true };
                }
                return infix(eq ? "==" : "~=", COMPARE, true);
            }
            case OP_LESS: return infix("<", COMPARE, true);
            case OP_GREATER: return infix(">", COMPARE, true);
            case OP_LEQ: return infix("<=", COMPARE, true);
            case OP_GEQ: return infix(">=", COMPARE, true);
            case OP_BOOLEAN_AND:
            case OP_BOOLEAN_OR:
            {
                const bool and_ = op == OP_BOOLEAN_AND;
                if (!ALLSLTraits::sideEffectFree(rhs))
                {
                    note(e, "SluaShortCircuit", std::string(and_ ? "and" : "or") + " leaves its right side unrun once the left decides it; "
                                                "LSL ran both sides.");
                }
                const int  prec = and_ ? AND : OR;
                const Expr a    = condition(lhs);
                const Expr b    = condition(rhs);
                return { bracketed(a, prec) + (and_ ? " and " : " or ") + bracketed(b, prec + 1), prec, true };
            }
            case OP_BIT_AND: return bit("band");
            case OP_BIT_OR: return bit("bor");
            case OP_BIT_XOR: return bit("bxor");
            case OP_SHIFT_LEFT: return bit("lshift");
            case OP_SHIFT_RIGHT: return bit("arshift");
            default:
                return infix(operation_repr_str(op), COMPARE);
        }
    }

    Expr Writer::unary(LSLUnaryExpression* e)
    {
        LSLExpression* child = e->getChildExpr();
        switch (e->getOperation())
        {
            case OP_MINUS: return { "-" + bracketed(value(child), UNARY), UNARY };
            case OP_BOOLEAN_NOT: return { "not " + bracketed(condition(child), UNARY), UNARY, true };
            case OP_BIT_NOT:
                noteOnce(e, "SluaBit32", "bit32 answers 0 to 4294967295; LSL's integers were signed, from -2147483648.");
                return { "bit32.bnot(" + value(child).text + ")" };
            case OP_PRE_INCR:
            case OP_PRE_DECR:
            case OP_POST_INCR:
            case OP_POST_DECR:
                return sideEffect(e);
            default:
                return value(child);
        }
    }

    Expr Writer::typecast(LSLTypecastExpression* e)
    {
        LSLExpression* child = e->getChildExpr();
        const LSLIType to    = e->getIType();
        const LSLIType from  = child->getIType();
        const Expr     v     = value(child);
        if (to == from)
        {
            return v;
        }
        switch (to)
        {
            case LST_INTEGER:
                if (from == LST_FLOATINGPOINT)
                {
                    // Toward zero, as LSL cuts a float.
                    return { "(math.modf(" + v.text + "))" };
                }
                if (from == LST_STRING)
                {
                    mLslInteger = true;
                    return { "lslInteger(" + v.text + ")" };
                }
                return v;
            case LST_FLOATINGPOINT:
                if (from == LST_STRING)
                {
                    mLslFloat = true;
                    return { "lslFloat(" + v.text + ")" };
                }
                return v;
            case LST_STRING:
                switch (from)
                {
                    case LST_FLOATINGPOINT: return { "string.format(\"%.6f\", " + v.text + ")" };
                    case LST_LIST: return { "ll.DumpList2String(" + v.text + ", \"\")" };
                    case LST_VECTOR:
                    case LST_QUATERNION:
                        // LSL writes each part with five places.
                        return { "ll.DumpList2String({" + v.text + "}, \"\")" };
                    default: return { "tostring(" + v.text + ")" };
                }
            case LST_KEY:
                note(e, "SluaUuid", "SLua's uuid holds a key; LSL's key could hold any text.");
                return { "uuid(" + v.text + ")" };
            case LST_VECTOR: return { "(tovector(" + v.text + ") or ZERO_VECTOR)" };
            case LST_QUATERNION: return { "(toquaternion(" + v.text + ") or ZERO_ROTATION)" };
            case LST_LIST: return { "{" + v.text + "}" };
            default: return v;
        }
    }

    Expr Writer::sideEffect(LSLExpression* e)
    {
        note(e, "SluaAssignInExpression", "SLua's assignments are statements: this one is made by a function called where it stood.");
        // The assignment, then what it gives: the variable after it, or
        // before it for a step after.
        const std::string saved  = mText;
        const int         depth  = mDepth;
        const bool        inline_ = mInline;
        mText.clear();
        mDepth  = 0;
        mInline = true;
        LSLLValueExpression* target;
        const LSLOperator    op   = e->getOperation();
        const bool           post = op == OP_POST_INCR || op == OP_POST_DECR;
        if (e->getNodeSubType() == NODE_UNARY_EXPRESSION)
        {
            target = static_cast<LSLLValueExpression*>(static_cast<LSLUnaryExpression*>(e)->getChildExpr());
        }
        else
        {
            target = static_cast<LSLLValueExpression*>(static_cast<LSLBinaryExpression*>(e)->getLHS());
        }
        const std::string name = lvalue(target).text;
        if (post)
        {
            mText += "local was = " + name + "; ";
        }
        effect(e);
        std::string made = mText;
        while (!made.empty() && made.back() == '\n')
        {
            made.pop_back();
        }
        std::replace(made.begin(), made.end(), '\n', ' ');
        mText   = saved;
        mDepth  = depth;
        mInline = inline_;
        return { "(function() " + made + " return " + (post ? "was" : name) + " end)()" };
    }

    Expr Writer::expr(LSLExpression* e)
    {
        if (isNull(e))
        {
            return { "nil" };
        }
        switch (e->getNodeSubType())
        {
            case NODE_CONSTANT_EXPRESSION: return constant(static_cast<LSLConstant*>(e->getChild(0)));
            case NODE_PARENTHESIS_EXPRESSION: return expr(static_cast<LSLParenthesisExpression*>(e)->getChildExpr());
            case NODE_LVALUE_EXPRESSION: return lvalue(static_cast<LSLLValueExpression*>(e));
            case NODE_FUNCTION_EXPRESSION: return call(static_cast<LSLFunctionExpression*>(e));
            case NODE_BINARY_EXPRESSION: return binary(static_cast<LSLBinaryExpression*>(e));
            case NODE_UNARY_EXPRESSION: return unary(static_cast<LSLUnaryExpression*>(e));
            case NODE_TYPECAST_EXPRESSION: return typecast(static_cast<LSLTypecastExpression*>(e));
            case NODE_BOOL_CONVERSION_EXPRESSION: return condition(static_cast<LSLBoolConversionExpression*>(e)->getChildExpr());
            case NODE_PRINT_EXPRESSION: return { "print(" + value(static_cast<LSLPrintExpression*>(e)->getChildExpr()).text + ")" };
            case NODE_VECTOR_EXPRESSION:
            {
                auto* v = static_cast<LSLVectorExpression*>(e);
                return { "vector(" + value(v->getX()).text + ", " + value(v->getY()).text + ", " + value(v->getZ()).text + ")" };
            }
            case NODE_QUATERNION_EXPRESSION:
            {
                auto* q = static_cast<LSLQuaternionExpression*>(e);
                return { "quaternion(" + value(q->getX()).text + ", " + value(q->getY()).text + ", " + value(q->getZ()).text + ", " +
                         value(q->getS()).text + ")" };
            }
            case NODE_LIST_EXPRESSION:
            {
                std::string out;
                for (LSLASTNode* item = e->getChild(0); item; item = item->getNext())
                {
                    out += (out.empty() ? "" : ", ") + value(static_cast<LSLExpression*>(item)).text;
                }
                return { "{" + out + "}" };
            }
            default:
                return { "nil" };
        }
    }

    // --- statements -------------------------------------------------------------------

    void Writer::assign(LSLLValueExpression* target, LSLOperator op, LSLExpression* rhs)
    {
        LSLIdentifier* id      = target->getIdentifier();
        const LSLIType type    = id->getSymbol() ? id->getSymbol()->getIType() : target->getIType();
        const std::string name = nameOf(id);
        LSLASTNode*    member  = target->getMember();
        // What the new value is, from the old one where the operator makes
        // it so.
        const auto made = [&](const std::string& old, LSLIType t) -> std::string {
            const Expr v = rhs ? value(rhs) : Expr{ "1" };
            switch (op)
            {
                case OP_ASSIGN: return rhs ? coerced(rhs, t).text : v.text;
                case OP_ADD_ASSIGN:
                case OP_PRE_INCR:
                case OP_POST_INCR:
                    if (t == LST_STRING)
                    {
                        return old + " .. " + bracketed(coerced(rhs, LST_STRING), CONCAT);
                    }
                    if (t == LST_LIST)
                    {
                        mJoinLists = true;
                        return "joinLists(" + old + ", " + (rhs && rhs->getIType() == LST_LIST ? v.text : "{" + v.text + "}") + ")";
                    }
                    return old + " + " + bracketed(v, ADD + 1);
                case OP_SUB_ASSIGN:
                case OP_PRE_DECR:
                case OP_POST_DECR: return old + " - " + bracketed(v, ADD + 1);
                case OP_MUL_ASSIGN: return old + " * " + bracketed(v, MUL + 1);
                case OP_DIV_ASSIGN:
                    if (t == LST_INTEGER && rhs && rhs->getIType() == LST_INTEGER)
                    {
                        note(rhs, "SluaIntegerDivision",
                             "// rounds down, and LSL's integer / rounded toward zero: they differ where the answer is negative.");
                        return old + " // " + bracketed(v, MUL + 1);
                    }
                    return old + " / " + bracketed(v, MUL + 1);
                case OP_MOD_ASSIGN:
                    note(rhs, "SluaModulo", "Luau's % takes the divisor's sign, and LSL's took the dividend's: they differ where one is negative.");
                    return old + " % " + bracketed(v, MUL + 1);
                default: return v.text;
            }
        };
        if (!isNull(member))
        {
            // A part of a vector or a rotation, which SLua's are made again
            // for: each part as it was, the one assigned as it becomes.
            const std::string part = static_cast<LSLIdentifier*>(member)->getName();
            const bool        rot  = type == LST_QUATERNION;
            std::string       out  = rot ? "quaternion(" : "vector(";
            const char*       parts[] = { "x", "y", "z", "s" };
            for (int i = 0; i < (rot ? 4 : 3); ++i)
            {
                const std::string each = name + "." + parts[i];
                out += (i ? ", " : "") + (part == parts[i] ? made(each, LST_FLOATINGPOINT) : each);
            }
            line(name + " = " + out + ")");
            return;
        }
        // Luau's compound assignments where they mean LSL's.
        if (type != LST_LIST && op != OP_ASSIGN)
        {
            const Expr v = rhs ? value(rhs) : Expr{ "1" };
            switch (op)
            {
                case OP_ADD_ASSIGN:
                    line(name + (type == LST_STRING ? " ..= " : " += ") + (type == LST_STRING ? coerced(rhs, LST_STRING).text : v.text));
                    return;
                case OP_PRE_INCR:
                case OP_POST_INCR: line(name + " += 1"); return;
                case OP_PRE_DECR:
                case OP_POST_DECR: line(name + " -= 1"); return;
                case OP_SUB_ASSIGN: line(name + " -= " + v.text); return;
                case OP_MUL_ASSIGN: line(name + " *= " + v.text); return;
                default: break;
            }
        }
        if (op == OP_ASSIGN && rhs && boolean(id->getSymbol()))
        {
            line(name + " = " + truthOf(rhs));
            return;
        }
        line(name + " = " + made(name, type));
    }

    void Writer::effect(LSLExpression* e)
    {
        switch (e->getNodeSubType())
        {
            case NODE_PARENTHESIS_EXPRESSION:
                effect(static_cast<LSLParenthesisExpression*>(e)->getChildExpr());
                return;
            case NODE_BINARY_EXPRESSION:
            {
                auto*             b  = static_cast<LSLBinaryExpression*>(e);
                const LSLOperator op = b->getOperation();
                if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN ||
                    op == OP_MOD_ASSIGN)
                {
                    assign(static_cast<LSLLValueExpression*>(b->getLHS()), op, b->getRHS());
                    return;
                }
                break;
            }
            case NODE_UNARY_EXPRESSION:
            {
                auto*             u  = static_cast<LSLUnaryExpression*>(e);
                const LSLOperator op = u->getOperation();
                if (op == OP_PRE_INCR || op == OP_PRE_DECR || op == OP_POST_INCR || op == OP_POST_DECR)
                {
                    assign(static_cast<LSLLValueExpression*>(u->getChildExpr()), op, nullptr);
                    return;
                }
                break;
            }
            case NODE_FUNCTION_EXPRESSION:
            case NODE_PRINT_EXPRESSION:
                line(expr(e).text);
                return;
            default:
                break;
        }
        // An expression for nothing but what it does: kept where it does
        // anything, in what Luau takes as a statement.
        if (!ALLSLTraits::sideEffectFree(e))
        {
            line("local _ = " + value(e).text);
        }
    }

    void Writer::stateChange(LSLStateStatement* s, bool last)
    {
        const std::string name = s->getIdentifier()->getName();
        if (mManyStates)
        {
            line("setState(" + luaString(name) + ")");
        }
        else
        {
            note(s, "SluaStateSame", "a change to the state the script is in; a script of one state has no other to go to.");
        }
        // LSL's state change ends the event it is made in.
        if (!last)
        {
            line("do return end");
        }
        else if (!mTopLevel)
        {
            line("return");
        }
    }

    void Writer::block(LSLASTNode* s)
    {
        if (isNull(s))
        {
            return;
        }
        if (s->getNodeSubType() == NODE_COMPOUND_STATEMENT)
        {
            for (LSLASTNode* child = s->getChild(0); child; child = child->getNext())
            {
                statement(child, !child->getNext());
            }
            return;
        }
        statement(s, true);
    }

    void Writer::statement(LSLASTNode* s, bool last)
    {
        if (isNull(s))
        {
            return;
        }
        switch (s->getNodeSubType())
        {
            case NODE_NOP_STATEMENT: return;
            case NODE_COMPOUND_STATEMENT:
                line("do");
                ++mDepth;
                block(s);
                --mDepth;
                line("end");
                return;
            case NODE_EXPRESSION_STATEMENT: effect(static_cast<LSLExpressionStatement*>(s)->getExpr()); return;
            case NODE_DECLARATION:
            {
                auto*             d    = static_cast<LSLDeclaration*>(s);
                LSLIdentifier*    id   = d->getIdentifier();
                const LSLIType    type = id->getIType();
                LSLExpression*    init = d->getInitializer();
                const std::string name = nameOf(id);
                // A counter that only numeric fors use, each with its own.
                if (mLoopOnly.contains(id->getSymbol()) && (isNull(init) || init->getNodeSubType() == NODE_CONSTANT_EXPRESSION))
                {
                    return;
                }
                if (boolean(id->getSymbol()))
                {
                    line("local " + name + (mOptions.types ? ": boolean" : "") + " = " + (isNull(init) ? std::string("false") : truthOf(init)));
                    return;
                }
                line("local " + name + typed(type) + " = " + (isNull(init) ? defaultOf(type) : coerced(init, type).text));
                return;
            }
            case NODE_RETURN_STATEMENT:
            {
                LSLExpression*    e    = static_cast<LSLReturnStatement*>(s)->getExpr();
                const std::string said = isNull(e) ? "return" : "return " + (boolean(mFunction) ? truthOf(e) : value(e).text);
                line(last ? said : "do " + said + " end");
                return;
            }
            case NODE_IF_STATEMENT:
            {
                auto* i = static_cast<LSLIfStatement*>(s);
                line("if " + condition(i->getCheckExpr()).text + " then");
                for (;;)
                {
                    ++mDepth;
                    block(i->getTrueBranch());
                    --mDepth;
                    LSLASTNode* otherwise = i->getFalseBranch();
                    if (isNull(otherwise))
                    {
                        break;
                    }
                    if (otherwise->getNodeSubType() == NODE_IF_STATEMENT)
                    {
                        i = static_cast<LSLIfStatement*>(otherwise);
                        line("elseif " + condition(i->getCheckExpr()).text + " then");
                        continue;
                    }
                    line("else");
                    ++mDepth;
                    block(otherwise);
                    --mDepth;
                    break;
                }
                line("end");
                return;
            }
            case NODE_WHILE_STATEMENT:
            {
                auto* w = static_cast<LSLWhileStatement*>(s);
                line("while " + condition(w->getCheckExpr()).text + " do");
                mLoops.push_back(nullptr);
                ++mDepth;
                block(w->getBody());
                --mDepth;
                mLoops.pop_back();
                line("end");
                return;
            }
            case NODE_DO_STATEMENT:
            {
                auto* d = static_cast<LSLDoStatement*>(s);
                line("repeat");
                mLoops.push_back(nullptr);
                ++mDepth;
                block(d->getBody());
                --mDepth;
                mLoops.pop_back();
                line("until " + bracketed(condition(d->getCheckExpr()), UNARY).insert(0, "not "));
                return;
            }
            case NODE_FOR_STATEMENT:
            {
                // Luau's numeric for where it counts as LSL's did.
                auto* f = static_cast<LSLForStatement*>(s);
                if (const auto numeric = mNumeric.find(f); numeric != mNumeric.end())
                {
                    numericFor(f, numeric->second);
                    return;
                }
                // Else as LSL runs one: what starts it, then while the check
                // holds, the body and then the steps.
                for (LSLASTNode* init = f->getInitExprs() ? f->getInitExprs()->getChild(0) : nullptr; init; init = init->getNext())
                {
                    effect(static_cast<LSLExpression*>(init));
                }
                LSLExpression* check = f->getCheckExpr();
                line("while " + (isNull(check) ? std::string("true") : condition(check).text) + " do");
                mLoops.push_back(f);
                ++mDepth;
                block(f->getBody());
                for (LSLASTNode* step = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr; step; step = step->getNext())
                {
                    effect(static_cast<LSLExpression*>(step));
                }
                --mDepth;
                mLoops.pop_back();
                line("end");
                return;
            }
            case NODE_JUMP_STATEMENT:
            {
                auto* j = static_cast<LSLJumpStatement*>(s);
                if (j->getIsBreakLike() && !mLoops.empty())
                {
                    line("break");
                    return;
                }
                if (j->getIsContinueLike() && !mLoops.empty())
                {
                    // A for's steps run before its next turn, as they would
                    // at the end of its body.
                    if (LSLForStatement* f = mLoops.back() && mLoops.back()->getNodeSubType() == NODE_FOR_STATEMENT
                                                 ? static_cast<LSLForStatement*>(mLoops.back())
                                                 : nullptr)
                    {
                        for (LSLASTNode* step = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr; step; step = step->getNext())
                        {
                            effect(static_cast<LSLExpression*>(step));
                        }
                    }
                    line("continue");
                    return;
                }
                note(s, "SluaJump", std::string("jump ") + j->getIdentifier()->getName() +
                                        ": SLua has no goto, and this jump is neither out of a loop nor to its next turn. Rewrite it.");
                line("-- jump " + std::string(j->getIdentifier()->getName()));
                return;
            }
            case NODE_LABEL:
            {
                auto* l = static_cast<LSLLabel*>(s);
                if (mUnstructured.contains(l->getIdentifier()->getSymbol()))
                {
                    line("-- @" + std::string(l->getIdentifier()->getName()));
                }
                return;
            }
            case NODE_STATE_STATEMENT: stateChange(static_cast<LSLStateStatement*>(s), last); return;
            default:
                return;
        }
    }

    // --- loops as Luau counts ---------------------------------------------------------

    namespace
    {
        LSLExpression* unwrapped(LSLExpression* e)
        {
            while (e && e->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
            {
                e = static_cast<LSLParenthesisExpression*>(e)->getChildExpr();
            }
            return e;
        }

        // The variable an lvalue names, whole: none for a part of one.
        LSLSymbol* wholeVariable(LSLExpression* e)
        {
            e = unwrapped(e);
            if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION || !isNull(static_cast<LSLLValueExpression*>(e)->getMember()))
            {
                return nullptr;
            }
            return static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol();
        }

        // The variable an expression sets, where it sets one: an
        // assignment of any kind, or a step.
        LSLSymbol* setBy(LSLASTNode* node)
        {
            if (node->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN || op == OP_MOD_ASSIGN)
                {
                    LSLExpression* lhs = static_cast<LSLBinaryExpression*>(node)->getLHS();
                    return lhs && lhs->getNodeSubType() == NODE_LVALUE_EXPRESSION ? static_cast<LSLLValueExpression*>(lhs)->getIdentifier()->getSymbol()
                                                                                   : nullptr;
                }
            }
            if (node->getNodeSubType() == NODE_UNARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op == OP_PRE_INCR || op == OP_PRE_DECR || op == OP_POST_INCR || op == OP_POST_DECR)
                {
                    LSLExpression* child = static_cast<LSLUnaryExpression*>(node)->getChildExpr();
                    return child && child->getNodeSubType() == NODE_LVALUE_EXPRESSION
                               ? static_cast<LSLLValueExpression*>(child)->getIdentifier()->getSymbol()
                               : nullptr;
                }
            }
            return nullptr;
        }
    }

    std::optional<Writer::Counting> Writer::counting(LSLForStatement* f) const
    {
        // One start, i = from; one check, i against a limit; one step, by
        // a constant, the way the check is going.
        LSLASTNode* init = f->getInitExprs() ? f->getInitExprs()->getChild(0) : nullptr;
        LSLASTNode* incr = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr;
        auto*       check = static_cast<LSLExpression*>(f->getCheckExpr());
        if (!init || init->getNext() || !incr || incr->getNext() || isNull(check) || init->getNodeSubType() != NODE_BINARY_EXPRESSION ||
            static_cast<LSLExpression*>(init)->getOperation() != OP_ASSIGN)
        {
            return std::nullopt;
        }
        Counting c;
        auto*    start = static_cast<LSLBinaryExpression*>(init);
        c.var          = wholeVariable(start->getLHS());
        if (!c.var || c.var->getIType() != LST_INTEGER || c.var->getSubType() != SYM_LOCAL)
        {
            return std::nullopt;
        }
        c.id   = static_cast<LSLLValueExpression*>(unwrapped(start->getLHS()))->getIdentifier();
        c.from = start->getRHS();
        check  = unwrapped(check);
        if (check->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
        {
            check = unwrapped(static_cast<LSLBoolConversionExpression*>(check)->getChildExpr());
        }
        if (!check || check->getNodeSubType() != NODE_BINARY_EXPRESSION)
        {
            return std::nullopt;
        }
        auto* test = static_cast<LSLBinaryExpression*>(check);
        c.check    = test->getOperation();
        if ((c.check != OP_LESS && c.check != OP_LEQ && c.check != OP_GREATER && c.check != OP_GEQ) || wholeVariable(test->getLHS()) != c.var)
        {
            return std::nullopt;
        }
        c.limit = test->getRHS();
        // The step: ++ and -- either side, or += and -= a whole number.
        const LSLOperator op = static_cast<LSLExpression*>(incr)->getOperation();
        if (setBy(incr) != c.var)
        {
            return std::nullopt;
        }
        int by = 0;
        if (op == OP_PRE_INCR || op == OP_POST_INCR)
        {
            c.step = 1;
        }
        else if (op == OP_PRE_DECR || op == OP_POST_DECR)
        {
            c.step = -1;
        }
        else if ((op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN) && wholeNumber(static_cast<LSLBinaryExpression*>(incr)->getRHS(), by) && by > 0)
        {
            c.step = op == OP_ADD_ASSIGN ? by : -by;
        }
        const bool up = c.check == OP_LESS || c.check == OP_LEQ;
        if (c.step == 0 || (c.step > 0) != up)
        {
            return std::nullopt;
        }
        // Nothing in the loop sets the counter but its step, and the limit
        // and where it starts are read once, as Luau reads them.
        bool setInBody = false;
        walk(f->getBody(), [&](LSLASTNode* node) { setInBody = setInBody || setBy(node) == c.var; });
        if (setInBody || !steadyIn(c.limit, f))
        {
            return std::nullopt;
        }
        return c;
    }

    bool Writer::steadyIn(LSLExpression* e, LSLASTNode* loop) const
    {
        // What the loop sets, and whether it calls anything of the script's
        // own, which could set a global.
        boost::unordered_flat_set<LSLSymbol*> set;
        bool                                  calls = false;
        walk(loop, [&](LSLASTNode* node) {
            if (LSLSymbol* var = setBy(node))
            {
                set.insert(var);
            }
            if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                LSLSymbol* fn = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                calls         = calls || !fn || fn->getSubType() != SYM_BUILTIN;
            }
        });
        bool steady = true;
        walk(e, [&](LSLASTNode* node) {
            switch (node->getNodeSubType())
            {
                case NODE_LVALUE_EXPRESSION:
                {
                    LSLSymbol* var = static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol();
                    steady = steady && var && (var->getSubType() == SYM_BUILTIN ||
                                               (!set.contains(var) && (var->getSubType() != SYM_GLOBAL || !calls)));
                    break;
                }
                case NODE_FUNCTION_EXPRESSION:
                    steady = steady && ALLSLTraits::pure(static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getName());
                    break;
                case NODE_BINARY_EXPRESSION:
                case NODE_UNARY_EXPRESSION:
                    steady = steady && !setBy(node);
                    break;
                default:
                    break;
            }
        });
        return steady;
    }

    void Writer::prepareBody(LSLASTNode* body)
    {
        mNumeric.clear();
        mLoopOnly.clear();
        // The fors of the shape, by their counters.
        boost::unordered_flat_map<LSLSymbol*, std::vector<LSLForStatement*>> byCounter;
        walk(body, [&](LSLASTNode* node) {
            if (node->getNodeSubType() == NODE_FOR_STATEMENT)
            {
                if (std::optional<Counting> c = counting(static_cast<LSLForStatement*>(node)))
                {
                    mNumeric.emplace(static_cast<LSLForStatement*>(node), *c);
                    byCounter[c->var].push_back(static_cast<LSLForStatement*>(node));
                }
            }
        });
        // A counter read or set anywhere but in its own numeric fors keeps
        // them LSL's: Luau's is a new local, gone after the loop.
        for (auto& [var, loops] : byCounter)
        {
            bool elsewhere = false;
            const auto within = [&loops](LSLASTNode* node) {
                for (LSLASTNode* up = node; up; up = up->getParent())
                {
                    if (std::find(loops.begin(), loops.end(), up) != loops.end())
                    {
                        return true;
                    }
                }
                return false;
            };
            walk(body, [&](LSLASTNode* node) {
                if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION && static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol() == var &&
                    !within(node))
                {
                    elsewhere = true;
                }
            });
            if (elsewhere)
            {
                for (LSLForStatement* f : loops)
                {
                    mNumeric.erase(f);
                }
                continue;
            }
            mLoopOnly.insert(var);
        }
    }

    void Writer::numericFor(LSLForStatement* f, const Counting& c)
    {
        // i < b counts to b - 1 in whole numbers, i > b to b + 1.
        int               limit = 0;
        std::string       to;
        const bool        strict = c.check == OP_LESS || c.check == OP_GREATER;
        const int         shift  = c.check == OP_LESS ? -1 : c.check == OP_GREATER ? 1 : 0;
        if (wholeNumber(c.limit, limit))
        {
            to = std::to_string(limit + shift);
        }
        else
        {
            const Expr bound = value(c.limit);
            to = strict ? bracketed(bound, ADD) + (shift < 0 ? " - 1" : " + 1") : bound.text;
        }
        const std::string step = c.step == 1 ? std::string() : ", " + std::to_string(c.step);
        line("for " + nameOf(c.id) + " = " + value(c.from).text + ", " + to + step + " do");
        // A jump to its next turn is continue, its step Luau's own.
        mLoops.push_back(nullptr);
        ++mDepth;
        block(f->getBody());
        --mDepth;
        mLoops.pop_back();
        line("end");
    }

    // --- truth values ----------------------------------------------------------------

    namespace
    {
        // TRUE, FALSE, or 1 or 0 written out, and which.
        bool truthConstant(LSLExpression* e, bool& truth)
        {
            int v = 0;
            if (wholeNumber(e, v))
            {
                truth = v != 0;
                return v == 0 || v == 1;
            }
            e = unwrapped(e);
            if (e && e->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                LSLIdentifier* id = static_cast<LSLLValueExpression*>(e)->getIdentifier();
                if (id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN &&
                    (std::string_view(id->getName()) == "TRUE" || std::string_view(id->getName()) == "FALSE"))
                {
                    truth = std::string_view(id->getName()) == "TRUE";
                    return true;
                }
            }
            return false;
        }

        // What a read reads: a variable, whole, or a function of the
        // script's own that is called.
        LSLSymbol* readOf(LSLASTNode* node)
        {
            if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                LSLSymbol* symbol = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                return symbol && symbol->getSubType() != SYM_BUILTIN ? symbol : nullptr;
            }
            return wholeVariable(static_cast<LSLExpression*>(node));
        }

        // Whether what an expression gives is used: not where it stands as
        // a statement, or as a for's start or step.
        bool valueUsed(LSLASTNode* node)
        {
            LSLASTNode* parent = node->getParent();
            while (parent && parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
            {
                parent = parent->getParent();
            }
            if (!parent || parent->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
            {
                return false;
            }
            return !(parent->getNodeType() == NODE_AST_NODE_LIST && parent->getParent() &&
                     parent->getParent()->getNodeSubType() == NODE_FOR_STATEMENT);
        }

        // Whether an assignment is what sets its variable, rather than a
        // read of it.
        bool assignedBy(LSLASTNode* read)
        {
            LSLASTNode* parent = read->getParent();
            return parent && parent->getNodeSubType() == NODE_BINARY_EXPRESSION && read->getParentSlot() == 0 &&
                   static_cast<LSLExpression*>(parent)->getOperation() == OP_ASSIGN;
        }

        // The function of the script's own a node stands in, if any.
        LSLSymbol* enclosingFunction(LSLASTNode* node)
        {
            for (; node; node = node->getParent())
            {
                if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    return static_cast<LSLGlobalFunction*>(node)->getSymbol();
                }
            }
            return nullptr;
        }
    }

    bool Writer::zeroOrOne(LSLExpression* e) const
    {
        bool truth = false;
        if (truthConstant(e, truth))
        {
            return true;
        }
        e = unwrapped(e);
        if (!e)
        {
            return false;
        }
        switch (e->getNodeSubType())
        {
            case NODE_LVALUE_EXPRESSION:
            case NODE_FUNCTION_EXPRESSION:
                if (LSLSymbol* read = readOf(e))
                {
                    return mZeroOne.contains(read);
                }
                if (e->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
                {
                    const ALLSLTraits::Trait* trait = ALLSLTraits::of(static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getName());
                    return trait && (trait->slua & ALLSLTraits::SluaBool);
                }
                return false;
            case NODE_UNARY_EXPRESSION: return e->getOperation() == OP_BOOLEAN_NOT;
            case NODE_BINARY_EXPRESSION:
            {
                const LSLOperator op = e->getOperation();
                if (op == OP_NEQ)
                {
                    // != of two lists says how much longer the left one is.
                    auto* b = static_cast<LSLBinaryExpression*>(e);
                    return b->getLHS()->getIType() != LST_LIST || b->getRHS()->getIType() != LST_LIST;
                }
                return op == OP_EQ || op == OP_LESS || op == OP_LEQ || op == OP_GREATER || op == OP_GEQ || op == OP_BOOLEAN_AND ||
                       op == OP_BOOLEAN_OR;
            }
            default: return false;
        }
    }

    bool Writer::readAsTruth(LSLASTNode* read) const
    {
        // Up through brackets and the tree's own conversions to a truth.
        LSLASTNode* node   = read;
        LSLASTNode* parent = node->getParent();
        while (parent && (parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION || parent->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION))
        {
            if (parent->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
            {
                return true;
            }
            node   = parent;
            parent = parent->getParent();
        }
        if (!parent)
        {
            return false;
        }
        const int slot = node->getParentSlot();
        switch (parent->getNodeSubType())
        {
            case NODE_IF_STATEMENT:
            case NODE_WHILE_STATEMENT: return slot == 0;
            case NODE_DO_STATEMENT: return slot == 1;
            case NODE_FOR_STATEMENT: return slot == 1;
            case NODE_RETURN_STATEMENT: return boolean(enclosingFunction(parent));
            case NODE_UNARY_EXPRESSION: return static_cast<LSLExpression*>(parent)->getOperation() == OP_BOOLEAN_NOT;
            case NODE_BINARY_EXPRESSION:
            {
                const LSLOperator op = static_cast<LSLExpression*>(parent)->getOperation();
                auto*             b  = static_cast<LSLBinaryExpression*>(parent);
                if (op == OP_BOOLEAN_AND || op == OP_BOOLEAN_OR)
                {
                    return true;
                }
                // Asked whether it is TRUE or FALSE, where it only ever is
                // one or the other.
                bool truth = false;
                if ((op == OP_EQ || op == OP_NEQ) && truthConstant(slot == 0 ? b->getRHS() : b->getLHS(), truth))
                {
                    return mZeroOne.contains(readOf(read));
                }
                // Copied, whole, into another truth value.
                return op == OP_ASSIGN && slot == 1 && boolean(wholeVariable(b->getLHS()));
            }
            case NODE_DECLARATION: return slot == 1 && boolean(static_cast<LSLDeclaration*>(parent)->getIdentifier()->getSymbol());
            default:
                if (parent->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return slot == 1 && boolean(static_cast<LSLGlobalVariable*>(parent)->getIdentifier()->getSymbol());
                }
                return false;
        }
    }

    void Writer::findBooleans()
    {
        // Every integer the script declares and every function of its own
        // that answers one, then each let go of that is stepped, added to or
        // read as a number, until none is.
        std::vector<LSLASTNode*>                           reads;
        std::vector<std::pair<LSLSymbol*, LSLExpression*>> sets;
        const auto candidate = [&](LSLIdentifier* id, LSLExpression* init) {
            if (id->getIType() == LST_INTEGER && id->getSymbol())
            {
                mBooleans.insert(id->getSymbol());
                if (!isNull(init))
                {
                    sets.emplace_back(id->getSymbol(), init);
                }
            }
        };
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                auto* global = static_cast<LSLGlobalVariable*>(node);
                candidate(global->getIdentifier(), global->getInitializer());
            }
            else if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                auto* f = static_cast<LSLGlobalFunction*>(node);
                if (f->getIdentifier()->getIType() == LST_INTEGER && f->getSymbol())
                {
                    mBooleans.insert(f->getSymbol());
                }
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                auto* declaration = static_cast<LSLDeclaration*>(node);
                candidate(declaration->getIdentifier(), declaration->getInitializer());
            }
            else if (node->getNodeSubType() == NODE_RETURN_STATEMENT)
            {
                LSLExpression* e = static_cast<LSLReturnStatement*>(node)->getExpr();
                if (LSLSymbol* f = enclosingFunction(node); f && !isNull(e))
                {
                    sets.emplace_back(f, e);
                }
            }
            else if ((node->getNodeSubType() == NODE_LVALUE_EXPRESSION || node->getNodeSubType() == NODE_FUNCTION_EXPRESSION) && readOf(node))
            {
                reads.push_back(node);
            }
        });
        // Stepped, added to, or set where what the assignment gives is
        // used: a number. After every declaration, which a function may
        // come before.
        walk(mScript, [&](LSLASTNode* node) {
            if (LSLSymbol* var = setBy(node))
            {
                if (static_cast<LSLExpression*>(node)->getOperation() != OP_ASSIGN || valueUsed(node))
                {
                    mBooleans.erase(var);
                }
                else
                {
                    sets.emplace_back(var, static_cast<LSLBinaryExpression*>(node)->getRHS());
                }
            }
        });
        // Those only ever TRUE or FALSE, which may be asked which.
        mZeroOne = mBooleans;
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& [symbol, e] : sets)
            {
                if (mZeroOne.contains(symbol) && !zeroOrOne(e))
                {
                    mZeroOne.erase(symbol);
                    changed = true;
                }
            }
        }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (LSLASTNode* read : reads)
            {
                LSLSymbol* symbol = readOf(read);
                if (boolean(symbol) && !assignedBy(read) && valueUsed(read) && !readAsTruth(read))
                {
                    mBooleans.erase(symbol);
                    changed = true;
                }
            }
        }
        // Nothing to say of one never read: kept a number, as it was.
        boost::unordered_flat_set<LSLSymbol*> readSome;
        for (LSLASTNode* read : reads)
        {
            if (!assignedBy(read) && valueUsed(read))
            {
                readSome.insert(readOf(read));
            }
        }
        boost::unordered::erase_if(mBooleans, [&readSome](LSLSymbol* symbol) { return !readSome.contains(symbol); });
    }

    std::optional<Expr> Writer::truthAsked(LSLBinaryExpression* e)
    {
        bool           truth = false;
        LSLExpression* side  = nullptr;
        if (boolean(readOf(unwrapped(e->getLHS()))) && truthConstant(e->getRHS(), truth))
        {
            side = e->getLHS();
        }
        else if (boolean(readOf(unwrapped(e->getRHS()))) && truthConstant(e->getLHS(), truth))
        {
            side = e->getRHS();
        }
        if (!side)
        {
            return std::nullopt;
        }
        const Expr is = expr(side);
        if ((e->getOperation() == OP_EQ) == truth)
        {
            return Expr{ is.text, is.prec, true };
        }
        return Expr{ "not " + bracketed(is, UNARY), UNARY, true };
    }

    std::string Writer::truthOf(LSLExpression* e)
    {
        int v = 0;
        if (wholeNumber(e, v))
        {
            return v != 0 ? "true" : "false";
        }
        return condition(e).text;
    }

    // --- the script -------------------------------------------------------------------

    void Writer::globals()
    {
        bool any = false;
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            if (g->getNodeType() != NODE_GLOBAL_VARIABLE)
            {
                continue;
            }
            auto*          global = static_cast<LSLGlobalVariable*>(g);
            LSLIdentifier* id     = global->getIdentifier();
            LSLExpression* init   = global->getInitializer();
            if (boolean(id->getSymbol()))
            {
                line("local " + nameOf(id) + (mOptions.types ? ": boolean" : "") + " = " + (isNull(init) ? std::string("false") : truthOf(init)));
            }
            else
            {
                line("local " + nameOf(id) + typed(id->getIType()) + " = " +
                     (isNull(init) ? defaultOf(id->getIType()) : coerced(init, id->getIType()).text));
            }
            any = true;
        }
        if (any)
        {
            line("");
        }
    }

    void Writer::functions()
    {
        // Declared together first where one calls another written after
        // it, which LSL allows and a Luau local does not.
        std::vector<LSLGlobalFunction*> all;
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            if (g->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                all.push_back(static_cast<LSLGlobalFunction*>(g));
            }
        }
        bool forward = false;
        for (size_t i = 0; i < all.size() && !forward; ++i)
        {
            walk(all[i]->getStatements(), [&](LSLASTNode* node) {
                if (node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    return;
                }
                // A call to one written after it; one to itself a Luau
                // local function reaches.
                LSLSymbol* callee = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                for (size_t j = i + 1; j < all.size(); ++j)
                {
                    forward = forward || all[j]->getSymbol() == callee;
                }
            });
        }
        if (forward)
        {
            std::string names;
            for (LSLGlobalFunction* f : all)
            {
                names += (names.empty() ? "" : ", ") + nameOf(f->getIdentifier());
            }
            line("local " + names);
            line("");
        }
        for (LSLGlobalFunction* f : all)
        {
            std::string params;
            for (LSLASTNode* p = f->getArguments() ? f->getArguments()->getChild(0) : nullptr; p; p = p->getNext())
            {
                params += (params.empty() ? "" : ", ") + nameOf(static_cast<LSLIdentifier*>(p)) + typed(p->getIType());
            }
            mFunction = f->getSymbol();
            line(std::string(forward ? "function " : "local function ") + nameOf(f->getIdentifier()) + "(" + params + ")" +
                 (boolean(mFunction) ? std::string(mOptions.types ? ": boolean" : "") : typed(f->getIdentifier()->getIType())));
            ++mDepth;
            prepareBody(f->getStatements());
            block(f->getStatements());
            --mDepth;
            mFunction = nullptr;
            line("end");
            line("");
        }
    }

    std::string Writer::handlerParams(LSLEventHandler* handler, std::string& lead)
    {
        // What LSL's handler took: the count of what was detected, made from
        // SLua's detected table.
        const std::string event = handler->getIdentifier()->getName();
        std::string       params;
        LSLASTNode*       first = handler->getArguments() ? handler->getArguments()->getChild(0) : nullptr;
        if (detectedEvent(event))
        {
            if (first)
            {
                lead = "local " + nameOf(static_cast<LSLIdentifier*>(first)) + " = #detected";
            }
            return "detected";
        }
        for (LSLASTNode* p = first; p; p = p->getNext())
        {
            params += (params.empty() ? "" : ", ") + nameOf(static_cast<LSLIdentifier*>(p));
        }
        return params;
    }

    void Writer::handlerBody(LSLEventHandler* handler)
    {
        prepareBody(handler->getStatements());
        mInDetected = detectedEvent(handler->getIdentifier()->getName());
        block(handler->getStatements());
        mInDetected = false;
    }

    void Writer::singleState(LSLState* state)
    {
        LSLEventHandler* entry = nullptr;
        for (LSLASTNode* h = state->getEventHandlers()->getChild(0); h; h = h->getNext())
        {
            auto*             handler = static_cast<LSLEventHandler*>(h);
            const std::string event   = handler->getIdentifier()->getName();
            if (event == "state_entry")
            {
                entry = handler;
                continue;
            }
            if (event == "state_exit")
            {
                note(handler, "SluaStateExit", "state_exit runs as a script leaves a state, and this one has no other: it never ran.");
            }
            std::string lead;
            const std::string params = handlerParams(handler, lead);
            // The timer's handler, which LLTimers calls, where the script's
            // timer is on LLTimers.
            const bool timer = event == "timer" && mTimers;
            if (event == "timer" && !mTimers)
            {
                noteOnce(handler, "SluaTimer", "the timer event, set going by llcompat.SetTimerEvent; LLTimers:every is SLua's own.");
            }
            const bool field = mOptions.handlers == ALLSLToSLua::Options::Handlers::Field;
            line(timer   ? "timerHandler = function()"
                 : field ? "LLEvents." + event + " = function(" + params + ")"
                         : "LLEvents:on(" + luaString(event) + ", function(" + params + ")");
            ++mDepth;
            if (!lead.empty())
            {
                line(lead);
            }
            handlerBody(handler);
            --mDepth;
            line(timer || field ? "end" : "end)");
            line("");
        }
        if (entry)
        {
            // What LSL ran as the script started, run as SLua runs a script:
            // once, from the top.
            line("-- state_entry");
            mTopLevel = true;
            handlerBody(entry);
            mTopLevel = false;
        }
    }

    void Writer::statesPreamble()
    {
        const bool field = mOptions.handlers == ALLSLToSLua::Options::Handlers::Field;
        line("-- LSL's states, which SLua has none of: each state's handlers in a table,");
        line(field ? "-- set on LLEvents as it is entered and taken off as it is left." : "-- put on LLEvents as it is entered and taken off as it is left.");
        note(nullptr, "SluaStates",
             "LSL let go of a state's listens, sensor repeats and targets as it left the state; setState does not. Remove them "
             "yourself where the script relied on it.");
        line("local states: { [string]: { [string]: (...any) -> () } } = {}");
        line("local currentState: string? = nil");
        line("");
        line("local function setState(name: string)");
        // Not an LLEvents event: state_entry and state_exit, which setState
        // runs, and the timer where LLTimers calls it.
        const std::string own = mTimers ? "event ~= \"state_entry\" and event ~= \"state_exit\" and event ~= \"timer\""
                                        : "event ~= \"state_entry\" and event ~= \"state_exit\"";
        const std::string off = field ? "(LLEvents :: any)[event] = nil" : "LLEvents:off(event :: any, handler)";
        const std::string on  = field ? "(LLEvents :: any)[event] = handler" : "LLEvents:on(event :: any, handler)";
        mText += "    if name == currentState then\n"
                 "        return\n"
                 "    end\n"
                 "    local leaving = currentState and states[currentState]\n"
                 "    if leaving then\n"
                 "        if leaving.state_exit then\n"
                 "            leaving.state_exit()\n"
                 "        end\n"
                 "        for event, handler in leaving do\n"
                 "            if " + own + " then\n"
                 "                " + off + "\n"
                 "            end\n"
                 "        end\n"
                 "    end\n"
                 "    currentState = name\n"
                 "    local entering = states[name]\n"
                 "    for event, handler in entering do\n"
                 "        if " + own + " then\n"
                 "            " + on + "\n"
                 "        end\n"
                 "    end\n"
                 "    if entering.state_entry then\n"
                 "        entering.state_entry()\n"
                 "    end\n"
                 "end\n\n";
    }

    void Writer::timersPreamble()
    {
        // llSetTimerEvent's one timer, on LLTimers: set going again, or
        // stopped, by each setTimer, calling the timer handler of the state
        // the script is in.
        line("-- llSetTimerEvent's timer, on LLTimers: one at a time, as LSL had it.");
        noteOnce(nullptr, "SluaTimers", "SLua's LLTimers can run several timers at once: LLTimers:every(seconds, callback), "
                                        "LLTimers:once(seconds, callback) and LLTimers:off(timer).");
        if (!mManyStates)
        {
            line("local timerHandler: (() -> ())? = nil");
        }
        line("local timerHandle: any = nil");
        line("");
        line("local function setTimer(seconds: number)");
        mText += "    if timerHandle then\n"
                 "        LLTimers:off(timerHandle)\n"
                 "        timerHandle = nil\n"
                 "    end\n"
                 "    if seconds > 0 then\n"
                 "        timerHandle = LLTimers:every(seconds, function()\n";
        mText += mManyStates ? "            local handler = currentState and states[currentState].timer\n"
                               "            if handler then\n"
                               "                handler()\n"
                               "            end\n"
                             : "            if timerHandler then\n"
                               "                timerHandler()\n"
                               "            end\n";
        mText += "        end)\n"
                 "    end\n"
                 "end\n\n";
    }

    void Writer::multiState()
    {
        for (LSLASTNode* s = mScript->getStates()->getChild(0); s; s = s->getNext())
        {
            auto*             state = static_cast<LSLState*>(s);
            const std::string name  = state->getIdentifier()->getName();
            line("states" + stateKey(name) + " = {");
            ++mDepth;
            for (LSLASTNode* h = state->getEventHandlers()->getChild(0); h; h = h->getNext())
            {
                auto*             handler = static_cast<LSLEventHandler*>(h);
                const std::string event   = handler->getIdentifier()->getName();
                std::string       lead;
                const std::string params = handlerParams(handler, lead);
                if (event == "timer" && !mTimers)
                {
                    noteOnce(handler, "SluaTimer",
                             "the timer event, set going by llcompat.SetTimerEvent, keeps going from state to state, as LSL's "
                             "did; LLTimers:every is SLua's own.");
                }
                line(event + " = function(" + params + ")");
                ++mDepth;
                if (!lead.empty())
                {
                    line(lead);
                }
                handlerBody(handler);
                --mDepth;
                line("end,");
            }
            --mDepth;
            line("}");
            line("");
        }
        line("setState(\"default\")");
    }

    void Writer::states()
    {
        LSLASTNode* first = mScript->getStates()->getChild(0);
        if (mManyStates)
        {
            multiState();
        }
        else if (first)
        {
            singleState(static_cast<LSLState*>(first));
        }
    }

    void Writer::helpers(std::string& out)
    {
        if (mJoinLists)
        {
            out += R"LUA(-- LSL's + on lists: a new list of both, the left's then the right's.
local function joinLists(a: { any }, b: { any }): { any }
    local out = table.clone(a)
    table.move(b, 1, #b, #out + 1, out)
    return out
end

)LUA";
        }
        if (mLslInteger)
        {
            out += R"LUA(-- LSL's (integer) of a string: the whole number it starts with, in
-- decimal or 0x hexadecimal, after any spaces; 0 where it starts with none.
local function lslInteger(s: string): number
    local sign, hex = string.match(s, "^%s*([+-]?)0[xX](%x+)")
    if hex then
        local v = tonumber(hex, 16) or 0
        return if sign == "-" then -v else v
    end
    return tonumber(string.match(s, "^%s*([+-]?%d+)") or "0") or 0
end

)LUA";
        }
        if (mLslFloat)
        {
            out += R"LUA(-- LSL's (float) of a string: the number it starts with; 0 where it
-- starts with none.
local function lslFloat(s: string): number
    local found = string.match(s, "^%s*([+-]?%d*%.?%d+[eE][+-]?%d+)") or string.match(s, "^%s*([+-]?%d*%.?%d*)")
    return tonumber(found or "0") or 0
end

)LUA";
        }
    }

    std::string Writer::write()
    {
        // The jumps SLua has nothing for, whose labels are kept as a mark.
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_JUMP_STATEMENT)
            {
                return;
            }
            auto* j = static_cast<LSLJumpStatement*>(node);
            if (!j->getIsBreakLike() && !j->getIsContinueLike())
            {
                mUnstructured.insert(j->getIdentifier()->getSymbol());
            }
        });
        // What the script sets going: a timer on LLTimers where it sets one.
        LSLASTNode* first = mScript->getStates()->getChild(0);
        mManyStates       = first && first->getNext();
        walk(mScript, [&](LSLASTNode* node) {
            if (mOptions.llTimers && node->getNodeSubType() == NODE_FUNCTION_EXPRESSION &&
                std::string_view(static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getName()) == "llSetTimerEvent")
            {
                mTimers = true;
            }
        });
        findBooleans();
        if (mManyStates)
        {
            statesPreamble();
        }
        if (mTimers)
        {
            timersPreamble();
        }
        globals();
        functions();
        states();
        std::string out = mOptions.comments
                              ? "-- Written from LSL by Script Studio. Each \"-- LSL:\" comment marks a place where\n"
                                "-- SLua means something else than LSL did, or has a way of its own: read it, then\n"
                                "-- delete it. LSL's integers wrapped at 32 bits and its floats were single\n"
                                "-- precision; SLua's numbers are doubles, which do neither.\n\n"
                              : "-- Written from LSL by Script Studio. LSL's integers wrapped at 32 bits and its\n"
                                "-- floats were single precision; SLua's numbers are doubles, which do neither.\n\n";
        helpers(out);
        return out + mText;
    }
}

// static
ALLSLToSLua::Result ALLSLToSLua::convert(std::string_view lsl, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Result result;
    if (!ALLSLService::builtinsLoaded())
    {
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Error;
        p.source   = ALScriptProblem::Source::Assistant;
        p.key      = "AssistantNoDefinitions";
        p.message  = "the LSL definitions are not loaded, so nothing was written";
        result.problems.push_back(std::move(p));
        return result;
    }
    AL_SCRIPT_ENGINE_HELD;
    ScopedScriptParser parser(nullptr);
    LSLScript*         script = parser.parseLSLBytes(lsl.data(), static_cast<int>(lsl.size()));
    if (script)
    {
        script->collectSymbols();
        script->determineTypes();
    }
    if (!script || parser.logger.getErrors())
    {
        for (LogMessage* message : parser.logger.getMessages())
        {
            if (message->getType() != LOG_ERROR && message->getType() != LOG_INTERNAL_ERROR)
            {
                continue;
            }
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Error;
            p.source   = ALScriptProblem::Source::Parser;
            p.line     = zeroBased(message->getLoc()->first_line);
            p.column   = zeroBased(message->getLoc()->first_column);
            p.message  = message->getMessage();
            result.problems.push_back(std::move(p));
        }
        return result;
    }
    Writer writer(script, options);
    result.text      = writer.write();
    result.notes     = std::move(writer.notes());
    result.converted = true;
    return result;
}

// static
ALLSLToSLua::Result ALLSLToSLua::convert(std::string_view lsl)
{
    return convert(lsl, Options());
}

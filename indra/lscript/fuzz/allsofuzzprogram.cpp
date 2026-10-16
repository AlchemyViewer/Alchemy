/**
 * @file allsofuzzprogram.cpp
 * @brief libFuzzer on LL's LSL compiler and VM: the input chooses a
 * well-typed script, which is compiled and run
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "allsofuzz.h"

#include "../lscript_library.h"
#include "lscript_typecheck.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Bytes taken as raw source mostly end in the parser. Here they are a
// sequence of choices instead, each among what LSL allows at that point,
// the operators and their operand types drawn from LL's own type checker,
// the library from LL's own table: the script that comes out compiles more
// often than not, so what the fuzzer works on is the code generation of
// every construct, and the VM running it -- its heap, its stack, its
// faults, its library calls and its state changes. Set ALLSO_FUZZ_PRINT to
// see each script, as a crash's reproduction needs.

namespace
{
    using Type = LSCRIPTType;

    constexpr Type VALUE_TYPES[] = { LST_INTEGER, LST_FLOATINGPOINT, LST_STRING, LST_KEY, LST_VECTOR, LST_QUATERNION, LST_LIST };

    const char* typeName(Type type)
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
        default: return "";
        }
    }

    Type typeOfCode(char code)
    {
        switch (code)
        {
        case 'i': return LST_INTEGER;
        case 'f': return LST_FLOATINGPOINT;
        case 's': return LST_STRING;
        case 'k': return LST_KEY;
        case 'v': return LST_VECTOR;
        case 'q': return LST_QUATERNION;
        case 'l': return LST_LIST;
        default: return LST_UNDEFINED;
        }
    }

    struct Callable
    {
        std::string       name;
        Type              ret = LST_NULL;
        std::vector<Type> params;
    };

    struct Binary
    {
        const char* token;
        Type        left, right;
    };

    struct Unary
    {
        const char* token;
        Type        operand;
    };

    struct Assign
    {
        const char* token;
        Type        right;
        Type        result;  // the expression's, which a compound one may change
    };

    // What LL's compiler allows, by the type an expression is to have: its
    // operators, from the type checker's own tables, and its library.
    struct Grammar
    {
        std::vector<Binary>   binary[LST_EOF];
        std::vector<Unary>    unary[LST_EOF];
        std::vector<Type>     castFrom[LST_EOF];
        std::vector<Assign>   assign[LST_EOF];  // by the variable's type
        std::vector<Callable> library[LST_EOF + 1];  // void at LST_NULL

        Grammar()
        {
            init_supported_expressions();
            static const std::pair<LSCRIPTExpressionType, const char*> BINARY[] = {
                { LET_EQUALITY, "==" },  { LET_NOT_EQUALS, "!=" }, { LET_LESS_EQUALS, "<=" }, { LET_GREATER_EQUALS, ">=" },
                { LET_LESS_THAN, "<" },  { LET_GREATER_THAN, ">" }, { LET_PLUS, "+" },          { LET_MINUS, "-" },
                { LET_TIMES, "*" },      { LET_DIVIDE, "/" },       { LET_MOD, "%" },           { LET_BIT_AND, "&" },
                { LET_BIT_OR, "|" },     { LET_BIT_XOR, "^" },      { LET_BOOLEAN_AND, "&&" },  { LET_BOOLEAN_OR, "||" },
                { LET_SHIFT_LEFT, "<<" }, { LET_SHIFT_RIGHT, ">>" },
            };
            static const std::pair<LSCRIPTExpressionType, const char*> UNARY[] = {
                { LET_UNARY_MINUS, "-" }, { LET_BOOLEAN_NOT, "!" }, { LET_BIT_NOT, "~" },
            };
            // = is checked as an assignment, and is the variable's type; the
            // others as the operators they are, integer *= float a float.
            static const std::pair<LSCRIPTExpressionType, const char*> ASSIGN[] = {
                { LET_ADD_ASSIGN, "+=" }, { LET_SUB_ASSIGN, "-=" }, { LET_MUL_ASSIGN, "*=" },
                { LET_DIV_ASSIGN, "/=" }, { LET_MOD_ASSIGN, "%=" },
            };
            for (Type left : VALUE_TYPES)
            {
                for (Type right : VALUE_TYPES)
                {
                    LSCRIPTType result = LST_NULL;
                    for (const auto& [op, token] : BINARY)
                    {
                        if (legal_binary_expression(result, left, right, op) && result > LST_NULL && result < LST_EOF)
                        {
                            binary[result].push_back({ token, left, right });
                        }
                    }
                    if (legal_assignment(left, right))
                    {
                        assign[left].push_back({ "=", right, left });
                    }
                    for (const auto& [op, token] : ASSIGN)
                    {
                        if (legal_binary_expression(result, left, right, op) && result > LST_NULL && result < LST_EOF)
                        {
                            assign[left].push_back({ token, right, result });
                        }
                    }
                }
                LSCRIPTType result = LST_NULL;
                for (const auto& [op, token] : UNARY)
                {
                    if (legal_unary_expression(result, left, op) && result > LST_NULL && result < LST_EOF)
                    {
                        unary[result].push_back({ token, left });
                    }
                }
                for (Type to : VALUE_TYPES)
                {
                    if (to != left && legal_casts(to, left))
                    {
                        castFrom[to].push_back(left);
                    }
                }
            }
            for (const LLScriptLibraryFunction& function : gScriptLibrary.mFunctions)
            {
                if (!function.mName || function.mGodOnly)
                {
                    continue;
                }
                Callable callable;
                callable.name = function.mName;
                callable.ret  = function.mReturnType ? typeOfCode(function.mReturnType[0]) : LST_NULL;
                bool known    = callable.ret != LST_UNDEFINED;
                for (const char* code = function.mArgs; code && *code && known; ++code)
                {
                    callable.params.push_back(typeOfCode(*code));
                    known = callable.params.back() != LST_UNDEFINED;
                }
                if (known)
                {
                    library[callable.ret].push_back(std::move(callable));
                }
            }
        }
    };

    const Grammar& grammar()
    {
        static const Grammar g;
        return g;
    }

    struct Var
    {
        std::string name;
        Type        type;
    };

    // The events a state may handle, with the parameters each has.
    struct Event
    {
        const char*       name;
        std::vector<Type> params;
    };
    const Event EVENTS[] = {
        { "state_entry", {} },
        { "touch_start", { LST_INTEGER } },
        { "timer", {} },
        { "listen", { LST_INTEGER, LST_STRING, LST_KEY, LST_STRING } },
        { "link_message", { LST_INTEGER, LST_INTEGER, LST_STRING, LST_KEY } },
        { "at_target", { LST_INTEGER, LST_VECTOR, LST_VECTOR } },
        { "at_rot_target", { LST_INTEGER, LST_QUATERNION, LST_QUATERNION } },
        { "money", { LST_KEY, LST_INTEGER } },
        { "collision_start", { LST_INTEGER } },
        { "state_exit", {} },
    };

    class Generator
    {
    public:
        explicit Generator(FuzzedDataProvider& in)
            : mIn(in)
        {
        }

        std::string program()
        {
            const size_t states = pick(4);
            mStates.push_back("default");
            for (size_t i = 0; i < states; ++i)
            {
                mStates.push_back("s" + std::to_string(i));
            }
            for (size_t i = 0, n = pick(7); i < n; ++i)
            {
                global();
            }
            for (size_t i = 0, n = pick(5); i < n; ++i)
            {
                function();
            }
            for (const std::string& state : mStates)
            {
                stateBody(state);
            }
            return mOut;
        }

    private:
        // A choice among n, 0 the simplest: an input run dry chooses it
        // everywhere, and the script stays small and ends.
        size_t pick(size_t n) { return n <= 1 ? 0 : mIn.ConsumeIntegralInRange<size_t>(0, n - 1); }
        bool   chance(size_t in) { return pick(in) == 1; }
        Type   anyType() { return VALUE_TYPES[pick(std::size(VALUE_TYPES))]; }
        std::string fresh(const char* prefix) { return prefix + std::to_string(mNext++); }

        void line(const std::string& text)
        {
            mOut.append(mIndent * 4, ' ');
            mOut += text;
            mOut += '\n';
        }

        // --- literals ---------------------------------------------------------

        // The edges of a type now and then: most values are ordinary, so a
        // script runs on past its first math error or overflow and the
        // fuzzer reaches what comes after.
        bool wild() { return chance(8); }

        std::string integerLiteral()
        {
            static const char* const ORDINARY[] = { "0", "1", "2", "3", "7", "10", "-1", "-5", "42", "100" };
            static const char* const EDGES[]    = { "255",        "65536",      "2147483647", "-2147483648", "2147483648",
                                                    "0x7FFFFFFF", "0xFFFFFFFF", "0x80000000", "4294967296",  "-0x1" };
            if (wild())
            {
                return pick(2) ? std::to_string(mIn.ConsumeIntegral<S32>()) : EDGES[pick(std::size(EDGES))];
            }
            return ORDINARY[pick(std::size(ORDINARY))];
        }

        std::string floatLiteral()
        {
            static const char* const ORDINARY[] = { "0.0", "1.0", "0.5", "-1.5", "2.5", "-2.5", "0.1", "3.25", "10.0", "123.456" };
            static const char* const EDGES[]    = { "-0.0", "3.402823466e38", "1e-38", "1.401298464e-45", "1e39",
                                                    "1e-50", "16777217.0", "1e10", "-3.402823466e38" };
            if (wild())
            {
                const float value = mIn.ConsumeFloatingPoint<float>();
                if (pick(2) && std::isfinite(value))
                {
                    char text[48];
                    std::snprintf(text, sizeof(text), "%.9e", value);
                    return text;
                }
                return EDGES[pick(std::size(EDGES))];
            }
            return ORDINARY[pick(std::size(ORDINARY))];
        }

        std::string stringLiteral()
        {
            static const char* const PIECES[] = { "",        "a",  "hello", " ",       "\\n",  "\\t",  "\\\"", "\\\\", "\\q",
                                                  "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", ",",   ";",    "<1, 2, 3>",
                                                  "<1,2,3,4>", "0x1F", "-12",  "1e5",     "[", "]", "00000000-0000-0000-0000-000000000001" };
            // What casts to a float or vector that is not finite.
            static const char* const WILD[] = { "nan", "inf", "-inf", "<nan, 0, 0>", "<inf, 1, 1, 1>", "1e39" };
            std::string text = "\"";
            for (size_t i = 0, n = pick(6); i < n; ++i)
            {
                text += wild() ? WILD[pick(std::size(WILD))] : PIECES[pick(std::size(PIECES))];
            }
            if (chance(16))
            {
                text += std::string(pick(512), 'x');
            }
            return text + "\"";
        }

        std::string keyLiteral()
        {
            static const char* const KEYS[] = { "\"00000000-0000-0000-0000-000000000000\"", "\"a822ff2b-ff02-461d-b45d-dcd10a2de0c2\"",
                                                "\"not a key\"", "\"\"" };
            return KEYS[pick(std::size(KEYS))];
        }

        // A constant, as a global's initializer takes: literals, and vectors,
        // rotations and lists of them; a key as a string.
        std::string constant(Type type)
        {
            switch (type)
            {
            case LST_INTEGER: return integerLiteral();
            case LST_FLOATINGPOINT: return pick(3) == 0 ? integerLiteral() : floatLiteral();
            case LST_STRING: return stringLiteral();
            case LST_KEY: return keyLiteral();
            case LST_VECTOR: return "<" + floatLiteral() + ", " + floatLiteral() + ", " + floatLiteral() + ">";
            case LST_QUATERNION: return "<" + floatLiteral() + ", " + floatLiteral() + ", " + floatLiteral() + ", " + floatLiteral() + ">";
            case LST_LIST:
            {
                std::string text = "[";
                for (size_t i = 0, n = pick(6); i < n; ++i)
                {
                    Type element = anyType();
                    if (element == LST_LIST)
                    {
                        element = LST_INTEGER;
                    }
                    text += (i ? ", " : "") + constant(element);
                }
                return text + "]";
            }
            default: return "0";
            }
        }

        // --- variables --------------------------------------------------------

        std::vector<const Var*> variablesOf(Type type) const
        {
            std::vector<const Var*> out;
            for (const Var& var : mGlobals)
            {
                if (var.type == type)
                {
                    out.push_back(&var);
                }
            }
            for (const std::vector<Var>& scope : mScopes)
            {
                for (const Var& var : scope)
                {
                    if (var.type == type)
                    {
                        out.push_back(&var);
                    }
                }
            }
            return out;
        }

        const Var* variable(Type type)
        {
            const std::vector<const Var*> vars = variablesOf(type);
            return vars.empty() ? nullptr : vars[pick(vars.size())];
        }

        // --- expressions ------------------------------------------------------

        std::string leaf(Type type)
        {
            if (const Var* var = pick(2) == 1 ? variable(type) : nullptr)
            {
                return var->name;
            }
            static const char* const NAMED[LST_EOF][3] = {
                {},
                { "TRUE", "FALSE", "STRING_TRIM" },
                { "PI", "TWO_PI", "DEG_TO_RAD" },
                { "EOF", "NULL_KEY", "\"\"" },
                { "NULL_KEY", "NULL_KEY", "NULL_KEY" },
                { "ZERO_VECTOR", "ZERO_VECTOR", "ZERO_VECTOR" },
                { "ZERO_ROTATION", "ZERO_ROTATION", "ZERO_ROTATION" },
                { "[]", "[]", "[]" },
            };
            if (chance(5))
            {
                return NAMED[type][pick(3)];
            }
            switch (type)
            {
            case LST_KEY: return "(key)" + keyLiteral();
            case LST_LIST: return listInitializer(0);
            default: return constant(type);
            }
        }

        std::string listInitializer(int depth)
        {
            std::string text = "[";
            for (size_t i = 0, n = pick(6); i < n; ++i)
            {
                Type element = anyType();
                if (element == LST_LIST)
                {
                    element = LST_STRING;
                }
                text += (i ? ", " : "") + expression(element, depth - 1);
            }
            return text + "]";
        }

        std::string call(const Callable& callable, int depth)
        {
            std::string text = callable.name + "(";
            for (size_t i = 0; i < callable.params.size(); ++i)
            {
                text += (i ? ", " : "") + expression(callable.params[i], depth - 1);
            }
            return text + ")";
        }

        const Callable* callableReturning(Type type)
        {
            std::vector<const Callable*> candidates;
            for (const Callable& f : mFunctions)
            {
                if (f.ret == type)
                {
                    candidates.push_back(&f);
                }
            }
            const std::vector<Callable>& library = grammar().library[type];
            if (!library.empty() && (candidates.empty() || pick(3) != 0))
            {
                return &library[pick(library.size())];
            }
            return candidates.empty() ? nullptr : candidates[pick(candidates.size())];
        }

        std::string expression(Type type, int depth)
        {
            if (depth <= 0 || mNodes >= MAX_NODES)
            {
                return leaf(type);
            }
            ++mNodes;
            const Grammar& g = grammar();
            switch (pick(9))
            {
            case 1:
                if (!g.binary[type].empty())
                {
                    const Binary& op = g.binary[type][pick(g.binary[type].size())];
                    // A divisor that is not nothing, mostly.
                    const bool divides = (op.token == std::string("/") || op.token == std::string("%")) && !wild();
                    return "(" + expression(op.left, depth - 1) + " " + op.token + " " +
                           (divides ? divisor(op.right) : expression(op.right, depth - 1)) + ")";
                }
                break;
            case 2:
                if (!g.unary[type].empty())
                {
                    const Unary& op = g.unary[type][pick(g.unary[type].size())];
                    // Spaced, or - before a negative literal lexes as --.
                    return "(" + std::string(op.token) + " " + expression(op.operand, depth - 1) + ")";
                }
                break;
            case 3:
                if (!g.castFrom[type].empty())
                {
                    const Type from = g.castFrom[type][pick(g.castFrom[type].size())];
                    return "((" + std::string(typeName(type)) + ")(" + expression(from, depth - 1) + "))";
                }
                break;
            case 4:
                if (const Callable* callable = callableReturning(type))
                {
                    return call(*callable, depth);
                }
                break;
            case 5:
                if (const Var* var = variable(type))
                {
                    if ((type == LST_INTEGER || type == LST_FLOATINGPOINT) && pick(2) == 0)
                    {
                        static const char* const STEPS[] = { "++", "--" };
                        const char* step = STEPS[pick(2)];
                        return pick(2) ? "(" + std::string(step) + var->name + ")" : "(" + var->name + step + ")";
                    }
                    return "(" + assignment(*var, depth, type) + ")";
                }
                break;
            case 6:
                switch (type)
                {
                case LST_VECTOR:
                    return "<" + expression(LST_FLOATINGPOINT, depth - 1) + ", " + expression(LST_FLOATINGPOINT, depth - 1) + ", " +
                           expression(LST_FLOATINGPOINT, depth - 1) + ">";
                case LST_QUATERNION:
                    return "<" + expression(LST_FLOATINGPOINT, depth - 1) + ", " + expression(LST_FLOATINGPOINT, depth - 1) + ", " +
                           expression(LST_FLOATINGPOINT, depth - 1) + ", " + expression(LST_FLOATINGPOINT, depth - 1) + ">";
                case LST_LIST: return listInitializer(depth);
                case LST_FLOATINGPOINT:
                {
                    // A component of a vector or rotation variable.
                    const bool rotation = pick(2) == 1;
                    if (const Var* var = variable(rotation ? LST_QUATERNION : LST_VECTOR))
                    {
                        static const char* const MEMBERS[] = { ".x", ".y", ".z", ".s" };
                        return var->name + MEMBERS[pick(rotation ? 4 : 3)];
                    }
                    break;
                }
                default: break;
                }
                break;
            default: break;
            }
            return leaf(type);
        }

        std::string divisor(Type type)
        {
            switch (type)
            {
            case LST_INTEGER: return "3";
            case LST_FLOATINGPOINT: return "2.5";
            case LST_QUATERNION: return "<0.0, 0.0, 0.7071068, 0.7071068>";
            default: return expression(type, 0);
            }
        }

        // The variable assigned, by an operator its type takes; where the
        // assignment is a value, one whose value is of the type wanted.
        std::string assignment(const Var& var, int depth, Type wanted = LST_UNDEFINED)
        {
            std::vector<const Assign*> ops;
            for (const Assign& op : grammar().assign[var.type])
            {
                if (wanted == LST_UNDEFINED || op.result == wanted)
                {
                    ops.push_back(&op);
                }
            }
            if (ops.empty())
            {
                return var.name + " = " + expression(var.type, depth - 1);
            }
            const Assign& op = *ops[pick(ops.size())];
            return var.name + " " + op.token + " " + expression(op.right, depth - 1);
        }

        // --- statements -------------------------------------------------------

        void block(int depth)
        {
            line("{");
            ++mIndent;
            mScopes.emplace_back();
            for (size_t i = 0, n = pick(5); i < n && mStatements < MAX_STATEMENTS; ++i)
            {
                statement(depth - 1);
            }
            mScopes.pop_back();
            --mIndent;
            line("}");
        }

        std::string condition() { return expression(pick(3) == 0 ? anyType() : LST_INTEGER, EXPRESSION_DEPTH); }

        // A counter declared where the loop is, which the loop counts to a
        // bound, so most loops end before the step cap does.
        std::string counter()
        {
            const std::string name = fresh("c");
            line("integer " + name + " = 0;");
            mScopes.back().push_back({ name, LST_INTEGER });
            return name;
        }

        void statement(int depth)
        {
            ++mStatements;
            const int e = EXPRESSION_DEPTH;
            switch (depth <= 0 ? pick(3) : pick(13))
            {
            case 0:
            {
                const Type        type = anyType();
                const std::string name = fresh("l");
                line(std::string(typeName(type)) + " " + name + (pick(4) == 0 ? "" : " = " + expression(type, e)) + ";");
                mScopes.back().push_back({ name, type });
                return;
            }
            case 1:
                if (const Var* var = variable(anyType()))
                {
                    line(assignment(*var, e) + ";");
                    return;
                }
                break;
            case 2:
                line("llSay(0, (string)(" + expression(anyType(), e) + "));");
                return;
            case 3:
            {
                // A void function, the library's or the script's.
                if (const Callable* callable = callableReturning(LST_NULL))
                {
                    line(call(*callable, e) + ";");
                    return;
                }
                break;
            }
            case 4:
                line("if (" + condition() + ")");
                block(depth);
                if (pick(2))
                {
                    line("else");
                    block(depth);
                }
                return;
            case 5:
            {
                const std::string c = counter();
                line("while (" + c + " < " + std::to_string(pick(6)) + ")");
                line("{");
                ++mIndent;
                line("++" + c + ";");
                block(depth);
                --mIndent;
                line("}");
                return;
            }
            case 6:
            {
                const std::string c = counter();
                line("for (" + c + " = 0; " + c + " < " + std::to_string(pick(6)) + "; ++" + c + ")");
                block(depth);
                return;
            }
            case 7:
            {
                const std::string c = counter();
                line("do");
                line("{");
                ++mIndent;
                line("++" + c + ";");
                block(depth);
                --mIndent;
                line("}");
                line("while (" + c + " < " + std::to_string(pick(6)) + ");");
                return;
            }
            case 8:
                // A loop on whatever condition comes: it may never end, and
                // the step cap is what stops it.
                if (chance(3))
                {
                    line("while (" + condition() + ")");
                    block(depth);
                    return;
                }
                break;
            case 9:
                block(depth);
                return;
            case 10:
                if (!mLabels.empty())
                {
                    line("jump " + mLabels[pick(mLabels.size())] + ";");
                    return;
                }
                break;
            case 11:
                if (mInEvent && pick(3) == 0)
                {
                    line("state " + mStates[pick(mStates.size())] + ";");
                    return;
                }
                if (chance(4))
                {
                    line(mReturn == LST_NULL ? "return;" : "return " + expression(mReturn, e) + ";");
                    return;
                }
                break;
            case 12:
            {
                const bool rotation = pick(2) == 1;
                if (const Var* var = variable(rotation ? LST_QUATERNION : LST_VECTOR))
                {
                    static const char* const MEMBERS[] = { ".x", ".y", ".z", ".s" };
                    line(var->name + MEMBERS[pick(rotation ? 4 : 3)] + " = " + expression(LST_FLOATINGPOINT, e) + ";");
                    return;
                }
                break;
            }
            default: break;
            }
            line(expression(anyType(), e) + ";");
        }

        // A function's or a handler's body: its statements, with its labels
        // put among them at the top, where every jump in the body sees them,
        // and in a function with a value, a return at the end.
        void body()
        {
            mLabels.clear();
            for (size_t i = 0, n = pick(3); i < n; ++i)
            {
                mLabels.push_back(fresh("j"));
            }
            std::vector<std::string> unplaced = mLabels;
            line("{");
            ++mIndent;
            mScopes.emplace_back();
            for (size_t i = 0, n = pick(8); i < n && mStatements < MAX_STATEMENTS; ++i)
            {
                if (!unplaced.empty() && pick(3) == 0)
                {
                    line("@" + unplaced.back() + ";");
                    unplaced.pop_back();
                }
                statement(BLOCK_DEPTH);
            }
            for (const std::string& label : unplaced)
            {
                line("@" + label + ";");
            }
            if (mReturn != LST_NULL)
            {
                line("return " + expression(mReturn, EXPRESSION_DEPTH) + ";");
            }
            mScopes.pop_back();
            --mIndent;
            line("}");
            mLabels.clear();
        }

        void global()
        {
            const Type        type = anyType();
            const std::string name = fresh("g");
            std::string       init;
            if (pick(4) != 0)
            {
                // A global earlier may stand for the value, as LSL allows.
                const Var* earlier = pick(4) == 0 ? variable(type) : nullptr;
                init               = " = " + (earlier ? earlier->name : constant(type));
            }
            line(std::string(typeName(type)) + " " + name + init + ";");
            mGlobals.push_back({ name, type });
        }

        void function()
        {
            Callable f;
            f.name = fresh("f");
            f.ret  = pick(3) == 0 ? LST_NULL : anyType();
            mScopes.emplace_back();
            std::string params;
            for (size_t i = 0, n = pick(4); i < n; ++i)
            {
                const Type        type = anyType();
                const std::string name = fresh("p");
                params += (i ? ", " : "") + std::string(typeName(type)) + " " + name;
                f.params.push_back(type);
                mScopes.back().push_back({ name, type });
            }
            line((f.ret == LST_NULL ? "" : std::string(typeName(f.ret)) + " ") + f.name + "(" + params + ")");
            // Declared before its body, so the body may call it: recursion
            // without a bound, which the VM's stack has to meet.
            mFunctions.push_back(f);
            mReturn  = f.ret;
            mInEvent = false;
            body();
            mScopes.pop_back();
            mReturn = LST_NULL;
        }

        void stateBody(const std::string& state)
        {
            line(state == "default" ? "default" : "state " + state);
            line("{");
            ++mIndent;
            std::vector<size_t> events;
            for (size_t i = 0; i < std::size(EVENTS); ++i)
            {
                // state_entry most of the time, which every run starts with.
                if (i == 0 ? pick(4) != 0 : pick(3) == 0)
                {
                    events.push_back(i);
                }
            }
            if (events.empty())
            {
                events.push_back(0);
            }
            for (size_t index : events)
            {
                const Event& event = EVENTS[index];
                mScopes.emplace_back();
                std::string params;
                for (size_t i = 0; i < event.params.size(); ++i)
                {
                    const std::string name = fresh("e");
                    params += (i ? ", " : "") + std::string(typeName(event.params[i])) + " " + name;
                    mScopes.back().push_back({ name, event.params[i] });
                }
                line(std::string(event.name) + "(" + params + ")");
                mReturn  = LST_NULL;
                mInEvent = true;
                body();
                mScopes.pop_back();
            }
            --mIndent;
            line("}");
        }

        static constexpr int    EXPRESSION_DEPTH = 4;
        static constexpr int    BLOCK_DEPTH      = 3;
        static constexpr size_t MAX_NODES        = 600;
        static constexpr size_t MAX_STATEMENTS   = 160;

        FuzzedDataProvider&      mIn;
        std::string              mOut;
        std::vector<std::string> mStates;
        std::vector<Var>         mGlobals;
        std::vector<std::vector<Var>> mScopes;
        std::vector<Callable>    mFunctions;
        std::vector<std::string> mLabels;
        Type                     mReturn   = LST_NULL;
        bool                     mInEvent  = false;
        size_t                   mIndent   = 0;
        size_t                   mNodes    = 0;
        size_t                   mStatements = 0;
        U32                      mNext     = 0;
    };
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    ll_fuzz_lso::setUp();
    FuzzedDataProvider in(data, size);
    const std::string  text = Generator(in).program();
    static const bool  print = std::getenv("ALLSO_FUZZ_PRINT") != nullptr;
    if (print)
    {
        std::cerr << "--- script\n" << text << "--- end\n";
    }
    const ll_fuzz_lso::Outcome outcome = ll_fuzz_lso::compileAndRun(text);
    if (print)
    {
        std::cerr << (outcome.compiled ? "--- ran: " + (outcome.fault.empty() ? std::string("no fault") : outcome.fault) + "\n"
                                       : "--- not compiled\n" + outcome.messages + "--- end\n");
    }
    return 0;
}

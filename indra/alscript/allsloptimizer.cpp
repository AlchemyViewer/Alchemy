/**
 * @file allsloptimizer.cpp
 * @brief The LSL optimizer over Tailslide's tree.
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

#include "allsloptimizer.h"

#include "allslservice.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>
#include <tailslide/passes/pretty_print.hh>
#include <tailslide/passes/values.hh>
#include <tailslide/visitor.hh>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
#include <unordered_map>

namespace
{
    using namespace Tailslide;

    // ---- what the definitions say of the library -------------------------------------

    struct Trait
    {
        const char* name;
        bool        pure;
        bool        mustUse;
        bool        native;
    };
    const Trait TRAITS[] = {
#include "allsltraits.inc"
    };

    const Trait* traitOf(const char* name)
    {
        for (const Trait& t : TRAITS)
        {
            if (!strcmp(t.name, name))
            {
                return &t;
            }
        }
        return nullptr;
    }

    bool isPure(const char* name)
    {
        const Trait* t = traitOf(name);
        return t && t->pure;
    }

    S32 zeroBased(int one) { return std::max(0, one - 1); }

    // ---- numbers as the printer writes them ----------------------------------------------

    // The shortest text that reads back as the same single (or, for Luau,
    // double), with a dot so that it stays a float literal.
    std::string floatText(double v, bool wide)
    {
        char buffer[64];
        if (!wide)
        {
            const float f = static_cast<float>(v);
            for (int p = 1; p <= 9; ++p)
            {
                snprintf(buffer, sizeof(buffer), "%.*g", p, static_cast<double>(f));
                if (strtof(buffer, nullptr) == f)
                {
                    break;
                }
            }
        }
        else
        {
            for (int p = 1; p <= 17; ++p)
            {
                snprintf(buffer, sizeof(buffer), "%.*g", p, v);
                if (strtod(buffer, nullptr) == v)
                {
                    break;
                }
            }
        }
        std::string  text = buffer;
        const size_t e    = text.find_first_of("eE");
        std::string  mantissa = e == std::string::npos ? text : text.substr(0, e);
        std::string  exponent = e == std::string::npos ? std::string() : text.substr(e);
        if (mantissa.find('.') == std::string::npos)
        {
            mantissa += ".0";
        }
        return mantissa + exponent;
    }

    // An integral value that an integer literal can stand for: not
    // negative zero, which keeps its sign, and within the integer's range.
    bool integral(double v)
    {
        return std::isfinite(v) && std::floor(v) == v && !std::signbit(v) && v < 2147483648.0;
    }

    bool ascii(const char* s)
    {
        for (; *s; ++s)
        {
            if (static_cast<unsigned char>(*s) >= 0x80 || (*s < 0x20 && *s != '\n' && *s != '\t'))
            {
                return false;
            }
        }
        return true;
    }

    // ---- notes ---------------------------------------------------------------------------

    class Report
    {
    public:
        explicit Report(ALScriptProblems& problems) : mProblems(problems) {}

        void note(const Tailslide::YYLTYPE* loc, const std::string& message)
        {
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Note;
            p.source   = ALScriptProblem::Source::Optimizer;
            if (loc)
            {
                p.line      = zeroBased(loc->first_line);
                p.column    = zeroBased(loc->first_column);
                p.endLine   = zeroBased(loc->last_line);
                p.endColumn = std::max(0, loc->last_column);
            }
            p.message = message;
            mProblems.push_back(std::move(p));
        }

    private:
        ALScriptProblems& mProblems;
    };

    std::string render(LSLASTNode* node)
    {
        PrettyPrintOpts    opts{};
        PrettyPrintVisitor printer(opts);
        node->visit(&printer);
        std::string text = printer.mStream.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        {
            text.pop_back();
        }
        // A statement's rendering starts with its indentation.
        const size_t start = text.find_first_not_of(" \t");
        return start == std::string::npos ? text : text.substr(start);
    }

    // What an expression can be dropped without losing: no assignment, no
    // call but to a function the definitions call pure, no print.
    bool sideEffectFree(LSLASTNode* node)
    {
        if (!node)
        {
            return true;
        }
        switch (node->getNodeType())
        {
            case NODE_NULL:
            case NODE_CONSTANT:
            case NODE_IDENTIFIER:
            case NODE_TYPE:
                return true;
            case NODE_EXPRESSION:
            case NODE_AST_NODE_LIST:
                break;
            default:
                return false;
        }
        if (node->getNodeType() == NODE_EXPRESSION)
        {
            auto* expr = static_cast<LSLExpression*>(node);
            if (operation_mutates(expr->getOperation()))
            {
                return false;
            }
            switch (expr->getNodeSubType())
            {
                case NODE_PRINT_EXPRESSION:
                    return false;
                case NODE_FUNCTION_EXPRESSION:
                {
                    LSLSymbol* sym = expr->getSymbol();
                    if (!sym || sym->getSubType() != SYM_BUILTIN || !isPure(sym->getName()))
                    {
                        return false;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        for (LSLASTNode* child : *node)
        {
            if (!sideEffectFree(child))
            {
                return false;
            }
        }
        return true;
    }

    // Skips the parentheses around an expression.
    LSLExpression* bare(LSLExpression* expr)
    {
        while (expr && expr->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            expr = static_cast<LSLParenthesisExpression*>(expr)->getChildExpr();
        }
        return expr;
    }

    bool isInteger(LSLASTNode* node, int value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(cv)->getValue() == value;
    }

    bool isFloat(LSLASTNode* node, double value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_FLOAT_CONSTANT && static_cast<LSLFloatConstant*>(cv)->getValue() == value;
    }

    bool isEmptyString(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_STRING_CONSTANT && !*static_cast<LSLStringConstant*>(cv)->getValue();
    }

    bool isEmptyList(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_LIST_CONSTANT && static_cast<LSLListConstant*>(cv)->getLength() == 0;
    }

    // Whether the parent of an expression is another expression that could
    // bind a replacement differently: parentheses go around it then. The
    // right side of an assignment binds nothing.
    bool wantsParens(LSLASTNode* parent, LSLASTNode* old)
    {
        if (!parent || parent->getNodeType() != NODE_EXPRESSION)
        {
            return false;
        }
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                if (operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(1) == old)
                {
                    return false;
                }
                return true;
            case NODE_UNARY_EXPRESSION:
            case NODE_TYPECAST_EXPRESSION:
            case NODE_PRINT_EXPRESSION:
            case NODE_BOOL_CONVERSION_EXPRESSION:
                return true;
            default:
                return false;
        }
    }

    // Puts `replacement` where `old` stands, in parentheses if the place
    // could bind it differently.
    void putInPlace(LSLASTNode* old, LSLExpression* replacement, ScriptAllocator* allocator)
    {
        LSLASTNode* parent = old->getParent();
        replacement->setLoc(old->getLoc());
        if (wantsParens(parent, old) && replacement->getNodeSubType() != NODE_CONSTANT_EXPRESSION &&
            replacement->getNodeSubType() != NODE_LVALUE_EXPRESSION && replacement->getNodeSubType() != NODE_PARENTHESIS_EXPRESSION &&
            replacement->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            auto* parens = allocator->newTracked<LSLParenthesisExpression>(replacement);
            parens->setType(replacement->getType());
            parens->setLoc(old->getLoc());
            LSLASTNode::replaceNode(old, parens);
            return;
        }
        LSLASTNode::replaceNode(old, replacement);
    }

    // ---- the arithmetic of the target -------------------------------------------------------

    // Tailslide folds in double; the LSL VMs work in single, and integers
    // wrap. Every result goes through here so that a fold is what the VM
    // would have given, or nothing.
    class Behavior : public TailslideOperationBehavior
    {
    public:
        Behavior(ScriptAllocator* allocator, bool addstrings, ALLSLOptimizer::Target target)
            : TailslideOperationBehavior(allocator, true), mAddStrings(addstrings), mTarget(target)
        {
        }

        LSLConstant* operation(LSLOperator op, LSLConstant* cv, LSLConstant* other, Tailslide::YYLTYPE* lloc) override
        {
            const bool single = mTarget != ALLSLOptimizer::Target::Luau;
            // Two literals joined are a new entry in the constant pool.
            if (!mAddStrings && op == OP_PLUS && cv->getIType() == LST_STRING && other && other->getIType() == LST_STRING)
            {
                return nullptr;
            }
            if (cv->getIType() == LST_INTEGER && other && other->getIType() == LST_INTEGER)
            {
                const uint32_t a = static_cast<uint32_t>(static_cast<LSLIntegerConstant*>(cv)->getValue());
                const uint32_t b = static_cast<uint32_t>(static_cast<LSLIntegerConstant*>(other)->getValue());
                switch (op)
                {
                    case OP_PLUS:
                        return integer(static_cast<int32_t>(a + b));
                    case OP_MINUS:
                        return integer(static_cast<int32_t>(a - b));
                    case OP_MUL:
                        return integer(static_cast<int32_t>(a * b));
                    case OP_SHIFT_LEFT:
                        return b < 32 ? integer(static_cast<int32_t>(a << b)) : nullptr;
                    case OP_SHIFT_RIGHT:
                        return b < 32 ? integer(static_cast<int32_t>(a) >> static_cast<int32_t>(b)) : nullptr;
                    case OP_DIV:
                    case OP_MOD:
                        // Zero raises the error where the author wrote it;
                        // the one overflow is the VM's own business.
                        if (b == 0 || (static_cast<int32_t>(a) == INT32_MIN && static_cast<int32_t>(b) == -1))
                        {
                            return nullptr;
                        }
                        break;
                    default:
                        break;
                }
            }
            // Single-precision arithmetic: the operands as singles, one
            // operation in double, which is exact enough, rounded once.
            if (single && other && isNumber(cv) && isNumber(other) && (cv->getIType() == LST_FLOATINGPOINT || other->getIType() == LST_FLOATINGPOINT))
            {
                const double a = asSingle(cv);
                const double b = asSingle(other);
                switch (op)
                {
                    case OP_PLUS:
                        return single_(a + b);
                    case OP_MINUS:
                        return single_(a - b);
                    case OP_MUL:
                        return single_(a * b);
                    case OP_DIV:
                        return b == 0.0 ? nullptr : single_(a / b);
                    case OP_LESS:
                        return integer(a < b);
                    case OP_GREATER:
                        return integer(a > b);
                    case OP_LEQ:
                        return integer(a <= b);
                    case OP_GEQ:
                        return integer(a >= b);
                    case OP_EQ:
                        return integer(a == b);
                    case OP_NEQ:
                        return integer(a != b);
                    default:
                        break;
                }
            }
            return rounded(TailslideOperationBehavior::operation(op, cv, other, lloc));
        }

        LSLConstant* cast(LSLType* to, LSLConstant* cv, Tailslide::YYLTYPE* lloc) override
        {
            const LSLIType from = cv->getIType();
            const LSLIType into = to->getIType();
            // How Luau spells a float, and reads one, is its own.
            if (mTarget == ALLSLOptimizer::Target::Luau && ((from == LST_FLOATINGPOINT && into == LST_STRING) || (from == LST_STRING && into == LST_FLOATINGPOINT)))
            {
                return nullptr;
            }
            return rounded(TailslideOperationBehavior::cast(to, cv, lloc));
        }

    private:
        static bool isNumber(LSLConstant* c) { return c->getIType() == LST_INTEGER || c->getIType() == LST_FLOATINGPOINT; }

        static double asSingle(LSLConstant* c)
        {
            if (c->getIType() == LST_INTEGER)
            {
                return static_cast<double>(static_cast<float>(static_cast<LSLIntegerConstant*>(c)->getValue()));
            }
            return static_cast<double>(static_cast<float>(static_cast<LSLFloatConstant*>(c)->getValue()));
        }

        LSLConstant* integer(int v) { return _mAllocator->newTracked<LSLIntegerConstant>(v); }

        LSLConstant* single_(double v)
        {
            const float f = static_cast<float>(v);
            if (!std::isfinite(f))
            {
                return nullptr;
            }
            return _mAllocator->newTracked<LSLFloatConstant>(static_cast<double>(f));
        }

        LSLConstant* rounded(LSLConstant* c)
        {
            if (c && c->getIType() == LST_FLOATINGPOINT)
            {
                const double v = static_cast<LSLFloatConstant*>(c)->getValue();
                if (!std::isfinite(v))
                {
                    return nullptr;
                }
                if (mTarget != ALLSLOptimizer::Target::Luau)
                {
                    const float f = static_cast<float>(v);
                    if (!std::isfinite(f))
                    {
                        return nullptr;
                    }
                    if (static_cast<double>(f) != v)
                    {
                        return _mAllocator->newTracked<LSLFloatConstant>(static_cast<double>(f));
                    }
                }
            }
            return c;
        }

        bool                   mAddStrings;
        ALLSLOptimizer::Target mTarget;
    };

    // ---- the library, evaluated -----------------------------------------------------------------

    struct Ctx
    {
        ScriptAllocator*       allocator = nullptr;
        ALLSLOptimizer::Target target    = ALLSLOptimizer::Target::Mono;
        bool                   foldtabs  = false;

        LSLConstant* integer(int v) { return allocator->newTracked<LSLIntegerConstant>(v); }
        LSLConstant* number(double v)
        {
            const bool single = target != ALLSLOptimizer::Target::Luau;
            if (single)
            {
                const float f = static_cast<float>(v);
                if (!std::isfinite(f))
                {
                    return nullptr;
                }
                return allocator->newTracked<LSLFloatConstant>(static_cast<double>(f));
            }
            return std::isfinite(v) ? allocator->newTracked<LSLFloatConstant>(v) : nullptr;
        }
        LSLConstant* string(const std::string& s)
        {
            if (!ascii(s.c_str()) || (!foldtabs && s.find('\t') != std::string::npos))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLStringConstant>(allocator->copyStr(s.c_str()));
        }
        LSLConstant* key(const std::string& s)
        {
            if (!ascii(s.c_str()))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLKeyConstant>(allocator->copyStr(s.c_str()));
        }
        LSLConstant* vector(double x, double y, double z)
        {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLVectorConstant>(fx, fy, fz);
        }
    };

    typedef std::vector<LSLConstant*> Args;

    bool argInt(const Args& a, size_t i, int& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_INTEGER_CONSTANT)
        {
            return false;
        }
        out = static_cast<LSLIntegerConstant*>(a[i])->getValue();
        return true;
    }

    // A float argument, or an integer where LSL would promote one.
    bool argFloat(const Args& a, size_t i, double& out)
    {
        if (i >= a.size())
        {
            return false;
        }
        if (a[i]->getNodeSubType() == NODE_FLOAT_CONSTANT)
        {
            out = static_cast<double>(static_cast<float>(static_cast<LSLFloatConstant*>(a[i])->getValue()));
            return true;
        }
        if (a[i]->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            out = static_cast<double>(static_cast<float>(static_cast<LSLIntegerConstant*>(a[i])->getValue()));
            return true;
        }
        return false;
    }

    // A string argument, plain ASCII, so that no VM's idea of a character
    // differs from ours.
    bool argString(const Args& a, size_t i, std::string& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_STRING_CONSTANT)
        {
            return false;
        }
        const char* s = static_cast<LSLStringConstant*>(a[i])->getValue();
        if (!ascii(s))
        {
            return false;
        }
        out = s;
        return true;
    }

    bool argList(const Args& a, size_t i, LSLListConstant*& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_LIST_CONSTANT)
        {
            return false;
        }
        out = static_cast<LSLListConstant*>(a[i]);
        return true;
    }

    bool argVector(const Args& a, size_t i, Vector3& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_VECTOR_CONSTANT)
        {
            return false;
        }
        out = *static_cast<LSLVectorConstant*>(a[i])->getValue();
        return true;
    }

    std::vector<LSLConstant*> elements(LSLListConstant* list)
    {
        std::vector<LSLConstant*> out;
        for (LSLASTNode* child : *list)
        {
            out.push_back(static_cast<LSLConstant*>(child));
        }
        return out;
    }

    LSLConstant* listOf(Ctx& ctx, const std::vector<LSLConstant*>& items)
    {
        auto* list = ctx.allocator->newTracked<LSLListConstant>(nullptr);
        for (LSLConstant* item : items)
        {
            list->pushChild(item->copy(ctx.allocator));
        }
        return list;
    }

    // LSL's indices: negative from the end. `start > end` names the
    // outside of the range, for a get, and the inside for a delete.
    void normalise(int length, int& start, int& end)
    {
        if (start < 0)
        {
            start += length;
        }
        if (end < 0)
        {
            end += length;
        }
    }

    template <class T> std::vector<T> subRange(const std::vector<T>& v, int start, int end)
    {
        const int length = static_cast<int>(v.size());
        normalise(length, start, end);
        std::vector<T> out;
        if (start <= end)
        {
            for (int i = std::max(0, start); i <= std::min(length - 1, end); ++i)
            {
                out.push_back(v[i]);
            }
            return out;
        }
        for (int i = 0; i <= std::min(length - 1, end); ++i)
        {
            out.push_back(v[i]);
        }
        for (int i = std::max(0, start); i < length; ++i)
        {
            out.push_back(v[i]);
        }
        return out;
    }

    template <class T> std::vector<T> deleteRange(const std::vector<T>& v, int start, int end)
    {
        const int length = static_cast<int>(v.size());
        normalise(length, start, end);
        std::vector<T> out;
        for (int i = 0; i < length; ++i)
        {
            const bool inside = start <= end ? (i >= start && i <= end) : (i <= end || i >= start);
            if (!inside)
            {
                out.push_back(v[i]);
            }
        }
        return out;
    }

    std::vector<char> chars(const std::string& s) { return std::vector<char>(s.begin(), s.end()); }
    std::string       fromChars(const std::vector<char>& v) { return std::string(v.begin(), v.end()); }

    // What (string) makes of a float: six places, as both VMs write it.
    std::string floatString(double v)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(static_cast<float>(v)));
        return buffer;
    }

    // An element as llList2String and llList2CSV write it, or nothing for
    // what neither should fold.
    bool elementText(LSLConstant* c, std::string& out)
    {
        switch (c->getNodeSubType())
        {
            case NODE_INTEGER_CONSTANT:
                out = std::to_string(static_cast<LSLIntegerConstant*>(c)->getValue());
                return true;
            case NODE_FLOAT_CONSTANT:
                out = floatString(static_cast<LSLFloatConstant*>(c)->getValue());
                return true;
            case NODE_STRING_CONSTANT:
            case NODE_KEY_CONSTANT:
                out = static_cast<LSLStringConstant*>(c)->getValue();
                return ascii(out.c_str());
            case NODE_VECTOR_CONSTANT:
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(c)->getValue();
                out              = "<" + floatString(v->x) + ", " + floatString(v->y) + ", " + floatString(v->z) + ">";
                return true;
            }
            case NODE_QUATERNION_CONSTANT:
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(c)->getValue();
                out = "<" + floatString(q->x) + ", " + floatString(q->y) + ", " + floatString(q->z) + ", " + floatString(q->s) + ">";
                return true;
            }
            default:
                return false;
        }
    }

    const char* const BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string toBase64(const std::string& in)
    {
        std::string out;
        size_t      i = 0;
        while (i + 2 < in.size())
        {
            const uint32_t n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += BASE64[(n >> 6) & 63];
            out += BASE64[n & 63];
            i += 3;
        }
        if (i + 1 == in.size())
        {
            const uint32_t n = static_cast<unsigned char>(in[i]) << 16;
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += "==";
        }
        else if (i + 2 == in.size())
        {
            const uint32_t n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += BASE64[(n >> 6) & 63];
            out += '=';
        }
        return out;
    }

    // Strictly formed input only; anything else is the VM's to answer.
    bool fromBase64(const std::string& in, std::string& out)
    {
        if (in.size() % 4 != 0)
        {
            return false;
        }
        out.clear();
        for (size_t i = 0; i < in.size(); i += 4)
        {
            uint32_t n   = 0;
            int      pad = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char c = in[i + k];
                if (c == '=')
                {
                    if (i + 4 != in.size() || (k < 2))
                    {
                        return false;
                    }
                    ++pad;
                    n <<= 6;
                    continue;
                }
                if (pad)
                {
                    return false;
                }
                const char* p = strchr(BASE64, c);
                if (!p || !c)
                {
                    return false;
                }
                n = (n << 6) | static_cast<uint32_t>(p - BASE64);
            }
            out += static_cast<char>((n >> 16) & 255);
            if (pad < 2)
            {
                out += static_cast<char>((n >> 8) & 255);
            }
            if (pad < 1)
            {
                out += static_cast<char>(n & 255);
            }
        }
        return true;
    }

    typedef std::function<LSLConstant*(Ctx&, const Args&)> Evaluator;

    const std::unordered_map<std::string, Evaluator>& evaluators()
    {
        static const std::unordered_map<std::string, Evaluator> table = {
            { "llAbs",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  return c.integer(v < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(v)) : v);
              } },
            { "llFabs",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::fabs(v)) : nullptr;
              } },
            { "llFloor",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v)) return nullptr;
                  const double r = std::floor(v);
                  return (r >= -2147483648.0 && r < 2147483648.0) ? c.integer(static_cast<int>(r)) : nullptr;
              } },
            { "llCeil",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v)) return nullptr;
                  const double r = std::ceil(v);
                  return (r >= -2147483648.0 && r < 2147483648.0) ? c.integer(static_cast<int>(r)) : nullptr;
              } },
            { "llSqrt",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v >= 0.0 ? c.number(std::sqrt(v)) : nullptr;
              } },
            { "llPow",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double b, e;
                  return argFloat(a, 0, b) && argFloat(a, 1, e) ? c.number(std::pow(b, e)) : nullptr;
              } },
            { "llSin",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::sin(v)) : nullptr;
              } },
            { "llCos",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::cos(v)) : nullptr;
              } },
            { "llTan",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::tan(v)) : nullptr;
              } },
            { "llAtan2",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double y, x;
                  return argFloat(a, 0, y) && argFloat(a, 1, x) ? c.number(std::atan2(y, x)) : nullptr;
              } },
            { "llLog",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v > 0.0 ? c.number(std::log(v)) : nullptr;
              } },
            { "llLog10",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v > 0.0 ? c.number(std::log10(v)) : nullptr;
              } },
            { "llModPow",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int base, exponent, modulus;
                  if (!argInt(a, 0, base) || !argInt(a, 1, exponent) || !argInt(a, 2, modulus) || base < 0 || exponent < 0 || modulus <= 0)
                  {
                      return nullptr;
                  }
                  uint64_t result = 1 % static_cast<uint64_t>(modulus);
                  uint64_t b      = static_cast<uint64_t>(base) % static_cast<uint64_t>(modulus);
                  for (uint32_t e = static_cast<uint32_t>(exponent); e; e >>= 1)
                  {
                      if (e & 1)
                      {
                          result = result * b % static_cast<uint64_t>(modulus);
                      }
                      b = b * b % static_cast<uint64_t>(modulus);
                  }
                  return c.integer(static_cast<int>(result));
              } },
            { "llVecMag",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  return argVector(a, 0, v) ? c.number(std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z)) : nullptr;
              } },
            { "llVecDist",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 p, q;
                  if (!argVector(a, 0, p) || !argVector(a, 1, q)) return nullptr;
                  const float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
                  return c.number(std::sqrt(double(dx) * dx + double(dy) * dy + double(dz) * dz));
              } },
            { "llVecNorm",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  if (!argVector(a, 0, v)) return nullptr;
                  const float mag = static_cast<float>(std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
                  if (mag == 0.0f) return c.vector(0, 0, 0);
                  return c.vector(v.x / mag, v.y / mag, v.z / mag);
              } },
            { "llStringLength",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  return argString(a, 0, s) ? c.integer(static_cast<int>(s.size())) : nullptr;
              } },
            { "llGetSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         start, end;
                  if (!argString(a, 0, s) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return c.string(fromChars(subRange(chars(s), start, end)));
              } },
            { "llDeleteSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         start, end;
                  if (!argString(a, 0, s) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return c.string(fromChars(deleteRange(chars(s), start, end)));
              } },
            { "llInsertString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, ins;
                  int         at;
                  if (!argString(a, 0, s) || !argInt(a, 1, at) || !argString(a, 2, ins) || at < 0) return nullptr;
                  const size_t pos = std::min(s.size(), static_cast<size_t>(at));
                  return c.string(s.substr(0, pos) + ins + s.substr(pos));
              } },
            { "llSubStringIndex",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, p;
                  if (!argString(a, 0, s) || !argString(a, 1, p)) return nullptr;
                  const size_t at = s.find(p);
                  return c.integer(at == std::string::npos ? -1 : static_cast<int>(at));
              } },
            { "llToUpper",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  for (char& ch : s) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                  return c.string(s);
              } },
            { "llToLower",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  for (char& ch : s) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
                  return c.string(s);
              } },
            { "llStringTrim",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         how;
                  if (!argString(a, 0, s) || !argInt(a, 1, how)) return nullptr;
                  const auto blank = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
                  size_t     from  = 0;
                  size_t     to    = s.size();
                  if (how & 1)
                  {
                      while (from < to && blank(s[from])) ++from;
                  }
                  if (how & 2)
                  {
                      while (to > from && blank(s[to - 1])) --to;
                  }
                  return c.string(s.substr(from, to - from));
              } },
            { "llEscapeURL",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  std::string out;
                  for (char ch : s)
                  {
                      if (isalnum(static_cast<unsigned char>(ch)))
                      {
                          out += ch;
                      }
                      else
                      {
                          char buffer[8];
                          snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(ch));
                          out += buffer;
                      }
                  }
                  return c.string(out);
              } },
            { "llUnescapeURL",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  std::string out;
                  for (size_t i = 0; i < s.size(); ++i)
                  {
                      if (s[i] != '%')
                      {
                          out += s[i];
                          continue;
                      }
                      if (i + 2 >= s.size() || !isxdigit(static_cast<unsigned char>(s[i + 1])) || !isxdigit(static_cast<unsigned char>(s[i + 2])))
                      {
                          return nullptr;
                      }
                      const int byte = static_cast<int>(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                      if (byte == 0 || byte >= 0x80)
                      {
                          return nullptr;
                      }
                      out += static_cast<char>(byte);
                      i += 2;
                  }
                  return c.string(out);
              } },
            { "llStringToBase64",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  return argString(a, 0, s) ? c.string(toBase64(s)) : nullptr;
              } },
            { "llBase64ToString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, out;
                  if (!argString(a, 0, s) || !fromBase64(s, out) || out.find('\0') != std::string::npos) return nullptr;
                  return c.string(out);
              } },
            { "llIntegerToBase64",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  const uint32_t u = static_cast<uint32_t>(v);
                  std::string    bytes{ static_cast<char>(u >> 24), static_cast<char>(u >> 16), static_cast<char>(u >> 8), static_cast<char>(u) };
                  return c.string(toBase64(bytes));
              } },
            { "llBase64ToInteger",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, out;
                  if (!argString(a, 0, s) || s.size() != 8 || !fromBase64(s, out) || out.size() != 4) return nullptr;
                  const uint32_t u = (static_cast<unsigned char>(out[0]) << 24) | (static_cast<unsigned char>(out[1]) << 16) |
                                     (static_cast<unsigned char>(out[2]) << 8) | static_cast<unsigned char>(out[3]);
                  return c.integer(static_cast<int>(u));
              } },
            { "llChar",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  if (v == 0) return c.string(std::string());
                  return (v > 0 && v < 128) ? c.string(std::string(1, static_cast<char>(v))) : nullptr;
              } },
            { "llOrd",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         at;
                  if (!argString(a, 0, s) || !argInt(a, 1, at)) return nullptr;
                  const int length = static_cast<int>(s.size());
                  if (at < 0) at += length;
                  return (at >= 0 && at < length) ? c.integer(static_cast<unsigned char>(s[at])) : c.integer(0);
              } },
            { "llReplaceSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, pattern, with;
                  int         count;
                  if (!argString(a, 0, s) || !argString(a, 1, pattern) || !argString(a, 2, with) || !argInt(a, 3, count) || pattern.empty() || count < 0)
                  {
                      return nullptr;
                  }
                  std::string out;
                  size_t      from = 0;
                  int         done = 0;
                  while (true)
                  {
                      const size_t at = s.find(pattern, from);
                      if (at == std::string::npos || (count > 0 && done == count))
                      {
                          out += s.substr(from);
                          break;
                      }
                      out += s.substr(from, at - from) + with;
                      from = at + pattern.size();
                      ++done;
                  }
                  return c.string(out);
              } },
            { "llList2List",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              start, end;
                  if (!argList(a, 0, l) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return listOf(c, subRange(elements(l), start, end));
              } },
            { "llDeleteSubList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              start, end;
                  if (!argList(a, 0, l) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return listOf(c, deleteRange(elements(l), start, end));
              } },
            { "llListInsertList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant *l, *ins;
                  int              at;
                  if (!argList(a, 0, l) || !argList(a, 1, ins) || !argInt(a, 2, at)) return nullptr;
                  std::vector<LSLConstant*> items = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  if (at < 0) at += length;
                  if (at < 0) return nullptr;
                  const size_t              pos = std::min(items.size(), static_cast<size_t>(at));
                  std::vector<LSLConstant*> more = elements(ins);
                  items.insert(items.begin() + pos, more.begin(), more.end());
                  return listOf(c, items);
              } },
            { "llListReplaceList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant *l, *with;
                  int              start, end;
                  if (!argList(a, 0, l) || !argList(a, 1, with) || !argInt(a, 2, start) || !argInt(a, 3, end)) return nullptr;
                  std::vector<LSLConstant*> items  = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  normalise(length, start, end);
                  if (start > end || start < 0 || start > length) return nullptr;
                  std::vector<LSLConstant*> out(items.begin(), items.begin() + std::min(length, start));
                  std::vector<LSLConstant*> more = elements(with);
                  out.insert(out.end(), more.begin(), more.end());
                  if (end + 1 < length)
                  {
                      out.insert(out.end(), items.begin() + end + 1, items.end());
                  }
                  return listOf(c, out);
              } },
            { "llGetListEntryType",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              at;
                  if (!argList(a, 0, l) || !argInt(a, 1, at)) return nullptr;
                  std::vector<LSLConstant*> items  = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  if (at < 0) at += length;
                  if (at < 0 || at >= length) return c.integer(0);
                  switch (items[at]->getNodeSubType())
                  {
                      case NODE_INTEGER_CONSTANT: return c.integer(1);
                      case NODE_FLOAT_CONSTANT: return c.integer(2);
                      case NODE_STRING_CONSTANT: return c.integer(3);
                      case NODE_KEY_CONSTANT: return c.integer(4);
                      case NODE_VECTOR_CONSTANT: return c.integer(5);
                      case NODE_QUATERNION_CONSTANT: return c.integer(6);
                      default: return nullptr;
                  }
              } },
            { "llList2CSV",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  if (!argList(a, 0, l)) return nullptr;
                  std::string out;
                  bool        first = true;
                  for (LSLConstant* item : elements(l))
                  {
                      std::string text;
                      if (!elementText(item, text)) return nullptr;
                      out += (first ? "" : ", ") + text;
                      first = false;
                  }
                  return c.string(out);
              } },
        };
        return table;
    }

    // llList2Integer and its kin: the element where it is the type asked
    // for, the type's nothing where the index is off the end, and the
    // VM's own answer otherwise.
    LSLConstant* listGet(Ctx& c, const Args& a, LSLNodeSubType wanted)
    {
        LSLListConstant* l;
        int              at;
        if (!argList(a, 0, l) || !argInt(a, 1, at))
        {
            return nullptr;
        }
        std::vector<LSLConstant*> items  = elements(l);
        const int                 length = static_cast<int>(items.size());
        if (at < 0)
        {
            at += length;
        }
        if (at < 0 || at >= length)
        {
            switch (wanted)
            {
                case NODE_INTEGER_CONSTANT: return c.integer(0);
                case NODE_FLOAT_CONSTANT: return c.number(0.0);
                case NODE_STRING_CONSTANT: return c.string(std::string());
                case NODE_KEY_CONSTANT: return c.key(std::string());
                case NODE_VECTOR_CONSTANT: return c.vector(0, 0, 0);
                case NODE_QUATERNION_CONSTANT: return c.allocator->newTracked<LSLQuaternionConstant>(0.f, 0.f, 0.f, 1.f);
                default: return nullptr;
            }
        }
        LSLConstant* item = items[at];
        if (wanted == NODE_STRING_CONSTANT)
        {
            std::string text;
            return elementText(item, text) ? c.string(text) : nullptr;
        }
        if (wanted == NODE_KEY_CONSTANT && item->getNodeSubType() == NODE_STRING_CONSTANT)
        {
            return c.key(static_cast<LSLStringConstant*>(item)->getValue());
        }
        return item->getNodeSubType() == wanted ? item->copy(c.allocator) : nullptr;
    }

    LSLConstant* evaluate(Ctx& ctx, const char* name, const Args& args)
    {
        if (!strcmp(name, "llList2Integer")) return listGet(ctx, args, NODE_INTEGER_CONSTANT);
        if (!strcmp(name, "llList2Float")) return listGet(ctx, args, NODE_FLOAT_CONSTANT);
        if (!strcmp(name, "llList2String")) return listGet(ctx, args, NODE_STRING_CONSTANT);
        if (!strcmp(name, "llList2Key")) return listGet(ctx, args, NODE_KEY_CONSTANT);
        if (!strcmp(name, "llList2Vector")) return listGet(ctx, args, NODE_VECTOR_CONSTANT);
        if (!strcmp(name, "llList2Rot")) return listGet(ctx, args, NODE_QUATERNION_CONSTANT);
        auto it = evaluators().find(name);
        return it == evaluators().end() ? nullptr : it->second(ctx, args);
    }

    // ---- the passes -----------------------------------------------------------------------------

    struct Pass
    {
        Ctx&                          ctx;
        Report&                       report;
        const ALLSLOptimizer::Options& options;
        int                           changes = 0;

        Pass(Ctx& c, Report& r, const ALLSLOptimizer::Options& o) : ctx(c), report(r), options(o) {}

        LSLConstantExpression* constant(LSLConstant* cv, LSLASTNode* at)
        {
            auto* expr = ctx.allocator->newTracked<LSLConstantExpression>(cv);
            expr->setLoc(at->getLoc());
            return expr;
        }

        // Whether a value may be written into the script as a literal. A
        // list literal is no smaller than the list expression it came
        // from, but it is smaller than a call.
        bool inlineable(LSLConstant* cv, bool listsToo = false)
        {
            if (!cv || cv->containsNaN())
            {
                return false;
            }
            switch (cv->getIType())
            {
                case LST_LIST:
                    if (!listsToo)
                    {
                        return false;
                    }
                    for (LSLASTNode* item : *cv)
                    {
                        if (!inlineable(static_cast<LSLConstant*>(item)) && item->getIType() != LST_KEY)
                        {
                            return false;
                        }
                    }
                    return true;
                case LST_KEY:
                    // A key literal is a string.
                    return false;
                case LST_STRING:
                {
                    const char* s = static_cast<LSLStringConstant*>(cv)->getValue();
                    return options.foldtabs || !strchr(s, '\t');
                }
                case LST_FLOATINGPOINT:
                    return std::isfinite(static_cast<LSLFloatConstant*>(cv)->getValue());
                default:
                    return true;
            }
        }

        void fold(LSLASTNode* node, LSLConstant* cv, const char* what)
        {
            const std::string was = render(node);
            LSLConstantExpression* expr = constant(cv, node);
            const std::string      now  = render(expr);
            if (was != now)
            {
                report.note(node->getLoc(), std::string(what) + " " + was + " to " + now);
            }
            LSLASTNode::replaceNode(node, expr);
            ++changes;
        }
    };

    // Constant expressions and never-assigned variables become their
    // values; a call to a pure library function with its arguments in
    // hand becomes its answer.
    class Folder : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLExpression* expr) override
        {
            switch (expr->getNodeSubType())
            {
                case NODE_CONSTANT_EXPRESSION:
                    return false;
                case NODE_FUNCTION_EXPRESSION:
                    return !call(static_cast<LSLFunctionExpression*>(expr));
                default:
                    break;
            }
            LSLConstant* cv = expr->getConstantValue();
            if (cv && inlineable(cv))
            {
                fold(expr, cv, "folded");
                return false;
            }
            return true;
        }

        bool visit(LSLLValueExpression* lvalue) override
        {
            LSLSymbol* sym = lvalue->getSymbol();
            if (!sym || sym->getSubType() == SYM_BUILTIN || sym->getIType() == LST_LIST)
            {
                // A builtin constant is a token the compiler already has.
                return false;
            }
            if (sym->getIType() == LST_KEY && !keyMayInline(lvalue))
            {
                return false;
            }
            LSLConstant* cv = lvalue->getConstantValue();
            if (cv && inlineable(cv))
            {
                fold(lvalue, cv, "inlined");
            }
            return false;
        }

    private:
        // Tailslide's rules: a key's key-ness must not be lost to a list,
        // a print, a condition or a boolean.
        static bool keyMayInline(LSLLValueExpression* lvalue)
        {
            LSLASTNode* ancestor = lvalue->getParent();
            LSLASTNode* top      = lvalue;
            while (ancestor && ancestor->getNodeType() == NODE_EXPRESSION)
            {
                switch (ancestor->getNodeSubType())
                {
                    case NODE_LIST_EXPRESSION:
                    case NODE_PRINT_EXPRESSION:
                    case NODE_BOOL_CONVERSION_EXPRESSION:
                        return false;
                    case NODE_TYPECAST_EXPRESSION:
                        if (ancestor->getIType() == LST_LIST)
                        {
                            return false;
                        }
                        break;
                    default:
                        break;
                }
                top      = ancestor;
                ancestor = ancestor->getParent();
            }
            if (top->getIType() == LST_KEY)
            {
                if (ancestor && ancestor->getNodeType() == NODE_AST_NODE_LIST)
                {
                    ancestor = ancestor->getParent();
                }
                if (ancestor && ancestor->getNodeType() == NODE_STATEMENT)
                {
                    switch (ancestor->getNodeSubType())
                    {
                        case NODE_WHILE_STATEMENT:
                        case NODE_IF_STATEMENT:
                        case NODE_DO_STATEMENT:
                        case NODE_FOR_STATEMENT:
                            return false;
                        default:
                            break;
                    }
                }
            }
            return true;
        }

        bool call(LSLFunctionExpression* expr)
        {
            LSLSymbol* sym = expr->getSymbol();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !isPure(sym->getName()))
            {
                return false;
            }
            Args args;
            for (LSLASTNode* arg : *expr->getArguments())
            {
                LSLConstant* cv = arg->getConstantValue();
                if (!cv && arg->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    // A list is never written into the script in place of
                    // its name, but a call may still be answered from it.
                    auto*      lvalue = static_cast<LSLLValueExpression*>(arg);
                    LSLSymbol* sym    = lvalue->getSymbol();
                    if (sym && !lvalue->getMember() && sym->getAssignments() == 0 && sym->getSubType() != SYM_BUILTIN)
                    {
                        cv = sym->getConstantValue();
                    }
                }
                if (!cv)
                {
                    return false;
                }
                args.push_back(cv);
            }
            LSLConstant* value = evaluate(ctx, sym->getName(), args);
            if (!value || !inlineable(value, true))
            {
                return false;
            }
            fold(expr, value, "evaluated");
            return true;
        }
    };

    // The identities, the shorter spellings, and the reshaped conditions.
    class Simplifier : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLBinaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (!left || !right || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            const LSLOperator op   = expr->getOperation();
            const LSLIType    type = expr->getIType();
            const bool        keepLeft  = left->getIType() == type;
            const bool        keepRight = right->getIType() == type;
            const auto        zero      = [](LSLASTNode* n) { return isInteger(n, 0) || isFloat(n, 0.0); };
            const auto        one       = [](LSLASTNode* n) { return isInteger(n, 1) || isFloat(n, 1.0); };
            switch (op)
            {
                case OP_PLUS:
                    if (keepLeft && (zero(right) || isEmptyString(right) || isEmptyList(right))) return keep(expr, 0);
                    if (keepRight && (zero(left) || isEmptyString(left) || isEmptyList(left))) return keep(expr, 1);
                    if (keepLeft && negative(right)) return resign(expr, OP_MINUS);
                    break;
                case OP_MINUS:
                    if (keepLeft && zero(right)) return keep(expr, 0);
                    if (keepLeft && negative(right)) return resign(expr, OP_PLUS);
                    break;
                case OP_MUL:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    if (keepRight && one(left)) return keep(expr, 1);
                    if (type == LST_INTEGER && isInteger(right, 0) && sideEffectFree(left)) return become(expr, ctx.integer(0));
                    if (type == LST_INTEGER && isInteger(left, 0) && sideEffectFree(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_DIV:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    break;
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_SHIFT_LEFT:
                case OP_SHIFT_RIGHT:
                    if (isInteger(right, 0)) return keep(expr, 0);
                    if ((op == OP_BIT_OR || op == OP_BIT_XOR) && isInteger(left, 0)) return keep(expr, 1);
                    break;
                case OP_BIT_AND:
                    if (isInteger(right, -1)) return keep(expr, 0);
                    if (isInteger(left, -1)) return keep(expr, 1);
                    if (isInteger(right, 0) && sideEffectFree(left)) return become(expr, ctx.integer(0));
                    if (isInteger(left, 0) && sideEffectFree(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_EQ:
                    // `x == 0` is `!x`, and one token shorter.
                    if (left->getIType() == LST_INTEGER && isInteger(right, 0)) return negate(expr, 0);
                    if (right->getIType() == LST_INTEGER && isInteger(left, 0)) return negate(expr, 1);
                    break;
                case OP_BOOLEAN_AND:
                case OP_BOOLEAN_OR:
                    condition(expr, 0);
                    condition(expr, 1);
                    break;
                default:
                    break;
            }
            return false;
        }

        bool visit(LSLUnaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (!child || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            LSLExpression* inner = bare(child);
            if (expr->getOperation() == OP_MINUS && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_MINUS)
            {
                // -(-x)
                LSLExpression* x = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (x && x->getIType() == expr->getIType())
                {
                    report.note(expr->getLoc(), "simplified " + render(expr) + " to " + render(x));
                    inner->setChild(0, nullptr);
                    putInPlace(expr, x, ctx.allocator);
                    ++changes;
                }
                return false;
            }
            if (expr->getOperation() == OP_BOOLEAN_NOT)
            {
                condition(expr, 0);
                child = expr->getChildExpr();
                inner = bare(child);
                if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getIType() == LST_INTEGER)
                {
                    // !(a == b) is a != b, and the other way; the orderings
                    // only for integers, which have no NaN.
                    auto*       bin      = static_cast<LSLBinaryExpression*>(inner);
                    LSLOperator opposite = OP_NONE;
                    const bool  ints     = bin->getLHS()->getIType() == LST_INTEGER && bin->getRHS()->getIType() == LST_INTEGER;
                    switch (bin->getOperation())
                    {
                        case OP_EQ: opposite = OP_NEQ; break;
                        case OP_NEQ: opposite = OP_EQ; break;
                        case OP_LESS: opposite = ints ? OP_GEQ : OP_NONE; break;
                        case OP_GREATER: opposite = ints ? OP_LEQ : OP_NONE; break;
                        case OP_LEQ: opposite = ints ? OP_GREATER : OP_NONE; break;
                        case OP_GEQ: opposite = ints ? OP_LESS : OP_NONE; break;
                        default: break;
                    }
                    if (opposite != OP_NONE && (bin->getLHS()->getIType() != LST_LIST || opposite == OP_EQ || opposite == OP_NEQ))
                    {
                        const std::string was = render(expr);
                        bin->setOperation(opposite);
                        inner->getParent()->takeChild(inner->getParentSlot());
                        report.note(expr->getLoc(), "simplified " + was + " to " + render(bin));
                        putInPlace(expr, bin, ctx.allocator);
                        ++changes;
                    }
                }
            }
            return false;
        }

        bool visit(LSLTypecastExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (child && child->getIType() == expr->getIType() && expr->getIType() != LST_ERROR)
            {
                report.note(expr->getLoc(), "dropped the cast in " + render(expr));
                expr->setChild(0, nullptr);
                putInPlace(expr, child, ctx.allocator);
                ++changes;
                return false;
            }
            return false;
        }

        bool visit(LSLFunctionExpression* expr) override
        {
            visitChildren(expr);
            LSLSymbol* sym = expr->getSymbol();
            if (options.listlength && sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llGetListLength") &&
                expr->getArguments()->getNumChildren() == 1)
            {
                // llGetListLength(x) is x != [], which is the length.
                auto* arg = static_cast<LSLExpression*>(expr->getArguments()->takeChild(0));
                auto* empty = ctx.allocator->newTracked<LSLConstantExpression>(ctx.allocator->newTracked<LSLListConstant>(nullptr));
                auto* test  = ctx.allocator->newTracked<LSLBinaryExpression>(arg, OP_NEQ, static_cast<LSLExpression*>(empty));
                test->setType(TYPE(LST_INTEGER));
                report.note(expr->getLoc(), "wrote " + render(expr) + " as " + render(test));
                putInPlace(expr, test, ctx.allocator);
                ++changes;
            }
            return false;
        }

        bool visit(LSLListExpression* expr) override
        {
            visitChildren(expr);
            if (!options.listadd || options.target != ALLSLOptimizer::Target::Mono || expr->getNumChildren() == 0)
            {
                return false;
            }
            // Not in a global's initializer, which must stay simple.
            for (LSLASTNode* up = expr->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return false;
                }
            }
            for (LSLASTNode* child : *expr)
            {
                if (child->getIType() == LST_LIST || child->getIType() == LST_ERROR)
                {
                    return false;
                }
            }
            // [a, b, c] as (list)a + b + c.
            const std::string was = render(expr);
            // takeChild leaves a null in the slot, dropped each time.
            auto* first = static_cast<LSLExpression*>(expr->takeChild(0));
            expr->removeChild(expr->getChild(0));
            LSLExpression* sum = ctx.allocator->newTracked<LSLTypecastExpression>(TYPE(LST_LIST), first);
            sum->setLoc(first->getLoc());
            while (expr->hasChildren())
            {
                auto* next = static_cast<LSLExpression*>(expr->takeChild(0));
                expr->removeChild(expr->getChild(0));
                sum = ctx.allocator->newTracked<LSLBinaryExpression>(sum, OP_PLUS, next);
                sum->setType(TYPE(LST_LIST));
                sum->setLoc(next->getLoc());
            }
            report.note(expr->getLoc(), "wrote " + was + " as " + render(sum));
            putInPlace(expr, sum, ctx.allocator);
            ++changes;
            return false;
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            if (!options.ifelseswap)
            {
                return false;
            }
            LSLExpression* cond  = stmt->getCheckExpr();
            LSLStatement*  yes   = stmt->getTrueBranch();
            LSLStatement*  no    = stmt->getFalseBranch();
            LSLExpression* inner = bare(cond);
            if (no && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                // if (!c) A else B is if (c) B else A.
                LSLExpression* c = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (c)
                {
                    inner->setChild(0, nullptr);
                    stmt->setCheckExpr(c);
                    swapBranches(stmt);
                    report.note(stmt->getLoc(), "swapped the branches of if (" + render(c) + ") and dropped the !");
                    ++changes;
                }
                return false;
            }
            if (no && empty(yes) && cond->getIType() == LST_INTEGER)
            {
                // if (c) ; else B is if (!c) B.
                stmt->setCheckExpr(nullptr);
                auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(cond, OP_BOOLEAN_NOT);
                negated->setType(TYPE(LST_INTEGER));
                negated->setLoc(cond->getLoc());
                stmt->setCheckExpr(negated);
                stmt->setTrueBranch(nullptr);
                stmt->setFalseBranch(nullptr);
                stmt->setTrueBranch(no);
                report.note(stmt->getLoc(), "turned an empty if branch around");
                ++changes;
            }
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

    private:
        static bool empty(LSLStatement* s)
        {
            if (!s)
            {
                return true;
            }
            if (s->getNodeSubType() == NODE_NOP_STATEMENT)
            {
                return true;
            }
            return s->getNodeSubType() == NODE_COMPOUND_STATEMENT && !s->hasChildren();
        }

        static void swapBranches(LSLIfStatement* stmt)
        {
            LSLStatement* yes = stmt->getTrueBranch();
            LSLStatement* no  = stmt->getFalseBranch();
            stmt->setTrueBranch(nullptr);
            stmt->setFalseBranch(nullptr);
            stmt->setTrueBranch(no);
            stmt->setFalseBranch(yes);
        }

        // A constant below zero, whose sign can move onto the operator.
        static bool negative(LSLASTNode* n)
        {
            LSLConstant* cv = n->getConstantValue();
            if (!cv)
            {
                return false;
            }
            if (cv->getNodeSubType() == NODE_INTEGER_CONSTANT)
            {
                const int v = static_cast<LSLIntegerConstant*>(cv)->getValue();
                return v < 0 && v != INT32_MIN;
            }
            if (cv->getNodeSubType() == NODE_FLOAT_CONSTANT)
            {
                return static_cast<LSLFloatConstant*>(cv)->getValue() < 0.0;
            }
            return false;
        }

        // The expression becomes one of its operands.
        bool keep(LSLBinaryExpression* expr, int slot)
        {
            const std::string was  = render(expr);
            auto*             kept = static_cast<LSLExpression*>(expr->takeChild(slot));
            report.note(expr->getLoc(), "simplified " + was + " to " + render(kept));
            putInPlace(expr, kept, ctx.allocator);
            ++changes;
            return false;
        }

        bool become(LSLBinaryExpression* expr, LSLConstant* cv)
        {
            fold(expr, cv, "simplified");
            return false;
        }

        // x + -c as x - c, x - -c as x + c.
        bool resign(LSLBinaryExpression* expr, LSLOperator op)
        {
            const std::string was = render(expr);
            LSLConstant*      cv  = expr->getRHS()->getConstantValue();
            LSLConstant*      pos = cv->getNodeSubType() == NODE_INTEGER_CONSTANT
                                        ? ctx.integer(-static_cast<LSLIntegerConstant*>(cv)->getValue())
                                        : ctx.allocator->newTracked<LSLFloatConstant>(-static_cast<LSLFloatConstant*>(cv)->getValue());
            expr->setOperation(op);
            expr->setRHS(constant(pos, expr->getRHS()));
            report.note(expr->getLoc(), "simplified " + was + " to " + render(expr));
            ++changes;
            return false;
        }

        // x == 0 as !x.
        bool negate(LSLBinaryExpression* expr, int slot)
        {
            const std::string was = render(expr);
            auto*             x   = static_cast<LSLExpression*>(expr->takeChild(slot));
            LSLExpression*    operand = x;
            if (x->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                operand = ctx.allocator->newTracked<LSLParenthesisExpression>(x);
                operand->setType(x->getType());
            }
            auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
            negated->setType(TYPE(LST_INTEGER));
            report.note(expr->getLoc(), "simplified " + was + " to " + render(negated));
            putInPlace(expr, negated, ctx.allocator);
            ++changes;
            return false;
        }

        // A child that is only ever true or false: `x != 0` is `x`, and
        // `!!x` is `x`.
        void condition(LSLASTNode* parent, int slot)
        {
            LSLASTNode* child = parent->getChild(slot);
            if (!child || child->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            auto*          expr  = static_cast<LSLExpression*>(child);
            LSLExpression* inner = bare(expr);
            if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getOperation() == OP_NEQ)
            {
                auto* bin = static_cast<LSLBinaryExpression*>(inner);
                int   keepSlot = -1;
                if (bin->getLHS()->getIType() == LST_INTEGER && isInteger(bin->getRHS(), 0)) keepSlot = 0;
                else if (bin->getRHS()->getIType() == LST_INTEGER && isInteger(bin->getLHS(), 0)) keepSlot = 1;
                if (keepSlot >= 0)
                {
                    const std::string was  = render(expr);
                    auto*             kept = static_cast<LSLExpression*>(bin->takeChild(keepSlot));
                    report.note(expr->getLoc(), "simplified " + was + " to " + render(kept));
                    LSLASTNode::replaceNode(expr, kept);
                    ++changes;
                    return;
                }
            }
            if (inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                LSLExpression* once = bare(static_cast<LSLUnaryExpression*>(inner)->getChildExpr());
                if (once && once->getNodeSubType() == NODE_UNARY_EXPRESSION && once->getOperation() == OP_BOOLEAN_NOT)
                {
                    LSLExpression* x = static_cast<LSLUnaryExpression*>(once)->getChildExpr();
                    if (x && x->getIType() == LST_INTEGER)
                    {
                        const std::string was = render(expr);
                        once->setChild(0, nullptr);
                        report.note(expr->getLoc(), "simplified " + was + " to " + render(x));
                        LSLASTNode::replaceNode(expr, x);
                        ++changes;
                    }
                }
            }
        }
    };

    // What can never run, and what is never used.
    class DeadCode : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION && global->getSymbol())
                {
                    mFunctions[global->getSymbol()] = static_cast<LSLGlobalFunction*>(global);
                }
            }
            script->visit(this);
            globals(script);
            states(script);
            return changes;
        }

        bool visit(LSLCompoundStatement* block) override
        {
            visitChildren(block);
            bool                     dead = false;
            std::vector<LSLASTNode*> going;
            const auto               go = [&](LSLASTNode* stmt, const std::string& why) {
                if (!why.empty())
                {
                    report.note(stmt->getLoc(), "removed " + render(stmt) + why);
                }
                going.push_back(stmt);
            };
            for (LSLASTNode* stmt : *block)
            {
                if (stmt->getNodeSubType() == NODE_LABEL)
                {
                    dead = false;
                    if (unusedLabel(stmt))
                    {
                        go(stmt, std::string());
                    }
                    continue;
                }
                if (dead)
                {
                    go(stmt, ", which can never run");
                    continue;
                }
                switch (stmt->getNodeSubType())
                {
                    case NODE_RETURN_STATEMENT:
                    case NODE_JUMP_STATEMENT:
                    case NODE_STATE_STATEMENT:
                        dead = true;
                        break;
                    case NODE_NOP_STATEMENT:
                        go(stmt, std::string());
                        break;
                    case NODE_COMPOUND_STATEMENT:
                        if (!stmt->hasChildren())
                        {
                            go(stmt, std::string());
                        }
                        break;
                    case NODE_EXPRESSION_STATEMENT:
                    {
                        LSLExpression* expr = static_cast<LSLExpressionStatement*>(stmt)->getExpr();
                        if (sideEffectFree(expr) || callsNothing(expr))
                        {
                            go(stmt, ", which does nothing");
                        }
                        break;
                    }
                    case NODE_DECLARATION:
                        if (unusedLocal(static_cast<LSLDeclaration*>(stmt)))
                        {
                            go(stmt, std::string());
                        }
                        break;
                    default:
                        break;
                }
            }
            for (LSLASTNode* stmt : going)
            {
                block->removeChild(stmt);
                ++changes;
            }
            return false;
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            LSLConstant* cv = stmt->getCheckExpr() ? stmt->getCheckExpr()->getConstantValue() : nullptr;
            if (!cv || cv->getNodeSubType() != NODE_INTEGER_CONSTANT)
            {
                return false;
            }
            const bool    taken  = static_cast<LSLIntegerConstant*>(cv)->getValue() != 0;
            LSLStatement* branch = taken ? stmt->getTrueBranch() : stmt->getFalseBranch();
            report.note(stmt->getLoc(), std::string("the condition of this if is always ") + (taken ? "true" : "false") + "; kept only what runs");
            replaceStatement(stmt, branch ? static_cast<LSLStatement*>(stmt->takeChild(taken ? 1 : 2)) : nullptr);
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0))
            {
                report.note(stmt->getLoc(), "removed a while loop whose condition is always false");
                replaceStatement(stmt, nullptr);
            }
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0))
            {
                report.note(stmt->getLoc(), "a do loop whose condition is always false runs once; kept its body");
                replaceStatement(stmt, static_cast<LSLStatement*>(stmt->takeChild(0)));
            }
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0))
            {
                // The initialisers still run, once.
                auto* block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
                block->setLoc(stmt->getLoc());
                LSLASTNode* init = stmt->getInitExprs();
                while (init && init->hasChildren())
                {
                    auto* expr = static_cast<LSLExpression*>(init->takeChild(0));
                    init->removeChild(init->getChild(0));
                    auto* es = ctx.allocator->newTracked<LSLExpressionStatement>(expr);
                    es->setLoc(expr->getLoc());
                    block->pushChild(es);
                }
                report.note(stmt->getLoc(), "removed a for loop whose condition is always false; its initialisers stay");
                replaceStatement(stmt, block->hasChildren() ? block : nullptr);
            }
            return false;
        }

    private:
        // A statement becomes another, or nothing: nothing is an empty
        // statement where a branch or a body needs one.
        void replaceStatement(LSLStatement* old, LSLStatement* now)
        {
            LSLASTNode* parent = old->getParent();
            if (!now)
            {
                if (parent && parent->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                {
                    parent->removeChild(old);
                    ++changes;
                    return;
                }
                now = ctx.allocator->newTracked<LSLNopStatement>();
                now->setLoc(old->getLoc());
            }
            LSLASTNode::replaceNode(old, now);
            ++changes;
        }

        // A call to a function of the script's own whose body is empty,
        // with arguments that do nothing on their own.
        bool callsNothing(LSLExpression* expr) const
        {
            if (!expr || expr->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym      = expr->getSymbol();
            auto       function = sym ? mFunctions.find(sym) : mFunctions.end();
            if (function == mFunctions.end())
            {
                return false;
            }
            LSLASTNode* body = function->second->getStatements();
            if (!body || body->getNodeSubType() != NODE_COMPOUND_STATEMENT || body->hasChildren())
            {
                return false;
            }
            for (LSLASTNode* arg : *static_cast<LSLFunctionExpression*>(expr)->getArguments())
            {
                if (!sideEffectFree(arg))
                {
                    return false;
                }
            }
            return true;
        }

        bool unusedLabel(LSLASTNode* label)
        {
            LSLSymbol* sym = label->getSymbol();
            if (!sym || sym->getReferences() > 1)
            {
                return false;
            }
            report.note(label->getLoc(), "removed the label " + render(label) + ", which nothing jumps to");
            return true;
        }

        bool unusedLocal(LSLDeclaration* decl)
        {
            LSLSymbol* sym = decl->getSymbol();
            if (!sym || sym->getReferences() != 1 || sym->getAssignments() != 0)
            {
                return false;
            }
            LSLASTNode* init = decl->getInitializer();
            if (init && !sideEffectFree(init))
            {
                return false;
            }
            forget(decl, sym);
            report.note(decl->getLoc(), "removed the unused local " + std::string(sym->getName()));
            return true;
        }

        static void forget(LSLASTNode* node, LSLSymbol* sym)
        {
            for (LSLASTNode* up = node; up; up = up->getParent())
            {
                if (up->getSymbolTable() && up->getSymbolTable()->remove(sym))
                {
                    return;
                }
            }
        }

        void globals(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* global : *script->getGlobals())
            {
                LSLSymbol* sym = global->getSymbol();
                if (!sym || sym->getReferences() != 1)
                {
                    continue;
                }
                if (global->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    report.note(global->getLoc(), "removed the unused global " + std::string(sym->getName()));
                    going.push_back(global);
                }
                else if (global->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    report.note(global->getLoc(), "removed the unused function " + std::string(sym->getName()));
                    going.push_back(global);
                }
            }
            for (LSLASTNode* global : going)
            {
                script->getSymbolTable()->remove(global->getSymbol());
                script->getGlobals()->removeChild(global);
                ++changes;
            }
        }

        std::unordered_map<LSLSymbol*, LSLGlobalFunction*> mFunctions;

        void states(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* state : *script->getStates())
            {
                LSLSymbol* sym = state->getSymbol();
                if (!sym || sym->getReferences() != 1 || !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                report.note(state->getLoc(), "removed the state " + std::string(sym->getName()) + ", which nothing enters");
                going.push_back(state);
            }
            for (LSLASTNode* state : going)
            {
                script->getSymbolTable()->remove(state->getSymbol());
                script->getStates()->removeChild(state);
                ++changes;
            }
        }
    };

    // ---- names -------------------------------------------------------------------------------

    class TableCollector : public ASTVisitor
    {
    public:
        bool visit(LSLASTNode* node) override
        {
            if (LSLSymbolTable* table = node->getSymbolTable())
            {
                if (mSeen.insert(table).second)
                {
                    tables.push_back(table);
                }
            }
            return true;
        }
        std::vector<LSLSymbolTable*> tables;

    private:
        std::set<LSLSymbolTable*> mSeen;
    };

    const char* const KEYWORDS[] = { "default", "state",  "event",   "jump",     "return", "if",    "else",       "for",  "do",
                                     "while",   "print",  "integer", "float",    "string", "key",   "vector",     "rotation",
                                     "quaternion", "list", "TRUE",   "FALSE",    "inline", "break", "continue",   "switch", "case", nullptr };

    // Every name the script owns, shortest first for the most used.
    void shrink(LSLScript* script, ScriptContext& context, ScriptAllocator& allocator, Report& report, ALLSLOptimizer::Result& result)
    {
        TableCollector collector;
        script->visit(&collector);
        std::vector<LSLSymbol*> symbols;
        for (LSLSymbolTable* table : collector.tables)
        {
            if (table->getTableType() == SYMTAB_BUILTINS)
            {
                continue;
            }
            for (auto& entry : table->getMap())
            {
                LSLSymbol* sym = entry.second;
                if (sym->getSymbolType() == SYM_EVENT || sym->getSubType() == SYM_BUILTIN)
                {
                    continue;
                }
                if (sym->getSymbolType() == SYM_STATE && !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                symbols.push_back(sym);
            }
        }
        std::stable_sort(symbols.begin(), symbols.end(), [](LSLSymbol* a, LSLSymbol* b) { return a->getReferences() > b->getReferences(); });
        const std::string first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
        const std::string rest  = first + "0123456789";
        std::vector<int>  digits;
        const auto        next = [&]() {
            // Counts up in a mixed radix: the first digit over `first`,
            // the rest over `rest`.
            std::string name;
            while (true)
            {
                if (digits.empty())
                {
                    digits.push_back(0);
                }
                else
                {
                    size_t i = digits.size();
                    while (i > 0)
                    {
                        --i;
                        const int radix = i == 0 ? static_cast<int>(first.size()) : static_cast<int>(rest.size());
                        if (++digits[i] < radix)
                        {
                            break;
                        }
                        digits[i] = 0;
                        if (i == 0)
                        {
                            digits.insert(digits.begin(), 0);
                            break;
                        }
                    }
                }
                name.clear();
                for (size_t i = 0; i < digits.size(); ++i)
                {
                    name += (i == 0 ? first : rest)[digits[i]];
                }
                bool keyword = false;
                for (const char* const* k = KEYWORDS; *k; ++k)
                {
                    keyword = keyword || name == *k;
                }
                if (keyword || (context.builtins && context.builtins->lookup(name.c_str(), SYM_ANY)))
                {
                    continue;
                }
                return name;
            }
        };
        for (LSLSymbol* sym : symbols)
        {
            const std::string name = next();
            sym->setMangledName(allocator.copyStr(name.c_str()));
            result.renamed.emplace(sym->getName(), name);
            report.note(sym->getLoc(), std::string("renamed the ") + LSLSymbol::getTypeName(sym->getSymbolType()) + " " + sym->getName() + " to " + name);
        }
    }

    // ---- the printer ----------------------------------------------------------------------------

    // Tailslide's printer with floats that read back exactly, integers
    // where a float may be one, and a note of where every name, number
    // and statement came from.
    class Printer : public PrettyPrintVisitor
    {
    public:
        Printer(const PrettyPrintOpts& opts, const ALLSLOptimizer::Options& options) : PrettyPrintVisitor(opts), mOptions(options) {}

        bool visit(LSLIdentifier* id) override { return marked(id, [&] { return PrettyPrintVisitor::visit(id); }); }
        bool visit(LSLIntegerConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
        bool visit(LSLStringConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
        bool visit(LSLKeyConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
        bool visit(LSLFloatConstant* c) override
        {
            return marked(c, [&] {
                mStream << number(c->getValue(), integerAllowed(c));
                return false;
            });
        }
        bool visit(LSLVectorConstant* c) override
        {
            return marked(c, [&] {
                const Vector3* v = c->getValue();
                mStream << '<' << number(v->x, true) << ", " << number(v->y, true) << ", " << number(v->z, true) << '>';
                return false;
            });
        }
        bool visit(LSLQuaternionConstant* c) override
        {
            return marked(c, [&] {
                const Quaternion* q = c->getValue();
                mStream << '<' << number(q->x, true) << ", " << number(q->y, true) << ", " << number(q->z, true) << ", " << number(q->s, true) << '>';
                return false;
            });
        }
        bool visit(LSLGlobalVariable* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLGlobalFunction* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLState* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLEventHandler* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLExpressionStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLDeclaration* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLIfStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLWhileStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLDoStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLForStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLReturnStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLJumpStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLLabel* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLStateStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }

        // The map, once everything is printed.
        ALSourceMap map(const std::string& name)
        {
            ALSourceMap       out;
            out.addFile(name, std::string());
            const std::string text = mStream.str();
            std::sort(mMarks.begin(), mMarks.end(), [](const Mark& a, const Mark& b) { return a.offset < b.offset; });
            S32    line = 0, column = 0;
            size_t pos  = 0;
            for (const Mark& mark : mMarks)
            {
                for (; pos < mark.offset && pos < text.size(); ++pos)
                {
                    if (text[pos] == '\n')
                    {
                        ++line;
                        column = 0;
                    }
                    else
                    {
                        ++column;
                    }
                }
                ALSourceMap::Segment s;
                s.outLine   = line;
                s.outColumn = column;
                s.length    = static_cast<S32>(mark.length);
                s.line      = zeroBased(mark.loc.first_line);
                s.column    = zeroBased(mark.loc.first_column);
                s.verbatim  = false;
                out.add(s);
            }
            out.finish();
            return out;
        }

    private:
        struct Mark
        {
            size_t  offset = 0;
            size_t  length = 0;
            Tailslide::YYLTYPE loc{};
        };

        template <class F> bool marked(LSLASTNode* node, F print)
        {
            const size_t start = static_cast<size_t>(mStream.tellp());
            const bool   r     = print();
            const size_t end   = static_cast<size_t>(mStream.tellp());
            if (node->getLoc()->first_line > 0)
            {
                mMarks.push_back(Mark{ start, end - start, *node->getLoc() });
            }
            return r;
        }

        template <class F> bool at(LSLASTNode* node, F print)
        {
            if (node->getLoc()->first_line > 0)
            {
                mMarks.push_back(Mark{ static_cast<size_t>(mStream.tellp()), 0, *node->getLoc() });
            }
            return print();
        }

        std::string number(double v, bool asInteger)
        {
            if (mOptions.optfloats && asInteger && integral(v))
            {
                return std::to_string(static_cast<long long>(v));
            }
            return floatText(v, mOptions.target == ALLSLOptimizer::Target::Luau);
        }

        // Whether an integer literal may stand where this float does: where
        // LSL converts one on its own, and the operation does not change
        // with it.
        static bool integerAllowed(LSLASTNode* c)
        {
            LSLASTNode* node   = c->getParent();
            LSLASTNode* parent = node ? node->getParent() : nullptr;
            // Up through parentheses and a leading minus.
            while (parent && parent->getNodeType() == NODE_EXPRESSION &&
                   (parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION ||
                    (parent->getNodeSubType() == NODE_UNARY_EXPRESSION && static_cast<LSLExpression*>(parent)->getOperation() == OP_MINUS)))
            {
                node   = parent;
                parent = parent->getParent();
            }
            if (!parent || !node)
            {
                return false;
            }
            const int slot = node->getParentSlot();
            switch (parent->getNodeType())
            {
                case NODE_GLOBAL_VARIABLE:
                    return slot == 1 && parent->getChild(0)->getIType() == LST_FLOATINGPOINT;
                case NODE_STATEMENT:
                    if (parent->getNodeSubType() == NODE_DECLARATION)
                    {
                        return slot == 1 && parent->getChild(0)->getIType() == LST_FLOATINGPOINT;
                    }
                    if (parent->getNodeSubType() == NODE_RETURN_STATEMENT)
                    {
                        for (LSLASTNode* up = parent; up; up = up->getParent())
                        {
                            if (up->getNodeType() == NODE_GLOBAL_FUNCTION)
                            {
                                return up->getChild(0)->getIType() == LST_FLOATINGPOINT;
                            }
                        }
                    }
                    return false;
                case NODE_AST_NODE_LIST:
                {
                    // An argument: the parameter's type decides.
                    LSLASTNode* call = parent->getParent();
                    if (!call || call->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                    {
                        return false;
                    }
                    LSLSymbol* sym = call->getSymbol();
                    if (!sym || !sym->getFunctionDecl())
                    {
                        return false;
                    }
                    LSLASTNode* param = sym->getFunctionDecl()->getChild(slot);
                    return param && param->getIType() == LST_FLOATINGPOINT;
                }
                case NODE_EXPRESSION:
                    break;
                default:
                    return false;
            }
            auto* expr = static_cast<LSLExpression*>(parent);
            switch (expr->getNodeSubType())
            {
                case NODE_VECTOR_EXPRESSION:
                case NODE_QUATERNION_EXPRESSION:
                    return true;
                case NODE_BINARY_EXPRESSION:
                {
                    const LSLOperator op = expr->getOperation();
                    LSLASTNode*       other = expr->getChild(slot == 0 ? 1 : 0);
                    if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN)
                    {
                        return slot == 1 && expr->getChild(0)->getIType() == LST_FLOATINGPOINT;
                    }
                    if (op == OP_MOD || op == OP_MOD_ASSIGN)
                    {
                        return false;
                    }
                    // The other operand keeps the operation in floats.
                    return other && (other->getIType() == LST_FLOATINGPOINT || other->getIType() == LST_VECTOR || other->getIType() == LST_QUATERNION);
                }
                default:
                    return false;
            }
        }

        const ALLSLOptimizer::Options& mOptions;
        std::vector<Mark>              mMarks;
    };

    void identityMap(const std::string& text, const std::string& name, ALSourceMap& map)
    {
        map = ALSourceMap();
        map.addFile(name, std::string());
        S32    line  = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
            {
                end = text.size();
            }
            ALSourceMap::Segment s;
            s.outLine  = line;
            s.line     = line;
            s.length   = static_cast<S32>(end - start);
            s.verbatim = true;
            map.add(s);
            ++line;
            start = end + 1;
        }
        map.finish();
    }

    void collectMessages(Logger& logger, ALScriptProblems& problems)
    {
        for (LogMessage* message : logger.getMessages())
        {
            if (message->getType() != LOG_ERROR && message->getType() != LOG_INTERNAL_ERROR)
            {
                continue;
            }
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Error;
            p.source   = message->getError() == E_SYNTAX_ERROR || message->getError() == E_PARSER_STACK_DEPTH ? ALScriptProblem::Source::Parser
                                                                                                                   : ALScriptProblem::Source::Types;
            p.line      = zeroBased(message->getLoc()->first_line);
            p.column    = zeroBased(message->getLoc()->first_column);
            p.endLine   = zeroBased(message->getLoc()->last_line);
            p.endColumn = std::max(0, message->getLoc()->last_column);
            p.code      = std::to_string(static_cast<int>(message->getError()));
            p.message   = message->getMessage();
            problems.push_back(std::move(p));
        }
    }
} // namespace

ALLSLOptimizer::Result ALLSLOptimizer::run(std::string_view source, const Options& options)
{
    Result result;
    result.text       = std::string(source);
    result.sizeBefore = source.size();
    result.sizeAfter  = source.size();
    identityMap(result.text, std::string(), result.map);
    if (!ALLSLService::builtinsLoaded())
    {
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Error;
        p.source   = ALScriptProblem::Source::Optimizer;
        p.message  = "the LSL definitions are not loaded, so nothing was optimized";
        result.problems.push_back(std::move(p));
        return result;
    }

    ScopedScriptParser parser(nullptr);
    LSLScript*         script = parser.parseLSLBytes(source.data(), static_cast<int>(source.size()));
    if (!script || parser.logger.getErrors())
    {
        collectMessages(parser.logger, result.problems);
        return result;
    }
    script->collectSymbols();
    script->determineTypes();
    script->recalculateReferenceData();
    Behavior   behavior(&parser.allocator, options.addstrings, options.target);
    const auto propagate = [&]() {
        ConstantDeterminingVisitor values(&behavior, &parser.allocator);
        script->visit(&values);
    };
    propagate();
    script->finalPass();
    if (parser.logger.getErrors())
    {
        collectMessages(parser.logger, result.problems);
        return result;
    }

    Ctx ctx;
    ctx.allocator = &parser.allocator;
    ctx.target    = options.target;
    ctx.foldtabs  = options.foldtabs;
    Report report(result.problems);
    // Each pass opens the way for the others; round and round until a
    // round changes nothing.
    for (int round = 0; round < 64; ++round)
    {
        int changes = 0;
        if (options.constfold)
        {
            Folder folder(ctx, report, options);
            script->visit(&folder);
            if (folder.changes)
            {
                changes += folder.changes;
                script->recalculateReferenceData();
                propagate();
            }
        }
        if (options.constfold)
        {
            Simplifier simplifier(ctx, report, options);
            script->visit(&simplifier);
            if (simplifier.changes)
            {
                changes += simplifier.changes;
                script->recalculateReferenceData();
                propagate();
            }
        }
        if (options.dcr)
        {
            DeadCode dead(ctx, report, options);
            const int removed = dead.run(script);
            if (removed)
            {
                changes += removed;
                script->recalculateReferenceData();
                propagate();
            }
        }
        if (!changes)
        {
            break;
        }
    }
    if (options.shrinknames)
    {
        shrink(script, parser.context, parser.allocator, report, result);
    }

    PrettyPrintOpts opts{};
    opts.mangle_local_names  = options.shrinknames;
    opts.mangle_func_names   = options.shrinknames;
    opts.mangle_global_names = options.shrinknames;
    opts.show_unmangled      = false;
    Printer printer(opts, options);
    script->visit(&printer);
    result.text      = printer.mStream.str();
    result.map       = printer.map(std::string());
    result.sizeAfter = result.text.size();
    result.optimized = true;
    return result;
}

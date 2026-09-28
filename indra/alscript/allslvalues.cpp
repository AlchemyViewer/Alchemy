/**
 * @file allslvalues.cpp
 * @brief The LSL targets' arithmetic, and numbers as the optimizer writes them.
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

#include "allslvalues.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Tailslide;

ALLSLArithmetic::ALLSLArithmetic(ScriptAllocator* allocator, bool addstrings, ALLSLOptimizer::Target target)
    : TailslideOperationBehavior(allocator, true), mAddStrings(addstrings), mTarget(target)
{
}

LSLConstant* ALLSLArithmetic::operation(LSLOperator op, LSLConstant* cv, LSLConstant* other, Tailslide::YYLTYPE* lloc)
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

LSLConstant* ALLSLArithmetic::cast(LSLType* to, LSLConstant* cv, Tailslide::YYLTYPE* lloc)
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

// static
bool ALLSLArithmetic::isNumber(LSLConstant* c)
{
    return c->getIType() == LST_INTEGER || c->getIType() == LST_FLOATINGPOINT;
}

// static
double ALLSLArithmetic::asSingle(LSLConstant* c)
{
    if (c->getIType() == LST_INTEGER)
    {
        return static_cast<double>(static_cast<float>(static_cast<LSLIntegerConstant*>(c)->getValue()));
    }
    return static_cast<double>(static_cast<float>(static_cast<LSLFloatConstant*>(c)->getValue()));
}

LSLConstant* ALLSLArithmetic::integer(int v)
{
    return _mAllocator->newTracked<LSLIntegerConstant>(v);
}

LSLConstant* ALLSLArithmetic::single_(double v)
{
    const float f = static_cast<float>(v);
    if (!std::isfinite(f))
    {
        return nullptr;
    }
    return _mAllocator->newTracked<LSLFloatConstant>(static_cast<double>(f));
}

LSLConstant* ALLSLArithmetic::rounded(LSLConstant* c)
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

namespace ALLSLValues
{
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

    bool integral(double v)
    {
        return std::isfinite(v) && std::floor(v) == v && !std::signbit(v) && v < 2147483648.0;
    }
}

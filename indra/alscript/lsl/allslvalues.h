/**
 * @file allslvalues.h
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

#pragma once

#include "allsloptimizer.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>

#include <optional>
#include <string>

// Tailslide folds in double; the LSL VMs work in single, and integers
// wrap. Every result goes through here so that a fold is what the VM
// would have given, or nothing.
class ALLSLArithmetic : public Tailslide::TailslideOperationBehavior
{
public:
    ALLSLArithmetic(Tailslide::ScriptAllocator* allocator, bool addstrings, ALLSLOptimizer::Target target);

    Tailslide::LSLConstant* operation(Tailslide::LSLOperator op, Tailslide::LSLConstant* cv, Tailslide::LSLConstant* other, Tailslide::YYLTYPE* lloc) override;
    Tailslide::LSLConstant* cast(Tailslide::LSLType* to, Tailslide::LSLConstant* cv, Tailslide::YYLTYPE* lloc) override;

private:
    static bool   isNumber(Tailslide::LSLConstant* c);
    static double asSingle(Tailslide::LSLConstant* c);

    Tailslide::LSLConstant* integer(int v);
    Tailslide::LSLConstant* single_(double v);
    Tailslide::LSLConstant* rounded(Tailslide::LSLConstant* c);

    bool                   mAddStrings;
    ALLSLOptimizer::Target mTarget;
};

// Numbers as the optimizer writes them.
namespace ALLSLValues
{
    // The shortest text that reads back as the same single (or, for Luau,
    // double), with a dot so that it stays a float literal; a whole number
    // under a billion as its digits; infinity as a literal past any
    // float's range. Not a NaN, which no literal is.
    std::string floatText(double v, bool wide);

    // An integral value that an integer literal can stand for: not
    // negative zero, which keeps its sign, and within the integer's range.
    bool integral(double v);

    // A value written as a literal LSL takes as a global's value: floats
    // as floats, the shortest that reads back the same (`wide`, Luau's
    // doubles, or singles). Nothing where no literal is the value: a float
    // that is not finite, a string with a tab, which the compiler would
    // turn into spaces, or a list holding a key, which a literal would make
    // a string.
    std::optional<std::string> literal(Tailslide::LSLConstant* value, bool wide);
}

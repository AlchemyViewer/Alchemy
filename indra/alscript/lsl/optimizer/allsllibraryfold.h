/**
 * @file allsllibraryfold.h
 * @brief The LSL library, answered for constant arguments.
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

#include "allsloptimizerpass.h"

#include <vector>

namespace ALLSLPasses
{
    typedef std::vector<LSLConstant*> Args;

    // A pure library function's answer to constant arguments, as the
    // target's VM gives it, or nothing where it is the VM's to answer: a
    // list's getters, the maths, the strings, the parsing, JSON, base64,
    // the rotations -- each only where every VM agrees.
    LSLConstant* evaluate(Ctx& ctx, const char* name, const Args& args);
}

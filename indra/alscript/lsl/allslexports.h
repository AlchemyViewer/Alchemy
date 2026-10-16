/**
 * @file allslexports.h
 * @brief What an LSL include declares for whoever includes it, read off its text.
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

#include <string>
#include <string_view>
#include <vector>

// What an LSL include declares for whoever includes it, as its text says
// it: the macros it defines, and the functions and variables it declares
// at the top. An include is a fragment -- no state, often no more than a
// few functions, macros all through it -- which Tailslide will not parse
// alone, so it is read by the preprocessor's own tokens:
//
//   #define CHANNEL -42                 -- CHANNEL
//   #define say(x) llOwnerSay(x)        -- say
//   integer gCount = 0;                 -- gCount
//   list gSeen;                         -- gSeen
//   string greet(string name) { ... }   -- greet
//   reset() { ... }                     -- reset
//   inline integer twice(integer n) ... -- twice
//
// What a state holds, and what a directive other than `#define` says, is
// no declaration. Each name once, in the order the text gives them.
namespace ALLSLExports
{
    std::vector<std::string> of(std::string_view source);
}

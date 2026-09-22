/**
 * @file alscriptmessages.h
 * @brief What the compilers and the running scripts say, read: pure, and so testable.
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

#include "llsd.h"
#include "stdtypes.h"

#include <string>
#include <string_view>
#include <vector>

// The reading of what the servers and the two VMs say: the compiler's
// error lines turned into places and words, a run-time error's header
// taken apart, and whether a script's text looks like Lua. Text in,
// values out, nothing of the viewer's in it -- the shapes change under
// us whenever a simulator or a VM is upgraded, and this is where a test
// can hold them still.
namespace ALScriptMessages
{
    // Zero-based line and column, as everything in the studio counts.
    struct Place
    {
        S32         line      = 0;
        S32         column    = 0;
        bool        hasColumn = false;
        // The compiler's own word: ERROR or WARNING.
        std::string level;
        std::string message;
    };

    // One line of what a compiler said. Luau names the chunk and a
    // one-based line; LSL gives a zero-based line and column in
    // brackets with its level between colons; anything else is an error
    // with no place.
    Place readDiagnostic(const std::string& line, bool lua);
    // Every line of them.
    std::vector<Place> readDiagnostics(const LLSD& errors, bool lua);

    // No `default` state anywhere means Lua: the same guess the legacy
    // editor made.
    bool looksLikeLua(std::string_view content);

    // How a run-time error's first line reads: the object's name and the
    // script's, before the words. False where the line is no such
    // header, which is every other thing a script says.
    struct Header
    {
        std::string object;
        std::string script;
    };
    bool readRuntimeHeader(const std::string& line, Header& out);
    // Whether a line ends with the words that mark a run-time error,
    // which is how the lines that follow are known to belong with it.
    bool endsRuntimeError(const std::string& line);

    // Where a run-time error happened, from the lines that followed its
    // header: Luau's `chunk:12: words`, LSL's `(12, 3) : ERROR : words`.
    // False where none of them says, and then the first line that is not
    // the header is the words.
    struct Location
    {
        S32         line   = -1;
        S32         column = -1;
        std::string message;
    };
    bool readRuntimeLocation(const std::vector<std::string>& lines, bool lua, Location& out);
}

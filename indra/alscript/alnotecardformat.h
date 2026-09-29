/**
 * @file alnotecardformat.h
 * @brief What a notecard's text is -- plain, JSON or settings -- how a script reads its lines, and a JSON notecard's outline.
 *
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

#include "alscriptsymbol.h"

#include "stdtypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A notecard as scripts read it: a line at a time, from line 0, each cut
// at so many bytes (llGetNotecardLine). Scripters keep data in them --
// settings as key = value, JSON, lists, words to say -- which the studio
// colours, checks and outlines as what it is.
namespace ALNotecardFormat
{
    // The most of a line llGetNotecardLine returns: its first 1024 bytes,
    // not characters.
    constexpr size_t READ_LINE_BYTES = 1024;

    // The grammar a notecard's text looks written in: "json" where it
    // opens with a brace or a bracket; "config" where most of its lines
    // that say anything are key = value (or key: value) or [sections],
    // two of them at least; "text" otherwise.
    std::string guess(std::string_view text);

    // The lines, counted from 0, longer than a script reads.
    std::vector<S32> linesPast(std::string_view text, size_t bytes = READ_LINE_BYTES);

    // A JSON text's outline: every key of every object, each nested under
    // the key it is in, and the objects and arrays in an array as [0], [1]
    // and so on. A key's span runs from its name to the end of its value;
    // its detail is the value, or {...} or [...] for one that holds more.
    // Read however far it goes: a text that is not JSON all through
    // outlines as much as it can, and one of more entries than `most` stops
    // there.
    std::vector<ALScriptOutlineEntry> outline(std::string_view text, size_t most = 2000);

    // A notecard a script names where it reads one: the string given first
    // to llGetNotecardLine, llGetNotecardLineSync or
    // llGetNumberOfNotecardLines -- or ll.GetNotecardLine and the rest in
    // SLua -- in either quote. The name, and where the string stands on
    // its line, quotes and all.
    struct Named
    {
        std::string name;
        S32         begin = 0;
        S32         end   = 0;
    };
    // The one whose string holds a column of a line, where there is one.
    std::optional<Named> namedAt(std::string_view line, S32 column);
    // Every place a script's text names a notecard of this name so, by
    // line and the string's columns.
    std::vector<ALScriptSpan> readersOf(std::string_view text, std::string_view name);
}

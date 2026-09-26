/**
 * @file alscriptstudioplaces.h
 * @brief Script Studio's places in scripts: spans as ranges, lines of a text, names, and places read back through an expansion.
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

#include "alscriptstudiodoc.h"
#include "alscriptworkspace.h"

#include <string>

// What the studio's window and its units share of places in scripts: a
// span as a range of a text, a line of a text, whether a word is a name,
// a span read back from an expansion to the file it came from, and a
// place's line as a pane's row shows it.
namespace ALScriptPlaces
{
    ALTextRange rangeOf(const ALScriptSpan& span);
    std::string lineOf(const std::string& text, S32 line);
    std::string lineOf(const ALTextDocument& text, S32 line);
    bool        isIdentifier(const std::string& text);
    // A span of an expansion as the source's, in place; the file of the
    // expansion's map it is in, or -1 where it is in none.
    S32         mapSpan(const ALSourceMap& map, ALScriptSpan& span);
    // A place's line, trimmed for a pane's row, and where the name is in
    // it.
    void        placeText(ALScriptStudioDoc::Place& place, const std::string& line);
    // What a loaded script's author wrote: the source out of the envelope
    // where one wrapped it, the text as it came otherwise, and nothing
    // where it could not be read.
    std::string sourceOf(const ALScriptWorkspace::Loaded& loaded);
}

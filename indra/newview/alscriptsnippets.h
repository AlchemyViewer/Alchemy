/**
 * @file alscriptsnippets.h
 * @brief The snippets Script Studio offers: the viewer's, and the scripter's own.
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
#include <vector>

// What completion and Insert > Snippet offer: the viewer's snippets, from
// app_settings/snippets, and the scripter's own, from the same folder under
// the settings folder, which the preferences' Snippets tab edits and a
// scripter may also edit as XML. Each file is an LLSD array of maps with a
// name, the prefix completion offers it under, a line of detail, and the
// body, where ${1:text}, ${2} and $1 are the places Tab goes through and $0
// is where the caret ends.
namespace ALScriptSnippets
{
    struct Snippet
    {
        std::string name;
        std::string prefix;
        std::string detail;
        std::string body;
        // The viewer's, which a scripter copies to change.
        bool        builtin = false;
    };

    // Every snippet of a language, the viewer's then the scripter's; read
    // once, and again after either file changes.
    const std::vector<Snippet>& all(bool lua);
    // The scripter's own alone, as their file holds them.
    std::vector<Snippet>        own(bool lua);
    // The scripter's own written back, and offered from then on. False
    // where the file could not be written.
    bool                        saveOwn(bool lua, const std::vector<Snippet>& snippets);

    // The same, by file: the snippets a file holds, added to `out` --
    // false where there is a file and it does not read as snippets; and
    // snippets written to a file, one there already that does not read
    // as snippets kept beside it, as its name with `.unreadable` after,
    // rather than written over.
    bool readFrom(const std::string& file, bool builtin, std::vector<Snippet>& out);
    bool writeTo(const std::string& file, const std::vector<Snippet>& snippets);

    // Where the scripter's own are kept.
    std::string path(bool lua);
    // The file's text as it stands, or nothing where there is no file; and
    // put back as it was -- a file that was not there taken away -- which
    // is what a Cancel does.
    std::string fileText(bool lua, bool& exists);
    void        restoreFileText(bool lua, const std::string& text, bool existed);
    // Read again at the next ask: a file changed.
    void        forget(bool lua);
}

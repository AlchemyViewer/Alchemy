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
// app_settings/snippets; those of a notecard the scripter follows, as it
// stands; and the scripter's own, from the same folder under the settings
// folder, which the preferences' Snippets tab edits and a scripter may also
// edit as XML. Each file is an LLSD array of maps with a
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
        // The followed notecard's, which a scripter copies to change, or
        // changes in the notecard.
        bool        followed = false;
    };

    // Every snippet of a language, the viewer's, the followed notecard's,
    // then the scripter's; read once, and again after either file or the
    // notecard changes.
    const std::vector<Snippet>& all(bool lua);
    // The notecard followed, by its text, its snippets taken in place of
    // those it had -- each in the language it says, one saying none in
    // both; none with empty text. False, with none taken, where the text
    // is not snippets.
    bool                        follow(const std::string& notecard_text);
    // The followed notecard's alone.
    const std::vector<Snippet>& followed(bool lua);
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

    // Snippets as a notecard carries them, to keep in inventory or give to
    // someone: the file's array, each entry saying the language it is for,
    // so that one notecard carries both. And read back, each into the
    // language it says -- one saying none into `lua`'s -- keys it does not
    // know passed over; false, with nothing added, where the text is not
    // snippets.
    std::string notecardText(const std::vector<Snippet>& lsl, const std::vector<Snippet>& slua);
    bool        readNotecard(const std::string& text, bool lua, std::vector<Snippet>& lsl, std::vector<Snippet>& slua);

    // Snippets brought in beside the scripter's own: one with the name and
    // the body of one there already is left out; one whose name is taken
    // by another body comes in as "name (2)", or the first number free --
    // left out too where a numbered one holds its body.
    struct Merged
    {
        size_t added   = 0;
        size_t renamed = 0;
        size_t skipped = 0;
    };
    Merged merge(std::vector<Snippet>& own, const std::vector<Snippet>& incoming);

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

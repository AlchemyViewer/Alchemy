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

#include "alscriptanalysis.h"
#include "alscriptstudiodoc.h"
#include "alscripttypes.h"

#include <memory>
#include <string>
#include <vector>

// What the studio's window and its units share of places in scripts: a
// span as a range of a text, a line of a text, whether a word is a name,
// a span read back from an expansion to the file it came from, a place's
// line as a pane's row shows it, an outline entry as a value, and where a
// name is declared.
namespace ALScriptPlaces
{
    // A span of a script -- a problem's, an edit's, a symbol's -- as a range
    // of its text: the one way one becomes the other.
    ALTextRange rangeOf(const ALScriptSpan& span);
    // Whether a span holds a place, its ends included; and whether it holds
    // the whole of another.
    bool        holds(const ALScriptSpan& span, const ALTextPos& pos);
    bool        within(const ALScriptSpan& inner, const ALScriptSpan& outer);
    std::string lineOf(const std::string& text, S32 line);
    std::string lineOf(const ALTextDocument& text, S32 line);
    // A text's lines, found once, for the many places a name has in one
    // text -- an include, an expansion -- rather than each read by walking
    // the text from its top: a text open in a tab, read where it stands for
    // as long as the lines are asked, or a copy held apart.
    class Lines
    {
    public:
        Lines() = default;
        explicit Lines(const ALTextDocument* open) : mOpen(open) {}
        explicit Lines(std::shared_ptr<const std::string> held);
        // A text read where it is, for as long as the lines are asked.
        explicit Lines(const std::string& text);
        // Whether it has a line: none past the end, or with no text.
        bool        has(S32 line) const;
        std::string line(S32 line) const;

    private:
        void index();

        const ALTextDocument*              mOpen = nullptr;
        std::shared_ptr<const std::string> mHeld;
        const std::string*                 mText = nullptr;
        // Where each of the text's lines starts.
        std::vector<size_t>                mStarts;
    };
    bool        isIdentifier(const std::string& text);
    // The symbols of an outline a place is in, the outermost first: which
    // entry at each depth holds it, within the one before. At a symbol's
    // very end is still in it; where two meet, the later.
    std::vector<size_t> pathAt(const std::vector<ALScriptOutlineEntry>& outline, const ALTextPos& at);
    // A span of an expansion as the source's, in place; the file of the
    // expansion's map it is in, or -1 where it is in none.
    S32         mapSpan(const ALSourceMap& map, ALScriptSpan& span);
    // A place's line, trimmed for a pane's row, and where the name is in
    // it.
    void        placeText(ALScriptStudioDoc::Place& place, const std::string& line);
    // What a loaded script's author wrote: the source out of the envelope
    // where one wrapped it, the text as it came otherwise, and nothing
    // where it could not be read.
    std::string sourceOf(const ALScriptLoaded& loaded);
    // An outline entry as a picker's value, and back: by where it was and
    // what it is called, so that an outline made again while a list is up
    // -- a check answering -- still finds it, or nothing.
    constexpr size_t NONE = static_cast<size_t>(-1);
    std::string      outlineValue(const ALScriptStudioDoc& doc, size_t index);
    size_t           outlineEntryOf(const ALScriptStudioDoc& doc, const std::string& value);
    // Where the analyzer said a name at a place is declared, back in the
    // source where the preprocessor made what it read: a line and column
    // of the script, or of the include it is in; none where it said none
    // or the place made no text of the source.
    struct Declared
    {
        // As a link's value: where to go.
        LLSD        value() const;
        S32         line   = -1;
        S32         column = -1;
        // The include it is in, by identity and by name; empty for the
        // script itself.
        std::string path;
        std::string name;
    };
    Declared declaredOf(const ALScriptStudioDoc& doc, const ALScriptAnalysis::Result& result, bool preprocessed);
}

/**
 * @file altextdocument.h
 * @brief Lines of UTF-8 with a version, for the text view.
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

#include "stdtypes.h"

#include <boost/signals2.hpp>

#include <string>
#include <string_view>
#include <vector>

// A place in a document: a line, and a byte offset into it. Zero-based, and
// the column counts bytes of UTF-8 rather than characters -- the one
// convention every boundary converts to.
// A position the document hands out is on a grapheme boundary; one a caller
// makes up may not be, and clamp() puts it right.
struct ALTextPos
{
    S32 line = 0;
    S32 column = 0;

    ALTextPos() = default;
    ALTextPos(S32 line_in, S32 column_in) : line(line_in), column(column_in) {}

    friend bool operator==(const ALTextPos& a, const ALTextPos& b) { return a.line == b.line && a.column == b.column; }
    friend bool operator!=(const ALTextPos& a, const ALTextPos& b) { return !(a == b); }
    friend bool operator<(const ALTextPos& a, const ALTextPos& b)
    {
        return a.line < b.line || (a.line == b.line && a.column < b.column);
    }
    friend bool operator>(const ALTextPos& a, const ALTextPos& b) { return b < a; }
    friend bool operator<=(const ALTextPos& a, const ALTextPos& b) { return !(b < a); }
    friend bool operator>=(const ALTextPos& a, const ALTextPos& b) { return !(a < b); }
};

// A span between two positions, half-open. A selection is one of these with
// the caret at either end, so begin may sit after end; normalised() puts
// them in order for anything that reads the text.
struct ALTextRange
{
    ALTextPos begin;
    ALTextPos end;

    ALTextRange() = default;
    ALTextRange(const ALTextPos& begin_in, const ALTextPos& end_in) : begin(begin_in), end(end_in) {}

    bool empty() const { return begin == end; }
    ALTextRange normalised() const { return end < begin ? ALTextRange(end, begin) : *this; }
    bool contains(const ALTextPos& pos) const
    {
        const ALTextRange ordered = normalised();
        return ordered.begin <= pos && pos < ordered.end;
    }

    friend bool operator==(const ALTextRange& a, const ALTextRange& b) { return a.begin == b.begin && a.end == b.end; }
    friend bool operator!=(const ALTextRange& a, const ALTextRange& b) { return !(a == b); }
};

// The text behind a text view: lines of UTF-8 without their line endings,
// and a version that moves on every change. Every edit is one replacement
// of a range by a string, reported to whoever is listening as the Edit it
// was, which is also all an undo stack needs to keep. Line endings arrive as
// whatever they are and leave as LF.
//
// Storage, positions and search live here; layout, style and drawing do
// not. What this knows about characters it asks llstring: graphemes for
// stepping and clamping, words for the caret's word motions.
class ALTextDocument
{
public:
    // One change, as it was made. The inverse is the change that puts it
    // back, and applying either is replace().
    struct Edit
    {
        // What was replaced, in the text as it was before.
        ALTextRange range;
        std::string removed;
        std::string inserted;

        // Where the inserted text ends, in the text as it is after.
        ALTextPos   endAfter() const;
        ALTextRange rangeAfter() const { return ALTextRange(range.begin, endAfter()); }
        Edit        inverse() const;
        bool        nothing() const { return removed.empty() && inserted.empty(); }

        // What the edit does to positions kept beside the text. A position
        // at or after the end of what was replaced, moved along with the
        // text; and a range through the edit: false where the edit took
        // some of it or landed inside it, else the range moved along.
        // Text put right before a range pushes it along; text put right
        // after it is not it.
        ALTextPos slidPast(const ALTextPos& pos) const;
        bool      slide(ALTextRange& range) const;
        // A range the edit may land inside -- a stretch a search is held
        // to -- which grows and shrinks with what is done within it rather
        // than going: a position before the edit stays, one after it moves
        // along, and one inside what was replaced goes to its start, for the
        // range's start, or past what went in, for its end. Text put right
        // at the range's start is within it; right at its end, not.
        ALTextRange stretched(const ALTextRange& range) const;
    };

    typedef boost::signals2::signal<void(const Edit&)> changed_signal_t;

    ALTextDocument();
    explicit ALTextDocument(std::string_view text);

    // --- the text ------------------------------------------------------------

    // The whole text, as one edit that replaces everything.
    Edit        setText(std::string_view text);
    std::string text() const;
    std::string text(const ALTextRange& range) const;
    // The whole text kept from one asking to the next while nothing
    // changes, for whoever reads it whole often -- a search that may
    // cross lines, say -- with where each line starts in it. Made again
    // on the first asking after an edit.
    const std::string&         wholeText() const;
    const std::vector<size_t>& lineStarts() const;

    S32                lineCount() const { return static_cast<S32>(mLines.size()); }
    const std::string& line(S32 index) const;
    S32                lineLength(S32 index) const;
    // The bytes of text(), line endings counted.
    size_t             byteCount() const;
    bool               empty() const { return mLines.size() == 1 && mLines.front().empty(); }
    U32                version() const { return mVersion; }

    // --- edits ---------------------------------------------------------------

    // The range is clamped to the text but not to grapheme boundaries: an
    // edit may cut wherever it was asked to. An edit that changes nothing --
    // nothing for nothing, or a stretch for the same text -- is answered as
    // nothing, without moving the version or telling anyone.
    Edit replace(ALTextRange range, std::string_view text);
    Edit insert(const ALTextPos& at, std::string_view text) { return replace(ALTextRange(at, at), text); }
    Edit remove(const ALTextRange& range) { return replace(range, std::string_view()); }

    // A log or a chat history: text arriving at the end, and the oldest
    // lines let go of from the front.
    Edit append(std::string_view text);
    Edit removeFirstLines(S32 count);

    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    // --- positions -----------------------------------------------------------

    ALTextPos start() const { return ALTextPos(); }
    ALTextPos end() const;
    ALTextPos lineStart(S32 line) const;
    ALTextPos lineEnd(S32 line) const;

    // Into the text, and back onto the grapheme boundary at or before.
    ALTextPos clamp(ALTextPos pos) const;

    // A grapheme along, across line ends; the same position at either end
    // of the text.
    ALTextPos nextCluster(ALTextPos pos) const;
    ALTextPos prevCluster(ALTextPos pos) const;

    // A word along, as a caret moves: to the end of the word under or after
    // the position going forward, to the start going back, and across a
    // line end where the line has nothing more.
    ALTextPos   nextWord(ALTextPos pos) const;
    ALTextPos   prevWord(ALTextPos pos) const;
    ALTextRange wordAt(ALTextPos pos) const;

    // A byte offset into text(), and back.
    size_t    offsetOf(ALTextPos pos) const;
    ALTextPos posAt(size_t offset) const;

    // The column a reader counts: one per grapheme, with a tab reaching
    // the next stop. What the status line shows, and what a caret keeps
    // when it moves between lines.
    S32       displayColumn(ALTextPos pos, S32 tab_width) const;
    ALTextPos posAtDisplayColumn(S32 line, S32 display_column, S32 tab_width) const;

    // Finding is ALTextSearch's, which every find in the studio goes through.

private:
    // Into the text, byte for byte, with no regard for graphemes.
    ALTextPos   clampBytes(ALTextPos pos) const;
    ALTextRange clampBytes(const ALTextRange& range) const;

    std::vector<std::string> mLines;
    U32                      mVersion = 0;
    changed_signal_t         mChanged;
    // The whole text and its line starts as of a version; good while
    // the version is the current one.
    mutable std::string         mWhole;
    mutable std::vector<size_t> mLineStarts;
    mutable U32                 mWholeVersion = 0;
    mutable bool                mWholeValid   = false;
};

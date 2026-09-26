/**
 * @file altextdocument.cpp
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

#include "linden_common.h"

#include "altextdocument.h"

#include "llstring.h"

#include <algorithm>

namespace
{
    const std::string EMPTY_LINE;

    // Lines out of text, whatever its line endings: CRLF and a lone CR read
    // as LF.
    void splitLines(std::string_view text, std::vector<std::string>& out)
    {
        out.clear();
        std::string current;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '\r')
            {
                out.push_back(std::move(current));
                current.clear();
                if (i + 1 < text.size() && text[i + 1] == '\n')
                {
                    ++i;
                }
            }
            else if (c == '\n')
            {
                out.push_back(std::move(current));
                current.clear();
            }
            else
            {
                current.push_back(c);
            }
        }
        out.push_back(std::move(current));
    }

    std::string joinLines(const std::vector<std::string>& lines)
    {
        size_t size = lines.size();
        for (const std::string& line : lines)
        {
            size += line.size();
        }
        std::string out;
        out.reserve(size);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i)
            {
                out.push_back('\n');
            }
            out += lines[i];
        }
        return out;
    }
}

// --- Edit --------------------------------------------------------------------

ALTextPos ALTextDocument::Edit::endAfter() const
{
    const size_t last_break = inserted.rfind('\n');
    if (last_break == std::string::npos)
    {
        return ALTextPos(range.begin.line, range.begin.column + static_cast<S32>(inserted.size()));
    }
    const S32 breaks = static_cast<S32>(std::count(inserted.begin(), inserted.end(), '\n'));
    return ALTextPos(range.begin.line + breaks, static_cast<S32>(inserted.size() - last_break - 1));
}

ALTextDocument::Edit ALTextDocument::Edit::inverse() const
{
    Edit back;
    back.range    = rangeAfter();
    back.removed  = inserted;
    back.inserted = removed;
    return back;
}

ALTextPos ALTextDocument::Edit::slidPast(const ALTextPos& pos) const
{
    const ALTextRange removed   = range.normalised();
    const ALTextPos   end_after = endAfter();
    if (pos.line == removed.end.line)
    {
        return ALTextPos(end_after.line, end_after.column + (pos.column - removed.end.column));
    }
    return ALTextPos(pos.line + (end_after.line - removed.end.line), pos.column);
}

ALTextRange ALTextDocument::Edit::stretched(const ALTextRange& range_in) const
{
    const ALTextRange removed = range.normalised();
    const ALTextRange r       = range_in.normalised();
    const auto        moved   = [&](const ALTextPos& pos, bool is_end) {
        if (pos <= removed.begin)
        {
            return pos;
        }
        if (pos >= removed.end)
        {
            return slidPast(pos);
        }
        return is_end ? endAfter() : removed.begin;
    };
    return ALTextRange(moved(r.begin, false), moved(r.end, true));
}

bool ALTextDocument::Edit::slide(ALTextRange& range_in) const
{
    const ALTextRange removed = range.normalised();
    ALTextRange       r       = range_in.normalised();
    const bool        cut     = removed.empty() ? (r.begin < removed.begin && removed.begin < r.end)
                                                : (r.begin < removed.end && removed.begin < r.end);
    if (cut)
    {
        return false;
    }
    if (removed.end <= r.begin)
    {
        r.begin = slidPast(r.begin);
        r.end   = slidPast(r.end);
    }
    range_in = r;
    return true;
}

// --- the text ----------------------------------------------------------------

ALTextDocument::ALTextDocument()
:   mLines(1)
{
}

ALTextDocument::ALTextDocument(std::string_view text)
{
    splitLines(text, mLines);
}

ALTextDocument::Edit ALTextDocument::setText(std::string_view text)
{
    return replace(ALTextRange(start(), end()), text);
}

std::string ALTextDocument::text() const
{
    return joinLines(mLines);
}

const std::string& ALTextDocument::wholeText() const
{
    if (!mWholeValid || mWholeVersion != mVersion)
    {
        mWhole.clear();
        mLineStarts.clear();
        mLineStarts.reserve(mLines.size());
        for (size_t l = 0; l < mLines.size(); ++l)
        {
            if (l > 0)
            {
                mWhole += '\n';
            }
            mLineStarts.push_back(mWhole.size());
            mWhole += mLines[l];
        }
        mWholeVersion = mVersion;
        mWholeValid   = true;
    }
    return mWhole;
}

const std::vector<size_t>& ALTextDocument::lineStarts() const
{
    wholeText();
    return mLineStarts;
}

std::string ALTextDocument::text(const ALTextRange& range_in) const
{
    const ALTextRange range = clampBytes(range_in.normalised());
    if (range.begin.line == range.end.line)
    {
        return mLines[range.begin.line].substr(range.begin.column, range.end.column - range.begin.column);
    }
    std::string out = mLines[range.begin.line].substr(range.begin.column);
    for (S32 l = range.begin.line + 1; l < range.end.line; ++l)
    {
        out.push_back('\n');
        out += mLines[l];
    }
    out.push_back('\n');
    out.append(mLines[range.end.line], 0, range.end.column);
    return out;
}

const std::string& ALTextDocument::line(S32 index) const
{
    if (index < 0 || index >= lineCount())
    {
        return EMPTY_LINE;
    }
    return mLines[index];
}

S32 ALTextDocument::lineLength(S32 index) const
{
    return static_cast<S32>(line(index).size());
}

size_t ALTextDocument::byteCount() const
{
    size_t size = mLines.size() - 1;
    for (const std::string& l : mLines)
    {
        size += l.size();
    }
    return size;
}

// --- edits -------------------------------------------------------------------

ALTextDocument::Edit ALTextDocument::replace(ALTextRange range, std::string_view text)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    range = clampBytes(range.normalised());

    Edit edit;
    edit.range   = range;
    edit.removed = this->text(range);

    // What is put in, its line endings as LF: as it came where it has
    // no CR, else joined again from its lines.
    std::vector<std::string> pieces;
    splitLines(text, pieces);
    if (text.find('\r') == std::string_view::npos)
    {
        edit.inserted.assign(text);
    }
    else
    {
        edit.inserted = joinLines(pieces);
    }
    if (edit.nothing() || edit.removed == edit.inserted)
    {
        // Nothing changes: answered as nothing, where it was asked for.
        Edit none;
        none.range = ALTextRange(range.begin, range.begin);
        return none;
    }

    // The line the range starts in keeps what came before it, the line it
    // ends in keeps what comes after, and the pieces go between.
    pieces.front().insert(0, mLines[range.begin.line], 0, range.begin.column);
    pieces.back().append(mLines[range.end.line], range.end.column, std::string::npos);
    mLines.erase(mLines.begin() + range.begin.line, mLines.begin() + range.end.line + 1);
    mLines.insert(mLines.begin() + range.begin.line,
                  std::make_move_iterator(pieces.begin()),
                  std::make_move_iterator(pieces.end()));

    ++mVersion;
    // The whole text kept, if it was, is made again when next asked
    // for: the same work as patching it here, and none where nobody
    // asks again.
    mWholeValid = false;
    mChanged(edit);
    return edit;
}

ALTextDocument::Edit ALTextDocument::append(std::string_view text)
{
    const ALTextPos at = end();
    return replace(ALTextRange(at, at), text);
}

ALTextDocument::Edit ALTextDocument::removeFirstLines(S32 count)
{
    if (count <= 0)
    {
        return Edit();
    }
    if (count >= lineCount())
    {
        return replace(ALTextRange(start(), end()), std::string_view());
    }
    return replace(ALTextRange(start(), ALTextPos(count, 0)), std::string_view());
}

// --- positions ---------------------------------------------------------------

ALTextPos ALTextDocument::end() const
{
    const S32 last = lineCount() - 1;
    return ALTextPos(last, lineLength(last));
}

ALTextPos ALTextDocument::lineStart(S32 line) const
{
    return ALTextPos(llclamp(line, 0, lineCount() - 1), 0);
}

ALTextPos ALTextDocument::lineEnd(S32 line) const
{
    line = llclamp(line, 0, lineCount() - 1);
    return ALTextPos(line, lineLength(line));
}

ALTextPos ALTextDocument::clampBytes(ALTextPos pos) const
{
    pos.line   = llclamp(pos.line, 0, lineCount() - 1);
    pos.column = llclamp(pos.column, 0, lineLength(pos.line));
    return pos;
}

ALTextRange ALTextDocument::clampBytes(const ALTextRange& range) const
{
    return ALTextRange(clampBytes(range.begin), clampBytes(range.end));
}

ALTextPos ALTextDocument::clamp(ALTextPos pos) const
{
    pos        = clampBytes(pos);
    pos.column = static_cast<S32>(utf8str_grapheme_align_backward(mLines[pos.line], pos.column));
    return pos;
}

ALTextPos ALTextDocument::nextCluster(ALTextPos pos) const
{
    pos = clamp(pos);
    if (pos.column >= lineLength(pos.line))
    {
        return pos.line + 1 < lineCount() ? ALTextPos(pos.line + 1, 0) : pos;
    }
    return ALTextPos(pos.line, static_cast<S32>(utf8str_step_grapheme_forward(mLines[pos.line], pos.column)));
}

ALTextPos ALTextDocument::prevCluster(ALTextPos pos) const
{
    pos = clamp(pos);
    if (pos.column == 0)
    {
        return pos.line > 0 ? lineEnd(pos.line - 1) : pos;
    }
    return ALTextPos(pos.line, static_cast<S32>(utf8str_step_grapheme_backward(mLines[pos.line], pos.column)));
}

ALTextPos ALTextDocument::nextWord(ALTextPos pos) const
{
    pos = clamp(pos);
    if (pos.column < lineLength(pos.line))
    {
        const size_t moved = utf8str_caret_word_forward(mLines[pos.line], pos.column);
        if (static_cast<S32>(moved) > pos.column)
        {
            return ALTextPos(pos.line, static_cast<S32>(moved));
        }
    }
    return pos.line + 1 < lineCount() ? ALTextPos(pos.line + 1, 0) : lineEnd(pos.line);
}

ALTextPos ALTextDocument::prevWord(ALTextPos pos) const
{
    pos = clamp(pos);
    if (pos.column > 0)
    {
        const size_t moved = utf8str_caret_word_backward(mLines[pos.line], pos.column);
        if (static_cast<S32>(moved) < pos.column)
        {
            return ALTextPos(pos.line, static_cast<S32>(moved));
        }
    }
    return pos.line > 0 ? lineEnd(pos.line - 1) : lineStart(pos.line);
}

ALTextRange ALTextDocument::wordAt(ALTextPos pos) const
{
    pos                             = clamp(pos);
    const std::pair<size_t, size_t> word = utf8str_word_range_at(mLines[pos.line], pos.column);
    return ALTextRange(ALTextPos(pos.line, static_cast<S32>(word.first)), ALTextPos(pos.line, static_cast<S32>(word.second)));
}

size_t ALTextDocument::offsetOf(ALTextPos pos) const
{
    pos = clampBytes(pos);
    return lineStarts()[static_cast<size_t>(pos.line)] + static_cast<size_t>(pos.column);
}

ALTextPos ALTextDocument::posAt(size_t offset) const
{
    const std::vector<size_t>& starts = lineStarts();
    if (offset >= mWhole.size())
    {
        return end();
    }
    const auto after = std::upper_bound(starts.begin(), starts.end(), offset);
    const S32  line  = static_cast<S32>(after - starts.begin()) - 1;
    return ALTextPos(line, static_cast<S32>(offset - starts[static_cast<size_t>(line)]));
}

S32 ALTextDocument::displayColumn(ALTextPos pos, S32 tab_width) const
{
    pos                    = clampBytes(pos);
    tab_width              = llmax(1, tab_width);
    const std::string& l   = mLines[pos.line];
    S32                col = 0;
    size_t             at  = 0;
    while (at < static_cast<size_t>(pos.column))
    {
        const size_t next = utf8str_step_grapheme_forward(l, at);
        col               = (l[at] == '\t') ? (col / tab_width + 1) * tab_width : col + 1;
        at                = next;
    }
    return col;
}

ALTextPos ALTextDocument::posAtDisplayColumn(S32 line, S32 display_column, S32 tab_width) const
{
    line                   = llclamp(line, 0, lineCount() - 1);
    tab_width              = llmax(1, tab_width);
    const std::string& l   = mLines[line];
    S32                col = 0;
    size_t             at  = 0;
    while (at < l.size() && col < display_column)
    {
        const S32 after = (l[at] == '\t') ? (col / tab_width + 1) * tab_width : col + 1;
        // A tab that reaches past the column wanted is the column wanted:
        // nothing sits inside a tab.
        if (after > display_column && l[at] == '\t')
        {
            break;
        }
        col = after;
        at  = utf8str_step_grapheme_forward(l, at);
    }
    return ALTextPos(line, static_cast<S32>(at));
}

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

    // Whether the byte at `at` begins something a word is made of: a letter,
    // a digit, an underscore, or anything beyond ASCII, which is a letter
    // often enough for a search to treat it as one.
    bool wordByteAt(std::string_view text, size_t at)
    {
        if (at >= text.size())
        {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(text[at]);
        return c >= 0x80 || c == '_' || std::isalnum(c);
    }

    // The end of a match of the needle at `at`, or npos. Without regard to
    // case it compares codepoint by codepoint, since lower-casing a string
    // can change its length and with it every offset.
    size_t matchAt(std::string_view hay, size_t at, std::string_view needle, bool case_insensitive)
    {
        if (!case_insensitive)
        {
            if (at + needle.size() > hay.size() || hay.compare(at, needle.size(), needle) != 0)
            {
                return std::string_view::npos;
            }
            return at + needle.size();
        }
        size_t h = at;
        size_t n = 0;
        while (n < needle.size())
        {
            if (h >= hay.size())
            {
                return std::string_view::npos;
            }
            const LLCodepointAt hc = utf8str_decode_at(hay, h);
            const LLCodepointAt nc = utf8str_decode_at(needle, n);
            if (LLStringOps::toLower(hc.cp) != LLStringOps::toLower(nc.cp))
            {
                return std::string_view::npos;
            }
            h = hc.next;
            n = nc.next;
        }
        return h;
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
    range = clampBytes(range.normalised());

    Edit edit;
    edit.range   = range;
    edit.removed = this->text(range);

    std::vector<std::string> pieces;
    splitLines(text, pieces);
    edit.inserted = joinLines(pieces);
    if (edit.nothing())
    {
        return edit;
    }

    // The whole text kept, if it is, is patched rather than made again:
    // the stretch replaced in it, and the line starts from the edit on.
    const bool   patch     = mWholeValid && mWholeVersion == mVersion;
    const size_t patch_at  = patch ? mLineStarts[static_cast<size_t>(range.begin.line)] + static_cast<size_t>(range.begin.column) : 0;
    const size_t patch_end = edit.removed.size();

    // The line the range starts in keeps what came before it, the line it
    // ends in keeps what comes after, and the pieces go between.
    pieces.front().insert(0, mLines[range.begin.line], 0, range.begin.column);
    pieces.back().append(mLines[range.end.line], range.end.column, std::string::npos);
    mLines.erase(mLines.begin() + range.begin.line, mLines.begin() + range.end.line + 1);
    mLines.insert(mLines.begin() + range.begin.line,
                  std::make_move_iterator(pieces.begin()),
                  std::make_move_iterator(pieces.end()));

    ++mVersion;
    if (patch)
    {
        mWhole.replace(patch_at, patch_end, edit.inserted);
        mLineStarts.resize(static_cast<size_t>(range.begin.line) + 1);
        for (size_t l = static_cast<size_t>(range.begin.line) + 1; l < mLines.size(); ++l)
        {
            mLineStarts.push_back(mLineStarts.back() + mLines[l - 1].size() + 1);
        }
        mWholeVersion = mVersion;
    }
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

// --- search ------------------------------------------------------------------

std::optional<ALTextRange> ALTextDocument::findInLine(S32 line, size_t from, size_t to, std::string_view needle, const FindOptions& options) const
{
    const std::string&         hay = mLines[line];
    std::optional<ALTextRange> found;
    to = llmin(to, hay.size());
    size_t at = from;
    while (at < to)
    {
        size_t begin = at;
        if (!options.caseInsensitive)
        {
            begin = hay.find(needle, at);
            if (begin == std::string::npos || begin >= to)
            {
                break;
            }
        }
        const size_t match_end = matchAt(hay, begin, needle, options.caseInsensitive);
        const bool   whole     = !options.wholeWord ||
                             ((begin == 0 || !wordByteAt(hay, utf8str_step_grapheme_backward(hay, begin))) && !wordByteAt(hay, match_end));
        if (match_end != std::string_view::npos && whole)
        {
            found = ALTextRange(ALTextPos(line, static_cast<S32>(begin)), ALTextPos(line, static_cast<S32>(match_end)));
            if (!options.backwards)
            {
                return found;
            }
        }
        at = utf8str_decode_at(hay, begin).next;
    }
    return found;
}

std::optional<ALTextRange> ALTextDocument::find(std::string_view needle, ALTextPos from) const
{
    return find(needle, from, FindOptions());
}

std::optional<ALTextRange> ALTextDocument::find(std::string_view needle, ALTextPos from, const FindOptions& options) const
{
    if (needle.empty() || needle.find('\n') != std::string_view::npos)
    {
        return std::nullopt;
    }
    from            = clampBytes(from);
    const S32    count = lineCount();
    const size_t none  = std::string::npos;

    if (!options.backwards)
    {
        // From here to the end, then from the start back to here.
        for (S32 l = from.line; l < count; ++l)
        {
            if (auto found = findInLine(l, l == from.line ? from.column : 0, none, needle, options))
            {
                return found;
            }
        }
        if (options.wrap)
        {
            for (S32 l = 0; l <= from.line; ++l)
            {
                if (auto found = findInLine(l, 0, l == from.line ? from.column : none, needle, options))
                {
                    return found;
                }
            }
        }
        return std::nullopt;
    }

    // The last match beginning before here, then round from the end.
    for (S32 l = from.line; l >= 0; --l)
    {
        if (auto found = findInLine(l, 0, l == from.line ? from.column : none, needle, options))
        {
            return found;
        }
    }
    if (options.wrap)
    {
        for (S32 l = count - 1; l >= from.line; --l)
        {
            if (auto found = findInLine(l, l == from.line ? from.column : 0, none, needle, options))
            {
                return found;
            }
        }
    }
    return std::nullopt;
}

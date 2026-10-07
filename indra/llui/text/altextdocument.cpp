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

#include "allinebreaks.h"
#include "altextchars.h"
#include "llstring.h"

#include <algorithm>

namespace
{
    const std::string EMPTY_LINE;
}

// --- Edit --------------------------------------------------------------------

ALTextPos ALTextDocument::Edit::workOutEnd() const
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
    // Put back, the text ends where it ended before; and a batch's
    // stretches are its again, each the other way round.
    back.keepEnd(range.normalised().end);
    back.parts.reserve(parts.size());
    for (const Part& part : parts)
    {
        back.parts.push_back({ part.after, part.before });
    }
    return back;
}

ALTextPos ALTextDocument::Edit::mapped(const ALTextPos& pos, bool pushed) const
{
    // The first stretch that ends past it; the one before is the last that
    // ended at or before it, but for one put in right at it that does not
    // push it, which is passed over.
    auto after = std::upper_bound(parts.begin(), parts.end(), pos, [](const ALTextPos& p, const Part& part) { return p < part.before.end; });
    while (after != parts.begin() && !pushed && (after - 1)->before.empty() && (after - 1)->before.end == pos)
    {
        --after;
    }
    if (after == parts.begin())
    {
        return pos;
    }
    const Part& last = *(after - 1);
    if (pos.line == last.before.end.line)
    {
        return ALTextPos(last.after.end.line, last.after.end.column + (pos.column - last.before.end.column));
    }
    return ALTextPos(pos.line + (last.after.end.line - last.before.end.line), pos.column);
}

const std::vector<ALTextDocument::Edit::LineSpan>& ALTextDocument::Edit::lineSpans() const
{
    if (!keptSpans.empty())
    {
        return keptSpans;
    }
    std::vector<LineSpan>& out = keptSpans;
    if (parts.empty())
    {
        const ALTextRange removed = range.normalised();
        LineSpan          span;
        span.first    = removed.begin.line;
        span.last     = removed.end.line;
        span.made     = 1 + breaksInserted();
        span.lastKept = removed.end.column == 0 && (inserted.empty() ? removed.begin.column == 0 : inserted.back() == '\n');
        span.firstColumn = removed.begin.column;
        span.shiftAfter  = span.made - (span.last - span.first + 1);
        out.push_back(span);
        return out;
    }
    // Where the run of stretches sharing lines began, in the text after.
    S32 made_from = 0;
    for (const Part& part : parts)
    {
        const bool kept = part.before.end.column == 0 && (part.after.empty() ? part.before.begin.column == 0
                                                                             : part.after.end.column == 0 && part.after.end.line > part.after.begin.line);
        if (!out.empty() && part.before.begin.line <= out.back().last)
        {
            LineSpan& span = out.back();
            span.last      = std::max(span.last, part.before.end.line);
            span.made      = part.after.end.line - made_from + 1;
            span.lastKept  = kept;
            span.shiftAfter = (out.size() > 1 ? out[out.size() - 2].shiftAfter : 0) + span.made - (span.last - span.first + 1);
            continue;
        }
        LineSpan span;
        span.first    = part.before.begin.line;
        span.last     = part.before.end.line;
        span.made     = part.after.end.line - part.after.begin.line + 1;
        span.lastKept    = kept;
        span.firstColumn = part.before.begin.column;
        span.shiftAfter  = (out.empty() ? 0 : out.back().shiftAfter) + span.made - (span.last - span.first + 1);
        made_from        = part.after.begin.line;
        out.push_back(span);
    }
    return out;
}

ALTextPos ALTextDocument::Edit::placed(const ALTextPos& pos, bool pushed) const
{
    if (parts.empty())
    {
        const ALTextRange removed = range.normalised();
        // Text put in right at it, and nothing taken, leaves it where it
        // was unless it is pushed.
        if (pos < removed.begin || (!pushed && pos == removed.begin))
        {
            return pos;
        }
        return removed.end <= pos ? slidPast(pos) : removed.begin;
    }
    const auto in = std::upper_bound(parts.begin(), parts.end(), pos, [](const ALTextPos& p, const Part& part) { return p < part.before.end; });
    if (in != parts.end() && in->before.begin <= pos)
    {
        return in->after.begin;
    }
    return mapped(pos, pushed);
}

std::optional<ALTextRange> ALTextDocument::Edit::replacedAround(const ALTextPos& pos) const
{
    if (parts.empty())
    {
        const ALTextRange removed = range.normalised();
        return removed.begin <= pos && pos < removed.end ? std::optional<ALTextRange>(removed) : std::nullopt;
    }
    const auto in = std::upper_bound(parts.begin(), parts.end(), pos, [](const ALTextPos& p, const Part& part) { return p < part.before.end; });
    return in != parts.end() && in->before.begin <= pos ? std::optional<ALTextRange>(in->before) : std::nullopt;
}

S32 ALTextDocument::Edit::lineAfter(S32 line) const
{
    const std::vector<LineSpan>& spans = lineSpans();
    // The last run that begins at or before the line.
    const auto next = std::upper_bound(spans.begin(), spans.end(), line, [](S32 l, const LineSpan& span) { return l < span.first; });
    if (next == spans.begin())
    {
        return line;
    }
    const LineSpan& span = *(next - 1);
    return line <= span.last ? -1 : line + span.shiftAfter;
}

ALTextPos ALTextDocument::Edit::slidPast(const ALTextPos& pos) const
{
    if (!parts.empty())
    {
        return mapped(pos, true);
    }
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
    const ALTextRange r = range_in.normalised();
    if (!parts.empty())
    {
        // Each end by the stretch it is inside, or else moved by those
        // before it, one put in right at it not moving it.
        const auto moved = [&](const ALTextPos& pos, bool is_end) {
            const auto in = std::upper_bound(parts.begin(), parts.end(), pos, [](const ALTextPos& p, const Part& part) { return p < part.before.end; });
            if (in != parts.end() && in->before.begin < pos && pos < in->before.end)
            {
                return is_end ? in->after.end : in->after.begin;
            }
            return mapped(pos, false);
        };
        return ALTextRange(moved(r.begin, false), moved(r.end, true));
    }
    const ALTextRange removed = range.normalised();
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
    if (!parts.empty())
    {
        // Taken where any stretch cut through it or landed inside it; else
        // each end moved by those before it.
        const ALTextRange r = range_in.normalised();
        for (auto it = std::lower_bound(parts.begin(), parts.end(), r.begin, [](const Part& part, const ALTextPos& p) { return part.before.end < p; });
             it != parts.end() && it->before.begin <= r.end; ++it)
        {
            const ALTextRange& removed = it->before;
            const bool         cut     = removed.empty() ? (r.begin < removed.begin && removed.begin < r.end)
                                                         : (r.begin < removed.end && removed.begin < r.end);
            if (cut)
            {
                return false;
            }
        }
        range_in = ALTextRange(mapped(r.begin, true), mapped(r.end, r.empty()));
        return true;
    }
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
    std::vector<std::string> lines;
    ALLineBreaks::split(text, lines);
    mLines.swap(lines);
}

ALTextDocument::Edit ALTextDocument::setText(std::string_view text)
{
    return replace(ALTextRange(start(), end()), text);
}

std::string ALTextDocument::text() const
{
    return ALLineBreaks::join(mLines.rows());
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
    // As long as it will be, and each line copied into its place: a
    // whole text is long.
    const std::string& head = mLines[range.begin.line];
    const size_t       from = static_cast<size_t>(range.begin.column);
    const size_t       tail = static_cast<size_t>(range.end.column);
    size_t             size = head.size() - from + 1 + tail;
    for (S32 l = range.begin.line + 1; l < range.end.line; ++l)
    {
        size += mLines[l].size() + 1;
    }
    std::string out;
    out.resize(size);
    {
        char* buffer = out.data();
        char* at = std::copy(head.begin() + static_cast<std::ptrdiff_t>(from), head.end(), buffer);
        for (S32 l = range.begin.line + 1; l < range.end.line; ++l)
        {
            *at++ = '\n';
            at    = std::copy(mLines[l].begin(), mLines[l].end(), at);
        }
        *at++ = '\n';
        std::copy(mLines[range.end.line].begin(), mLines[range.end.line].begin() + static_cast<std::ptrdiff_t>(tail), at);
    }
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
    return replace(range, text, {});
}

ALTextDocument::Edit ALTextDocument::replaceMany(std::vector<std::pair<ALTextRange, std::string>> edits)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    for (auto& one : edits)
    {
        one.first = clampBytes(one.first.normalised());
    }
    std::stable_sort(edits.begin(), edits.end(),
                     [](const auto& a, const auto& b) { return a.first.begin < b.first.begin || (a.first.begin == b.first.begin && a.first.end < b.first.end); });
    // None over another: one that would be is left out.
    std::vector<std::pair<ALTextRange, std::string>> kept;
    kept.reserve(edits.size());
    for (auto& one : edits)
    {
        if (!kept.empty() && one.first.begin < kept.back().first.end)
        {
            continue;
        }
        kept.push_back(std::move(one));
    }
    if (kept.empty())
    {
        Edit none;
        none.range = ALTextRange(start(), start());
        return none;
    }
    if (kept.size() == 1)
    {
        return replace(kept.front().first, kept.front().second);
    }
    // The text from the first to the last as it will be: what lies between
    // them as it is, and each put in, its line endings as LF; and where
    // each is, as it was and as it will be.
    const ALTextRange       span(kept.front().first.begin, kept.back().first.end);
    std::string             out;
    std::vector<Edit::Part> parts;
    parts.reserve(kept.size());
    ALTextPos  at   = span.begin;
    ALTextPos  made = span.begin;
    const auto append = [&out, &made](std::string_view piece) {
        out.append(piece);
        const size_t last_break = piece.rfind('\n');
        if (last_break == std::string_view::npos)
        {
            made.column += static_cast<S32>(piece.size());
            return;
        }
        made.line += static_cast<S32>(std::count(piece.begin(), piece.end(), '\n'));
        made.column = static_cast<S32>(piece.size() - last_break - 1);
    };
    for (auto& [range, piece] : kept)
    {
        append(this->text(ALTextRange(at, range.begin)));
        if (piece.find('\r') != std::string::npos)
        {
            piece = ALLineBreaks::withLineFeeds(piece);
        }
        Edit::Part part;
        part.before       = range;
        part.after.begin  = made;
        append(piece);
        part.after.end    = made;
        parts.push_back(part);
        at = range.end;
    }
    return replace(span, out, std::move(parts));
}

ALTextDocument::Edit ALTextDocument::replace(ALTextRange range, std::string_view text, std::vector<Edit::Part> parts)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    range = clampBytes(range.normalised());

    Edit edit;
    edit.range    = range;
    edit.removed  = this->text(range);
    // What is put in, its line endings as LF.
    edit.inserted = ALLineBreaks::withLineFeeds(text);
    if (edit.nothing() || edit.removed == edit.inserted)
    {
        // Nothing changes: answered as nothing, where it was asked for.
        Edit none;
        none.range = ALTextRange(range.begin, range.begin);
        return none;
    }

    edit.keepEnd();
    edit.parts = std::move(parts);

    const size_t           first = static_cast<size_t>(range.begin.line);
    const size_t           last  = static_cast<size_t>(range.end.line);
    const std::string_view put   = edit.inserted;
    if (first == last && put.find('\n') == std::string_view::npos)
    {
        // Within a line, and none broken: the line changed where it is.
        mLines[first].replace(static_cast<size_t>(range.begin.column), static_cast<size_t>(range.end.column - range.begin.column), put);
    }
    else
    {
        // The line the range starts in keeps what came before it, the line
        // it ends in keeps what comes after, and the lines put in go
        // between. Each made in the string of a line it replaces, while
        // there are any, which keeps what that held room for: a text put
        // in over another -- loaded again, a comparison made again --
        // makes and lets go of next to nothing.
        const std::string after = mLines[last].substr(static_cast<size_t>(range.end.column));
        std::vector<std::string> made;
        made.reserve(static_cast<size_t>(std::count(put.begin(), put.end(), '\n')) + 1);
        size_t reuse = first;
        size_t start = 0;
        while (true)
        {
            const size_t           at    = put.find('\n', start);
            const std::string_view piece = put.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
            std::string            line  = reuse <= last ? std::move(mLines[reuse++]) : std::string();
            if (made.empty())
            {
                line.resize(static_cast<size_t>(range.begin.column));
                line.append(piece);
            }
            else
            {
                line.assign(piece);
            }
            made.push_back(std::move(line));
            if (at == std::string_view::npos)
            {
                break;
            }
            start = at + 1;
        }
        made.back().append(after);
        mLines.replace(first, last - first + 1, std::make_move_iterator(made.begin()), std::make_move_iterator(made.end()));
    }

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
    // A plain ASCII character with another after it, or the line's end, is
    // a cluster of its own -- all that can join to one is not ASCII, but
    // for a carriage return and the line feed after it -- and needs no
    // walk of the line to say so.
    const std::string&  line = mLines[pos.line];
    const size_t        at   = static_cast<size_t>(pos.column);
    const unsigned char c    = static_cast<unsigned char>(line[at]);
    if (c < 0x80 && (at + 1 == line.size() || (static_cast<unsigned char>(line[at + 1]) < 0x80 && !(c == '\r' && line[at + 1] == '\n'))))
    {
        return ALTextPos(pos.line, pos.column + 1);
    }
    return ALTextPos(pos.line, static_cast<S32>(utf8str_step_grapheme_forward(line, pos.column)));
}

ALTextPos ALTextDocument::prevCluster(ALTextPos pos) const
{
    pos = clamp(pos);
    if (pos.column == 0)
    {
        return pos.line > 0 ? lineEnd(pos.line - 1) : pos;
    }
    // Likewise back over a plain ASCII character with another before it,
    // or the line's start.
    const std::string&  line = mLines[pos.line];
    const size_t        at   = static_cast<size_t>(pos.column) - 1;
    const unsigned char c    = static_cast<unsigned char>(line[at]);
    if (c < 0x80 && (at == 0 || (static_cast<unsigned char>(line[at - 1]) < 0x80 && !(line[at - 1] == '\r' && c == '\n'))))
    {
        return ALTextPos(pos.line, pos.column - 1);
    }
    return ALTextPos(pos.line, static_cast<S32>(utf8str_step_grapheme_backward(line, pos.column)));
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

namespace
{
    // What a byte is to code's word motion: a blank, a name's, or a mark.
    // Bytes past ASCII are a name's, so that a character is never split.
    enum class CodeRun : U8
    {
        Blank,
        Name,
        Mark,
    };
    CodeRun codeRunOf(char c)
    {
        return c == ' ' || c == '\t' ? CodeRun::Blank : alWordByte(c) ? CodeRun::Name : CodeRun::Mark;
    }
}

ALTextPos ALTextDocument::nextCodeWord(ALTextPos pos, bool parts) const
{
    pos                    = clamp(pos);
    const std::string& l   = mLines[pos.line];
    const size_t       n   = l.size();
    size_t             at  = static_cast<size_t>(pos.column);
    if (at >= n)
    {
        return pos.line + 1 < lineCount() ? ALTextPos(pos.line + 1, 0) : lineEnd(pos.line);
    }
    const CodeRun run = codeRunOf(l[at]);
    if (run != CodeRun::Blank)
    {
        ++at;
        while (at < n && codeRunOf(l[at]) == run && !(parts && run == CodeRun::Name && alNamePartAt(l, at)))
        {
            ++at;
        }
    }
    while (at < n && codeRunOf(l[at]) == CodeRun::Blank)
    {
        ++at;
    }
    return ALTextPos(pos.line, static_cast<S32>(at));
}

ALTextPos ALTextDocument::prevCodeWord(ALTextPos pos, bool parts) const
{
    pos = clamp(pos);
    if (pos.column == 0)
    {
        return pos.line > 0 ? lineEnd(pos.line - 1) : pos;
    }
    const std::string& l  = mLines[pos.line];
    size_t             at = static_cast<size_t>(pos.column);
    while (at > 0 && codeRunOf(l[at - 1]) == CodeRun::Blank)
    {
        --at;
    }
    if (at > 0)
    {
        const CodeRun run = codeRunOf(l[at - 1]);
        while (at > 0 && codeRunOf(l[at - 1]) == run)
        {
            --at;
            if (parts && run == CodeRun::Name && alNamePartAt(l, at))
            {
                break;
            }
        }
    }
    return ALTextPos(pos.line, static_cast<S32>(at));
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
    const size_t       end = static_cast<size_t>(pos.column);
    S32                col = 0;
    // ASCII, as code nearly always is: a byte a column, a tab to its stop.
    if (std::all_of(l.begin(), l.begin() + end, [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
    {
        for (size_t at = 0; at < end; ++at)
        {
            col = (l[at] == '\t') ? alNextTabStop(col, tab_width) : col + 1;
        }
        return col;
    }
    // A column a character as it is seen, walked with one iterator.
    for (const size_t at : utf8str_grapheme_starts(l, end))
    {
        col = (l[at] == '\t') ? alNextTabStop(col, tab_width) : col + 1;
    }
    return col;
}

ALTextPos ALTextDocument::posAtDisplayColumn(S32 line, S32 display_column, S32 tab_width) const
{
    line                   = llclamp(line, 0, lineCount() - 1);
    tab_width              = llmax(1, tab_width);
    const std::string& l   = mLines[line];
    const bool         ascii = std::all_of(l.begin(), l.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    // Where each character starts: every byte of ASCII, else as a walk
    // with one iterator finds them.
    const std::vector<size_t> starts = ascii ? std::vector<size_t>() : utf8str_grapheme_starts(l, l.size());
    const size_t              count  = ascii ? l.size() : starts.size();
    S32                       col    = 0;
    size_t                    i      = 0;
    for (; i < count && col < display_column; ++i)
    {
        const size_t at    = ascii ? i : starts[i];
        const S32    after = (l[at] == '\t') ? alNextTabStop(col, tab_width) : col + 1;
        // A tab that reaches past the column wanted is the column wanted:
        // nothing sits inside a tab.
        if (after > display_column && l[at] == '\t')
        {
            break;
        }
        col = after;
    }
    const size_t at = i >= count ? l.size() : ascii ? i : starts[i];
    return ALTextPos(line, static_cast<S32>(at));
}

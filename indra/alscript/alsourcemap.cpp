/**
 * @file alsourcemap.cpp
 * @brief Where every piece of a preprocessed text came from.
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

#include "alsourcemap.h"

#include <algorithm>

S32 ALSourceMap::addFile(const std::string& name, const std::string& path)
{
    File file;
    file.name = name;
    file.path = path.empty() ? name : path;
    mFiles.push_back(std::move(file));
    return S32(mFiles.size()) - 1;
}

S32 ALSourceMap::fileOf(const std::string& path) const
{
    for (size_t i = 0; i < mFiles.size(); ++i)
    {
        if (mFiles[i].path == path)
        {
            return S32(i);
        }
    }
    return -1;
}

void ALSourceMap::add(const Segment& segment)
{
    if (segment.verbatim && !mSegments.empty())
    {
        Segment& last = mSegments.back();
        if (last.verbatim && last.outLine == segment.outLine && last.file == segment.file && last.line == segment.line &&
            last.outColumn + last.length == segment.outColumn && last.column + last.length == segment.column)
        {
            last.length += segment.length;
            return;
        }
    }
    mSegments.push_back(segment);
}

// static
ALSourceMap ALSourceMap::identity(std::string_view text, const std::string& name)
{
    ALSourceMap map;
    map.addFile(name, std::string());
    S32    line  = 0;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        Segment s;
        s.outLine  = line;
        s.line     = line;
        s.length   = S32(end - start);
        s.verbatim = true;
        map.add(s);
        ++line;
        start = end + 1;
    }
    map.finish();
    return map;
}

size_t ALSourceMap::segmentAt(S32 line, S32 column) const
{
    if (line < 0 || line >= S32(mLineStart.size()))
    {
        return std::string::npos;
    }
    const size_t first = mLineStart[line];
    if (first >= mSegments.size() || mSegments[first].outLine != line)
    {
        return std::string::npos;
    }
    const size_t end   = size_t(line) + 1 < mLineStart.size() ? mLineStart[line + 1] : mSegments.size();
    const auto   after = std::upper_bound(mSegments.begin() + first, mSegments.begin() + end, column,
                                          [](S32 at, const Segment& segment) { return at < segment.outColumn; });
    return after == mSegments.begin() + first ? first : size_t(after - mSegments.begin()) - 1;
}

void ALSourceMap::finish()
{
    mLineStart.clear();
    for (size_t i = 0; i < mSegments.size(); ++i)
    {
        const S32 line = mSegments[i].outLine;
        while (S32(mLineStart.size()) <= line)
        {
            mLineStart.push_back(i);
        }
    }
    mByOrigin.resize(mSegments.size());
    for (size_t i = 0; i < mSegments.size(); ++i)
    {
        mByOrigin[i] = i;
    }
    std::stable_sort(mByOrigin.begin(), mByOrigin.end(), [this](size_t a, size_t b) {
        const Segment& x = mSegments[a];
        const Segment& y = mSegments[b];
        if (x.file != y.file)
        {
            return x.file < y.file;
        }
        if (x.line != y.line)
        {
            return x.line < y.line;
        }
        return x.column < y.column;
    });
}

ALSourceMap ALSourceMap::composed(const ALSourceMap& inner) const
{
    ALSourceMap out;
    out.mFiles = inner.mFiles;
    for (const Segment& s : mSegments)
    {
        if (!s.verbatim || s.length <= 0)
        {
            // What a macro made maps to where it was made, as the other
            // map says.
            const Loc loc = inner.toSource(s.line, s.column);
            if (!loc.found())
            {
                continue;
            }
            Segment through  = s;
            through.file     = loc.file;
            through.line     = loc.line;
            through.column   = loc.column;
            through.verbatim = false;
            out.add(through);
            continue;
        }
        // Copied as it stands: each stretch of it read through the other's
        // segment it lies over, in one walk along the line -- exact where
        // that segment is, the invocation or the nearest before it where
        // it is not.
        S32 offset = 0;
        while (offset < s.length)
        {
            const S32    at    = s.column + offset;
            const size_t index = inner.segmentAt(s.line, at);
            if (index == std::string::npos)
            {
                break;
            }
            const Segment& in    = inner.mSegments[index];
            S32            until = s.length;
            if (at < in.outColumn)
            {
                // Before the line's first: up to it.
                until = std::min(until, in.outColumn - s.column);
            }
            else if (index + 1 < inner.mSegments.size() && inner.mSegments[index + 1].outLine == s.line)
            {
                until = std::min(until, inner.mSegments[index + 1].outColumn - s.column);
            }
            const bool exact = in.verbatim && at >= in.outColumn && at < in.outColumn + in.length;
            if (exact)
            {
                until = std::min(until, in.outColumn + in.length - s.column);
            }
            until = std::max(until, offset + 1);
            Segment piece;
            piece.outLine   = s.outLine;
            piece.outColumn = s.outColumn + offset;
            piece.length    = until - offset;
            piece.file      = in.file;
            if (exact)
            {
                piece.line     = in.line;
                piece.column   = in.column + (at - in.outColumn);
                piece.verbatim = true;
            }
            else
            {
                const Loc loc  = inner.toSource(s.line, at);
                piece.line     = loc.line;
                piece.column   = loc.column;
                piece.verbatim = false;
            }
            out.add(piece);
            offset = until;
        }
    }
    out.finish();
    return out;
}

bool ALSourceMap::lineStart(S32 line, Loc& at) const
{
    if (line < 0 || line >= S32(mLineStart.size()) || mLineStart[line] >= mSegments.size())
    {
        return false;
    }
    const Segment& first = mSegments[mLineStart[line]];
    if (first.outLine != line || !first.verbatim || first.column != first.outColumn)
    {
        return false;
    }
    at = Loc{ first.file, first.line, 0 };
    return true;
}

std::vector<std::pair<S32, S32>> ALSourceMap::othersLines() const
{
    std::vector<std::pair<S32, S32>> out;
    const auto add = [&out](S32 line) {
        if (!out.empty() && out.back().second + 1 == line)
        {
            out.back().second = line;
        }
        else
        {
            out.emplace_back(line, line);
        }
    };
    // The segments in output order: a line's are together.
    S32  line   = -1;
    bool others = false;
    for (const Segment& segment : mSegments)
    {
        if (segment.outLine != line)
        {
            if (line >= 0 && others)
            {
                add(line);
            }
            line   = segment.outLine;
            others = true;
        }
        others = others && segment.file > 0;
    }
    if (line >= 0 && others)
    {
        add(line);
    }
    return out;
}

// static
bool ALSourceMap::within(const std::vector<std::pair<S32, S32>>& runs, S32 first, S32 last)
{
    // The run starting at or before the first line, if any, holds them all
    // or none does: the runs are in order and apart.
    auto after = std::upper_bound(runs.begin(), runs.end(), first, [](S32 line, const std::pair<S32, S32>& run) { return line < run.first; });
    if (after == runs.begin())
    {
        return false;
    }
    --after;
    return first >= after->first && last <= after->second;
}

ALSourceMap::Loc ALSourceMap::toSource(S32 line, S32 column) const
{
    Loc loc;
    // The last segment on the line that starts at or before the column,
    // else the line's first.
    const size_t best = segmentAt(line, column);
    if (best == std::string::npos)
    {
        return loc;
    }
    const Segment& segment = mSegments[best];
    loc.file   = segment.file;
    loc.line   = segment.line;
    loc.column = segment.column;
    if (segment.verbatim && column > segment.outColumn)
    {
        loc.column += std::min(column - segment.outColumn, segment.length);
    }
    return loc;
}

bool ALSourceMap::verbatimSpan(S32 line, S32 column, S32 endColumn, Loc& begin, Loc& end) const
{
    if (line < 0 || line >= S32(mLineStart.size()) || endColumn < column)
    {
        return false;
    }
    // The segments of the line the stretch runs over, each of them copied
    // from the same line of the same file at the same distance as the
    // others -- the blanks between them the source's own -- so that the
    // stretch is the source's as it stands. A segment's end is still its
    // own: an insertion just after a token goes with the token.
    const Segment* first = nullptr;
    S32            shift = 0;
    for (size_t i = mLineStart[line]; i < mSegments.size() && mSegments[i].outLine == line; ++i)
    {
        const Segment& segment = mSegments[i];
        const S32      seg_end = segment.outColumn + segment.length;
        if (!first)
        {
            if (segment.outColumn <= column && column <= seg_end)
            {
                if (!segment.verbatim)
                {
                    return false;
                }
                first = &segment;
                shift = segment.column - segment.outColumn;
            }
            else
            {
                continue;
            }
        }
        else if (segment.outColumn >= endColumn)
        {
            break;
        }
        else if (!segment.verbatim || segment.file != first->file || segment.line != first->line || segment.column - segment.outColumn != shift)
        {
            return false;
        }
        if (endColumn <= seg_end)
        {
            begin = Loc{ first->file, first->line, column + shift };
            end   = Loc{ first->file, first->line, endColumn + shift };
            return true;
        }
    }
    return false;
}

ALSourceMap::Loc ALSourceMap::toExpanded(S32 file, S32 line, S32 column) const
{
    Loc loc;
    // The first segment of that file and line whose span reaches the
    // column, else the first one after it on the same line.
    auto after = std::lower_bound(mByOrigin.begin(), mByOrigin.end(), std::make_pair(file, line), [this](size_t index, const std::pair<S32, S32>& key) {
        const Segment& s = mSegments[index];
        return s.file < key.first || (s.file == key.first && s.line < key.second);
    });
    for (auto it = after; it != mByOrigin.end(); ++it)
    {
        const Segment& s = mSegments[*it];
        if (s.file != file || s.line != line)
        {
            break;
        }
        // A verbatim segment covers its own text; a macro's product covers
        // the invocation, up to whatever comes next on the source line.
        bool within = column >= s.column;
        if (within)
        {
            if (s.verbatim)
            {
                within = column < s.column + s.length;
            }
            else
            {
                auto next = it + 1;
                within    = next == mByOrigin.end() || mSegments[*next].file != file || mSegments[*next].line != line ||
                         column < mSegments[*next].column;
            }
        }
        if (within || s.column >= column)
        {
            loc.file   = 0;
            loc.line   = s.outLine;
            loc.column = s.outColumn;
            if (s.verbatim && column > s.column && column < s.column + s.length)
            {
                loc.column += column - s.column;
            }
            return loc;
        }
    }
    // Past the last token on the line: the end of that token.
    for (auto it = after; it != mByOrigin.end(); ++it)
    {
        const Segment& s = mSegments[*it];
        if (s.file != file || s.line != line)
        {
            break;
        }
        loc.file   = 0;
        loc.line   = s.outLine;
        loc.column = s.outColumn + s.length;
    }
    return loc;
}

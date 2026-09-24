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
    mSegments.push_back(segment);
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
        const Loc loc = inner.toSource(s.line, s.column);
        if (!loc.found())
        {
            continue;
        }
        Segment through  = s;
        through.file     = loc.file;
        through.line     = loc.line;
        through.column   = loc.column;
        // Exact only where both maps are.
        through.verbatim = s.verbatim && inner.toSource(s.line, s.column + 1).column == loc.column + 1;
        out.mSegments.push_back(through);
    }
    out.finish();
    return out;
}

ALSourceMap::Loc ALSourceMap::toSource(S32 line, S32 column) const
{
    Loc loc;
    if (line < 0 || line >= S32(mLineStart.size()))
    {
        return loc;
    }
    // The last segment on the line that starts at or before the column,
    // else the line's first.
    size_t first = mLineStart[line];
    if (mSegments[first].outLine != line)
    {
        return loc;
    }
    size_t best = first;
    for (size_t i = first; i < mSegments.size() && mSegments[i].outLine == line; ++i)
    {
        if (mSegments[i].outColumn <= column)
        {
            best = i;
        }
        else
        {
            break;
        }
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

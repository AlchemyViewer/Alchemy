/**
 * @file alfoldmodel.cpp
 * @brief The blocks of a code editor's text that fold, and which of them are folded.
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

#include "alfoldmodel.h"

#include "altextchars.h"

#include <algorithm>

namespace
{
    // A line with nothing but whitespace, or nothing.
    std::string_view trimmed(const std::string& line)
    {
        size_t begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos)
        {
            return std::string_view();
        }
        size_t end = line.find_last_not_of(" \t\r");
        return std::string_view(line).substr(begin, end - begin + 1);
    }

    // What closes a block on a line of its own, and so belongs to the
    // block above it.
    bool closesBlock(std::string_view text)
    {
        static const char* const CLOSERS[] = { "}", "};", "})", "});", "end", "end)", "end,", "end);", ")", ");", "]", "],", "];" };
        for (const char* closer : CLOSERS)
        {
            if (text == closer)
            {
                return true;
            }
        }
        return false;
    }
}

const std::vector<ALFoldModel::Region>& ALFoldModel::regions(const ALTextDocument& doc, S32 tab_width)
{
    if (mValid && mVersion == doc.version() && mTabWidth == tab_width)
    {
        return mRegions;
    }
    mRegions.clear();
    const S32        count = doc.lineCount();
    const S32        tab   = tab_width;
    std::vector<S32> indent(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        const std::string& line  = doc.line(l);
        size_t             lead  = 0;
        const S32          n     = alBlanksWidth(line, tab, &lead);
        const bool         blank = line.find_first_not_of(" \t\r", lead) == std::string::npos;
        indent[l]                = blank ? -1 : n;
    }
    // A block is a line and the deeper lines after it, with a line of
    // nothing going with whichever side keeps the block whole, and the
    // closer on the line after -- a brace, an `end` -- taken as part of it.
    std::vector<S32> end_of(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        if (indent[l] < 0)
        {
            continue;
        }
        S32 end = l;
        S32 k   = l + 1;
        for (; k < count; ++k)
        {
            if (indent[k] < 0)
            {
                continue;
            }
            if (indent[k] > indent[l])
            {
                end = k;
            }
            else
            {
                break;
            }
        }
        if (end == l)
        {
            continue;
        }
        if (k < count && indent[k] == indent[l] && closesBlock(trimmed(doc.line(k))))
        {
            end = k;
        }
        end_of[l] = end;
    }
    // A brace on a line of its own is its header's: `default` and the
    // `{` under it fold as one block, from the header.
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] < 0 || trimmed(doc.line(l)) != "{")
        {
            continue;
        }
        S32 header = l - 1;
        while (header >= 0 && indent[header] < 0)
        {
            --header;
        }
        if (header >= 0 && indent[header] == indent[l] && end_of[header] < 0)
        {
            end_of[header] = end_of[l];
            end_of[l]      = -1;
        }
    }
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] > l)
        {
            mRegions.push_back(Region{ l, end_of[l] });
        }
    }
    mVersion  = doc.version();
    mTabWidth = tab_width;
    mValid    = true;
    return mRegions;
}

const ALFoldModel::Region* ALFoldModel::startingAt(const ALTextDocument& doc, S32 tab_width, S32 line)
{
    const std::vector<Region>& all = regions(doc, tab_width);
    const auto it = std::lower_bound(all.begin(), all.end(), line, [](const Region& region, S32 l) { return region.start < l; });
    return (it != all.end() && it->start == line) ? &*it : nullptr;
}

const ALFoldModel::Region* ALFoldModel::around(const ALTextDocument& doc, S32 tab_width, S32 line)
{
    // The innermost: of those that hold the line, the one that starts last.
    const Region* found = nullptr;
    for (const Region& region : regions(doc, tab_width))
    {
        if (region.start >= line)
        {
            break;
        }
        if (region.end >= line)
        {
            found = &region;
        }
    }
    return found;
}

bool ALFoldModel::isFolded(S32 line) const
{
    return std::binary_search(mFolded.begin(), mFolded.end(), line);
}

std::optional<ALFoldModel::Region> ALFoldModel::fold(const ALTextDocument& doc, S32 tab_width, S32 line)
{
    const Region* region = startingAt(doc, tab_width, line);
    if (!region)
    {
        region = around(doc, tab_width, line);
    }
    if (!region || isFolded(region->start))
    {
        return std::nullopt;
    }
    const Region chosen = *region;
    mFolded.insert(std::upper_bound(mFolded.begin(), mFolded.end(), chosen.start), chosen.start);
    return chosen;
}

bool ALFoldModel::unfold(const ALTextDocument& doc, S32 tab_width, S32 line)
{
    S32 start = -1;
    if (isFolded(line))
    {
        start = line;
    }
    else
    {
        // The innermost folded block around the line -- which, since a
        // folded block's lines are hidden, is the one whose first line
        // this is a descendant of.
        for (S32 folded : mFolded)
        {
            const Region* region = startingAt(doc, tab_width, folded);
            if (region && region->start < line && line <= region->end)
            {
                start = folded;
            }
        }
    }
    if (start < 0)
    {
        return false;
    }
    mFolded.erase(std::remove(mFolded.begin(), mFolded.end(), start), mFolded.end());
    return true;
}

void ALFoldModel::foldAll(const ALTextDocument& doc, S32 tab_width)
{
    mFolded.clear();
    for (const Region& region : regions(doc, tab_width))
    {
        mFolded.push_back(region.start);
    }
}

bool ALFoldModel::reveal(const ALTextDocument& doc, S32 tab_width, S32 line)
{
    // Every folded block the line is inside opens.
    bool changed = false;
    for (S32 i = static_cast<S32>(mFolded.size()) - 1; i >= 0; --i)
    {
        const Region* region = startingAt(doc, tab_width, mFolded[i]);
        if (!region || (region->start < line && line <= region->end))
        {
            mFolded.erase(mFolded.begin() + i);
            changed = true;
        }
    }
    return changed;
}

std::vector<std::pair<S32, S32>> ALFoldModel::hidden(const ALTextDocument& doc, S32 tab_width)
{
    // A fold whose block is gone is gone with it.
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(), [&](S32 start) { return startingAt(doc, tab_width, start) == nullptr; }),
                  mFolded.end());
    std::vector<std::pair<S32, S32>> out;
    for (S32 start : mFolded)
    {
        const Region* region = startingAt(doc, tab_width, start);
        out.emplace_back(region->start + 1, region->end);
    }
    return out;
}

void ALFoldModel::edited(const ALTextDocument::Edit& edit)
{
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    std::vector<S32>                                  kept;
    kept.reserve(mFolded.size());
    for (const S32 start : mFolded)
    {
        // Moved along by every run before it; gone where one is over it.
        S32  shift = 0;
        bool gone  = false;
        for (const ALTextDocument::Edit::LineSpan& span : spans)
        {
            if (start > span.first && (start < span.last || (start == span.last && !span.lastKept)))
            {
                gone = true;
                break;
            }
            if (start > span.last || (start == span.last && span.lastKept))
            {
                shift += span.made - (span.last - span.first + 1);
                continue;
            }
            break;
        }
        if (!gone)
        {
            kept.push_back(start + shift);
        }
    }
    mFolded.swap(kept);
    mValid = false;
}

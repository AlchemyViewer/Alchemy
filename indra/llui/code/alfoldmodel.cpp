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

void ALFoldModel::setSyntax(blocks_t blocks, revision_t revision)
{
    mBlocks   = std::move(blocks);
    mRevision = std::move(revision);
    mLines.clear();
    mValid = false;
}

void ALFoldModel::setLineComment(std::string token)
{
    if (token != mLineComment)
    {
        mLineComment = std::move(token);
        mLines.clear();
        mValid = false;
    }
}

const ALFoldModel::Line& ALFoldModel::lineAt(const ALTextDocument& doc, S32 line)
{
    if (mLines.size() != static_cast<size_t>(doc.lineCount()))
    {
        mLines.resize(static_cast<size_t>(doc.lineCount()));
    }
    Line&     entry    = mLines[static_cast<size_t>(line)];
    const U32 revision = mRevision ? mRevision(line) : 0;
    if (entry.valid && entry.revision == revision)
    {
        return entry;
    }
    entry          = Line();
    entry.valid    = true;
    entry.revision = revision;
    const std::string&     text = doc.line(line);
    size_t                 lead = 0;
    const S32              n    = alBlanksWidth(text, mTabWidth, &lead);
    const std::string_view body = trimmed(text);
    entry.indent                = body.empty() ? -1 : n;
    entry.closes                = closesBlock(body);
    entry.brace                 = body == "{";
    // `-- #region name` and `-- #endregion`, in the text's line comment.
    if (!mLineComment.empty() && body.substr(0, mLineComment.size()) == mLineComment)
    {
        std::string_view rest = body.substr(mLineComment.size());
        rest.remove_prefix(std::min(rest.size(), rest.find_first_not_of(" \t")));
        entry.marker = rest.substr(0, 7) == "#region" ? 1 : rest.substr(0, 10) == "#endregion" ? -1 : 0;
    }
    if (mBlocks && !body.empty())
    {
        mBlocks(line, entry.blocks);
        const S32 first = static_cast<S32>(lead);
        for (Block& block : entry.blocks)
        {
            block.first = block.column == first;
        }
    }
    return entry;
}

void ALFoldModel::setTabWidth(S32 tab_width)
{
    if (tab_width != mTabWidth)
    {
        mTabWidth = tab_width;
        mLines.clear();
        mValid = false;
    }
}

// static
void ALFoldModel::pairMarker(S8 marker, S32 line, std::vector<S32>& open, std::vector<S32>& end_of)
{
    if (marker > 0)
    {
        open.push_back(line);
    }
    else if (marker < 0 && !open.empty())
    {
        if (line > open.back())
        {
            end_of[static_cast<size_t>(open.back())] = llmax(end_of[static_cast<size_t>(open.back())], line);
        }
        open.pop_back();
    }
}

S32 ALFoldModel::indentOf(const ALTextDocument& doc, S32 tab_width, S32 line, S32 reach)
{
    setTabWidth(tab_width);
    const S32 count = doc.lineCount();
    for (S32 l = line; l < count && l < line + reach; ++l)
    {
        const S32 indent = lineAt(doc, l).indent;
        if (indent >= 0)
        {
            return indent;
        }
    }
    return 0;
}

const std::vector<ALFoldModel::Region>& ALFoldModel::regions(const ALTextDocument& doc, S32 tab_width)
{
    setTabWidth(tab_width);
    if (mValid && mVersion == doc.version())
    {
        return mRegions;
    }
    const S32 count = doc.lineCount();
    // Each block's last line by its first, the widest where several start
    // on a line; then in order, with no sort.
    std::vector<S32> end_of(static_cast<size_t>(count), -1);
    if (mBlocks)
    {
        bySyntax(doc, end_of);
    }
    else
    {
        byIndent(doc, end_of);
        // And the regions the text marks, as the comment says: after the
        // blocks by indentation, which give a brace alone to its header
        // without them; the syntax pass pairs them as it goes.
        if (!mLineComment.empty())
        {
            std::vector<S32> open;
            for (S32 l = 0; l < count; ++l)
            {
                pairMarker(lineAt(doc, l).marker, l, open, end_of);
            }
        }
    }
    mRegions.clear();
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[static_cast<size_t>(l)] > l)
        {
            mRegions.push_back(Region{ l, end_of[static_cast<size_t>(l)] });
        }
    }
    mVersion = doc.version();
    mValid   = true;
    return mRegions;
}

void ALFoldModel::bySyntax(const ALTextDocument& doc, std::vector<S32>& end_of)
{
    // What is open, each where it opened and whether it opened its line.
    struct Opened
    {
        S32  line;
        bool first;
    };
    const S32           count = doc.lineCount();
    std::vector<Opened> open;
    // The blocks, as they close; those opened on a line of their own go
    // with their headers after.
    std::vector<Region> alone;
    std::vector<S32>    marked;
    for (S32 l = 0; l < count; ++l)
    {
        const Line& line = lineAt(doc, l);
        pairMarker(line.marker, l, marked, end_of);
        for (size_t i = 0; i < line.blocks.size(); ++i)
        {
            const Block& block = line.blocks[i];
            if (block.event != Event::Open && !open.empty())
            {
                // Through this line, unless it opens again after the
                // close, which it then starts.
                bool again = block.event == Event::Middle;
                for (size_t k = i + 1; k < line.blocks.size() && !again; ++k)
                {
                    again = line.blocks[k].event != Event::Close;
                }
                const Region region{ open.back().line, again ? l - 1 : l };
                if (region.end > region.start)
                {
                    if (open.back().first)
                    {
                        alone.push_back(region);
                    }
                    else
                    {
                        end_of[static_cast<size_t>(region.start)] = llmax(end_of[static_cast<size_t>(region.start)], region.end);
                    }
                }
                open.pop_back();
            }
            if (block.event != Event::Close)
            {
                open.push_back(Opened{ l, block.event == Event::Open && block.first });
            }
        }
    }
    // Opened on a line of its own -- a brace under its header -- a block
    // is its header's, where that starts none of its own.
    for (Region region : alone)
    {
        S32 header = region.start - 1;
        while (header >= 0 && lineAt(doc, header).indent < 0)
        {
            --header;
        }
        if (header >= 0 && end_of[static_cast<size_t>(header)] < 0)
        {
            region.start = header;
        }
        end_of[static_cast<size_t>(region.start)] = llmax(end_of[static_cast<size_t>(region.start)], region.end);
    }
}

std::vector<S32> ALFoldModel::openAt(const ALTextDocument& doc, S32 tab_width, S32 line, size_t most, S32 reach)
{
    std::vector<S32> out;
    if (!mBlocks)
    {
        for (const Region& region : regions(doc, tab_width))
        {
            // By start: none past the line holds it.
            if (region.start >= line)
            {
                break;
            }
            if (region.end >= line)
            {
                out.push_back(region.start);
            }
        }
        if (out.size() > most)
        {
            out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(most));
        }
        return out;
    }
    setTabWidth(tab_width);
    // Whether a line opens what nothing after it closes, walked back to with
    // `closes` closers after it still to be matched, which it matches; and
    // whether the outermost it leaves open opened the line.
    const auto opensOn = [](const Line& entry, S32& closes, bool& first) {
        bool opens = false;
        for (auto it = entry.blocks.rbegin(); it != entry.blocks.rend(); ++it)
        {
            if (it->event != Event::Close)
            {
                if (closes > 0)
                {
                    --closes;
                }
                else
                {
                    opens = true;
                    first = it->event == Event::Open && it->first;
                }
            }
            if (it->event != Event::Open)
            {
                ++closes;
            }
        }
        return opens;
    };
    // Back from the line, innermost first: what opens with nothing after
    // it closing it is open there.
    S32 closes = 0;
    for (S32 l = llmin(line, doc.lineCount()) - 1; l >= 0 && l >= line - reach && out.size() < most; --l)
    {
        bool first = false;
        if (!opensOn(lineAt(doc, l), closes, first))
        {
            continue;
        }
        // Opened on a line of its own: its header's line, where that opens
        // nothing of its own, as the blocks have it (bySyntax); else its
        // own, the header's block the next one out. Only blank lines are
        // between them, which close nothing.
        S32 start = l;
        if (first)
        {
            S32 header = l - 1;
            while (header >= 0 && lineAt(doc, header).indent < 0)
            {
                --header;
            }
            S32  after_header = closes;
            bool header_first = false;
            if (header >= 0 && !opensOn(lineAt(doc, header), after_header, header_first))
            {
                start = header;
            }
        }
        out.insert(out.begin(), start);
    }
    return out;
}

void ALFoldModel::byIndent(const ALTextDocument& doc, std::vector<S32>& end_of)
{
    const S32        count = doc.lineCount();
    std::vector<S32> indent(static_cast<size_t>(count), -1);
    for (S32 l = 0; l < count; ++l)
    {
        indent[static_cast<size_t>(l)] = lineAt(doc, l).indent;
    }
    // A block is a line and the deeper lines after it, with a line of
    // nothing going with whichever side keeps the block whole, and the
    // closer on the line after -- a brace, an `end` -- taken as part of it.
    for (S32 l = 0; l < count; ++l)
    {
        if (indent[static_cast<size_t>(l)] < 0)
        {
            continue;
        }
        S32 end = l;
        S32 k   = l + 1;
        for (; k < count; ++k)
        {
            if (indent[static_cast<size_t>(k)] < 0)
            {
                continue;
            }
            if (indent[static_cast<size_t>(k)] > indent[static_cast<size_t>(l)])
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
        if (k < count && indent[static_cast<size_t>(k)] == indent[static_cast<size_t>(l)] && lineAt(doc, k).closes)
        {
            end = k;
        }
        end_of[static_cast<size_t>(l)] = end;
    }
    // A brace on a line of its own is its header's: `default` and the
    // `{` under it fold as one block, from the header.
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[static_cast<size_t>(l)] < 0 || !lineAt(doc, l).brace)
        {
            continue;
        }
        S32 header = l - 1;
        while (header >= 0 && indent[static_cast<size_t>(header)] < 0)
        {
            --header;
        }
        if (header >= 0 && indent[static_cast<size_t>(header)] == indent[static_cast<size_t>(l)] && end_of[static_cast<size_t>(header)] < 0)
        {
            end_of[static_cast<size_t>(header)] = end_of[static_cast<size_t>(l)];
            end_of[static_cast<size_t>(l)]      = -1;
        }
    }
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

void ALFoldModel::edited(const ALTextDocument::Edit& edit, S32 lines)
{
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    // The lines it touched are read again.
    mLines.applySpans(spans, lines, Line(), Line());
    std::vector<S32>                                  kept;
    kept.reserve(mFolded.size());
    for (const S32 start : mFolded)
    {
        // Moved along by every run before it; gone where one is over it.
        S32  shift = 0;
        bool gone  = false;
        for (const ALTextDocument::Edit::LineSpan& span : spans)
        {
            // From its first line's start, a run that does more than type
            // there is over that line as well: it takes the line, unless
            // all it did was put whole lines in above it.
            const bool from_start = span.firstColumn == 0 && (span.last > span.first || span.made > 1);
            if ((start > span.first || (start == span.first && from_start)) && (start < span.last || (start == span.last && !span.lastKept)))
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
    // Two the edit brought to one line are one fold.
    kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
    mFolded.swap(kept);
    mValid = false;
}

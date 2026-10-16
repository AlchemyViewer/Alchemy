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

    typedef ALTextDocument::Edit::LineSpan LineSpan;

    // Where a line kept is after an edit's runs: after a run, moved by what
    // the runs up to it added or took; inside one, the line as far down
    // the run's lines as it was, or their last -- any place, so long as
    // every line kept goes by the same.
    S32 movedLine(const std::vector<LineSpan>& spans, S32 line)
    {
        const auto after = std::upper_bound(spans.begin(), spans.end(), line, [](S32 at, const LineSpan& span) { return at < span.first; });
        if (after == spans.begin())
        {
            return line;
        }
        const LineSpan& span   = *(after - 1);
        const S32       before = after - 1 == spans.begin() ? 0 : (after - 2)->shiftAfter;
        return line <= span.last ? span.first + before + llmin(line - span.first, llmax(span.made, 1) - 1) : line + span.shiftAfter;
    }
}

void ALFoldModel::setSyntax(blocks_t blocks, lex_t lex)
{
    mBlocks = std::move(blocks);
    mLex    = std::move(lex);
    mLines.clear();
    invalidate();
}

void ALFoldModel::relexed(S32 first, S32 last)
{
    const S32 end = llmin(last, static_cast<S32>(mLines.size()) - 1);
    for (S32 line = llmax(first, 0); line <= end; ++line)
    {
        mLines[static_cast<size_t>(line)].valid = false;
    }
    if (mBlocks && first <= last)
    {
        mValid      = false;
        mDirtyFirst = mDirtyFirst > mDirtyLast ? first : llmin(mDirtyFirst, first);
        mDirtyLast  = llmax(mDirtyLast, last);
    }
}

void ALFoldModel::setLineComment(std::string token)
{
    if (token != mLineComment)
    {
        mLineComment = std::move(token);
        mLines.clear();
        invalidate();
    }
}

const ALFoldModel::Line& ALFoldModel::lineAt(const ALTextDocument& doc, S32 line)
{
    if (mLines.size() != static_cast<size_t>(doc.lineCount()))
    {
        mLines.resize(static_cast<size_t>(doc.lineCount()));
    }
    Line& entry = mLines[static_cast<size_t>(line)];
    if (entry.valid)
    {
        return entry;
    }
    entry       = Line();
    entry.valid = true;
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
        invalidate();
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
    // Lexed through to the end first, so that every line whose blocks
    // changed has been said to (relexed) before any is read.
    if (mLex && count > 0)
    {
        mLex(count - 1);
    }
    // By syntax, from the walk's changes alone where they say enough
    // (spliceRegions); else each block's last line by its first, the
    // widest where several start on a line, then in order, with no sort.
    mLastSpliced = false;
    if (mBlocks)
    {
        walkSyntax(doc);
        mLastSpliced = mRegionsKept && mSplice.ok;
        if (mLastSpliced)
        {
            spliceRegions();
        }
    }
    if (!mLastSpliced)
    {
        std::vector<S32>& end_of = mEndOf;
        end_of.assign(static_cast<size_t>(count), -1);
        if (mBlocks)
        {
            bySyntax(end_of);
        }
        else
        {
            byIndent(doc, end_of);
            // And the regions the text marks, as the comment says: after the
            // blocks by indentation, which give a brace alone to its header
            // without them; the syntax pass pairs them as it goes.
            if (!mLineComment.empty())
            {
                mMarked.clear();
                for (S32 l = 0; l < count; ++l)
                {
                    pairMarker(lineAt(doc, l).marker, l, mMarked, end_of);
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
    }
    // Kept, to move with the edits and be put together from the next walk's
    // changes, where they were found by syntax.
    mRegionsKept = static_cast<bool>(mBlocks);
    mVersion     = doc.version();
    mValid       = true;
    return mRegions;
}

S32 ALFoldModel::aloneTarget(const ALTextDocument& doc, S32 at, const std::vector<Opened>& open, const std::vector<S32>& marked)
{
    // The header is the line with anything on it above; what it opens of
    // its own that is still open is opened later than anything under it,
    // so near the top of what is open.
    S32 header = at - 1;
    while (header >= 0 && lineAt(doc, header).indent < 0)
    {
        --header;
    }
    if (header < 0)
    {
        return at;
    }
    for (auto it = open.rbegin(); it != open.rend() && it->line >= header; ++it)
    {
        if (it->line == header && !it->first)
        {
            return at;
        }
    }
    for (auto it = marked.rbegin(); it != marked.rend() && *it >= header; ++it)
    {
        if (*it == header)
        {
            return at;
        }
    }
    return header;
}

void ALFoldModel::walkLine(const ALTextDocument& doc, const Line& line, S32 at, std::vector<Opened>& open, std::vector<S32>& marked, std::vector<Closed>& closed,
                           size_t& open_low, size_t& marked_low)
{
    // A region the comment marks, as pairMarker pairs one.
    if (line.marker > 0)
    {
        marked.push_back(at);
    }
    else if (line.marker < 0 && !marked.empty())
    {
        if (at > marked.back())
        {
            closed.push_back(Closed{ at, marked.back(), at, false, marked.back() });
        }
        marked.pop_back();
        marked_low = std::min(marked_low, marked.size());
    }
    for (size_t i = 0; i < line.blocks.size(); ++i)
    {
        const Block& block = line.blocks[i];
        if (block.event != Event::Open && !open.empty())
        {
            // Through this line, unless it opens again after the close,
            // which it then starts.
            bool again = block.event == Event::Middle;
            for (size_t k = i + 1; k < line.blocks.size() && !again; ++k)
            {
                again = line.blocks[k].event != Event::Close;
            }
            const S32 start = open.back().line;
            const S32 end   = again ? at - 1 : at;
            if (end > start)
            {
                closed.push_back(Closed{ at, start, end, open.back().first, open.back().target });
            }
            open.pop_back();
            open_low = std::min(open_low, open.size());
        }
        if (block.event != Event::Close)
        {
            // One opened on a line of its own -- a brace under its header --
            // is told now the line it folds from (aloneTarget).
            const bool alone = block.event == Event::Open && block.first;
            open.push_back(Opened{ at, alone, alone ? aloneTarget(doc, at, open, marked) : at });
        }
    }
}

// static
ALFoldModel::Walked ALFoldModel::placeAt(S32 line, size_t closed, const std::vector<Opened>& open, size_t open_low, const std::vector<S32>& marked, size_t marked_low)
{
    Walked place;
    place.line       = line;
    place.closed     = closed;
    place.open.under = open_low;
    place.open.own.assign(open.begin() + static_cast<std::ptrdiff_t>(open_low), open.end());
    place.marked.under = marked_low;
    place.marked.own.assign(marked.begin() + static_cast<std::ptrdiff_t>(marked_low), marked.end());
    return place;
}

template <typename T>
void ALFoldModel::wholeAt(size_t index, Held<T> Walked::* held, std::vector<T>& out) const
{
    // Back through the places before it, taking of each as much of its own
    // as is wanted over what it holds under it, until nothing under it is
    // wanted; then what was taken, from the first.
    std::vector<std::pair<size_t, size_t>> pieces;
    size_t                                 need = (mWalked[index].*held).under + (mWalked[index].*held).own.size();
    for (size_t k = index;; --k)
    {
        const Held<T>& place = mWalked[k].*held;
        if (need > place.under)
        {
            pieces.emplace_back(k, need - place.under);
        }
        need = std::min(need, place.under);
        if (need == 0 || k == 0)
        {
            break;
        }
    }
    out.clear();
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
    {
        const std::vector<T>& own = (mWalked[it->first].*held).own;
        out.insert(out.end(), own.begin(), own.begin() + static_cast<std::ptrdiff_t>(it->second));
    }
}

// static
template <typename T>
void ALFoldModel::handDown(const Held<T>& gone, Held<T>& next)
{
    // What the next held under its own was the place before's under what
    // that held of its own: that much of its own goes under the next's.
    if (next.under > gone.under)
    {
        next.own.insert(next.own.begin(), gone.own.begin(), gone.own.begin() + static_cast<std::ptrdiff_t>(next.under - gone.under));
        next.under = gone.under;
    }
}

void ALFoldModel::walkSyntax(const ALTextDocument& doc)
{
    const S32 count = doc.lineCount();
    // What is open, each where it opened and whether it opened its line,
    // and the regions marked open; and how far down each the lines walked
    // since the last place kept have reached.
    std::vector<Opened>& open       = mOpen;
    std::vector<S32>&    marked     = mMarked;
    size_t               open_low   = 0;
    size_t               marked_low = 0;
    mSplice.ok                      = false;
    if (!mWalkKept || mWalked.empty() || mWalkLines != count)
    {
        // Whole, from the top.
        open.clear();
        marked.clear();
        mClosed.clear();
        mWalked.clear();
        for (S32 l = 0; l < count; ++l)
        {
            if (l % WALK_STEP == 0)
            {
                if (!mWalked.empty())
                {
                    mWalked.back().open.low   = open_low;
                    mWalked.back().marked.low = marked_low;
                }
                mWalked.push_back(placeAt(l, mClosed.size(), open, open_low, marked, marked_low));
                open_low   = open.size();
                marked_low = marked.size();
            }
            walkLine(doc, lineAt(doc, l), l, open, marked, mClosed, open_low, marked_low);
        }
        mWalked.back().open.low   = open_low;
        mWalked.back().marked.low = marked_low;
        mLastWalked               = count;
    }
    else if (mDirtyFirst > mDirtyLast)
    {
        mLastWalked = 0;
    }
    else
    {
        const S32 first = llclamp(mDirtyFirst, 0, count - 1);
        // And the first line with anything on it after those changed, whose
        // header they may have changed: a brace on a line of its own folds
        // from it (aloneTarget).
        S32 last = llmin(mDirtyLast, count - 1);
        while (last + 1 < count && lineAt(doc, last + 1).indent < 0)
        {
            ++last;
        }
        last              = llmin(last + 1, count - 1);
        const auto byLine = [](S32 line, const Walked& walked) { return line < walked.line; };
        // From the last place kept before the first line changed: every
        // line above it as it was, with what was open there and what it
        // closed before it. What the walk closes from there and the places
        // it keeps go in the place of the old walk's over the same lines.
        const size_t        from         = static_cast<size_t>(std::upper_bound(mWalked.begin(), mWalked.end(), first, byLine) - mWalked.begin()) - 1;
        const size_t        start_closed = mWalked[from].closed;
        std::vector<Closed> fresh;
        std::vector<Walked> places;
        wholeAt(from, &Walked::open, open);
        wholeAt(from, &Walked::marked, marked);
        open_low   = open.size();
        marked_low = marked.size();
        // The last place kept, told how far down its lines reached once
        // they are walked; and a place kept at a line.
        const auto latest = [&]() -> Walked& { return places.empty() ? mWalked[from] : places.back(); };
        const auto settle = [&]() {
            latest().open.low   = open_low;
            latest().marked.low = marked_low;
        };
        const auto keep = [&](S32 line) {
            settle();
            places.push_back(placeAt(line, start_closed + fresh.size(), open, open_low, marked, marked_low));
            open_low   = open.size();
            marked_low = marked.size();
        };
        // What was open at the old places, whole, kept up as the walk passes
        // them in order: each the one before cut to what it holds under its
        // own, and its own over that.
        std::vector<Opened> was_open;
        std::vector<S32>    was_marked;
        size_t              was_at = mWalked.size();
        const auto          wasAt  = [&](size_t index) {
            if (index == was_at)
            {
                return;
            }
            if (was_at < mWalked.size() && index == was_at + 1)
            {
                const Walked& place = mWalked[index];
                was_open.resize(place.open.under);
                was_open.insert(was_open.end(), place.open.own.begin(), place.open.own.end());
                was_marked.resize(place.marked.under);
                was_marked.insert(was_marked.end(), place.marked.own.begin(), place.marked.own.end());
            }
            else
            {
                wholeAt(index, &Walked::open, was_open);
                wholeAt(index, &Walked::marked, was_marked);
            }
            was_at = index;
        };
        // Whether what was open at a place, over how far its lines reached,
        // is what is open over the rest now. Where they closed all of it, a
        // close after that closed nothing, which with more under it now
        // would close that: so then there may be nothing under it.
        const auto tops = [](const auto& now, const auto& was, size_t low) {
            const size_t need = was.size() - low;
            return now.size() >= need && (low > 0 || now.size() == need) &&
                   std::equal(was.begin() + static_cast<std::ptrdiff_t>(low), was.end(), now.end() - static_cast<std::ptrdiff_t>(need));
        };
        // The old places past the last line changed. At one, where the
        // walk finds open what it found there before, it goes on as it went
        // before: what it closed from there is kept, as are those places.
        // Where only what is under what the lines from there to the next
        // place reached differs, they close what they closed before, and
        // what is open after them is what was then over what is under.
        size_t old    = static_cast<size_t>(std::upper_bound(mWalked.begin(), mWalked.end(), last, byLine) - mWalked.begin());
        size_t rejoin = mWalked.size();
        S32    kept   = mWalked[from].line;
        S32    l      = kept;
        S32    lines  = 0;
        bool   done   = false;
        bool   whole  = false;
        while (l < count)
        {
            if (l > last)
            {
                while (old < mWalked.size() && mWalked[old].line < l)
                {
                    ++old;
                }
                if (old < mWalked.size() && mWalked[old].line == l)
                {
                    wasAt(old);
                    const Walked& was = mWalked[old];
                    if (was_open == open && was_marked == marked)
                    {
                        settle();
                        rejoin = old;
                        done   = true;
                        whole  = true;
                        break;
                    }
                    if (latest().line != l)
                    {
                        keep(l);
                        kept = l;
                    }
                    if (tops(open, was_open, was.open.low) && tops(marked, was_marked, was.marked.low))
                    {
                        const size_t next  = old + 1;
                        const size_t until = next < mWalked.size() ? mWalked[next].closed : mClosed.size();
                        fresh.insert(fresh.end(), mClosed.begin() + static_cast<std::ptrdiff_t>(was.closed), mClosed.begin() + static_cast<std::ptrdiff_t>(until));
                        open_low   = open.size() - (was_open.size() - was.open.low);
                        marked_low = marked.size() - (was_marked.size() - was.marked.low);
                        if (next == mWalked.size())
                        {
                            settle();
                            done = true;
                            break;
                        }
                        const size_t open_from   = was.open.low;
                        const size_t marked_from = was.marked.low;
                        wasAt(next);
                        open.resize(open_low);
                        open.insert(open.end(), was_open.begin() + static_cast<std::ptrdiff_t>(open_from), was_open.end());
                        marked.resize(marked_low);
                        marked.insert(marked.end(), was_marked.begin() + static_cast<std::ptrdiff_t>(marked_from), was_marked.end());
                        l    = mWalked[next].line;
                        kept = l;
                        continue;
                    }
                }
            }
            if (l - kept >= WALK_STEP)
            {
                keep(l);
                kept = l;
            }
            walkLine(doc, lineAt(doc, l), l, open, marked, fresh, open_low, marked_low);
            ++lines;
            ++l;
        }
        if (!done)
        {
            settle();
        }
        const size_t         old_closed = rejoin < mWalked.size() ? mWalked[rejoin].closed : mClosed.size();
        const std::ptrdiff_t moved      = static_cast<std::ptrdiff_t>(start_closed + fresh.size()) - static_cast<std::ptrdiff_t>(old_closed);
        // What the blocks are put together from, where they may be from the
        // walk's changes alone: it found open where it rejoined all it found
        // there before, under which is all that was open where it began --
        // nothing open above the lines walked closed among them, then or now.
        // What closed among them, then and now, folds from those lines, or
        // from the header just above the first of them (spliceRegions).
        if (whole)
        {
            // What was open where it began; where it rejoined, what is open
            // now, which is what was open there.
            std::vector<Opened> began_open;
            std::vector<S32>    began_marked;
            wholeAt(from, &Walked::open, began_open);
            wholeAt(from, &Walked::marked, began_marked);
            const auto under = [](const auto& below, const auto& above) {
                return below.size() <= above.size() && std::equal(below.begin(), below.end(), above.begin());
            };
            if (under(began_open, open) && under(began_marked, marked))
            {
                mSplice.ok   = true;
                mSplice.from = mWalked[from].line;
                mSplice.to   = mWalked[rejoin].line;
                mSplice.touched.clear();
                S32 low = mSplice.from;
                for (size_t i = start_closed; i < old_closed; ++i)
                {
                    mSplice.touched.push_back(mClosed[i].target);
                    low = llmin(low, mClosed[i].target);
                }
                for (const Closed& block : fresh)
                {
                    mSplice.touched.push_back(block.target);
                    low = llmin(low, block.target);
                }
                // Of what is open, what folds from a line no lower than
                // any the blocks are put together for: in the order they
                // opened, so of their lines, those opened from the lowest
                // such line on, as nothing folds from below its own line.
                mSplice.kept.clear();
                for (auto it = std::partition_point(open.begin(), open.end(), [low](const Opened& opened) { return opened.line < low; }); it != open.end(); ++it)
                {
                    mSplice.kept.push_back(it->target);
                }
                mSplice.kept.insert(mSplice.kept.end(), std::lower_bound(marked.begin(), marked.end(), low), marked.end());
                // And a brace on a line of its own first after where it
                // rejoined, which was not walked again: it folds from the
                // header above it, among the lines walked, as it did.
                S32 below = mSplice.to;
                while (below < count && lineAt(doc, below).indent < 0)
                {
                    ++below;
                }
                if (below < count)
                {
                    const std::vector<Block>& blocks = lineAt(doc, below).blocks;
                    if (!blocks.empty() && blocks.front().event == Event::Open && blocks.front().first)
                    {
                        S32 header = below - 1;
                        while (header >= 0 && lineAt(doc, header).indent < 0)
                        {
                            --header;
                        }
                        if (header >= 0)
                        {
                            mSplice.kept.push_back(header);
                        }
                    }
                }
                mSplice.freshBegin = start_closed;
                mSplice.freshEnd   = start_closed + fresh.size();
            }
        }
        if (rejoin < mWalked.size())
        {
            // The place it rejoined at holds what is open there over the
            // place now before it.
            Walked& at    = mWalked[rejoin];
            at.open.under = open_low;
            at.open.own.assign(open.begin() + static_cast<std::ptrdiff_t>(open_low), open.end());
            at.marked.under = marked_low;
            at.marked.own.assign(marked.begin() + static_cast<std::ptrdiff_t>(marked_low), marked.end());
        }
        mClosed.erase(mClosed.begin() + static_cast<std::ptrdiff_t>(start_closed), mClosed.begin() + static_cast<std::ptrdiff_t>(old_closed));
        mClosed.insert(mClosed.begin() + static_cast<std::ptrdiff_t>(start_closed), fresh.begin(), fresh.end());
        for (size_t i = rejoin; i < mWalked.size(); ++i)
        {
            mWalked[i].closed = static_cast<size_t>(static_cast<std::ptrdiff_t>(mWalked[i].closed) + moved);
        }
        mWalked.erase(mWalked.begin() + static_cast<std::ptrdiff_t>(from + 1), mWalked.begin() + static_cast<std::ptrdiff_t>(rejoin));
        mWalked.insert(mWalked.begin() + static_cast<std::ptrdiff_t>(from + 1), std::make_move_iterator(places.begin()), std::make_move_iterator(places.end()));
        mLastWalked = lines;
    }
    mWalkKept   = true;
    mWalkLines  = count;
    mDirtyFirst = 0;
    mDirtyLast  = -1;
}

void ALFoldModel::bySyntax(std::vector<S32>& end_of) const
{
    // Each block's end by the line it folds from -- its start, or for one
    // opened on a line of its own its header's, as it was told when opened
    // -- the widest where several fold from a line.
    for (const Closed& block : mClosed)
    {
        end_of[static_cast<size_t>(block.target)] = llmax(end_of[static_cast<size_t>(block.target)], block.end);
    }
}

void ALFoldModel::spliceRegions()
{
    // Every block that folds from a line the walk did not reach is as it
    // was, and so is every one that folds from a line something still open
    // where the walk rejoined folds from: of the blocks that fold from one
    // line and close, the first opened is the last to close, and it was not
    // walked again. The rest that fold from the lines walked are what the
    // walk closed among them.
    const S32  from  = mSplice.from;
    const S32  to    = mSplice.to;
    const auto found = [this](S32 line) {
        const auto it = std::lower_bound(mRegions.begin(), mRegions.end(), line, [](const Region& region, S32 l) { return region.start < l; });
        return it != mRegions.end() && it->start == line ? it : mRegions.end();
    };
    // Of the lines what was still open where the walk rejoined folds from,
    // those where something of it closes after the lines walked: the block
    // found reaches past them, or one closed on the line it rejoined at,
    // reopened there, ends just before it. Where nothing of it ever closes,
    // the line is as any other walked.
    std::vector<S32> kept;
    for (const S32 line : mSplice.kept)
    {
        const auto at    = found(line);
        bool       stays = at != mRegions.end() && at->end >= to;
        for (size_t i = mSplice.freshEnd; !stays && i < mClosed.size() && mClosed[i].line == to; ++i)
        {
            stays = mClosed[i].target == line;
        }
        if (stays)
        {
            kept.push_back(line);
        }
    }
    std::sort(kept.begin(), kept.end());
    kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
    const auto keeps = [&kept](S32 line) { return std::binary_search(kept.begin(), kept.end(), line); };
    // The lines above the walk that what it took out or put in folds from:
    // only the header just above it, which holds no block that is not kept.
    std::vector<S32> above;
    for (const S32 target : mSplice.touched)
    {
        if (target < from && !keeps(target))
        {
            above.push_back(target);
        }
    }
    std::sort(above.begin(), above.end());
    above.erase(std::unique(above.begin(), above.end()), above.end());
    // What closed among the lines walked, by the line it folds from, the
    // widest where several fold from one, but for those kept.
    std::vector<Region> made;
    for (size_t i = mSplice.freshBegin; i < mSplice.freshEnd; ++i)
    {
        const Closed& block = mClosed[i];
        if (!keeps(block.target))
        {
            made.push_back(Region{ block.target, block.end });
        }
    }
    std::sort(made.begin(), made.end(), [](const Region& a, const Region& b) { return a.start < b.start || (a.start == b.start && a.end > b.end); });
    made.erase(std::unique(made.begin(), made.end(), [](const Region& a, const Region& b) { return a.start == b.start; }), made.end());
    made.erase(std::remove_if(made.begin(), made.end(), [](const Region& region) { return region.end <= region.start; }), made.end());
    // In their places: those that folded from the lines walked, but those
    // kept, and from the header above, let go of, and these put in.
    const auto in_place = [&](S32 line) { return ((line >= from && line < to) || std::binary_search(above.begin(), above.end(), line)) && !keeps(line); };
    std::vector<Region> between;
    between.reserve(made.size() + 16);
    const auto first = std::lower_bound(mRegions.begin(), mRegions.end(), above.empty() ? from : llmin(from, above.front()),
                                        [](const Region& region, S32 l) { return region.start < l; });
    const auto past  = std::lower_bound(first, mRegions.end(), to, [](const Region& region, S32 l) { return region.start < l; });
    auto       next  = made.begin();
    for (auto it = first; it != past; ++it)
    {
        if (in_place(it->start))
        {
            continue;
        }
        for (; next != made.end() && next->start < it->start; ++next)
        {
            between.push_back(*next);
        }
        between.push_back(*it);
    }
    between.insert(between.end(), next, made.end());
    const auto at = mRegions.erase(first, past);
    mRegions.insert(at, between.begin(), between.end());
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
    // Lexed through the line before it, so that every line read back from
    // there has been said to where its blocks changed, and none below it.
    if (mLex && line > 0)
    {
        mLex(llmin(line, doc.lineCount()) - 1);
    }
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
    const S32         count  = doc.lineCount();
    std::vector<S32>& indent = mIndents;
    indent.assign(static_cast<size_t>(count), -1);
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
    mRegionsKept = mRegionsKept && mWalkKept;
    if (mWalkKept && !spans.empty())
    {
        // The walk kept moved along with the text: a line after a run by
        // what the runs up to it added or took, one inside a run to the
        // line as far down the run's lines as it was, or their last -- any
        // place, so long as every line kept goes by the same. The lines the
        // runs made are walked again, with those changed before.
        const auto now = [&spans](S32 line) { return movedLine(spans, line); };
        // Where every run made as many lines as it took -- typing on a line
        // -- no line moves. Else those closed in order, so that none closed
        // above the first run moves; past it, with one run -- as nearly
        // every edit has -- by its sums alone.
        const bool same  = std::all_of(spans.begin(), spans.end(), [](const LineSpan& span) { return span.made == span.last - span.first + 1; });
        const auto moves = same ? mClosed.end()
                                : std::lower_bound(mClosed.begin(), mClosed.end(), spans.front().first,
                                                   [](const Closed& block, S32 line) { return block.line < line; });
        if (spans.size() == 1)
        {
            const S32 first = spans.front().first;
            const S32 last  = spans.front().last;
            const S32 most  = first + llmax(spans.front().made, 1) - 1;
            const S32 shift = spans.front().shiftAfter;
            const auto one  = [=](S32 line) { return line > last ? line + shift : line > first ? llmin(line, most) : line; };
            for (auto it = moves; it != mClosed.end(); ++it)
            {
                it->line   = one(it->line);
                it->start  = one(it->start);
                it->end    = one(it->end);
                it->target = one(it->target);
            }
            if (mRegionsKept && !same)
            {
                // The blocks as found move with them, to be put together
                // from the next walk's changes (spliceRegions).
                for (Region& region : mRegions)
                {
                    region.start = one(region.start);
                    region.end   = one(region.end);
                }
            }
        }
        else
        {
            for (auto it = moves; it != mClosed.end(); ++it)
            {
                it->line   = now(it->line);
                it->start  = now(it->start);
                it->end    = now(it->end);
                it->target = now(it->target);
            }
            if (mRegionsKept && !same)
            {
                // The blocks as found move with them, to be put together
                // from the next walk's changes (spliceRegions).
                for (Region& region : mRegions)
                {
                    region.start = now(region.start);
                    region.end   = now(region.end);
                }
            }
        }
        // A place inside a run, after its first line, had lines above it
        // that the run changed: no place to walk again from, nor one the
        // walk is where it was at. What it held under the place after it
        // goes to that one (handDown).
        const auto inside = [&spans](const Walked& walked) {
            const auto after = std::upper_bound(spans.begin(), spans.end(), walked.line, [](S32 at, const LineSpan& span) { return at < span.first; });
            return after != spans.begin() && walked.line > (after - 1)->first && walked.line <= (after - 1)->last;
        };
        size_t places = 0;
        for (size_t i = 0; i < mWalked.size(); ++i)
        {
            if (inside(mWalked[i]))
            {
                if (i + 1 < mWalked.size())
                {
                    handDown(mWalked[i].open, mWalked[i + 1].open);
                    handDown(mWalked[i].marked, mWalked[i + 1].marked);
                }
                continue;
            }
            if (places != i)
            {
                mWalked[places] = std::move(mWalked[i]);
            }
            ++places;
        }
        mWalked.resize(places);
        if (!same)
        {
            // Each place's line, and what it holds of its own: in the order
            // they opened, so of their lines, those at or above the first
            // run's first line stay where they are, as do the lines they
            // fold from, none below their own.
            const S32 first = spans.front().first;
            for (Walked& walked : mWalked)
            {
                walked.line               = now(walked.line);
                std::vector<Opened>& open = walked.open.own;
                for (auto it = std::partition_point(open.begin(), open.end(), [first](const Opened& opened) { return opened.line <= first; }); it != open.end(); ++it)
                {
                    it->line   = now(it->line);
                    it->target = now(it->target);
                }
                std::vector<S32>& marked = walked.marked.own;
                for (auto it = std::upper_bound(marked.begin(), marked.end(), first); it != marked.end(); ++it)
                {
                    *it = now(*it);
                }
            }
        }
        if (mDirtyFirst <= mDirtyLast)
        {
            mDirtyFirst = now(mDirtyFirst);
            mDirtyLast  = now(mDirtyLast);
        }
        S32 before = 0;
        for (const LineSpan& span : spans)
        {
            const S32 first = span.first + before;
            const S32 last  = first + llmax(span.made, 1) - 1;
            mDirtyFirst     = mDirtyFirst > mDirtyLast ? first : llmin(mDirtyFirst, first);
            mDirtyLast      = llmax(mDirtyLast, last);
            before          = span.shiftAfter;
        }
        mWalkLines += spans.back().shiftAfter;
    }
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
                shift = span.shiftAfter;
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

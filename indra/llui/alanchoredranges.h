/**
 * @file alanchoredranges.h
 * @brief Ranges of a text, in order, kept in step with its edits.
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

#ifndef AL_ALANCHOREDRANGES_H
#define AL_ALANCHOREDRANGES_H

#include "altextdocument.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

// Where an element of ALAnchoredRanges is: its `range`, or itself for a
// range or a place.
template <typename T>
struct ALRangeOf
{
    ALTextRange operator()(const T& item) const { return item.range; }
};
template <>
struct ALRangeOf<ALTextRange>
{
    ALTextRange operator()(const ALTextRange& range) const { return range; }
};
template <>
struct ALRangeOf<ALTextPos>
{
    ALTextRange operator()(const ALTextPos& pos) const { return ALTextRange(pos, pos); }
};

// Things laid over a text -- a style, a squiggle, a match, an inlay -- each
// at a range of it, kept in the order they begin and in step with its
// edits. An edit is applied to the few it can touch: those that end before
// it begins are passed over by a search, and where it leaves the lines as
// many as they were, those that begin past its last line are left where
// they are. The rest are asked of by `slide`, which moves each along with
// the text and says whether it stays; the text's own rule
// (ALTextDocument::Edit::slide) where none is given.
//
// The order is the caller's to keep while it holds an element: its range
// may be changed only as far as it stays where it was among the others.
template <typename T, typename RangeOf = ALRangeOf<T>>
class ALAnchoredRanges
{
public:
    typedef std::vector<T>                  items_t;
    typedef typename items_t::iterator       iterator;
    typedef typename items_t::const_iterator const_iterator;

    // Put in the order they begin; those that begin together keep the
    // order given.
    void assign(items_t items)
    {
        mItems = std::move(items);
        std::stable_sort(mItems.begin(), mItems.end(), [](const T& a, const T& b) { return RangeOf()(a).begin < RangeOf()(b).begin; });
        mSpan = 0;
        for (const T& item : mItems)
        {
            widen(item);
        }
    }
    // One more, after those that begin where it does.
    void insert(T item)
    {
        const ALTextPos begin = RangeOf()(item).begin;
        const auto      at    = std::upper_bound(mItems.begin(), mItems.end(), begin, [](const ALTextPos& p, const T& t) { return p < RangeOf()(t).begin; });
        widen(item);
        mItems.insert(at, std::move(item));
    }
    void clear()
    {
        mItems.clear();
        mSpan = 0;
    }
    iterator erase(const_iterator at) { return mItems.erase(at); }
    template <typename Pred>
    size_t eraseIf(Pred pred)
    {
        const size_t before = mItems.size();
        mItems.erase(std::remove_if(mItems.begin(), mItems.end(), pred), mItems.end());
        return before - mItems.size();
    }

    const items_t& items() const { return mItems; }
    size_t         size() const { return mItems.size(); }
    bool           empty() const { return mItems.empty(); }
    const T&       operator[](size_t i) const { return mItems[i]; }
    T&             operator[](size_t i) { return mItems[i]; }
    const_iterator begin() const { return mItems.begin(); }
    const_iterator end() const { return mItems.end(); }
    iterator       begin() { return mItems.begin(); }
    iterator       end() { return mItems.end(); }

    // Those that may lie on a line, in order: from the first that could
    // reach down to it to the last that begins on it. A caller still asks
    // each whether it does, since one that began above may have ended.
    std::pair<const_iterator, const_iterator> onLine(S32 line) const
    {
        const auto first = std::lower_bound(mItems.begin(), mItems.end(), line - mSpan, [](const T& t, S32 l) { return RangeOf()(t).begin.line < l; });
        const auto last  = std::upper_bound(first, mItems.end(), line, [](S32 l, const T& t) { return l < RangeOf()(t).begin.line; });
        return { first, last };
    }

    // An edit applied: the text's own rule, those it cut through let go.
    size_t apply(const ALTextDocument::Edit& edit)
    {
        return apply(edit, [](T& item, const ALTextDocument::Edit& e) { return slideRange(item, e); }, [](T&) {});
    }
    // With a rule of the caller's: `slide` moves an element along and
    // says whether it stays; `dropped` is told of each let go, before it
    // goes. How many went.
    template <typename Slide, typename Dropped>
    size_t apply(const ALTextDocument::Edit& edit, Slide slide, Dropped dropped)
    {
        if (mItems.empty())
        {
            return 0;
        }
        const ALTextRange removed = edit.range.normalised();
        const S32         delta   = edit.endAfter().line - removed.end.line;
        // Nothing that ends before the edit begins is touched, and none
        // that begins more lines above it than the widest spans does.
        size_t i = static_cast<size_t>(onLine(removed.begin.line).first - mItems.cbegin());
        const size_t start = i;
        size_t       kept  = i;
        const size_t count = mItems.size();
        for (; i < count; ++i)
        {
            T& item = mItems[i];
            if (delta == 0 && RangeOf()(item).begin.line > removed.end.line)
            {
                // Past its last line, the lines keeping their numbers.
                break;
            }
            if (slide(item, edit))
            {
                if (kept != i)
                {
                    mItems[kept] = std::move(item);
                }
                ++kept;
            }
            else
            {
                dropped(item);
            }
        }
        const size_t gone = i - kept;
        if (gone > 0)
        {
            std::move(mItems.begin() + static_cast<std::ptrdiff_t>(i), mItems.end(), mItems.begin() + static_cast<std::ptrdiff_t>(kept));
            mItems.resize(count - gone);
        }
        // A rule of a caller's may part two that began together -- one
        // left before what is typed at it, one moved past -- so the order
        // is looked at where anything moved.
        const auto moved_begin = mItems.begin() + static_cast<std::ptrdiff_t>(start);
        const auto moved_end   = mItems.begin() + static_cast<std::ptrdiff_t>(kept);
        const auto by_begin    = [](const T& a, const T& b) { return RangeOf()(a).begin < RangeOf()(b).begin; };
        if (!std::is_sorted(moved_begin, moved_end, by_begin))
        {
            std::stable_sort(moved_begin, moved_end, by_begin);
        }
        return gone;
    }

private:
    // The text's rule, for an element that is its range or has one.
    static bool slideRange(T& item, const ALTextDocument::Edit& edit)
    {
        if constexpr (std::is_same_v<T, ALTextRange>)
        {
            return edit.slide(item);
        }
        else if constexpr (std::is_same_v<T, ALTextPos>)
        {
            ALTextRange at(item, item);
            const bool  stays = edit.slide(at);
            item              = at.begin;
            return stays;
        }
        else
        {
            return edit.slide(item.range);
        }
    }

    void widen(const T& item)
    {
        const ALTextRange r = RangeOf()(item).normalised();
        mSpan               = std::max(mSpan, r.end.line - r.begin.line);
    }

    items_t mItems;
    // The most lines any of them spans: how far above a line to look for
    // one that reaches it.
    S32     mSpan = 0;
};

#endif // AL_ALANCHOREDRANGES_H

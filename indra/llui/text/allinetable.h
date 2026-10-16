/**
 * @file allinetable.h
 * @brief One row per line of a text, kept in step with its edits.
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

#ifndef AL_ALLINETABLE_H
#define AL_ALLINETABLE_H

#include "stdtypes.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

// A row for each line of a text -- the text's own lines, their layout, their
// tokens, a mark in the gutter -- kept in step with its edits. An edit
// replaces the lines from its first to its last with the lines it makes.
// The rows are assigned in place where it makes as many as it replaced,
// which is every edit within a line. Otherwise they go in or out at a gap of
// spare rows the table keeps where the last such edit was, which moves to
// the next one past the rows between the two: a line broken or joined where
// the one before was moves no row at all, and one further down the text
// moves only the rows between, rather than every row below it.
template <typename T>
class ALLineTable
{
public:
    typedef std::vector<T> rows_t;

    // The rows in order, the gap passed over.
    template <typename Table, typename V>
    class Iter
    {
    public:
        typedef std::random_access_iterator_tag iterator_category;
        typedef T                               value_type;
        typedef std::ptrdiff_t                  difference_type;
        typedef V*                              pointer;
        typedef V&                              reference;

        Iter() = default;
        Iter(Table* table, size_t at) : mTable(table), mAt(at) {}
        // A const one from one that is not.
        template <typename OtherTable, typename OtherV>
        Iter(const Iter<OtherTable, OtherV>& other) : mTable(other.mTable), mAt(other.mAt)
        {
        }

        reference operator*() const { return (*mTable)[mAt]; }
        pointer   operator->() const { return &(*mTable)[mAt]; }
        reference operator[](difference_type n) const { return (*mTable)[static_cast<size_t>(static_cast<difference_type>(mAt) + n)]; }

        Iter& operator++()
        {
            ++mAt;
            return *this;
        }
        Iter operator++(int)
        {
            Iter was = *this;
            ++mAt;
            return was;
        }
        Iter& operator--()
        {
            --mAt;
            return *this;
        }
        Iter operator--(int)
        {
            Iter was = *this;
            --mAt;
            return was;
        }
        Iter& operator+=(difference_type n)
        {
            mAt = static_cast<size_t>(static_cast<difference_type>(mAt) + n);
            return *this;
        }
        Iter& operator-=(difference_type n) { return *this += -n; }
        friend Iter operator+(Iter it, difference_type n) { return it += n; }
        friend Iter operator+(difference_type n, Iter it) { return it += n; }
        friend Iter operator-(Iter it, difference_type n) { return it -= n; }
        friend difference_type operator-(const Iter& a, const Iter& b) { return static_cast<difference_type>(a.mAt) - static_cast<difference_type>(b.mAt); }
        friend bool operator==(const Iter& a, const Iter& b) { return a.mAt == b.mAt; }
        friend bool operator!=(const Iter& a, const Iter& b) { return a.mAt != b.mAt; }
        friend bool operator<(const Iter& a, const Iter& b) { return a.mAt < b.mAt; }
        friend bool operator>(const Iter& a, const Iter& b) { return a.mAt > b.mAt; }
        friend bool operator<=(const Iter& a, const Iter& b) { return a.mAt <= b.mAt; }
        friend bool operator>=(const Iter& a, const Iter& b) { return a.mAt >= b.mAt; }

    private:
        template <typename, typename>
        friend class Iter;
        Table* mTable = nullptr;
        size_t mAt    = 0;
    };
    typedef Iter<ALLineTable, T>             iterator;
    typedef Iter<const ALLineTable, const T> const_iterator;

    ALLineTable() = default;
    explicit ALLineTable(size_t n, const T& fill = T()) : mRows(n, fill) {}

    // What an edit did to the table: the first row it replaced, how many
    // of the rows it held it replaced, and how many it made.
    struct Replaced
    {
        S32 first = 0;
        S32 count = 0;
        S32 made  = 0;
    };

    // An edit from line `first` to line `last` that made `made` lines, the
    // text then `line_count` long: the rows it replaced taken out and
    // `made` rows of `fill` in their place. Rows the table did not yet
    // have, up to the edit's last line and past it to the text's end, are
    // `other` -- a table kept only as far as it has been told of lines. A
    // `line_count` below 0 leaves the table as long as the edit makes it.
    Replaced apply(S32 first, S32 last, S32 made, S32 line_count, const T& fill, const T& other = T())
    {
        Replaced out;
        if (first > static_cast<S32>(size()))
        {
            resize(static_cast<size_t>(first), other);
        }
        const S32 rows = static_cast<S32>(size());
        out.first      = std::clamp(first, 0, rows);
        out.count      = out.first < rows ? std::clamp(last, out.first, rows - 1) - out.first + 1 : 0;
        out.made       = std::max(made, 0);
        replace(static_cast<size_t>(out.first), static_cast<size_t>(out.count), static_cast<size_t>(out.made), fill);
        if (line_count >= 0)
        {
            resize(static_cast<size_t>(line_count), other);
        }
        return out;
    }

    // Several runs of lines replaced at once, in order and apart, each its
    // `first` and `last` line as they were and the lines it `made` -- a
    // batch's (ALTextDocument::Edit::lineSpans): the rows between them
    // kept, the rows each made `fill`. From the last run up, so that each
    // run's lines are still where they were, the gap moving only from the
    // last run to the first.
    template <typename Spans>
    void applySpans(const Spans& spans, S32 line_count, const T& fill, const T& other = T())
    {
        if (spans.empty())
        {
            return;
        }
        if (spans.size() == 1)
        {
            apply(spans.front().first, spans.front().last, spans.front().made, line_count, fill, other);
            return;
        }
        // A table kept only as far as it was told of lines, as far as the
        // last run reaches.
        if (static_cast<S32>(size()) <= spans.back().last)
        {
            resize(static_cast<size_t>(spans.back().last + 1), other);
        }
        for (auto s = spans.rbegin(); s != spans.rend(); ++s)
        {
            replace(static_cast<size_t>(s->first), static_cast<size_t>(s->last - s->first + 1), static_cast<size_t>(std::max(s->made, 0)), fill);
        }
        if (line_count >= 0)
        {
            resize(static_cast<size_t>(line_count), other);
        }
    }

    // `count` rows from `first` replaced by `made` rows of `fill`.
    void replace(size_t first, size_t count, size_t made, const T& fill)
    {
        first             = std::min(first, size());
        count             = std::min(count, size() - first);
        const size_t same = std::min(count, made);
        stretches(first, same, [&fill](auto at, size_t n) { std::fill(at, at + static_cast<std::ptrdiff_t>(n), fill); });
        if (made > count)
        {
            // Into the gap as far as it reaches, the rest put in straight.
            const size_t more = made - count;
            moveGap(first + count);
            const size_t into = std::min(more, mGapLen);
            std::fill(gap(), gap() + static_cast<std::ptrdiff_t>(into), fill);
            mGapAt += into;
            mGapLen -= into;
            if (more > into)
            {
                if (widens(more - into))
                {
                    std::fill(gap(), gap() + static_cast<std::ptrdiff_t>(more - into), fill);
                    mGapLen -= more - into;
                }
                else
                {
                    mRows.insert(gap(), more - into, fill);
                }
                mGapAt += more - into;
            }
        }
        else if (count > made)
        {
            take(first + made, count - made);
        }
    }

    // `count` rows from `first` replaced by the rows from `begin` to
    // `end`, moved in where they are moved from.
    template <typename It>
    void replace(size_t first, size_t count, It begin, It end)
    {
        first             = std::min(first, size());
        count             = std::min(count, size() - first);
        const size_t made = static_cast<size_t>(std::distance(begin, end));
        const size_t same = std::min(count, made);
        It           from = begin;
        stretches(first, same, [&from](auto at, size_t n) {
            for (size_t i = 0; i < n; ++i, ++from)
            {
                at[static_cast<std::ptrdiff_t>(i)] = *from;
            }
        });
        if (made > count)
        {
            const size_t more = made - count;
            moveGap(first + count);
            const size_t into = std::min(more, mGapLen);
            for (size_t i = 0; i < into; ++i, ++from)
            {
                mRows[mGapAt + i] = *from;
            }
            mGapAt += into;
            mGapLen -= into;
            if (more > into)
            {
                if (widens(more - into))
                {
                    for (size_t i = 0; i < more - into; ++i, ++from)
                    {
                        mRows[mGapAt + i] = *from;
                    }
                    mGapLen -= more - into;
                }
                else
                {
                    mRows.insert(gap(), from, end);
                }
                mGapAt += more - into;
            }
        }
        else if (count > made)
        {
            take(first + made, count - made);
        }
    }

    size_t   size() const { return mRows.size() - mGapLen; }
    bool     empty() const { return size() == 0; }
    T&       operator[](size_t i) { return mRows[i < mGapAt ? i : i + mGapLen]; }
    const T& operator[](size_t i) const { return mRows[i < mGapAt ? i : i + mGapLen]; }
    T&       front() { return (*this)[0]; }
    const T& front() const { return (*this)[0]; }
    T&       back() { return (*this)[size() - 1]; }
    const T& back() const { return (*this)[size() - 1]; }

    iterator       begin() { return iterator(this, 0); }
    iterator       end() { return iterator(this, size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }

    // The rows as a whole, as any table's; the gap closed first. Rows put
    // on or taken off at the end leave it where it is.
    void clear()
    {
        mRows.clear();
        mGapAt  = 0;
        mGapLen = 0;
    }
    void resize(size_t n) { resize(n, T()); }
    void resize(size_t n, const T& fill)
    {
        if (n >= size())
        {
            mRows.resize(mRows.size() + (n - size()), fill);
        }
        else if (n >= mGapAt)
        {
            mRows.resize(n + mGapLen);
        }
        else
        {
            mRows.resize(n);
            mGapAt  = 0;
            mGapLen = 0;
        }
    }
    void assign(size_t n, const T& fill)
    {
        clear();
        mRows.assign(n, fill);
    }
    template <typename It>
    void assign(It begin, It end)
    {
        clear();
        mRows.assign(begin, end);
    }
    void push_back(const T& row) { mRows.push_back(row); }
    void push_back(T&& row) { mRows.push_back(std::move(row)); }
    void reserve(size_t n) { mRows.reserve(n + mGapLen); }
    void swap(rows_t& other)
    {
        close();
        mRows.swap(other);
    }

private:
    // The gap moved to start before row `at`, past the rows between: those
    // it passes go to its other side.
    void moveGap(size_t at)
    {
        // No gap: none to move, and no row to move onto itself.
        const auto rows = mRows.begin();
        if (mGapLen == 0)
        {
            mGapAt = at;
            return;
        }
        if (at < mGapAt)
        {
            std::move_backward(rows + static_cast<std::ptrdiff_t>(at), rows + static_cast<std::ptrdiff_t>(mGapAt), rows + static_cast<std::ptrdiff_t>(mGapAt + mGapLen));
        }
        else if (at > mGapAt)
        {
            std::move(rows + static_cast<std::ptrdiff_t>(mGapAt + mGapLen), rows + static_cast<std::ptrdiff_t>(at + mGapLen), rows + static_cast<std::ptrdiff_t>(mGapAt));
        }
        mGapAt = at;
    }
    // Where the gap starts in the vector.
    typename rows_t::iterator gap() { return mRows.begin() + static_cast<std::ptrdiff_t>(mGapAt); }
    // The rows from `first`, `count` of them, as the stretches of the
    // vector they are -- before the gap, after it, or both -- each handed
    // to `run` with how many it holds.
    template <typename Run>
    void stretches(size_t first, size_t count, Run&& run)
    {
        const size_t end = first + count;
        if (first < mGapAt && first < end)
        {
            run(mRows.begin() + static_cast<std::ptrdiff_t>(first), std::min(end, mGapAt) - first);
        }
        if (end > mGapAt)
        {
            const size_t from = std::max(first, mGapAt);
            run(mRows.begin() + static_cast<std::ptrdiff_t>(from + mGapLen), end - from);
        }
    }
    // The gap, all used, widened for `count` more rows where they are a
    // few -- by an eighth of the table besides, so that a run of lines put
    // in one after another widens it seldom -- and true; false for more, a
    // paste of thousands or the text set, which the caller puts in straight.
    bool widens(size_t count)
    {
        const size_t spare = std::max<size_t>(size() / 8, 64);
        if (count >= spare)
        {
            return false;
        }
        mRows.insert(gap(), count + spare, T());
        mGapLen = count + spare;
        return true;
    }
    // `count` rows from `at` taken into the gap, let go of what they held.
    // A gap left far wider than the rows -- most of the text taken out --
    // is closed, its rows gone, as a vector's would be.
    void take(size_t at, size_t count)
    {
        moveGap(at + count);
        mGapAt -= count;
        mGapLen += count;
        std::fill(gap(), gap() + static_cast<std::ptrdiff_t>(count), T());
        if (mGapLen > 2 * size() + 1024)
        {
            close();
        }
    }
    // The gap gone: the rows after it moved up to close it.
    void close()
    {
        if (mGapLen > 0)
        {
            moveGap(size());
            mRows.resize(mGapAt);
            mGapLen = 0;
        }
    }

    // The rows, the gap's among them from mGapAt, mGapLen of them.
    rows_t mRows;
    size_t mGapAt  = 0;
    size_t mGapLen = 0;
};

#endif // AL_ALLINETABLE_H

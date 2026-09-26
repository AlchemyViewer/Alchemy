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
#include <iterator>
#include <vector>

// A row for each line of a text -- the text's own lines, their layout, their
// tokens, a mark in the gutter -- kept in step with its edits. An edit
// replaces the lines from its first to its last with the lines it makes.
// The rows are assigned in place where it makes as many as it replaced,
// which is every edit within a line; otherwise the rows below move once,
// not once to take the old lines out and again to put the new ones in.
template <typename T>
class ALLineTable
{
public:
    typedef std::vector<T>                   rows_t;
    typedef typename rows_t::iterator        iterator;
    typedef typename rows_t::const_iterator  const_iterator;

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
        if (first > static_cast<S32>(mRows.size()))
        {
            mRows.resize(static_cast<size_t>(first), other);
        }
        const S32 size = static_cast<S32>(mRows.size());
        out.first      = std::clamp(first, 0, size);
        out.count      = out.first < size ? std::clamp(last, out.first, size - 1) - out.first + 1 : 0;
        out.made       = std::max(made, 0);
        replace(static_cast<size_t>(out.first), static_cast<size_t>(out.count), static_cast<size_t>(out.made), fill);
        if (line_count >= 0)
        {
            mRows.resize(static_cast<size_t>(line_count), other);
        }
        return out;
    }

    // `count` rows from `first` replaced by `made` rows of `fill`.
    void replace(size_t first, size_t count, size_t made, const T& fill)
    {
        first = std::min(first, mRows.size());
        count = std::min(count, mRows.size() - first);
        const size_t same = std::min(count, made);
        std::fill(mRows.begin() + first, mRows.begin() + first + same, fill);
        if (made > count)
        {
            mRows.insert(mRows.begin() + first + count, made - count, fill);
        }
        else if (count > made)
        {
            mRows.erase(mRows.begin() + first + made, mRows.begin() + first + count);
        }
    }

    // `count` rows from `first` replaced by the rows from `begin` to
    // `end`, moved in where they are moved from.
    template <typename Iter>
    void replace(size_t first, size_t count, Iter begin, Iter end)
    {
        first             = std::min(first, mRows.size());
        count             = std::min(count, mRows.size() - first);
        const size_t made = static_cast<size_t>(std::distance(begin, end));
        const size_t same = std::min(count, made);
        Iter         from = begin;
        for (size_t i = 0; i < same; ++i, ++from)
        {
            mRows[first + i] = *from;
        }
        if (made > count)
        {
            mRows.insert(mRows.begin() + first + count, from, end);
        }
        else if (count > made)
        {
            mRows.erase(mRows.begin() + first + made, mRows.begin() + first + count);
        }
    }

    // The rows as a vector reads them.
    const rows_t& rows() const { return mRows; }
    size_t        size() const { return mRows.size(); }
    bool          empty() const { return mRows.empty(); }
    T&            operator[](size_t i) { return mRows[i]; }
    const T&      operator[](size_t i) const { return mRows[i]; }
    T&            front() { return mRows.front(); }
    const T&      front() const { return mRows.front(); }
    T&            back() { return mRows.back(); }
    const T&      back() const { return mRows.back(); }
    iterator       begin() { return mRows.begin(); }
    iterator       end() { return mRows.end(); }
    const_iterator begin() const { return mRows.begin(); }
    const_iterator end() const { return mRows.end(); }

    // The rows as a whole, as any table's.
    void clear() { mRows.clear(); }
    void resize(size_t n) { mRows.resize(n); }
    void resize(size_t n, const T& fill) { mRows.resize(n, fill); }
    void assign(size_t n, const T& fill) { mRows.assign(n, fill); }
    template <typename Iter>
    void assign(Iter begin, Iter end)
    {
        mRows.assign(begin, end);
    }
    void push_back(const T& row) { mRows.push_back(row); }
    void push_back(T&& row) { mRows.push_back(std::move(row)); }
    void reserve(size_t n) { mRows.reserve(n); }
    void swap(rows_t& other) { mRows.swap(other); }

private:
    rows_t mRows;
};

#endif // AL_ALLINETABLE_H

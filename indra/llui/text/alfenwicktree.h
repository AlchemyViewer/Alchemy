/**
 * @file alfenwicktree.h
 * @brief Sums of a run of numbers that change one at a time.
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

#ifndef AL_ALFENWICKTREE_H
#define AL_ALFENWICKTREE_H

#include <cstddef>
#include <vector>

// A run of numbers kept so that any one of them may change, the sum of
// those before any place be found, and the place a sum reaches be found,
// each in time that grows as the log of how many there are: a Fenwick
// tree. The heights of a document's lines, say, where one line laid out
// again changes its own and the top of every line after it.
template <typename T>
class ALFenwickTree
{
public:
    // Made again from these, in time that grows as how many there are.
    void assign(const std::vector<T>& values)
    {
        mValues = values;
        mTree.assign(values.size() + 1, T());
        for (size_t i = 1; i <= values.size(); ++i)
        {
            mTree[i] += values[i - 1];
            const size_t up = i + (i & (~i + 1));
            if (up <= values.size())
            {
                mTree[up] += mTree[i];
            }
        }
    }

    size_t size() const { return mValues.size(); }
    bool   empty() const { return mValues.empty(); }
    T      at(size_t index) const { return mValues[index]; }

    void set(size_t index, T value)
    {
        const T change = value - mValues[index];
        if (change == T())
        {
            return;
        }
        mValues[index] = value;
        for (size_t i = index + 1; i < mTree.size(); i += i & (~i + 1))
        {
            mTree[i] += change;
        }
    }

    // The sum of those before `index`; of them all at size().
    T before(size_t index) const
    {
        T sum = T();
        for (size_t i = index; i > 0; i -= i & (~i + 1))
        {
            sum += mTree[i];
        }
        return sum;
    }
    T total() const { return before(mValues.size()); }

    // The most of them from the start whose sum is no more than `sum`,
    // where none is less than nothing: the index of the one `sum` falls
    // in, or size() past them all. Of several that sum to nothing in a
    // row, past the last of them.
    size_t reach(T sum) const
    {
        size_t at   = 0;
        size_t step = 1;
        while (step * 2 <= mValues.size())
        {
            step *= 2;
        }
        for (; step > 0; step /= 2)
        {
            if (at + step <= mValues.size() && mTree[at + step] <= sum)
            {
                at += step;
                sum -= mTree[at];
            }
        }
        return at;
    }

private:
    std::vector<T> mValues;
    // One-based: mTree[i] sums the (i & -i) values ending at the i-th.
    std::vector<T> mTree;
};

#endif // AL_ALFENWICKTREE_H

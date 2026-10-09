/**
 * @file alprefixsums.h
 * @brief A run of numbers summed before any place, which numbers go into and out of anywhere.
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

#ifndef AL_ALPREFIXSUMS_H
#define AL_ALPREFIXSUMS_H

#include "alfenwicktree.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

// A run of numbers, none less than nothing, kept so that any one of them may
// change, the sum of those before any place be found, and the place a sum
// reaches be found -- as a Fenwick tree keeps them -- and numbers go in and
// out anywhere besides: the heights of a document's lines, say, where a line
// put in or taken out moves the top of every line after it. They are kept
// in blocks of a hundred or so, each block as the sums through each of its
// numbers, with a Fenwick tree over the blocks' sums: a number put in or
// taken out costs its block and the tree over the blocks made again, not
// every number summed again, and a sum is looked for in the tree and then
// in its block, in time that grows as the log of how many there are.
template <typename T>
class ALPrefixSums
{
public:
    // Made again from these.
    void assign(const std::vector<T>& values)
    {
        mBlocks.clear();
        for (size_t at = 0; at < values.size(); at += BLOCK)
        {
            mBlocks.emplace_back();
            summed(mBlocks.back(), values.begin() + static_cast<std::ptrdiff_t>(at), values.begin() + static_cast<std::ptrdiff_t>(std::min(at + BLOCK, values.size())));
        }
        reindex();
    }

    size_t size() const { return mSize; }
    bool   empty() const { return mSize == 0; }
    T      at(size_t index) const
    {
        const auto [block, in] = locate(index);
        const std::vector<T>& sums = mBlocks[block];
        return sums[in + 1] - sums[in];
    }

    void set(size_t index, T value)
    {
        const auto [block, in] = locate(index);
        std::vector<T>& sums   = mBlocks[block];
        const T         change = value - (sums[in + 1] - sums[in]);
        if (change == T())
        {
            return;
        }
        for (size_t i = in + 1; i < sums.size(); ++i)
        {
            sums[i] += change;
        }
        mTree.set(block, sums.back());
    }

    // The sum of those before `index`; of them all at size().
    T before(size_t index) const
    {
        if (index >= mSize)
        {
            return total();
        }
        const auto [block, in] = locate(index);
        return mTree.before(block) + mBlocks[block][in];
    }
    T total() const { return mTree.total(); }

    // The most of them from the start whose sum is no more than `sum`: the
    // index of the one `sum` falls in, or size() past them all. Of several
    // that sum to nothing in a row, past the last of them.
    size_t reach(T sum) const
    {
        const size_t block = mTree.reach(sum);
        if (block >= mBlocks.size())
        {
            return mSize;
        }
        const std::vector<T>& sums = mBlocks[block];
        const T               left = sum - mTree.before(block);
        const size_t          in   = static_cast<size_t>(std::upper_bound(sums.begin(), sums.end(), left) - sums.begin());
        return mStarts[block] + (in > 0 ? in - 1 : 0);
    }

    // These put in before `index`, or after the last at size().
    void insert(size_t index, const std::vector<T>& values)
    {
        if (values.empty())
        {
            return;
        }
        if (mBlocks.empty())
        {
            assign(values);
            return;
        }
        // Into the block the place is in, or the last past them all; one
        // grown too long is cut into blocks again.
        size_t block = mBlocks.size() - 1;
        size_t in    = mBlocks.back().size() - 1;
        if (index < mSize)
        {
            const auto found = locate(index);
            block            = found.first;
            in               = found.second;
        }
        std::vector<T> whole = valuesOf(mBlocks[block]);
        whole.insert(whole.begin() + static_cast<std::ptrdiff_t>(in), values.begin(), values.end());
        std::vector<std::vector<T>> cut;
        for (size_t at = 0; at < whole.size(); at += whole.size() > 2 * BLOCK ? BLOCK : whole.size())
        {
            cut.emplace_back();
            summed(cut.back(), whole.begin() + static_cast<std::ptrdiff_t>(at), whole.begin() + static_cast<std::ptrdiff_t>(std::min(at + (whole.size() > 2 * BLOCK ? BLOCK : whole.size()), whole.size())));
        }
        mBlocks.erase(mBlocks.begin() + static_cast<std::ptrdiff_t>(block));
        mBlocks.insert(mBlocks.begin() + static_cast<std::ptrdiff_t>(block), std::make_move_iterator(cut.begin()), std::make_move_iterator(cut.end()));
        reindex();
    }

    // `count` of them from `index` taken out. A block left empty goes, and
    // one left short joins the one after it, or before it at the end.
    void erase(size_t index, size_t count)
    {
        count = std::min(count, mSize - std::min(index, mSize));
        if (count == 0)
        {
            return;
        }
        auto [block, in] = locate(index);
        size_t left      = count;
        while (left > 0)
        {
            std::vector<T> values = valuesOf(mBlocks[block]);
            const size_t   taken  = std::min(left, values.size() - in);
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(in), values.begin() + static_cast<std::ptrdiff_t>(in + taken));
            left -= taken;
            if (values.empty())
            {
                mBlocks.erase(mBlocks.begin() + static_cast<std::ptrdiff_t>(block));
            }
            else
            {
                summed(mBlocks[block], values.begin(), values.end());
                ++block;
            }
            in = 0;
        }
        reindex();
        // The block the place is in now, the last where it is past them.
        if (mBlocks.size() < 2)
        {
            return;
        }
        const size_t ended = locate(std::min(index, mSize - 1)).first;
        if (mBlocks[ended].size() - 1 >= BLOCK / 4)
        {
            return;
        }
        const size_t keep = ended + 1 < mBlocks.size() ? ended : ended - 1;
        if (mBlocks[keep].size() + mBlocks[keep + 1].size() - 2 <= 2 * BLOCK)
        {
            std::vector<T> values = valuesOf(mBlocks[keep]);
            std::vector<T> next   = valuesOf(mBlocks[keep + 1]);
            values.insert(values.end(), next.begin(), next.end());
            summed(mBlocks[keep], values.begin(), values.end());
            mBlocks.erase(mBlocks.begin() + static_cast<std::ptrdiff_t>(keep + 1));
            reindex();
        }
    }

private:
    static constexpr size_t BLOCK = 128;

    // A block as the sums through each of these: one more than there are,
    // the first nothing and the last their sum.
    template <typename It>
    static void summed(std::vector<T>& sums, It begin, It end)
    {
        sums.assign(1, T());
        sums.reserve(static_cast<size_t>(std::distance(begin, end)) + 1);
        for (It it = begin; it != end; ++it)
        {
            sums.push_back(sums.back() + *it);
        }
    }
    static std::vector<T> valuesOf(const std::vector<T>& sums)
    {
        std::vector<T> values;
        values.reserve(sums.size() - 1);
        for (size_t i = 1; i < sums.size(); ++i)
        {
            values.push_back(sums[i] - sums[i - 1]);
        }
        return values;
    }
    // The block a place is in, and where in it.
    std::pair<size_t, size_t> locate(size_t index) const
    {
        const size_t block = static_cast<size_t>(std::upper_bound(mStarts.begin(), mStarts.end(), index) - mStarts.begin()) - 1;
        return { block, index - mStarts[block] };
    }
    // Where each block starts, and the tree over their sums, made again
    // from the blocks: as many as there are blocks, not numbers.
    void reindex()
    {
        mStarts.resize(mBlocks.size());
        mBlockSums.resize(mBlocks.size());
        size_t at = 0;
        for (size_t block = 0; block < mBlocks.size(); ++block)
        {
            mStarts[block]    = at;
            mBlockSums[block] = mBlocks[block].back();
            at += mBlocks[block].size() - 1;
        }
        mSize = at;
        mTree.assign(mBlockSums);
    }

    std::vector<std::vector<T>> mBlocks;
    std::vector<size_t>         mStarts;
    std::vector<T>              mBlockSums;
    ALFenwickTree<T>            mTree;
    size_t                      mSize = 0;
};

#endif // AL_ALPREFIXSUMS_H

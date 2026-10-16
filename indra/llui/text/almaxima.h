/**
 * @file almaxima.h
 * @brief A run of numbers whose greatest is kept, which numbers go into and out of anywhere.
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

#ifndef AL_ALMAXIMA_H
#define AL_ALMAXIMA_H

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

// A run of numbers, kept so that any one of them may change and the greatest
// of them all be known, and numbers go in and out anywhere besides: the
// widths of a document's lines, say, where the widest cut short has another
// be the widest. They are kept in blocks of a hundred or so, each with its
// greatest: a number changed costs its block's greatest found again where it
// was that and is less now, and the greatest of all found again over the
// blocks' where that block's was it -- not every number looked at again.
template <typename T>
class ALMaxima
{
public:
    // Made again from these.
    void assign(const std::vector<T>& values)
    {
        mBlocks.clear();
        for (size_t at = 0; at < values.size(); at += BLOCK)
        {
            mBlocks.emplace_back(values.begin() + static_cast<std::ptrdiff_t>(at), values.begin() + static_cast<std::ptrdiff_t>(std::min(at + BLOCK, values.size())));
        }
        reindex();
    }

    size_t size() const { return mSize; }
    bool   empty() const { return mSize == 0; }
    T      at(size_t index) const
    {
        const auto [block, in] = locate(index);
        return mBlocks[block].values[in];
    }

    // The greatest of them; nothing where there are none.
    T most() const { return mMost; }

    void set(size_t index, T value)
    {
        const auto [block, in] = locate(index);
        Block&  b   = mBlocks[block];
        const T was = b.values[in];
        if (value == was)
        {
            return;
        }
        b.values[in]     = value;
        const T was_most = b.most;
        if (value > b.most)
        {
            b.most = value;
        }
        else if (was == b.most)
        {
            b.most = mostOf(b.values);
        }
        if (b.most > mMost)
        {
            mMost = b.most;
        }
        else if (was_most == mMost && b.most < was_most)
        {
            mMost = mostOfBlocks();
        }
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
        size_t in    = mBlocks.back().values.size();
        if (index < mSize)
        {
            const auto found = locate(index);
            block            = found.first;
            in               = found.second;
        }
        std::vector<T> whole = std::move(mBlocks[block].values);
        whole.insert(whole.begin() + static_cast<std::ptrdiff_t>(in), values.begin(), values.end());
        const size_t        step = whole.size() > 2 * BLOCK ? BLOCK : whole.size();
        std::vector<Block> cut;
        for (size_t at = 0; at < whole.size(); at += step)
        {
            cut.emplace_back(whole.begin() + static_cast<std::ptrdiff_t>(at), whole.begin() + static_cast<std::ptrdiff_t>(std::min(at + step, whole.size())));
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
            std::vector<T>& values = mBlocks[block].values;
            const size_t    taken  = std::min(left, values.size() - in);
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(in), values.begin() + static_cast<std::ptrdiff_t>(in + taken));
            left -= taken;
            if (values.empty())
            {
                mBlocks.erase(mBlocks.begin() + static_cast<std::ptrdiff_t>(block));
            }
            else
            {
                mBlocks[block].most = mostOf(values);
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
        if (mBlocks[ended].values.size() >= BLOCK / 4)
        {
            return;
        }
        const size_t keep = ended + 1 < mBlocks.size() ? ended : ended - 1;
        if (mBlocks[keep].values.size() + mBlocks[keep + 1].values.size() <= 2 * BLOCK)
        {
            std::vector<T>&       values = mBlocks[keep].values;
            const std::vector<T>& next   = mBlocks[keep + 1].values;
            values.insert(values.end(), next.begin(), next.end());
            mBlocks[keep].most = std::max(mBlocks[keep].most, mBlocks[keep + 1].most);
            mBlocks.erase(mBlocks.begin() + static_cast<std::ptrdiff_t>(keep + 1));
            reindex();
        }
    }

private:
    static constexpr size_t BLOCK = 128;

    struct Block
    {
        template <typename It>
        Block(It begin, It end) : values(begin, end), most(mostOf(values))
        {
        }
        std::vector<T> values;
        T              most;
    };

    // The greatest of some, never none.
    static T mostOf(const std::vector<T>& values) { return *std::max_element(values.begin(), values.end()); }
    T        mostOfBlocks() const
    {
        T most = mBlocks.empty() ? T() : mBlocks.front().most;
        for (const Block& block : mBlocks)
        {
            most = std::max(most, block.most);
        }
        return most;
    }
    // The block a place is in, and where in it.
    std::pair<size_t, size_t> locate(size_t index) const
    {
        const size_t block = static_cast<size_t>(std::upper_bound(mStarts.begin(), mStarts.end(), index) - mStarts.begin()) - 1;
        return { block, index - mStarts[block] };
    }
    // Where each block starts, and the greatest of all, found again from the
    // blocks: as many as there are blocks, not numbers.
    void reindex()
    {
        mStarts.resize(mBlocks.size());
        size_t at = 0;
        for (size_t block = 0; block < mBlocks.size(); ++block)
        {
            mStarts[block] = at;
            at += mBlocks[block].values.size();
        }
        mSize = at;
        mMost = mostOfBlocks();
    }

    std::vector<Block>  mBlocks;
    std::vector<size_t> mStarts;
    T                   mMost = T();
    size_t              mSize = 0;
};

#endif // AL_ALMAXIMA_H

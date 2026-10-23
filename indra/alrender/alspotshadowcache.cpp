/**
 * @file alspotshadowcache.cpp
 * @brief Whether a spot light's shadow map must be drawn again, from what its last draw was drawn from.
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

#include "alspotshadowcache.h"

#include "llvertexbuffer.h"

#include <boost/container_hash/hash.hpp>

#include <bit>
#include <cstring>

ALSpotShadowCache::Hash& ALSpotShadowCache::Hash::add(U64 value)
{
    // size_t and U64 are one type on some platforms and not others.
    std::size_t seed = (std::size_t)mValue;
    boost::hash_combine(seed, value);
    mValue = (U64)seed;
    return *this;
}

ALSpotShadowCache::Hash& ALSpotShadowCache::Hash::add(F32 value)
{
    return add((U64)std::bit_cast<U32>(value));
}

ALSpotShadowCache::Hash& ALSpotShadowCache::Hash::add(const void* bytes, size_t size)
{
    const U8* at = static_cast<const U8*>(bytes);
    for (; size >= sizeof(U64); at += sizeof(U64), size -= sizeof(U64))
    {
        U64 word;
        std::memcpy(&word, at, sizeof(word));
        add(word);
    }
    if (size > 0)
    {
        U64 word = 0;
        std::memcpy(&word, at, size);
        add(word ^ ((U64)size << 56));
    }
    return *this;
}

void ALSpotShadowCache::Casters::add(const Hash& draw)
{
    mSum += draw.value();
    ++mCount;
}

U64 ALSpotShadowCache::Placements::of(const LLMatrix4a* matrix)
{
    if (!matrix)
    {
        return 0;
    }
    if (matrix != mLast)
    {
        mLast      = matrix;
        mLastValue = Hash().add(*matrix).value();
    }
    return mLastValue;
}

// static
void ALSpotShadowCache::foldGeometry(Hash& draw, const LLVertexBuffer* buffer, U32 start, U32 end, U32 count, U32 offset,
                                     U64 placement)
{
    draw.add(buffer).add(buffer ? buffer->getContentStamp() : 0);
    draw.add((U64)start).add((U64)end).add((U64)count).add((U64)offset);
    draw.add(placement);
}

bool ALSpotShadowCache::Key::sameAs(const Key& other) const
{
    return std::memcmp(mView.getF32ptr(), other.mView.getF32ptr(), sizeof(F32) * 16) == 0 &&
           std::memcmp(mProjection.getF32ptr(), other.mProjection.getF32ptr(), sizeof(F32) * 16) == 0 &&
           mLight == other.mLight && mState == other.mState && mCasters == other.mCasters;
}

bool ALSpotShadowCache::needsDraw(U32 map, const Key& key) const
{
    llassert(map < MAPS);
    return key.mCasters.changing() || !mDrawn[map] || !mDrawn[map]->sameAs(key);
}

void ALSpotShadowCache::drawn(U32 map, const Key& key)
{
    llassert(map < MAPS);
    mDrawn[map] = key;
}

void ALSpotShadowCache::invalidate(U32 map)
{
    llassert(map < MAPS);
    mDrawn[map].reset();
}

void ALSpotShadowCache::invalidate()
{
    for (std::optional<Key>& drawn : mDrawn)
    {
        drawn.reset();
    }
}

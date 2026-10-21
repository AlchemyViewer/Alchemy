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

#include <bit>
#include <cstring>

namespace
{
    // SplitMix64's finaliser.
    constexpr U64 mix(U64 x)
    {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return x;
    }
}

ALSpotShadowCache::Hash& ALSpotShadowCache::Hash::add(U64 value)
{
    // The golden ratio's step keeps a run of zeros from folding to the same value whatever its length.
    mValue = mix(mValue ^ (value + 0x9e3779b97f4a7c15ull));
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
    mSum += mix(draw.value());
    ++mCount;
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

/**
 * @file alspotshadowcache.h
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

#pragma once

#include "llmath.h"
#include "llmatrix4a.h"
#include "stdtypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

class LLVertexBuffer;

/// Whether a spot light's shadow map must be drawn again. A map keeps what was last drawn into it, so a pass whose
/// light and casters are what they were at its last draw can leave the map as it is. The viewer describes each pass by
/// a Key, made from the light's matrices and from what the pass's cull handed it to draw, and asks before it draws.
class ALSpotShadowCache
{
public:
    /// The pair of maps the main view keeps.
    static constexpr U32 MAPS = 2;

    /// Values folded in order into one 64-bit hash (boost::hash_combine).
    class Hash
    {
    public:
        Hash& add(U64 value);
        Hash& add(const void* object) { return add((U64)(uintptr_t)object); }
        Hash& add(F32 value);
        /// @a size bytes from @a bytes, as they lie.
        Hash& add(const void* bytes, size_t size);
        /// Its sixteen values exactly: a caster that moved has a different matrix.
        Hash& add(const LLMatrix4a& matrix) { return add(matrix.getF32ptr(), sizeof(F32) * 16); }

        U64 value() const { return mValue; }

    private:
        U64 mValue = 0x243f6a8885a308d3ull;
    };

    /// What a pass draws, folded into one value that changes when anything drawn does. Each draw is a Hash of what it
    /// reads, and the draws are summed, so the order the cull found them in does not count.
    class Casters
    {
    public:
        void add(const Hash& draw);
        /// Something the pass draws changes every frame, as an avatar's skin or a texture that plays does, so no draw
        /// of the map holds past its frame.
        void markChanging() { mChanging = true; }

        bool changing() const { return mChanging; }
        U32  count() const { return mCount; }

        friend bool operator==(const Casters&, const Casters&) = default;

    private:
        U64  mSum      = 0;
        U32  mCount    = 0;
        bool mChanging = false;
    };

    /// The matrices a walk over a pass's draws has folded, so that the draws one matrix places, as a linkset's or a
    /// bridge's are, fold its contents once. Made afresh for each walk, within which a matrix holds still.
    class Placements
    {
    public:
        /// @a matrix's contents as one value, 0 for none: a draw in world space.
        U64 of(const LLMatrix4a* matrix);

    private:
        const LLMatrix4a* mLast = nullptr;
        U64               mLastValue = 0;
    };

    /// A draw of @a buffer's @a count indices from @a offset, over its vertices @a start to @a end and placed by
    /// @a placement (Placements::of), folded into @a draw: the buffer, what it holds now
    /// (LLVertexBuffer::getContentStamp), the range and the placement.
    static void foldGeometry(Hash& draw, const LLVertexBuffer* buffer, U32 start, U32 end, U32 count, U32 offset,
                             U64 placement);

    /// What one map's pass is drawn from.
    struct Key
    {
        LLMatrix4a mView;            ///< the light's position and rotation
        LLMatrix4a mProjection;      ///< its field of view, aspect, near and far
        U64        mLight = 0;       ///< which light: its own geometry stays out of its map
        U64        mState = 0;       ///< what the casters are drawn with: the programs' load, the settings the pass reads
        Casters    mCasters;

        bool sameAs(const Key& other) const;
    };

    /// Whether map @a map must be drawn for @a key: nothing kept in it is known, it was drawn for another key, or what
    /// it is drawn from changes every frame.
    bool needsDraw(U32 map, const Key& key) const;
    /// Map @a map now holds what @a key draws.
    void drawn(U32 map, const Key& key);

    /// Map @a map holds nothing to keep: it was built again, or its contents were lost with the context.
    void invalidate(U32 map);
    void invalidate();

private:
    std::array<std::optional<Key>, MAPS> mDrawn;
};

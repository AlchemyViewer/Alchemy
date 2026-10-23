/**
 * @file alspotshadowcache_test.cpp
 * @brief When a spot light's shadow map is drawn again, and when what it holds is kept.
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

#include "../alspotshadowcache.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace
{
    using Cache   = ALSpotShadowCache;
    using Hash    = Cache::Hash;
    using Casters = Cache::Casters;
    using Key     = Cache::Key;

    LLMatrix4a translation(F32 x, F32 y, F32 z)
    {
        LLMatrix4a m;
        m.setIdentity();
        m.mMatrix[3].set(x, y, z, 1.f);
        return m;
    }

    // A GL perspective, as a spot light's projection is made from its field of view, aspect and clip planes.
    LLMatrix4a perspective(F32 fovy, F32 aspect, F32 near_clip, F32 far_clip)
    {
        const F32 f = 1.f / std::tan(fovy * 0.5f);
        LLMatrix4a m;
        m.mMatrix[0].set(f / aspect, 0.f, 0.f, 0.f);
        m.mMatrix[1].set(0.f, f, 0.f, 0.f);
        m.mMatrix[2].set(0.f, 0.f, (far_clip + near_clip) / (near_clip - far_clip), -1.f);
        m.mMatrix[3].set(0.f, 0.f, 2.f * far_clip * near_clip / (near_clip - far_clip), 0.f);
        return m;
    }

    // What the viewer folds of one draw: its buffer and the stamp of what the buffer holds, the range drawn, where the
    // draw is placed and the texture it alpha tests by, as image, level and size.
    struct Caster
    {
        U64        mBuffer   = 1;
        U64        mContents = 1;
        U32        mStart    = 0;
        U32        mEnd      = 23;
        U32        mCount    = 36;
        U32        mOffset   = 0;
        LLMatrix4a mModel    = translation(0.f, 0.f, 0.f);
        U64        mTexture  = 0;
        S32        mDiscard  = 0;
    };

    Hash draw_of(const Caster& caster)
    {
        Hash draw;
        draw.add(caster.mBuffer).add(caster.mContents);
        draw.add((U64)caster.mStart).add((U64)caster.mEnd).add((U64)caster.mCount).add((U64)caster.mOffset);
        draw.add(caster.mModel);
        draw.add(caster.mTexture).add((U64)(U32)caster.mDiscard);
        return draw;
    }

    Casters casters_of(const std::vector<Caster>& casters)
    {
        Casters folded;
        for (const Caster& caster : casters)
        {
            folded.add(draw_of(caster));
        }
        return folded;
    }

    // Three casters under a light, of two buffers, one moved off the origin and one alpha tested.
    std::vector<Caster> room()
    {
        std::vector<Caster> casters(3);
        casters[1].mBuffer = 2;
        casters[1].mContents = 7;
        casters[1].mStart = 24;
        casters[1].mEnd = 47;
        casters[1].mOffset = 36;
        casters[2].mBuffer = 2;
        casters[2].mContents = 7;
        casters[2].mModel = translation(4.f, 0.f, 1.5f);
        casters[2].mTexture = 9;
        return casters;
    }

    Key key_of(const std::vector<Caster>& casters)
    {
        Key key;
        key.mView       = translation(-12.f, 3.f, -40.f);
        key.mProjection = perspective(1.2f, 1.f, 2.01f, 17.f);
        key.mLight      = 0x1000;
        key.mState      = 1;
        key.mCasters    = casters_of(casters);
        return key;
    }

    // A cache whose map 0 holds what @a key draws.
    Cache drawn_for(const Key& key)
    {
        Cache cache;
        REQUIRE(cache.needsDraw(0, key));
        cache.drawn(0, key);
        REQUIRE_FALSE(cache.needsDraw(0, key));
        return cache;
    }

    TEST_CASE("alspotshadowcache: a map is drawn until it holds what its pass draws, and kept from then", "[alspotshadowcache]")
    {
        const Key key = key_of(room());

        Cache cache;
        CHECK(cache.needsDraw(0, key));
        CHECK(cache.needsDraw(1, key));

        cache.drawn(0, key);
        CHECK_FALSE(cache.needsDraw(0, key));
        CHECK_FALSE(cache.needsDraw(0, key_of(room())));
        // Each map is its own: the other was never drawn.
        CHECK(cache.needsDraw(1, key));

        cache.drawn(1, key);
        CHECK_FALSE(cache.needsDraw(1, key));
    }

    TEST_CASE("alspotshadowcache: the light's transform, field of view, aspect and clip planes each draw the map again",
              "[alspotshadowcache]")
    {
        const Key held = key_of(room());

        const std::vector<std::pair<const char*, std::function<void(Key&)>>> changes = {
            { "moved", [](Key& key) { key.mView = translation(-12.f, 3.f, -40.01f); } },
            { "turned",
              [](Key& key)
              {
                  key.mView.mMatrix[0].set(0.f, 1.f, 0.f, 0.f);
                  key.mView.mMatrix[1].set(-1.f, 0.f, 0.f, 0.f);
              } },
            { "a wider field of view", [](Key& key) { key.mProjection = perspective(1.3f, 1.f, 2.01f, 17.f); } },
            { "another aspect", [](Key& key) { key.mProjection = perspective(1.2f, 1.5f, 2.01f, 17.f); } },
            { "a nearer near plane", [](Key& key) { key.mProjection = perspective(1.2f, 1.f, 1.01f, 17.f); } },
            { "a farther far plane", [](Key& key) { key.mProjection = perspective(1.2f, 1.f, 2.01f, 18.f); } },
        };

        for (const auto& [what, change] : changes)
        {
            INFO(what);
            const Cache cache = drawn_for(held);
            Key key = held;
            change(key);
            CHECK(cache.needsDraw(0, key));
        }
    }

    TEST_CASE("alspotshadowcache: another light, or casters drawn another way, draw the map again", "[alspotshadowcache]")
    {
        const Key held = key_of(room());
        const Cache cache = drawn_for(held);

        // A light that took the map over where the last one stood: its own geometry is the one left out.
        Key light = held;
        light.mLight = 0x2000;
        CHECK(cache.needsDraw(0, light));

        // The programs loaded again, or a setting the pass reads.
        Key state = held;
        state.mState = 2;
        CHECK(cache.needsDraw(0, state));
    }

    TEST_CASE("alspotshadowcache: a caster added, removed, moved or changed draws the map again", "[alspotshadowcache]")
    {
        const Key held = key_of(room());

        const std::vector<std::pair<const char*, std::function<void(std::vector<Caster>&)>>> changes = {
            { "a caster added", [](std::vector<Caster>& casters) { casters.push_back(Caster{ .mBuffer = 3 }); } },
            { "a caster removed", [](std::vector<Caster>& casters) { casters.pop_back(); } },
            { "a caster that is a second draw of one already there", [](std::vector<Caster>& casters) { casters.push_back(casters[0]); } },
            { "a caster moved", [](std::vector<Caster>& casters) { casters[2].mModel = translation(4.f, 0.001f, 1.5f); } },
            { "a buffer written again", [](std::vector<Caster>& casters) { casters[0].mContents = 2; } },
            { "a buffer two casters share written again",
              [](std::vector<Caster>& casters)
              {
                  casters[1].mContents = 8;
                  casters[2].mContents = 8;
              } },
            { "a draw of another range", [](std::vector<Caster>& casters) { casters[1].mCount = 33; } },
            { "a texture at another level", [](std::vector<Caster>& casters) { casters[2].mDiscard = 1; } },
            { "another texture", [](std::vector<Caster>& casters) { casters[2].mTexture = 10; } },
            { "two casters that swapped textures",
              [](std::vector<Caster>& casters)
              {
                  casters[1].mTexture = casters[2].mTexture;
                  casters[2].mTexture = 0;
              } },
        };

        for (const auto& [what, change] : changes)
        {
            INFO(what);
            const Cache cache = drawn_for(held);
            std::vector<Caster> casters = room();
            change(casters);
            CHECK(cache.needsDraw(0, key_of(casters)));
        }
    }

    TEST_CASE("alspotshadowcache: the same casters found in another order keep the map", "[alspotshadowcache]")
    {
        const Cache cache = drawn_for(key_of(room()));

        std::vector<Caster> casters = room();
        std::swap(casters[0], casters[2]);
        CHECK_FALSE(cache.needsDraw(0, key_of(casters)));
    }

    TEST_CASE("alspotshadowcache: casters that change every frame draw the map every frame, and once after they stop",
              "[alspotshadowcache]")
    {
        Key changing = key_of(room());
        changing.mCasters.markChanging();

        Cache cache;
        for (int frame = 0; frame < 3; ++frame)
        {
            INFO("frame " << frame);
            CHECK(cache.needsDraw(0, changing));
            cache.drawn(0, changing);
        }

        // An avatar walked out of the light: what is left folds to what the frames before it folded, but their map
        // still holds the avatar.
        const Key still = key_of(room());
        CHECK(cache.needsDraw(0, still));
        cache.drawn(0, still);
        CHECK_FALSE(cache.needsDraw(0, still));
    }

    TEST_CASE("alspotshadowcache: a map invalidated is drawn again, and the other kept", "[alspotshadowcache]")
    {
        const Key key = key_of(room());

        Cache cache;
        cache.drawn(0, key);
        cache.drawn(1, key);

        cache.invalidate(1);
        CHECK_FALSE(cache.needsDraw(0, key));
        CHECK(cache.needsDraw(1, key));

        cache.drawn(1, key);
        cache.invalidate();
        CHECK(cache.needsDraw(0, key));
        CHECK(cache.needsDraw(1, key));
    }

    TEST_CASE("alspotshadowcache: a walk folds each matrix by what it holds, once for the draws it places", "[alspotshadowcache]")
    {
        LLMatrix4a       a = translation(1.f, 0.f, 0.f);
        const LLMatrix4a b = translation(2.f, 0.f, 0.f);

        Cache::Placements walk;
        const U64         placed = walk.of(&a);
        CHECK(placed != 0);
        CHECK(walk.of(&a) == placed);
        CHECK(walk.of(&b) != placed);
        // Back to the first, after another: the same matrix folds to the same value.
        CHECK(walk.of(&a) == placed);
        // A draw in world space.
        CHECK(walk.of(nullptr) == 0);

        // A linkset moved between walks: the next walk sees where it is now, through the same matrix.
        a = translation(1.f, 0.5f, 0.f);
        Cache::Placements next;
        CHECK(next.of(&a) != placed);
    }

    TEST_CASE("alspotshadowcache: a draw's geometry folds its range and its placement", "[alspotshadowcache]")
    {
        const auto fold = [](U32 start, U32 end, U32 count, U32 offset, U64 placement)
        {
            Hash draw;
            Cache::foldGeometry(draw, nullptr, start, end, count, offset, placement);
            return draw.value();
        };

        const U64 held = fold(0, 23, 36, 0, 7);
        CHECK(fold(0, 23, 36, 0, 7) == held);
        CHECK(fold(1, 23, 36, 0, 7) != held);
        CHECK(fold(0, 24, 36, 0, 7) != held);
        CHECK(fold(0, 23, 33, 0, 7) != held);
        CHECK(fold(0, 23, 36, 6, 7) != held);
        CHECK(fold(0, 23, 36, 0, 8) != held);
        CHECK(fold(0, 23, 36, 0, 0) != held);
    }

    TEST_CASE("alspotshadowcache: a draw's hash takes every value it is given, in order", "[alspotshadowcache]")
    {
        CHECK(Hash().add((U64)1).add((U64)2).value() != Hash().add((U64)2).add((U64)1).value());
        // A run of zeros, by its length.
        CHECK(Hash().add((U64)0).value() != Hash().value());
        CHECK(Hash().add((U64)0).add((U64)0).value() != Hash().add((U64)0).value());
        // The bytes past the last whole word, by how many there are.
        const U8 bytes[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0 };
        CHECK(Hash().add(bytes, 9).value() != Hash().add(bytes, 10).value());
        CHECK(Hash().add(bytes, 12).value() != Hash().add(bytes, 8).value());
        // Every value of a matrix, the translation's among them.
        CHECK(Hash().add(translation(0.f, 0.f, 1.f)).value() != Hash().add(translation(0.f, 0.f, 2.f)).value());
        CHECK(Hash().add(1.f).value() != Hash().add(1.0000001f).value());
    }
}

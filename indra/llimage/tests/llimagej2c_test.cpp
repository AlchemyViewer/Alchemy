/**
 * @file llimagej2c_test.cpp
 * @brief Tests for LLImageJ2C over the build's codec
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

#include "llimagej2c.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace
{
    // A 64x64 gradient, encoded by the build's codec.
    LLPointer<LLImageJ2C> encoded()
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(64, 64, 3);
        U8* data = raw->getData();
        for (S32 y = 0; y < 64; ++y)
        {
            for (S32 x = 0; x < 64; ++x)
            {
                data[(y * 64 + x) * 3 + 0] = U8(x * 4);
                data[(y * 64 + x) * 3 + 1] = U8(y * 4);
                data[(y * 64 + x) * 3 + 2] = U8((x + y) * 2);
            }
        }
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C;
        REQUIRE(j2c->encode(raw, 0.f));
        return j2c;
    }

    // An image holding a copy of `source`'s codestream, its header read.
    LLPointer<LLImageJ2C> copyOf(const LLImageJ2C* source)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C;
        U8* copy = (U8*)ll_aligned_malloc_16(source->getDataSize());
        memcpy(copy, source->getData(), source->getDataSize());
        j2c->setData(copy, source->getDataSize());
        REQUIRE(j2c->updateData());
        return j2c;
    }

    // initDecode takes -1 for no discard level, which is full size, not the
    // codec's reading of -1.
    TEST_CASE("llimagej2c: initDecode with no discard level decodes at full size", "[llimagej2c]")
    {
        const LLPointer<LLImageJ2C> source = encoded();

        LLPointer<LLImageJ2C> j2c = copyOf(source);
        LLPointer<LLImageRaw> raw = new LLImageRaw(j2c->getWidth(), j2c->getHeight(), j2c->getComponents());
        j2c->initDecode(*raw, -1, nullptr);
        CHECK(j2c->getDiscardLevel() == 0);
        REQUIRE(j2c->decode(raw, 0.f));
        CHECK(raw->getWidth() == 64);
        CHECK(raw->getHeight() == 64);
    }

    TEST_CASE("llimagej2c: initDecode with a discard level decodes at that level", "[llimagej2c]")
    {
        const LLPointer<LLImageJ2C> source = encoded();

        LLPointer<LLImageJ2C> j2c = copyOf(source);
        LLPointer<LLImageRaw> raw = new LLImageRaw(j2c->getWidth(), j2c->getHeight(), j2c->getComponents());
        j2c->initDecode(*raw, 2, nullptr);
        CHECK(j2c->getDiscardLevel() == 2);
        REQUIRE(j2c->decode(raw, 0.f));
        CHECK(raw->getWidth() == 16);
        CHECK(raw->getHeight() == 16);
    }
}

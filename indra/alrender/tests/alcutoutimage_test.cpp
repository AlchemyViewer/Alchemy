/**
 * @file alcutoutimage_test.cpp
 * @brief Unit tests for a snapshot cutout's image arithmetic
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../alcutoutimage.h"

#include "llimage.h"

#include <array>
#include <cstdlib>
#include <vector>

namespace
{
    typedef std::array<U8, 4> texel_t;

    /// A premultiplied RGBA image, row by row from the first texel.
    LLPointer<LLImageRaw> image(S32 width, S32 height, const std::vector<texel_t>& texels)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw((U16)width, (U16)height, 4);
        U8* data = raw->getData();
        for (const texel_t& t : texels)
        {
            for (U8 c : t)
            {
                *data++ = c;
            }
        }
        return raw;
    }

    std::vector<U8> texel(const LLImageRaw* raw, S32 index)
    {
        const S32 comps = raw->getComponents();
        const U8* data = raw->getData() + index * comps;
        return std::vector<U8>(data, data + comps);
    }

    void closeTo(const char* what, U8 actual, S32 expected, S32 tolerance = 2)
    {
        UNSCOPED_INFO(what << ": got " << (S32)actual << ", wanted " << expected);
        CHECK(std::abs((S32)actual - expected) <= tolerance);
    }

    // Transparent fill, kept alpha: covered texels keep their colour, empty
    // ones stay empty.
    TEST_CASE("alcutoutimage: a transparent fill keeps covered texels and leaves empty ones empty", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 2, 1, LLColor4(0.f, 1.f, 0.f, 0.f), true));
        REQUIRE(raw->getComponents() == 4);

        const std::vector<U8> subject = texel(raw, 0);
        closeTo("subject red", subject[0], 255);
        closeTo("subject green", subject[1], 0);
        closeTo("subject alpha", subject[3], 255);

        const std::vector<U8> empty = texel(raw, 1);
        closeTo("empty alpha", empty[3], 0);
    }

    // An edge averaged down keeps its colour and loses only coverage -- the
    // whole reason the read-back is premultiplied.
    //
    // The failing control beside it: the same two texels averaged as straight
    // colour, which is what averaging after the division would do, come out a
    // half-dark red. Were resolve to do that, the first assertion would fail.
    TEST_CASE("alcutoutimage: an edge averaged down keeps its colour and loses only coverage", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true));

        const std::vector<U8> edge = texel(raw, 0);
        closeTo("edge keeps its red", edge[0], 255, 3);
        closeTo("edge is half covered", edge[3], 128, 3);

        // Straight colour averaged: the empty texel's colour is black, and it
        // counts as much as the red one.
        LLPointer<LLImageRaw> straight = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        REQUIRE(straight->scale(1, 1));
        const std::vector<U8> darkened = texel(straight, 0);
        UNSCOPED_INFO("control darkens the edge");
        CHECK(darkened[0] < 200);
    }

    // An opaque fill under a half-covered texel: the fill shows through the
    // uncovered half, and the result is opaque.
    TEST_CASE("alcutoutimage: an opaque fill shows through a half-covered texel", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 128, 0, 0, 128 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 1, 1, LLColor4(1.f, 1.f, 1.f, 1.f), true));

        const std::vector<U8> t = texel(raw, 0);
        closeTo("red", t[0], 255);
        closeTo("green from the fill", t[1], 127);
        closeTo("blue from the fill", t[2], 127);
        closeTo("opaque", t[3], 255);
    }

    // A see-through fill: half as much fill as a solid one, and the colour
    // straight, not darkened by it.
    TEST_CASE("alcutoutimage: a see-through fill is half there and its colour straight", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 0, 0, 0, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 1, 1, LLColor4(0.f, 0.f, 1.f, 0.5f), true));

        const std::vector<U8> t = texel(raw, 0);
        closeTo("blue, straight", t[2], 255);
        closeTo("half there", t[3], 128);
    }

    // Nowhere to put alpha: three components, over the fill's colour taken as
    // opaque whatever its alpha says.
    TEST_CASE("alcutoutimage: with nowhere to put alpha the fill is taken as opaque", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 128, 0, 0, 128 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 1, 1, LLColor4(0.f, 0.f, 1.f, 0.f), false));
        REQUIRE(raw->getComponents() == 3);

        const std::vector<U8> t = texel(raw, 0);
        closeTo("red", t[0], 128);
        closeTo("blue from the fill", t[2], 127);
    }

    // Colour over an empty texel -- dither, stray light -- has nothing to
    // belong to, and must not reach the image.
    TEST_CASE("alcutoutimage: colour over an empty texel does not reach the image", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> kept = image(1, 1, { { 10, 10, 10, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*kept, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true));
        closeTo("kept: nothing", texel(kept, 0)[3], 0, 0);

        LLPointer<LLImageRaw> flat = image(1, 1, { { 10, 10, 10, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*flat, 1, 1, LLColor4(0.f, 0.f, 0.f, 1.f), false));
        closeTo("flat: the fill, black", texel(flat, 0)[0], 0, 0);
    }

    // Resolved alone, a cutout is still premultiplied and has no fill: what is
    // laid under it later, and whether it keeps alpha, takes no second render.
    TEST_CASE("alcutoutimage: resolve leaves the cutout premultiplied and clipped for a fill to go under later", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(4, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 }, { 10, 10, 10, 0 }, { 0, 0, 0, 0 } });
        bool drawn = false;
        REQUIRE(ALCutoutImage::resolve(*raw, 2, 1, &drawn));
        CHECK(drawn);
        REQUIRE(raw->getComponents() == 4);
        REQUIRE(raw->getWidth() == 2);

        const std::vector<U8> edge = texel(raw, 0);
        closeTo("edge red, premultiplied", edge[0], 128, 3);
        closeTo("edge half covered", edge[3], 128, 3);

        const std::vector<U8> empty = texel(raw, 1);
        closeTo("empty: the stray colour clipped", empty[0], 0, 0);
        closeTo("empty: no coverage", empty[3], 0, 0);
    }

    // Laid over a fill with nowhere to put alpha, a cutout is the size it was
    // resolved to: the rows are not padded for BMP, whose encoder pads its own.
    TEST_CASE("alcutoutimage: laid over a fill without alpha a cutout keeps the width it was resolved to", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(5, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } });
        REQUIRE(ALCutoutImage::layOverFill(*raw, LLColor4(1.f, 1.f, 1.f, 1.f), false));
        CHECK(raw->getComponents() == 3);
        CHECK(raw->getWidth() == 5);
        CHECK(raw->getDataSize() == 5 * 3);
        closeTo("the last texel is the fill", texel(raw, 4)[1], 255);
    }

    // Only premultiplied RGBA is a cutout's read-back.
    TEST_CASE("alcutoutimage: a read-back of three components is refused", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> rgb = new LLImageRaw(1, 1, 3);
        CHECK_FALSE(ALCutoutImage::resolveOverFill(*rgb, 1, 1, LLColor4::black, true));
    }

    // A resolved cutout -- straight RGBA -- made smaller, as its thumbnail is,
    // keeps its edge colour. Its control is the averaged edge's: the same
    // texels scaled as they are come out half dark.
    TEST_CASE("alcutoutimage: a resolved cutout made smaller keeps its edge colour", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 255, 0, 0 } });
        REQUIRE(ALCutoutImage::scaleStraightAlpha(*raw, 1, 1));
        REQUIRE(raw->getComponents() == 4);

        const std::vector<U8> edge = texel(raw, 0);
        closeTo("edge keeps its red", edge[0], 255, 3);
        // The empty texel's colour, green, carries no weight: it covers nothing.
        closeTo("no green from the empty texel", edge[1], 0, 3);
        closeTo("edge is half covered", edge[3], 128, 3);
    }

    // A read-back with nothing covered is the subject out of view -- colour
    // in its empty texels or not -- and one covered texel, however faint and
    // however much it is averaged away, is the subject drawn.
    TEST_CASE("alcutoutimage: a read-back with nothing covered says the subject was not drawn", "[alcutoutimage]")
    {
        bool drawn = true;
        LLPointer<LLImageRaw> empty = image(2, 2, { { 10, 10, 10, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 255, 255, 255, 0 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*empty, 1, 1, LLColor4(1.f, 1.f, 1.f, 1.f), false, &drawn));
        UNSCOPED_INFO("nothing covered: not drawn");
        CHECK_FALSE(drawn);

        drawn = false;
        LLPointer<LLImageRaw> faint = image(2, 2, { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 1, 0, 0, 1 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*faint, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true, &drawn));
        UNSCOPED_INFO("one faint texel: drawn");
        CHECK(drawn);
    }

    // Composited in place, a row packed down to three components keeps every
    // texel its own: a write behind the read must not reach a texel not yet
    // read. Unscaled, so each texel is checked as it went in.
    TEST_CASE("alcutoutimage: a row packed in place to three components keeps every texel its own", "[alcutoutimage]")
    {
        LLPointer<LLImageRaw> raw = image(4, 1, { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 0, 0 }, { 0, 0, 128, 128 } });
        REQUIRE(ALCutoutImage::resolveOverFill(*raw, 4, 1, LLColor4(1.f, 1.f, 1.f, 0.f), false));
        REQUIRE(raw->getComponents() == 3);
        REQUIRE(raw->getWidth() == 4);
        CHECK(raw->getDataSize() == 4 * 3);

        const std::vector<U8> red = texel(raw, 0);
        closeTo("red stays red", red[0], 255);
        closeTo("red has no green", red[1], 0);

        const std::vector<U8> green = texel(raw, 1);
        closeTo("green has no red", green[0], 0);
        closeTo("green stays green", green[1], 255);

        const std::vector<U8> empty = texel(raw, 2);
        closeTo("empty is the fill", empty[0], 255);
        closeTo("empty is the fill, green", empty[1], 255);

        const std::vector<U8> half = texel(raw, 3);
        closeTo("half blue: the fill's red through it", half[0], 127);
        closeTo("half blue: blue on the fill", half[2], 255);
    }
}

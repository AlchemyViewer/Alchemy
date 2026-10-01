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

#include "../test/lltut.h"

#include "../alcutoutimage.h"

#include "llimage.h"

#include <array>
#include <cstdlib>
#include <vector>

namespace tut
{
    struct cutout_data
    {
        typedef std::array<U8, 4> texel_t;

        /// A premultiplied RGBA image, row by row from the first texel.
        static LLPointer<LLImageRaw> image(S32 width, S32 height, const std::vector<texel_t>& texels)
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

        static std::vector<U8> texel(const LLImageRaw* raw, S32 index)
        {
            const S32 comps = raw->getComponents();
            const U8* data = raw->getData() + index * comps;
            return std::vector<U8>(data, data + comps);
        }

        static void close_to(const std::string& what, U8 actual, S32 expected, S32 tolerance = 2)
        {
            ensure(what + ": got " + std::to_string(actual) + ", wanted " + std::to_string(expected),
                   std::abs((S32)actual - expected) <= tolerance);
        }
    };

    typedef test_group<cutout_data> cutout_group;
    typedef cutout_group::object    cutout_object;
    tut::cutout_group cutoutg("ALCutoutImage");

    // Transparent fill, kept alpha: covered texels keep their colour, empty
    // ones stay empty.
    template<> template<>
    void cutout_object::test<1>()
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        ensure("resolves", ALCutoutImage::resolve(*raw, 2, 1, LLColor4(0.f, 1.f, 0.f, 0.f), true));
        ensure_equals("four components", (S32)raw->getComponents(), 4);

        const std::vector<U8> subject = texel(raw, 0);
        close_to("subject red", subject[0], 255);
        close_to("subject green", subject[1], 0);
        close_to("subject alpha", subject[3], 255);

        const std::vector<U8> empty = texel(raw, 1);
        close_to("empty alpha", empty[3], 0);
    }

    // An edge averaged down keeps its colour and loses only coverage -- the
    // whole reason the read-back is premultiplied.
    //
    // The failing control beside it: the same two texels averaged as straight
    // colour, which is what averaging after the division would do, come out a
    // half-dark red. Were resolve to do that, the first assertion would fail.
    template<> template<>
    void cutout_object::test<2>()
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        ensure("resolves", ALCutoutImage::resolve(*raw, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true));

        const std::vector<U8> edge = texel(raw, 0);
        close_to("edge keeps its red", edge[0], 255, 3);
        close_to("edge is half covered", edge[3], 128, 3);

        // Straight colour averaged: the empty texel's colour is black, and it
        // counts as much as the red one.
        LLPointer<LLImageRaw> straight = image(2, 1, { { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
        ensure("control scales", straight->scale(1, 1));
        const std::vector<U8> darkened = texel(straight, 0);
        ensure("control darkens the edge", darkened[0] < 200);
    }

    // An opaque fill under a half-covered texel: the fill shows through the
    // uncovered half, and the result is opaque.
    template<> template<>
    void cutout_object::test<3>()
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 128, 0, 0, 128 } });
        ensure("resolves", ALCutoutImage::resolve(*raw, 1, 1, LLColor4(1.f, 1.f, 1.f, 1.f), true));

        const std::vector<U8> t = texel(raw, 0);
        close_to("red", t[0], 255);
        close_to("green from the fill", t[1], 127);
        close_to("blue from the fill", t[2], 127);
        close_to("opaque", t[3], 255);
    }

    // A see-through fill: half as much fill as a solid one, and the colour
    // straight, not darkened by it.
    template<> template<>
    void cutout_object::test<4>()
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 0, 0, 0, 0 } });
        ensure("resolves", ALCutoutImage::resolve(*raw, 1, 1, LLColor4(0.f, 0.f, 1.f, 0.5f), true));

        const std::vector<U8> t = texel(raw, 0);
        close_to("blue, straight", t[2], 255);
        close_to("half there", t[3], 128);
    }

    // Nowhere to put alpha: three components, over the fill's colour taken as
    // opaque whatever its alpha says.
    template<> template<>
    void cutout_object::test<5>()
    {
        LLPointer<LLImageRaw> raw = image(1, 1, { { 128, 0, 0, 128 } });
        ensure("resolves", ALCutoutImage::resolve(*raw, 1, 1, LLColor4(0.f, 0.f, 1.f, 0.f), false));
        ensure_equals("three components", (S32)raw->getComponents(), 3);

        const std::vector<U8> t = texel(raw, 0);
        close_to("red", t[0], 128);
        close_to("blue from the fill", t[2], 127);
    }

    // Colour over an empty texel -- dither, stray light -- has nothing to
    // belong to, and must not reach the image.
    template<> template<>
    void cutout_object::test<6>()
    {
        LLPointer<LLImageRaw> kept = image(1, 1, { { 10, 10, 10, 0 } });
        ensure("resolves kept", ALCutoutImage::resolve(*kept, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true));
        close_to("kept: nothing", texel(kept, 0)[3], 0, 0);

        LLPointer<LLImageRaw> flat = image(1, 1, { { 10, 10, 10, 0 } });
        ensure("resolves flat", ALCutoutImage::resolve(*flat, 1, 1, LLColor4(0.f, 0.f, 0.f, 1.f), false));
        close_to("flat: the fill, black", texel(flat, 0)[0], 0, 0);
    }

    // Only premultiplied RGBA is a cutout's read-back.
    template<> template<>
    void cutout_object::test<7>()
    {
        LLPointer<LLImageRaw> rgb = new LLImageRaw(1, 1, 3);
        ensure("three components refused", !ALCutoutImage::resolve(*rgb, 1, 1, LLColor4::black, true));
    }

    // A resolved cutout -- straight RGBA -- made smaller, as its thumbnail is,
    // keeps its edge colour. Its control is test 2's: the same texels scaled
    // as they are come out half dark.
    template<> template<>
    void cutout_object::test<8>()
    {
        LLPointer<LLImageRaw> raw = image(2, 1, { { 255, 0, 0, 255 }, { 0, 255, 0, 0 } });
        ensure("scales", ALCutoutImage::scaleStraight(*raw, 1, 1));
        ensure_equals("still four components", (S32)raw->getComponents(), 4);

        const std::vector<U8> edge = texel(raw, 0);
        close_to("edge keeps its red", edge[0], 255, 3);
        // The empty texel's colour, green, carries no weight: it covers nothing.
        close_to("no green from the empty texel", edge[1], 0, 3);
        close_to("edge is half covered", edge[3], 128, 3);
    }

    // A read-back with nothing covered is the subject out of view -- colour
    // in its empty texels or not -- and one covered texel, however faint and
    // however much it is averaged away, is the subject drawn.
    template<> template<>
    void cutout_object::test<9>()
    {
        bool drawn = true;
        LLPointer<LLImageRaw> empty = image(2, 2, { { 10, 10, 10, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 255, 255, 255, 0 } });
        ensure("resolves empty", ALCutoutImage::resolve(*empty, 1, 1, LLColor4(1.f, 1.f, 1.f, 1.f), false, &drawn));
        ensure("nothing covered: not drawn", !drawn);

        drawn = false;
        LLPointer<LLImageRaw> faint = image(2, 2, { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 1, 0, 0, 1 } });
        ensure("resolves faint", ALCutoutImage::resolve(*faint, 1, 1, LLColor4(0.f, 0.f, 0.f, 0.f), true, &drawn));
        ensure("one faint texel: drawn", drawn);
    }
}

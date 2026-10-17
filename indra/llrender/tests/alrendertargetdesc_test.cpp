/**
 * @file alrendertargetdesc_test.cpp
 * @brief ALRenderTargetDesc: its defaults, its colour count, and equality and hash over every field.
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

#include "../alrendertargetdesc.h"
#include "../llglheaders.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
{
    using Depth = ALRenderTargetDesc::Depth;
    using Mips  = ALRenderTargetDesc::Mips;

    constexpr ALRenderTargetDesc GBUFFER{ .width = 1920, .height = 1080,
                                          .color = { GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGBA16 },
                                          .depth = Depth::D32F, .stencil = true };

    // GBUFFER with one field changed, for each field.
    std::vector<std::pair<std::string, ALRenderTargetDesc>> one_field_changed()
    {
        std::vector<std::pair<std::string, ALRenderTargetDesc>> variants;
        auto add = [&variants](const char* what, auto change)
        {
            ALRenderTargetDesc desc = GBUFFER;
            change(desc);
            variants.emplace_back(what, desc);
        };
        add("width", [](ALRenderTargetDesc& d) { d.width = 1921; });
        add("height", [](ALRenderTargetDesc& d) { d.height = 1081; });
        add("color 0", [](ALRenderTargetDesc& d) { d.color[0] = GL_RGBA8; });
        add("color 1", [](ALRenderTargetDesc& d) { d.color[1] = GL_RGB10_A2; });
        add("color 2", [](ALRenderTargetDesc& d) { d.color[2] = GL_RGB10_A2; });
        add("color 3", [](ALRenderTargetDesc& d) { d.color[3] = GL_R11F_G11F_B10F; });
        add("depth", [](ALRenderTargetDesc& d) { d.depth = Depth::D24; });
        add("stencil", [](ALRenderTargetDesc& d) { d.stencil = false; });
        add("mips", [](ALRenderTargetDesc& d) { d.mips = Mips::Auto; });
        return variants;
    }

    // A designated initializer names what differs; everything it leaves out is the default, which is no target.
    TEST_CASE("alrendertargetdesc: what an initializer leaves out is the default", "[alrendertargetdesc]")
    {
        constexpr ALRenderTargetDesc none{};
        STATIC_CHECK(none.width == 0);
        STATIC_CHECK(none.height == 0);
        STATIC_CHECK(none.colorCount() == 0);
        STATIC_CHECK(none.depth == Depth::None);
        STATIC_CHECK(!none.stencil);
        STATIC_CHECK(none.mips == Mips::None);

        STATIC_CHECK(GBUFFER.color[3] == 0);
        STATIC_CHECK(GBUFFER.mips == Mips::None);
    }

    // The colour count is the formats before the first 0, whatever follows it.
    TEST_CASE("alrendertargetdesc: the colour count stops at the first 0", "[alrendertargetdesc]")
    {
        STATIC_CHECK(GBUFFER.colorCount() == 3);
        STATIC_CHECK(ALRenderTargetDesc{ .color = { GL_RGBA8, GL_RGBA8, GL_RGBA8, GL_RGBA8 } }.colorCount() == 4);
        STATIC_CHECK(ALRenderTargetDesc{ .color = { 0, GL_RGBA8 } }.colorCount() == 0);
        STATIC_CHECK(ALRenderTargetDesc{ .color = { GL_RGBA8, 0, GL_RGBA8 } }.colorCount() == 1);
        STATIC_CHECK(ALRenderTargetDesc{ .depth = Depth::D24 }.colorCount() == 0);
    }

    // Each field on its own makes two descriptions differ and moves the hash, and equal descriptions hash alike.
    TEST_CASE("alrendertargetdesc: every field counts for equality and the hash", "[alrendertargetdesc]")
    {
        ALRenderTargetDesc copy = GBUFFER;
        CHECK(copy == GBUFFER);
        CHECK(copy.hash() == GBUFFER.hash());
        STATIC_CHECK(GBUFFER.hash() == ALRenderTargetDesc(GBUFFER).hash());

        for (const auto& [what, changed] : one_field_changed())
        {
            CAPTURE(what);
            CHECK(changed != GBUFFER);
            CHECK(changed.hash() != GBUFFER.hash());
        }
    }

    // The header's tripwire counts the description's fields, since their size alone misses a byte added in the tail
    // padding: two layouts of one size, told apart by their field counts, and the description's own count.
    TEST_CASE("alrendertargetdesc: the field count sees a field the size does not", "[alrendertargetdesc]")
    {
        struct Padded
        {
            U32  width;
            bool flag;
        };
        struct PaddingFilled
        {
            U32  width;
            bool flag;
            bool another;
        };
        STATIC_CHECK(sizeof(Padded) == sizeof(PaddingFilled));
        STATIC_CHECK(al_render_target_desc::field_count<Padded>() == 2);
        STATIC_CHECK(al_render_target_desc::field_count<PaddingFilled>() == 3);
        STATIC_CHECK(al_render_target_desc::field_count<ALRenderTargetDesc>() == 6);
    }

    // Two descriptions alike but for the order of two fields hash apart: the hash folds each field in its place.
    TEST_CASE("alrendertargetdesc: the hash folds each field in its place", "[alrendertargetdesc]")
    {
        const ALRenderTargetDesc wide{ .width = 1024, .height = 512, .color = { GL_RGBA8, GL_RGBA16F } };
        const ALRenderTargetDesc tall{ .width = 512, .height = 1024, .color = { GL_RGBA8, GL_RGBA16F } };
        const ALRenderTargetDesc swapped{ .width = 1024, .height = 512, .color = { GL_RGBA16F, GL_RGBA8 } };
        CHECK(wide.hash() != tall.hash());
        CHECK(wide.hash() != swapped.hash());
    }

    // boost::unordered keys a description on hash_value and ==: each variant is its own key, and an equal
    // description finds the one stored under it.
    TEST_CASE("alrendertargetdesc: keys a boost::unordered map", "[alrendertargetdesc]")
    {
        boost::unordered_flat_map<ALRenderTargetDesc, int> map;
        map.emplace(GBUFFER, -1);
        const auto variants = one_field_changed();
        for (size_t i = 0; i < variants.size(); ++i)
        {
            map.emplace(variants[i].second, (int)i);
        }
        CHECK(map.size() == variants.size() + 1);

        ALRenderTargetDesc lookup = GBUFFER;
        CHECK(map.at(lookup) == -1);
        lookup.mips = Mips::Auto;
        CHECK(map.at(lookup) == (int)variants.size() - 1);
    }
}

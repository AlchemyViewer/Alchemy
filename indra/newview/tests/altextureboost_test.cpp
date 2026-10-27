/**
 * @file altextureboost_test.cpp
 * @brief Unit tests for what a texture's boost is made of
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

#include <catch2/catch_test_macros.hpp>

#include "../altextureboost.h"

#include "llgltexture.h"

namespace
{
    constexpr S32 NONE     = LLGLTexture::BOOST_NONE;
    constexpr S32 SCULPTED = LLGLTexture::BOOST_SCULPTED;
    constexpr S32 SELECTED = LLGLTexture::BOOST_SELECTED;
    constexpr S32 HUD      = LLGLTexture::BOOST_HUD;
    constexpr S32 UI       = LLGLTexture::BOOST_UI;
    constexpr S32 PREVIEW  = LLGLTexture::BOOST_PREVIEW;

    // The boost is the highest of the texture's own level, every level held and the frame's, and none of them
    // changes another: a sculpt map selected keeps its own level once the selection lets it go.
    TEST_CASE("ALTextureBoostLevels: the boost is the highest of its levels and each stays its own", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        CHECK(levels.level() == NONE);

        levels.setOwn(SCULPTED);
        CHECK(levels.level() == SCULPTED);

        levels.askForFrame(SELECTED, 10);
        CHECK(levels.level() == SELECTED);
        levels.addHold(PREVIEW);
        CHECK(levels.level() == PREVIEW);

        CHECK(levels.removeHold(PREVIEW));
        CHECK(levels.lapse(12));
        UNSCOPED_INFO("the own level, untouched by the frame's and the hold");
        CHECK(levels.level() == SCULPTED);
        CHECK(levels.own() == SCULPTED);

        levels.setOwn(NONE);
        CHECK(levels.level() == NONE);
    }

    // Holds are counted by holder: a level held twice stays until both holders let go, and letting go of a level not
    // held changes nothing.
    TEST_CASE("ALTextureBoostLevels: a hold lasts until its last holder lets go", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        levels.addHold(PREVIEW);
        levels.addHold(PREVIEW);
        levels.addHold(HUD);
        CHECK(levels.level() == PREVIEW);

        CHECK(levels.removeHold(PREVIEW));
        UNSCOPED_INFO("one of two holders gone");
        CHECK(levels.level() == PREVIEW);
        CHECK(levels.removeHold(PREVIEW));
        CHECK(levels.level() == HUD);

        UNSCOPED_INFO("a level not held");
        CHECK_FALSE(levels.removeHold(PREVIEW));
        CHECK(levels.level() == HUD);
        CHECK(levels.removeHold(HUD));
        CHECK(levels.level() == NONE);
    }

    // A level asked for frame by frame lasts through the frame after its asking, so a reader that comes before the
    // asker in a frame still sees it, and goes once a whole frame has passed without it.
    TEST_CASE("ALTextureBoostLevels: a frame's level lapses a frame after its last asking", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        CHECK_FALSE(levels.lapse(5));

        levels.askForFrame(PREVIEW, 100);
        CHECK_FALSE(levels.lapse(100));
        UNSCOPED_INFO("in the next frame, before it is asked for again");
        CHECK_FALSE(levels.lapse(101));
        CHECK(levels.level() == PREVIEW);

        for (U32 frame = 101; frame < 110; ++frame)
        {
            levels.askForFrame(PREVIEW, frame);
            CHECK_FALSE(levels.lapse(frame + 1));
        }
        CHECK(levels.level() == PREVIEW);

        UNSCOPED_INFO("a whole frame without it");
        CHECK(levels.lapse(111));
        CHECK(levels.level() == NONE);
        CHECK_FALSE(levels.lapse(112));
    }

    // Asked for more than once in a frame, the highest level stands; asked for in the next frame, the last frame's
    // stands beside it through that frame, and goes once that frame has passed without it.
    TEST_CASE("ALTextureBoostLevels: a frame's level is the highest asked for in its frame", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        levels.askForFrame(SELECTED, 7);
        levels.askForFrame(UI, 7);
        levels.askForFrame(SELECTED, 7);
        CHECK(levels.level() == UI);

        levels.askForFrame(SELECTED, 8);
        UNSCOPED_INFO("the last frame's, through this one");
        CHECK(levels.level() == UI);
        CHECK(levels.lapse(9));
        CHECK(levels.level() == SELECTED);
        CHECK(levels.lapse(10));
        CHECK(levels.level() == NONE);
    }

    // A selection asks for its textures before a swatch asks for the one it draws, every frame: the swatch's level,
    // the higher, holds throughout, read before the selection asks, between the two and after both.
    TEST_CASE("ALTextureBoostLevels: a lower level asked for earlier in each frame does not lower a higher one", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        levels.askForFrame(SELECTED, 20);
        levels.askForFrame(PREVIEW, 20);
        for (U32 frame = 21; frame < 30; ++frame)
        {
            CHECK_FALSE(levels.lapse(frame));
            CHECK(levels.level() == PREVIEW);
            levels.askForFrame(SELECTED, frame);
            CHECK(levels.level() == PREVIEW);
            levels.askForFrame(PREVIEW, frame);
            CHECK(levels.level() == PREVIEW);
        }

        UNSCOPED_INFO("the swatch closed: the selection's level, a frame on");
        levels.askForFrame(SELECTED, 30);
        CHECK(levels.level() == PREVIEW);
        CHECK(levels.lapse(31));
        CHECK(levels.level() == SELECTED);
    }

    // A level held by many holders is counted, not listed: each holder's letting go takes one, in any order.
    TEST_CASE("ALTextureBoostLevels: holds of many levels by many holders go in any order", "[ALTextureBoostLevels]")
    {
        ALTextureBoostLevels levels;
        for (int i = 0; i < 1000; ++i)
        {
            levels.addHold(HUD);
        }
        levels.addHold(PREVIEW);
        levels.addHold(SELECTED);
        CHECK(levels.level() == PREVIEW);

        CHECK(levels.removeHold(PREVIEW));
        CHECK(levels.level() == HUD);
        for (int i = 0; i < 999; ++i)
        {
            CHECK(levels.removeHold(HUD));
        }
        CHECK(levels.level() == HUD);
        CHECK(levels.removeHold(HUD));
        CHECK(levels.level() == SELECTED);
        CHECK_FALSE(levels.removeHold(HUD));
        CHECK(levels.removeHold(SELECTED));
        CHECK(levels.level() == NONE);
    }
}

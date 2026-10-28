/**
 * @file alcvarlayers_test.cpp
 * @brief The layers a value is worked out from: the user's writes and
 *        resets, the Session and Quality layers, owners' holds, clamps and
 *        restrictions, validation, the early return of an unchanged write,
 *        and strings replaced under a view.
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

#include "alcvartestschema.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <limits>

using namespace std::chrono_literals;

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;

    TEST_CASE_METHOD(TestStore, "alcvarlayers: the top layer present gives the value", "[alcvarlayers]")
    {
        ALCVars&  cvars = store();
        const U32 index = render::draw_distance.index();
        CHECK(cvars.layerOf(index) == ALCVarLayer::Default);

        const std::pair<U32, ALCVarValue> quality[] = { { index, ALCVarValue(96.f) } };
        cvars.setQuality(quality, false);
        CHECK(render::draw_distance() == 96.f);
        CHECK(cvars.layerOf(index) == ALCVarLayer::Quality);
        CHECK_FALSE(render::draw_distance.isUserSet());

        CHECK(render::draw_distance.set(256.f));
        CHECK(render::draw_distance() == 256.f);
        CHECK(cvars.layerOf(index) == ALCVarLayer::Global);
        CHECK(render::draw_distance.isUserSet());

        CHECK(cvars.setSession(index, ALCVarValue(512.f)));
        CHECK(render::draw_distance() == 512.f);
        CHECK(cvars.layerOf(index) == ALCVarLayer::Session);

        // The user's own write clears what the session put over it.
        CHECK(render::draw_distance.set(300.f));
        CHECK(render::draw_distance() == 300.f);
        CHECK_FALSE(cvars.layerValue(index, ALCVarLayer::Session));

        render::draw_distance.reset();
        CHECK(render::draw_distance() == 96.f);
        CHECK_FALSE(render::draw_distance.isUserSet());

        cvars.setQuality({}, false);
        CHECK(render::draw_distance() == 128.f);
        CHECK(cvars.layerOf(index) == ALCVarLayer::Default);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a user value is kept unless it is the default and no level gives one",
                     "[alcvarlayers]")
    {
        ALCVars&  cvars = store();
        const U32 index = render::draw_distance.index();

        // The compiled default, with no Quality entry: nothing to keep.
        CHECK(render::draw_distance.set(128.f));
        CHECK_FALSE(render::draw_distance.isUserSet());

        // The Quality layer's own value is kept: the user chose it, and it
        // stays theirs when the level's list changes.
        const std::pair<U32, ALCVarValue> quality[] = { { index, ALCVarValue(96.f) } };
        cvars.setQuality(quality, false);
        CHECK(render::draw_distance.set(96.f));
        CHECK(render::draw_distance.isUserSet());
        // The default over a level's value is a choice too.
        CHECK(render::draw_distance.set(128.f));
        CHECK(render::draw_distance.isUserSet());
        CHECK(render::draw_distance() == 128.f);

        const std::pair<U32, ALCVarValue> other[] = { { index, ALCVarValue(64.f) } };
        cvars.setQuality(other, false);
        CHECK(render::draw_distance() == 128.f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: choosing a level drops the user values its list names", "[alcvarlayers]")
    {
        ALCVars& cvars = store();
        CHECK(render::draw_distance.set(300.f));
        CHECK(shadows::detail.set(1));
        CHECK(shadows::resolution_scale.set(2.f));

        const std::pair<U32, ALCVarValue> level[] = { { render::draw_distance.index(), ALCVarValue(64.f) },
                                                      { shadows::detail.index(), ALCVarValue(S32(0)) } };
        cvars.setQuality(level, true);
        CHECK(render::draw_distance() == 64.f);
        CHECK(shadows::detail() == 0);
        CHECK_FALSE(render::draw_distance.isUserSet());
        CHECK_FALSE(shadows::detail.isUserSet());
        // Not on the level's list: the user's still.
        CHECK(shadows::resolution_scale() == 2.f);
        CHECK(shadows::resolution_scale.isUserSet());
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a group's reset drops every user value in it and below", "[alcvarlayers]")
    {
        CHECK(render::draw_distance.set(300.f));
        CHECK(shadows::detail.set(1));
        CHECK(render::exposure::dynamic::speed.set(4.f));
        CHECK(ui::scale.set(1.5f));

        shadows::group.reset();
        CHECK(shadows::detail() == 2);
        CHECK(render::draw_distance() == 300.f);

        render::group.reset();
        CHECK(render::draw_distance() == 128.f);
        CHECK(render::exposure::dynamic::speed() == 1.f);
        CHECK(ui::scale() == 1.5f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: holds from two owners released in either order", "[alcvarlayers]")
    {
        ALCVars&  cvars = store();
        const U32 index = render::draw_distance.index();
        CHECK(render::draw_distance.set(300.f));

        for (const bool firstFirst : { true, false })
        {
            INFO("first released first: " << firstFirst);
            cvars.hold("autotune", index, ALCVarValue(64.f));
            CHECK(render::draw_distance() == 64.f);
            CHECK(cvars.isHeld(index));
            cvars.hold("safemode", index, ALCVarValue(32.f));
            CHECK(render::draw_distance() == 32.f);

            // The user's writes go on underneath.
            CHECK(render::draw_distance.set(400.f));
            CHECK(render::draw_distance() == 32.f);

            cvars.release(firstFirst ? "autotune" : "safemode", index);
            CHECK(render::draw_distance() == (firstFirst ? 32.f : 64.f));
            cvars.release(firstFirst ? "safemode" : "autotune", index);
            CHECK(render::draw_distance() == 400.f);
            CHECK_FALSE(cvars.isHeld(index));
            CHECK(render::draw_distance.set(300.f));
        }
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: clamps from two owners take the tightest", "[alcvarlayers]")
    {
        ALCVars&  cvars = store();
        const U32 index = render::draw_distance.index();
        CHECK(render::draw_distance.set(900.f));

        cvars.clamp("rlva:object1", index, 0.0, 500.0);
        CHECK(render::draw_distance() == 500.f);
        cvars.clamp("rlva:object2", index, 0.0, 200.0);
        CHECK(render::draw_distance() == 200.f);
        cvars.clamp("rlva:object1", index, 300.0, 1000.0);
        // Nothing lies between them: the higher minimum wins.
        CHECK(render::draw_distance() == 300.f);

        cvars.release("rlva:object1", index);
        CHECK(render::draw_distance() == 200.f);
        cvars.releaseAll("rlva:object2");
        CHECK(render::draw_distance() == 900.f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: deny write refuses the user and hide hides", "[alcvarlayers]")
    {
        ALCVars&  cvars  = store();
        const U32 detail = shadows::detail.index();
        const U32 scale  = shadows::resolution_scale.index();

        cvars.denyWrite("rlva", ALCVarTarget::setting(detail));
        CHECK_FALSE(shadows::detail.set(1));
        CHECK(shadows::detail() == 2);
        CHECK(cvars.isWriteDenied(detail));
        CHECK(shadows::resolution_scale.set(2.f));

        cvars.denyWrite("rlva", ALCVarTarget::group(render::group.index()));
        CHECK_FALSE(shadows::resolution_scale.set(3.f));
        CHECK(shadows::resolution_scale() == 2.f);
        CHECK(ui::scale.set(1.25f));

        cvars.denyWrite("other", ALCVarTarget::setting(detail));
        cvars.allowWrite("rlva", ALCVarTarget::group(render::group.index()));
        CHECK(shadows::resolution_scale.set(3.f));
        cvars.releaseAll("rlva");
        // Still the other owner's.
        CHECK_FALSE(shadows::detail.set(1));
        cvars.allowWrite("other", ALCVarTarget::setting(detail));
        CHECK(shadows::detail.set(1));

        CHECK_FALSE(cvars.isHidden(scale));
        CHECK(cvars.isHidden(cvartest::net::translate::proxy.index()) == false);
        cvars.hide("rlva", ALCVarTarget::group(shadows::group.index()));
        CHECK(cvars.isHidden(scale));
        CHECK_FALSE(cvars.isHidden(render::draw_distance.index()));
        cvars.unhide("rlva", ALCVarTarget::group(shadows::group.index()));
        CHECK_FALSE(cvars.isHidden(scale));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a write is clamped to the range and refused outside the choices",
                     "[alcvarlayers]")
    {
        CHECK(shadows::resolution_scale.set(10.f));
        CHECK(shadows::resolution_scale() == 4.f);
        CHECK(shadows::resolution_scale.set(-1.f));
        CHECK(shadows::resolution_scale() == 0.25f);
        CHECK_FALSE(shadows::resolution_scale.set(std::numeric_limits<F32>::quiet_NaN()));
        CHECK_FALSE(shadows::resolution_scale.set(std::numeric_limits<F32>::infinity()));
        CHECK(shadows::resolution_scale() == 0.25f);

        CHECK_FALSE(shadows::detail.set(5));
        CHECK(shadows::detail() == 2);
        CHECK(shadows::detail.set(0));
        CHECK_FALSE(ui::toast::quality.set("best"));
        CHECK(ui::toast::quality.set("low"));
        CHECK(ui::toast::quality.view() == "low");

        CHECK(shadows::tint.set(LLColor4(2.f, -1.f, 0.5f, 1.f)));
        CHECK(shadows::tint() == LLColor4(1.f, 0.f, 0.5f, 1.f));
        CHECK(shadows::max_maps.set(100u));
        CHECK(shadows::max_maps() == 8u);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: an unchanged write takes no lock and moves no generation",
                     "[alcvarlayers]")
    {
        ALCVars& cvars = store();
        CHECK(render::draw_distance.set(256.f));
        CHECK(shadows::tint.set(LLColor4::red));
        CHECK(ui::font_name.set("Inter"));
        const U32 generation = render::draw_distance.generation();
        const U64 group      = render::group.generation();
        const U64 locks      = cvars.stats().locks;

        for (int i = 0; i < 1000000; ++i)
        {
            render::draw_distance.set(256.f);
        }
        for (int i = 0; i < 1000; ++i)
        {
            shadows::tint.set(LLColor4::red);
            ui::font_name.set("Inter");
            // Nothing to keep, nothing above it: still nothing to do.
            shadows::detail.set(2);
        }
        CHECK(cvars.stats().locks == locks);
        CHECK(render::draw_distance.generation() == generation);
        CHECK(render::group.generation() == group);

        // A write over a Session value is not unchanged: it clears it.
        CHECK(cvars.setSession(render::draw_distance.index(), ALCVarValue(256.f)));
        CHECK(render::draw_distance.set(256.f));
        CHECK_FALSE(cvars.layerValue(render::draw_distance.index(), ALCVarLayer::Session));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a write is not taken for unchanged while a hold lands under it",
                     "[alcvarlayers][slow]")
    {
        // A worker writes 5 then 7 while this thread holds the setting at 7
        // and lets it go: whichever lands first, the user's value is 7.
        ALCVars&          cvars = store();
        const U32         level = render::quality_level.index();
        std::atomic<bool> done{ false };
        std::atomic<U64>  lost{ 0 };
        std::atomic<U64>  pairs{ 0 };
        auto              worker = std::async(std::launch::async, [&] {
            while (!done.load(std::memory_order_acquire))
            {
                render::quality_level.set(5);
                render::quality_level.set(7);
                const auto user = cvars.layerValue(level, ALCVarLayer::Global);
                if (!user || !(*user == ALCVarValue(S32(7))))
                {
                    ++lost;
                }
                ++pairs;
            }
        });
        const auto until = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < until && lost.load() == 0)
        {
            cvars.hold("autotune", level, ALCVarValue(S32(7)));
            cvars.release("autotune", level);
        }
        done = true;
        worker.get();
        INFO(pairs.load() << " pairs of writes");
        CHECK(lost.load() == 0);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a setting whose writes are denied keeps its user value through every reset",
                     "[alcvarlayers]")
    {
        ALCVars&  cvars    = store();
        const U32 distance = render::draw_distance.index();
        CHECK(render::draw_distance.set(300.f));
        cvars.denyWrite("rlva", ALCVarTarget::setting(distance));
        render::draw_distance.reset();
        render::group.reset();
        const std::pair<U32, ALCVarValue> level[] = { { distance, ALCVarValue(64.f) } };
        cvars.setQuality(level, true);
        REQUIRE(cvars.layerValue(distance, ALCVarLayer::Global));
        CHECK(*cvars.layerValue(distance, ALCVarLayer::Global) == ALCVarValue(300.f));
        // Allowed again, the user's value is still theirs.
        cvars.allowWrite("rlva", ALCVarTarget::setting(distance));
        CHECK(render::draw_distance() == 300.f);
        render::draw_distance.reset();
        CHECK(render::draw_distance() == 64.f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a hold is fitted as a write and other owners' clamps bound it",
                     "[alcvarlayers]")
    {
        ALCVars&  cvars    = store();
        const U32 distance = render::draw_distance.index();
        // Not one of the choices: refused.
        cvars.hold("autotune", shadows::detail.index(), ALCVarValue(S32(7)));
        CHECK(shadows::detail() == 2);
        CHECK_FALSE(cvars.isHeld(shadows::detail.index()));
        // Past the range: clamped into it.
        cvars.hold("autotune", distance, ALCVarValue(5000.f));
        CHECK(render::draw_distance() == 1024.f);
        // Another owner's bounds over the hold, the tightest winning.
        cvars.clamp("rlva", distance, 0.0, 64.0);
        CHECK(render::draw_distance() == 64.f);
        cvars.hold("autotune", distance, ALCVarValue(512.f));
        CHECK(render::draw_distance() == 64.f);
        cvars.release("rlva", distance);
        CHECK(render::draw_distance() == 512.f);
        // Bounds past the range keep the value in it.
        cvars.clamp("rlva", distance, 2000.0, 3000.0);
        CHECK(render::draw_distance() == 1024.f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a clamp past what the type holds stays within it", "[alcvarlayers]")
    {
        CHECK(ALCVarValues::clampNumber(ALCVarValue(U32(5)), 5e9, 6e9) == ALCVarValue(std::numeric_limits<U32>::max()));
        CHECK(ALCVarValues::clampNumber(ALCVarValue(S32(5)), 3e9, 4e9) == ALCVarValue(std::numeric_limits<S32>::max()));
        CHECK(ALCVarValues::clampNumber(ALCVarValue(S32(5)), -4e9, -3e9) ==
              ALCVarValue(std::numeric_limits<S32>::lowest()));
        CHECK(ALCVarValues::clampNumber(ALCVarValue(S64(5)), 1e19, 2e19).get<S64>() > S64(9e18));
        CHECK(ALCVarValues::clampNumber(ALCVarValue(1.f), 1e300, 1e301) ==
              ALCVarValue(std::numeric_limits<F32>::max()));

        // An owner's bound that is no number is refused.
        ALCVars&  cvars = store();
        const U32 scale = shadows::resolution_scale.index();
        cvars.clamp("autotune", scale, std::numeric_limits<F64>::infinity(), std::numeric_limits<F64>::infinity());
        cvars.clamp("rlva", scale, std::numeric_limits<F64>::quiet_NaN(), 2.0);
        CHECK(shadows::resolution_scale() == 1.f);
        CHECK_FALSE(cvars.isHeld(scale));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a file setting refuses a name Windows opens as a device",
                     "[alcvarlayers]")
    {
        for (const char* device : { "NUL.wav", "nul", "COM1", "con.ogg", "LPT9.wav", "AUX .wav" })
        {
            INFO(device);
            CHECK_FALSE(ui::toast::sound.set(device));
        }
        CHECK(ui::toast::sound.view().empty());
        CHECK(ui::toast::sound.set("chime.wav"));
        CHECK(ui::toast::sound.view() == "chime.wav");
        CHECK(ui::toast::sound.set("COM10.wav"));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: generations move with each change of the effective value",
                     "[alcvarlayers]")
    {
        const U32 setting = shadows::resolution_scale.generation();
        const U64 group   = shadows::group.generation();
        const U64 parent  = render::group.generation();
        const U64 sibling = render::exposure::group.generation();
        const U64 tag     = cvartest::tags::resource_input.generation();
        const U64 other   = cvartest::tags::shader_input.generation();

        CHECK(shadows::resolution_scale.set(2.f));
        CHECK(shadows::resolution_scale.generation() == setting + 1);
        CHECK(shadows::group.generation() == group + 1);
        CHECK(render::group.generation() == parent + 1);
        CHECK(render::exposure::group.generation() == sibling);
        CHECK(cvartest::tags::resource_input.generation() == tag + 1);
        CHECK(cvartest::tags::shader_input.generation() == other);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: a string replaced under a view stays readable to the frame's end",
                     "[alcvarlayers]")
    {
        ALCVars& cvars = store();
        CHECK(ui::font_name.set("Inter"));
        cvars.endFrame();
        const std::string_view before = ui::font_name.view();
        const auto             shared = ui::font_name.share();

        // From another thread, as a worker's write would come.
        std::async(std::launch::async, [] {
            CHECK(ui::font_name.set("Fira Sans, a name long enough to be its own allocation"));
        }).get();
        CHECK(ui::font_name.view() == "Fira Sans, a name long enough to be its own allocation");
        CHECK(before == "Inter");
        CHECK(*shared == "Inter");
        CHECK(cvars.retiredCount() == 1);

        cvars.endFrame();
        CHECK(cvars.retiredCount() == 0);
        // The share keeps its own.
        CHECK(*shared == "Inter");
    }

    TEST_CASE_METHOD(TestStore, "alcvarlayers: every type is written and read back through its handle", "[alcvarlayers]")
    {
        const LLUUID id("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d");
        CHECK(render::vintage::enabled.set(true));
        CHECK(render::frame_count.set(S64(1) << 40));
        CHECK(shadows::bias.set(0.25));
        CHECK(shadows::ambient.set(LLColor3(0.5f, 0.25f, 0.125f)));
        CHECK(shadows::offset.set(LLVector3(1.f, 2.f, 3.f)));
        CHECK(shadows::anchor.set(LLVector3d(1.0e7, 2.0, 3.0)));
        CHECK(shadows::rotation.set(LLQuaternion(0.f, 0.f, 0.70710677f, 0.70710677f)));
        CHECK(shadows::uv_scale.set(LLVector2(3.f, 4.f)));
        CHECK(shadows::screen_rect.set(LLRect(5, 60, 70, 8)));
        CHECK(ui::sounds::click.set(id));
        CHECK(ui::sounds::blocked.set(std::vector<LLUUID>{ id, LLUUID::null }));
        CHECK(ui::recent.set(std::vector<std::string>{ "x" }));
        CHECK(ui::curve.set(std::vector<LLVector2>{ LLVector2(0.5f, 0.25f) }));
        LLSD layout;
        layout["pane"] = 3;
        CHECK(ui::layout.set(layout));

        CHECK(render::vintage::enabled());
        CHECK(render::frame_count() == S64(1) << 40);
        CHECK(shadows::bias() == 0.25);
        CHECK(shadows::ambient() == LLColor3(0.5f, 0.25f, 0.125f));
        CHECK(shadows::offset() == LLVector3(1.f, 2.f, 3.f));
        CHECK(shadows::anchor() == LLVector3d(1.0e7, 2.0, 3.0));
        CHECK(shadows::rotation() == LLQuaternion(0.f, 0.f, 0.70710677f, 0.70710677f));
        CHECK(shadows::uv_scale() == LLVector2(3.f, 4.f));
        CHECK(shadows::screen_rect() == LLRect(5, 60, 70, 8));
        CHECK(ui::sounds::click() == id);
        CHECK(ui::sounds::blocked() == std::vector<LLUUID>{ id, LLUUID::null });
        CHECK(ui::sounds::blocked.view().size() == 2);
        CHECK(ui::recent() == std::vector<std::string>{ "x" });
        CHECK(ui::curve() == std::vector<LLVector2>{ LLVector2(0.5f, 0.25f) });
        CHECK(ui::layout()["pane"].asInteger() == 3);

        const shadows::Values values = shadows::read();
        CHECK(values.anchor == LLVector3d(1.0e7, 2.0, 3.0));
        CHECK(values.screen_rect == LLRect(5, 60, 70, 8));
    }
}

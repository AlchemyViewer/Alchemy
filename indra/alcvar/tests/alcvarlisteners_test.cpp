/**
 * @file alcvarlisteners_test.cpp
 * @brief Listeners, batches and validators: told once per setting per batch,
 *        of changes off the main thread at its pump, of changes listeners
 *        make in the same pass, and never of a write that changed nothing.
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

#include "altestfatal.h"
#include "llthread.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;
    namespace world   = cvartest::world;

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a listener is given the new value and the old after the slot",
                     "[alcvarlisteners]")
    {
        std::vector<std::pair<F32, F32>> told;
        F32                              seen = 0.f;
        auto connection = render::draw_distance.onChange([&](F32 value, F32 old) {
            told.emplace_back(value, old);
            seen = render::draw_distance();
        });
        CHECK(render::draw_distance.set(256.f));
        CHECK(render::draw_distance.set(256.f));
        CHECK(render::draw_distance.set(300.f));
        REQUIRE(told.size() == 2);
        CHECK(told[0] == std::pair(256.f, 128.f));
        CHECK(told[1] == std::pair(300.f, 256.f));
        CHECK(seen == 300.f);

        render::draw_distance.reset();
        REQUIRE(told.size() == 3);
        CHECK(told[2] == std::pair(128.f, 300.f));

        connection.disconnect();
        CHECK(render::draw_distance.set(64.f));
        CHECK(told.size() == 3);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a batch tells each changed setting once when it ends",
                     "[alcvarlisteners]")
    {
        std::vector<std::pair<F32, F32>> distances;
        std::vector<S32>                 details;
        auto a = render::draw_distance.onChange([&](F32 value, F32 old) { distances.emplace_back(value, old); });
        auto b = shadows::detail.onChange([&](S32 value, S32) { details.push_back(value); });
        {
            ALCVars::Batch batch;
            CHECK(render::draw_distance.set(200.f));
            CHECK(render::draw_distance.set(300.f));
            CHECK(shadows::detail.set(1));
            CHECK(shadows::detail.set(2));
            {
                ALCVars::Batch inner;
                CHECK(render::draw_distance.set(400.f));
            }
            CHECK(distances.empty());
            // The values are there before anyone is told.
            CHECK(render::draw_distance() == 400.f);
        }
        REQUIRE(distances.size() == 1);
        CHECK(distances[0] == std::pair(400.f, 128.f));
        // Changed and changed back: nothing to tell.
        CHECK(details.empty());
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a change off the main thread is told at the pump",
                     "[alcvarlisteners]")
    {
        ALCVars&                         cvars = store();
        std::vector<std::pair<F32, F32>> told;
        auto connection = render::draw_distance.onChange([&](F32 value, F32 old) { told.emplace_back(value, old); });

        std::async(std::launch::async, [] {
            CHECK(render::draw_distance.set(200.f));
            CHECK(render::draw_distance.set(300.f));
        }).get();
        // Visible to every thread at once; told to listeners on the main one.
        CHECK(render::draw_distance() == 300.f);
        CHECK(told.empty());

        cvars.pump();
        REQUIRE(told.size() == 1);
        CHECK(told[0] == std::pair(300.f, 128.f));

        // A main-thread write after one off it gives the older old value.
        std::async(std::launch::async, [] { CHECK(render::draw_distance.set(350.f)); }).get();
        CHECK(render::draw_distance.set(360.f));
        REQUIRE(told.size() == 2);
        CHECK(told[1] == std::pair(360.f, 300.f));
        cvars.pump();
        CHECK(told.size() == 2);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a listener's own writes are told in the same pass",
                     "[alcvarlisteners]")
    {
        std::vector<std::string> order;
        auto first = render::draw_distance.onChange([&](F32 value, F32) {
            order.push_back("distance");
            CHECK(shadows::resolution_scale.set(value / 128.f));
        });
        auto second = shadows::resolution_scale.onChange([&](F32 value, F32) {
            order.push_back("scale");
            CHECK(shadows::max_maps.set(U32(value * 3.f)));
        });
        auto third = shadows::max_maps.onChange([&](U32, U32) { order.push_back("maps"); });

        CHECK(render::draw_distance.set(256.f));
        CHECK(order == std::vector<std::string>{ "distance", "scale", "maps" });
        CHECK(shadows::max_maps() == 6u);
        CHECK(shadows::resolution_scale() == 2.f);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: two settings mirroring each other settle", "[alcvarlisteners]")
    {
        int  told = 0;
        auto a    = render::quality_level.onChange([&](S32 value, S32) {
            ++told;
            CHECK(shadows::detail.set(std::min(value, 2)));
        });
        auto b    = shadows::detail.onChange([&](S32 value, S32) {
            ++told;
            CHECK(render::quality_level.set(value));
        });
        CHECK(render::quality_level.set(1));
        CHECK(render::quality_level() == 1);
        CHECK(shadows::detail() == 1);
        CHECK(told == 2);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a pass that keeps feeding itself asserts", "[alcvarlisteners]")
    {
        auto a = render::quality_level.onChange([](S32 value, S32) { render::frame_count.set(S64(value) + 1); });
        auto b = render::frame_count.onChange([](S64 value, S64) { render::quality_level.set(S32(value % 7) + 1); });
        REQUIRE_THROWS_AS(render::quality_level.set(5), ll_test::FatalError);

        // The store goes on.
        a.disconnect();
        b.disconnect();
        int  told = 0;
        auto c    = render::draw_distance.onChange([&](F32, F32) { ++told; });
        CHECK(render::draw_distance.set(256.f));
        CHECK(told == 1);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a listener disconnects itself and another during a pass",
                     "[alcvarlisteners]")
    {
        int              first = 0;
        int              second = 0;
        ALCVarConnection a;
        ALCVarConnection b;
        a = render::draw_distance.onChange([&](F32, F32) {
            ++first;
            a.disconnect();
            b.disconnect();
        });
        b = render::draw_distance.onChange([&](F32, F32) { ++second; });
        CHECK(a.connected());
        CHECK(render::draw_distance.set(256.f));
        CHECK(first == 1);
        CHECK(second == 0);
        CHECK_FALSE(a.connected());
        CHECK_FALSE(b.connected());
        CHECK(render::draw_distance.set(300.f));
        CHECK(first == 1);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a group's listener hears of every change below it",
                     "[alcvarlisteners]")
    {
        std::vector<std::string> changed;
        auto connection = render::group.onChange([&](const ALCVarDesc& desc) { changed.emplace_back(desc.name); });
        CHECK(shadows::detail.set(1));
        CHECK(render::draw_distance.set(256.f));
        CHECK(render::exposure::dynamic::enabled.set(false));
        CHECK(ui::scale.set(1.5f));
        CHECK(changed == std::vector<std::string>{ "render.shadows.detail", "render.draw_distance",
                                                    "render.exposure.dynamic.enabled" });
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a schema naming an unregistered hook asserts once startup is done",
                     "[alcvarlisteners]")
    {
        ALCVars& cvars = store();
        REQUIRE_THROWS_AS(cvars.checkHooks(), ll_test::FatalError);
        cvars.registerValidator("maturity", [](const ALCVarDesc&, ALCVarValue&) { return true; });
        REQUIRE_THROWS_AS(cvars.checkHooks(), ll_test::FatalError);
        const ALCVarMigration nothing = [](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>();
        };
        cvars.registerMigration("fog_from_old", nothing);
        // A setting's sources' hooks too.
        REQUIRE_THROWS_AS(cvars.checkHooks(), ll_test::FatalError);
        for (const char* hook : { "sky_tint_from_two", "azimuth_from_angles", "elevation_from_angles", "gust_from_wind" })
        {
            cvars.registerMigration(hook, nothing);
        }
        CHECK_NOTHROW(cvars.checkHooks());
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a validator refuses or adjusts a write", "[alcvarlisteners]")
    {
        ALCVars& cvars = store();
        // Adult content is refused; Moderate asked for twice is Moderate.
        cvars.registerValidator("maturity", [](const ALCVarDesc&, ALCVarValue& value) {
            return value.get<S32>() != 42;
        });
        CHECK_FALSE(world::maturity.set(42));
        CHECK(world::maturity() == 13);
        CHECK(world::maturity.set(21));
        CHECK(world::maturity() == 21);

        // What a validator gives back is fitted to the choices again.
        cvars.registerValidator("maturity", [](const ALCVarDesc&, ALCVarValue& value) {
            value = ALCVarValue(S32(7));
            return true;
        });
        CHECK_FALSE(world::maturity.set(13));
        CHECK(world::maturity() == 21);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a main-thread validator's off-thread write is posted to it",
                     "[alcvarlisteners]")
    {
        ALCVars&        cvars = store();
        LLThread::id_t  ranOn;
        cvars.registerValidator(
            "maturity",
            [&ranOn](const ALCVarDesc&, ALCVarValue& value) {
                ranOn = LLThread::currentID();
                return value.get<S32>() != 42;
            },
            ALCVars::HookThread::Main);

        std::async(std::launch::async, [] {
            CHECK(world::maturity.set(21));
            CHECK(world::maturity.set(42));
        }).get();
        CHECK(world::maturity() == 13);
        CHECK(ranOn == LLThread::id_t());

        cvars.pump();
        CHECK(ranOn == LLThread::currentID());
        // The refused one refused there.
        CHECK(world::maturity() == 21);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a worker's writes posted to the main thread land in their order",
                     "[alcvarlisteners]")
    {
        ALCVars& cvars = store();
        cvars.registerValidator("maturity", [](const ALCVarDesc&, ALCVarValue&) { return true; },
                                ALCVars::HookThread::Main);

        // The second is the value the slot holds already, and still goes
        // after the first.
        std::async(std::launch::async, [] {
            CHECK(world::maturity.set(21));
            CHECK(world::maturity.set(13));
        }).get();
        cvars.pump();
        CHECK(world::maturity() == 13);

        // A reset after a write.
        std::async(std::launch::async, [] {
            CHECK(world::maturity.set(42));
            world::maturity.reset();
        }).get();
        cvars.pump();
        CHECK(world::maturity() == 13);
        CHECK_FALSE(world::maturity.isUserSet());

        // A Session value placed and cleared.
        std::async(std::launch::async, [&cvars] {
            CHECK(cvars.setSession(world::maturity.index(), ALCVarValue(S32(21))));
            cvars.clearSession(world::maturity.index());
        }).get();
        cvars.pump();
        CHECK(world::maturity() == 13);
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a Session value is refused by the setting's validator",
                     "[alcvarlisteners]")
    {
        ALCVars&  cvars    = store();
        const U32 maturity = world::maturity.index();
        cvars.registerValidator("maturity", [](const ALCVarDesc&, ALCVarValue& value) {
            return value.get<S32>() != 42;
        });
        CHECK_FALSE(cvars.setSession(maturity, ALCVarValue(S32(42))));
        CHECK(world::maturity() == 13);
        CHECK(cvars.setSession(maturity, ALCVarValue(S32(21))));
        CHECK(world::maturity() == 21);
        cvars.clearSession(maturity);

        // The main thread's validator: posted to it, and refused there.
        cvars.registerValidator(
            "maturity", [](const ALCVarDesc&, ALCVarValue& value) { return value.get<S32>() != 42; },
            ALCVars::HookThread::Main);
        std::async(std::launch::async, [&cvars, maturity] {
            CHECK(cvars.setSession(maturity, ALCVarValue(S32(42))));
        }).get();
        CHECK(world::maturity() == 13);
        cvars.pump();
        CHECK(world::maturity() == 13);
        CHECK_FALSE(cvars.layerValue(maturity, ALCVarLayer::Session));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a listener that owns another's connection is let go whole",
                     "[alcvarlisteners]")
    {
        int  heard = 0;
        auto owned = render::draw_distance.onChange([&heard](F32, F32) { ++heard; });
        auto owner = render::quality_level.onChange([owned = std::move(owned)](S32, S32) {});
        owner.disconnect();
        CHECK(render::draw_distance.set(300.f));
        CHECK(heard == 0);

        // Let go during a pass, by the listener being told.
        auto              inner = render::draw_distance.onChange([&heard](F32, F32) { ++heard; });
        ALCVarConnection  outer;
        outer = render::quality_level.onChange([&outer, inner = std::move(inner)](S32, S32) { outer.disconnect(); });
        CHECK(render::quality_level.set(5));
        CHECK(render::draw_distance.set(400.f));
        CHECK(heard == 0);
        CHECK_FALSE(outer.connected());
    }

    TEST_CASE("alcvarlisteners: a connection a listener owns is let go with the store", "[alcvarlisteners]")
    {
        ALCVars::instance().registerSchema(cvartest::schema());
        auto owned = render::draw_distance.onChange([](F32, F32) {});
        render::quality_level.onChange([owned = std::move(owned)](S32, S32) {}).release();
        auto group = render::shadows::group.onChange([](const ALCVarDesc&) {});
        render::shadows::detail.onChange([group = std::move(group)](S32, S32) {}).release();
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        CHECK_FALSE(ALCVars::instanceExists());
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a setting a listener writes while it waits in the pass is told once",
                     "[alcvarlisteners]")
    {
        std::vector<std::pair<F32, F32>> told;
        auto first = render::quality_level.onChange([](S32, S32) { render::draw_distance.set(500.f); });
        auto then  = render::draw_distance.onChange([&told](F32 value, F32 old) { told.emplace_back(value, old); });
        {
            ALCVars::Batch batch;
            CHECK(render::quality_level.set(5));
            CHECK(render::draw_distance.set(300.f));
        }
        REQUIRE(told.size() == 1);
        CHECK(told[0] == std::make_pair(500.f, 128.f));

        // Written again once told: the next round, from what it was told.
        told.clear();
        auto again = render::draw_distance.onChange([](F32 value, F32) {
            if (value < 600.f)
            {
                render::draw_distance.set(600.f);
            }
        });
        CHECK(render::draw_distance.set(550.f));
        REQUIRE(told.size() == 2);
        CHECK(told[0] == std::make_pair(550.f, 500.f));
        CHECK(told[1] == std::make_pair(600.f, 550.f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarlisteners: a batch whose pass feeds itself fails the case and not the run",
                     "[alcvarlisteners]")
    {
        auto a = render::quality_level.onChange([](S32 value, S32) { render::frame_count.set(S64(value) + 1); });
        auto b = render::frame_count.onChange([](S64 value, S64) { render::quality_level.set(S32(value % 7) + 1); });
        REQUIRE_THROWS_AS(
            [] {
                ALCVars::Batch batch;
                render::quality_level.set(5);
            }(),
            ll_test::FatalError);
        a.disconnect();
        b.disconnect();

        // Unwound by another exception, its pass waits for the next one.
        int  told = 0;
        auto c    = render::draw_distance.onChange([&told](F32, F32) { ++told; });
        CHECK_THROWS_AS(
            [] {
                ALCVars::Batch batch;
                render::draw_distance.set(256.f);
                throw std::runtime_error("unwound");
            }(),
            std::runtime_error);
        CHECK(told == 0);
        store().pump();
        CHECK(told == 1);
    }

    TEST_CASE("alcvarlisteners: a batch kept past its store ends nothing on the next", "[alcvarlisteners]")
    {
        ALCVars::instance().registerSchema(cvartest::schema());
        auto batch = std::make_unique<ALCVars::Batch>();
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        ALCVars::instance().registerSchema(cvartest::schema());
        int  told = 0;
        auto c    = render::draw_distance.onChange([&told](F32, F32) { ++told; });
        batch.reset();
        // The new store has no batch open: a write is told at once.
        CHECK(render::draw_distance.set(256.f));
        CHECK(told == 1);
        c.disconnect();
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
    }

    TEST_CASE("alcvarlisteners: a handle with no store reads no generation and makes none", "[alcvarlisteners]")
    {
        REQUIRE_FALSE(ALCVars::instanceExists());
        CHECK(render::draw_distance.generation() == 0);
        CHECK(render::group.generation() == 0);
        CHECK(cvartest::tags::shader_input.generation() == 0);
        // A worker's write, the main thread waiting for it.
        auto write = std::async(std::launch::async, [] { return render::draw_distance.set(300.f); });
        REQUIRE(write.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        CHECK_FALSE(write.get());
        CHECK_FALSE(ALCVars::instanceExists());
    }
}

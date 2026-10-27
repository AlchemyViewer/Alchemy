/**
 * @file alcvarthreads_test.cpp
 * @brief Settings read on other threads while they are written: small values
 *        never torn, a group's read() never half of a change, strings shared
 *        across the main thread's frames.
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

namespace
{
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;

    using namespace std::chrono_literals;

    constexpr int WRITES = 20000;

    // Runs `reader` on two threads for as long as `writer` writes, and
    // returns how many reads they made.
    template <typename Writer, typename Reader>
    U64 race(Writer writer, Reader reader)
    {
        std::atomic<bool> done{ false };
        std::atomic<U64>  reads{ 0 };
        std::atomic<bool> torn{ false };
        auto              read = [&] {
            U64 count = 0;
            while (!done.load(std::memory_order_acquire))
            {
                if (!reader())
                {
                    torn = true;
                }
                ++count;
            }
            reads += count;
        };
        auto a = std::async(std::launch::async, read);
        auto b = std::async(std::launch::async, read);
        writer();
        done = true;
        a.get();
        b.get();
        CHECK_FALSE(torn.load());
        return reads.load();
    }

    TEST_CASE_METHOD(TestStore, "alcvarthreads: a small value is never seen torn", "[alcvarthreads][slow]")
    {
        // Writers store values whose components are all one number, and two
        // other writers race them; readers check every value they see.
        const U64 reads = race(
            [] {
                auto other = std::async(std::launch::async, [] {
                    for (int i = 0; i < WRITES; ++i)
                    {
                        const F32 x = F32(i % 97) / 97.f;
                        shadows::tint.set(LLColor4(x, x, x, x));
                        shadows::anchor.set(LLVector3d(i, i, i));
                    }
                });
                for (int i = 0; i < WRITES; ++i)
                {
                    const F32 x = F32(i % 89) / 89.f;
                    shadows::tint.set(LLColor4(x, x, x, x));
                    shadows::anchor.set(LLVector3d(-i, -i, -i));
                    U8 bytes[UUID_BYTES];
                    std::fill(std::begin(bytes), std::end(bytes), U8(i));
                    ui::sounds::click.set(LLUUID(bytes));
                }
                other.get();
            },
            [] {
                const LLColor4   tint   = shadows::tint();
                const LLVector3d anchor = shadows::anchor();
                const LLUUID     click  = ui::sounds::click();
                bool             whole  = tint.mV[0] == tint.mV[1] && tint.mV[1] == tint.mV[2] && tint.mV[2] == tint.mV[3] &&
                              anchor.mdV[0] == anchor.mdV[1] && anchor.mdV[1] == anchor.mdV[2];
                for (int i = 1; i < UUID_BYTES; ++i)
                {
                    whole = whole && click.mData[i] == click.mData[0];
                }
                return whole || click == LLUUID("4c8c3c77-de8d-bde2-b9b8-32635e0fd4a6");
            });
        CHECK(reads > 0);
    }

    TEST_CASE_METHOD(TestStore, "alcvarthreads: a group's read sees a change of several settings whole",
                     "[alcvarthreads][slow]")
    {
        ALCVars&  cvars = store();
        const U32 maps  = shadows::max_maps.index();
        const U32 uv    = shadows::uv_scale.index();
        const U32 bias  = shadows::bias.index();
        const U64 reads = race(
            [&] {
                for (int i = 0; i < WRITES; ++i)
                {
                    const U32                         n        = U32(i % 8) + 1;
                    const std::pair<U32, ALCVarValue> level[] = { { maps, ALCVarValue(n) },
                                                                  { uv, ALCVarValue(LLVector2(F32(n), F32(n))) },
                                                                  { bias, ALCVarValue(F64(n) / 8.0) } };
                    cvars.setQuality(level, false);
                }
            },
            [] {
                const shadows::Values values = shadows::read();
                // Before the first level, the defaults; after, one level's.
                if (values.max_maps == 4u && values.uv_scale == LLVector2(1.f, 2.f) && values.bias == 0.001)
                {
                    return true;
                }
                return F32(values.max_maps) == values.uv_scale.mV[0] && values.uv_scale.mV[0] == values.uv_scale.mV[1] &&
                       F64(values.max_maps) / 8.0 == values.bias;
            });
        CHECK(reads > 0);
    }

    TEST_CASE_METHOD(TestStore, "alcvarthreads: a worker's share of a string outlives the frames it spans",
                     "[alcvarthreads][slow]")
    {
        ALCVars&          cvars = store();
        std::atomic<bool> done{ false };
        std::atomic<bool> bad{ false };
        auto              worker = std::async(std::launch::async, [&] {
            while (!done.load(std::memory_order_acquire))
            {
                const auto        name = ui::font_name.share();
                const std::string copy = ui::font_name();
                if (name->empty() || (name->find("Face") == std::string::npos && *name != "DejaVu Sans"))
                {
                    bad = true;
                }
                if (copy.empty())
                {
                    bad = true;
                }
            }
        });
        for (int i = 0; i < WRITES; ++i)
        {
            CHECK(ui::font_name.set("Face number " + std::to_string(i) + ", long enough to live on the heap"));
            if (i % 16 == 0)
            {
                cvars.endFrame();
            }
        }
        done = true;
        worker.get();
        cvars.endFrame();
        CHECK_FALSE(bad.load());
    }

    TEST_CASE_METHOD(TestStore, "alcvarthreads: listeners are told each change from the value they last had",
                     "[alcvarthreads][slow]")
    {
        // A worker and this thread write the same setting; each change told
        // starts from the value the one before it ended on.
        namespace render = cvartest::render;
        ALCVars&  cvars  = store();
        F32       last   = render::draw_distance();
        U64       told   = 0;
        U64       broken = 0;
        auto      heard  = render::draw_distance.onChange([&](F32 value, F32 old) {
            if (old != last)
            {
                ++broken;
            }
            last = value;
            ++told;
        });
        std::atomic<bool> done{ false };
        auto              worker = std::async(std::launch::async, [&done] {
            for (U32 i = 0; !done.load(std::memory_order_acquire); ++i)
            {
                render::draw_distance.set(F32(100 + i % 400));
            }
        });
        const auto until = std::chrono::steady_clock::now() + 2s;
        for (U32 i = 0; std::chrono::steady_clock::now() < until && broken == 0; ++i)
        {
            render::draw_distance.set(F32(600 + i % 400));
            cvars.pump();
        }
        done = true;
        worker.get();
        cvars.pump();
        INFO(told << " changes told");
        CHECK(broken == 0);
        CHECK(last == render::draw_distance());
    }
}

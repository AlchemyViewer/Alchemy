/**
 * @file alcvar_bench.cpp
 * @brief What reading and writing settings costs, each beside a control that
 *        does the least the same work could: the budgets the store is held to.
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
#include "cvartest/perf/all.h"

#include "aldbfaultvfs.h"
#include "altestbenchmark.h"
#include "llcoros.h"
#include "lltimer.h"

#include LLCOROS_MUTEX_HEADER

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;
    namespace grade   = cvartest::perf::grade;

    // The fixture of a benchmark: a store over the test schema with its
    // global file open, as the viewer's is.
    struct BenchStore
    {
        ll_test::ALDbTempDir dir;
        TestStore            test;

        BenchStore() { REQUIRE(test.store().openGlobal(dir.file("settings.db"))); }
        ALCVars& store() { return test.store(); }
    };

    TEST_CASE("alcvar_bench: reads", "[alcvar_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        BenchStore bench;
        CHECK(ui::font_name.set("Inter"));

        std::atomic<U32> plain{ 0x43000000u };
        BENCHMARK("control: a relaxed load of a std::atomic<U32>")
        {
            return plain.load(std::memory_order_relaxed);
        };
        BENCHMARK("a scalar handle read, F32")
        {
            return render::draw_distance();
        };
        BENCHMARK("a scalar handle read, bool")
        {
            return shadows::soft();
        };

        struct alignas(8) Words
        {
            std::atomic<U64> sequence{ 0 };
            std::atomic<U64> words[2] = { 0, 0 };
        };
        static Words control;
        BENCHMARK("control: two relaxed loads of std::atomic<U64>")
        {
            return control.words[0].load(std::memory_order_relaxed) ^ control.words[1].load(std::memory_order_relaxed);
        };
        BENCHMARK("a small value handle read, LLColor4")
        {
            return shadows::tint();
        };
        BENCHMARK("a string view on the main thread")
        {
            return ui::font_name.view().size();
        };

        static grade::Values copy{};
        BENCHMARK("control: a copy of a group's values, unsequenced")
        {
            grade::Values out;
            std::memcpy(static_cast<void*>(&out), &copy, sizeof(out));
            Catch::Benchmark::keep_memory(&out);
            return out.gain_0;
        };
        BENCHMARK("read() of a group of 71 values")
        {
            const grade::Values values = grade::read();
            return values.gain_39 + F32(values.mode_9) + values.tint_4.mV[3];
        };
    }

    TEST_CASE("alcvar_bench: a small value read with a writer on another thread", "[alcvar_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        BenchStore        bench;
        std::atomic<bool> done{ false };
        auto              writer = std::async(std::launch::async, [&] {
            U32 i = 0;
            while (!done.load(std::memory_order_relaxed))
            {
                const F32 x = F32(++i % 64) / 64.f;
                shadows::tint.set(LLColor4(x, x, x, 1.f));
            }
        });
        BENCHMARK("a small value handle read, LLColor4, written meanwhile")
        {
            return shadows::tint();
        };
        done = true;
        writer.get();
    }

    TEST_CASE("alcvar_bench: writes", "[alcvar_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        BenchStore bench;
        ALCVars&   cvars = bench.store();
        CHECK(render::draw_distance.set(256.f));

        LLCoros::Mutex   mutex;
        std::atomic<U32> word{ 0 };
        U32              n = 0;
        BENCHMARK("control: a mutex of the write lock's kind taken and a word stored")
        {
            LLCoros::LockType lock(mutex);
            word.store(++n, std::memory_order_relaxed);
            return n;
        };
        BENCHMARK("set() of an unchanged value")
        {
            return render::draw_distance.set(256.f);
        };

        U32 i = 0;
        BENCHMARK("set() with no listener, the file open")
        {
            return shadows::resolution_scale.set((++i & 1) ? 2.f : 3.f);
        };

        U32  told       = 0;
        auto connection = shadows::bias.onChange([&told](F64, F64) { ++told; });
        BENCHMARK("set() with one listener")
        {
            return shadows::bias.set((++i & 1) ? 0.25 : 0.5);
        };
        CHECK(told > 0);

        // A Look: 150 settings, each with a listener, in one batch.
        const ALCVarGroupDesc&        look = cvartest::perf::look::group.desc();
        std::vector<ALCVarConnection> listeners;
        for (const ALCVarDesc& desc : look.settings)
        {
            listeners.push_back(cvars.listen(look.state->first + desc.key, [&told](const ALCVarValue&, const ALCVarValue&) {
                ++told;
            }));
        }
        BENCHMARK("a batch of 150 set()s each with a listener, a Look applied")
        {
            const F32 value = (++i & 1) ? 2.f : 3.f;
            {
                ALCVars::Batch batch;
                for (const ALCVarDesc& desc : look.settings)
                {
                    cvars.set(look.state->first + desc.key, ALCVarValue(value));
                }
            }
            return told;
        };
        cvars.flushNow();
    }

    TEST_CASE("alcvar_bench: by name", "[alcvar_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        BenchStore bench;
        ALCVars&   cvars = bench.store();

        boost::unordered_flat_map<std::string_view, U32> names;
        for (U32 i = 0; i < cvars.settingCount(); ++i)
        {
            names.emplace(cvars.desc(i).name, i);
        }
        std::string_view name = "render.draw_distance";
        BENCHMARK("control: a lookup in a map of every name")
        {
            return names.find(name)->second;
        };
        BENCHMARK("find() of a name and a read as F32, as getF32(\"Name\") will")
        {
            return cvars.find(name).getAs(ALCVarType::F32)->get<F32>();
        };
        std::string_view old = "RenderFarClip";
        BENCHMARK("find() of an old name and a read as F32")
        {
            return cvars.find(old).getAs(ALCVarType::F32)->get<F32>();
        };
    }

    TEST_CASE("alcvar_bench: opening a global file of five hundred rows", "[alcvar_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("settings.db");
        {
            TestStore store;
            REQUIRE(store.store().openGlobal(path));
            U32 rows = 0;
            for (const char* name : { "perf.rows", "perf.look", "perf.grade" })
            {
                const ALCVarGroupDesc& group = store.store().groupDesc(store.store().findGroup(name));
                for (const ALCVarDesc& desc : group.settings)
                {
                    const U32 index = group.state->first + desc.key;
                    if (rows < 500 && ALCVarTypes::isNumber(desc.type) &&
                        store.store().set(index, *ALCVarConvert::toType(ALCVarValue(S32(2)), desc.type)))
                    {
                        ++rows;
                    }
                }
            }
            REQUIRE(rows == 500);
            store.store().close();
        }

        // Timed by hand: each open needs a store of its own.
        std::vector<F64> times;
        for (int i = 0; i < 25; ++i)
        {
            TestStore  store;
            LLTimer    timer;
            const bool opened = store.store().openGlobal(path);
            times.push_back(F64(timer.getElapsedTimeF64()) * 1000.0);
            REQUIRE(opened);
            REQUIRE(cvartest::perf::rows::level_0() == 2);
        }
        std::sort(times.begin(), times.end());
        WARN("the main thread's wait to open a global file of 500 rows: median " << times[times.size() / 2]
                                                                                  << " ms, fastest " << times.front()
                                                                                  << " ms, slowest " << times.back()
                                                                                  << " ms, of " << times.size());
    }
}

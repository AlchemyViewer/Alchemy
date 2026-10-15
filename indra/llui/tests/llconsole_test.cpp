/**
 * @file llconsole_test.cpp
 * @brief Unit tests for LLConsole taking in the lines added to it
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

#include "../llconsole.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    struct llconsole_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLConsole* console()
        {
            LLConsole::Params p(LLUICtrlFactory::getDefaultParams<LLConsole>());
            p.name = "console";
            p.rect = LLRect(0, 200, 600, 0);
            LLConsole* made = LLUICtrlFactory::create<LLConsole>(p);
            made->setMaxLines(40);
            return made;
        }
    };

    TEST_CASE_METHOD(llconsole_data, "llconsole: a line added while the console lays lines out does not wait for the layout", "[llconsole]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // Laying a line out draws its glyphs, and whatever is logged meanwhile is
        // added to the console by its recorder. A console that held its buffer's lock
        // through the layout made a line logged on the same thread take that lock
        // again, which throws, and one logged on another thread wait for the layout.
        LLConsole* c = console();
        std::string paragraph;
        for (int i = 0; i < 40000; ++i)
        {
            paragraph += "lorem ipsum ";
        }
        c->addLine(paragraph);

        std::atomic<bool> updating{false};
        F64 add_ms = -1.0;
        std::thread adder([&]()
            {
                while (!updating)
                {
                    std::this_thread::yield();
                }
                // Well into the layout: taking the lines out is all that comes first.
                std::this_thread::sleep_for(5ms);
                const auto start = std::chrono::steady_clock::now();
                c->addLine("added during the layout");
                add_ms = std::chrono::duration<F64, std::milli>(std::chrono::steady_clock::now() - start).count();
            });

        const auto start = std::chrono::steady_clock::now();
        updating = true;
        LLConsole::updateClass();
        const F64 update_ms = std::chrono::duration<F64, std::milli>(std::chrono::steady_clock::now() - start).count();
        adder.join();

        const size_t waiting = c->mLines.size();
        c->die();
        if (update_ms < 50.0)
        {
            SKIP("the layout took " << update_ms << " ms, too short to tell a wait for it");
        }
        INFO("the layout took " << update_ms << " ms and the line added during it " << add_ms << " ms");
        CHECK(add_ms < update_ms / 2.0);
        UNSCOPED_INFO("the line added during the layout was lost");
        CHECK(waiting == 1);
    }

    TEST_CASE_METHOD(llconsole_data, "llconsole: an update takes the buffer's lines and their times together", "[llconsole]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLConsole* c = console();
        c->addLine("one");
        c->addLine("two");
        REQUIRE(c->mAddTimes.size() == 2);

        LLConsole::updateClass();
        CHECK(c->mLines.empty());
        UNSCOPED_INFO("the times of lines already taken were kept, one for every line ever added");
        CHECK(c->mAddTimes.empty());
        c->die();
    }
}

/**
 * @file almasterburst_test.cpp
 * @brief Changes to master files gathered over a burst and let go together once it is over.
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

#include "../masters/almasterburst.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{
    struct almasterburst_data
    {
        static std::string joined(const std::vector<std::string>& list)
        {
            std::string out;
            for (const std::string& one : list)
            {
                out += (out.empty() ? "" : " ") + one;
            }
            return out;
        }

        // The time just before another, where nothing may yet be let go.
        static F64 before(F64 time) { return std::nextafter(time, -std::numeric_limits<F64>::infinity()); }
    };

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a single file goes once only after the quiet time with nothing changed", "[almasterburst]")
    {
        ALMasterBurst burst;
        {
            INFO("nothing waits at first");
            REQUIRE(burst.empty());
            REQUIRE_FALSE(burst.dueAt());
        }
        UNSCOPED_INFO("and nothing goes");
        REQUIRE(burst.release(100.0).empty());

        burst.changed("/s/door.lsl", 100.0);
        CHECK(burst.waiting("/s/door.lsl"));
        REQUIRE(burst.size() == 1);
        std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due a second on");
        CHECK(*due == 101.0);
        {
            INFO("not before");
            REQUIRE(burst.release(100.5).empty());
            REQUIRE(burst.release(almasterburst_data::before(101.0)).empty());
        }
        CHECK(almasterburst_data::joined(burst.release(101.0)) == std::string("/s/door.lsl"));
        REQUIRE(burst.empty());
        REQUIRE_FALSE(burst.dueAt());
        UNSCOPED_INFO("not given twice");
        REQUIRE(burst.release(200.0).empty());

        // A quiet time set shorter, and one less than nothing.
        burst.setQuiet(0.25);
        burst.changed("/s/door.lsl", 300.0);
        due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("the quiet time set");
        CHECK(*due == 300.25);
        burst.setQuiet(-1.0);
        due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("none at all is no wait");
        CHECK(*due == 300.0);
        UNSCOPED_INFO("and goes at once");
        CHECK(almasterburst_data::joined(burst.release(300.0)) == std::string("/s/door.lsl"));
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a checkout of thirty files over 0.8 s is let go together once", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 10.0);
        std::string   expected;
        F64           last = 0.0;
        for (int n = 0; n < 30; ++n)
        {
            last                   = 50.0 + 0.8 * n / 29.0;
            const std::string path = "/s/repo/script" + std::to_string(n) + ".lsl";
            burst.changed(path, last);
            expected += (expected.empty() ? "" : " ") + path;
            // Looked at as each is heard: the burst goes on.
            UNSCOPED_INFO("none while it goes on");
            REQUIRE(burst.release(last).empty());
        }
        REQUIRE(burst.size() == size_t(30));
        const std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due a second after the last");
        CHECK(*due == last + 1.0);
        UNSCOPED_INFO("not a moment before");
        REQUIRE(burst.release(almasterburst_data::before(last + 1.0)).empty());
        UNSCOPED_INFO("all of them, in the order heard");
        CHECK(almasterburst_data::joined(burst.release(last + 1.0)) == expected);
        REQUIRE(burst.empty());
        REQUIRE(burst.release(last + 60.0).empty());
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a file rewritten every half-second still goes at the burst's longest", "[almasterburst]")
    {
        ALMasterBurst    burst(1.0, 10.0);
        std::vector<F64> gone;
        for (int n = 0; n <= 50; ++n)
        {
            const F64 now = 0.5 * n;
            burst.changed("/s/build/out.lsl", now);
            if (n == 6)
            {
                const std::optional<F64> due = burst.dueAt();
                REQUIRE(due.has_value());
                UNSCOPED_INFO("due when quiet, while that is sooner");
                REQUIRE(*due == 4.0);
            }
            if (!burst.release(now).empty())
            {
                gone.push_back(now);
            }
        }
        UNSCOPED_INFO("let go twice in 25 s");
        REQUIRE(gone.size() == size_t(2));
        UNSCOPED_INFO("first at ten seconds");
        CHECK(gone[0] == 10.0);
        // The next change began a burst of its own.
        UNSCOPED_INFO("then ten seconds after the change that followed");
        CHECK(gone[1] == 20.5);
        const std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("and once more due when that one is over");
        CHECK(*due == 25.0 + 1.0);
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a file held past the quiet time stays and the others go first", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0);
        // Found emptied: held a second and a half.
        burst.changed("/s/emptied.lsl", 0.25, 1.5);
        burst.changed("/s/b.lsl", 0.5);
        std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due when quiet");
        CHECK(*due == 1.5);
        UNSCOPED_INFO("the others go");
        CHECK(almasterburst_data::joined(burst.release(1.5)) == std::string("/s/a.lsl /s/b.lsl"));
        {
            INFO("the held one waits");
            CHECK(burst.waiting("/s/emptied.lsl"));
            REQUIRE(burst.size() == 1);
        }
        due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due when its hold is over");
        CHECK(*due == 1.75);
        UNSCOPED_INFO("not before");
        REQUIRE(burst.release(almasterburst_data::before(1.75)).empty());
        UNSCOPED_INFO("then it goes");
        CHECK(almasterburst_data::joined(burst.release(1.75)) == std::string("/s/emptied.lsl"));

        // Held, but the burst it is in goes on: it waits for both.
        burst.changed("/s/emptied.lsl", 10.0, 1.5);
        burst.changed("/s/c.lsl", 11.0);
        due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("past its hold, the burst not over");
        CHECK(*due == 12.0);
        UNSCOPED_INFO("together then");
        CHECK(almasterburst_data::joined(burst.release(12.0)) == std::string("/s/emptied.lsl /s/c.lsl"));
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a refilled file loses its hold and keeps its place; the last hold counts", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0, 1.5);
        burst.changed("/s/b.lsl", 0.125);
        burst.changed("/s/a.lsl", 0.25);
        std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due when quiet, the hold gone");
        CHECK(*due == 1.25);
        UNSCOPED_INFO("both, the refilled one in its first place");
        CHECK(almasterburst_data::joined(burst.release(1.25)) ==
                      std::string("/s/a.lsl /s/b.lsl"));

        burst.changed("/s/a.lsl", 5.0);
        burst.changed("/s/a.lsl", 5.25, 1.5);
        due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("emptied later, held from then");
        CHECK(*due == 6.75);
        UNSCOPED_INFO("not when quiet");
        REQUIRE(burst.release(6.25).empty());
        UNSCOPED_INFO("when the later hold is over");
        CHECK(almasterburst_data::joined(burst.release(6.75)) == std::string("/s/a.lsl"));
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: a file forgotten is not let go and the burst is of what is left", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0);
        burst.changed("/s/b.lsl", 0.25);
        burst.forget("/s/a.lsl");
        burst.forget("/s/never.lsl");
        CHECK_FALSE(burst.waiting("/s/a.lsl"));
        REQUIRE(burst.size() == 1);
        UNSCOPED_INFO("the rest goes");
        CHECK(almasterburst_data::joined(burst.release(1.25)) == std::string("/s/b.lsl"));

        // The first heard forgotten: the burst is as long as the rest's.
        burst.changed("/s/a.lsl", 100.0);
        for (int n = 0; n < 20; ++n)
        {
            const F64 now = 105.0 + 0.5 * n;
            burst.changed("/s/busy.lsl", now);
            if (n == 2)
            {
                burst.forget("/s/a.lsl");
            }
            UNSCOPED_INFO("not at the forgotten one's longest, nor before the rest's");
            REQUIRE(burst.release(now).empty());
        }
        burst.changed("/s/busy.lsl", 115.0);
        const std::optional<F64> due = burst.dueAt();
        REQUIRE(due.has_value());
        UNSCOPED_INFO("due at the rest's longest");
        CHECK(*due == 115.0);
        UNSCOPED_INFO("which it goes at");
        CHECK(almasterburst_data::joined(burst.release(115.0)) == std::string("/s/busy.lsl"));

        burst.changed("/s/a.lsl", 200.0);
        burst.changed("/s/b.lsl", 200.0, 1.5);
        burst.clear();
        REQUIRE(burst.empty());
        REQUIRE_FALSE(burst.dueAt());
        REQUIRE(burst.release(300.0).empty());
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: Windows paths match in any case and separator and go as last spelled", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("C:\\Scripts\\Door.lsl", 0.0);
        burst.changed("c:/scripts/door.LSL", 0.5);
        burst.changed("/s/Door.lsl", 0.5);
        burst.changed("/s/door.lsl", 0.5);
        UNSCOPED_INFO("one Windows file, and two others");
        REQUIRE(burst.size() == size_t(3));
        UNSCOPED_INFO("found in any case");
        CHECK(burst.waiting("C:/SCRIPTS/DOOR.LSL"));
        UNSCOPED_INFO("but not another's");
        CHECK_FALSE(burst.waiting("/S/door.lsl"));
        UNSCOPED_INFO("each once, as last spelled");
        CHECK(almasterburst_data::joined(burst.release(1.5)) ==
                      std::string("c:/scripts/door.LSL /s/Door.lsl /s/door.lsl"));
        burst.changed("C:\\Scripts\\Door.lsl", 2.0);
        burst.forget("c:\\SCRIPTS\\door.lsl");
        UNSCOPED_INFO("forgotten in any case");
        REQUIRE(burst.empty());
    }

    TEST_CASE_METHOD(almasterburst_data, "almasterburst: dueAt and release agree: nothing goes before it and something goes at it", "[almasterburst]")
    {
        ALMasterBurst burst(1.0, 4.0);
        // Changes to twenty files at times a step of 1/8 s apart, some of
        // them emptied, as a small generator picks them.
        U32                   seed  = 12345;
        const auto            next  = [&seed]() {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 16) & 0x7fff;
        };
        std::set<std::string> heard;
        size_t                given = 0;
        F64                   now   = 0.0;
        for (int step = 0; step < 2000; ++step)
        {
            now += 0.125 * static_cast<F64>(next() % 12);
            // Everything due before this change is let go first, as a timer
            // set to dueAt would let it go.
            while (const std::optional<F64> due = burst.dueAt())
            {
                if (*due > now)
                {
                    break;
                }
                UNSCOPED_INFO("nothing before it is due");
                REQUIRE(burst.release(almasterburst_data::before(*due)).empty());
                const std::vector<std::string> gone = burst.release(*due);
                UNSCOPED_INFO("something when it is");
                REQUIRE_FALSE(gone.empty());
                const std::set<std::string> once(gone.begin(), gone.end());
                UNSCOPED_INFO("each once");
                REQUIRE(once.size() == gone.size());
                for (const std::string& path : gone)
                {
                    UNSCOPED_INFO("only what was heard");
                    REQUIRE(heard.count(path) == 1);
                    UNSCOPED_INFO("and is waited on no longer");
                    REQUIRE_FALSE(burst.waiting(path));
                }
                given += gone.size();
            }
            const std::string path = "/s/f" + std::to_string(next() % 20) + ".lsl";
            heard.insert(path);
            burst.changed(path, now, next() % 4 == 0 ? 1.5 : 0.0);
        }
        while (const std::optional<F64> due = burst.dueAt())
        {
            UNSCOPED_INFO("the last of them too");
            REQUIRE_FALSE(burst.release(*due).empty());
        }
        UNSCOPED_INFO("all let go in the end");
        REQUIRE(burst.empty());
        UNSCOPED_INFO("many times over");
        CHECK(given > 100);
    }
}

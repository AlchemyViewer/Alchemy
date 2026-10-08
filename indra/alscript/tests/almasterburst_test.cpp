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

#include "../test/lltut.h"

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace tut
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

    typedef test_group<almasterburst_data> almasterburst_group;
    typedef almasterburst_group::object    almasterburst_object;
    almasterburst_group                    almasterburst_instance("almasterburst");

    template<> template<>
    void almasterburst_object::test<1>()
    {
        set_test_name("a single file goes once nothing has changed for the quiet time, and once only");
        ALMasterBurst burst;
        ensure("nothing waits at first", burst.empty() && !burst.dueAt());
        ensure("and nothing goes", burst.release(100.0).empty());

        burst.changed("/s/door.lsl", 100.0);
        ensure("it waits", burst.waiting("/s/door.lsl") && burst.size() == 1);
        ensure_equals("due a second on", *burst.dueAt(), 101.0);
        ensure("not before", burst.release(100.5).empty() && burst.release(almasterburst_data::before(101.0)).empty());
        ensure_equals("then", almasterburst_data::joined(burst.release(101.0)), std::string("/s/door.lsl"));
        ensure("and is gone", burst.empty() && !burst.dueAt());
        ensure("not given twice", burst.release(200.0).empty());

        // A quiet time set shorter, and one less than nothing.
        burst.setQuiet(0.25);
        burst.changed("/s/door.lsl", 300.0);
        ensure_equals("the quiet time set", *burst.dueAt(), 300.25);
        burst.setQuiet(-1.0);
        ensure_equals("none at all is no wait", *burst.dueAt(), 300.0);
        ensure_equals("and goes at once", almasterburst_data::joined(burst.release(300.0)), std::string("/s/door.lsl"));
    }

    template<> template<>
    void almasterburst_object::test<2>()
    {
        set_test_name("a checkout of thirty files over 0.8 s is let go together, once");
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
            ensure("none while it goes on", burst.release(last).empty());
        }
        ensure_equals("all thirty wait", burst.size(), size_t(30));
        ensure_equals("due a second after the last", *burst.dueAt(), last + 1.0);
        ensure("not a moment before", burst.release(almasterburst_data::before(last + 1.0)).empty());
        ensure_equals("all of them, in the order heard", almasterburst_data::joined(burst.release(last + 1.0)), expected);
        ensure("nothing left", burst.empty() && burst.release(last + 60.0).empty());
    }

    template<> template<>
    void almasterburst_object::test<3>()
    {
        set_test_name("a file rewritten every half-second still goes, once the burst has gone on its longest");
        ALMasterBurst    burst(1.0, 10.0);
        std::vector<F64> gone;
        for (int n = 0; n <= 50; ++n)
        {
            const F64 now = 0.5 * n;
            burst.changed("/s/build/out.lsl", now);
            if (n == 6)
            {
                ensure_equals("due when quiet, while that is sooner", *burst.dueAt(), 4.0);
            }
            if (!burst.release(now).empty())
            {
                gone.push_back(now);
            }
        }
        ensure_equals("let go twice in 25 s", gone.size(), size_t(2));
        ensure_equals("first at ten seconds", gone[0], 10.0);
        // The next change began a burst of its own.
        ensure_equals("then ten seconds after the change that followed", gone[1], 20.5);
        ensure_equals("and once more due when that one is over", *burst.dueAt(), 25.0 + 1.0);
    }

    template<> template<>
    void almasterburst_object::test<4>()
    {
        set_test_name("a file held past the quiet time stays, and the others go first");
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0);
        // Found emptied: held a second and a half.
        burst.changed("/s/emptied.lsl", 0.25, 1.5);
        burst.changed("/s/b.lsl", 0.5);
        ensure_equals("due when quiet", *burst.dueAt(), 1.5);
        ensure_equals("the others go", almasterburst_data::joined(burst.release(1.5)), std::string("/s/a.lsl /s/b.lsl"));
        ensure("the held one waits", burst.waiting("/s/emptied.lsl") && burst.size() == 1);
        ensure_equals("due when its hold is over", *burst.dueAt(), 1.75);
        ensure("not before", burst.release(almasterburst_data::before(1.75)).empty());
        ensure_equals("then it goes", almasterburst_data::joined(burst.release(1.75)), std::string("/s/emptied.lsl"));

        // Held, but the burst it is in goes on: it waits for both.
        burst.changed("/s/emptied.lsl", 10.0, 1.5);
        burst.changed("/s/c.lsl", 11.0);
        ensure_equals("past its hold, the burst not over", *burst.dueAt(), 12.0);
        ensure_equals("together then", almasterburst_data::joined(burst.release(12.0)), std::string("/s/emptied.lsl /s/c.lsl"));
    }

    template<> template<>
    void almasterburst_object::test<5>()
    {
        set_test_name("an emptied file filled again is held no longer, and keeps its place; the hold given last counts");
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0, 1.5);
        burst.changed("/s/b.lsl", 0.125);
        burst.changed("/s/a.lsl", 0.25);
        ensure_equals("due when quiet, the hold gone", *burst.dueAt(), 1.25);
        ensure_equals("both, the refilled one in its first place", almasterburst_data::joined(burst.release(1.25)),
                      std::string("/s/a.lsl /s/b.lsl"));

        burst.changed("/s/a.lsl", 5.0);
        burst.changed("/s/a.lsl", 5.25, 1.5);
        ensure_equals("emptied later, held from then", *burst.dueAt(), 6.75);
        ensure("not when quiet", burst.release(6.25).empty());
        ensure_equals("when the later hold is over", almasterburst_data::joined(burst.release(6.75)), std::string("/s/a.lsl"));
    }

    template<> template<>
    void almasterburst_object::test<6>()
    {
        set_test_name("a file forgotten is not let go, and the burst is of what is left");
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("/s/a.lsl", 0.0);
        burst.changed("/s/b.lsl", 0.25);
        burst.forget("/s/a.lsl");
        burst.forget("/s/never.lsl");
        ensure("forgotten", !burst.waiting("/s/a.lsl") && burst.size() == 1);
        ensure_equals("the rest goes", almasterburst_data::joined(burst.release(1.25)), std::string("/s/b.lsl"));

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
            ensure("not at the forgotten one's longest, nor before the rest's", burst.release(now).empty());
        }
        burst.changed("/s/busy.lsl", 115.0);
        ensure_equals("due at the rest's longest", *burst.dueAt(), 115.0);
        ensure_equals("which it goes at", almasterburst_data::joined(burst.release(115.0)), std::string("/s/busy.lsl"));

        burst.changed("/s/a.lsl", 200.0);
        burst.changed("/s/b.lsl", 200.0, 1.5);
        burst.clear();
        ensure("cleared", burst.empty() && !burst.dueAt() && burst.release(300.0).empty());
    }

    template<> template<>
    void almasterburst_object::test<7>()
    {
        set_test_name("Windows's paths are one file in any case and with either separator; the spelling last given goes");
        ALMasterBurst burst(1.0, 10.0);
        burst.changed("C:\\Scripts\\Door.lsl", 0.0);
        burst.changed("c:/scripts/door.LSL", 0.5);
        burst.changed("/s/Door.lsl", 0.5);
        burst.changed("/s/door.lsl", 0.5);
        ensure_equals("one Windows file, and two others", burst.size(), size_t(3));
        ensure("found in any case", burst.waiting("C:/SCRIPTS/DOOR.LSL"));
        ensure("but not another's", !burst.waiting("/S/door.lsl"));
        ensure_equals("each once, as last spelled", almasterburst_data::joined(burst.release(1.5)),
                      std::string("c:/scripts/door.LSL /s/Door.lsl /s/door.lsl"));
        burst.changed("C:\\Scripts\\Door.lsl", 2.0);
        burst.forget("c:\\SCRIPTS\\door.lsl");
        ensure("forgotten in any case", burst.empty());
    }

    template<> template<>
    void almasterburst_object::test<8>()
    {
        set_test_name("dueAt and release agree: nothing goes before it, and something goes at it");
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
                ensure("nothing before it is due", burst.release(almasterburst_data::before(*due)).empty());
                const std::vector<std::string> gone = burst.release(*due);
                ensure("something when it is", !gone.empty());
                const std::set<std::string> once(gone.begin(), gone.end());
                ensure_equals("each once", once.size(), gone.size());
                for (const std::string& path : gone)
                {
                    ensure("only what was heard", heard.count(path) == 1);
                    ensure("and is waited on no longer", !burst.waiting(path));
                }
                given += gone.size();
            }
            const std::string path = "/s/f" + std::to_string(next() % 20) + ".lsl";
            heard.insert(path);
            burst.changed(path, now, next() % 4 == 0 ? 1.5 : 0.0);
        }
        while (const std::optional<F64> due = burst.dueAt())
        {
            ensure("the last of them too", !burst.release(*due).empty());
        }
        ensure("all let go in the end", burst.empty());
        ensure("many times over", given > 100);
    }
}

/**
 * @file altestsupport_test.cpp
 * @brief Tests of the Catch2 harness itself: its matchers, what an LL_ERRS
 *        does, the teardown, and the cases that CTest entries in
 *        test/CMakeLists.txt run to check the guard, the log replay and the exit
 *        codes from outside.
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

#include "altestfatal.h"
#include "altesthelper.h"
#include "altestmatchers.h"
#include "altestname.h"
#include "alteststateguard.h"
#include "altestteardown.h"

#include "llapr.h"
#include "llerror.h"
#include "llsd.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#if LL_WINDOWS
#include "llwin32headers.h"
#include <direct.h>
#endif

namespace
{
    LLSD nested(F64 leaf)
    {
        LLSD sd;
        sd["a"].resize(3);
        sd["a"][2] = leaf;
        sd["b"] = "text";
        return sd;
    }

    void setLeak(const char* name)
    {
#if LL_WINDOWS
        _putenv_s(name, "1");
#else
        setenv(name, "1", 1);
#endif
    }

    // A library's state, which its own listener adds a check for as the run
    // starts, as a library's tests do.
    int sLibraryState = 0;

    class LibraryChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            ll_test::StateGuard::add("the library's state", [] { return std::to_string(sLibraryState); });
        }
    };
}

CATCH_REGISTER_LISTENER(LibraryChecks)

// The helper the helper_* entries run, which says whether it found the
// process set up as a test would, and exits with a code no test run gives.
AL_TEST_HELPER(altestsupport_exit)
{
    std::cout << "the helper ran, " << (gAPRPoolp ? "with" : "without") << " APR up" << std::endl;
    return 37;
}

TEST_CASE("WithinBits holds to the bits asked for, and no further", "[altestsupport]")
{
    // 2^-20 apart: equal to 16 bits of the fraction, not to 24. (Not "near":
    // windows.h defines that, and far, as nothing.)
    const F32 one = 1.f;
    const F32 just_off = 1.f + 1.f / (1 << 20);
    CHECK_THAT(just_off, ll_test::WithinBits(one, 16));
    CHECK_FALSE(ll_test::WithinBits(one, 24).match(just_off));
    CHECK_FALSE(ll_test::WithinBits(one, 16).match(1.01f));
    CHECK_THAT(F64(just_off), ll_test::WithinBits(F64(one), 16));
}

TEST_CASE("WithinDistance holds strictly inside the distance, as ensure_distance did", "[altestsupport]")
{
    CHECK_THAT(11, ll_test::WithinDistance(12, 2));
    CHECK_FALSE(ll_test::WithinDistance(12, 2).match(10));
    CHECK_FALSE(ll_test::WithinDistance(12, 2).match(14));
    CHECK_THAT(0.5, ll_test::WithinDistance(1.0, 0.75));
    CHECK_FALSE(ll_test::WithinDistance(1.0, 0.5).match(0.5));
}

TEST_CASE("LLSDEquals compares all the way down and says where two differ", "[altestsupport]")
{
    CHECK_THAT(nested(1.5), ll_test::LLSDEquals(nested(1.5)));

    const ll_test::LLSDEqualsMatcher deep = ll_test::LLSDEquals(nested(1.5));
    CHECK_FALSE(deep.match(nested(2.5)));
    CHECK_THAT(deep.describe(), Catch::Matchers::ContainsSubstring("[\"a\"][2]: value"));

    const ll_test::LLSDEqualsMatcher top = ll_test::LLSDEquals(LLSD("1"));
    CHECK_FALSE(top.match(LLSD(1)));
    CHECK_THAT(top.describe(), Catch::Matchers::ContainsSubstring("the top: type"));

    // LLDate's own == holds a NaN date equal to any.
    const LLSD nan_date = LLDate(std::numeric_limits<F64>::quiet_NaN());
    CHECK_FALSE(ll_test::LLSDEquals(LLDate(0.0)).match(nan_date));
    // Nor a NaN number, which MSVC's /fp:fast compare would hold equal.
    const LLSD nan_real = std::numeric_limits<F64>::quiet_NaN();
    CHECK_FALSE(ll_test::LLSDEquals(0.0).match(nan_real));
    CHECK_THAT(LLSD(LLDate(1.0)), ll_test::LLSDEquals(LLDate(1.0)));
}

TEST_CASE("MemoryMatches compares byte for byte and says where two part", "[altestsupport]")
{
    const char same[] = "abcd";
    const char other[] = "abxd";
    CHECK_THAT(ll_test::Memory(same, 4), ll_test::MemoryMatches(same, 4));

    const ll_test::MemoryMatchesMatcher differs = ll_test::MemoryMatches(other, 4);
    CHECK_FALSE(differs.match(ll_test::Memory(same, 4)));
    CHECK_THAT(differs.describe(), Catch::Matchers::ContainsSubstring("byte 2"));

    CHECK_FALSE(ll_test::MemoryMatches(same, 3).match(ll_test::Memory(same, 4)));
}

TEST_CASE("currentTestName is the running test case's", "[altestsupport]")
{
    CHECK(ll_test::currentTestName() == "currentTestName is the running test case's");
}

TEST_CASE("An LL_ERRS throws FatalError, which a test can catch", "[altestsupport]")
{
    // Named, not written inside the assertion: Catch2 hands its argument on to
    // a macro of its own after expanding it, and LL_ERRS expands to commas.
    const auto deliberate = [] { LL_ERRS("altestsupport") << "deliberate" << LL_ENDL; };
    CHECK_THROWS_AS(deliberate(), ll_test::FatalError);
    CHECK_THROWS_WITH(deliberate(), Catch::Matchers::ContainsSubstring("deliberate"));
}

TEST_CASE("Teardown runs each step once, newest first", "[altestsupport]")
{
    std::vector<int> order;
    ll_test::Teardown::add([&order] { order.push_back(1); });
    ll_test::Teardown::add([&order] { order.push_back(2); });
    CHECK(ll_test::Teardown::runAll() == 0);
    const std::vector<int> newest_first{ 2, 1 };
    CHECK(order == newest_first);
    CHECK(ll_test::Teardown::runAll() == 0);
    CHECK(order.size() == 2);
}

// Hidden: the CTest entries in test/CMakeLists.txt run these one at a time
// and check what the harness made of them.

TEST_CASE("a test case that leaves the environment changed", "[.guard-leak]")
{
    setLeak("AL_TEST_SUPPORT_LEAK");
    SUCCEED();
}

TEST_CASE("a test case that leaves a library's state changed", "[.library-leak]")
{
    sLibraryState = 1;
    SUCCEED();
}

TEST_CASE("a failing test case that logged", "[.replay-fail]")
{
    LL_INFOS("altestsupport") << "the line a failing case logged" << LL_ENDL;
    FAIL("deliberately");
}

TEST_CASE("a passing test case that logged", "[.replay-pass]")
{
    LL_INFOS("altestsupport") << "the line a passing case logged" << LL_ENDL;
    SUCCEED();
}

TEST_CASE("a test case that skips", "[.skip]")
{
    SKIP("deliberately");
}

TEST_CASE("a test case that leaves the environment changed, then skips", "[.skip-leak]")
{
    setLeak("AL_TEST_SUPPORT_SKIP_LEAK");
    SKIP("deliberately");
}

TEST_CASE("a test case whose fixture sets a variable and says so", "[.guard-accept]")
{
    setLeak("AL_TEST_SUPPORT_ACCEPTED");
    ll_test::StateGuard::accept("the environment");
    SUCCEED();
}

#if LL_WINDOWS
// The CRT's change of directory leaves the hidden =C:=C:\dir variable for the
// drive it went to, as a font or GL driver lookup on the system drive does in
// a tree on another one.
TEST_CASE("a test case that changes directory on another drive and back", "[.drive-visit]")
{
    wchar_t system[MAX_PATH];
    REQUIRE(GetSystemDirectoryW(system, MAX_PATH) != 0);
    wchar_t* current = _wgetcwd(nullptr, 0);
    REQUIRE(current);
    const std::wstring back(current);
    std::free(current);
    CHECK(_wchdir(system) == 0);
    CHECK(_wchdir(back.c_str()) == 0);
}
#endif

TEST_CASE("a test case whose fixture's end fails", "[.teardown-fails]")
{
    ll_test::Teardown::add([] { LL_ERRS("altestsupport") << "a teardown step that fails" << LL_ENDL; });
    SUCCEED();
}

TEST_CASE("a test case that logged, then crashes", "[.crash-replay]")
{
    LL_INFOS("altestsupport") << "the line a crashing case logged" << LL_ENDL;
    // Through a volatile pointer, so no compiler can know it is null.
    volatile int* volatile nowhere = nullptr;
    *nowhere = 1;
}

TEST_CASE("a test case whose thread hits an LL_ERRS", "[.thread-errs]")
{
    std::thread([] { LL_ERRS("altestsupport") << "an error off the main thread" << LL_ENDL; }).join();
}

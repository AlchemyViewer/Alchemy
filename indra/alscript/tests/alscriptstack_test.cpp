/**
 * @file alscriptstack_test.cpp
 * @brief Work over a script's text run on a stack deep enough for the script.
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

#include "../alscriptstack.h"

#include "../alpreprocessor.h"

#include "../test/lltut.h"

#include <stdexcept>
#include <thread>

namespace
{
    int descend(int levels);
    // Called through a pointer the compiler cannot see through, so that
    // every level is a frame of its own rather than a turn of a loop.
    int (*volatile sDescend)(int) = descend;

    // A frame a kilobyte deep, so many times over: what a deeply nested
    // script asks of the engines, in round numbers. The frame is read
    // after the call below it returns, so it lives while that one runs.
    int descend(int levels)
    {
        volatile char frame[1024];
        frame[levels % 1024] = 1;
        if (levels == 0)
        {
            return 0;
        }
        const int below = sDescend(levels - 1);
        return below + frame[levels % 1024];
    }
}

namespace tut
{
    struct alscriptstack_data
    {
    };

    typedef test_group<alscriptstack_data> alscriptstack_group;
    typedef alscriptstack_group::object    alscriptstack_object;
    alscriptstack_group                    alscriptstack_instance("alscriptstack");

    template<> template<>
    void alscriptstack_object::test<1>()
    {
        set_test_name("the work runs, on the thread that asked, and what it throws comes back");
        const std::thread::id asking = std::this_thread::get_id();
        std::thread::id       ran;
        alScriptOnLargeStack([&]() { ran = std::this_thread::get_id(); });
        ensure("on the same thread", ran == asking);

        bool caught = false;
        try
        {
            alScriptOnLargeStack([]() { throw std::runtime_error("from below"); });
        }
        catch (const std::runtime_error& e)
        {
            caught = std::string(e.what()) == "from below";
        }
        ensure("thrown again once back", caught);
    }

    template<> template<>
    void alscriptstack_object::test<2>()
    {
        set_test_name("a pool's thread, whose own stack is half a megabyte on a Mac, goes eight megabytes deep");
        int         got = -1;
        std::thread worker([&]() { alScriptOnLargeStack([&]() { got = descend(8000); }); });
        worker.join();
        ensure_equals("all the way down and back", got, 8000);

        // And the preprocessor as deep as its own bounds let a script go,
        // on such a thread.
        std::string nested = "#define F(x) x\n";
        for (int i = 0; i < 190; ++i)
        {
            nested += "F(";
        }
        nested += "1";
        for (int i = 0; i < 190; ++i)
        {
            nested += ")";
        }
        nested += ";\n";
        ALPreprocessor::Result result;
        std::thread            preprocessing([&]() {
            alScriptOnLargeStack([&]() {
                ALPreprocessor::Options options;
                result = ALPreprocessor::run(nested, options);
            });
        });
        preprocessing.join();
        ensure("expanded, within the bound", !result.overran && result.text.find("1;") != std::string::npos);
    }

    template<> template<>
    void alscriptstack_object::test<3>()
    {
        set_test_name("the thread's stack kept from one job to the next, and a job begun inside another run on it where it stands");
        int         first = -1, second = -1, nested = -1;
        bool        threw = false;
        std::thread worker([&]() {
            alScriptOnLargeStack([&]() { first = descend(8000); });
            try
            {
                alScriptOnLargeStack([]() { throw std::runtime_error("between"); });
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            alScriptOnLargeStack([&]() {
                second = descend(4000);
                // Begun inside: on the stack already, part way down it.
                alScriptOnLargeStack([&]() { nested = descend(3000); });
            });
        });
        worker.join();
        ensure_equals("the first all the way", first, 8000);
        ensure("a throw between", threw);
        ensure_equals("the second, on the same stack", second, 4000);
        ensure_equals("and one inside it", nested, 3000);
    }
}

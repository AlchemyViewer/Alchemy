/**
 * @file almasterqueue_test.cpp
 * @brief The sends of master files to their scripts: one at a time each, a few at a time in all.
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

#include "../masters/almasterqueue.h"

#include "../test/lltut.h"

#include <string>
#include <utility>
#include <vector>

namespace tut
{
    struct almasterqueue_data
    {
        using Send    = ALMasterPlan::Send;
        using Started = std::vector<std::pair<std::string, ALMasterPlan::Send>>;

        // What started, as "key" for a save of the master's and "key*" for
        // the studio's own.
        static std::string started(const Started& list)
        {
            std::string out;
            for (const auto& [key, kind] : list)
            {
                out += (out.empty() ? "" : " ") + key + (kind == Send::Derived ? "*" : "");
            }
            return out;
        }
    };

    typedef test_group<almasterqueue_data> almasterqueue_group;
    typedef almasterqueue_group::object    almasterqueue_object;
    almasterqueue_group                    almasterqueue_instance("almasterqueue");

    template<> template<>
    void almasterqueue_object::test<1>()
    {
        set_test_name("four go at once, the rest wait their turn, first come, first served");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        ensure_equals("four by default", queue.limit(), size_t(4));
        for (const char* key : { "a", "b", "c", "d" })
        {
            ensure(std::string("starts: ") + key, queue.ask(key, Send::Derived));
            ensure(std::string("is underway: ") + key, queue.underway(key) && !queue.waiting(key));
        }
        ensure_equals("four busy", queue.busy(), size_t(4));
        ensure("a fifth waits", !queue.ask("e", Send::Derived));
        ensure("a sixth too", !queue.ask("f", Send::Direct));
        ensure("waiting, not underway", queue.waiting("e") && !queue.underway("e"));
        ensure_equals("still four busy", queue.busy(), size_t(4));

        ensure_equals("one ends, the first waiting starts", almasterqueue_data::started(queue.finished("c")), std::string("e*"));
        ensure("the one ended is not underway", !queue.underway("c") && !queue.waiting("c"));
        ensure("the one started is", queue.underway("e") && !queue.waiting("e"));
        ensure_equals("then the next", almasterqueue_data::started(queue.finished("a")), std::string("f"));
        ensure_equals("then nothing waits", almasterqueue_data::started(queue.finished("b")), std::string());
        ensure_equals("three busy", queue.busy(), size_t(3));
        ensure("one asked now starts at once", queue.ask("g", Send::Derived));
        ensure_equals("one ended that was never asked starts nothing", almasterqueue_data::started(queue.finished("zz")), std::string());
        ensure_equals("and frees nothing", queue.busy(), size_t(4));
    }

    template<> template<>
    void almasterqueue_object::test<2>()
    {
        set_test_name("asked while on its way, a send goes again after it, once");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        ensure("starts", queue.ask("a", Send::Direct));
        ensure("asked again, waits", !queue.ask("a", Send::Direct));
        ensure("and again", !queue.ask("a", Send::Direct));
        ensure("underway, and to go again", queue.underway("a") && queue.waiting("a"));
        ensure_equals("one busy, not three", queue.busy(), size_t(1));
        ensure_equals("ended: goes again at once", almasterqueue_data::started(queue.finished("a")), std::string("a"));
        ensure("underway again, not to go a third time", queue.underway("a") && !queue.waiting("a"));
        ensure_equals("ended: nothing more", almasterqueue_data::started(queue.finished("a")), std::string());
        ensure("and gone", !queue.underway("a") && queue.busy() == 0);
    }

    template<> template<>
    void almasterqueue_object::test<3>()
    {
        set_test_name("asked while it waits, a send stays in its place");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue(1);
        ensure("one place", queue.ask("a", Send::Derived));
        ensure("b waits", !queue.ask("b", Send::Derived));
        ensure("c waits", !queue.ask("c", Send::Derived));
        ensure("b again, in its place", !queue.ask("b", Send::Derived));
        ensure_equals("b first", almasterqueue_data::started(queue.finished("a")), std::string("b*"));
        ensure_equals("then c", almasterqueue_data::started(queue.finished("b")), std::string("c*"));
        ensure_equals("and b went once", almasterqueue_data::started(queue.finished("c")), std::string());
    }

    template<> template<>
    void almasterqueue_object::test<4>()
    {
        set_test_name("a save of the master outranks the studio's own send, whichever was asked first");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue(1);
        queue.ask("a", Send::Derived);
        // On its way: to go again as a save, asked either way round.
        queue.ask("a", Send::Derived);
        queue.ask("a", Send::Direct);
        ensure_equals("a save after the studio's", almasterqueue_data::started(queue.finished("a")), std::string("a"));
        queue.ask("a", Send::Direct);
        queue.ask("a", Send::Derived);
        ensure_equals("the studio's after a save", almasterqueue_data::started(queue.finished("a")), std::string("a"));
        queue.ask("a", Send::Derived);
        ensure_equals("the studio's alone stays its own", almasterqueue_data::started(queue.finished("a")), std::string("a*"));

        // Waiting: the same.
        queue.ask("b", Send::Derived);
        queue.ask("b", Send::Direct);
        queue.ask("c", Send::Direct);
        queue.ask("c", Send::Derived);
        queue.ask("d", Send::Derived);
        queue.ask("d", Send::Derived);
        ensure_equals("b as a save", almasterqueue_data::started(queue.finished("a")), std::string("b"));
        ensure_equals("c as a save", almasterqueue_data::started(queue.finished("b")), std::string("c"));
        ensure_equals("d as the studio's", almasterqueue_data::started(queue.finished("c")), std::string("d*"));
    }

    template<> template<>
    void almasterqueue_object::test<5>()
    {
        set_test_name("a send asked again while on its way goes behind all that wait, its newest text last");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        for (const char* key : { "a", "b", "c", "d" })
        {
            queue.ask(key, Send::Derived);
        }
        queue.ask("e", Send::Derived);
        queue.ask("f", Send::Derived);
        queue.ask("a", Send::Direct);
        ensure_equals("a ends, the first waiting goes, not a", almasterqueue_data::started(queue.finished("a")), std::string("e*"));
        ensure("a waits", queue.waiting("a") && !queue.underway("a"));
        ensure_equals("then f", almasterqueue_data::started(queue.finished("b")), std::string("f*"));
        ensure_equals("then a, as it was asked again", almasterqueue_data::started(queue.finished("c")), std::string("a"));
        ensure_equals("four busy throughout", queue.busy(), size_t(4));
        ensure_equals("then nothing", almasterqueue_data::started(queue.finished("d")), std::string());
    }

    template<> template<>
    void almasterqueue_object::test<6>()
    {
        set_test_name("a script dropped is not sent after all, waiting or to go again; one on its way goes on");
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue(2);
        queue.ask("a", Send::Direct);
        queue.ask("b", Send::Direct);
        queue.ask("c", Send::Derived);
        queue.ask("d", Send::Derived);
        queue.ask("a", Send::Direct);
        queue.drop("c");
        queue.drop("a");
        queue.drop("never");
        ensure("c waits no longer", !queue.waiting("c") && !queue.underway("c"));
        ensure("a goes on, but not again", queue.underway("a") && !queue.waiting("a"));
        ensure_equals("a ends: d, not c, and not a", almasterqueue_data::started(queue.finished("a")), std::string("d*"));
        ensure("c asked again goes in at the back", !queue.ask("c", Send::Direct));
        ensure_equals("and goes in its turn", almasterqueue_data::started(queue.finished("b")), std::string("c"));
    }

    template<> template<>
    void almasterqueue_object::test<7>()
    {
        set_test_name("never more on their way than the limit, every one sent once, in order; at least one");
        using Send = almasterqueue_data::Send;
        ALMasterQueue            queue;
        std::string              order;
        std::string              expected;
        std::vector<std::string> underway;
        for (int n = 0; n < 30; ++n)
        {
            const std::string key = "s" + std::to_string(n);
            expected += (expected.empty() ? "" : " ") + key;
            if (queue.ask(key, Send::Derived))
            {
                underway.push_back(key);
                order += (order.empty() ? "" : " ") + key;
            }
            ensure("never more than four", queue.busy() <= 4);
        }
        ensure_equals("four started", underway.size(), size_t(4));
        while (!underway.empty())
        {
            const std::string done = underway.front();
            underway.erase(underway.begin());
            for (const auto& [key, kind] : queue.finished(done))
            {
                underway.push_back(key);
                order += " " + key;
            }
            ensure("never more than four", queue.busy() <= 4 && queue.busy() == underway.size());
        }
        ensure_equals("all, once, in the order asked", order, expected);

        ALMasterQueue none(0);
        ensure_equals("a limit of none is one", none.limit(), size_t(1));
        ensure("which starts", none.ask("a", Send::Derived));
        ensure("one only", !none.ask("b", Send::Derived));

        none.clear();
        ensure("cleared", !none.underway("a") && !none.waiting("b") && none.busy() == 0);
        ensure("and starts afresh", none.ask("b", Send::Derived));
        ensure_equals("with nothing behind it", almasterqueue_data::started(none.finished("b")), std::string());
    }
}

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

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
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

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: four go at once and the rest wait their turn in the order asked", "[almasterqueue]")
    {
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        UNSCOPED_INFO("four by default");
        CHECK(queue.limit() == size_t(4));
        for (const char* key : { "a", "b", "c", "d" })
        {
            CAPTURE(key);
            REQUIRE(queue.ask(key, Send::Derived));
            REQUIRE(queue.underway(key));
            REQUIRE_FALSE(queue.waiting(key));
        }
        CHECK(queue.busy() == size_t(4));
        UNSCOPED_INFO("a fifth waits");
        CHECK_FALSE(queue.ask("e", Send::Derived));
        UNSCOPED_INFO("a sixth too");
        CHECK_FALSE(queue.ask("f", Send::Direct));
        CHECK(queue.waiting("e"));
        CHECK_FALSE(queue.underway("e"));
        CHECK(queue.busy() == size_t(4));

        UNSCOPED_INFO("one ends, the first waiting starts");
        CHECK(almasterqueue_data::started(queue.finished("c")) == std::string("e*"));
        CHECK_FALSE(queue.underway("c"));
        CHECK_FALSE(queue.waiting("c"));
        CHECK(queue.underway("e"));
        CHECK_FALSE(queue.waiting("e"));
        UNSCOPED_INFO("then the next");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("f"));
        UNSCOPED_INFO("then nothing waits");
        CHECK(almasterqueue_data::started(queue.finished("b")) == std::string());
        CHECK(queue.busy() == size_t(3));
        UNSCOPED_INFO("one asked now starts at once");
        CHECK(queue.ask("g", Send::Derived));
        UNSCOPED_INFO("one ended that was never asked starts nothing");
        CHECK(almasterqueue_data::started(queue.finished("zz")) == std::string());
        UNSCOPED_INFO("and frees nothing");
        CHECK(queue.busy() == size_t(4));
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: a send asked while on its way goes again once after it", "[almasterqueue]")
    {
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        REQUIRE(queue.ask("a", Send::Direct));
        UNSCOPED_INFO("asked again, waits");
        CHECK_FALSE(queue.ask("a", Send::Direct));
        UNSCOPED_INFO("and again");
        CHECK_FALSE(queue.ask("a", Send::Direct));
        {
            INFO("underway, and to go again");
            CHECK(queue.underway("a"));
            CHECK(queue.waiting("a"));
        }
        UNSCOPED_INFO("one busy, not three");
        CHECK(queue.busy() == size_t(1));
        UNSCOPED_INFO("ended: goes again at once");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("a"));
        {
            INFO("underway again, not to go a third time");
            CHECK(queue.underway("a"));
            CHECK_FALSE(queue.waiting("a"));
        }
        UNSCOPED_INFO("ended: nothing more");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string());
        CHECK_FALSE(queue.underway("a"));
        CHECK(queue.busy() == 0);
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: a send asked while it waits stays in its place", "[almasterqueue]")
    {
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue(1);
        UNSCOPED_INFO("one place");
        CHECK(queue.ask("a", Send::Derived));
        UNSCOPED_INFO("b waits");
        CHECK_FALSE(queue.ask("b", Send::Derived));
        UNSCOPED_INFO("c waits");
        CHECK_FALSE(queue.ask("c", Send::Derived));
        UNSCOPED_INFO("b again, in its place");
        CHECK_FALSE(queue.ask("b", Send::Derived));
        UNSCOPED_INFO("b first");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("b*"));
        UNSCOPED_INFO("then c");
        CHECK(almasterqueue_data::started(queue.finished("b")) == std::string("c*"));
        UNSCOPED_INFO("and b went once");
        CHECK(almasterqueue_data::started(queue.finished("c")) == std::string());
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: a save of the master outranks the studio's own send whichever came first", "[almasterqueue]")
    {
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue(1);
        queue.ask("a", Send::Derived);
        // On its way: to go again as a save, asked either way round.
        queue.ask("a", Send::Derived);
        queue.ask("a", Send::Direct);
        UNSCOPED_INFO("a save after the studio's");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("a"));
        queue.ask("a", Send::Direct);
        queue.ask("a", Send::Derived);
        UNSCOPED_INFO("the studio's after a save");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("a"));
        queue.ask("a", Send::Derived);
        UNSCOPED_INFO("the studio's alone stays its own");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("a*"));

        // Waiting: the same.
        queue.ask("b", Send::Derived);
        queue.ask("b", Send::Direct);
        queue.ask("c", Send::Direct);
        queue.ask("c", Send::Derived);
        queue.ask("d", Send::Derived);
        queue.ask("d", Send::Derived);
        UNSCOPED_INFO("b as a save");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("b"));
        UNSCOPED_INFO("c as a save");
        CHECK(almasterqueue_data::started(queue.finished("b")) == std::string("c"));
        UNSCOPED_INFO("d as the studio's");
        CHECK(almasterqueue_data::started(queue.finished("c")) == std::string("d*"));
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: a send asked again on its way goes behind all that wait with its newest text", "[almasterqueue]")
    {
        using Send = almasterqueue_data::Send;
        ALMasterQueue queue;
        for (const char* key : { "a", "b", "c", "d" })
        {
            queue.ask(key, Send::Derived);
        }
        queue.ask("e", Send::Derived);
        queue.ask("f", Send::Derived);
        queue.ask("a", Send::Direct);
        UNSCOPED_INFO("a ends, the first waiting goes, not a");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("e*"));
        CHECK(queue.waiting("a"));
        CHECK_FALSE(queue.underway("a"));
        UNSCOPED_INFO("then f");
        CHECK(almasterqueue_data::started(queue.finished("b")) == std::string("f*"));
        UNSCOPED_INFO("then a, as it was asked again");
        CHECK(almasterqueue_data::started(queue.finished("c")) == std::string("a"));
        UNSCOPED_INFO("four busy throughout");
        CHECK(queue.busy() == size_t(4));
        UNSCOPED_INFO("then nothing");
        CHECK(almasterqueue_data::started(queue.finished("d")) == std::string());
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: a dropped script waiting or due again is not sent; one on its way goes on", "[almasterqueue]")
    {
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
        {
            INFO("c waits no longer");
            CHECK_FALSE(queue.waiting("c"));
            CHECK_FALSE(queue.underway("c"));
        }
        {
            INFO("a goes on, but not again");
            CHECK(queue.underway("a"));
            CHECK_FALSE(queue.waiting("a"));
        }
        UNSCOPED_INFO("a ends: d, not c, and not a");
        CHECK(almasterqueue_data::started(queue.finished("a")) == std::string("d*"));
        UNSCOPED_INFO("c asked again goes in at the back");
        CHECK_FALSE(queue.ask("c", Send::Direct));
        UNSCOPED_INFO("and goes in its turn");
        CHECK(almasterqueue_data::started(queue.finished("b")) == std::string("c"));
    }

    TEST_CASE_METHOD(almasterqueue_data, "almasterqueue: the limit holds and each is sent once in order; a limit is at least one", "[almasterqueue]")
    {
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
            UNSCOPED_INFO("never more than four");
            REQUIRE(queue.busy() <= 4);
        }
        REQUIRE(underway.size() == size_t(4));
        while (!underway.empty())
        {
            const std::string done = underway.front();
            underway.erase(underway.begin());
            for (const auto& [key, kind] : queue.finished(done))
            {
                underway.push_back(key);
                order += " " + key;
            }
            {
                INFO("never more than four");
                REQUIRE(queue.busy() <= 4);
                REQUIRE(queue.busy() == underway.size());
            }
        }
        UNSCOPED_INFO("all, once, in the order asked");
        CHECK(order == expected);

        ALMasterQueue none(0);
        UNSCOPED_INFO("a limit of none is one");
        CHECK(none.limit() == size_t(1));
        UNSCOPED_INFO("which starts");
        CHECK(none.ask("a", Send::Derived));
        UNSCOPED_INFO("one only");
        CHECK_FALSE(none.ask("b", Send::Derived));

        none.clear();
        CHECK_FALSE(none.underway("a"));
        CHECK_FALSE(none.waiting("b"));
        CHECK(none.busy() == 0);
        UNSCOPED_INFO("and starts afresh");
        CHECK(none.ask("b", Send::Derived));
        UNSCOPED_INFO("with nothing behind it");
        CHECK(almasterqueue_data::started(none.finished("b")) == std::string());
    }
}

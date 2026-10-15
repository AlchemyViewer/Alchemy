/**
 * @file alscriptjobqueue_test.cpp
 * @brief What waits for the script analysis thread: the latest of each, by rank, stale texts passed over.
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

#include "../alscriptjobqueue.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscriptjobqueue_data
    {
        ALScriptJobQueue<std::string> queue;

        // The jobs as they are taken, each run and finished at once.
        std::string drained()
        {
            std::string out;
            while (auto next = queue.take())
            {
                out += (out.empty() ? "" : " ") + next->second;
                queue.finished();
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptjobqueue_data, "alscriptjobqueue: the latest job of each key waits and they run by rank then by age", "[alscriptjobqueue]")
    {
        queue.add("back/check", "back", 1, 3, "back-check-1");
        queue.add("front/check", "front", 1, 1, "front-check-1");
        queue.add("front/check", "front", 2, 1, "front-check-2");
        queue.add("front/hover", "front", 2, 0, "front-hover");
        queue.add("front/weigh", "front", 2, 2, "front-weigh");
        queue.add("other/check", "other", 4, 3, "other-check");
        UNSCOPED_INFO("one of each key");
        CHECK(queue.waiting() == size_t(5));
        UNSCOPED_INFO("by rank, then by when");
        CHECK(drained() == std::string("front-hover front-check-2 front-weigh back-check-1 other-check"));
        CHECK(queue.passedOver() == size_t(0));
    }

    TEST_CASE_METHOD(alscriptjobqueue_data, "alscriptjobqueue: a question about a stale text is passed over but not across a reopen", "[alscriptjobqueue]")
    {
        queue.add("a/hover", "a", 5, 0, "hover-5");
        queue.add("a/check", "a", 6, 1, "check-6");
        UNSCOPED_INFO("the hover of the older text dropped as it is reached");
        CHECK(drained() == std::string("check-6"));
        UNSCOPED_INFO("and counted");
        CHECK(queue.passedOver() == size_t(1));
        // A script closed and opened again starts its versions over: what
        // its old text left waiting does not make the new one's stale.
        queue.add("b/hover", "b", 40, 0, "old-hover-40");
        queue.add("b/check", "b", 1, 1, "new-check-1");
        UNSCOPED_INFO("both run");
        CHECK(drained() == std::string("old-hover-40 new-check-1"));
        queue.add("c/check", "c", 1, 1, "c-check");
        queue.add("d/check", "d", 1, 1, "d-check");
        queue.forget("c");
        UNSCOPED_INFO("a script let go of: nothing it asked runs");
        CHECK(drained() == std::string("d-check"));
    }

    TEST_CASE_METHOD(alscriptjobqueue_data, "alscriptjobqueue: the running job is superseded only by its own key or a newer text", "[alscriptjobqueue]")
    {
        queue.add("a/check", "a", 3, 1, "check-3");
        REQUIRE(queue.take().has_value());
        UNSCOPED_INFO("nothing asked since");
        CHECK_FALSE(queue.superseded());
        UNSCOPED_INFO("another script's question leaves it be");
        CHECK_FALSE(queue.add("b/check", "b", 9, 1, "b-check"));
        UNSCOPED_INFO("another kind, the same text, leaves it be");
        CHECK_FALSE(queue.add("a/hover", "a", 3, 0, "hover-3"));
        UNSCOPED_INFO("an older text's leaves it be");
        CHECK_FALSE(queue.add("a/signature", "a", 2, 0, "signature-2"));
        UNSCOPED_INFO("not yet");
        CHECK_FALSE(queue.superseded());
        UNSCOPED_INFO("its own key asked again stops it");
        CHECK(queue.add("a/check", "a", 3, 1, "check-3-again"));
        CHECK(queue.superseded());
        UNSCOPED_INFO("said once");
        CHECK_FALSE(queue.add("a/check", "a", 4, 1, "check-4"));
        queue.finished();
        UNSCOPED_INFO("done with");
        CHECK_FALSE(queue.superseded());
        queue.add("x/hover", "x", 1, 0, "x-hover");
        REQUIRE(queue.take().has_value());
        UNSCOPED_INFO("a newer text of its script stops it");
        CHECK(queue.add("x/check", "x", 2, 1, "x-check"));
        queue.finished();
    }

    TEST_CASE_METHOD(alscriptjobqueue_data, "alscriptjobqueue: a yielding job stops for a rank 0 question and runs after it", "[alscriptjobqueue]")
    {
        queue.add("back/check", "back", 1, 4, "back-check", true);
        REQUIRE(queue.take().has_value());
        UNSCOPED_INFO("a check of the front tab leaves it be");
        CHECK_FALSE(queue.add("front/check", "front", 1, 1, "front-check"));
        UNSCOPED_INFO("a question of the front tab stops it");
        CHECK(queue.add("front/hover", "front", 1, 0, "front-hover"));
        CHECK(queue.yielded());
        UNSCOPED_INFO("its answer wanted still");
        CHECK_FALSE(queue.superseded());
        UNSCOPED_INFO("said once");
        CHECK_FALSE(queue.add("front/signature", "front", 1, 0, "front-signature"));
        queue.requeue("back-check");
        queue.finished();
        UNSCOPED_INFO("it runs after what it yielded to");
        CHECK(drained() == std::string("front-hover front-signature front-check back-check"));

        // Asked again while it ran: the newer stands for it.
        queue.add("back/check", "back", 1, 4, "back-check-1", true);
        REQUIRE(queue.take().has_value());
        UNSCOPED_INFO("stopped");
        CHECK(queue.add("front/hover", "front", 2, 0, "front-hover-2"));
        queue.add("back/check", "back", 2, 4, "back-check-2", true);
        queue.requeue("back-check-1");
        queue.finished();
        UNSCOPED_INFO("the newer alone");
        CHECK(drained() == std::string("front-hover-2 back-check-2"));

        // One that does not yield runs on.
        queue.add("back/weigh", "back", 3, 3, "back-weigh");
        REQUIRE(queue.take().has_value());
        {
            INFO("left be");
            CHECK_FALSE(queue.add("front/hover", "front", 3, 0, "front-hover-3"));
            CHECK_FALSE(queue.yielded());
        }
        queue.finished();
        drained();
    }

    TEST_CASE_METHOD(alscriptjobqueue_data, "alscriptjobqueue: a yielded job keeps its turn and one forgotten while running is dropped", "[alscriptjobqueue]")
    {
        queue.add("back/check", "back", 1, 4, "back-check", true);
        REQUIRE(queue.take().has_value());
        queue.add("other/check", "other", 1, 4, "other-check", true);
        {
            INFO("stopped by a question of the front tab");
            CHECK(queue.add("front/hover", "front", 1, 0, "front-hover"));
            CHECK(queue.yielded());
        }
        queue.requeue("back-check");
        queue.finished();
        auto next = queue.take();
        {
            INFO("the question first");
            REQUIRE(next);
            CHECK(next->second == "front-hover");
        }
        queue.finished();
        next = queue.take();
        {
            INFO("then it, ahead of what was asked after it");
            REQUIRE(next);
            CHECK(next->second == "back-check");
        }
        {
            INFO("not stopped again");
            CHECK_FALSE(queue.add("front/hover", "front", 1, 0, "front-hover-again"));
            CHECK_FALSE(queue.yielded());
        }
        UNSCOPED_INFO("its answer wanted");
        CHECK_FALSE(queue.superseded());
        queue.finished();
        UNSCOPED_INFO("the rest after");
        CHECK(drained() == std::string("front-hover-again other-check"));

        // Let go of while it runs: unwanted, said once, and not put back
        // though it was stopped as it yielded.
        queue.add("gone/check", "gone", 1, 4, "gone-check", true);
        REQUIRE(queue.take().has_value());
        queue.add("gone/hover", "gone", 1, 0, "gone-hover");
        CHECK(queue.yielded());
        UNSCOPED_INFO("let go of: to be stopped");
        CHECK(queue.forget("gone"));
        CHECK(queue.superseded());
        UNSCOPED_INFO("said once");
        CHECK_FALSE(queue.forget("gone"));
        queue.requeue("gone-check");
        queue.finished();
        UNSCOPED_INFO("nothing of it waits");
        CHECK(queue.waiting() == size_t(0));
        UNSCOPED_INFO("another script's running job left be");
        CHECK_FALSE(queue.forget("other"));
    }
}

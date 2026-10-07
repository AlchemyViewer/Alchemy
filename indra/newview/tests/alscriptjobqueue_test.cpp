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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alscriptjobqueue_data> alscriptjobqueue_group;
    typedef alscriptjobqueue_group::object    alscriptjobqueue_object;
    alscriptjobqueue_group                    alscriptjobqueue_instance("alscriptjobqueue");

    template<> template<>
    void alscriptjobqueue_object::test<1>()
    {
        set_test_name("only the latest of each kind about each script waits, and the lowest rank goes first, the oldest first among equals");
        queue.add("back/check", "back", 1, 3, "back-check-1");
        queue.add("front/check", "front", 1, 1, "front-check-1");
        queue.add("front/check", "front", 2, 1, "front-check-2");
        queue.add("front/hover", "front", 2, 0, "front-hover");
        queue.add("front/weigh", "front", 2, 2, "front-weigh");
        queue.add("other/check", "other", 4, 3, "other-check");
        ensure_equals("one of each key", queue.waiting(), size_t(5));
        ensure_equals("by rank, then by when", drained(), std::string("front-hover front-check-2 front-weigh back-check-1 other-check"));
        ensure_equals("nothing passed over", queue.passedOver(), size_t(0));
    }

    template<> template<>
    void alscriptjobqueue_object::test<2>()
    {
        set_test_name("a question about a text older than one asked about since is passed over; one asked before a newer text of a script opened again is not");
        queue.add("a/hover", "a", 5, 0, "hover-5");
        queue.add("a/check", "a", 6, 1, "check-6");
        ensure_equals("the hover of the older text dropped as it is reached", drained(), std::string("check-6"));
        ensure_equals("and counted", queue.passedOver(), size_t(1));
        // A script closed and opened again starts its versions over: what
        // its old text left waiting does not make the new one's stale.
        queue.add("b/hover", "b", 40, 0, "old-hover-40");
        queue.add("b/check", "b", 1, 1, "new-check-1");
        ensure_equals("both run", drained(), std::string("old-hover-40 new-check-1"));
        queue.add("c/check", "c", 1, 1, "c-check");
        queue.add("d/check", "d", 1, 1, "d-check");
        queue.forget("c");
        ensure_equals("a script let go of: nothing it asked runs", drained(), std::string("d-check"));
    }

    template<> template<>
    void alscriptjobqueue_object::test<3>()
    {
        set_test_name("the running job is told its answer is unwanted by its own key asked again or a newer text of its script, and by nothing else");
        queue.add("a/check", "a", 3, 1, "check-3");
        ensure("taken", queue.take().has_value());
        ensure("nothing asked since", !queue.superseded());
        ensure("another script's question leaves it be", !queue.add("b/check", "b", 9, 1, "b-check"));
        ensure("another kind, the same text, leaves it be", !queue.add("a/hover", "a", 3, 0, "hover-3"));
        ensure("an older text's leaves it be", !queue.add("a/signature", "a", 2, 0, "signature-2"));
        ensure("not yet", !queue.superseded());
        ensure("its own key asked again stops it", queue.add("a/check", "a", 3, 1, "check-3-again"));
        ensure("unwanted", queue.superseded());
        ensure("said once", !queue.add("a/check", "a", 4, 1, "check-4"));
        queue.finished();
        ensure("done with", !queue.superseded());
        queue.add("x/hover", "x", 1, 0, "x-hover");
        ensure("taken", queue.take().has_value());
        ensure("a newer text of its script stops it", queue.add("x/check", "x", 2, 1, "x-check"));
        queue.finished();
    }

    template<> template<>
    void alscriptjobqueue_object::test<4>()
    {
        set_test_name("a job that yields is stopped by a question of the lowest rank, waits again behind it and runs after; one that does not yield runs on");
        queue.add("back/check", "back", 1, 4, "back-check", true);
        ensure("taken", queue.take().has_value());
        ensure("a check of the front tab leaves it be", !queue.add("front/check", "front", 1, 1, "front-check"));
        ensure("a question of the front tab stops it", queue.add("front/hover", "front", 1, 0, "front-hover"));
        ensure("as it yields", queue.yielded());
        ensure("its answer wanted still", !queue.superseded());
        ensure("said once", !queue.add("front/signature", "front", 1, 0, "front-signature"));
        queue.requeue("back-check");
        queue.finished();
        ensure_equals("it runs after what it yielded to", drained(), std::string("front-hover front-signature front-check back-check"));

        // Asked again while it ran: the newer stands for it.
        queue.add("back/check", "back", 1, 4, "back-check-1", true);
        ensure("taken", queue.take().has_value());
        ensure("stopped", queue.add("front/hover", "front", 2, 0, "front-hover-2"));
        queue.add("back/check", "back", 2, 4, "back-check-2", true);
        queue.requeue("back-check-1");
        queue.finished();
        ensure_equals("the newer alone", drained(), std::string("front-hover-2 back-check-2"));

        // One that does not yield runs on.
        queue.add("back/weigh", "back", 3, 3, "back-weigh");
        ensure("taken", queue.take().has_value());
        ensure("left be", !queue.add("front/hover", "front", 3, 0, "front-hover-3") && !queue.yielded());
        queue.finished();
        drained();
    }

    template<> template<>
    void alscriptjobqueue_object::test<5>()
    {
        set_test_name("a job that yielded waits again in the turn it was asked at, and runs through the next time; one let go of as it runs is unwanted and waits no more");
        queue.add("back/check", "back", 1, 4, "back-check", true);
        ensure("taken", queue.take().has_value());
        queue.add("other/check", "other", 1, 4, "other-check", true);
        ensure("stopped by a question of the front tab", queue.add("front/hover", "front", 1, 0, "front-hover") && queue.yielded());
        queue.requeue("back-check");
        queue.finished();
        auto next = queue.take();
        ensure("the question first", next && next->second == "front-hover");
        queue.finished();
        next = queue.take();
        ensure("then it, ahead of what was asked after it", next && next->second == "back-check");
        ensure("not stopped again", !queue.add("front/hover", "front", 1, 0, "front-hover-again") && !queue.yielded());
        ensure("its answer wanted", !queue.superseded());
        queue.finished();
        ensure_equals("the rest after", drained(), std::string("front-hover-again other-check"));

        // Let go of while it runs: unwanted, said once, and not put back
        // though it was stopped as it yielded.
        queue.add("gone/check", "gone", 1, 4, "gone-check", true);
        ensure("taken", queue.take().has_value());
        queue.add("gone/hover", "gone", 1, 0, "gone-hover");
        ensure("yielded", queue.yielded());
        ensure("let go of: to be stopped", queue.forget("gone"));
        ensure("unwanted", queue.superseded());
        ensure("said once", !queue.forget("gone"));
        queue.requeue("gone-check");
        queue.finished();
        ensure_equals("nothing of it waits", queue.waiting(), size_t(0));
        ensure("another script's running job left be", !queue.forget("other"));
    }
}

/**
 * @file alfeedbackoutbox_test.cpp
 * @brief Tests for the feedback reports kept until the server answers
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

#include "../alfeedbackoutbox.h"

#include "fsyspath.h"
#include "lldate.h"
#include "lldir.h"
#include "llfile.h"
#include "lluuid.h"

#include "../test/lltut.h"

#include <fmt/format.h>

#include <chrono>
#include <filesystem>

namespace
{
    S64 now_seconds()
    {
        return static_cast<S64>(LLDate::now().secondsSinceEpoch());
    }

    ALFeedback::Queued report(const std::string& event_id, const std::string& message = "It broke")
    {
        ALFeedback::Queued queued;
        queued.eventId = event_id;
        queued.message = message;
        return queued;
    }

    // An id that sorts after the last: the n-th report.
    std::string id(int n)
    {
        return fmt::format("{:032x}", n + 1);
    }

    // Backdates a file, as if written that many seconds ago.
    void age(const std::string& path, S64 seconds)
    {
        std::error_code ec;
        const fsyspath file(path);
        std::filesystem::last_write_time(file, std::filesystem::last_write_time(file, ec) - std::chrono::seconds(seconds),
                                         ec);
    }
}

namespace tut
{
    struct alfeedbackoutbox_data
    {
        std::string mDir;

        alfeedbackoutbox_data()
        {
            mDir = gDirUtilp->add(gDirUtilp->getTempDir(), "feedback_outbox_" + LLUUID::generateNewID().asString());
            LLFile::mkdir(mDir);
        }

        ~alfeedbackoutbox_data()
        {
            gDirUtilp->deleteDirAndContents(mDir);
        }

        std::string path(const std::string& event_id, const char* extension) const
        {
            return gDirUtilp->add(mDir, event_id + extension);
        }
    };

    typedef test_group<alfeedbackoutbox_data> alfeedbackoutbox_group;
    typedef alfeedbackoutbox_group::object object;
    alfeedbackoutbox_group alfeedbackoutboxgrp("alfeedbackoutbox");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("a kept report is a body and a record, due a quarter hour later");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        ensure("kept", box.keep(report(id(1)), "body", now, given_up));
        ensure("has", box.has(id(1)));
        ensure_equals("body", LLFile::getContents(path(id(1), ".envelope")), "body");

        const auto records = box.records(now, given_up);
        ensure_equals("one", records.size(), 1u);
        ensure_equals("made now", records[0].created, now);
        ensure_equals("due", records[0].nextAt, now + ALFeedback::QUEUE_RETRY_SECONDS);
        ensure_equals("what the user wrote", records[0].message, "It broke");
        ensure("nothing given up", given_up.empty());
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("an id that is not one names no file");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        ensure("not kept", !box.keep(report("../escape"), "body", now_seconds(), given_up));
        ensure("not claimed", !box.claim("../escape"));
        ensure("nothing written", gDirUtilp->getFilesInDir(mDir).empty());
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("a claim is one run's: a second claim finds nothing");
        ALFeedbackOutbox box(mDir);
        ALFeedbackOutbox other(mDir);
        std::vector<ALFeedback::Queued> given_up;
        box.keep(report(id(1)), "body", now_seconds(), given_up);
        ensure("claimed", box.claim(id(1)));
        ensure("not twice", !other.claim(id(1)));
        ensure_equals("its body", box.claimedBody(id(1)), "body");
        ensure("still kept", box.has(id(1)));
        box.release(id(1));
        ensure("free again", other.claim(id(1)));
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("a claim is fresh however old its report: another run leaves it alone");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        // A report kept for hours: the body's time is old, the claim's is not.
        age(path(id(1), ".envelope"), 3 * 60 * 60);
        ensure("claimed", box.claim(id(1)));

        ALFeedbackOutbox(mDir).records(now, given_up);
        ensure("still claimed", LLFile::isfile(path(id(1), ".sending")));
        ensure("not freed", !LLFile::isfile(path(id(1), ".envelope")));
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("a claim left by a run that ended mid-send is free after an hour");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.claim(id(1));
        age(path(id(1), ".sending"), ALFeedbackOutbox::CLAIM_STALE_SECONDS + 60);
        box.records(now, given_up);
        ensure("freed", LLFile::isfile(path(id(1), ".envelope")));
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("the server's answer settles a report");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.keep(report(id(2)), "body", now, given_up);
        box.keep(report(id(3)), "body", now, given_up);

        ensure("sent is gone", !box.settle(id(1), false, ALFeedback::Outcome::Sent, 0.f, now).kept);
        ensure("forgotten", !box.has(id(1)));
        ensure("refused is gone", !box.settle(id(2), false, ALFeedback::Outcome::Rejected, 0.f, now).kept);

        box.claim(id(3));
        const auto settled = box.settle(id(3), true, ALFeedback::Outcome::RateLimited, 7200.f, now);
        ensure("unreached is kept", settled.kept);
        ensure("released", LLFile::isfile(path(id(3), ".envelope")));
        const auto records = box.records(now, given_up);
        ensure_equals("one left", records.size(), 1u);
        ensure_equals("tried", records[0].attempts, 1);
        ensure_equals("waits as asked", records[0].nextAt, now + 7200);
        ensure_equals("not before", records[0].notBefore, now + 7200);
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("a report tried too often is given up, with what the user wrote");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1), "Lost words"), "body", now, given_up);
        ALFeedbackOutbox::Settled settled;
        for (S32 i = 0; i < ALFeedback::QUEUE_MAX_ATTEMPTS; ++i)
        {
            settled = box.settle(id(1), false, ALFeedback::Outcome::Unreachable, 0.f, now);
        }
        ensure("given up", settled.givenUp.has_value());
        ensure_equals("its words", settled.givenUp->message, "Lost words");
        ensure("forgotten", !box.has(id(1)));
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("a report forgotten while it went is not kept again");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.claim(id(1));
        box.forget(id(1));
        ensure("not kept", !box.settle(id(1), true, ALFeedback::Outcome::Unreachable, 0.f, now).kept);
        ensure("nothing left", gDirUtilp->getFilesInDir(mDir).empty());
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("the oldest reports make room, and are given back");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        for (int i = 0; i < static_cast<int>(ALFeedback::QUEUE_MAX_ENTRIES); ++i)
        {
            box.keep(report(id(i)), "body", now + i, given_up);
        }
        ensure("room so far", given_up.empty());
        box.keep(report(id(99)), "body", now + 99, given_up);
        ensure_equals("one made room", given_up.size(), 1u);
        ensure_equals("the oldest", given_up[0].eventId, id(0));
        ensure_equals("as many as fit", box.records(now + 100, given_up).size(), ALFeedback::QUEUE_MAX_ENTRIES);
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("what a run left half-written is tidied away once it is old");
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        // A record whose body never came, a body whose record went, a part,
        // and a record that is not one.
        box.keep(report(id(1)), "body", now, given_up);
        LLFile::remove(path(id(1), ".envelope"));
        box.keep(report(id(2)), "body", now, given_up);
        LLFile::remove(path(id(2), ".json"));
        ALFeedbackOutbox::replaceFile(path(id(3), ".json"), "not json");
        ALFeedbackOutbox::replaceFile(gDirUtilp->add(mDir, "x.json.0123.tmp"), "part");

        ensure("young: kept", gDirUtilp->getFilesInDir(mDir).size() == 4u && box.records(now, given_up).empty());
        for (const std::string& name : gDirUtilp->getFilesInDir(mDir))
        {
            age(gDirUtilp->add(mDir, name), ALFeedbackOutbox::ORPHAN_SECONDS + 60);
        }
        box.records(now, given_up);
        ensure("old: gone", gDirUtilp->getFilesInDir(mDir).empty());
        ensure("nothing was a report", given_up.empty());
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("a file is replaced whole");
        const std::string file = gDirUtilp->add(mDir, "file.json");
        ensure("written", ALFeedbackOutbox::replaceFile(file, "first"));
        ensure("replaced", ALFeedbackOutbox::replaceFile(file, "second"));
        ensure_equals("the new", LLFile::getContents(file), "second");
        ensure_equals("no parts left", gDirUtilp->getFilesInDir(mDir).size(), 1u);
    }
}

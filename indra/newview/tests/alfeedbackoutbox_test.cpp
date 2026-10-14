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

#include <catch2/catch_test_macros.hpp>

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

namespace
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

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a kept report is a body and a record due a quarter hour later", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        REQUIRE(box.keep(report(id(1)), "body", now, given_up));
        REQUIRE(box.has(id(1)));
        CHECK(LLFile::getContents(path(id(1), ".envelope")) == "body");

        const auto records = box.records(now, given_up);
        REQUIRE(records.size() == 1u);
        CHECK(records[0].created == now);
        CHECK(records[0].nextAt == now + ALFeedback::QUEUE_RETRY_SECONDS);
        CHECK(records[0].message == "It broke");
        REQUIRE(given_up.empty());
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: an id that would leave the folder names no file", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        CHECK_FALSE(box.keep(report("../escape"), "body", now_seconds(), given_up));
        CHECK_FALSE(box.claim("../escape"));
        REQUIRE(gDirUtilp->getFilesInDir(mDir).empty());
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a claimed report cannot be claimed again until released", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        ALFeedbackOutbox other(mDir);
        std::vector<ALFeedback::Queued> given_up;
        box.keep(report(id(1)), "body", now_seconds(), given_up);
        CHECK(box.claim(id(1)));
        CHECK_FALSE(other.claim(id(1)));
        CHECK(box.claimedBody(id(1)) == "body");
        UNSCOPED_INFO("a claimed report is still kept");
        REQUIRE(box.has(id(1)));
        box.release(id(1));
        CHECK(other.claim(id(1)));
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: another run leaves a fresh claim on an old report alone", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        // A report kept for hours: the body's time is old, the claim's is not.
        age(path(id(1), ".envelope"), 3 * 60 * 60);
        CHECK(box.claim(id(1)));

        ALFeedbackOutbox(mDir).records(now, given_up);
        UNSCOPED_INFO("still claimed");
        CHECK(LLFile::isfile(path(id(1), ".sending")));
        UNSCOPED_INFO("not freed");
        CHECK_FALSE(LLFile::isfile(path(id(1), ".envelope")));
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a claim left by a run that ended mid-send is free after an hour", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.claim(id(1));
        age(path(id(1), ".sending"), ALFeedbackOutbox::CLAIM_STALE_SECONDS + 60);
        box.records(now, given_up);
        UNSCOPED_INFO("the stale claim is freed");
        CHECK(LLFile::isfile(path(id(1), ".envelope")));
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a sent or refused report goes and a rate-limited one waits", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.keep(report(id(2)), "body", now, given_up);
        box.keep(report(id(3)), "body", now, given_up);

        CHECK_FALSE(box.settle(id(1), false, ALFeedback::Outcome::Sent, 0.f, now).kept);
        REQUIRE_FALSE(box.has(id(1)));
        CHECK_FALSE(box.settle(id(2), false, ALFeedback::Outcome::Rejected, 0.f, now).kept);

        box.claim(id(3));
        const auto settled = box.settle(id(3), true, ALFeedback::Outcome::RateLimited, 7200.f, now);
        UNSCOPED_INFO("a rate-limited report is kept");
        CHECK(settled.kept);
        UNSCOPED_INFO("the claim is released");
        CHECK(LLFile::isfile(path(id(3), ".envelope")));
        const auto records = box.records(now, given_up);
        REQUIRE(records.size() == 1u);
        CHECK(records[0].attempts == 1);
        UNSCOPED_INFO("waits as long as the server asked");
        CHECK(records[0].nextAt == now + 7200);
        CHECK(records[0].notBefore == now + 7200);
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a report tried too often is given up with what the user wrote", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1), "Lost words"), "body", now, given_up);
        ALFeedbackOutbox::Settled settled;
        for (S32 i = 0; i < ALFeedback::QUEUE_MAX_ATTEMPTS; ++i)
        {
            settled = box.settle(id(1), false, ALFeedback::Outcome::Unreachable, 0.f, now);
        }
        REQUIRE(settled.givenUp.has_value());
        CHECK(settled.givenUp->message == "Lost words");
        REQUIRE_FALSE(box.has(id(1)));
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: a report forgotten while it was sending is not kept again", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        box.keep(report(id(1)), "body", now, given_up);
        box.claim(id(1));
        box.forget(id(1));
        CHECK_FALSE(box.settle(id(1), true, ALFeedback::Outcome::Unreachable, 0.f, now).kept);
        REQUIRE(gDirUtilp->getFilesInDir(mDir).empty());
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: the oldest reports make room and are given back", "[alfeedbackoutbox]")
    {
        ALFeedbackOutbox box(mDir);
        std::vector<ALFeedback::Queued> given_up;
        const S64 now = now_seconds();
        for (int i = 0; i < static_cast<int>(ALFeedback::QUEUE_MAX_ENTRIES); ++i)
        {
            box.keep(report(id(i)), "body", now + i, given_up);
        }
        UNSCOPED_INFO("nothing given up while there is room");
        REQUIRE(given_up.empty());
        box.keep(report(id(99)), "body", now + 99, given_up);
        REQUIRE(given_up.size() == 1u);
        UNSCOPED_INFO("the oldest is given back");
        CHECK(given_up[0].eventId == id(0));
        REQUIRE(box.records(now + 100, given_up).size() == ALFeedback::QUEUE_MAX_ENTRIES);
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: what a run left half-written is tidied away once it is old", "[alfeedbackoutbox]")
    {
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

        {
            INFO("young: kept");
            REQUIRE(gDirUtilp->getFilesInDir(mDir).size() == 4u);
            REQUIRE(box.records(now, given_up).empty());
        }
        for (const std::string& name : gDirUtilp->getFilesInDir(mDir))
        {
            age(gDirUtilp->add(mDir, name), ALFeedbackOutbox::ORPHAN_SECONDS + 60);
        }
        box.records(now, given_up);
        UNSCOPED_INFO("old: gone");
        REQUIRE(gDirUtilp->getFilesInDir(mDir).empty());
        UNSCOPED_INFO("nothing was a report");
        REQUIRE(given_up.empty());
    }

    TEST_CASE_METHOD(alfeedbackoutbox_data, "alfeedbackoutbox: replaceFile replaces a file whole and leaves no parts", "[alfeedbackoutbox]")
    {
        const std::string file = gDirUtilp->add(mDir, "file.json");
        CHECK(ALFeedbackOutbox::replaceFile(file, "first"));
        CHECK(ALFeedbackOutbox::replaceFile(file, "second"));
        CHECK(LLFile::getContents(file) == "second");
        UNSCOPED_INFO("no parts left");
        REQUIRE(gDirUtilp->getFilesInDir(mDir).size() == 1u);
    }
}

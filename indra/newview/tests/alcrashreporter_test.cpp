/**
 * @file alcrashreporter_test.cpp
 * @brief Tests for what a crash report files under
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

#include "../alcrashreporter.h"

#include "llsd.h"
#include "lluuid.h"
#include "v3math.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace
{
    TEST_CASE("alcrashreporter: the release is the package at a version plus a build", "[alcrashreporter]")
    {
        CHECK(ALCrashReporter::releaseName(26, 4, 0, 64033) == "alchemy@26.4.0+64033");
    }

    TEST_CASE("alcrashreporter: a location is the region and whole metres", "[alcrashreporter]")
    {
        CHECK(ALCrashReporter::locationTag("Hippotropolis", LLVector3(128.4f, 63.6f, 22.5f)) ==
                      "Hippotropolis/128/64/23");
    }

    TEST_CASE("alcrashreporter: a location keeps the region's own spelling", "[alcrashreporter]")
    {
        CHECK(ALCrashReporter::locationTag("Ye Olde Region", LLVector3(0.f, 0.f, 0.f)) ==
                      "Ye Olde Region/0/0/0");
    }

    TEST_CASE("alcrashreporter: the run id is one string for the whole run", "[alcrashreporter]")
    {
        const std::string& first = ALCrashReporter::runId();
        UNSCOPED_INFO("a UUID");
        CHECK(first.size() == 36u);
        CHECK(ALCrashReporter::runId() == first);
    }

    TEST_CASE("alcrashreporter: the previous run is read from its static debug file", "[alcrashreporter]")
    {
        LLSD info;
        info["RunId"] = "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c";
        info["SLLog"] = "/logs/Alchemy.crash";
        info["SettingsFilename"] = "/settings/settings.xml";
        info["PerAccountSettingsFilename"] = "/settings/ye_olde/settings_per_account.xml";
        info["LoginName"] = "Ye_Olde_Avatar";
        info["CurrentRegion"] = "Hippotropolis";
        info["FatalMessage"] = "LL_ERRS: something";

        ALCrashReporter::PreviousRun run = ALCrashReporter::previousRun(info);
        CHECK(run.runId == "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c");
        CHECK(run.logFile == "/logs/Alchemy.crash");
        CHECK(run.userSettingsFile == "/settings/settings.xml");
        CHECK(run.accountSettingsFile == "/settings/ye_olde/settings_per_account.xml");
        UNSCOPED_INFO("underscores become spaces");
        CHECK(run.agentName == "Ye Olde Avatar");
        CHECK(run.region == "Hippotropolis");
        CHECK(run.fatalMessage == "LL_ERRS: something");
    }

    TEST_CASE("alcrashreporter: a file from before run ids reads as empty fields", "[alcrashreporter]")
    {
        ALCrashReporter::PreviousRun run = ALCrashReporter::previousRun(LLSD());
        CHECK(run.runId.empty());
        CHECK(run.logFile.empty());
        CHECK(run.agentName.empty());
    }

    TEST_CASE("alcrashreporter: consent is a sentinel file that allowing makes and withdrawing removes", "[alcrashreporter]")
    {
        // A name of this run's own: another run of the tests may be at it.
        const std::string sentinel =
            (std::filesystem::temp_directory_path() / ("alcrashreporter_test_consent_" + LLUUID::generateNewID().asString())).string();

        ALCrashReporter::recordConsent(sentinel, false);
        UNSCOPED_INFO("absent to begin with");
        CHECK_FALSE(ALCrashReporter::consentRecorded(sentinel));

        ALCrashReporter::recordConsent(sentinel, true);
        UNSCOPED_INFO("present once allowed");
        CHECK(ALCrashReporter::consentRecorded(sentinel));
        ALCrashReporter::recordConsent(sentinel, true);
        UNSCOPED_INFO("allowing twice keeps it");
        CHECK(ALCrashReporter::consentRecorded(sentinel));

        ALCrashReporter::recordConsent(sentinel, false);
        UNSCOPED_INFO("gone once withdrawn");
        CHECK_FALSE(ALCrashReporter::consentRecorded(sentinel));
        ALCrashReporter::recordConsent(sentinel, false);
        UNSCOPED_INFO("withdrawing twice is quiet");
        CHECK_FALSE(ALCrashReporter::consentRecorded(sentinel));
    }

    TEST_CASE("alcrashreporter: a location written into a buffer is the same or cut to fit", "[alcrashreporter]")
    {
        const LLVector3 position(128.4f, -63.6f, 22.5f);
        char buffer[64];
        size_t length = ALCrashReporter::locationTag(buffer, sizeof(buffer), "Hippotropolis", position);
        REQUIRE(length < sizeof(buffer));
        CHECK(std::string(buffer, length) == "Hippotropolis/128/-64/23");
        CHECK(buffer[length] == '\0');

        char small[12];
        length = ALCrashReporter::locationTag(small, sizeof(small), "Hippotropolis", position);
        REQUIRE(length < sizeof(small));
        CHECK(std::string(small, length) == "Hippotropol");
        CHECK(small[length] == '\0');

        length = ALCrashReporter::locationTag(small, sizeof(small), "Ahern", position);
        REQUIRE(length < sizeof(small));
        UNSCOPED_INFO("cut in the numbers");
        CHECK(std::string(small, length) == "Ahern/128/-");
    }

    TEST_CASE("alcrashreporter: an event id is its 32 hex digits however it is spelled", "[alcrashreporter]")
    {
        CHECK(ALCrashReporter::compactEventId("0123ABCD-89ab-cdef-0123-456789ABCDEF") ==
                      "0123abcd89abcdef0123456789abcdef");
        CHECK(ALCrashReporter::compactEventId("{0123abcd-89ab-cdef-0123-456789abcdef}") ==
                      "0123abcd89abcdef0123456789abcdef");
        CHECK(ALCrashReporter::compactEventId("0123abcd89abcdef0123456789abcdef") ==
                      "0123abcd89abcdef0123456789abcdef");
        UNSCOPED_INFO("too short");
        CHECK(ALCrashReporter::compactEventId("0123abcd").empty());
        UNSCOPED_INFO("not hex");
        CHECK(ALCrashReporter::compactEventId("0123abcd89abcdef0123456789abcdeg").empty());
        CHECK(ALCrashReporter::compactEventId("").empty());
    }

    TEST_CASE("alcrashreporter: a freeze's report is recorded for the next run to take once", "[alcrashreporter]")
    {
        // Names of this run's own, here and below: another run of the tests may be at it.
        const std::string record =
            (std::filesystem::temp_directory_path() / ("alcrashreporter_test_last_report_" + LLUUID::generateNewID().asString())).string();
        ALCrashReporter::takeRecordedReport(record);
        UNSCOPED_INFO("nothing to begin with");
        CHECK_FALSE(ALCrashReporter::takeRecordedReport(record));

        ALCrashReporter::recordReport(record, { "freeze", "0123abcd89abcdef0123456789abcdef" });
        const auto taken = ALCrashReporter::takeRecordedReport(record);
        REQUIRE(taken.has_value());
        CHECK(taken->kind == "freeze");
        CHECK(taken->eventId == "0123abcd89abcdef0123456789abcdef");
        UNSCOPED_INFO("taken only once");
        CHECK_FALSE(ALCrashReporter::takeRecordedReport(record));

        ALCrashReporter::recordReport(record, { "lunch", "0123abcd89abcdef0123456789abcdef" });
        UNSCOPED_INFO("an unknown kind is not a report");
        CHECK_FALSE(ALCrashReporter::takeRecordedReport(record));
        ALCrashReporter::recordReport(record, { "crash", "nonsense" });
        UNSCOPED_INFO("nor is a broken id");
        CHECK_FALSE(ALCrashReporter::takeRecordedReport(record));
    }

    TEST_CASE("alcrashreporter: the nil UUID is no event however it is spelled", "[alcrashreporter]")
    {
        CHECK(ALCrashReporter::compactEventId("00000000-0000-0000-0000-000000000000").empty());
        CHECK(ALCrashReporter::compactEventId("{00000000-0000-0000-0000-000000000000}").empty());
        CHECK(ALCrashReporter::compactEventId("00000000000000000000000000000000").empty());
        UNSCOPED_INFO("one digit set is an event");
        CHECK(ALCrashReporter::compactEventId("00000000-0000-0000-0000-000000000001") ==
                      "00000000000000000000000000000001");

        const std::string record =
            (std::filesystem::temp_directory_path() / ("alcrashreporter_test_nil_report_" + LLUUID::generateNewID().asString())).string();
        ALCrashReporter::recordReport(record, { "freeze", "00000000000000000000000000000000" });
        UNSCOPED_INFO("a nil report is not taken");
        CHECK_FALSE(ALCrashReporter::takeRecordedReport(record));
    }

    TEST_CASE("alcrashreporter: a crash found at start is the previous run's only if it carries its id", "[alcrashreporter]")
    {
        const std::string_view event = "0123abcd-89ab-cdef-0123-456789abcdef";
        const std::string_view previous = "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c";

        const auto crash = ALCrashReporter::previousRunCrash(event, previous, previous);
        UNSCOPED_INFO("the previous run's");
        REQUIRE(crash.has_value());
        CHECK(crash->kind == "crash");
        CHECK(crash->eventId == "0123abcd89abcdef0123456789abcdef");
        UNSCOPED_INFO("the run id however it is spelled");
        CHECK(ALCrashReporter::previousRunCrash(event, "6F4B1B2E9D8C4D0FA5B72C3E4F5A6B7C", previous).has_value());

        UNSCOPED_INFO("another run's");
        CHECK_FALSE(ALCrashReporter::previousRunCrash(event, "7f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c",
                                                                   previous));
        UNSCOPED_INFO("one that carries no run id");
        CHECK_FALSE(ALCrashReporter::previousRunCrash(event, "", previous));
        UNSCOPED_INFO("any when the previous run is not known");
        CHECK_FALSE(ALCrashReporter::previousRunCrash(event, previous, ""));
        UNSCOPED_INFO("one filed as nothing");
        CHECK_FALSE(ALCrashReporter::previousRunCrash("00000000-0000-0000-0000-000000000000", previous, previous));
    }

    TEST_CASE("alcrashreporter: a record replaces the last whole and leaves nothing beside it", "[alcrashreporter]")
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / ("alcrashreporter_test_records_" + LLUUID::generateNewID().asString());
        std::filesystem::create_directories(dir);
        const auto entries = [&dir]()
        {
            return std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator());
        };
        const std::string record = (dir / "last_report").string();

        ALCrashReporter::recordReport(record, { "freeze", "0123abcd89abcdef0123456789abcdef" });
        ALCrashReporter::recordReport(record, { "crash", "fedcba9876543210fedcba9876543210" });
        UNSCOPED_INFO("the record alone");
        CHECK(entries() == 1);
        const auto taken = ALCrashReporter::takeRecordedReport(record);
        REQUIRE(taken.has_value());
        UNSCOPED_INFO("the later kind");
        CHECK(taken->kind == "crash");
        UNSCOPED_INFO("the later id");
        CHECK(taken->eventId == "fedcba9876543210fedcba9876543210");
        UNSCOPED_INFO("nothing once taken");
        CHECK(entries() == 0);

        // A record that cannot be put in place leaves nothing written.
        const std::filesystem::path blocked = dir / "blocked";
        std::filesystem::create_directories(blocked / "inside");
        ALCrashReporter::recordReport(blocked.string(), { "freeze", "0123abcd89abcdef0123456789abcdef" });
        UNSCOPED_INFO("only what was there");
        CHECK(entries() == 1);
        UNSCOPED_INFO("still a directory");
        CHECK(std::filesystem::is_directory(blocked));

        std::filesystem::remove_all(dir);
    }
}

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
#include "v3math.h"

#include "../test/lltut.h"

#include <filesystem>

namespace tut
{
    struct alcrashreporter_data
    {
    };

    typedef test_group<alcrashreporter_data> alcrashreporter_group;
    typedef alcrashreporter_group::object object;
    alcrashreporter_group alcrashreportergrp("alcrashreporter");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("the release is the package at a version plus a build");
        ensure_equals(ALCrashReporter::releaseName(26, 4, 0, 64033), "alchemy@26.4.0+64033");
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("a location is the region and whole metres");
        ensure_equals(ALCrashReporter::locationTag("Hippotropolis", LLVector3(128.4f, 63.6f, 22.5f)),
                      "Hippotropolis/128/64/23");
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("a location keeps the region's own spelling");
        ensure_equals(ALCrashReporter::locationTag("Ye Olde Region", LLVector3(0.f, 0.f, 0.f)),
                      "Ye Olde Region/0/0/0");
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("the run id is one string for the whole run");
        const std::string& first = ALCrashReporter::runId();
        ensure_equals("a UUID", first.size(), 36u);
        ensure_equals("stable", ALCrashReporter::runId(), first);
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("the previous run is read from its static debug file");
        LLSD info;
        info["RunId"] = "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c";
        info["SLLog"] = "/logs/Alchemy.crash";
        info["SettingsFilename"] = "/settings/settings.xml";
        info["PerAccountSettingsFilename"] = "/settings/ye_olde/settings_per_account.xml";
        info["LoginName"] = "Ye_Olde_Avatar";
        info["CurrentRegion"] = "Hippotropolis";
        info["FatalMessage"] = "LL_ERRS: something";

        ALCrashReporter::PreviousRun run = ALCrashReporter::previousRun(info);
        ensure_equals(run.runId, "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c");
        ensure_equals(run.logFile, "/logs/Alchemy.crash");
        ensure_equals(run.userSettingsFile, "/settings/settings.xml");
        ensure_equals(run.accountSettingsFile, "/settings/ye_olde/settings_per_account.xml");
        ensure_equals("underscores become spaces", run.agentName, "Ye Olde Avatar");
        ensure_equals(run.region, "Hippotropolis");
        ensure_equals(run.fatalMessage, "LL_ERRS: something");
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("a file from before run ids reads as empty fields");
        ALCrashReporter::PreviousRun run = ALCrashReporter::previousRun(LLSD());
        ensure("no run id", run.runId.empty());
        ensure("no log", run.logFile.empty());
        ensure("no name", run.agentName.empty());
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("consent is a sentinel file: recorded, seen, withdrawn");
        const std::string sentinel =
            (std::filesystem::temp_directory_path() / "alcrashreporter_test_consent").string();

        ALCrashReporter::recordConsent(sentinel, false);
        ensure("absent to begin with", !ALCrashReporter::consentRecorded(sentinel));

        ALCrashReporter::recordConsent(sentinel, true);
        ensure("present once allowed", ALCrashReporter::consentRecorded(sentinel));
        ALCrashReporter::recordConsent(sentinel, true);
        ensure("allowing twice keeps it", ALCrashReporter::consentRecorded(sentinel));

        ALCrashReporter::recordConsent(sentinel, false);
        ensure("gone once withdrawn", !ALCrashReporter::consentRecorded(sentinel));
        ALCrashReporter::recordConsent(sentinel, false);
        ensure("withdrawing twice is quiet", !ALCrashReporter::consentRecorded(sentinel));
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("a location written into a buffer is the same, or cut to fit");
        const LLVector3 position(128.4f, -63.6f, 22.5f);
        char buffer[64];
        size_t length = ALCrashReporter::locationTag(buffer, sizeof(buffer), "Hippotropolis", position);
        ensure_equals("whole", std::string(buffer, length), "Hippotropolis/128/-64/23");
        ensure_equals("terminated", buffer[length], '\0');

        char small[12];
        length = ALCrashReporter::locationTag(small, sizeof(small), "Hippotropolis", position);
        ensure_equals("cut", std::string(small, length), "Hippotropol");
        ensure_equals("cut and terminated", small[length], '\0');

        length = ALCrashReporter::locationTag(small, sizeof(small), "Ahern", position);
        ensure_equals("cut in the numbers", std::string(small, length), "Ahern/128/-");
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("an event id is its 32 hex digits however it is spelled");
        ensure_equals("dashed", ALCrashReporter::compactEventId("0123ABCD-89ab-cdef-0123-456789ABCDEF"),
                      "0123abcd89abcdef0123456789abcdef");
        ensure_equals("braced", ALCrashReporter::compactEventId("{0123abcd-89ab-cdef-0123-456789abcdef}"),
                      "0123abcd89abcdef0123456789abcdef");
        ensure_equals("already compact", ALCrashReporter::compactEventId("0123abcd89abcdef0123456789abcdef"),
                      "0123abcd89abcdef0123456789abcdef");
        ensure("too short", ALCrashReporter::compactEventId("0123abcd").empty());
        ensure("not hex", ALCrashReporter::compactEventId("0123abcd89abcdef0123456789abcdeg").empty());
        ensure("empty", ALCrashReporter::compactEventId("").empty());
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("a freeze's report is recorded for the next run, which takes it once");
        const std::string record =
            (std::filesystem::temp_directory_path() / "alcrashreporter_test_last_report").string();
        ALCrashReporter::takeRecordedReport(record);
        ensure("nothing to begin with", !ALCrashReporter::takeRecordedReport(record));

        ALCrashReporter::recordReport(record, { "freeze", "0123abcd89abcdef0123456789abcdef" });
        const auto taken = ALCrashReporter::takeRecordedReport(record);
        ensure("taken", taken.has_value());
        ensure_equals("kind", taken->kind, "freeze");
        ensure_equals("id", taken->eventId, "0123abcd89abcdef0123456789abcdef");
        ensure("once", !ALCrashReporter::takeRecordedReport(record));

        ALCrashReporter::recordReport(record, { "lunch", "0123abcd89abcdef0123456789abcdef" });
        ensure("an unknown kind is not a report", !ALCrashReporter::takeRecordedReport(record));
        ALCrashReporter::recordReport(record, { "crash", "nonsense" });
        ensure("nor is a broken id", !ALCrashReporter::takeRecordedReport(record));
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("the nil UUID is no event, however it is spelled");
        ensure("dashed", ALCrashReporter::compactEventId("00000000-0000-0000-0000-000000000000").empty());
        ensure("braced", ALCrashReporter::compactEventId("{00000000-0000-0000-0000-000000000000}").empty());
        ensure("compact", ALCrashReporter::compactEventId("00000000000000000000000000000000").empty());
        ensure_equals("one digit set is an event",
                      ALCrashReporter::compactEventId("00000000-0000-0000-0000-000000000001"),
                      "00000000000000000000000000000001");

        const std::string record =
            (std::filesystem::temp_directory_path() / "alcrashreporter_test_nil_report").string();
        ALCrashReporter::recordReport(record, { "freeze", "00000000000000000000000000000000" });
        ensure("a nil report is not taken", !ALCrashReporter::takeRecordedReport(record));
    }

    template<> template<>
    void object::test<12>()
    {
        set_test_name("a crash found at start is the previous run's only when it carries that run's id");
        const std::string_view event = "0123abcd-89ab-cdef-0123-456789abcdef";
        const std::string_view previous = "6f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c";

        const auto crash = ALCrashReporter::previousRunCrash(event, previous, previous);
        ensure("the previous run's", crash.has_value());
        ensure_equals("a crash", crash->kind, "crash");
        ensure_equals("its id", crash->eventId, "0123abcd89abcdef0123456789abcdef");
        ensure("the run id however it is spelled",
               ALCrashReporter::previousRunCrash(event, "6F4B1B2E9D8C4D0FA5B72C3E4F5A6B7C", previous).has_value());

        ensure("another run's", !ALCrashReporter::previousRunCrash(event, "7f4b1b2e-9d8c-4d0f-a5b7-2c3e4f5a6b7c",
                                                                   previous));
        ensure("one that carries no run id", !ALCrashReporter::previousRunCrash(event, "", previous));
        ensure("any, when the previous run is not known", !ALCrashReporter::previousRunCrash(event, previous, ""));
        ensure("one filed as nothing",
               !ALCrashReporter::previousRunCrash("00000000-0000-0000-0000-000000000000", previous, previous));
    }

    template<> template<>
    void object::test<13>()
    {
        set_test_name("a record is written whole over the last, and nothing is left beside it");
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "alcrashreporter_test_records";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto entries = [&dir]()
        {
            return std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator());
        };
        const std::string record = (dir / "last_report").string();

        ALCrashReporter::recordReport(record, { "freeze", "0123abcd89abcdef0123456789abcdef" });
        ALCrashReporter::recordReport(record, { "crash", "fedcba9876543210fedcba9876543210" });
        ensure_equals("the record alone", entries(), 1);
        const auto taken = ALCrashReporter::takeRecordedReport(record);
        ensure("taken", taken.has_value());
        ensure_equals("the later kind", taken->kind, "crash");
        ensure_equals("the later id", taken->eventId, "fedcba9876543210fedcba9876543210");
        ensure_equals("nothing once taken", entries(), 0);

        // A record that cannot be put in place leaves nothing written.
        const std::filesystem::path blocked = dir / "blocked";
        std::filesystem::create_directories(blocked / "inside");
        ALCrashReporter::recordReport(blocked.string(), { "freeze", "0123abcd89abcdef0123456789abcdef" });
        ensure_equals("only what was there", entries(), 1);
        ensure("still a directory", std::filesystem::is_directory(blocked));

        std::filesystem::remove_all(dir);
    }
}

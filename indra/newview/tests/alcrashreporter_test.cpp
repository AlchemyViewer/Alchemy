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
}

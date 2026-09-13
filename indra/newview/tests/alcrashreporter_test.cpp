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

#include "v3math.h"

#include "../test/lltut.h"

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
}

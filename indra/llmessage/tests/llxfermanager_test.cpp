/**
 * @file llxfermanager_test.cpp
 * @brief Tests of LLXferManager's file requests.
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

#include "../llxfermanager.h"
#include "../llxfer_file.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("LLXferManager: a remote file name shorter than its suffix is requested", "[LLXferManager]")
    {
        // The simulator names the remote file, and may give it fewer
        // characters than ".tmp". With no download allowed to start, the
        // request is only queued, and nothing is sent.
        LLXferManager manager;
        manager.setMaxIncomingXfers(0);
        for (const char* remote : { "", "a", "ab", "abc", ".tmp", "a.tmp" })
        {
            INFO("remote file name '" << remote << "'");
            U64 id = 0;
            CHECK_NOTHROW(id = manager.requestFile("llxfermanager_test_local.tmp", remote, LL_PATH_CACHE, LLHost(),
                                                   true, nullptr, nullptr));
            CHECK(id != 0);
        }
    }

    TEST_CASE("LLXferManager: a local file name shorter than its suffix makes a file transfer", "[LLXferManager]")
    {
        for (const char* local : { "a", "ab", "abc" })
        {
            INFO("local file name '" << local << "'");
            CHECK_NOTHROW(LLXfer_File(local, true, -1));
        }
    }
}

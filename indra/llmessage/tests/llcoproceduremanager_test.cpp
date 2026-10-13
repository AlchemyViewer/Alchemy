/**
 * @file llcoproceduremanager_test.cpp
 * @author Brad
 * @date 2019-02
 * @brief LLCoprocedureManager unit test
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llwin32headers.h"

#include "linden_common.h"
#include "llsdserialize.h"

#include "../llcoproceduremanager.h"

#include <functional>

#include <catch2/catch_test_macros.hpp>
#include "../test/sync.h"


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

LLCoreHttpUtil::HttpCoroutineAdapter::HttpCoroutineAdapter(std::string name, LLCore::HttpRequest::policy_t policyId,
                                                           Destination destination)
{
}

void LLCoreHttpUtil::HttpCoroutineAdapter::cancelSuspendedOperation()
{
}

LLCoreHttpUtil::HttpCoroutineAdapter::~HttpCoroutineAdapter()
{
}

LLCore::HttpRequest::HttpRequest()
{
}

LLCore::HttpRequest::~HttpRequest()
{
}

namespace
{
    struct coproceduremanager_test
    {
        coproceduremanager_test()
        {
        }

        ~coproceduremanager_test()
        {
            LLCoprocedureManager::instance().close();
        }
    };


    TEST_CASE_METHOD(coproceduremanager_test, "LLCoprocedureManager: an enqueued coprocedure runs in its pool", "[LLCoprocedureManager]")
    {
        Sync sync;
        int foo = 0;
        LLCoprocedureManager::instance().initializePool("PoolName");
        LLCoprocedureManager::instance().enqueueCoprocedure("PoolName", "ProcName",
            [&foo, &sync] (LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t & ptr, const LLUUID & id) {
                sync.bump();
                foo = 1;
            });

        sync.yield();
        CHECK(foo == 1);

        LLCoprocedureManager::instance().close("PoolName");
    }

    TEST_CASE_METHOD(coproceduremanager_test, "LLCoprocedureManager: a pool runs every coprocedure enqueued on it", "[LLCoprocedureManager]")
    {
        // More than the pool has coroutines, so at least one of them goes back for another.
        const int COPROC_COUNT = 12;
        Sync sync;
        int counter = 0;
        LLCoprocedureManager::instance().initializePool("EveryCoproc");
        for (int i = 0; i < COPROC_COUNT; ++i)
        {
            LLCoprocedureManager::instance().enqueueCoprocedure("EveryCoproc", "Count",
                [&counter, &sync] (LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t & ptr, const LLUUID & id) {
                    ++counter;
                    sync.bump();
                });
        }

        sync.yield_until(COPROC_COUNT);
        CHECK(counter == COPROC_COUNT);

        LLCoprocedureManager::instance().close("EveryCoproc");
    }
}  // anonymous namespace

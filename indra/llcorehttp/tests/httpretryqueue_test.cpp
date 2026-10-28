/**
 * @file httpretryqueue_test.cpp
 * @brief Tests of the order HttpRetryQueue keeps its retries in.
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

#include "_httpretryqueue.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace LLCore;

namespace
{
    HttpOpRequest::ptr_t retryAt(HttpTime when)
    {
        HttpOpRequest::ptr_t op(new HttpOpRequest());
        op->mPolicyRetryAt = when;
        return op;
    }

    std::vector<HttpTime> drain(HttpRetryQueue & queue)
    {
        std::vector<HttpTime> times;
        while (! queue.empty())
        {
            times.push_back(queue.top()->mPolicyRetryAt);
            queue.pop();
        }
        return times;
    }

    TEST_CASE("httpretryqueue: the earliest retry is at the top", "[httpretryqueue]")
    {
        HttpRetryQueue queue;
        for (HttpTime when : { 5, 4, 3, 2, 1 })
        {
            queue.push(retryAt(when));
        }
        CHECK(drain(queue) == std::vector<HttpTime>{ 1, 2, 3, 4, 5 });
    }

    TEST_CASE("httpretryqueue: erasing a retry keeps the rest in order", "[httpretryqueue]")
    {
        // Each position of the heap's container in turn, since only some
        // erasures leave the rest out of heap order.
        const std::vector<HttpTime> pushed{ 5, 8, 1, 7, 3, 6, 2, 4 };
        for (size_t position(0); position < pushed.size(); ++position)
        {
            CAPTURE(position);
            HttpRetryQueue queue;
            for (HttpTime when : pushed)
            {
                queue.push(retryAt(when));
            }
            HttpRetryQueue::container_type & ops(queue.get_container());
            const HttpTime erased(ops[position]->mPolicyRetryAt);
            queue.erase(ops.begin() + position);

            std::vector<HttpTime> expected;
            for (HttpTime when(1); when <= 8; ++when)
            {
                if (when != erased)
                {
                    expected.push_back(when);
                }
            }
            CHECK(drain(queue) == expected);
        }
    }
}

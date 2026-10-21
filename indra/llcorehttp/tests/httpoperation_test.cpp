/**
 * @file httpoperation_test.cpp
 * @brief unit tests for the LLCore::HttpOperation-derived classes
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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
#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "_httpoperation.h"
#include "_httpreplyqueue.h"
#include "httphandler.h"

#include <iostream>


using namespace LLCore;
using namespace LLCoreInt;


namespace
{

class TestHandler : public LLCore::HttpHandler
{
public:
    virtual void onCompleted(HttpHandle, HttpResponse *)
        {
            std::cout << "TestHandler::onCompleted() invoked" << std::endl;
        }

};


}  // end namespace anonymous


namespace
{
    TEST_CASE("HttpOperation Tests: a new HttpOpNull has one reference", "[HttpOperation_Tests]")
    {
        // create a new ref counted object with an implicit reference
        HttpOperation::ptr_t op (new HttpOpNull());
        CHECK(op.use_count() == 1);

        // release the implicit reference, causing the object to be released
        op.reset();
    }

    TEST_CASE("HttpOperation Tests: setting a reply handler adds no reference to the op", "[HttpOperation_Tests]")
    {
        // Get some handlers
        LLCore::HttpHandler::ptr_t h1 (new TestHandler());

        // create a new ref counted object with an implicit reference
        HttpOperation::ptr_t op (new HttpOpNull());

        // Add the handlers
        op->setReplyPath(LLCore::HttpOperation::HttpReplyQueuePtr_t(), h1);

        // Check ref count
        CHECK(op.use_count() == 1);

        // release the reference, releasing the operation but
        // not the handlers.
        op.reset();

        // release the handlers
        h1.reset();
    }

    TEST_CASE("HttpOperation Tests: a reply lets go of the reply queue", "[HttpOperation_Tests]")
    {
        // The queue holds a reply until it is fetched. Were the op to keep holding the
        // queue, the two would keep each other alive once the request that made the
        // queue went with the reply unfetched, as an event poll's last cancel does.
        auto queue = std::make_shared<HttpReplyQueue>();
        std::weak_ptr<HttpReplyQueue> watch(queue);
        HttpOperation::ptr_t op(new HttpOpNull());
        op->setReplyPath(queue, LLCore::HttpHandler::ptr_t());
        CHECK(queue.use_count() == 2);

        op->stageFromRequest(nullptr);  // HttpOpNull bounces straight to its reply
        CHECK(queue.use_count() == 1);
        CHECK(op.use_count() == 2);     // its own and the queue's

        op.reset();
        queue.reset();
        CHECK(watch.expired());
    }

}

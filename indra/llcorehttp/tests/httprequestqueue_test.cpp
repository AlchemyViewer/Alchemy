/**
 * @file httprequestqueue_test.cpp
 * @brief unit tests for the LLCore::HttpRequestQueue class
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

#include "_httprequestqueue.h"

#include <iostream>

#include "_httpoperation.h"


using namespace LLCore;
using namespace LLCoreInt;



namespace
{
TEST_CASE("HttpRequestqueue Tests: init makes a queue holding one reference", "[HttpRequestqueue_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpRequestQueue::init();

    CHECK(HttpRequestQueue::instanceOf()->getRefCount() == 1);

    // release the implicit reference, causing the object to be released
    HttpRequestQueue::term();
}

TEST_CASE("HttpRequestqueue Tests: term drops only the reference init made", "[HttpRequestqueue_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpRequestQueue::init();

    HttpRequestQueue * rq = HttpRequestQueue::instanceOf();
    rq->addRef();

    // release the singleton, hold on to the object
    HttpRequestQueue::term();

    CHECK(rq->getRefCount() == 1);

    // Drop ref
    rq->release();
}

TEST_CASE("HttpRequestqueue Tests: fetchOp returns an added op once", "[HttpRequestqueue_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpRequestQueue::init();

    HttpRequestQueue * rq = HttpRequestQueue::instanceOf();

    HttpOperation::ptr_t op(new HttpOpNull());

    rq->addOp(op);      // transfer my refcount

    op = rq->fetchOp(true);     // Potentially hangs the test on failure
    CHECK(static_cast<bool>(op));
    op.reset();

    op = rq->fetchOp(false);
    CHECK_FALSE(op);

    // release the singleton, hold on to the object
    HttpRequestQueue::term();
}

TEST_CASE("HttpRequestqueue Tests: fetchAll returns every added op", "[HttpRequestqueue_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpRequestQueue::init();

    HttpRequestQueue * rq = HttpRequestQueue::instanceOf();

    HttpOperation::ptr_t op (new HttpOpNull());
    rq->addOp(op);      // transfer my refcount

    op.reset(new HttpOpNull());
    rq->addOp(op);      // transfer my refcount

    op.reset(new HttpOpNull());
    rq->addOp(op);      // transfer my refcount

    {
        HttpRequestQueue::OpContainer ops;
        rq->fetchAll(true, ops);        // Potentially hangs the test on failure
        CHECK(ops.size() == 3);

        op = rq->fetchOp(false);
        CHECK_FALSE(op);
        op.reset();

        // release the singleton, hold on to the object
        HttpRequestQueue::term();

        // Release them
        ops.clear();
//      while (! ops.empty())
//      {
//          HttpOperation * op = ops.front();
//          ops.erase(ops.begin());
//          op->release();
//      }
    }
}

}  // anonymous namespace

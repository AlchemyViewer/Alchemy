/**
 * @file refcounted_test.cpp
 * @brief unit tests for the LLCoreInt::RefCounted class
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

#include "_refcounted.h"

using namespace LLCoreInt;

namespace
{
    // A RefCounted the test may own: its destructor is public, so it can live on the stack
    // after noRef() has taken it out of reference counting
    struct OwnedRefCounted : public RefCounted
    {
        OwnedRefCounted()
            : RefCounted(false)
        {}

        ~OwnedRefCounted() override = default;
    };

    TEST_CASE("RefCounted Tests: construction with an implicit count holds one reference", "[RefCounted_Tests]")
    {
        // create a new ref counted object with an implicit reference
        RefCounted * rc = new RefCounted(true);
        CHECK(rc->getRefCount() == 1);

        // release the implicit reference, causing the object to be released
        rc->release();
    }

    TEST_CASE("RefCounted Tests: construction without an implicit count holds none", "[RefCounted_Tests]")
    {
        // create a new ref counted object without an implicit reference
        RefCounted * rc = new RefCounted(false);
        CHECK(rc->getRefCount() == 0);

        // add a reference
        rc->addRef();
        CHECK(rc->getRefCount() == 1);

        // release the added reference, causing the object to be released
        rc->release();
    }

    TEST_CASE("RefCounted Tests: addRef counts every reference", "[RefCounted_Tests]")
    {
        RefCounted * rc = new RefCounted(false);

        for (int i = 0; i < 1024; ++i)
        {
            rc->addRef();
        }

        CHECK(rc->getRefCount() == 1024);

        for (int i = 0; i < 1024; ++i)
        {
            rc->release();
        }
    }

    TEST_CASE("RefCounted Tests: isLastRef is true with one reference", "[RefCounted_Tests]")
    {
        RefCounted * rc = new RefCounted(true);

        // with only one reference, isLastRef should be true
        CHECK(rc->isLastRef());

        // release it to clean up memory
        rc->release();
    }

    TEST_CASE("RefCounted Tests: noRef marks the object not ref counted", "[RefCounted_Tests]")
    {
        // owned by the test, since an object out of reference counting cannot be released
        OwnedRefCounted rc;

        // set the noRef
        rc.noRef();

        // the count now says the object is not ref counted
        CHECK(rc.getRefCount() == RefCounted::NOT_REF_COUNTED);
    }
}

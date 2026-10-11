/**
 * @file llremoteparcelrequest_test.cpp
 * @author Brad Kittenbrink <brad@lindenlab.com>
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include "linden_common.h"

#include "../test/lltut.h"

#include "../llremoteparcelrequest.h"

#include "../llagent.h"

namespace {
    const LLUUID TEST_PARCEL_ID("11111111-1111-1111-1111-111111111111");

    // A reply for the test parcel, as processParcelInfoReply() reads one
    // off the wire.
    LLParcelData testParcelData()
    {
        LLParcelData parcel_data;
        parcel_data.parcel_id = TEST_PARCEL_ID;
        return parcel_data;
    }
}

LLAgent gAgent;
LLAgent::LLAgent() : mAgentAccess(NULL) { }
LLAgent::~LLAgent() { }
void LLAgent::sendReliableMessage(void) { }
LLUUID gAgentSessionID;
LLUUID gAgentID;

namespace tut
{
namespace
{
    struct TestObserver : public LLRemoteParcelInfoObserver {
        TestObserver() : mProcessed(false) { }

        virtual void processParcelInfo(const LLParcelData& parcel_data)
        {
            mProcessed = true;
        }

        virtual void setParcelID(const LLUUID& parcel_id) { }

        virtual void setErrorStatus(S32 status, const std::string& reason) { }

        bool mProcessed;
    };
} // anonymous namespace

    struct RemoteParcelRequestData
    {
        RemoteParcelRequestData()
        {
        }
    };

    typedef test_group<RemoteParcelRequestData> remoteparcelrequest_t;
    typedef remoteparcelrequest_t::object remoteparcelrequest_object_t;
    tut::remoteparcelrequest_t tut_remoteparcelrequest("LLRemoteParcelRequest");

    template<> template<>
    void remoteparcelrequest_object_t::test<1>()
    {
        set_test_name("observer pointer");

        std::unique_ptr<TestObserver> observer(new TestObserver());

        LLRemoteParcelInfoProcessor & processor = LLRemoteParcelInfoProcessor::instance();
        processor.addObserver(LLUUID(TEST_PARCEL_ID), observer.get());

        processor.processParcelData(testParcelData());

        ensure(observer->mProcessed);
    }

    template<> template<>
    void remoteparcelrequest_object_t::test<2>()
    {
        set_test_name("CHOP-220: dangling observer pointer");

        LLRemoteParcelInfoObserver * observer = new TestObserver();

        LLRemoteParcelInfoProcessor & processor = LLRemoteParcelInfoProcessor::instance();
        processor.addObserver(LLUUID(TEST_PARCEL_ID), observer);

        delete observer;
        observer = NULL;

        processor.processParcelData(testParcelData());
    }
}

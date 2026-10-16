/**
 * @file llapp_tut.cpp
 * @author Phoenix
 * @date 2006-09-12
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2006-2011, Linden Research, Inc.
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

#include <catch2/catch_test_macros.hpp>

#include "linden_common.h"
#include "llapp.h"

#include "alteststateguard.h"


namespace
{
    struct application
    {
        class LLTestApp : public LLApp
        {
        public:
            virtual bool init() { return true; }
            virtual bool cleanup() { return true; }
            virtual bool frame() { return true; }
        };
        LLTestApp* mApp;
        application()
        {
            mApp = new LLTestApp;
        }
        ~application()
        {
            delete mApp;
            // An LLApp ends LLCommon with it, the APR pool and LLTrace's
            // master recorder, and this binary is alone so that one can:
            // the next case starts from what this one leaves.
            ll_test::StateGuard::accept("the LLApp");
            ll_test::StateGuard::accept("the LLTrace master recorder");
            ll_test::StateGuard::accept("the APR pool");
        }
    };

    TEST_CASE_METHOD(application, "application: getOption returns the default option data", "[application]")
    {
        LLSD defaults;
        defaults["template"] = "../../../scripts/messages/message_template.msg";
        defaults["configdir"] = ".";
        defaults["datadir"] = "data";
        mApp->setOptionData(LLApp::PRIORITY_DEFAULT, defaults);

        LLSD datadir_sd = mApp->getOption("datadir");
        CHECK(datadir_sd.type() == LLSD::TypeString);
        CHECK(datadir_sd.asString() == std::string("data"));
    }

    TEST_CASE_METHOD(application, "application: a runtime override beats the general configuration", "[application]")
    {
        LLSD options;
        options["boolean-test"] = true;
        mApp->setOptionData(LLApp::PRIORITY_GENERAL_CONFIGURATION, options);
        CHECK(mApp->getOption("boolean-test").asBoolean());
        options["boolean-test"] = false;
        mApp->setOptionData(LLApp::PRIORITY_RUNTIME_OVERRIDE, options);
        CHECK_FALSE(mApp->getOption("boolean-test").asBoolean());
    }

    TEST_CASE_METHOD(application, "application: instance() finds no app once it is gone", "[application]")
    {
        CHECK(LLApp::instance() == mApp);
        delete mApp;
        mApp = nullptr;
        CHECK(LLApp::instance() == nullptr);
    }
}

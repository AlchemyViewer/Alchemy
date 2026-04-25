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

#include <tut/tut.hpp>

#include "linden_common.h"
#include "llapp.h"
#include "lltut.h"


namespace tut
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
        }
    };

    typedef test_group<application> application_t;
    typedef application_t::object application_object_t;
    tut::application_t tut_application("application");

    template<> template<>
    void application_object_t::test<1>()
    {
        LLSD defaults;
        defaults["template"] = "../../../scripts/messages/message_template.msg";
        defaults["configdir"] = ".";
        defaults["datadir"] = "data";
        mApp->setOptionData(LLApp::PRIORITY_DEFAULT, defaults);

        LLSD datadir_sd = mApp->getOption("datadir");
        ensure_equals("data type", datadir_sd.type(), LLSD::TypeString);
        ensure_equals(
            "data value", datadir_sd.asString(), std::string("data"));
    }

    template<> template<>
    void application_object_t::test<2>()
    {
        LLSD options;
        options["boolean-test"] = true;
        mApp->setOptionData(LLApp::PRIORITY_GENERAL_CONFIGURATION, options);
        ensure("bool set", mApp->getOption("boolean-test").asBoolean());
        options["boolean-test"] = false;
        mApp->setOptionData(LLApp::PRIORITY_RUNTIME_OVERRIDE, options);
        ensure("bool unset", !mApp->getOption("boolean-test").asBoolean());
    }
}

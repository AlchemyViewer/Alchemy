/**
 * @file llmessageconfig_tut.cpp
 * @date   March 2007
 * @brief LLMessageConfig unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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
#include "llmessageconfig.h"
#include <catch2/catch_test_macros.hpp>
#include "llsdserialize.h"
#include "llfile.h"
#include "lltimer.h"
#include "llframetimer.h"
#include "llsdutil.h"

namespace
{
    struct LLMessageConfigTestData {
        std::string mTestConfigDir;

        LLMessageConfigTestData()
        {
            LLUUID random;
            random.generate();
            // generate temp dir
            std::ostringstream oStr;
#if LL_WINDOWS
            oStr << "llmessage-config-test-" << random;
#else
            oStr << "/tmp/llmessage-config-test-" << random;
#endif
            mTestConfigDir = oStr.str();
            LLFile::mkdir(mTestConfigDir);
            writeConfigFile(LLSD());
            LLMessageConfig::initClass("simulator", mTestConfigDir);
        }

        // A destructor cannot fail a test, so the clean-up only says what
        // it could not remove.
        ~LLMessageConfigTestData()
        {
            // rm contents of temp dir
            if (LLFile::remove((mTestConfigDir + "/message.xml")) != 0)
            {
                LL_WARNS() << "Could not remove " << mTestConfigDir << "/message.xml" << LL_ENDL;
            }
            // rm temp dir
            if (LLFile::remove(mTestConfigDir) != 0)
            {
                LL_WARNS() << "Could not remove " << mTestConfigDir << LL_ENDL;
            }
        }

        void writeConfigFile(const LLSD& config)
        {
            llofstream file((mTestConfigDir + "/message.xml").c_str());
            if (file.is_open())
            {
                LLSDSerialize::toPrettyXML(config, file);
            }
            file.close();
        }
    };


    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: serverDefaults sets the server default flavor", "[LLMessageConfig]")
        // tests server defaults
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "template";
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure server default is not template");
        CHECK(LLMessageConfig::getServerDefaultFlavor() ==
                      LLMessageConfig::TEMPLATE_FLAVOR);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: each message takes the flavor its entry names", "[LLMessageConfig]")
        // tests message flavors
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "template";
        config["messages"]["msg1"]["flavor"] = "template";
        config["messages"]["msg2"]["flavor"] = "llsd";
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure msg template flavor");
        CHECK(LLMessageConfig::getMessageFlavor("msg1") ==
                      LLMessageConfig::TEMPLATE_FLAVOR);
        UNSCOPED_INFO("Ensure msg llsd flavor");
        CHECK(LLMessageConfig::getMessageFlavor("msg2") ==
                      LLMessageConfig::LLSD_FLAVOR);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: trusted-sender sets a message's sender trustedness", "[LLMessageConfig]")
        // tests trusted/untrusted senders
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "template";
        config["messages"]["msg1"]["flavor"] = "llsd";
        config["messages"]["msg1"]["trusted-sender"] = false;
        config["messages"]["msg2"]["flavor"] = "llsd";
        config["messages"]["msg2"]["trusted-sender"] = true;
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure untrusted is untrusted");
        CHECK(LLMessageConfig::getSenderTrustedness("msg1") ==
                      LLMessageConfig::UNTRUSTED);
        UNSCOPED_INFO("Ensure trusted is trusted");
        CHECK(LLMessageConfig::getSenderTrustedness("msg2") ==
                      LLMessageConfig::TRUSTED);
        UNSCOPED_INFO("Ensure missing trustedness is NOT_SET");
        CHECK(LLMessageConfig::getSenderTrustedness("msg3") ==
                      LLMessageConfig::NOT_SET);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: a message with no flavor entry has NO_FLAVOR", "[LLMessageConfig]")
        // tests message flavor defaults
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "llsd";
        config["messages"]["msg1"]["trusted-sender"] = true;
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure missing message gives no flavor");
        CHECK(LLMessageConfig::getMessageFlavor("Test") ==
                      LLMessageConfig::NO_FLAVOR);
        UNSCOPED_INFO("Ensure missing flavor is NO_FLAVOR even with sender trustedness set");
        CHECK(LLMessageConfig::getMessageFlavor("msg1") ==
                      LLMessageConfig::NO_FLAVOR);
        UNSCOPED_INFO("Ensure server default is llsd");
        CHECK(LLMessageConfig::getServerDefaultFlavor() ==
                      LLMessageConfig::LLSD_FLAVOR);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: a message with only a flavor has trustedness NOT_SET", "[LLMessageConfig]")
        // tests trusted/untrusted without flag, only flavor
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "template";
        config["messages"]["msg1"]["flavor"] = "llsd";
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure msg1 exists, has llsd flavor");
        CHECK(LLMessageConfig::getMessageFlavor("msg1") ==
                      LLMessageConfig::LLSD_FLAVOR);
        UNSCOPED_INFO("Ensure missing trusted is not set");
        CHECK(LLMessageConfig::getSenderTrustedness("msg1") ==
                      LLMessageConfig::NOT_SET);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: capBans says which caps are banned", "[LLMessageConfig]")
    {
        LLSD config;
        config["capBans"]["MapLayer"] = true;
        config["capBans"]["MapLayerGod"] = false;
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure cap ban true MapLayer");
        CHECK(LLMessageConfig::isCapBanned("MapLayer") ==
                      true);
        UNSCOPED_INFO("Ensure cap ban false");
        CHECK(LLMessageConfig::isCapBanned("MapLayerGod") ==
                      false);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: a changed config file is reloaded after a wait", "[LLMessageConfig]")
        // tests that config changes are picked up/refreshed periodically
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "llsd";
        writeConfigFile(config);

        // wait for it to reload after N seconds
        ms_sleep(6*1000);
        LLFrameTimer::updateFrameTime();
        UNSCOPED_INFO("Ensure reload after 6 seconds");
        CHECK(LLMessageConfig::getServerDefaultFlavor() ==
                      LLMessageConfig::LLSD_FLAVOR);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: only-send-latest sets onlySendLatest per message", "[LLMessageConfig]")
        // tests that config changes are picked up/refreshed periodically
    {
        LLSD config;
        config["serverDefaults"]["simulator"] = "template";
        config["messages"]["msg1"]["flavor"] = "llsd";
        config["messages"]["msg1"]["only-send-latest"] = true;
        config["messages"]["msg2"]["flavor"] = "llsd";
        config["messages"]["msg2"]["only-send-latest"] = false;
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure msg1 exists, sent latest-only");
        CHECK(LLMessageConfig::onlySendLatest("msg1") ==
                      true);
        UNSCOPED_INFO("Ensure msg2 exists, sent latest-only");
        CHECK(LLMessageConfig::onlySendLatest("msg2") ==
                      false);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: maxQueuedEvents is read from the config and defaults to 100", "[LLMessageConfig]")
         // tests that event queue max is reloaded
    {
        LLSD config;
        config["maxQueuedEvents"] = 200;
        LLMessageConfig::useConfig(config);
        UNSCOPED_INFO("Ensure setting maxQueuedEvents");
        CHECK(LLMessageConfig::getMaxQueuedEvents() ==
                      200);

        LLMessageConfig::useConfig(LLSD());
        UNSCOPED_INFO("Ensure default of event queue max 100");
        CHECK(LLMessageConfig::getMaxQueuedEvents() ==
                      100);
    }

    TEST_CASE_METHOD(LLMessageConfigTestData, "LLMessageConfig: initClass with another directory reads that directory's file", "[LLMessageConfig]")
        // initClass with another directory reads that directory's file
    {
        const std::string other_dir = mTestConfigDir + "-other";
        LLFile::mkdir(other_dir);
        LLSD config;
        config["serverDefaults"]["simulator"] = "llsd";
        {
            llofstream file((other_dir + "/message.xml").c_str());
            LLSDSerialize::toPrettyXML(config, file);
        }

        // The fixture named its own directory, whose file holds nothing.
        LLMessageConfig::initClass("simulator", other_dir);
        const LLMessageConfig::Flavor flavor = LLMessageConfig::getServerDefaultFlavor();
        LLFile::remove(other_dir + "/message.xml");
        LLFile::remove(other_dir);
        UNSCOPED_INFO("the config of the directory named last");
        CHECK(flavor == LLMessageConfig::LLSD_FLAVOR);
    }
}

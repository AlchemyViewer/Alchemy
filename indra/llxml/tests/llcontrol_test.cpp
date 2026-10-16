/**
 * @file llcontrol_test.cpp
 * @date   February 2008
 * @brief control group unit tests
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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
#include "llsdserialize.h"
#include "llfile.h"
#include "stringize.h"

#include "../llcontrol.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace
{
    struct control_group
    {
        std::unique_ptr<LLControlGroup> mCG;
        std::string mTestConfigDir;
        std::string mTestConfigFile;
        std::vector<std::string> mCleanups;
        bool mListenerFired = false;
        control_group()
        {
            mCG.reset(new LLControlGroup("foo"));
            LLUUID random;
            random.generate();
            // generate temp dir
            mTestConfigDir = STRINGIZE(LLFile::tmpdir() << "llcontrol-test-" << random << "/");
            mTestConfigFile = mTestConfigDir + "settings.xml";
            LLFile::mkdir(mTestConfigDir);
            LLSD config;
            config["TestSetting"]["Comment"] = "Dummy setting used for testing";
            config["TestSetting"]["Persist"] = 1;
            config["TestSetting"]["Type"] = "U32";
            config["TestSetting"]["Value"] = 12;
            writeSettingsFile(config);
        }
        ~control_group()
        {
            //Remove test files
            for (auto filename : mCleanups)
            {
                LLFile::remove(filename);
            }
            LLFile::remove(mTestConfigFile);
            LLFile::remove(mTestConfigDir);
        }
        void writeSettingsFile(const LLSD& config)
        {
            llofstream file(mTestConfigFile.c_str());
            if (file.is_open())
            {
                LLSDSerialize::toPrettyXML(config, file);
            }
            file.close();
        }
    };

    //load settings from files - LLSD
    TEST_CASE_METHOD(control_group, "control_group: loadFromFile reads the settings in an LLSD file", "[control_group]")
    {
        int results = mCG->loadFromFile(mTestConfigFile.c_str());
        UNSCOPED_INFO("number of settings");
        CHECK(results == 1);
        CHECK(mCG->getU32("TestSetting") == 12);
    }

    //save settings to files
    TEST_CASE_METHOD(control_group, "control_group: saveToFile writes a changed setting another group loads", "[control_group]")
    {
        int results = mCG->loadFromFile(mTestConfigFile.c_str());
        mCG->setU32("TestSetting", 13);
        CHECK(mCG->getU32("TestSetting") == 13);
        LLControlGroup test_cg("foo2");
        std::string temp_test_file = (mTestConfigDir + "setting_llsd_temp.xml");
        mCleanups.push_back(temp_test_file);
        mCG->saveToFile(temp_test_file.c_str(), true);
        results = test_cg.loadFromFile(temp_test_file.c_str());
        UNSCOPED_INFO("number of changed settings loaded");
        CHECK(results == 1);
        CHECK(test_cg.getU32("TestSetting") == 13);
    }

    //priorities
    TEST_CASE_METHOD(control_group, "control_group: a value set without saving it is left out of the saved file", "[control_group]")
    {
        // Pass default_values = true. This tells loadFromFile() we're loading
        // a default settings file that declares variables, rather than a user
        // settings file. When loadFromFile() encounters an unrecognized user
        // settings variable, it forcibly preserves it (CHOP-962).
        int results = mCG->loadFromFile(mTestConfigFile.c_str(), true);
        LLControlVariable* control = mCG->getControl("TestSetting");
        LLSD new_value = 13;
        control->setValue(new_value, false);
        CHECK(mCG->getU32("TestSetting") == 13);
        LLControlGroup test_cg("foo3");
        std::string temp_test_file = (mTestConfigDir + "setting_llsd_persist_temp.xml");
        mCleanups.push_back(temp_test_file);
        mCG->saveToFile(temp_test_file.c_str(), true);
        results = test_cg.loadFromFile(temp_test_file.c_str());
        //If we haven't changed any settings, then we shouldn't have any settings to load
        UNSCOPED_INFO("number of non-persisted changed settings loaded");
        CHECK(results == 0);
    }

    //listeners
    TEST_CASE_METHOD(control_group, "control_group: changing a setting fires its listener", "[control_group]")
    {
        int results = mCG->loadFromFile(mTestConfigFile.c_str());
        UNSCOPED_INFO("number of settings");
        REQUIRE(results == 1);
        mCG->getControl("TestSetting")->getSignal()->connect(
            [this](LLControlVariable*, const LLSD&, const LLSD&) { mListenerFired = true; });
        mCG->setU32("TestSetting", 13);
        CHECK(mListenerFired);
    }


    // An LLSD-typed setting compares by value, and numbers compare as
    // numbers: an integer-typed list equal to the real-typed default is at
    // default, and writing it changes nothing and fires nothing.
    TEST_CASE_METHOD(control_group, "control_group: an LLSD setting compares by value and its numbers as numbers", "[control_group]")
    {
        auto pair_of = [](const LLSD& x, const LLSD& y) {
            LLSD p = LLSD::emptyArray();
            p.append(x);
            p.append(y);
            return p;
        };
        LLSD real_default = LLSD::emptyArray();
        real_default.append(pair_of(LLSD::Real(0.0), LLSD::Real(0.0)));
        real_default.append(pair_of(LLSD::Real(1.0), LLSD::Real(1.0)));
        mCG->declareLLSD("CurveSetting", real_default, "Dummy LLSD setting", LLControlVariable::PERSIST_NONDFT);
        LLControlVariable* control = mCG->getControl("CurveSetting");
        UNSCOPED_INFO("declared at default");
        CHECK(control->isDefault());

        bool fired = false;
        control->getSignal()->connect([&fired](LLControlVariable*, const LLSD&, const LLSD&) { fired = true; });

        LLSD same = LLSD::emptyArray();
        same.append(pair_of(LLSD::Real(0.0), LLSD::Real(0.0)));
        same.append(pair_of(LLSD::Real(1.0), LLSD::Real(1.0)));
        control->setValue(same, true);
        UNSCOPED_INFO("an equal value does not fire");
        CHECK_FALSE(fired);
        UNSCOPED_INFO("and leaves the control at default");
        CHECK(control->isDefault());

        LLSD integers = LLSD::emptyArray();
        integers.append(pair_of(LLSD::Integer(0), LLSD::Integer(0)));
        integers.append(pair_of(LLSD::Integer(1), LLSD::Integer(1)));
        control->setValue(integers, true);
        UNSCOPED_INFO("an integer-typed equal value does not fire");
        CHECK_FALSE(fired);
        UNSCOPED_INFO("and is still at default");
        CHECK(control->isDefault());

        LLSD other = LLSD::emptyArray();
        other.append(pair_of(LLSD::Real(0.0), LLSD::Real(0.0)));
        other.append(pair_of(LLSD::Real(0.5), LLSD::Real(0.25)));
        other.append(pair_of(LLSD::Real(1.0), LLSD::Real(1.0)));
        control->setValue(other, true);
        UNSCOPED_INFO("a different value fires");
        CHECK(fired);
        UNSCOPED_INFO("and is no longer default");
        CHECK_FALSE(control->isDefault());

        control->resetToDefault(true);
        UNSCOPED_INFO("reset restores the default");
        CHECK(control->isDefault());
    }
}

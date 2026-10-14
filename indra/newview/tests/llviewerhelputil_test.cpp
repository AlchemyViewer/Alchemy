/**
 * @file llviewerhelputil_test.cpp
 * @brief LLViewerHelpUtil tests
 * @author Tofu Linden
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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
// Precompiled header
#include "../llviewerprecompiledheaders.h"

#include <catch2/catch_test_macros.hpp>

#include "../llviewerhelputil.h"
#include "../llweb.h"
#include "llcontrol.h"

#include <iostream>

// values for all of the supported substitutions parameters
static std::string gHelpURL;
static std::string gVersion;
static std::string gChannel;
static std::string gLanguage;
static std::string gGrid;
static std::string gOS;

//----------------------------------------------------------------------------
// Mock objects for the dependencies of the code we're testing

LLControlGroup::LLControlGroup(const std::string& name)
    : LLInstanceTracker<LLControlGroup, std::string>(name) {}
LLControlGroup::~LLControlGroup() {}
LLControlVariable* LLControlGroup::declareString(const std::string& name,
                   const std::string& initial_val,
                   const std::string& comment,
                   LLControlVariable::ePersist persist) {return NULL;}
void LLControlGroup::setString(std::string_view name, const std::string& val){}
std::string LLControlGroup::getString(std::string_view name)
{
    if (name == "HelpURLFormat")
        return gHelpURL;
    return "";
}
LLControlGroup gSavedSettings("llviewerhelputil");

static void substitute_string(std::string &input, const std::string &search, const std::string &replace)
{
    size_t pos = input.find(search);
    while (pos != std::string::npos)
    {
        input = input.replace(pos, search.size(), replace);
        pos = input.find(search);
    }
}

#include "../llagent.h"
LLAgent::LLAgent() : mAgentAccess(NULL) { }
LLAgent::~LLAgent() { }
bool LLAgent::isGodlike() const { return false; }

LLAgent gAgent;

std::string LLWeb::expandURLSubstitutions(const std::string &url,
                                          const LLSD &default_subs)
{
    (void)gAgent.isGodlike(); // ref symbol to stop compiler from stripping it
    std::string new_url = url;
    substitute_string(new_url, "[TOPIC]", default_subs["TOPIC"].asString());
    substitute_string(new_url, "[VERSION]", gVersion);
    substitute_string(new_url, "[CHANNEL]", gChannel);
    substitute_string(new_url, "[LANGUAGE]", gLanguage);
    substitute_string(new_url, "[GRID]", gGrid);
    substitute_string(new_url, "[OS]", gOS);
    return new_url;
}


//----------------------------------------------------------------------------

namespace
{
    // What buildHelpURL does itself: the format read from HelpURLFormat, and
    // the topic made safe for a URL and handed to the expansion as [TOPIC].
    // The viewer's own tags ([LANGUAGE], [VERSION], [OS] and the rest) are
    // LLWeb::expandURLSubstitutions's, which this test stubs, so they are not
    // tested here.
    TEST_CASE("LLViewerHelpUtil: buildHelpURL expands the format from the settings with the topic encoded", "[LLViewerHelpUtil]")
    {
        std::string topic("test_topic");
        std::string subresult;

        gHelpURL = "fooformat";
        subresult = LLViewerHelpUtil::buildHelpURL(topic);
        UNSCOPED_INFO("no substitution tags");
        CHECK(subresult == "fooformat");

        gHelpURL = "";
        subresult = LLViewerHelpUtil::buildHelpURL(topic);
        UNSCOPED_INFO("blank substitution format");
        CHECK(subresult == "");

        gHelpURL = "[TOPIC]";
        subresult = LLViewerHelpUtil::buildHelpURL(topic);
        UNSCOPED_INFO("topic name");
        CHECK(subresult == "test_topic");

        gHelpURL = "http://help/[TOPIC]";
        subresult = LLViewerHelpUtil::buildHelpURL("two words/a~b");
        UNSCOPED_INFO("all but letters, digits, '-', '.' and '_' escaped, the tilde too");
        CHECK(subresult == "http://help/two%20words%2Fa%7Eb");
    }
}

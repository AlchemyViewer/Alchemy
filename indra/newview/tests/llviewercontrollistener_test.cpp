/**
 * @file   llviewercontrollistener_test.cpp
 * @author Nat Goodspeed
 * @date   2022-06-09
 * @brief  Test for llviewercontrollistener.
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Copyright (c) 2022, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "../llviewerprecompiledheaders.h"
// associated header
#include "../llviewercontrollistener.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "altestmatchers.h"
#include "../test/catch_and_store_what_in.h" // catch_what()
#include "commoncontrol.h"
#include "llcontrol.h"              // LLControlGroup

#include <algorithm>

/*****************************************************************************
*   TUT
*****************************************************************************/
namespace
{
    void ensure_contains(const std::string& msg, const std::string& substr)
    {
        UNSCOPED_INFO("Exception does not contain " + substr);
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring(substr));
    }

    struct llviewercontrollistener_data
    {
        LLControlGroup Global{"FakeGlobal"};

        llviewercontrollistener_data()
        {
            Global.declareString("strvar", "woof", "string variable");
            // together we will stroll the boolvar, ma cherie
            Global.declareBOOL("boolvar",  true, "bool variable");
        }
    };

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl with no listener is left untested", "[llviewercontrollistener]")
    {
        // Not implemented: the linker drags in LLViewerControlListener when
        // we bring in LLViewerControl.
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get of an unknown group names the group", "[llviewercontrollistener]")
    {
        std::string threw{ catch_what<LL::CommonControl::ParamError>(
                [](){ LL::CommonControl::get("Nonexistent", "Variable"); }) };
        ensure_contains(threw, "group");
        ensure_contains(threw, "Nonexistent");
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get of an unknown variable names the key", "[llviewercontrollistener]")
    {
        std::string threw{ catch_what<LL::CommonControl::ParamError>(
                [](){ LL::CommonControl::get("FakeGlobal", "Nonexistent"); }) };
        ensure_contains(threw, "key");
        ensure_contains(threw, "Nonexistent");
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::toggle of a string variable says it is non-boolean", "[llviewercontrollistener]")
    {
        std::string threw{ catch_what<LL::CommonControl::ParamError>(
                [](){ LL::CommonControl::toggle("FakeGlobal", "strvar"); }) };
        ensure_contains(threw, "non-boolean");
        ensure_contains(threw, "strvar");
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get_vars of an unknown group names the group", "[llviewercontrollistener]")
    {
        std::string threw{ catch_what<LL::CommonControl::ParamError>(
                [](){ LL::CommonControl::get_vars("Nonexistent"); }) };
        ensure_contains(threw, "group");
        ensure_contains(threw, "Nonexistent");
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get returns each variable's value", "[llviewercontrollistener]")
    {
        auto strvar{ LL::CommonControl::get("FakeGlobal", "strvar") };
        CHECK(strvar == "woof");
        auto boolvar{ LL::CommonControl::get("FakeGlobal", "boolvar") };
        REQUIRE(boolvar);
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl set and set_default and toggle return the new value", "[llviewercontrollistener]")
    {
        std::string newstr{ LL::CommonControl::set("FakeGlobal", "strvar", "mouse").asString() };
        CHECK(newstr == "mouse");
        CHECK(LL::CommonControl::get("FakeGlobal", "strvar").asString() == "mouse");
        CHECK(LL::CommonControl::set_default("FakeGlobal", "strvar").asString() == "woof");

        bool newbool{ LL::CommonControl::set("FakeGlobal", "boolvar", false) };
        CHECK_FALSE(newbool);
        CHECK_FALSE(LL::CommonControl::get("FakeGlobal", "boolvar").asBoolean());
        CHECK(LL::CommonControl::set_default("FakeGlobal", "boolvar").asBoolean());
        CHECK_FALSE(LL::CommonControl::toggle("FakeGlobal", "boolvar").asBoolean());
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get_def describes a variable", "[llviewercontrollistener]")
    {
        LLSD def{ LL::CommonControl::get_def("FakeGlobal", "strvar") };
        CHECK_THAT(def,
            ll_test::LLSDEquals(llsd::map("name", "strvar",
                      "type", "String",
                      "value", "woof",
                      "comment", "string variable")));
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get_groups lists each group once while it lives", "[llviewercontrollistener]")
    {
        // Every group the process has: the fixture's, and whatever else has
        // been made -- HeadlessUI's, another test's gSavedSettings. So the
        // fixture's is there once, and one more group made adds its name.
        const std::vector<std::string> groups{ LL::CommonControl::get_groups() };
        UNSCOPED_INFO("FakeGlobal");
        CHECK(std::count(groups.begin(), groups.end(), "FakeGlobal") == std::ptrdiff_t(1));
        {
            LLControlGroup another{ "FakeAnother" };
            const std::vector<std::string> more{ LL::CommonControl::get_groups() };
            UNSCOPED_INFO("one more");
            REQUIRE(more.size() == groups.size() + 1);
            UNSCOPED_INFO("FakeAnother");
            CHECK(std::count(more.begin(), more.end(), "FakeAnother") == std::ptrdiff_t(1));
        }
        UNSCOPED_INFO("one fewer once it goes");
        REQUIRE(LL::CommonControl::get_groups().size() == groups.size());
    }

    TEST_CASE_METHOD(llviewercontrollistener_data, "llviewercontrollistener: CommonControl::get_vars describes every variable of a group", "[llviewercontrollistener]")
    {
        LLSD vars{ LL::CommonControl::get_vars("FakeGlobal") };
        // convert from array (unpredictable order) to map
        LLSD varsmap{ LLSD::emptyMap() };
        for (auto& var : llsd::inArray(vars))
        {
            varsmap[var["name"].asString()] = var;
        }
        // comparing maps is order-insensitive
        CHECK_THAT(varsmap,
            ll_test::LLSDEquals(llsd::map(
                "strvar",
                llsd::map("name", "strvar",
                          "type", "String",
                          "value", "woof",
                          "comment", "string variable"),
                "boolvar",
                llsd::map("name", "boolvar",
                          "type", "Boolean",
                          "value", true,
                          "comment", "bool variable"))));
    }
} // anonymous namespace

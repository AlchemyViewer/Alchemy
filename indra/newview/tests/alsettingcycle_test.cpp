/**
 * @file alsettingcycle_test.cpp
 * @brief Tests for flipping and cycling a debug setting from chat.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../alsettingcycle.h"

#include "llcontrol.h"
#include "llrect.h"
#include "v3math.h"
#include "v4color.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
using ALSettingCycle::Result;

LLControlVariablePtr declared(LLControlGroup& group, const char* name, eControlType type, const LLSD& value)
{
    group.declareControl(name, type, value, "test", LLControlVariable::PERSIST_NO);
    return group.getControl(name);
}

TEST_CASE("alsettingcycle: a boolean is flipped and its values are ignored", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_bool");
    LLControlVariablePtr flag = declared(group, "Flag", TYPE_BOOLEAN, false);
    REQUIRE(ALSettingCycle::apply(*flag, {}) == Result::Written);
    CHECK(group.getBOOL("Flag"));
    REQUIRE(ALSettingCycle::apply(*flag, { "nonsense" }) == Result::Written);
    CHECK_FALSE(group.getBOOL("Flag"));
}

TEST_CASE("alsettingcycle: an integer moves to the next value and wraps to the first", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_int");
    LLControlVariablePtr value = declared(group, "Count", TYPE_S32, 0);
    const std::vector<std::string> values = { "1", "2", "3" };
    // Not one of them: the first.
    for (const S32 expected : { 1, 2, 3, 1 })
    {
        REQUIRE(ALSettingCycle::apply(*value, values) == Result::Written);
        CHECK(group.getS32("Count") == expected);
    }
}

TEST_CASE("alsettingcycle: a float matches its stored single precision value", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_float");
    LLControlVariablePtr value = declared(group, "Scale", TYPE_F32, 0.5);
    REQUIRE(ALSettingCycle::apply(*value, { "0.5", "0.7" }) == Result::Written);
    CHECK(group.getF32("Scale") == 0.7f);
    REQUIRE(ALSettingCycle::apply(*value, { "0.5", "0.7" }) == Result::Written);
    CHECK(group.getF32("Scale") == 0.5f);
}

TEST_CASE("alsettingcycle: vectors colors and rects are one comma separated value", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_parts");
    LLControlVariablePtr vector = declared(group, "Where", TYPE_VEC3, LLVector3(0, 0, 0).getValue());
    LLControlVariablePtr color = declared(group, "Tint", TYPE_COL4, LLColor4(0, 0, 0, 1).getValue());
    LLControlVariablePtr rect = declared(group, "Box", TYPE_RECT, LLRect(0, 0, 0, 0).getValue());

    REQUIRE(ALSettingCycle::apply(*vector, { "0,0,0", "1,2,3" }) == Result::Written);
    CHECK(group.getVector3("Where") == LLVector3(1, 2, 3));
    REQUIRE(ALSettingCycle::apply(*vector, { "0,0,0", "1,2,3" }) == Result::Written);
    CHECK(group.getVector3("Where") == LLVector3(0, 0, 0));

    REQUIRE(ALSettingCycle::apply(*color, { "1,0,0,1", "0,1,0,0.5" }) == Result::Written);
    CHECK(group.getColor4("Tint") == LLColor4(1, 0, 0, 1));
    REQUIRE(ALSettingCycle::apply(*color, { "1,0,0,1", "0,1,0,0.5" }) == Result::Written);
    CHECK(group.getColor4("Tint") == LLColor4(0, 1, 0, 0.5f));

    REQUIRE(ALSettingCycle::apply(*rect, { "0,10,10,0", "5,20,20,5" }) == Result::Written);
    CHECK(group.getRect("Box") == LLRect(0, 10, 10, 0));
}

TEST_CASE("alsettingcycle: a string cycles through the words given", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_string");
    LLControlVariablePtr word = declared(group, "Mode", TYPE_STRING, "b");
    REQUIRE(ALSettingCycle::apply(*word, { "a", "b", "c" }) == Result::Written);
    CHECK(group.getString("Mode") == "c");
    REQUIRE(ALSettingCycle::apply(*word, { "a", "b", "c" }) == Result::Written);
    CHECK(group.getString("Mode") == "a");
}

TEST_CASE("alsettingcycle: a value that is not whole and of the type writes nothing", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_bad");
    LLControlVariablePtr integer = declared(group, "Count", TYPE_S32, 7);
    LLControlVariablePtr unsigned_value = declared(group, "Size", TYPE_U32, 7);
    LLControlVariablePtr vector = declared(group, "Where", TYPE_VEC3, LLVector3(1, 1, 1).getValue());

    for (const char* bad : { "12abc", "1.5", "nan", "inf", "0x10", "", "1e", "--1", "2147483648", "1,2" })
    {
        CAPTURE(bad);
        CHECK(ALSettingCycle::apply(*integer, { "1", bad }) == Result::BadValue);
        CHECK(group.getS32("Count") == 7);
    }
    CHECK(ALSettingCycle::apply(*unsigned_value, { "1", "-1" }) == Result::BadValue);
    CHECK(group.getU32("Size") == 7);
    for (const char* bad : { "0,0,", "0,0", "0,,0", "0 0 0", "0,0,0,0", "a,b,c", "," })
    {
        CAPTURE(bad);
        CHECK(ALSettingCycle::apply(*vector, { "1,2,3", bad }) == Result::BadValue);
        CHECK(group.getVector3("Where") == LLVector3(1, 1, 1));
    }
}

TEST_CASE("alsettingcycle: too few values and LLSD settings are refused", "[alsettingcycle]")
{
    LLControlGroup group("alsettingcycle_refused");
    LLControlVariablePtr integer = declared(group, "Count", TYPE_S32, 7);
    LLControlVariablePtr any = declared(group, "Anything", TYPE_LLSD, LLSD());

    CHECK(ALSettingCycle::apply(*integer, {}) == Result::TooFewValues);
    CHECK(ALSettingCycle::apply(*integer, { "1" }) == Result::TooFewValues);
    CHECK(group.getS32("Count") == 7);
    CHECK(ALSettingCycle::apply(*any, { "1", "2" }) == Result::Unsupported);
}

TEST_CASE("alsettingcycle: mapto takes x|y or x|y|z after the region name", "[alsettingcycle]")
{
    std::string region;
    F32 x = 0.f, y = 0.f, z = 0.f;
    bool has_z = true;

    REQUIRE(ALSettingCycle::parseMapToCoordinates("Cool Region 128|64", region, x, y, has_z, z));
    CHECK(region == "Cool Region");
    CHECK(x == 128.f);
    CHECK(y == 64.f);
    CHECK_FALSE(has_z);

    REQUIRE(ALSettingCycle::parseMapToCoordinates("Cool Region  128|64|30  ", region, x, y, has_z, z));
    CHECK(region == "Cool Region");
    CHECK(z == 30.f);
    CHECK(has_z);

    for (const char* bad : { "128|64", "Region 128", "Region 128|", "Region 1|2|3|4", "Region a|b", "Region |1" })
    {
        CAPTURE(bad);
        CHECK_FALSE(ALSettingCycle::parseMapToCoordinates(bad, region, x, y, has_z, z));
    }
}
} // anonymous namespace

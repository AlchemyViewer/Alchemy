/**
 * @file albulkrename_test.cpp
 * @brief Tests for the naming rule of the inventory's bulk rename.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Wolfgang <wolfgang@wlf.io>
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

#include "../albulkrename.h"
#include <catch2/catch_test_macros.hpp>

#include <ctime>

namespace
{
ALBulkRename add(const std::string& text, ALBulkRename::Position position)
{
    ALBulkRename rule;
    rule.mode     = ALBulkRename::Mode::Add;
    rule.position = position;
    rule.text     = text;
    return rule;
}

ALBulkRename replace(const std::string& text, const std::string& with)
{
    ALBulkRename rule;
    rule.mode = ALBulkRename::Mode::Replace;
    rule.text = text;
    rule.with = with;
    return rule;
}

ALBulkRename format_counter(const std::string& text, ALBulkRename::Position position, int start)
{
    ALBulkRename rule;
    rule.mode          = ALBulkRename::Mode::Format;
    rule.format_kind   = ALBulkRename::FormatKind::Count;
    rule.position      = position;
    rule.text          = text;
    rule.counter_start = start;
    return rule;
}

ALBulkRename format_index(const std::string& text, ALBulkRename::Position position, int start)
{
    ALBulkRename rule = format_counter(text, position, start);
    rule.format_kind  = ALBulkRename::FormatKind::Index;
    return rule;
}

ALBulkRename format_date(ALBulkRename::FormatKind kind, const std::string& text, ALBulkRename::Position position,
                         const std::string& date_format)
{
    ALBulkRename rule;
    rule.mode        = ALBulkRename::Mode::Format;
    rule.format_kind = kind;
    rule.position    = position;
    rule.text        = text;
    rule.date_format = date_format;
    return rule;
}

ALBulkRename format_acquired(const std::string& text, ALBulkRename::Position position, const std::string& date_format)
{
    return format_date(ALBulkRename::FormatKind::AcquiredDate, text, position, date_format);
}

ALBulkRename format_current(const std::string& text, ALBulkRename::Position position, const std::string& date_format)
{
    return format_date(ALBulkRename::FormatKind::CurrentDate, text, position, date_format);
}

ALBulkRename format_custom(const std::string& text, int start = 1)
{
    ALBulkRename rule;
    rule.mode          = ALBulkRename::Mode::Format;
    rule.format_kind   = ALBulkRename::FormatKind::Custom;
    rule.text          = text;
    rule.counter_start = start;
    return rule;
}

std::string local_stamp(std::time_t when, const char* format)
{
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char buffer[64] = {};
    REQUIRE(std::strftime(buffer, sizeof(buffer), format, &local) != 0);
    return buffer;
}

TEST_CASE("albulkrename: add puts the text before or after the name", "[albulkrename]")
{
    CHECK(add("New ", ALBulkRename::Position::Before).apply("Hat") == "New Hat");
    CHECK(add(" (old)", ALBulkRename::Position::After).apply("Hat") == "Hat (old)");
    UNSCOPED_INFO("nothing to add leaves the name");
    CHECK(add("", ALBulkRename::Position::Before).apply("Hat") == "Hat");
    CHECK(add("", ALBulkRename::Position::After).apply("Hat") == "Hat");
}

TEST_CASE("albulkrename: replace swaps every occurrence, matching case", "[albulkrename]")
{
    CHECK(replace("Red", "Blue").apply("Red Hat Red") == "Blue Hat Blue");
    CHECK(replace("red", "Blue").apply("Red Hat") == "Red Hat");
    CHECK(replace(" ", "").apply("A B C") == "ABC");
    UNSCOPED_INFO("a replacement holding the text is not replaced again");
    CHECK(replace("a", "aa").apply("banana") == "baanaanaa");
    UNSCOPED_INFO("occurrences do not overlap");
    CHECK(replace("aa", "b").apply("aaa") == "ba");
}

TEST_CASE("albulkrename: replacing nothing or text absent leaves the name", "[albulkrename]")
{
    CHECK(replace("", "x").apply("Hat") == "Hat");
    CHECK(replace("Shoe", "Boot").apply("Hat") == "Hat");
    CHECK(replace("Hat", "x").apply("") == "");
}

TEST_CASE("albulkrename: format counter replaces the name with text and a number", "[albulkrename]")
{
    const ALBulkRename after = format_counter("Shot", ALBulkRename::Position::After, 1);
    CHECK(after.apply("ignored", 0) == "Shot1");
    CHECK(after.apply("ignored", 1) == "Shot2");

    const ALBulkRename before = format_counter("Shot", ALBulkRename::Position::Before, 1);
    CHECK(before.apply("ignored", 0) == "1Shot");

    UNSCOPED_INFO("an empty format is only the counter");
    const ALBulkRename bare = format_counter("", ALBulkRename::Position::After, 1);
    CHECK(bare.apply("ignored", 0) == "1");
    CHECK(bare.apply("ignored", 1) == "2");
}

TEST_CASE("albulkrename: format counter zero-pads to the width of the batch", "[albulkrename]")
{
    const ALBulkRename rule = format_counter("Shot", ALBulkRename::Position::After, 1);
    CHECK(rule.apply("ignored", 0, 10) == "Shot01");
    CHECK(rule.apply("ignored", 8, 10) == "Shot09");
    CHECK(rule.apply("ignored", 9, 10) == "Shot10");

    UNSCOPED_INFO("a start of zero still pads up to the last counter");
    const ALBulkRename from_zero = format_counter("", ALBulkRename::Position::Before, 0);
    CHECK(from_zero.apply("ignored", 0, 100) == "00");
    CHECK(from_zero.apply("ignored", 99, 100) == "99");

    UNSCOPED_INFO("the width follows the last number when the batch crosses a power of ten");
    const ALBulkRename crossing = format_counter("n", ALBulkRename::Position::Before, 98);
    CHECK(crossing.apply("ignored", 0, 5) == "098n");
    CHECK(crossing.apply("ignored", 4, 5) == "102n");
}

TEST_CASE("albulkrename: format counter with no batch to pad against", "[albulkrename]")
{
    const ALBulkRename rule = format_counter("", ALBulkRename::Position::After, 1);
    CHECK(rule.apply("ignored", 0, 1) == "1");
    UNSCOPED_INFO("a batch size of zero or less is taken as one");
    CHECK(rule.apply("ignored", 0, 0) == "1");
    CHECK(rule.apply("ignored", 0, -5) == "1");
    CHECK(rule.apply("ignored", 11, 0) == "12");
}

TEST_CASE("albulkrename: format index numbers without padding", "[albulkrename]")
{
    const ALBulkRename rule = format_index("Shot", ALBulkRename::Position::After, 1);
    CHECK(rule.apply("ignored", 0, 10) == "Shot1");
    CHECK(rule.apply("ignored", 9, 10) == "Shot10");

    const ALBulkRename before = format_index("Shot", ALBulkRename::Position::Before, 0);
    CHECK(before.apply("ignored", 0, 100) == "0Shot");
}

TEST_CASE("albulkrename: format acquired date replaces the name with text and a local stamp", "[albulkrename]")
{
    // 2024-06-15 12:00:00 UTC. Local formatting still yields a non-empty date
    // string that contains 2024 for ordinary timezones.
    const std::time_t when = 1718452800;
    const ALBulkRename after = format_acquired("Shot", ALBulkRename::Position::After, "%Y-%m-%d");
    const std::string  name  = after.apply("ignored", 0, 1, when);
    CHECK(name.rfind("Shot", 0) == 0);
    CHECK(name.find("2024") != std::string::npos);

    const ALBulkRename before  = format_acquired("Shot", ALBulkRename::Position::Before, "%Y%m%d");
    const std::string  stamped = before.apply("ignored", 0, 1, when);
    CHECK(stamped.find("2024") == 0);
    CHECK(stamped.size() == std::string("20240615Shot").size());
    CHECK(stamped.compare(stamped.size() - 4, 4, "Shot") == 0);
}

TEST_CASE("albulkrename: format acquired date without a date leaves the name", "[albulkrename]")
{
    const ALBulkRename rule = format_acquired("Shot", ALBulkRename::Position::After, "%Y-%m-%d");
    CHECK(rule.apply("Hat", 0, 1, 0) == "Hat");
}

TEST_CASE("albulkrename: format current date stamps now, not the acquired date", "[albulkrename]")
{
    const std::string year = local_stamp(std::time(nullptr), "%Y");

    const ALBulkRename rule = format_current("Shot", ALBulkRename::Position::After, "%Y");
    UNSCOPED_INFO("an acquired date passed in is ignored");
    CHECK(rule.apply("ignored", 0, 1, 1718452800) == "Shot" + year);
    CHECK(rule.apply("ignored", 0, 1, 0) == "Shot" + year);
}

TEST_CASE("albulkrename: format custom substitutes name and counters", "[albulkrename]")
{
    const ALBulkRename rule = format_custom("$n_$i_$c", 1);
    CHECK(rule.apply("Hat", 0, 10) == "Hat_1_01");
    CHECK(rule.apply("Hat", 9, 10) == "Hat_10_10");

    UNSCOPED_INFO("every occurrence of a token is replaced");
    CHECK(format_custom("$n-$n").apply("Hat") == "Hat-Hat");

    UNSCOPED_INFO("a start other than one shifts both $i and $c");
    CHECK(format_custom("$i/$c", 5).apply("Hat", 0, 10) == "5/05");
}

TEST_CASE("albulkrename: format custom substitutes acquired date and time", "[albulkrename]")
{
    // 2024-06-15 12:00:00 UTC; local $a/$t still match formatTime's local conversion.
    const std::time_t when = 1718452800;
    const ALBulkRename rule = format_custom("$a $t");
    CHECK(rule.apply("Hat", 0, 1, when) == local_stamp(when, "%Y-%m-%d") + " " + local_stamp(when, "%H:%M:%S"));
}

TEST_CASE("albulkrename: format custom runs remaining strftime codes on the current time", "[albulkrename]")
{
    const std::string year = local_stamp(std::time(nullptr), "%Y");
    const ALBulkRename rule = format_custom("$n_%Y");
    CHECK(rule.apply("Hat", 0, 1, 1718452800) == "Hat_" + year);
}

TEST_CASE("albulkrename: format custom with no text leaves the name", "[albulkrename]")
{
    CHECK(format_custom("").apply("Hat", 0, 10) == "Hat");
}
} // anonymous namespace

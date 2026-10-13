/**
 * @file alsavehistory_test.cpp
 * @brief Tests for ALSaveHistory: saves kept, listed, let go of and moved.
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

#include "../alsavehistory.h"

#include "alfilewrite.h"
#include "fsyspath.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    struct alsavehistory_data
    {
        // A folder of its own for each test, gone after it.
        std::string folder;
        // A day, in seconds; and a moment well past the epoch to count from.
        static constexpr F64 DAY  = 24.0 * 60.0 * 60.0;
        static constexpr F64 THEN = 1.8e9;

        alsavehistory_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alsavehistory_" + LLUUID::generateNewID().asString())).string();
        }

        ~alsavehistory_data()
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        static ALSavedText saved(const std::string& key, const std::string& text, F64 when)
        {
            ALSavedText one;
            one.key        = key;
            one.text       = text;
            one.when       = LLDate(when);
            one.name       = "Door";
            one.objectName = "House";
            one.region     = "Ahern";
            one.asset      = LLUUID::generateNewID(text);
            return one;
        }

        // A key's texts, newest first, each read whole.
        static std::string require_texts(const ALSaveHistory& history, const std::string& key)
        {
            CAPTURE(key);
            std::string out;
            for (ALSavedText one : history.list(key))
            {
                REQUIRE(history.load(one));
                out += (out.empty() ? "" : "|") + one.text;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: saves list newest first per key and the text is read on asking", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        // Within the age: dated from now.
        const F64 now = LLDate::now().secondsSinceEpoch();
        CHECK(history.keep(saved("item:a", "one", now - 60.0)));
        CHECK(history.keep(saved("item:a", "two", now - 30.0)));
        CHECK(history.keep(saved("item:b", "other", now - 45.0)));
        const std::vector<ALSavedText> listed = history.list("item:a");
        REQUIRE(listed.size() == 2U);
        CHECK(listed[0].when.secondsSinceEpoch() > listed[1].when.secondsSinceEpoch());
        {
            INFO("what a listing reads");
            CHECK(listed[0].name == "Door");
            CHECK(listed[0].objectName == "House");
            CHECK(listed[0].region == "Ahern");
            CHECK(listed[0].asset == LLUUID::generateNewID("two"));
            CHECK(listed[0].bytes == 3);
        }
        {
            INFO("and not the text");
            CHECK_FALSE(listed[0].whole);
            REQUIRE(listed[0].text.empty());
        }
        UNSCOPED_INFO("read on asking");
        CHECK(require_texts(history, "item:a") == std::string("two|one"));
        CHECK(require_texts(history, "item:b") == std::string("other"));
        UNSCOPED_INFO("none for a key never saved");
        REQUIRE(history.list("item:c").empty());
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: the text kept last is not kept again but a change back is", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        CHECK(history.keep(saved("item:a", "one", now - 60.0)));
        CHECK_FALSE(history.keep(saved("item:a", "one", now - 50.0)));
        CHECK(history.keep(saved("item:a", "two", now - 40.0)));
        UNSCOPED_INFO("back to one is a change");
        CHECK(history.keep(saved("item:a", "one", now - 30.0)));
        CHECK(require_texts(history, "item:a") == std::string("one|two|one"));
        UNSCOPED_INFO("no key, nothing");
        CHECK_FALSE(history.keep(saved("", "x", now)));
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: the oldest go past so many for a key or past the age at a prune", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        history.limit(3, ALSaveHistory::MAX_AGE, ALSaveHistory::MAX_BYTES);
        const F64 now = LLDate::now().secondsSinceEpoch();
        for (int i = 1; i <= 5; ++i)
        {
            history.keep(saved("item:a", std::to_string(i), now - 100.0 + i));
        }
        CHECK(require_texts(history, "item:a") == std::string("5|4|3"));

        ALSaveHistory aged(folder + "/aged");
        aged.limit(50, 10.0 * DAY, ALSaveHistory::MAX_BYTES);
        aged.keep(saved("item:a", "old", THEN));
        aged.keep(saved("item:a", "new", THEN + 8.0 * DAY));
        aged.prune(LLDate(THEN + 9.0 * DAY));
        UNSCOPED_INFO("both within the age");
        CHECK(require_texts(aged, "item:a") == std::string("new|old"));
        aged.prune(LLDate(THEN + 12.0 * DAY));
        UNSCOPED_INFO("the old one past it");
        CHECK(require_texts(aged, "item:a") == std::string("new"));
        aged.prune(LLDate(THEN + 30.0 * DAY));
        UNSCOPED_INFO("and then the other");
        REQUIRE(aged.list("item:a").empty());
        UNSCOPED_INFO("its folder gone with it");
        CHECK(std::filesystem::is_empty(fsyspath(folder + "/aged")));
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: past so much in all the oldest of any key go first", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("item:a", "aaaaaaaaaa", now - 40.0));
        history.keep(saved("item:b", "bbbbbbbbbb", now - 30.0));
        history.keep(saved("item:a", "cccccccccc", now - 20.0));
        const auto size = [](const ALSavedText& one) { return static_cast<size_t>(std::filesystem::file_size(fsyspath(one.path))); };
        // Room for the two newest, and not the third.
        const size_t room = size(history.list("item:a").front()) + size(history.list("item:b").front());
        history.limit(50, 1e12, room);
        history.prune();
        UNSCOPED_INFO("the oldest gone");
        CHECK(require_texts(history, "item:a") == std::string("cccccccccc"));
        UNSCOPED_INFO("another key's newer one kept");
        CHECK(require_texts(history, "item:b") == std::string("bbbbbbbbbb"));
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: rekey moves a key's saves behind another's under the new key", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("task:p:old", "one", now - 60.0));
        history.keep(saved("task:p:old", "two", now - 50.0));
        history.keep(saved("task:p:new", "three", now - 40.0));
        CHECK(history.rekey("task:p:old", "task:p:new"));
        REQUIRE(history.list("task:p:old").empty());
        UNSCOPED_INFO("all under the new, in time's order");
        CHECK(require_texts(history, "task:p:new") == std::string("three|two|one"));
        for (const ALSavedText& one : history.list("task:p:new"))
        {
            REQUIRE(one.key == std::string("task:p:new"));
        }
        UNSCOPED_INFO("nothing to move");
        CHECK_FALSE(history.rekey("task:p:gone", "task:p:new"));
        UNSCOPED_INFO("not to itself");
        CHECK_FALSE(history.rekey("task:p:new", "task:p:new"));
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: an unreadable file is passed over and a half write is swept", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("item:a", "one", now - 60.0));
        const std::string kept = history.list("item:a").front().path;
        const std::string dir  = kept.substr(0, kept.find_last_of('/') + 1);
        {
            llofstream broken(dir + "1.llsd");
            broken << "not llsd at all";
        }
        {
            llofstream half(dir + "2.llsd" + std::string(ALFileWrite::BESIDE));
            half << "{'key':'item:a'";
        }
        UNSCOPED_INFO("the one readable");
        REQUIRE(history.list("item:a").size() == 1U);
        history.prune();
        UNSCOPED_INFO("the half-written swept");
        CHECK_FALSE(LLFile::isfile(dir + "2.llsd" + std::string(ALFileWrite::BESIDE)));
        UNSCOPED_INFO("the save still there");
        CHECK(require_texts(history, "item:a") == std::string("one"));
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: count counts a key's saves by file name and not half writes", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        UNSCOPED_INFO("none for a key never saved");
        REQUIRE(history.count("item:a") == 0U);
        REQUIRE(history.count("") == 0U);
        history.keep(saved("item:a", "one", now - 60.0));
        history.keep(saved("item:a", "two", now - 50.0));
        history.keep(saved("item:b", "other", now - 40.0));
        REQUIRE(history.count("item:a") == history.list("item:a").size());
        REQUIRE(history.count("item:a") == 2U);
        REQUIRE(history.count("item:b") == 1U);
        const std::string kept = history.list("item:a").front().path;
        const std::string dir  = kept.substr(0, kept.find_last_of('/') + 1);
        {
            llofstream half(dir + "3.llsd" + std::string(ALFileWrite::BESIDE));
            half << "{'key':'item:a'";
        }
        UNSCOPED_INFO("what a write cut short is none");
        REQUIRE(history.count("item:a") == 2U);
    }

    TEST_CASE_METHOD(alsavehistory_data, "ALSaveHistory: holds finds a text by its asset or its text among a key's newest", "[ALSaveHistory]")
    {
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("item:a", "one", now - 60.0));
        history.keep(saved("item:a", "two", now - 50.0));
        history.keep(saved("item:a", "three", now - 40.0));
        history.keep(saved("item:b", "other", now - 30.0));
        const LLUUID elsewhere = LLUUID::generateNewID("elsewhere");
        UNSCOPED_INFO("by the asset its save made, whatever the text");
        CHECK(history.holds("item:a", LLUUID::generateNewID("two"), "not it", 10));
        // The same text sent again made another asset, and was not kept
        // again: found by the text.
        UNSCOPED_INFO("by the text, under an asset no save made");
        CHECK(history.holds("item:a", elsewhere, "one", 10));
        UNSCOPED_INFO("a text of the same length that is not one");
        CHECK_FALSE(history.holds("item:a", elsewhere, "owe", 10));
        UNSCOPED_INFO("nor one never kept");
        CHECK_FALSE(history.holds("item:a", elsewhere, "four", 10));
        UNSCOPED_INFO("nor without an asset");
        CHECK_FALSE(history.holds("item:a", LLUUID::null, "four", 10));
        // Among the newest two only: "one" is the third.
        UNSCOPED_INFO("the newest");
        CHECK(history.holds("item:a", elsewhere, "three", 2));
        UNSCOPED_INFO("past the newest, by the text");
        CHECK_FALSE(history.holds("item:a", elsewhere, "one", 2));
        UNSCOPED_INFO("past the newest, by the asset");
        CHECK_FALSE(history.holds("item:a", LLUUID::generateNewID("one"), "x", 2));
        UNSCOPED_INFO("another key's is not this one's");
        CHECK_FALSE(history.holds("item:a", LLUUID::generateNewID("other"), "other", 10));
        UNSCOPED_INFO("nothing for a key never saved");
        CHECK_FALSE(history.holds("item:c", LLUUID::generateNewID("one"), "one", 10));
        UNSCOPED_INFO("nor for no key");
        CHECK_FALSE(history.holds("", LLUUID::generateNewID("one"), "one", 10));
    }
}

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

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

namespace tut
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
        static std::string texts(const ALSaveHistory& history, const std::string& key)
        {
            std::string out;
            for (ALSavedText one : history.list(key))
            {
                ensure("read whole", history.load(one));
                out += (out.empty() ? "" : "|") + one.text;
            }
            return out;
        }
    };

    typedef test_group<alsavehistory_data> alsavehistory_group;
    typedef alsavehistory_group::object    alsavehistory_object;
    tut::alsavehistory_group               alsavehistory_test("ALSaveHistory");

    template<> template<>
    void alsavehistory_object::test<1>()
    {
        set_test_name("kept, and listed newest first as far as a listing reads; the text read on asking; each key its own");
        ALSaveHistory history(folder);
        // Within the age: dated from now.
        const F64 now = LLDate::now().secondsSinceEpoch();
        ensure("kept", history.keep(saved("item:a", "one", now - 60.0)));
        ensure("kept", history.keep(saved("item:a", "two", now - 30.0)));
        ensure("another's", history.keep(saved("item:b", "other", now - 45.0)));
        const std::vector<ALSavedText> listed = history.list("item:a");
        ensure_equals("two of its own", listed.size(), 2U);
        ensure("newest first", listed[0].when.secondsSinceEpoch() > listed[1].when.secondsSinceEpoch());
        ensure("what a listing reads", listed[0].name == "Door" && listed[0].objectName == "House" && listed[0].region == "Ahern" &&
                                           listed[0].asset == LLUUID::generateNewID("two") && listed[0].bytes == 3);
        ensure("and not the text", !listed[0].whole && listed[0].text.empty());
        ensure_equals("read on asking", texts(history, "item:a"), std::string("two|one"));
        ensure_equals("the other key's", texts(history, "item:b"), std::string("other"));
        ensure("none for a key never saved", history.list("item:c").empty());
    }

    template<> template<>
    void alsavehistory_object::test<2>()
    {
        set_test_name("the text kept last is not kept again; one kept before that is, since it is a change back");
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        ensure("kept", history.keep(saved("item:a", "one", now - 60.0)));
        ensure("not again", !history.keep(saved("item:a", "one", now - 50.0)));
        ensure("kept", history.keep(saved("item:a", "two", now - 40.0)));
        ensure("back to one is a change", history.keep(saved("item:a", "one", now - 30.0)));
        ensure_equals("three", texts(history, "item:a"), std::string("one|two|one"));
        ensure("no key, nothing", !history.keep(saved("", "x", now)));
    }

    template<> template<>
    void alsavehistory_object::test<3>()
    {
        set_test_name("past so many for a key, the oldest go as the next is kept; past the age, as it is pruned");
        ALSaveHistory history(folder);
        history.limit(3, ALSaveHistory::MAX_AGE, ALSaveHistory::MAX_BYTES);
        const F64 now = LLDate::now().secondsSinceEpoch();
        for (int i = 1; i <= 5; ++i)
        {
            history.keep(saved("item:a", std::to_string(i), now - 100.0 + i));
        }
        ensure_equals("the newest three", texts(history, "item:a"), std::string("5|4|3"));

        ALSaveHistory aged(folder + "/aged");
        aged.limit(50, 10.0 * DAY, ALSaveHistory::MAX_BYTES);
        aged.keep(saved("item:a", "old", THEN));
        aged.keep(saved("item:a", "new", THEN + 8.0 * DAY));
        aged.prune(LLDate(THEN + 9.0 * DAY));
        ensure_equals("both within the age", texts(aged, "item:a"), std::string("new|old"));
        aged.prune(LLDate(THEN + 12.0 * DAY));
        ensure_equals("the old one past it", texts(aged, "item:a"), std::string("new"));
        aged.prune(LLDate(THEN + 30.0 * DAY));
        ensure("and then the other", aged.list("item:a").empty());
        ensure("its folder gone with it", std::filesystem::is_empty(fsyspath(folder + "/aged")));
    }

    template<> template<>
    void alsavehistory_object::test<4>()
    {
        set_test_name("past so much in all, the oldest of any key go first");
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
        ensure_equals("the oldest gone", texts(history, "item:a"), std::string("cccccccccc"));
        ensure_equals("another key's newer one kept", texts(history, "item:b"), std::string("bbbbbbbbbb"));
    }

    template<> template<>
    void alsavehistory_object::test<5>()
    {
        set_test_name("a key's saves moved to another, behind what that has, saying the new key; the old one empty");
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("task:p:old", "one", now - 60.0));
        history.keep(saved("task:p:old", "two", now - 50.0));
        history.keep(saved("task:p:new", "three", now - 40.0));
        ensure("moved", history.rekey("task:p:old", "task:p:new"));
        ensure("none left", history.list("task:p:old").empty());
        ensure_equals("all under the new, in time's order", texts(history, "task:p:new"), std::string("three|two|one"));
        for (const ALSavedText& one : history.list("task:p:new"))
        {
            ensure_equals("saying the new key", one.key, std::string("task:p:new"));
        }
        ensure("nothing to move", !history.rekey("task:p:gone", "task:p:new"));
        ensure("not to itself", !history.rekey("task:p:new", "task:p:new"));
    }

    template<> template<>
    void alsavehistory_object::test<6>()
    {
        set_test_name("a file that cannot be read is passed over, and what a write cut short is swept");
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
        ensure_equals("the one readable", history.list("item:a").size(), 1U);
        history.prune();
        ensure("the half-written swept", !LLFile::isfile(dir + "2.llsd" + std::string(ALFileWrite::BESIDE)));
        ensure_equals("the save still there", texts(history, "item:a"), std::string("one"));
    }

    template<> template<>
    void alsavehistory_object::test<7>()
    {
        set_test_name("a key's saves counted by their files' names, each key its own; what is no save of the name not counted");
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        ensure_equals("none for a key never saved", history.count("item:a"), 0U);
        ensure_equals("none for no key", history.count(""), 0U);
        history.keep(saved("item:a", "one", now - 60.0));
        history.keep(saved("item:a", "two", now - 50.0));
        history.keep(saved("item:b", "other", now - 40.0));
        ensure_equals("as many as listed", history.count("item:a"), history.list("item:a").size());
        ensure_equals("two", history.count("item:a"), 2U);
        ensure_equals("the other key's", history.count("item:b"), 1U);
        const std::string kept = history.list("item:a").front().path;
        const std::string dir  = kept.substr(0, kept.find_last_of('/') + 1);
        {
            llofstream half(dir + "3.llsd" + std::string(ALFileWrite::BESIDE));
            half << "{'key':'item:a'";
        }
        ensure_equals("what a write cut short is none", history.count("item:a"), 2U);
    }

    template<> template<>
    void alsavehistory_object::test<8>()
    {
        set_test_name("a text kept already, by the asset its save made or by the text, among a key's newest; not past them, nor another key's");
        ALSaveHistory history(folder);
        const F64     now = LLDate::now().secondsSinceEpoch();
        history.keep(saved("item:a", "one", now - 60.0));
        history.keep(saved("item:a", "two", now - 50.0));
        history.keep(saved("item:a", "three", now - 40.0));
        history.keep(saved("item:b", "other", now - 30.0));
        const LLUUID elsewhere = LLUUID::generateNewID("elsewhere");
        ensure("by the asset its save made, whatever the text", history.holds("item:a", LLUUID::generateNewID("two"), "not it", 10));
        // The same text sent again made another asset, and was not kept
        // again: found by the text.
        ensure("by the text, under an asset no save made", history.holds("item:a", elsewhere, "one", 10));
        ensure("a text of the same length that is not one", !history.holds("item:a", elsewhere, "owe", 10));
        ensure("nor one never kept", !history.holds("item:a", elsewhere, "four", 10));
        ensure("nor without an asset", !history.holds("item:a", LLUUID::null, "four", 10));
        // Among the newest two only: "one" is the third.
        ensure("the newest", history.holds("item:a", elsewhere, "three", 2));
        ensure("past the newest, by the text", !history.holds("item:a", elsewhere, "one", 2));
        ensure("past the newest, by the asset", !history.holds("item:a", LLUUID::generateNewID("one"), "x", 2));
        ensure("another key's is not this one's", !history.holds("item:a", LLUUID::generateNewID("other"), "other", 10));
        ensure("nothing for a key never saved", !history.holds("item:c", LLUUID::generateNewID("one"), "one", 10));
        ensure("nor for no key", !history.holds("", LLUUID::generateNewID("one"), "one", 10));
    }
}

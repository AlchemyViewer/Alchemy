/**
 * @file alrecoverystore_test.cpp
 * @brief Script Studio's store of unsaved work: written whole, one session's apart from another's, discarded and pruned.
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

#include "../alrecoverystore.h"

#include "fsyspath.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    struct alrecoverystore_data
    {
        // A folder of its own for each test, gone after it.
        std::string folder;

        alrecoverystore_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alrecoverystore_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(folder), ec);
        }

        ~alrecoverystore_data()
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        // The files a folder holds, by name.
        std::vector<std::string> files(const std::string& under = std::string()) const
        {
            std::vector<std::string> names;
            std::error_code          ec;
            for (std::filesystem::directory_iterator it(fsyspath(folder + under), ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_regular_file())
                {
                    names.push_back(fsyspath(it->path().filename()).string());
                }
            }
            return names;
        }

        // Entries as a listing reads them, read whole: what they say of
        // their text is only there then.
        static std::vector<ALRecoveryEntry> whole(const ALRecoveryStore& store, std::vector<ALRecoveryEntry> entries)
        {
            for (ALRecoveryEntry& one : entries)
            {
                UNSCOPED_INFO("read whole: " + one.key);
                REQUIRE(store.load(one));
            }
            return entries;
        }
        static std::optional<ALRecoveryEntry> whole(const ALRecoveryStore& store, std::optional<ALRecoveryEntry> one)
        {
            if (one)
            {
                UNSCOPED_INFO("read whole: " + one->key);
                REQUIRE(store.load(*one));
            }
            return one;
        }

        static ALRecoveryEntry entry(const std::string& key, const std::string& text)
        {
            ALRecoveryEntry one;
            one.key  = key;
            one.name = "My Script";
            one.text = text;
            return one;
        }
    };

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: keyOf tells an object's item from the inventory's and a file's", "[alrecoverystore]")
    {
        const LLUUID object = LLUUID::generateNewID();
        const LLUUID item   = LLUUID::generateNewID();
        const std::string task      = ALRecoveryStore::keyOf(object, item, std::string());
        const std::string inventory = ALRecoveryStore::keyOf(LLUUID::null, item, std::string());
        const std::string file      = ALRecoveryStore::keyOf(LLUUID::null, LLUUID::null, "/scripts/lib.lsl");
        UNSCOPED_INFO("an object's item and the inventory's differ");
        CHECK(task != inventory);
        UNSCOPED_INFO("a file is its path");
        CHECK(file == "disk:/scripts/lib.lsl");
        UNSCOPED_INFO("and a key is the same asked twice");
        CHECK(ALRecoveryStore::keyOf(object, item, std::string()) == task);
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: an entry reads back as it was written", "[alrecoverystore]")
    {
        ALRecoveryEntry was  = entry("task:a:b", "default\n{\n    state_entry() { llSay(0, \"<&>\"); }\n}\n");
        was.session                = "one";
        was.state                  = ALRecoveryEntry::State::Kept;
        was.when                   = LLDate(1700000000.0);
        was.object                 = LLUUID::generateNewID();
        was.item                   = LLUUID::generateNewID();
        was.objectName             = "Door";
        was.region                 = "Ahern";
        was.lua                    = true;
        was.notecard               = false;
        was.wrapped                = true;
        was.compileTarget          = "luau";
        was.baseAsset              = LLUUID::generateNewID();
        ALRecoveryEntry back;
        UNSCOPED_INFO("it reads");
        REQUIRE(ALRecoveryEntry::fromLLSD(was.asLLSD(), back));
        UNSCOPED_INFO("the text, whatever is in it");
        CHECK(back.text == was.text);
        UNSCOPED_INFO("kept");
        CHECK(back.state == ALRecoveryEntry::State::Kept);
        UNSCOPED_INFO("when");
        CHECK(back.when.secondsSinceEpoch() == was.when.secondsSinceEpoch());
        {
            INFO("the object and the item");
            CHECK(back.object == was.object);
            CHECK(back.item == was.item);
        }
        {
            INFO("where it was");
            CHECK(back.objectName == "Door");
            CHECK(back.region == "Ahern");
        }
        {
            INFO("what it is");
            CHECK(back.lua);
            CHECK_FALSE(back.notecard);
            CHECK(back.wrapped);
            CHECK(back.compileTarget == "luau");
            CHECK(back.baseAsset == was.baseAsset);
        }
        {
            INFO("no history said, none read");
            REQUIRE_FALSE(back.history.isDefined());
            CHECK(back.caretLine == -1);
        }
        LLSD history;
        history["undo"]  = LLSD::emptyArray().with(0, LLSD().with("label", "rename"));
        history["saved"] = 0;
        was.history      = history;
        was.caretLine    = 4;
        was.caretColumn  = 7;
        UNSCOPED_INFO("it reads again");
        CHECK(ALRecoveryEntry::fromLLSD(was.asLLSD(), back));
        {
            INFO("the history as it was written");
            CHECK(back.history["undo"][0]["label"].asString() == "rename");
            CHECK(back.history["saved"].asInteger() == 0);
        }
        {
            INFO("and where the caret stood");
            CHECK(back.caretLine == 4);
            CHECK(back.caretColumn == 7);
        }
        UNSCOPED_INFO("something that is no entry is not read as one");
        CHECK_FALSE(ALRecoveryEntry::fromLLSD(LLSD("text"), back));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a session's entry is written whole then over by its next and forgotten", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "session-a");
        UNSCOPED_INFO("written");
        CHECK(store.write(entry("item:x", "first")));
        UNSCOPED_INFO("written again");
        CHECK(store.write(entry("item:x", "second")));
        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("one entry for one key");
        REQUIRE(all.size() == size_t(1));
        UNSCOPED_INFO("the last text");
        CHECK(all.front().text == std::string("second"));
        UNSCOPED_INFO("this session's");
        CHECK(all.front().session == std::string("session-a"));
        UNSCOPED_INFO("unsaved");
        CHECK(all.front().state == ALRecoveryEntry::State::Unsaved);
        for (const std::string& name : files())
        {
            UNSCOPED_INFO("nothing half written left beside it: " + name);
            REQUIRE(name.find(".tmp") == std::string::npos);
        }
        UNSCOPED_INFO("this session's own is nothing to offer");
        CHECK_FALSE(store.hasOffers());
        UNSCOPED_INFO("nor left by another");
        REQUIRE(store.left().empty());
        store.forget("item:x");
        UNSCOPED_INFO("forgotten");
        REQUIRE(store.list().empty());
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: typing in a script another session left keeps what it left", "[alrecoverystore]")
    {
        ALRecoveryStore before(folder, "before");
        ALRecoveryStore now(folder, "now");
        UNSCOPED_INFO("the crashed session's");
        CHECK(before.write(entry("task:o:i", "left behind")));
        UNSCOPED_INFO("this session's");
        CHECK(now.write(entry("task:o:i", "typed since")));
        UNSCOPED_INFO("both kept");
        REQUIRE(now.list().size() == size_t(2));
        const std::vector<ALRecoveryEntry> left = whole(now, now.left());
        UNSCOPED_INFO("one left by another session");
        REQUIRE(left.size() == size_t(1));
        UNSCOPED_INFO("its text");
        CHECK(left.front().text == std::string("left behind"));
        const std::optional<ALRecoveryEntry> found = whole(now, now.leftFor("task:o:i"));
        {
            INFO("found by its key");
            REQUIRE(found.has_value());
            CHECK(found->text == "left behind");
        }
        UNSCOPED_INFO("nothing for another key");
        REQUIRE_FALSE(now.leftFor("task:o:j").has_value());
        UNSCOPED_INFO("something to offer");
        CHECK(now.hasOffers());
        now.forget("task:o:i");
        UNSCOPED_INFO("this session forgetting its own leaves the other's");
        REQUIRE(now.leftFor("task:o:i").has_value());
        now.remove(*found);
        {
            INFO("taken up, it is gone");
            REQUIRE_FALSE(now.leftFor("task:o:i").has_value());
            CHECK_FALSE(now.hasOffers());
        }
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a discarded entry is pruned once old but an unsaved one never is", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("written");
        CHECK(store.write(entry("item:gone", "thrown away")));
        UNSCOPED_INFO("and another session's");
        CHECK(other.write(entry("item:kept", "never thrown away")));
        {
            const std::vector<ALRecoveryEntry> written = store.list();
            const auto mine = std::find_if(written.begin(), written.end(), [](const ALRecoveryEntry& one) { return one.key == "item:gone"; });
            {
                INFO("discarded");
                REQUIRE(mine != written.end());
                CHECK(store.discard(*mine));
            }
        }
        std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("both still listed");
        REQUIRE(all.size() == size_t(2));
        const auto discarded = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.key == "item:gone"; });
        {
            INFO("the discarded one");
            REQUIRE(discarded != all.end());
            CHECK(discarded->state == ALRecoveryEntry::State::Discarded);
        }
        UNSCOPED_INFO("with its text");
        CHECK(discarded->text == std::string("thrown away"));
        UNSCOPED_INFO("in the folder beside");
        REQUIRE(files("/discarded").size() == size_t(1));
        UNSCOPED_INFO("not among what was left unsaved");
        CHECK(store.leftFor("item:gone") == std::nullopt);
        UNSCOPED_INFO("but offered");
        CHECK(store.hasOffers());
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("young, it stays");
        REQUIRE(files("/discarded").size() == size_t(1));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        UNSCOPED_INFO("old, it goes");
        REQUIRE(files("/discarded").empty());
        UNSCOPED_INFO("and the unsaved one does not");
        REQUIRE(store.leftFor("item:kept").has_value());
        // Another session's, discarded from the list of what was left.
        const std::optional<ALRecoveryEntry> theirs = store.leftFor("item:kept");
        UNSCOPED_INFO("discarded too");
        CHECK(store.discard(*theirs));
        UNSCOPED_INFO("gone from what was left");
        REQUIRE_FALSE(store.leftFor("item:kept").has_value());
        UNSCOPED_INFO("into the folder beside");
        REQUIRE(files("/discarded").size() == size_t(1));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a file that cannot be read is passed over and left in place", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        UNSCOPED_INFO("written");
        CHECK(store.write(entry("item:ok", "fine")));
        {
            LLFILE* broken = LLFile::fopen(folder + "/0000.someone.llsd", LLFILE_MODE("wb"));
            UNSCOPED_INFO("made");
            REQUIRE(broken != nullptr);
            fputs("<llsd><map><key>text</key><string>half", broken);
            fclose(broken);
        }
        const std::vector<ALRecoveryEntry> all = store.list();
        UNSCOPED_INFO("the one that reads");
        REQUIRE(all.size() == size_t(1));
        UNSCOPED_INFO("is the good one");
        CHECK(all.front().key == std::string("item:ok"));
        UNSCOPED_INFO("the other is still there");
        CHECK(LLFile::isfile(folder + "/0000.someone.llsd"));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a notecard keeps its items in order with its text through the disk", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryEntry card = entry("item:card", std::string("Look: ") + "\xF4\x80\x80\x80" + " and " + "\xF4\x80\x80\x81");
        card.notecard              = true;
        LLSD first;
        first["item_id"] = LLUUID::generateNewID();
        first["name"]    = "Sunset";
        LLSD second;
        second["item_id"] = LLUUID::generateNewID();
        second["name"]    = "Home";
        card.embedded     = LLSD::emptyArray().with(0, first).with(1, second);
        UNSCOPED_INFO("written");
        CHECK(store.write(card));
        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("one");
        REQUIRE(all.size() == size_t(1));
        UNSCOPED_INFO("a notecard");
        CHECK(all.front().notecard);
        UNSCOPED_INFO("its text, the placeholders in it byte for byte");
        CHECK(all.front().text == card.text);
        UNSCOPED_INFO("both items");
        REQUIRE(all.front().embedded.size() == 2);
        {
            INFO("in the order the text stands them");
            CHECK(all.front().embedded[0]["name"].asString() == "Sunset");
            CHECK(all.front().embedded[1]["name"].asString() == "Home");
        }
        UNSCOPED_INFO("each whole");
        CHECK(all.front().embedded[1]["item_id"].asUUID() == second["item_id"].asUUID());
        // Discarded, it keeps them too.
        UNSCOPED_INFO("discarded");
        CHECK(store.discard(all.front()));
        const std::vector<ALRecoveryEntry> thrown = whole(store, store.list());
        {
            INFO("the items go with it");
            REQUIRE(thrown.size() == 1);
            REQUIRE(thrown.front().embedded.size() == 2);
        }
        // A script has none, and says so as an empty list.
        ALRecoveryEntry script;
        UNSCOPED_INFO("read");
        REQUIRE(ALRecoveryEntry::fromLLSD(entry("item:s", "x").asLLSD(), script));
        {
            INFO("no items");
            REQUIRE(script.embedded.isArray());
            REQUIRE(script.embedded.size() == 0);
        }
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: setAside leaves its source; prune sweeps halves; an XML entry reads", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("another session's");
        CHECK(other.write(entry("item:a", "left")));
        UNSCOPED_INFO("set aside");
        CHECK(store.setAside(entry("item:a", "thrown")));
        UNSCOPED_INFO("among the discarded");
        REQUIRE(files("/discarded").size() == size_t(1));
        const std::optional<ALRecoveryEntry> left = whole(store, store.leftFor("item:a"));
        {
            INFO("what it came from still left");
            REQUIRE(left.has_value());
            CHECK(left->text == "left");
        }
        UNSCOPED_INFO("and only that is left");
        REQUIRE(store.left().size() == size_t(1));
        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        const auto thrown = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "thrown"; });
        {
            INFO("set aside by this session");
            REQUIRE(thrown != all.end());
            REQUIRE(thrown->session == "mine");
            CHECK(thrown->state == ALRecoveryEntry::State::Discarded);
        }

        // Written half by a session that went, and by this one.
        for (const char* name : { "/0001.theirs.llsd.tmp", "/0002.mine.llsd.tmp" })
        {
            LLFILE* half = LLFile::fopen(folder + name, LLFILE_MODE("wb"));
            UNSCOPED_INFO("made");
            REQUIRE(half != nullptr);
            fputs("<? llsd/notation ?>\n{'key':'ite", half);
            fclose(half);
        }
        store.prune(60.0);
        UNSCOPED_INFO("the gone session's half gone");
        CHECK_FALSE(LLFile::isfile(folder + "/0001.theirs.llsd.tmp"));
        UNSCOPED_INFO("this session's own left");
        CHECK(LLFile::isfile(folder + "/0002.mine.llsd.tmp"));
        UNSCOPED_INFO("the young discard stays");
        REQUIRE(files("/discarded").size() == size_t(1));

        // An entry as the XML it was once written as.
        {
            ALRecoveryEntry was = entry("item:old", "from before");
            was.session               = "older";
            LLFILE* xml = LLFile::fopen(folder + "/0003.older.llsd", LLFILE_MODE("wb"));
            UNSCOPED_INFO("made");
            REQUIRE(xml != nullptr);
            std::ostringstream text;
            LLSDSerialize::toPrettyXML(was.asLLSD(), text);
            fputs(text.str().c_str(), xml);
            fclose(xml);
        }
        const std::vector<ALRecoveryEntry> now = whole(store, store.left());
        UNSCOPED_INFO("read");
        CHECK(std::any_of(now.begin(), now.end(), [](const ALRecoveryEntry& one) { return one.text == "from before"; }));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: letGo of a changed tab sets its text aside and drops what it took up", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("left by another session");
        CHECK(other.write(entry("item:x", "left before")));
        const std::optional<ALRecoveryEntry> took = store.leftFor("item:x");
        UNSCOPED_INFO("found");
        REQUIRE(took.has_value());
        UNSCOPED_INFO("kept by this one");
        CHECK(store.write(entry("item:x", "typed since")));

        ALRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(parting));

        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("one entry");
        REQUIRE(all.size() == size_t(1));
        {
            INFO("set aside, as it stood");
            CHECK(all.front().state == ALRecoveryEntry::State::Discarded);
            CHECK(all.front().text == "typed since, then closed");
        }
        UNSCOPED_INFO("by this session");
        CHECK(all.front().session == "mine");
        {
            INFO("what it took up gone");
            REQUIRE_FALSE(store.leftFor("item:x").has_value());
            REQUIRE(store.left().empty());
        }
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: where the text cannot be set aside letGo lets go of nothing", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("left by another session");
        CHECK(other.write(entry("item:x", "left before")));
        const std::optional<ALRecoveryEntry> took = store.leftFor("item:x");
        UNSCOPED_INFO("kept by this one");
        CHECK(store.write(entry("item:x", "typed since")));
        // A file where the folder of the discarded would be: nothing can be
        // written there.
        {
            LLFILE* blocking = LLFile::fopen(folder + "/discarded", LLFILE_MODE("wb"));
            UNSCOPED_INFO("made");
            REQUIRE(blocking != nullptr);
            fclose(blocking);
        }

        ALRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        UNSCOPED_INFO("said to have failed");
        CHECK_FALSE(store.letGo(parting));

        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        const auto own = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.session == "mine"; });
        {
            INFO("its own entry still there");
            REQUIRE(own != all.end());
            CHECK(own->text == "typed since");
        }
        UNSCOPED_INFO("and the one it took up");
        REQUIRE(store.leftFor("item:x").has_value());
        UNSCOPED_INFO("a set-aside that fails is said");
        CHECK_FALSE(store.setAside(entry("item:y", "anything")));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a clean tab drops what it took up only where a save could reach it", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("left by another session");
        CHECK(other.write(entry("item:x", "left before")));

        // Holding the kept text where nothing can save it -- the script may
        // no longer be changed, or never loaded: left to be offered again.
        UNSCOPED_INFO("kept by this one");
        CHECK(store.write(entry("item:x", "typed since")));
        ALRecoveryStore::Parting stuck;
        stuck.key     = "item:x";
        stuck.tookUp  = store.leftFor("item:x");
        stuck.settled = false;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(stuck));
        const std::vector<ALRecoveryEntry> after = store.list();
        UNSCOPED_INFO("its own forgotten");
        CHECK(std::none_of(after.begin(), after.end(), [](const ALRecoveryEntry& one) { return one.session == "mine"; }));
        UNSCOPED_INFO("what it took up still offered");
        REQUIRE(store.leftFor("item:x").has_value());
        UNSCOPED_INFO("nothing set aside");
        REQUIRE(files("/discarded").empty());

        // Loaded and clean: the text it took up is what was saved.
        ALRecoveryStore::Parting clean;
        clean.key     = "item:x";
        clean.tookUp  = store.leftFor("item:x");
        clean.settled = true;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(clean));
        UNSCOPED_INFO("what it took up gone");
        REQUIRE_FALSE(store.leftFor("item:x").has_value());
        UNSCOPED_INFO("and nothing set aside");
        REQUIRE(files("/discarded").empty());
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a carrying tab sets aside text with no file and leaves an entry on disk", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");

        // Moved from another window, or loading its item under what it held:
        // the text it carried is the only copy of what was typed, and this
        // session's entry for it was written by the tab it came from.
        UNSCOPED_INFO("kept by the tab it came from");
        CHECK(store.write(entry("item:moved", "typed in the other window")));
        ALRecoveryStore::Parting moved;
        moved.key      = "item:moved";
        moved.tookUp   = entry("item:moved", "typed in the other window");
        moved.carrying = true;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(moved));
        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("one entry");
        REQUIRE(all.size() == size_t(1));
        {
            INFO("the carried text set aside");
            CHECK(all.front().state == ALRecoveryEntry::State::Discarded);
            CHECK(all.front().text == "typed in the other window");
        }

        // Opened to take up another session's entry, and closed before it
        // loaded: that entry is still what it was, to be offered again.
        UNSCOPED_INFO("left by another session");
        CHECK(other.write(entry("item:left", "left before")));
        ALRecoveryStore::Parting waiting;
        waiting.key      = "item:left";
        waiting.tookUp   = store.leftFor("item:left");
        waiting.carrying = true;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(waiting));
        UNSCOPED_INFO("the entry left");
        REQUIRE(store.leftFor("item:left").has_value());
        UNSCOPED_INFO("and nothing more set aside");
        REQUIRE(files("/discarded").size() == size_t(1));

        // A new item opened with a text of its own: nothing to keep.
        UNSCOPED_INFO("kept");
        CHECK(store.write(entry("item:new", "template")));
        ALRecoveryStore::Parting fresh;
        fresh.key      = "item:new";
        fresh.carrying = true;
        UNSCOPED_INFO("let go of");
        CHECK(store.letGo(fresh));
        const std::vector<ALRecoveryEntry> after = store.list();
        UNSCOPED_INFO("forgotten");
        CHECK(std::none_of(after.begin(), after.end(), [](const ALRecoveryEntry& one) { return one.key == "item:new"; }));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a failed load is retried with delays tripling and a few times only", "[alrecoverystore]")
    {
        UNSCOPED_INFO("after the first");
        CHECK(ALRecoveryRetry::delayAfter(1) == ALRecoveryRetry::FIRST);
        UNSCOPED_INFO("after the second");
        CHECK(ALRecoveryRetry::delayAfter(2) == ALRecoveryRetry::FIRST * 3.0);
        UNSCOPED_INFO("after the third");
        CHECK(ALRecoveryRetry::delayAfter(3) == ALRecoveryRetry::FIRST * 9.0);
        UNSCOPED_INFO("none yet is as the first");
        CHECK(ALRecoveryRetry::delayAfter(0) == ALRecoveryRetry::FIRST);
        {
            INFO("tried while there have been fewer failures than the tries");
            CHECK(ALRecoveryRetry::mayTry(0));
            CHECK(ALRecoveryRetry::mayTry(ALRecoveryRetry::TRIES - 1));
        }
        UNSCOPED_INFO("and then waits to be asked");
        CHECK_FALSE(ALRecoveryRetry::mayTry(ALRecoveryRetry::TRIES));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: written as notation; setAside under the session named; newest left first", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryEntry awkward = entry("item:a", "tab\there, 'quoted' \"twice\", back\\slash,\r\nlines, \xC3\xA9, \xF4\x80\x80\x80");
        LLSD                  history;
        history["version"] = 2;
        history["undo"]    = LLSD::emptyArray().with(0, LLSD().with("label", "it's").with("edits", LLSD::emptyArray().with(0, LLSD::emptyArray().with(0, 0).with(1, 0).with(2, 0).with(3, 0).with(4, "").with(5, awkward.text))));
        history["redo"]    = LLSD::emptyArray();
        history["saved"]   = -1;
        awkward.history    = history;
        UNSCOPED_INFO("written");
        CHECK(store.write(awkward));
        {
            const std::vector<std::string> names = files();
            UNSCOPED_INFO("one file");
            REQUIRE(names.size() == size_t(1));
            std::error_code   ec;
            const std::string written = LLFile::getContents(fsyspath(folder + "/" + names.front()), ec);
            // A line of what a listing reads, then a line of the rest, each
            // LLSD notation.
            const size_t line = written.find('\n');
            {
                INFO("as notation, a line of what a listing reads");
                CHECK(written.rfind("{", 0) == 0);
                REQUIRE(line != std::string::npos);
            }
            UNSCOPED_INFO("which is not the text");
            REQUIRE(written.substr(0, line).find("'text'") == std::string::npos);
            UNSCOPED_INFO("then the rest");
            CHECK((written.compare(line + 1, 8, "{'caret'") == 0 || written.compare(line + 1, 10, "{'history'") == 0 ||
                                        written.compare(line + 1, 7, "{'text'") == 0));
        }
        const std::vector<ALRecoveryEntry> back = whole(store, store.list());
        {
            INFO("the text as it was");
            REQUIRE(back.size() == 1);
            CHECK(back.front().text == awkward.text);
        }
        {
            INFO("the history as it was");
            CHECK(back.front().history["undo"][0]["edits"][0][5].asString() == awkward.text);
            CHECK(back.front().history["undo"][0]["label"].asString() == "it's");
        }

        // Set aside for another session, and two a moment apart.
        ALRecoveryEntry theirs = entry("item:b", "theirs");
        theirs.session               = "theirs";
        UNSCOPED_INFO("set aside");
        CHECK(store.setAside(theirs));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        UNSCOPED_INFO("and another");
        CHECK(store.setAside(entry("item:b", "mine, later")));
        UNSCOPED_INFO("both kept");
        REQUIRE(files("/discarded").size() == size_t(2));
        const std::vector<ALRecoveryEntry> all = whole(store, store.list());
        UNSCOPED_INFO("the session it was set aside for");
        CHECK(std::any_of(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "theirs" && one.session == "theirs"; }));
        UNSCOPED_INFO("this one where none was named");
        CHECK(std::any_of(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "mine, later" && one.session == "mine"; }));

        // Left by other sessions: the newest first.
        ALRecoveryStore older(folder, "older");
        ALRecoveryStore newer(folder, "newer");
        UNSCOPED_INFO("older");
        CHECK(older.write(entry("item:c", "older")));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        UNSCOPED_INFO("newer");
        CHECK(newer.write(entry("item:d", "newer")));
        const std::vector<ALRecoveryEntry> left = whole(store, store.left());
        {
            INFO("newest first");
            REQUIRE(left.size() == 2);
            CHECK(left.front().text == "newer");
            CHECK(left.back().text == "older");
        }
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a discard whose name says no time is pruned by what it says inside", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        UNSCOPED_INFO("set aside");
        CHECK(store.setAside(entry("item:a", "old")));
        // Renamed to say nothing of when, and then to say it badly.
        const std::vector<std::string> names = files("/discarded");
        UNSCOPED_INFO("one");
        REQUIRE(names.size() == size_t(1));
        const std::string stamped = folder + "/discarded/" + names.front();
        const std::string bare    = folder + "/discarded/0000.mine.llsd";
        UNSCOPED_INFO("renamed");
        CHECK(LLFile::rename(stamped, bare) == 0);
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("young by what it says, it stays");
        CHECK(LLFile::isfile(bare));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        UNSCOPED_INFO("old by what it says, it goes");
        CHECK_FALSE(LLFile::isfile(bare));

        UNSCOPED_INFO("set aside");
        CHECK(store.setAside(entry("item:b", "old")));
        const std::string garbled = folder + "/discarded/0001.mine.soon.llsd";
        UNSCOPED_INFO("renamed");
        CHECK(LLFile::rename(folder + "/discarded/" + files("/discarded").front(), garbled) == 0);
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("a stamp that is no number read past");
        CHECK(LLFile::isfile(garbled));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        UNSCOPED_INFO("and pruned by what it says");
        CHECK_FALSE(LLFile::isfile(garbled));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: writeSoon writes on the store's thread and the newest for a key wins", "[alrecoverystore]")
    {
        {
            ALRecoveryStore store(folder, "session-a");
            for (int i = 0; i < 20; ++i)
            {
                store.writeSoon(entry("item:x", "typed " + std::to_string(i)));
            }
            store.writeSoon(entry("item:y", "other"));
            store.flush();
            const std::vector<ALRecoveryEntry> all = whole(store, store.list());
            UNSCOPED_INFO("one entry a key");
            REQUIRE(all.size() == size_t(2));
            for (const ALRecoveryEntry& one : all)
            {
                UNSCOPED_INFO("the newest of x: " + one.text);
                REQUIRE((one.key != "item:x" || one.text == "typed 19"));
                UNSCOPED_INFO("this session's");
                REQUIRE(one.session == "session-a");
            }
            for (const std::string& name : files())
            {
                UNSCOPED_INFO("nothing half written: " + name);
                REQUIRE(name.find(".tmp") == std::string::npos);
            }
            // Forgotten right after it was asked to be written: the write
            // goes first, and the file does not come back after.
            store.writeSoon(entry("item:x", "late"));
            store.forget("item:x");
            store.flush();
            const std::vector<ALRecoveryEntry> after = store.list();
            {
                INFO("x gone: " + std::to_string(after.size()));
                REQUIRE(after.size() == 1);
                CHECK(after.front().key == "item:y");
            }
            UNSCOPED_INFO("nothing failed");
            REQUIRE(store.takeFailures().empty());
            // Left waiting as the store goes: written all the same.
            store.writeSoon(entry("item:z", "as it went"));
        }
        ALRecoveryStore again(folder, "session-a");
        bool z = false;
        for (const ALRecoveryEntry& one : whole(again, again.list()))
        {
            z |= one.key == "item:z" && one.text == "as it went";
        }
        UNSCOPED_INFO("written as the store went");
        CHECK(z);
        // Where the folder cannot be written, said by key.
        const std::string blocked = folder + "/blocked";
        {
            llofstream file(blocked);
            file << "a file where a folder would be";
        }
        ALRecoveryStore nowhere(blocked, "session-a");
        nowhere.writeSoon(entry("item:w", "lost"));
        nowhere.flush();
        const std::vector<std::string> failed = nowhere.takeFailures();
        {
            INFO("failed, by key");
            REQUIRE(failed.size() == 1);
            CHECK(failed.front() == "item:w");
        }
        UNSCOPED_INFO("and said once");
        REQUIRE(nowhere.takeFailures().empty());
        // Each asking of its own key leaves another's.
        nowhere.writeSoon(entry("item:u", "lost"));
        nowhere.writeSoon(entry("item:v", "lost"));
        nowhere.flush();
        {
            INFO("one key's own");
            CHECK(nowhere.takeFailure("item:u"));
            CHECK_FALSE(nowhere.takeFailure("item:u"));
        }
        UNSCOPED_INFO("the other's left for it");
        CHECK(nowhere.takeFailures() == std::vector<std::string>({ "item:v" }));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: whenSaid gives the day and time an entry was written", "[alrecoverystore]")
    {
        ALRecoveryEntry one;
        one.when                = LLDate(1758772800.0);
        const std::string said  = one.whenSaid();
        // The day and the hour are the local zone's; the shape is not.
        UNSCOPED_INFO("YYYY-MM-DD HH:MM: " + said);
        REQUIRE(said.size() == size_t(16));
        {
            INFO("dashes and a colon: " + said);
            CHECK(said[4] == '-');
            CHECK(said[7] == '-');
            CHECK(said[10] == ' ');
            CHECK(said[13] == ':');
        }
        UNSCOPED_INFO("the year: " + said);
        CHECK(said.starts_with("2025-09-2"));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: reclaim sets this session's own entry aside keeping when it was written", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "now");
        ALRecoveryEntry went = entry("task:a:b", "typed, then its window went");
        store.write(went);
        const LLDate written = store.list().front().when;
        UNSCOPED_INFO("not left by another session");
        REQUIRE_FALSE(store.leftFor("task:a:b"));

        const std::optional<ALRecoveryEntry> back = store.reclaim("task:a:b");
        UNSCOPED_INFO("reclaimed");
        REQUIRE(back.has_value());
        UNSCOPED_INFO("its text");
        CHECK(back->text == went.text);
        UNSCOPED_INFO("set aside");
        CHECK(back->state == ALRecoveryEntry::State::Discarded);
        UNSCOPED_INFO("when it was written, not when it was set aside");
        CHECK(back->when.secondsSinceEpoch() == written.secondsSinceEpoch());
        {
            INFO("its own file gone, and it among the discarded");
            REQUIRE(files().empty());
            REQUIRE(files("/discarded").size() == 1);
        }
        UNSCOPED_INFO("where it is now");
        REQUIRE(back->path.find("discarded") != std::string::npos);

        store.forget("task:a:b");
        UNSCOPED_INFO("a tab opened clean forgets nothing of it");
        REQUIRE(files("/discarded").size() == 1);
        const std::vector<ALRecoveryEntry> listed = whole(store, store.list());
        {
            INFO("offered under Recover Unsaved Changes");
            REQUIRE(listed.size() == 1);
            CHECK(listed.front().text == went.text);
        }
        UNSCOPED_INFO("reclaimed once");
        REQUIRE_FALSE(store.reclaim("task:a:b"));

        ALRecoveryStore other(folder, "earlier");
        other.write(entry("task:c:d", "another session's"));
        {
            INFO("another session's is not this one's to reclaim");
            REQUIRE_FALSE(store.reclaim("task:c:d"));
            REQUIRE(store.leftFor("task:c:d"));
        }
        store.remove(*back);
        UNSCOPED_INFO("taken up, gone");
        REQUIRE(files("/discarded").empty());
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a listing reads only what it shows and load reads the rest", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        ALRecoveryEntry big = entry("item:big", std::string(200000, 'x'));
        big.caretLine             = 3;
        big.caretColumn           = 4;
        big.historyWritten        = "{'version':i2,'undo':[{'label':'it\\'s','edits':[]}],'redo':[],'saved':i-1}";
        UNSCOPED_INFO("written");
        CHECK(other.write(big));
        const std::vector<ALRecoveryEntry> listed = store.left();
        UNSCOPED_INFO("listed");
        REQUIRE(listed.size() == size_t(1));
        {
            INFO("with what it shows");
            CHECK(listed.front().name == "My Script");
            CHECK(listed.front().key == "item:big");
            CHECK(listed.front().session == "theirs");
        }
        {
            INFO("and not its text");
            CHECK_FALSE(listed.front().whole);
            REQUIRE(listed.front().text.empty());
            CHECK(listed.front().caretLine < 0);
        }
        ALRecoveryEntry taken = listed.front();
        UNSCOPED_INFO("read whole");
        CHECK(store.load(taken));
        {
            INFO("its text");
            CHECK(taken.whole);
            CHECK(taken.text == big.text);
        }
        {
            INFO("its caret");
            CHECK(taken.caretLine == 3);
            CHECK(taken.caretColumn == 4);
        }
        {
            INFO("its history, written as notation, read back");
            CHECK(taken.history["undo"][0]["label"].asString() == "it's");
            CHECK(taken.history["saved"].asInteger() == -1);
        }
        {
            INFO("whole already, read again as it is");
            CHECK(store.load(taken));
            CHECK(taken.text == big.text);
        }
        store.remove(taken);
        ALRecoveryEntry gone = listed.front();
        UNSCOPED_INFO("its file gone, not read");
        CHECK_FALSE(store.load(gone));
        // Set aside from a listing, it is read whole first: nothing of its
        // text is lost among the discarded.
        UNSCOPED_INFO("written again");
        CHECK(other.write(big));
        const std::optional<ALRecoveryEntry> again = store.leftFor("item:big");
        {
            INFO("found, as a listing reads it");
            REQUIRE(again);
            CHECK_FALSE(again->whole);
        }
        UNSCOPED_INFO("discarded");
        CHECK(store.discard(*again));
        const std::vector<ALRecoveryEntry> thrown = whole(store, store.list());
        {
            INFO("with its text");
            REQUIRE(thrown.size() == 1);
            CHECK(thrown.front().text == big.text);
            REQUIRE(thrown.front().history["undo"].size() == 1);
        }
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: offers expire; discards are capped; offers are counted once a moment", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        UNSCOPED_INFO("nothing to offer");
        CHECK_FALSE(store.hasOffers());
        UNSCOPED_INFO("left by another session");
        CHECK(other.write(entry("item:a", "left")));
        UNSCOPED_INFO("counted a moment ago: not yet seen");
        CHECK_FALSE(store.hasOffers());
        store.forget("item:none");
        UNSCOPED_INFO("seen once this store changes anything");
        CHECK(store.hasOffers());

        // Offered: marked when, still offered, and gone a while after.
        const LLDate offered = LLDate::now();
        store.markOffered(store.left(), offered);
        const std::vector<std::string> names = files();
        {
            INFO("renamed to say when");
            REQUIRE(names.size() == 1);
            REQUIRE(names.front().find(std::to_string(static_cast<S64>(offered.secondsSinceEpoch() * 1000.0))) != std::string::npos);
        }
        {
            INFO("still offered");
            CHECK(store.hasOffers());
            REQUIRE(store.left().size() == 1);
            REQUIRE(store.leftFor("item:a").has_value());
        }
        UNSCOPED_INFO("and read");
        CHECK(whole(store, store.leftFor("item:a"))->text == "left");
        store.markOffered(store.left(), LLDate(offered.secondsSinceEpoch() + 100.0));
        UNSCOPED_INFO("marked once");
        CHECK(files().front() == names.front());
        store.prune(60.0, LLDate(offered.secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("young, it stays");
        REQUIRE(files().size() == 1);
        store.prune(60.0, LLDate(offered.secondsSinceEpoch() + 120.0));
        {
            INFO("left alone a while, it goes");
            REQUIRE(files().empty());
            CHECK_FALSE(store.hasOffers());
        }
        UNSCOPED_INFO("this session's own is never offered");
        CHECK(store.write(entry("item:mine", "mine")));
        store.markOffered(store.list());
        {
            INFO("nor marked");
            REQUIRE(files().size() == 1);
            REQUIRE(files().front().find(".mine.llsd") != std::string::npos);
        }

        // The discarded held to three, then to what two of them weigh.
        store.limitDiscarded(3, 1024 * 1024);
        for (int i = 0; i < 5; ++i)
        {
            UNSCOPED_INFO("set aside");
            REQUIRE(store.setAside(entry("item:d" + std::to_string(i), "discarded " + std::to_string(i))));
            // Apart by more than the millisecond their names say.
            ms_sleep(5);
        }
        std::vector<ALRecoveryEntry> kept = whole(store, store.list());
        UNSCOPED_INFO("three kept");
        REQUIRE(files("/discarded").size() == size_t(3));
        {
            INFO("the newest");
            CHECK(std::any_of(kept.begin(), kept.end(), [](const ALRecoveryEntry& one) { return one.text == "discarded 4"; }));
            CHECK(std::none_of(kept.begin(), kept.end(), [](const ALRecoveryEntry& one) { return one.text == "discarded 1"; }));
        }
        std::error_code ec;
        const uintmax_t one = std::filesystem::file_size(fsyspath(folder + "/discarded/" + files("/discarded").front()), ec);
        store.limitDiscarded(100, static_cast<size_t>(one * 2 + one / 2));
        store.prune(60.0);
        UNSCOPED_INFO("two, by what they weigh");
        REQUIRE(files("/discarded").size() == size_t(2));
    }
}

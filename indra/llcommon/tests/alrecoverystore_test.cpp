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
#include "alteststateguard.h"
#include "altestteardown.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    // The stores write on one thread for the process, ALRecoveryWriter's,
    // which the viewer stops in its cleanup. Here it is made once, before the
    // first case that writes would make it, and stopped by the harness's
    // teardown: that it then runs is no change a case made.
    void makeRecoveryWriter()
    {
        static bool sMade = false;
        if (!sMade)
        {
            sMade = true;
            ALRecoveryWriter::getInstance();
            ll_test::Teardown::add([] { ALRecoveryWriter::deleteSingleton(); });
            ll_test::StateGuard::accept("the live thread pools");
            ll_test::StateGuard::accept("the live work queues");
        }
    }

    struct alrecoverystore_data
    {
        // A folder of its own for each test, gone after it.
        std::string folder;

        alrecoverystore_data()
        {
            makeRecoveryWriter();
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
        static std::vector<ALRecoveryEntry> require_whole(const ALRecoveryStore& store, std::vector<ALRecoveryEntry> entries)
        {
            for (ALRecoveryEntry& one : entries)
            {
                CAPTURE(one.key);
                REQUIRE(store.load(one));
            }
            return entries;
        }
        static std::optional<ALRecoveryEntry> require_whole(const ALRecoveryStore& store, std::optional<ALRecoveryEntry> one)
        {
            if (one)
            {
                CAPTURE(one->key);
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
        CHECK(task != inventory);
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
        REQUIRE(ALRecoveryEntry::fromLLSD(was.asLLSD(), back));
        UNSCOPED_INFO("the text, whatever is in it");
        CHECK(back.text == was.text);
        CHECK(back.state == ALRecoveryEntry::State::Kept);
        CHECK(back.when.secondsSinceEpoch() == was.when.secondsSinceEpoch());
        CHECK(back.object == was.object);
        CHECK(back.item == was.item);
        CHECK(back.objectName == "Door");
        CHECK(back.region == "Ahern");
        CHECK(back.lua);
        CHECK_FALSE(back.notecard);
        CHECK(back.wrapped);
        CHECK(back.compileTarget == "luau");
        CHECK(back.baseAsset == was.baseAsset);
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
        CHECK(ALRecoveryEntry::fromLLSD(was.asLLSD(), back));
        CHECK(back.history["undo"][0]["label"].asString() == "rename");
        CHECK(back.history["saved"].asInteger() == 0);
        CHECK(back.caretLine == 4);
        CHECK(back.caretColumn == 7);
        UNSCOPED_INFO("something that is no entry is not read as one");
        CHECK_FALSE(ALRecoveryEntry::fromLLSD(LLSD("text"), back));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a session's entry is written whole then over by its next and forgotten", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "session-a");
        CHECK(store.write(entry("item:x", "first")));
        CHECK(store.write(entry("item:x", "second")));
        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        UNSCOPED_INFO("one entry for one key");
        REQUIRE(all.size() == size_t(1));
        CHECK(all.front().text == std::string("second"));
        CHECK(all.front().session == std::string("session-a"));
        CHECK(all.front().state == ALRecoveryEntry::State::Unsaved);
        for (const std::string& name : files())
        {
            CAPTURE(name);
            REQUIRE(name.find(".tmp") == std::string::npos);
        }
        UNSCOPED_INFO("this session's own is nothing to offer");
        CHECK_FALSE(store.hasOffers());
        REQUIRE(store.left().empty());
        store.forget("item:x");
        REQUIRE(store.list().empty());
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: typing in a script another session left keeps what it left", "[alrecoverystore]")
    {
        ALRecoveryStore before(folder, "before");
        ALRecoveryStore now(folder, "now");
        UNSCOPED_INFO("the crashed session's");
        CHECK(before.write(entry("task:o:i", "left behind")));
        CHECK(now.write(entry("task:o:i", "typed since")));
        UNSCOPED_INFO("both kept");
        REQUIRE(now.list().size() == size_t(2));
        const std::vector<ALRecoveryEntry> left = require_whole(now, now.left());
        REQUIRE(left.size() == size_t(1));
        CHECK(left.front().text == std::string("left behind"));
        const std::optional<ALRecoveryEntry> found = require_whole(now, now.leftFor("task:o:i"));
        REQUIRE(found.has_value());
        CHECK(found->text == "left behind");
        REQUIRE_FALSE(now.leftFor("task:o:j").has_value());
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
        CHECK(store.write(entry("item:gone", "thrown away")));
        CHECK(other.write(entry("item:kept", "never thrown away")));
        {
            const std::vector<ALRecoveryEntry> written = store.list();
            const auto mine = std::find_if(written.begin(), written.end(), [](const ALRecoveryEntry& one) { return one.key == "item:gone"; });
            REQUIRE(mine != written.end());
            CHECK(store.discard(*mine));
        }
        std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        UNSCOPED_INFO("both still listed");
        REQUIRE(all.size() == size_t(2));
        const auto discarded = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.key == "item:gone"; });
        REQUIRE(discarded != all.end());
        CHECK(discarded->state == ALRecoveryEntry::State::Discarded);
        CHECK(discarded->text == std::string("thrown away"));
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
        CHECK(store.discard(*theirs));
        REQUIRE_FALSE(store.leftFor("item:kept").has_value());
        REQUIRE(files("/discarded").size() == size_t(1));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a file that cannot be read is passed over and left in place", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        CHECK(store.write(entry("item:ok", "fine")));
        {
            LLFILE* broken = LLFile::fopen(folder + "/0000.someone.llsd", LLFILE_MODE("wb"));
            REQUIRE(broken != nullptr);
            fputs("<llsd><map><key>text</key><string>half", broken);
            fclose(broken);
        }
        const std::vector<ALRecoveryEntry> all = store.list();
        UNSCOPED_INFO("the one that reads");
        REQUIRE(all.size() == size_t(1));
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
        CHECK(store.write(card));
        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        REQUIRE(all.size() == size_t(1));
        CHECK(all.front().notecard);
        UNSCOPED_INFO("its text, the placeholders in it byte for byte");
        CHECK(all.front().text == card.text);
        REQUIRE(all.front().embedded.size() == 2);
        CHECK(all.front().embedded[0]["name"].asString() == "Sunset");
        CHECK(all.front().embedded[1]["name"].asString() == "Home");
        UNSCOPED_INFO("each whole");
        CHECK(all.front().embedded[1]["item_id"].asUUID() == second["item_id"].asUUID());
        // Discarded, it keeps them too.
        CHECK(store.discard(all.front()));
        const std::vector<ALRecoveryEntry> thrown = require_whole(store, store.list());
        {
            INFO("the items go with it");
            REQUIRE(thrown.size() == 1);
            REQUIRE(thrown.front().embedded.size() == 2);
        }
        // A script has none, and says so as an empty list.
        ALRecoveryEntry script;
        REQUIRE(ALRecoveryEntry::fromLLSD(entry("item:s", "x").asLLSD(), script));
        REQUIRE(script.embedded.isArray());
        REQUIRE(script.embedded.size() == 0);
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: setAside leaves its source; prune sweeps halves; an XML entry reads", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        CHECK(other.write(entry("item:a", "left")));
        CHECK(store.setAside(entry("item:a", "thrown")));
        REQUIRE(files("/discarded").size() == size_t(1));
        const std::optional<ALRecoveryEntry> left = require_whole(store, store.leftFor("item:a"));
        {
            INFO("what it came from still left");
            REQUIRE(left.has_value());
            CHECK(left->text == "left");
        }
        UNSCOPED_INFO("and only that is left");
        REQUIRE(store.left().size() == size_t(1));
        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        const auto thrown = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "thrown"; });
        REQUIRE(thrown != all.end());
        REQUIRE(thrown->session == "mine");
        CHECK(thrown->state == ALRecoveryEntry::State::Discarded);

        // Written half by a session that went, and by this one.
        for (const char* name : { "/0001.theirs.llsd.tmp", "/0002.mine.llsd.tmp" })
        {
            LLFILE* half = LLFile::fopen(folder + name, LLFILE_MODE("wb"));
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
            REQUIRE(xml != nullptr);
            std::ostringstream text;
            LLSDSerialize::toPrettyXML(was.asLLSD(), text);
            fputs(text.str().c_str(), xml);
            fclose(xml);
        }
        const std::vector<ALRecoveryEntry> now = require_whole(store, store.left());
        CHECK(std::any_of(now.begin(), now.end(), [](const ALRecoveryEntry& one) { return one.text == "from before"; }));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: letGo of a changed tab sets its text aside and drops what it took up", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        CHECK(other.write(entry("item:x", "left before")));
        const std::optional<ALRecoveryEntry> took = store.leftFor("item:x");
        REQUIRE(took.has_value());
        CHECK(store.write(entry("item:x", "typed since")));

        ALRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        CHECK(store.letGo(parting));

        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        REQUIRE(all.size() == size_t(1));
        {
            INFO("set aside, as it stood");
            CHECK(all.front().state == ALRecoveryEntry::State::Discarded);
            CHECK(all.front().text == "typed since, then closed");
        }
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
        CHECK(other.write(entry("item:x", "left before")));
        const std::optional<ALRecoveryEntry> took = store.leftFor("item:x");
        CHECK(took.has_value());
        CHECK(store.write(entry("item:x", "typed since")));
        // A file where the folder of the discarded would be: nothing can be
        // written there.
        {
            LLFILE* blocking = LLFile::fopen(folder + "/discarded", LLFILE_MODE("wb"));
            REQUIRE(blocking != nullptr);
            fclose(blocking);
        }

        ALRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        CHECK_FALSE(store.letGo(parting));

        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        const auto own = std::find_if(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.session == "mine"; });
        {
            INFO("its own entry still there");
            REQUIRE(own != all.end());
            CHECK(own->text == "typed since");
        }
        UNSCOPED_INFO("and the one it took up");
        REQUIRE(store.leftFor("item:x").has_value());
        CHECK_FALSE(store.setAside(entry("item:y", "anything")));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a clean tab drops what it took up only where a save could reach it", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        CHECK(other.write(entry("item:x", "left before")));

        // Holding the kept text where nothing can save it -- the script may
        // no longer be changed, or never loaded: left to be offered again.
        CHECK(store.write(entry("item:x", "typed since")));
        ALRecoveryStore::Parting stuck;
        stuck.key     = "item:x";
        stuck.tookUp  = store.leftFor("item:x");
        stuck.settled = false;
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
        CHECK(store.letGo(moved));
        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        REQUIRE(all.size() == size_t(1));
        {
            INFO("the carried text set aside");
            CHECK(all.front().state == ALRecoveryEntry::State::Discarded);
            CHECK(all.front().text == "typed in the other window");
        }

        // Opened to take up another session's entry, and closed before it
        // loaded: that entry is still what it was, to be offered again.
        CHECK(other.write(entry("item:left", "left before")));
        ALRecoveryStore::Parting waiting;
        waiting.key      = "item:left";
        waiting.tookUp   = store.leftFor("item:left");
        waiting.carrying = true;
        CHECK(store.letGo(waiting));
        UNSCOPED_INFO("the entry left");
        REQUIRE(store.leftFor("item:left").has_value());
        UNSCOPED_INFO("and nothing more set aside");
        REQUIRE(files("/discarded").size() == size_t(1));

        // A new item opened with a text of its own: nothing to keep.
        CHECK(store.write(entry("item:new", "template")));
        ALRecoveryStore::Parting fresh;
        fresh.key      = "item:new";
        fresh.carrying = true;
        CHECK(store.letGo(fresh));
        const std::vector<ALRecoveryEntry> after = store.list();
        CHECK(std::none_of(after.begin(), after.end(), [](const ALRecoveryEntry& one) { return one.key == "item:new"; }));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a failed load is retried with delays tripling and a few times only", "[alrecoverystore]")
    {
        CHECK(ALRecoveryRetry::delayAfter(1) == ALRecoveryRetry::FIRST);
        CHECK(ALRecoveryRetry::delayAfter(2) == ALRecoveryRetry::FIRST * 3.0);
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
        CHECK(store.write(awkward));
        {
            const std::vector<std::string> names = files();
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
        const std::vector<ALRecoveryEntry> back = require_whole(store, store.list());
        REQUIRE(back.size() == 1);
        CHECK(back.front().text == awkward.text);
        CHECK(back.front().history["undo"][0]["edits"][0][5].asString() == awkward.text);
        CHECK(back.front().history["undo"][0]["label"].asString() == "it's");

        // Set aside for another session, and two a moment apart.
        ALRecoveryEntry theirs = entry("item:b", "theirs");
        theirs.session               = "theirs";
        CHECK(store.setAside(theirs));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        CHECK(store.setAside(entry("item:b", "mine, later")));
        UNSCOPED_INFO("both kept");
        REQUIRE(files("/discarded").size() == size_t(2));
        const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
        UNSCOPED_INFO("the session it was set aside for");
        CHECK(std::any_of(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "theirs" && one.session == "theirs"; }));
        UNSCOPED_INFO("this one where none was named");
        CHECK(std::any_of(all.begin(), all.end(), [](const ALRecoveryEntry& one) { return one.text == "mine, later" && one.session == "mine"; }));

        // Left by other sessions: the newest first.
        ALRecoveryStore older(folder, "older");
        ALRecoveryStore newer(folder, "newer");
        CHECK(older.write(entry("item:c", "older")));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        CHECK(newer.write(entry("item:d", "newer")));
        const std::vector<ALRecoveryEntry> left = require_whole(store, store.left());
        REQUIRE(left.size() == 2);
        CHECK(left.front().text == "newer");
        CHECK(left.back().text == "older");
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: a discard whose name says no time is pruned by what it says inside", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        CHECK(store.setAside(entry("item:a", "old")));
        // Renamed to say nothing of when, and then to say it badly.
        const std::vector<std::string> names = files("/discarded");
        REQUIRE(names.size() == size_t(1));
        const std::string stamped = folder + "/discarded/" + names.front();
        const std::string bare    = folder + "/discarded/0000.mine.llsd";
        CHECK(LLFile::rename(stamped, bare) == 0);
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("young by what it says, it stays");
        CHECK(LLFile::isfile(bare));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        UNSCOPED_INFO("old by what it says, it goes");
        CHECK_FALSE(LLFile::isfile(bare));

        CHECK(store.setAside(entry("item:b", "old")));
        const std::vector<std::string> again = files("/discarded");
        REQUIRE(again.size() == size_t(1));
        const std::string garbled = folder + "/discarded/0001.mine.soon.llsd";
        CHECK(LLFile::rename(folder + "/discarded/" + again.front(), garbled) == 0);
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
            const std::vector<ALRecoveryEntry> all = require_whole(store, store.list());
            UNSCOPED_INFO("one entry a key");
            REQUIRE(all.size() == size_t(2));
            for (const ALRecoveryEntry& one : all)
            {
                CAPTURE(one.key, one.text);
                REQUIRE((one.key != "item:x" || one.text == "typed 19"));
                REQUIRE(one.session == "session-a");
            }
            for (const std::string& name : files())
            {
                CAPTURE(name);
                REQUIRE(name.find(".tmp") == std::string::npos);
            }
            // Forgotten right after it was asked to be written: the write
            // goes first, and the file does not come back after.
            store.writeSoon(entry("item:x", "late"));
            store.forget("item:x");
            store.flush();
            const std::vector<ALRecoveryEntry> after = store.list();
            {
                INFO("x gone");
                REQUIRE(after.size() == 1);
                CHECK(after.front().key == "item:y");
            }
            REQUIRE(store.takeFailures().empty());
            // Left waiting as the store goes: written all the same.
            store.writeSoon(entry("item:z", "as it went"));
        }
        ALRecoveryStore again(folder, "session-a");
        bool z = false;
        for (const ALRecoveryEntry& one : require_whole(again, again.list()))
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
        REQUIRE(failed.size() == 1);
        CHECK(failed.front() == "item:w");
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
        CAPTURE(said);
        UNSCOPED_INFO("YYYY-MM-DD HH:MM");
        REQUIRE(said.size() == size_t(16));
        CHECK(said[4] == '-');
        CHECK(said[7] == '-');
        CHECK(said[10] == ' ');
        CHECK(said[13] == ':');
        CHECK(said.starts_with("2025-09-2"));
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: reclaim sets this session's own entry aside keeping when it was written", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "now");
        ALRecoveryEntry went = entry("task:a:b", "typed, then its window went");
        CHECK(store.write(went));
        const std::vector<ALRecoveryEntry> own = store.list();
        REQUIRE(own.size() == size_t(1));
        const LLDate written = own.front().when;
        UNSCOPED_INFO("not left by another session");
        REQUIRE_FALSE(store.leftFor("task:a:b"));

        const std::optional<ALRecoveryEntry> back = store.reclaim("task:a:b");
        REQUIRE(back.has_value());
        CHECK(back->text == went.text);
        CHECK(back->state == ALRecoveryEntry::State::Discarded);
        UNSCOPED_INFO("when it was written, not when it was set aside");
        CHECK(back->when.secondsSinceEpoch() == written.secondsSinceEpoch());
        {
            INFO("its own file gone, and it among the discarded");
            REQUIRE(files().empty());
            REQUIRE(files("/discarded").size() == 1);
        }
        REQUIRE(back->path.find("discarded") != std::string::npos);

        store.forget("task:a:b");
        UNSCOPED_INFO("a tab opened clean forgets nothing of it");
        REQUIRE(files("/discarded").size() == 1);
        const std::vector<ALRecoveryEntry> listed = require_whole(store, store.list());
        {
            INFO("offered under Recover Unsaved Changes");
            REQUIRE(listed.size() == 1);
            CHECK(listed.front().text == went.text);
        }
        UNSCOPED_INFO("reclaimed once");
        REQUIRE_FALSE(store.reclaim("task:a:b"));

        ALRecoveryStore other(folder, "earlier");
        CHECK(other.write(entry("task:c:d", "another session's")));
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
        CHECK(other.write(big));
        const std::vector<ALRecoveryEntry> listed = store.left();
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
        CHECK(store.load(taken));
        CHECK(taken.whole);
        CHECK(taken.text == big.text);
        CHECK(taken.caretLine == 3);
        CHECK(taken.caretColumn == 4);
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
        CHECK(other.write(big));
        const std::optional<ALRecoveryEntry> again = store.leftFor("item:big");
        {
            INFO("found, as a listing reads it");
            REQUIRE(again);
            CHECK_FALSE(again->whole);
        }
        CHECK(store.discard(*again));
        const std::vector<ALRecoveryEntry> thrown = require_whole(store, store.list());
        REQUIRE(thrown.size() == 1);
        CHECK(thrown.front().text == big.text);
        REQUIRE(thrown.front().history["undo"].size() == 1);
    }

    TEST_CASE_METHOD(alrecoverystore_data, "alrecoverystore: offers expire; discards are capped; offers are counted once a moment", "[alrecoverystore]")
    {
        ALRecoveryStore store(folder, "mine");
        ALRecoveryStore other(folder, "theirs");
        CHECK_FALSE(store.hasOffers());
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
        CHECK(require_whole(store, store.leftFor("item:a"))->text == "left");
        store.markOffered(store.left(), LLDate(offered.secondsSinceEpoch() + 100.0));
        const std::vector<std::string> remarked = files();
        REQUIRE(remarked.size() == 1);
        UNSCOPED_INFO("marked once");
        CHECK(remarked.front() == names.front());
        store.prune(60.0, LLDate(offered.secondsSinceEpoch() + 30.0));
        UNSCOPED_INFO("young, it stays");
        REQUIRE(files().size() == 1);
        store.prune(60.0, LLDate(offered.secondsSinceEpoch() + 120.0));
        {
            INFO("left alone a while, it goes");
            REQUIRE(files().empty());
            CHECK_FALSE(store.hasOffers());
        }
        // This session's own is never offered, nor marked.
        CHECK(store.write(entry("item:mine", "mine")));
        store.markOffered(store.list());
        {
            INFO("this session's own not marked");
            REQUIRE(files().size() == 1);
            REQUIRE(files().front().find(".mine.llsd") != std::string::npos);
        }

        // The discarded held to three, then to what two of them weigh.
        store.limitDiscarded(3, 1024 * 1024);
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE(store.setAside(entry("item:d" + std::to_string(i), "discarded " + std::to_string(i))));
            // Apart by more than the millisecond their names say.
            ms_sleep(5);
        }
        std::vector<ALRecoveryEntry> kept = require_whole(store, store.list());
        const std::vector<std::string> discarded = files("/discarded");
        REQUIRE(discarded.size() == size_t(3));
        {
            INFO("the newest");
            CHECK(std::any_of(kept.begin(), kept.end(), [](const ALRecoveryEntry& one) { return one.text == "discarded 4"; }));
            CHECK(std::none_of(kept.begin(), kept.end(), [](const ALRecoveryEntry& one) { return one.text == "discarded 1"; }));
        }
        std::error_code ec;
        const uintmax_t one = std::filesystem::file_size(fsyspath(folder + "/discarded/" + discarded.front()), ec);
        store.limitDiscarded(100, static_cast<size_t>(one * 2 + one / 2));
        store.prune(60.0);
        UNSCOPED_INFO("two, by what they weigh");
        REQUIRE(files("/discarded").size() == size_t(2));
    }
}

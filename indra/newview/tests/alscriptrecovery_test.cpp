/**
 * @file alscriptrecovery_test.cpp
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

#include "../alscriptrecovery.h"

#include "fsyspath.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "lltimer.h"

#include "../test/lltut.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace tut
{
    struct alscriptrecovery_data
    {
        // A folder of its own for each test, gone after it.
        std::string folder;

        alscriptrecovery_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptrecovery_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(folder), ec);
        }

        ~alscriptrecovery_data()
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

        static ALScriptRecoveryEntry entry(const std::string& key, const std::string& text)
        {
            ALScriptRecoveryEntry one;
            one.key  = key;
            one.name = "My Script";
            one.text = text;
            return one;
        }
    };
    typedef test_group<alscriptrecovery_data> alscriptrecovery_group;
    typedef alscriptrecovery_group::object    alscriptrecovery_object;
    tut::alscriptrecovery_group               alscriptrecovery_instance("alscriptrecovery");

    template<> template<>
    void alscriptrecovery_object::test<1>()
    {
        set_test_name("a key says whose text it is: an object's item, the inventory's, or a file's");
        const LLUUID object = LLUUID::generateNewID();
        const LLUUID item   = LLUUID::generateNewID();
        const std::string task      = ALScriptRecoveryStore::keyOf(object, item, std::string());
        const std::string inventory = ALScriptRecoveryStore::keyOf(LLUUID::null, item, std::string());
        const std::string file      = ALScriptRecoveryStore::keyOf(LLUUID::null, LLUUID::null, "/scripts/lib.lsl");
        ensure("an object's item and the inventory's differ", task != inventory);
        ensure("a file is its path", file == "disk:/scripts/lib.lsl");
        ensure_equals("and a key is the same asked twice", ALScriptRecoveryStore::keyOf(object, item, std::string()), task);
    }

    template<> template<>
    void alscriptrecovery_object::test<2>()
    {
        set_test_name("an entry reads back as it was written");
        ALScriptRecoveryEntry was  = entry("task:a:b", "default\n{\n    state_entry() { llSay(0, \"<&>\"); }\n}\n");
        was.session                = "one";
        was.state                  = ALScriptRecoveryEntry::State::Kept;
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
        ALScriptRecoveryEntry back;
        ensure("it reads", ALScriptRecoveryEntry::fromLLSD(was.asLLSD(), back));
        ensure_equals("the text, whatever is in it", back.text, was.text);
        ensure("kept", back.state == ALScriptRecoveryEntry::State::Kept);
        ensure_equals("when", back.when.secondsSinceEpoch(), was.when.secondsSinceEpoch());
        ensure("the object and the item", back.object == was.object && back.item == was.item);
        ensure("where it was", back.objectName == "Door" && back.region == "Ahern");
        ensure("what it is", back.lua && !back.notecard && back.wrapped && back.compileTarget == "luau" && back.baseAsset == was.baseAsset);
        ensure("no history said, none read", !back.history.isDefined() && back.caretLine == -1);
        LLSD history;
        history["undo"]  = LLSD::emptyArray().with(0, LLSD().with("label", "rename"));
        history["saved"] = 0;
        was.history      = history;
        was.caretLine    = 4;
        was.caretColumn  = 7;
        ensure("it reads again", ALScriptRecoveryEntry::fromLLSD(was.asLLSD(), back));
        ensure("the history as it was written", back.history["undo"][0]["label"].asString() == "rename" && back.history["saved"].asInteger() == 0);
        ensure("and where the caret stood", back.caretLine == 4 && back.caretColumn == 7);
        ensure("something that is no entry is not read as one", !ALScriptRecoveryEntry::fromLLSD(LLSD("text"), back));
    }

    template<> template<>
    void alscriptrecovery_object::test<7>()
    {
        set_test_name("a notecard keeps its items with its text, in order, through the disk and back");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryEntry card = entry("item:card", std::string("Look: ") + "\xF4\x80\x80\x80" + " and " + "\xF4\x80\x80\x81");
        card.notecard              = true;
        LLSD first;
        first["item_id"] = LLUUID::generateNewID();
        first["name"]    = "Sunset";
        LLSD second;
        second["item_id"] = LLUUID::generateNewID();
        second["name"]    = "Home";
        card.embedded     = LLSD::emptyArray().with(0, first).with(1, second);
        ensure("written", store.write(card));
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("one", all.size(), size_t(1));
        ensure("a notecard", all.front().notecard);
        ensure_equals("its text, the placeholders in it byte for byte", all.front().text, card.text);
        ensure_equals("both items", all.front().embedded.size(), 2);
        ensure("in the order the text stands them",
               all.front().embedded[0]["name"].asString() == "Sunset" && all.front().embedded[1]["name"].asString() == "Home");
        ensure("each whole", all.front().embedded[1]["item_id"].asUUID() == second["item_id"].asUUID());
        // Discarded, it keeps them too.
        ensure("discarded", store.discard(all.front()));
        const std::vector<ALScriptRecoveryEntry> thrown = store.list();
        ensure("the items go with it", thrown.size() == 1 && thrown.front().embedded.size() == 2);
        // A script has none, and says so as an empty list.
        ALScriptRecoveryEntry script;
        ensure("read", ALScriptRecoveryEntry::fromLLSD(entry("item:s", "x").asLLSD(), script));
        ensure("no items", script.embedded.isArray() && script.embedded.size() == 0);
    }

    template<> template<>
    void alscriptrecovery_object::test<3>()
    {
        set_test_name("a session's entry is written whole, written over by its next, and forgotten");
        ALScriptRecoveryStore store(folder, "session-a");
        ensure("written", store.write(entry("item:x", "first")));
        ensure("written again", store.write(entry("item:x", "second")));
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("one entry for one key", all.size(), size_t(1));
        ensure_equals("the last text", all.front().text, std::string("second"));
        ensure_equals("this session's", all.front().session, std::string("session-a"));
        ensure("unsaved", all.front().state == ALScriptRecoveryEntry::State::Unsaved);
        for (const std::string& name : files())
        {
            ensure("nothing half written left beside it: " + name, name.find(".tmp") == std::string::npos);
        }
        ensure("this session's own is nothing to offer", !store.hasOffers());
        ensure("nor left by another", store.left().empty());
        store.forget("item:x");
        ensure("forgotten", store.list().empty());
    }

    template<> template<>
    void alscriptrecovery_object::test<4>()
    {
        set_test_name("one session's entries are apart from another's: typing in a script another session left does not write over what it left");
        ALScriptRecoveryStore before(folder, "before");
        ALScriptRecoveryStore now(folder, "now");
        ensure("the crashed session's", before.write(entry("task:o:i", "left behind")));
        ensure("this session's", now.write(entry("task:o:i", "typed since")));
        ensure_equals("both kept", now.list().size(), size_t(2));
        const std::vector<ALScriptRecoveryEntry> left = now.left();
        ensure_equals("one left by another session", left.size(), size_t(1));
        ensure_equals("its text", left.front().text, std::string("left behind"));
        const std::optional<ALScriptRecoveryEntry> found = now.leftFor("task:o:i");
        ensure("found by its key", found.has_value() && found->text == "left behind");
        ensure("nothing for another key", !now.leftFor("task:o:j").has_value());
        ensure("something to offer", now.hasOffers());
        now.forget("task:o:i");
        ensure("this session forgetting its own leaves the other's", now.leftFor("task:o:i").has_value());
        now.remove(*found);
        ensure("taken up, it is gone", !now.leftFor("task:o:i").has_value() && !now.hasOffers());
    }

    template<> template<>
    void alscriptrecovery_object::test<5>()
    {
        set_test_name("a discarded entry is kept apart a while, then let go of; an unsaved one never is");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");
        ensure("written", store.write(entry("item:gone", "thrown away")));
        ensure("and another session's", other.write(entry("item:kept", "never thrown away")));
        {
            const std::vector<ALScriptRecoveryEntry> written = store.list();
            const auto mine = std::find_if(written.begin(), written.end(), [](const ALScriptRecoveryEntry& one) { return one.key == "item:gone"; });
            ensure("discarded", mine != written.end() && store.discard(*mine));
        }
        std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("both still listed", all.size(), size_t(2));
        const auto discarded = std::find_if(all.begin(), all.end(), [](const ALScriptRecoveryEntry& one) { return one.key == "item:gone"; });
        ensure("the discarded one", discarded != all.end() && discarded->state == ALScriptRecoveryEntry::State::Discarded);
        ensure_equals("with its text", discarded->text, std::string("thrown away"));
        ensure_equals("in the folder beside", files("/discarded").size(), size_t(1));
        ensure("not among what was left unsaved", store.leftFor("item:gone") == std::nullopt);
        ensure("but offered", store.hasOffers());
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        ensure_equals("young, it stays", files("/discarded").size(), size_t(1));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        ensure("old, it goes", files("/discarded").empty());
        ensure("and the unsaved one does not", store.leftFor("item:kept").has_value());
        // Another session's, discarded from the list of what was left.
        const std::optional<ALScriptRecoveryEntry> theirs = store.leftFor("item:kept");
        ensure("discarded too", store.discard(*theirs));
        ensure("gone from what was left", !store.leftFor("item:kept").has_value());
        ensure_equals("into the folder beside", files("/discarded").size(), size_t(1));
    }

    template<> template<>
    void alscriptrecovery_object::test<6>()
    {
        set_test_name("a file that cannot be read is passed over and left where it is, for a person to look at");
        ALScriptRecoveryStore store(folder, "mine");
        ensure("written", store.write(entry("item:ok", "fine")));
        {
            LLFILE* broken = LLFile::fopen(folder + "/0000.someone.llsd", LLFILE_MODE("wb"));
            ensure("made", broken != nullptr);
            fputs("<llsd><map><key>text</key><string>half", broken);
            fclose(broken);
        }
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("the one that reads", all.size(), size_t(1));
        ensure_equals("is the good one", all.front().key, std::string("item:ok"));
        ensure("the other is still there", LLFile::isfile(folder + "/0000.someone.llsd"));
    }

    template<> template<>
    void alscriptrecovery_object::test<8>()
    {
        set_test_name("a text set aside leaves what it came from; half-written files and old discards go at a prune; an XML entry still reads");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");
        ensure("another session's", other.write(entry("item:a", "left")));
        ensure("set aside", store.setAside(entry("item:a", "thrown")));
        ensure_equals("among the discarded", files("/discarded").size(), size_t(1));
        const std::optional<ALScriptRecoveryEntry> left = store.leftFor("item:a");
        ensure("what it came from still left", left.has_value() && left->text == "left");
        ensure_equals("and only that is left", store.left().size(), size_t(1));
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        const auto thrown = std::find_if(all.begin(), all.end(), [](const ALScriptRecoveryEntry& one) { return one.text == "thrown"; });
        ensure("set aside by this session", thrown != all.end() && thrown->session == "mine" && thrown->state == ALScriptRecoveryEntry::State::Discarded);

        // Written half by a session that went, and by this one.
        for (const char* name : { "/0001.theirs.llsd.tmp", "/0002.mine.llsd.tmp" })
        {
            LLFILE* half = LLFile::fopen(folder + name, LLFILE_MODE("wb"));
            ensure("made", half != nullptr);
            fputs("<? llsd/notation ?>\n{'key':'ite", half);
            fclose(half);
        }
        store.prune(60.0);
        ensure("the gone session's half gone", !LLFile::isfile(folder + "/0001.theirs.llsd.tmp"));
        ensure("this session's own left", LLFile::isfile(folder + "/0002.mine.llsd.tmp"));
        ensure_equals("the young discard stays", files("/discarded").size(), size_t(1));

        // An entry as the XML it was once written as.
        {
            ALScriptRecoveryEntry was = entry("item:old", "from before");
            was.session               = "older";
            LLFILE* xml = LLFile::fopen(folder + "/0003.older.llsd", LLFILE_MODE("wb"));
            ensure("made", xml != nullptr);
            std::ostringstream text;
            LLSDSerialize::toPrettyXML(was.asLLSD(), text);
            fputs(text.str().c_str(), xml);
            fclose(xml);
        }
        const std::vector<ALScriptRecoveryEntry> now = store.left();
        ensure("read", std::any_of(now.begin(), now.end(), [](const ALScriptRecoveryEntry& one) { return one.text == "from before"; }));
    }

    template<> template<>
    void alscriptrecovery_object::test<9>()
    {
        set_test_name("a changed tab let go of has its text set aside, its own entry forgotten, and the entry it took up let go of");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");
        ensure("left by another session", other.write(entry("item:x", "left before")));
        const std::optional<ALScriptRecoveryEntry> took = store.leftFor("item:x");
        ensure("found", took.has_value());
        ensure("kept by this one", store.write(entry("item:x", "typed since")));

        ALScriptRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        ensure("let go of", store.letGo(parting));

        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("one entry", all.size(), size_t(1));
        ensure("set aside, as it stood", all.front().state == ALScriptRecoveryEntry::State::Discarded && all.front().text == "typed since, then closed");
        ensure("by this session", all.front().session == "mine");
        ensure("what it took up gone", !store.leftFor("item:x").has_value() && store.left().empty());
    }

    template<> template<>
    void alscriptrecovery_object::test<10>()
    {
        set_test_name("where the text cannot be set aside, nothing is let go of: its own entry and the one it took up both stay");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");
        ensure("left by another session", other.write(entry("item:x", "left before")));
        const std::optional<ALScriptRecoveryEntry> took = store.leftFor("item:x");
        ensure("kept by this one", store.write(entry("item:x", "typed since")));
        // A file where the folder of the discarded would be: nothing can be
        // written there.
        {
            LLFILE* blocking = LLFile::fopen(folder + "/discarded", LLFILE_MODE("wb"));
            ensure("made", blocking != nullptr);
            fclose(blocking);
        }

        ALScriptRecoveryStore::Parting parting;
        parting.key     = "item:x";
        parting.unsaved = entry("item:x", "typed since, then closed");
        parting.tookUp  = took;
        parting.settled = true;
        ensure("said to have failed", !store.letGo(parting));

        const std::vector<ALScriptRecoveryEntry> all = store.list();
        const auto own = std::find_if(all.begin(), all.end(), [](const ALScriptRecoveryEntry& one) { return one.session == "mine"; });
        ensure("its own entry still there", own != all.end() && own->text == "typed since");
        ensure("and the one it took up", store.leftFor("item:x").has_value());
        ensure("a set-aside that fails is said", !store.setAside(entry("item:y", "anything")));
    }

    template<> template<>
    void alscriptrecovery_object::test<11>()
    {
        set_test_name("a clean tab forgets its own entry, and lets go of what it took up only where a save could reach its text");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");
        ensure("left by another session", other.write(entry("item:x", "left before")));

        // Holding the kept text where nothing can save it -- the script may
        // no longer be changed, or never loaded: left to be offered again.
        ensure("kept by this one", store.write(entry("item:x", "typed since")));
        ALScriptRecoveryStore::Parting stuck;
        stuck.key     = "item:x";
        stuck.tookUp  = store.leftFor("item:x");
        stuck.settled = false;
        ensure("let go of", store.letGo(stuck));
        const std::vector<ALScriptRecoveryEntry> after = store.list();
        ensure("its own forgotten", std::none_of(after.begin(), after.end(), [](const ALScriptRecoveryEntry& one) { return one.session == "mine"; }));
        ensure("what it took up still offered", store.leftFor("item:x").has_value());
        ensure("nothing set aside", files("/discarded").empty());

        // Loaded and clean: the text it took up is what was saved.
        ALScriptRecoveryStore::Parting clean;
        clean.key     = "item:x";
        clean.tookUp  = store.leftFor("item:x");
        clean.settled = true;
        ensure("let go of", store.letGo(clean));
        ensure("what it took up gone", !store.leftFor("item:x").has_value());
        ensure("and nothing set aside", files("/discarded").empty());
    }

    template<> template<>
    void alscriptrecovery_object::test<12>()
    {
        set_test_name("a tab let go of before what it carried was put in: a text with no file of its own set aside, an entry on disk left as it was");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryStore other(folder, "theirs");

        // Moved from another window, or loading its item under what it held:
        // the text it carried is the only copy of what was typed, and this
        // session's entry for it was written by the tab it came from.
        ensure("kept by the tab it came from", store.write(entry("item:moved", "typed in the other window")));
        ALScriptRecoveryStore::Parting moved;
        moved.key      = "item:moved";
        moved.tookUp   = entry("item:moved", "typed in the other window");
        moved.carrying = true;
        ensure("let go of", store.letGo(moved));
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure_equals("one entry", all.size(), size_t(1));
        ensure("the carried text set aside", all.front().state == ALScriptRecoveryEntry::State::Discarded && all.front().text == "typed in the other window");

        // Opened to take up another session's entry, and closed before it
        // loaded: that entry is still what it was, to be offered again.
        ensure("left by another session", other.write(entry("item:left", "left before")));
        ALScriptRecoveryStore::Parting waiting;
        waiting.key      = "item:left";
        waiting.tookUp   = store.leftFor("item:left");
        waiting.carrying = true;
        ensure("let go of", store.letGo(waiting));
        ensure("the entry left", store.leftFor("item:left").has_value());
        ensure_equals("and nothing more set aside", files("/discarded").size(), size_t(1));

        // A new item opened with a text of its own: nothing to keep.
        ensure("kept", store.write(entry("item:new", "template")));
        ALScriptRecoveryStore::Parting fresh;
        fresh.key      = "item:new";
        fresh.carrying = true;
        ensure("let go of", store.letGo(fresh));
        const std::vector<ALScriptRecoveryEntry> after = store.list();
        ensure("forgotten", std::none_of(after.begin(), after.end(), [](const ALScriptRecoveryEntry& one) { return one.key == "item:new"; }));
    }

    template<> template<>
    void alscriptrecovery_object::test<13>()
    {
        set_test_name("a failed load is tried again a moment later, three times as long after each failure since, and a few times only");
        ensure_equals("after the first", ALScriptRecoveryRetry::delayAfter(1), ALScriptRecoveryRetry::FIRST);
        ensure_equals("after the second", ALScriptRecoveryRetry::delayAfter(2), ALScriptRecoveryRetry::FIRST * 3.0);
        ensure_equals("after the third", ALScriptRecoveryRetry::delayAfter(3), ALScriptRecoveryRetry::FIRST * 9.0);
        ensure_equals("none yet is as the first", ALScriptRecoveryRetry::delayAfter(0), ALScriptRecoveryRetry::FIRST);
        ensure("tried while there have been fewer failures than the tries", ALScriptRecoveryRetry::mayTry(0) && ALScriptRecoveryRetry::mayTry(ALScriptRecoveryRetry::TRIES - 1));
        ensure("and then waits to be asked", !ALScriptRecoveryRetry::mayTry(ALScriptRecoveryRetry::TRIES));
    }

    template<> template<>
    void alscriptrecovery_object::test<14>()
    {
        set_test_name("written as notation, whatever the text and history hold; set aside by the session named, a moment apart kept apart; the newest left first");
        ALScriptRecoveryStore store(folder, "mine");
        ALScriptRecoveryEntry awkward = entry("item:a", "tab\there, 'quoted' \"twice\", back\\slash,\r\nlines, \xC3\xA9, \xF4\x80\x80\x80");
        LLSD                  history;
        history["version"] = 2;
        history["undo"]    = LLSD::emptyArray().with(0, LLSD().with("label", "it's").with("edits", LLSD::emptyArray().with(0, LLSD::emptyArray().with(0, 0).with(1, 0).with(2, 0).with(3, 0).with(4, "").with(5, awkward.text))));
        history["redo"]    = LLSD::emptyArray();
        history["saved"]   = -1;
        awkward.history    = history;
        ensure("written", store.write(awkward));
        {
            const std::vector<std::string> names = files();
            ensure_equals("one file", names.size(), size_t(1));
            std::error_code   ec;
            const std::string written = LLFile::getContents(fsyspath(folder + "/" + names.front()), ec);
            ensure("as notation", written.rfind("<? llsd/notation ?>", 0) == 0);
        }
        const std::vector<ALScriptRecoveryEntry> back = store.list();
        ensure("the text as it was", back.size() == 1 && back.front().text == awkward.text);
        ensure("the history as it was", back.front().history["undo"][0]["edits"][0][5].asString() == awkward.text &&
                                             back.front().history["undo"][0]["label"].asString() == "it's");

        // Set aside for another session, and two a moment apart.
        ALScriptRecoveryEntry theirs = entry("item:b", "theirs");
        theirs.session               = "theirs";
        ensure("set aside", store.setAside(theirs));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        ensure("and another", store.setAside(entry("item:b", "mine, later")));
        ensure_equals("both kept", files("/discarded").size(), size_t(2));
        const std::vector<ALScriptRecoveryEntry> all = store.list();
        ensure("the session it was set aside for", std::any_of(all.begin(), all.end(), [](const ALScriptRecoveryEntry& one) { return one.text == "theirs" && one.session == "theirs"; }));
        ensure("this one where none was named", std::any_of(all.begin(), all.end(), [](const ALScriptRecoveryEntry& one) { return one.text == "mine, later" && one.session == "mine"; }));

        // Left by other sessions: the newest first.
        ALScriptRecoveryStore older(folder, "older");
        ALScriptRecoveryStore newer(folder, "newer");
        ensure("older", older.write(entry("item:c", "older")));
        // Apart by more than the centisecond a date is written to.
        ms_sleep(20);
        ensure("newer", newer.write(entry("item:d", "newer")));
        const std::vector<ALScriptRecoveryEntry> left = store.left();
        ensure("newest first", left.size() == 2 && left.front().text == "newer" && left.back().text == "older");
    }

    template<> template<>
    void alscriptrecovery_object::test<15>()
    {
        set_test_name("a discarded file whose name does not say when is pruned by what it says inside");
        ALScriptRecoveryStore store(folder, "mine");
        ensure("set aside", store.setAside(entry("item:a", "old")));
        // Renamed to say nothing of when, and then to say it badly.
        const std::vector<std::string> names = files("/discarded");
        ensure_equals("one", names.size(), size_t(1));
        const std::string stamped = folder + "/discarded/" + names.front();
        const std::string bare    = folder + "/discarded/0000.mine.llsd";
        ensure("renamed", LLFile::rename(stamped, bare) == 0);
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        ensure("young by what it says, it stays", LLFile::isfile(bare));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        ensure("old by what it says, it goes", !LLFile::isfile(bare));

        ensure("set aside", store.setAside(entry("item:b", "old")));
        const std::string garbled = folder + "/discarded/0001.mine.soon.llsd";
        ensure("renamed", LLFile::rename(folder + "/discarded/" + files("/discarded").front(), garbled) == 0);
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 30.0));
        ensure("a stamp that is no number read past", LLFile::isfile(garbled));
        store.prune(60.0, LLDate(LLDate::now().secondsSinceEpoch() + 120.0));
        ensure("and pruned by what it says", !LLFile::isfile(garbled));
    }
}

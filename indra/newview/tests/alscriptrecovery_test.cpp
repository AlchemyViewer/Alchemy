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

#include "../test/lltut.h"

#include <filesystem>

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
        ensure("discarded", store.discard("item:card"));
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
        ensure("discarded", store.discard("item:gone"));
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
}

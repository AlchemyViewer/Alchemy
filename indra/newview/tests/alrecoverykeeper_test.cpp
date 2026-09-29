/**
 * @file alrecoverykeeper_test.cpp
 * @brief Tests of a window's unsaved text kept in a recovery store of the test's.
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

#include "../alrecoverykeeper.h"

#include "../alrecovery.h"

#include "fsyspath.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <filesystem>
#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it.
class LLAvatarName;
const std::string gKeeperTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gKeeperTestAnonName;
}

namespace tut
{
    struct alrecoverykeeper_data
    {
        std::string                      folder;
        std::unique_ptr<ALRecoveryStore> store;
        // The window: its text, whether it is unsaved, and what was said.
        std::string                      text    = "saved";
        bool                             unsaved = false;
        S32                              failed  = 0;
        LLUUID                           object  = LLUUID::generateNewID();
        LLUUID                           item    = LLUUID::generateNewID();

        alrecoverykeeper_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alrecoverykeeper_" + LLUUID::generateNewID().asString())).string();
            std::filesystem::create_directories(fsyspath(folder));
            store = std::make_unique<ALRecoveryStore>(folder, "this-session");
            ALRecovery::useStore(store.get());
        }
        ~alrecoverykeeper_data()
        {
            ALRecovery::useStore(nullptr);
            store.reset();
            std::error_code ignored;
            std::filesystem::remove_all(fsyspath(folder), ignored);
        }

        ALRecoveryKeeper::Holder holder()
        {
            ALRecoveryKeeper::Holder one;
            one.entry = [this]() {
                ALRecoveryEntry entry;
                entry.object = object;
                entry.item   = item;
                entry.name   = "Card";
                entry.text   = text;
                return entry;
            };
            one.unsaved = [this]() { return unsaved; };
            one.failed  = [this]() { ++failed; };
            return one;
        }

        // This session's entry for the window, as the store has it.
        std::optional<ALRecoveryEntry> mine()
        {
            store->flush();
            for (ALRecoveryEntry& entry : store->list())
            {
                if (entry.session == "this-session" && entry.state == ALRecoveryEntry::State::Unsaved &&
                    entry.key == ALRecoveryStore::windowKeyOf(object, item) && store->load(entry))
                {
                    return entry;
                }
            }
            return std::nullopt;
        }
    };
    typedef test_group<alrecoverykeeper_data> alrecoverykeeper_group;
    typedef alrecoverykeeper_group::object    alrecoverykeeper_object;
    alrecoverykeeper_group                    alrecoverykeeper_instance("ALRecoveryKeeper");

    template<> template<>
    void alrecoverykeeper_object::test<1>()
    {
        set_test_name("written a moment after it first changed, under the window's own key, again for each change, and forgotten once saved");
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        ensure("the window's own key", ALRecoveryStore::isWindowKey(keeper.key()));
        keeper.pump(1, 10.0);
        ensure("nothing unsaved, nothing written", !mine());
        unsaved = true;
        text    = "typed";
        keeper.pump(2, 10.0);
        ensure("not at once", !mine());
        keeper.pump(3, 10.5);
        keeper.pump(3, 10.0 + ALRecoveryKeeper::DELAY);
        ensure("a moment after it first changed, whatever was typed meanwhile", mine() && mine()->text == "typed" && mine()->name == "Card");
        text = "typed more";
        keeper.pump(4, 12.0);
        keeper.pump(4, 12.0 + ALRecoveryKeeper::DELAY);
        ensure("again for the next change", mine() && mine()->text == "typed more");
        unsaved = false;
        keeper.pump(4, 20.0);
        ensure("saved: forgotten", !mine());
        ensure("nothing failed", failed == 0);
    }

    template<> template<>
    void alrecoverykeeper_object::test<2>()
    {
        set_test_name("thrown away: set aside, and the window's own gone; what another session left offered, taken up once the window's own is written, or turned down");
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        unsaved = true;
        text    = "thrown away";
        keeper.pump(1, 0.0);
        keeper.pump(1, ALRecoveryKeeper::DELAY);
        ensure("written", mine().has_value());
        ensure("set aside", keeper.setAside());
        ensure("its own gone", !mine());
        bool discarded = false;
        for (const ALRecoveryEntry& entry : store->list())
        {
            discarded |= entry.state == ALRecoveryEntry::State::Discarded && entry.key == keeper.key();
        }
        ensure("among the discarded", discarded);

        // What a session that ended left of the same item.
        {
            ALRecoveryStore before(folder, "ended-session");
            ALRecoveryEntry left;
            left.key    = keeper.key();
            left.object = object;
            left.item   = item;
            left.name   = "Card";
            left.text   = "left behind";
            ensure("left", before.write(left));
        }
        std::optional<ALRecoveryEntry> offered = keeper.left();
        ensure("offered", offered && offered->text == "left behind");
        text = "left behind";
        keeper.took(*offered, 7);
        ensure("the window's own written", mine() && mine()->text == "left behind");
        ensure("and the one taken up gone", !keeper.left());

        // Turned down: among the discarded, and offered no more.
        {
            ALRecoveryStore before(folder, "another-session");
            ALRecoveryEntry left;
            left.key  = keeper.key();
            left.item = item;
            left.text = "not wanted";
            before.write(left);
        }
        offered = keeper.left();
        ensure("offered again", offered && offered->text == "not wanted");
        keeper.turnDown(*offered);
        ensure("turned down: offered no more", !keeper.left());
    }

    template<> template<>
    void alrecoverykeeper_object::test<3>()
    {
        set_test_name("another item after a save: the old key forgotten, the new one kept; no item, nothing kept; a write that fails said once");
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        unsaved = true;
        keeper.pump(1, 0.0);
        keeper.pump(1, ALRecoveryKeeper::DELAY);
        ensure("written", mine().has_value());
        const LLUUID was = item;
        item             = LLUUID::generateNewID();
        keeper.setItem(object, item);
        store->flush();
        bool old_left = false;
        for (const ALRecoveryEntry& entry : store->list())
        {
            old_left |= entry.key == ALRecoveryStore::windowKeyOf(object, was);
        }
        ensure("the old key forgotten", !old_left);
        keeper.pump(2, 5.0);
        keeper.pump(2, 5.0 + ALRecoveryKeeper::DELAY);
        ensure("the new one kept", mine().has_value());
        keeper.setItem(object, LLUUID::null);
        ensure("no item, no key", keeper.key().empty());

        // Where the store cannot write: said once.
        const std::string blocked = folder + "/blocked";
        {
            llofstream file(blocked);
            file << "a file where a folder would be";
        }
        ALRecoveryStore nowhere(blocked, "this-session");
        ALRecovery::useStore(&nowhere);
        ALRecoveryKeeper failing(holder());
        failing.setItem(object, item);
        failing.pump(1, 0.0);
        failing.pump(1, ALRecoveryKeeper::DELAY);
        nowhere.flush();
        failing.pump(1, 10.0);
        ensure_equals("said", failed, 1);
        failing.pump(2, 11.0);
        failing.pump(2, 11.0 + ALRecoveryKeeper::DELAY);
        nowhere.flush();
        failing.pump(2, 20.0);
        ensure_equals("once", failed, 1);
        ALRecovery::useStore(store.get());
    }

    template<> template<>
    void alrecoverykeeper_object::test<4>()
    {
        set_test_name("the legacy editors' backups taken into the store as a session that ended, named as they named them, a notecard's text out of its format, and the files gone");
        const std::string temp = folder + "/temp";
        std::filesystem::create_directories(fsyspath(temp));
        {
            llofstream script(temp + "/Door Script-1A2B3C4D.lslbackup");
            script << "default { state_entry() {} }\n";
        }
        {
            llofstream card(temp + "/Settings-0000ABCD.ncbackup");
            card << "Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\nText length 10\nspeed = 2\n}\n";
        }
        {
            llofstream other(temp + "/unrelated.txt");
            other << "left alone";
        }
        ensure_equals("two taken", ALRecovery::importLegacyBackups(temp), 2);
        ensure("the backups gone", !LLFile::isfile(temp + "/Door Script-1A2B3C4D.lslbackup") && !LLFile::isfile(temp + "/Settings-0000ABCD.ncbackup"));
        ensure("anything else left alone", LLFile::isfile(temp + "/unrelated.txt"));
        bool script = false, card = false;
        for (ALRecoveryEntry& entry : store->left())
        {
            ensure("whole", store->load(entry));
            script |= entry.name == "Door Script" && !entry.notecard && entry.text == "default { state_entry() {} }\n" &&
                      entry.state == ALRecoveryEntry::State::Unsaved;
            card |= entry.name == "Settings" && entry.notecard && entry.text == "speed = 2\n";
        }
        ensure("the script, as left by a session that ended", script);
        ensure("the notecard, its text out of its format", card);
        ensure_equals("nothing more the next time", ALRecovery::importLegacyBackups(temp), 0);
    }
}

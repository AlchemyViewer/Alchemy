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
#include "newview_test_fakes.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace
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
            newview_test::useRecoveryWriter();
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

    TEST_CASE_METHOD(alrecoverykeeper_data, "ALRecoveryKeeper: unsaved text is written a moment after it changes until it is saved", "[ALRecoveryKeeper]")
    {
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        CHECK(ALRecoveryStore::isWindowKey(keeper.key()));
        keeper.pump(1, 10.0);
        UNSCOPED_INFO("nothing unsaved, nothing written");
        CHECK_FALSE(mine());
        unsaved = true;
        text    = "typed";
        keeper.pump(2, 10.0);
        UNSCOPED_INFO("not at once");
        CHECK_FALSE(mine());
        keeper.pump(3, 10.5);
        keeper.pump(3, 10.0 + ALRecoveryKeeper::DELAY);
        {
            INFO("a moment after it first changed, whatever was typed meanwhile");
            const std::optional<ALRecoveryEntry> typed = mine();
            REQUIRE(typed);
            CHECK(typed->text == "typed");
            CHECK(typed->name == "Card");
        }
        text = "typed more";
        keeper.pump(4, 12.0);
        keeper.pump(4, 12.0 + ALRecoveryKeeper::DELAY);
        {
            INFO("again for the next change");
            const std::optional<ALRecoveryEntry> again = mine();
            REQUIRE(again);
            CHECK(again->text == "typed more");
        }
        unsaved = false;
        keeper.pump(4, 20.0);
        UNSCOPED_INFO("saved: forgotten");
        CHECK_FALSE(mine());
        CHECK(failed == 0);
    }

    TEST_CASE_METHOD(alrecoverykeeper_data, "ALRecoveryKeeper: setAside discards and what another session left is taken up or turned down", "[ALRecoveryKeeper]")
    {
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        unsaved = true;
        text    = "thrown away";
        keeper.pump(1, 0.0);
        keeper.pump(1, ALRecoveryKeeper::DELAY);
        REQUIRE(mine().has_value());
        CHECK(keeper.setAside());
        UNSCOPED_INFO("its own gone");
        CHECK_FALSE(mine());
        bool discarded = false;
        for (const ALRecoveryEntry& entry : store->list())
        {
            discarded |= entry.state == ALRecoveryEntry::State::Discarded && entry.key == keeper.key();
        }
        CHECK(discarded);

        // What a session that ended left of the same item.
        {
            ALRecoveryStore before(folder, "ended-session");
            ALRecoveryEntry left;
            left.key    = keeper.key();
            left.object = object;
            left.item   = item;
            left.name   = "Card";
            left.text   = "left behind";
            CHECK(before.write(left));
        }
        std::optional<ALRecoveryEntry> offered = keeper.left();
        REQUIRE(offered);
        CHECK(offered->text == "left behind");
        text = "left behind";
        keeper.took(*offered, 7);
        {
            INFO("the window's own written");
            const std::optional<ALRecoveryEntry> own = mine();
            REQUIRE(own);
            CHECK(own->text == "left behind");
        }
        UNSCOPED_INFO("and the one taken up gone");
        CHECK_FALSE(keeper.left());

        // Turned down: among the discarded, and offered no more.
        {
            ALRecoveryStore before(folder, "another-session");
            ALRecoveryEntry left;
            left.key  = keeper.key();
            left.item = item;
            left.text = "not wanted";
            CHECK(before.write(left));
        }
        offered = keeper.left();
        REQUIRE(offered);
        CHECK(offered->text == "not wanted");
        keeper.turnDown(*offered);
        UNSCOPED_INFO("turned down: offered no more");
        CHECK_FALSE(keeper.left());
        // Among the discarded as the session that left it: told apart so from
        // this session's own, set aside above.
        bool turned_down = false;
        for (const ALRecoveryEntry& entry : store->list())
        {
            turned_down |= entry.state == ALRecoveryEntry::State::Discarded && entry.key == keeper.key() &&
                           entry.session == "another-session";
        }
        CHECK(turned_down);
    }

    TEST_CASE_METHOD(alrecoverykeeper_data, "ALRecoveryKeeper: a new item or none moves the key and a failed write is said once", "[ALRecoveryKeeper]")
    {
        ALRecoveryKeeper keeper(holder());
        keeper.setItem(object, item);
        unsaved = true;
        keeper.pump(1, 0.0);
        keeper.pump(1, ALRecoveryKeeper::DELAY);
        REQUIRE(mine().has_value());
        const LLUUID was = item;
        item             = LLUUID::generateNewID();
        keeper.setItem(object, item);
        store->flush();
        bool old_left = false;
        for (const ALRecoveryEntry& entry : store->list())
        {
            old_left |= entry.key == ALRecoveryStore::windowKeyOf(object, was);
        }
        UNSCOPED_INFO("the old key forgotten");
        CHECK_FALSE(old_left);
        keeper.pump(2, 5.0);
        keeper.pump(2, 5.0 + ALRecoveryKeeper::DELAY);
        UNSCOPED_INFO("the new one kept");
        CHECK(mine().has_value());
        keeper.setItem(object, LLUUID::null);
        UNSCOPED_INFO("no item, no key");
        CHECK(keeper.key().empty());

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
        UNSCOPED_INFO("the failed write said");
        CHECK(failed == 1);
        failing.pump(2, 11.0);
        failing.pump(2, 11.0 + ALRecoveryKeeper::DELAY);
        nowhere.flush();
        failing.pump(2, 20.0);
        UNSCOPED_INFO("and not said again");
        CHECK(failed == 1);
        ALRecovery::useStore(store.get());
    }

    TEST_CASE_METHOD(alrecoverykeeper_data, "ALRecoveryKeeper: the legacy editors' backups are imported as an ended session and removed", "[ALRecoveryKeeper]")
    {
        const std::string temp = folder + "/temp";
        std::filesystem::create_directories(fsyspath(temp));
        // Written as the editors wrote them, byte for byte.
        {
            llofstream script(temp + "/Door Script-1A2B3C4D.lslbackup", std::ios::out | std::ios::binary);
            script << "default { state_entry() {} }\n";
        }
        {
            llofstream card(temp + "/Settings-0000ABCD.ncbackup", std::ios::out | std::ios::binary);
            card << "Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\nText length 10\nspeed = 2\n}\n";
        }
        {
            llofstream other(temp + "/unrelated.txt");
            other << "left alone";
        }
        CHECK(ALRecovery::importLegacyBackups(temp) == 2);
        CHECK_FALSE(LLFile::isfile(temp + "/Door Script-1A2B3C4D.lslbackup"));
        CHECK_FALSE(LLFile::isfile(temp + "/Settings-0000ABCD.ncbackup"));
        UNSCOPED_INFO("anything else left alone");
        CHECK(LLFile::isfile(temp + "/unrelated.txt"));
        bool script = false, card = false;
        for (ALRecoveryEntry& entry : store->left())
        {
            CAPTURE(entry.name);
            REQUIRE(store->load(entry));
            script |= entry.name == "Door Script" && !entry.notecard && entry.text == "default { state_entry() {} }\n" &&
                      entry.state == ALRecoveryEntry::State::Unsaved;
            card |= entry.name == "Settings" && entry.notecard && entry.text == "speed = 2\n";
        }
        UNSCOPED_INFO("the script, as left by a session that ended");
        CHECK(script);
        UNSCOPED_INFO("the notecard, its text out of its format");
        CHECK(card);
        UNSCOPED_INFO("nothing more the next time");
        CHECK(ALRecovery::importLegacyBackups(temp) == 0);
    }
}

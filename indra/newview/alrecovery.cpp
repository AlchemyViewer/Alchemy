/**
 * @file alrecovery.cpp
 * @brief The account's store of unsaved texts, where what each editor kept goes back to, and what is offered back at login.
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

#include "llviewerprecompiledheaders.h"

#include "alrecovery.h"

#include "alscriptmessages.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "llfile.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"

#include <iterator>
#include <memory>
#include <sstream>

namespace
{
    // How long discarded text is kept before it goes for good.
    const F64 DISCARDED_KEPT = 7.0 * 24.0 * 60.0 * 60.0;

    // A test's store, in place of the account's.
    ALRecoveryStore* sGivenStore = nullptr;

    // The viewer's own windows' way of taking up what they kept.
    ALRecovery::window_t& ownWindows()
    {
        static ALRecovery::window_t window;
        return window;
    }

    // The account's store, one for the process, made again where another
    // account has logged in since; let go of in the viewer's cleanup --
    // what it has waiting written first -- before the writer it writes on
    // stops.
    class AccountRecovery final : public LLSingleton<AccountRecovery>
    {
        LLSINGLETON(AccountRecovery);
        void cleanupSingleton() override { mStore.reset(); }

    public:
        ALRecoveryStore* storeFor(const std::string& directory)
        {
            if (!mStore || mMadeFor != directory)
            {
                LLFile::mkdir(directory);
                mStore   = std::make_unique<ALRecoveryStore>(directory, mSession);
                mMadeFor = directory;
                // What was discarded long ago goes for good.
                mStore->prune(DISCARDED_KEPT);
            }
            return mStore.get();
        }

    private:
        std::unique_ptr<ALRecoveryStore> mStore;
        std::string                            mMadeFor;
        const std::string                      mSession = LLUUID::generateNewID().asString();
    };

    AccountRecovery::AccountRecovery()
    {
        // Asked for here, so that it is let go of after this.
        ALRecoveryWriter::getInstance();
    }
}

// static
ALRecoveryStore* ALRecovery::store()
{
    if (sGivenStore)
    {
        return sGivenStore;
    }
    if (!gDirUtilp || gDirUtilp->getLindenUserDir().empty() || AccountRecovery::wasDeleted())
    {
        return nullptr;
    }
    // Under the name it had while only Script Studio kept texts there,
    // taken over the first time.
    const std::string folder = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "recovery");
    const std::string before = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "script_studio_recovery");
    if (!LLFile::isdir(folder) && LLFile::isdir(before))
    {
        LLFile::rename(before, folder);
    }
    return AccountRecovery::instance().storeFor(folder);
}

// static
void ALRecovery::useStore(ALRecoveryStore* store)
{
    sGivenStore = store;
}

// static
void ALRecovery::takeWindowsTo(window_t window)
{
    ownWindows() = std::move(window);
}

// static
bool ALRecovery::toOwnWindow(const Entry& entry)
{
    const window_t& window = ownWindows();
    return ALRecoveryStore::isWindowKey(entry.key) && window && window(entry);
}

// static
std::string ALRecovery::nameOf(const Entry& entry)
{
    return entry.name.empty() ? gDirUtilp->getBaseFileName(entry.file) : entry.name;
}

// static
S32 ALRecovery::importLegacyBackups(const std::string& folder)
{
    ALRecoveryStore* kept = store();
    if (!kept || folder.empty())
    {
        return 0;
    }
    const std::string dir = folder.back() == '/' || folder.back() == '\\' ? folder : folder + gDirUtilp->getDirDelimiter();
    // Written as a session of their own, which the one logging in finds
    // left, as it finds what any session that ended left.
    ALRecoveryStore legacy(kept->directory(), "legacy-backups");
    S32             taken = 0;
    for (const auto& [mask, notecard] : { std::pair{ "*.lslbackup", false }, std::pair{ "*.ncbackup", true } })
    {
        LLDirIterator files(dir, mask);
        std::string   file;
        while (files.next(file))
        {
            const std::string path = dir + file;
            std::string       text;
            {
                llifstream in(path.c_str(), std::ios::in | std::ios::binary);
                if (!in.is_open())
                {
                    continue;
                }
                text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
            // A notecard was backed up in its format; its items are not in
            // the backup, and their characters stand for nothing now.
            if (notecard)
            {
                LLNotecard         parsed(LLNotecard::MAX_SIZE);
                std::istringstream in(text);
                if (parsed.importStream(in))
                {
                    text = parsed.getText();
                }
            }
            // Named as the backup named it: the item's name, then a dash and
            // eight digits of a checksum of its id.
            std::string name = gDirUtilp->getBaseFileName(file, true);
            if (const size_t dash = name.find_last_of('-'); dash != std::string::npos && dash != 0 && dash == name.size() - 9)
            {
                name.erase(dash);
            }
            LLStringUtil::trim(name);
            Entry entry;
            entry.key      = "legacy:" + file;
            entry.state    = Entry::State::Unsaved;
            entry.name     = name.empty() ? file : name;
            entry.notecard = notecard;
            entry.lua      = !notecard && ALScriptMessages::looksLikeLua(text);
            entry.text     = std::move(text);
            if (legacy.write(std::move(entry)))
            {
                LLFile::remove(path);
                ++taken;
            }
        }
    }
    return taken;
}

// static
ALRecovery::Offers ALRecovery::offersAt(const ALRecoveryStore& store, bool studio_open)
{
    Offers offers;
    for (Entry& entry : store.left())
    {
        if (entry.state == Entry::State::Unsaved)
        {
            offers.unsaved.push_back(std::move(entry));
        }
        else if (entry.state == Entry::State::Kept && !studio_open)
        {
            offers.kept.push_back(std::move(entry));
        }
    }
    return offers;
}

namespace
{
    // The first few names of entries, for a question to say.
    std::string namesOf(const std::vector<ALRecoveryEntry>& entries)
    {
        std::string names;
        for (size_t i = 0; i < entries.size() && i < 5; ++i)
        {
            names += (names.empty() ? "" : ", ") + ALRecovery::nameOf(entries[i]);
        }
        if (entries.size() > 5)
        {
            names += ", ...";
        }
        return names;
    }
}

// static
void ALRecovery::offer(studio_t studio, bool studio_open)
{
    ALRecoveryStore* kept = store();
    if (!kept)
    {
        return;
    }
    const Offers offers = offersAt(*kept, studio_open);
    if (!offers.kept.empty())
    {
        // Kept on purpose: they open with the studio, now or whenever it
        // next opens; this only says so and offers to open it.
        LLSD args;
        args["COUNT"] = static_cast<S32>(offers.kept.size());
        args["NAMES"] = namesOf(offers.kept);
        LLNotificationsUtil::add(offers.kept.size() == 1 ? "ScriptStudioKeptOne" : "ScriptStudioKept", args, LLSD(),
                                 [studio](const LLSD& notification, const LLSD& response) {
                                     if (LLNotificationsUtil::getSelectedOption(notification, response) == 0 && studio)
                                     {
                                         studio({});
                                     }
                                 });
    }
    const std::vector<Entry>& unsaved = offers.unsaved;
    if (unsaved.empty())
    {
        return;
    }
    const std::string names = namesOf(unsaved);
    // Offered now: left alone, they go a while after, as the discarded
    // do, rather than being kept for ever.
    kept->markOffered(unsaved);
    LLSD args;
    args["COUNT"] = static_cast<S32>(unsaved.size());
    args["NAMES"] = names;
    LLNotificationsUtil::add(unsaved.size() == 1 ? "RecoveredUnsavedOne" : "RecoveredUnsaved", args, LLSD(),
                             [studio](const LLSD& notification, const LLSD& response) {
                                 const S32              option = LLNotificationsUtil::getSelectedOption(notification, response);
                                 ALRecoveryStore* kept   = store();
                                 if (!kept || option == 1)
                                 {
                                     return;
                                 }
                                 // As they are now, not as they were when asked.
                                 std::vector<Entry> now;
                                 for (Entry& entry : kept->left())
                                 {
                                     if (entry.state == Entry::State::Unsaved)
                                     {
                                         now.push_back(std::move(entry));
                                     }
                                 }
                                 if (option == 2)
                                 {
                                     for (const Entry& entry : now)
                                     {
                                         kept->discard(entry);
                                     }
                                     return;
                                 }
                                 // What the viewer's own windows kept goes back
                                 // to them; the studio opens only for the rest.
                                 std::vector<Entry> rest;
                                 for (const Entry& entry : now)
                                 {
                                     Entry whole = entry;
                                     if (!(ALRecoveryStore::isWindowKey(entry.key) && kept->load(whole) && toOwnWindow(whole)))
                                     {
                                         rest.push_back(entry);
                                     }
                                 }
                                 if (!rest.empty() && studio)
                                 {
                                     studio(rest);
                                 }
                             });
}

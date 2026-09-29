/**
 * @file alrecovery.cpp
 * @brief The account's store of unsaved texts, where what each editor kept goes back to, and what is offered back at login.
 *
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

#include "alscriptstudiorecovery.h"
#include "lldir.h"
#include "llfile.h"
#include "llnotificationsutil.h"

#include <memory>

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
    return AccountRecovery::instance().storeFor(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "script_studio_recovery"));
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
void ALRecovery::offer(std::function<ALScriptStudioRecovery*()> studio, bool studio_open)
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
                                         studio();
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
    LLNotificationsUtil::add(unsaved.size() == 1 ? "ScriptStudioRecoveredOne" : "ScriptStudioRecovered", args, LLSD(),
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
                                 ALScriptStudioRecovery* recovery = rest.empty() || !studio ? nullptr : studio();
                                 if (!recovery)
                                 {
                                     return;
                                 }
                                 for (const Entry& entry : rest)
                                 {
                                     recovery->recover(entry);
                                 }
                             });
}

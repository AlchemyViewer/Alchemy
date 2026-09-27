/**
 * @file alscriptincluderesolver.h
 * @brief What an include or a require names: found in the object, the inventory or the disk, and read.
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

#pragma once

#include "aldiskcache.h"
#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alscriptpreprocessor.h"
#include "alscripttextcache.h"
#include "alscriptworkspace.h"
#include "llinventorymodel.h"
#include "llstl.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <optional>
#include <string>
#include <vector>

// What an `#include` or a SLua `require` names, found as the preprocessor
// finds it -- in the object holding the script, in the agent's inventory,
// or in a folder on disk someone blessed, in the order the setting says --
// with the `.luaurc` chain that governs a SLua script, and each text read:
// from the texts fetched, or the disk. Nothing is fetched here: what is in
// the world and not in hand is Pending, and wanted. Main thread only; the
// preprocessor's (ALScriptPreprocessor), which runs the jobs.
class ALScriptIncludeResolver
{
public:
    typedef ALScriptPreprocessor::Request                                            Request;
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> wanted_t;

    explicit ALScriptIncludeResolver(ALScriptTextCache& texts);

    // As ALScriptPreprocessor says of each (heldText, heldPaths, lookUp,
    // includedBy, nearby, moduleFolders, configOf).
    bool                                             heldText(const std::string& path, std::string& text) const;
    std::vector<std::string>                         heldPaths() const;
    ALPreprocessor::Found                            lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out);
    std::vector<ALPreprocessor::Include>             includedBy(const Request& request);
    std::vector<std::string>                         nearby(const Request& request, size_t most);
    std::vector<std::pair<std::string, std::string>> moduleFolders(const Request& request);
    bool                                             configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base = nullptr);

    // What a prim holds, as the region said; one that did not answer; and
    // what is known of each.
    void heard(const LLUUID& prim, std::vector<ALScriptWorkspace::Item> items)
    {
        mContents[prim] = std::move(items);
        mUnanswered.erase(prim);
    }
    void notAnswered(const LLUUID& prim) { mUnanswered.insert(prim); }
    bool listed(const LLUUID& prim) const { return mContents.contains(prim); }
    bool unanswered(const LLUUID& prim) const { return mUnanswered.contains(prim); }

    // Whether an include or a module so named is in the world a script is
    // in -- its object, the inventory -- whether or not it may be taken.
    bool                                  inWorld(const Request& request, const std::string& name);
    // `retry` asks again for what failed before rather than taking the
    // failure for an answer: what a run's first round does, since an
    // include that was not there may be there now.
    // `alias_folders` gathers the folders the aliases of a `.luaurc` on
    // disk bless, for the rest of a run.
    ALPreprocessor::Found  resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Request& request, wanted_t* wanted,
                                   bool retry, std::vector<std::string>* alias_folders = nullptr);
    // The `.luaurc` files over a file, by the file's identity, nearest
    // first, as Luau reads a chain of them from the top down: a file on
    // disk's up the directories to the root; a script in the world's up
    // its folders or in its object -- only where includes are taken from
    // the world -- and above those, the one at the top of each of the
    // scripter's include folders, in their order, since a script in the
    // world has no folders on disk to look up through and its modules are
    // read from those. Each with its identity and its text, fetched like
    // an include where it is in the world: Pending while any is on its
    // way. Each alias is its own file's, taken from beside it.
    struct Config
    {
        std::string path;
        std::string text;
    };
    ALPreprocessor::Found  configsFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry, std::vector<Config>& out);

private:
    // Something an include name could mean, in the order tried.
    struct Candidate
    {
        std::string path;
        std::string name;
        ALScriptRef ref;
        LLUUID      assetId;
        // A file on disk, read on the spot.
        std::string file;
    };
    // `unknown` says the object's contents have not been listed yet, so
    // a name not found may still be there; an object asked that did not
    // answer is not unknown, and holds nothing.
    std::vector<Candidate> candidatesFor(const ALPreprocessor::Ask& ask, const Request& request, const std::vector<std::string>& alias_folders,
                                         bool& unknown);
    // The folders on disk an include asked for may come from: the
    // scripter's include folders while disk includes are on, and what a
    // `.lslrc` or `.luaurc` on disk lists -- `alias_folders` being those
    // the run's aliases have blessed so far. Nothing else, ever.
    // Kept a moment in the disk's cache (ALDiskCache), with what they admit.
    ALDiskCache::Blessed&  blessedFor(const ALPreprocessor::Ask& ask, const Request& request, const std::vector<std::string>& alias_folders);
    // The scripter's own include folders, blessed, while disk includes are
    // on; nothing otherwise. What a configuration on disk may reach past
    // its own folder.
    ALDiskCache::Blessed&  ownFolders();
    // The include folders as the setting holds them, read once each time
    // the settings that decide the disk move; and how often they have.
    const std::vector<std::string>& ownIncludeFolders();
    U32                             diskGeneration();
    // Every script and notecard of a name in the inventory.
    LLInventoryModel::item_array_t        namedItems(const std::string& name);
    // An include's text, from the cache or a file; Pending, and wanted,
    // where it is in the world and not in hand yet.
    ALPreprocessor::Found  textOf(const Candidate& candidate, wanted_t* wanted, bool retry, std::string& text, std::string& assetId);

    // The files on disk a run has admitted, by identity: what the studio
    // may ask the text of, and nothing else on the disk.
    wanted_t                                                                        mAdmitted;
    // What the disk said, kept a moment: every check asks it again for each
    // include. The settings that decide it counted, and the folders they
    // name.
    ALDiskCache                                                                     mDisk;
    U32                                                                             mDiskGeneration = 1;
    std::optional<std::vector<std::string>>                                         mOwnFolders;
    std::vector<boost::signals2::scoped_connection>                                 mDiskSettings;
    // What each prim was last said to hold.
    boost::unordered_flat_map<LLUUID, std::vector<ALScriptWorkspace::Item>>         mContents;
    // The prims asked what they hold that did not answer -- not in time,
    // or not in view -- and never had: nothing is looked for in one, and
    // a run over a script in one says why, until a save asks again and
    // it answers.
    boost::unordered_flat_set<LLUUID>                                               mUnanswered;
    // The texts fetched, and what failed to come: the preprocessor's.
    ALScriptTextCache&                                                              mTexts;
};

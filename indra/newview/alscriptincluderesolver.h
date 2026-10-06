/**
 * @file alscriptincluderesolver.h
 * @brief The viewer's side of finding an include: the inventory and the objects asked, the settings read.
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

#include "alincludesearch.h"
#include "alscriptpreprocessor.h"
#include "alscripttypes.h"
#include "llinventorymodel.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <optional>
#include <string>
#include <vector>

// The viewer's side of finding what an include or a require names: the
// agent's inventory and what each object holds asked (ALIncludeWorld),
// and the settings read, for the search itself (ALIncludeSearch, in
// alscript), which the preprocessor's questions go to. Main thread only.
class ALScriptIncludeResolver final : public ALIncludeWorld
{
public:
    typedef ALScriptPreprocessor::Request Request;
    typedef ALIncludeSearch::wanted_t     wanted_t;
    typedef ALIncludeSearch::Config       Config;

    explicit ALScriptIncludeResolver(ALScriptTextCache& texts);

    // As ALIncludeSearch says of each, for a script the preprocessor is
    // asked about.
    ALPreprocessor::Found resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Request& request, wanted_t* wanted,
                                  bool retry, std::vector<std::string>* alias_folders = nullptr);
    ALPreprocessor::Found configsFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry, std::vector<Config>& out);
    // As ALScriptPreprocessor says of each (heldText, heldPaths, lookUp,
    // includedBy, nearby, moduleFolders, configOf).
    bool                                             heldText(const std::string& path, std::string& text) const;
    std::vector<std::string>                         heldPaths() const;
    ALPreprocessor::Found                            lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out);
    std::vector<ALPreprocessor::Include>             includedBy(const Request& request);
    std::vector<std::string>                         nearby(const Request& request, size_t most);
    std::vector<std::pair<std::string, std::string>> moduleFolders(const Request& request);
    bool                                             configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base = nullptr);
    // Whether an include or a module so named is in the world a script is
    // in -- its object, the inventory -- whether or not it may be taken.
    bool inWorld(const Request& request, const std::string& name);

    // What a prim holds, as the region said; one that did not answer; and
    // what is known of each.
    // Past a few hundred prims, what all of them said let go of, and each
    // asked again as a script in it is run.
    static constexpr size_t PRIMS_KEPT = 256;
    void heard(const LLUUID& prim, std::vector<ALScriptContents::Item> items)
    {
        if (mContents.size() >= PRIMS_KEPT && !mContents.contains(prim))
        {
            mContents.clear();
        }
        mContents[prim] = std::move(items);
        mUnanswered.erase(prim);
    }
    void notAnswered(const LLUUID& prim)
    {
        if (mUnanswered.size() >= PRIMS_KEPT && !mUnanswered.contains(prim))
        {
            mUnanswered.clear();
        }
        mUnanswered.insert(prim);
    }
    bool listed(const LLUUID& prim) const { return mContents.contains(prim); }
    bool unanswered(const LLUUID& prim) const { return mUnanswered.contains(prim); }

    // ALIncludeWorld
    std::vector<Item>     inObject(const std::string& asking, const std::string& item_name, bool& unknown) override;
    std::vector<Item>     inInventory(const std::string& item_name, const std::vector<std::string>& folders, const std::string& from) override;
    ALPreprocessor::Found configsOver(const std::string& from, const std::string& name, std::vector<Item>& out) override;
    bool                  folderOf(const std::string& item, std::string& folder, std::string& name) override;
    ALPreprocessor::Found folderAbove(const std::string& folder, std::string& out) override;
    ALPreprocessor::Found named(const std::string& folder, const std::string& name, std::vector<Item>& items, std::string& subfolder) override;

private:
    // Where a name is looked for, as the settings say now; who asks.
    ALIncludeSearch::Where         where();
    static ALIncludeSearch::Asking askingOf(const Request& request);
    // The include folders as the setting holds them, read once each time
    // the settings that decide the disk move; and how often they have.
    const std::vector<std::string>& ownIncludeFolders();
    // The SLua aliases the studio names, read once likewise.
    const std::vector<ALScriptPreprocessor::StudioAlias>& studioAliases();
    U32                             diskGeneration();
    // Every script and notecard of a name in the inventory.
    LLInventoryModel::item_array_t namedItems(const std::string& name);

    // The texts fetched: the preprocessor's.
    ALScriptTextCache& mTexts;
    ALIncludeSearch    mSearch;
    U32                                             mDiskGeneration = 1;
    std::optional<std::vector<std::string>>         mOwnFolders;
    std::optional<std::vector<ALScriptPreprocessor::StudioAlias>> mStudioAliases;
    std::vector<boost::signals2::scoped_connection> mDiskSettings;
    // What each prim was last said to hold.
    boost::unordered_flat_map<LLUUID, std::vector<ALScriptContents::Item>> mContents;
    // The prims asked what they hold that did not answer -- not in time,
    // or not in view -- and never had: nothing is looked for in one, and
    // a run over a script in one says why, until a save asks again and it
    // answers.
    boost::unordered_flat_set<LLUUID> mUnanswered;
};

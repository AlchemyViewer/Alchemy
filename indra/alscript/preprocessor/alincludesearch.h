/**
 * @file alincludesearch.h
 * @brief What an include or a require names, found as the preprocessor finds it: the world asked, the disk read.
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
 */

#pragma once

#include "aldiskcache.h"
#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alrequirenavigation.h"
#include "alscripttextcache.h"
#include "llstl.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tut { struct alincludesearch_data; }

// What finding an include asks of the world a script is in, which only the
// viewer can answer -- its inventory, what an object holds -- and a test
// fakes. Items by their identities (ALIncludeIdentity).
class ALIncludeWorld
{
public:
    // Something in the world a name may mean: its identity, its name, and
    // the asset its text is.
    struct Item
    {
        std::string path;
        std::string name;
        LLUUID      assetId;
    };

    virtual ~ALIncludeWorld() = default;
    // What the object the asking script is in holds of an item's name, in
    // the order it lists them; nothing for a script not in an object.
    // `unknown` where the object has not said what it holds, so that the
    // name may still be there; an object asked that did not answer is not
    // unknown, and holds nothing.
    virtual std::vector<Item> inObject(const std::string& asking, const std::string& item_name, bool& unknown) = 0;
    // What the inventory holds of an item's name, scripts before notecards:
    // under the folders `folders` names, where any of the items is -- from
    // the folder of the item `from`, where they begin with `.` or `..` --
    // and all of them otherwise.
    virtual std::vector<Item> inInventory(const std::string& item_name, const std::vector<std::string>& folders, const std::string& from) = 0;
    // The configurations of `name` in the world over the item `from`,
    // nearest first: the first so named in each folder up an inventory
    // item's, or the one in its object. Pending where the object has not
    // said what it holds.
    virtual ALPreprocessor::Found configsOver(const std::string& from, const std::string& name, std::vector<Item>& out) = 0;

    // A SLua require walked through the world (ALRequireNavigation): the
    // folder an item is in -- its inventory folder, or its object's
    // contents, which are one folder -- by an id of this world's, with the
    // item's name; false for an item not known. The folder above one, No
    // at the top: an object's contents are no folder's. And what a folder
    // holds by a name: its items so named, scripts before notecards, and
    // its folder so named; Pending where what it holds is not known yet --
    // an object that has not said, an inventory folder not fetched.
    virtual bool                  folderOf(const std::string& item, std::string& folder, std::string& name) = 0;
    virtual ALPreprocessor::Found folderAbove(const std::string& folder, std::string& out) = 0;
    virtual ALPreprocessor::Found named(const std::string& folder, const std::string& name, std::vector<Item>& items, std::string& subfolder) = 0;
    // What a folder holds, for what a path typed may go on with: its
    // scripts and notecards, and its folders' names.
    virtual ALPreprocessor::Found contents(const std::string& folder, std::vector<Item>& items, std::vector<std::string>& folders) = 0;
};

// What an `#include` names, found as the preprocessor finds it -- in the
// object holding the script, in the agent's inventory, or in a folder on
// disk someone blessed, in the order the settings say -- and what a SLua
// `require` names, as the plugin's rules have it (LAD9): walked by Luau's
// own navigator from the file asking, with no search (ALRequireNavigation).
// With the `.luaurc` chain that governs a SLua script, and each text read:
// from the texts fetched, or the disk (ALDiskCache). Nothing is fetched
// here: what is in the world and not in hand is Pending, and wanted.
//
// A require walks the places as one tree: folders on disk, inventory
// folders, an object's contents as one folder -- the world's only where
// world includes are on (LAD10); with them off a place in the world holds
// nothing. Above a script in the world, or one in no place, the
// `.luaurc` at the top of each of the scripter's include folders, in their
// order, as the chain of configurations goes on. A `.luaurc` on disk
// blesses where its aliases reach, for the run, as far as a configuration
// may (ALDiskIncludes::blessFromConfig).
class ALIncludeSearch
{
public:
    // Where a name is looked for, as the settings say, read afresh for
    // each question.
    struct Where
    {
        // The places in the order they are looked in: "inventory",
        // "object", "disk".
        std::vector<std::string> order;
        // The object and the inventory, only where the scripter let them
        // in; the disk, and the scripter's folders on it.
        bool                     world = false;
        bool                     disk  = false;
        std::vector<std::string> folders;
        // SLua's studio aliases (LA22): each name, in lower case, and the
        // folder on disk it stands for, which naming it blesses for a
        // require -- not for an include's search -- or a folder of the
        // world by the world's own id, an inventory folder, read only
        // while the world is let in. A require goes to them after the
        // `.luaurc` chain, so that a project's own wins.
        std::vector<std::pair<std::string, std::string>> aliases;
        // What the disk's settings have been through, and the time: what
        // the disk said is kept a moment (ALDiskCache).
        U32                      generation = 0;
        F64                      now        = 0.0;
    };
    // Who asks: a script's identity, and its language.
    struct Asking
    {
        std::string self;
        bool        lua = false;
    };
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> wanted_t;
    // A configuration of a chain, by its identity, and its text as a
    // `.luaurc` reads: a `.config.luau`'s is what it returned.
    struct Config
    {
        std::string path;
        std::string text;
    };

    // How many files on disk are remembered as admitted at once.
    static constexpr size_t ADMITTED_KEPT = 4096;

    ALIncludeSearch(ALScriptTextCache& texts, ALIncludeWorld& world);

    // What a name stands for: Yes with its identity and text; Pending with
    // its identity where it is in the world and not in hand -- `wanted`
    // gathers it -- and with none where the object has not said what it
    // holds; No where nothing is so named. `retry` asks again for what
    // failed before. `alias_folders` gathers the folders the aliases of a
    // `.luaurc` on disk bless -- or a module's files, each alone -- for the
    // rest of a run.
    ALPreprocessor::Found resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Asking& asking, const Where& where,
                                  wanted_t* wanted, bool retry, std::vector<std::string>* alias_folders = nullptr);
    // The `.luaurc` files over a file, nearest first, as Luau reads a chain
    // of them: a file on disk's up the directories to the root; a script in
    // the world's up its folders or in its object -- only where the world
    // is let in -- and above those, the one at the top of each of the
    // scripter's include folders, in their order. Pending while any is on
    // its way.
    ALPreprocessor::Found configsFor(const std::string& from, const Asking& asking, const Where& where, wanted_t* wanted, bool retry,
                                     std::vector<Config>& out);
    // What could follow a path typed so far in a string that names a file
    // in `from` -- the script asking where empty: for a SLua require, as
    // Luau's own suggester walks it by the require's rules
    // (ALRequireNavigation::suggest); for an include, the names under the
    // folders its search looks in on disk, the file's own first, each named
    // with its extension, and the folders. What the world holds is offered
    // only while world includes are on.
    std::vector<ALRequireNavigation::Suggestion> suggest(const std::string& from, const std::string& typed, bool require, const Asking& asking,
                                                         const Where& where);
    // resolve() with nothing fetched, and the aliases' folders blessed for
    // the asking.
    ALPreprocessor::Found lookUp(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Asking& asking, const Where& where);
    // Every file `text` includes or requires, and theirs in turn, as a run
    // would find them now, each once, in the order met.
    std::vector<ALPreprocessor::Include> includedBy(const std::string& text, const Asking& asking, const Where& where);
    // The folders on disk a require or an include from a script reads,
    // each with what a name under it starts with (ALScriptPreprocessor::
    // moduleFolders).
    std::vector<std::pair<std::string, std::string>> moduleFolders(const Asking& asking, const Where& where);
    // What the `.luaurc` chain of a SLua script says over `base`; false,
    // and the base, where there is none in hand.
    bool configOf(const Asking& asking, const Where& where, ALLuauConfig& out, const ALLuauConfig* base = nullptr);
    // An include's text as it was last read: what the texts fetched hold,
    // or a file on disk a run admitted.
    bool heldText(const std::string& path, std::string& text) const;

    // What is known of why a name may not have been found, for what is
    // said of it: the object the script is in never said what it holds;
    // the world, or the disk, not looked in -- the disk on with none of
    // the scripter's folders, and the script not a file on disk, which a
    // `.luaurc` beside it may have let folders in for -- and whether one so
    // named is in the object or the inventory.
    struct Missing
    {
        bool                                          objectUnanswered = false;
        bool                                          world            = false;
        bool                                          disk             = false;
        bool                                          noFolders        = false;
        bool                                          fromDisk         = false;
        std::function<bool(const std::string& name)> inWorld;
    };
    // An include or a module not found, said as why, where that is known:
    // the object's silence first, ahead of the names it would have
    // answered; one in the world that the world is not taken from; the
    // disk not looked in.
    static void explainMissing(ALScriptProblems& problems, const Missing& facts);

    // What a run left out, for a save to say before it goes without them:
    // the includes and modules not found, by the names the script gave
    // them, each once in the order said; how many problems said so; and
    // whether any was one a folder on disk would have let in -- one so
    // named in the world, which is not taken from, or the disk not looked
    // in.
    struct LeftOut
    {
        std::vector<std::string> names;
        S32                      problems  = 0;
        bool                     diskRoute = false;
    };
    static LeftOut leftOut(const ALScriptProblems& problems);

    // The name an include asks for, as an item would be called: without a
    // folder, and without the `./` a require may start with; and the
    // folders it gives before that, `.` and `..` as they were.
    static std::string              itemNameOf(const std::string& name);
    static std::vector<std::string> foldersOf(const std::string& name);

private:
    // The require parity suite asks the search before LAD9 itself, as a
    // require found nowhere does (searchedBefore).
    friend struct ::tut::alincludesearch_data;

    // The places a SLua require is walked through, as this search sees
    // them now (alincludesearch.cpp).
    class Places;
    // A SLua require: walked, and the first of what it stands for whose
    // text is there, or on its way, taken.
    ALPreprocessor::Found resolveRequire(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Asking& asking, const Where& where,
                                         wanted_t* wanted, bool retry, std::vector<std::string>& alias_folders);
    // A file on disk a require walked to, where it stands, where a folder
    // blessed for a require admits it: the scripter's, an alias's of a
    // `.luaurc`, a studio alias's.
    std::optional<std::string> requireAdmits(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                             const std::vector<std::string>& alias_folders, const std::string& file);
    // A require found nowhere: where the search before LAD9 found it, and
    // the forms a require may say it by now that find that very file --
    // relative to the file asking, through an alias that reaches it
    // already, or otherwise through a studio alias of the include folder
    // that holds it (Include::moves). For one release.
    void searchedBefore(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where, std::vector<std::string>& alias_folders,
                        ALPreprocessor::Include& out);

    // Something an include name could mean, in the order tried: in the
    // world, or a file on disk, read on the spot.
    struct Candidate
    {
        std::string path;
        std::string name;
        LLUUID      assetId;
        std::string file;
    };
    std::vector<Candidate> candidatesFor(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                         const std::vector<std::string>& alias_folders, bool& unknown);
    // The folders on disk a name may come from: the scripter's while the
    // disk is on, and what a `.lslrc` or `.luaurc` on disk lists --
    // `alias_folders` being those a run's aliases have blessed. Nothing
    // else, ever.
    ALDiskCache::Blessed& blessedFor(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                     const std::vector<std::string>& alias_folders);
    ALDiskCache::Blessed& ownFolders(const Where& where);
    // A folder on disk's configuration: its `.luaurc` and its
    // `.config.luau`, where each is, either empty; both is ambiguous.
    struct DiskConfig
    {
        std::string dir;
        std::string luaurc;
        std::string luau;
    };
    // Each folder's from one up to the root that has either, nearest
    // first; and at the top of each of the scripter's include folders that
    // has either, in their order. A walk's own climb reads a folder's from
    // the same look up the folders (Places).
    std::vector<DiskConfig> configsUp(const std::string& dir, const Where& where);
    std::vector<DiskConfig> configsAtTop(const Where& where);
    // One read as a `.luaurc` reads, and named by its file: its `.luaurc`,
    // or its `.config.luau` run (ALLuauConfigScript) -- no text, and why,
    // where that fails -- or ambiguous, with both. False where it has
    // neither, or the file cannot be read.
    bool readConfig(const DiskConfig& config, ALRequirePlaces::Config& out);
    // A file on disk a blessed folder admits, where it stands: what may be
    // asked the text of from now on.
    Candidate admitted(const std::string& real);
    // A candidate's text, from the texts fetched or the disk; Pending, and
    // wanted, where it is in the world and not in hand yet.
    ALPreprocessor::Found textOf(const Candidate& candidate, wanted_t* wanted, bool retry, std::string& text, std::string& assetId);

    ALScriptTextCache& mTexts;
    ALIncludeWorld&    mWorld;
    ALDiskCache        mDisk;
    // The files on disk a run has admitted, by identity: what may be asked
    // the text of, and nothing else on the disk.
    wanted_t           mAdmitted;
};

/**
 * @file alscriptpreprocessor.h
 * @brief The preprocessor with its includes found and fetched from the world.
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
#include "alincludeidentity.h"
#include "aldiskincludes.h"
#include "alscriptjoblane.h"
#include "alscriptsnapshot.h"
#include "alscripttextcache.h"
#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alrequirenavigation.h"
#include "alscripttypes.h"
#include "llinventorymodel.h"
#include "llsingleton.h"
#include "llstl.h"

class ALScriptIncludeResolver;
class ALSerialWorker;

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The preprocessor as the viewer runs it: the settings for what it does,
// the agent and the asset for its predefined macros, and its includes
// found by name -- in the object holding the script, in the agent's
// inventory (scripts and notecards alike), or in a folder on disk, in
// the order the setting says -- and fetched from the region when they
// are not in hand yet. A run over a script whose includes are not all in
// fetches them and runs again, and answers once, on the main thread,
// when every include has come or failed to. The texts fetched are kept
// for the session, by the asset they were, so that the analyzers can
// preprocess a script as it is typed without waiting on anything.
//
// An include's identity, as the source map names it, says where it came
// from: `object:<prim>:<item>`, `inventory:<item>`, or `disk:<path>`.
//
// A SLua `require("@name/rest")` goes through the `.luaurc` that governs
// the asking file -- the notecard so named in its folder or the nearest
// folder above, in inventory; the item so named in its object; the file
// so named in its directory or the nearest above, on disk -- whose
// alias `name` stands for a path from beside it, so that the name reads
// as `./path/rest` from the configuration's own folder.
class ALScriptPreprocessor : public LLSingleton<ALScriptPreprocessor>
{
    LLSINGLETON(ALScriptPreprocessor);
    ~ALScriptPreprocessor() override;

public:
    // Whether a save preprocesses a plain script. One that came wrapped
    // in the envelope is preprocessed on saving whatever this says, so
    // that its source stays its source.
    static bool enabled();
    // Whether an include or a require may come from the world: the object
    // holding the script, the agent's inventory. Off unless the scripter
    // turns it on -- a script is expanded the moment it is on screen,
    // somebody else's too, and a name it asks for from the inventory is
    // the agent's own text going into it -- and then only folders on disk
    // the scripter blessed are looked in.
    static bool worldIncludes();

    struct Request
    {
        ALScriptRef ref;
        // The script's own identity, where it is not the item `ref`
        // names: a file on disk, as `disk:<path>`. A name relative to
        // the script itself is taken from here.
        std::string path;
        // The script's name and asset, for `__SHORTFILE__` and `__ASSETID__`.
        std::string name;
        LLUUID      assetId;
        // The text run, shared rather than copied: a tab's snapshot of its
        // version, which the questions about it hold too; and read.
        std::shared_ptr<const std::string> source;
        const std::string& sourceText() const
        {
            static const std::string none;
            return source ? *source : none;
        }
        bool        lua = false;
        // What the script compiles for, which the optimizer's arithmetic
        // follows: mono, lsl2 or lsl-luau.
        std::string compileTarget;
        // Whether the optimizer runs over what was expanded. A save
        // wants it; a run made to look a name up does not -- the
        // optimizer may rename or remove the very name being looked
        // for, and its work is thrown away with the text.
        bool        optimize = true;
        // Whether an optimizing run is weighed before and after, so that
        // its notes say what each change saved in code: for a window that
        // shows them, and not for a compile of many, which would pay two
        // compiles more a script for words nobody reads.
        bool        weigh    = false;
        // SLua, for the analyzers: the script and its modules apart as
        // well as bundled (ALPreprocessor::Options::apart).
        bool        apart    = false;
    };
    typedef std::function<void(const ALPreprocessor::Result&)> callback_t;

    // Runs, fetching whatever is missing and running again until nothing
    // is, then answers once, on the main thread. The expansion, the
    // optimizer and the compression are all done on a thread of their
    // own: what the main thread does here is resolve the includes --
    // inventory, object contents and the disk, which are its alone --
    // and hand the answers over.
    void run(const Request& request, callback_t callback);
    // The same without the optimizer, for the analyzers: what the
    // compiler would see, mapped back to what the author wrote, and
    // none of the renaming the optimizer may do on top of it. Waits for
    // the worker behind every run, which a save waits on; and one still
    // waiting when the same script is asked for again is not made at
    // all, but answered with the later one's result.
    void expand(const Request& request, callback_t callback);
    // What the `.luaurc` governing a SLua script says -- its mode, its
    // lints, its globals -- over the base given, a scripter's own lints
    // and mode, from what is in hand: false, and the base or the
    // defaults, where there is no configuration, it is not in yet, or
    // it does not parse. A run fetches the configuration along with the
    // includes.
    bool configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base = nullptr);
    // The folders an include is looked for in on disk, in order, as the
    // setting holds them one to a line; and them put back.
    // What could follow a path typed in a string that names a file -- a
    // require's, by its rules, or an include's, by its search -- as the
    // places stand now (ALIncludeSearch::suggest).
    std::vector<ALRequireNavigation::Suggestion> suggestPaths(const Request& request, const std::string& typed, bool require);
    static std::vector<std::string> includeFolders();
    static void                     setIncludeFolders(const std::vector<std::string>& folders);
    // The SLua aliases the studio names (LA22): each name as a require
    // says it after the @ (ALLuauConfig::studioAliasName), and the folder
    // on disk it stands for -- or an inventory folder, `folder:<id>`, read
    // only while world includes are on -- in order, as the setting holds
    // them one to a line; and them put back.
    struct StudioAlias
    {
        std::string name;
        std::string folder;
    };
    // An inventory folder as a studio alias's folder names it, and back.
    static std::string inventoryAliasFolder(const LLUUID& folder);
    static bool        inventoryAliasFolder(const std::string& folder, LLUUID& out);
    static std::vector<StudioAlias> studioAliases();
    static void                     setStudioAliases(const std::vector<StudioAlias>& aliases);
    // The configuration fetched where it is in the world and not in hand,
    // and `fetched` called once it is; nothing where it is in hand, or
    // there is none.
    void fetchConfig(const Request& request, std::function<void()> fetched);

    // What an include's identity starts with: an item in an object, one in
    // the inventory, a file on disk.
    static constexpr std::string_view OBJECT_PREFIX    = ALIncludeIdentity::OBJECT;
    static constexpr std::string_view INVENTORY_PREFIX = ALIncludeIdentity::INVENTORY;
    static constexpr std::string_view DISK_PREFIX      = ALIncludeIdentity::DISK;
    // The identity a script's asks are remembered under.
    static std::string keyOf(const Request& request);
    // An include's identity back to the item it names, or the file; and
    // an item's identity, as the source map would name it.
    static bool        refOf(const std::string& path, ALScriptRef& ref);
    static bool        fileOf(const std::string& path, std::string& file);
    static std::string pathOf(const ALScriptRef& ref);
    // An include's text as it was last read, by its identity: what the
    // cache holds of one in the world, a file's as the disk has it now.
    // False where neither is in hand; nothing is fetched.
    bool               heldText(const std::string& path, std::string& text) const;
    // The identities of everything the cache holds, in no order.
    std::vector<std::string> heldPaths() const;

    // What an `#include` or a SLua `require` from a script finds now, as a
    // run would: Yes with its identity and text; Pending with its identity
    // where it is in the world and not in hand, and with none where the
    // object has not said what it holds; No where nothing is so named.
    // Nothing is fetched, so that the fixes may ask about every name a
    // script does not know.
    ALPreprocessor::Found lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out);
    // Every file a script's text includes or requires, and theirs in turn,
    // as a run would find them now -- nothing fetched, so one in the world
    // and not in hand is not among them -- each once, in the order met.
    std::vector<ALPreprocessor::Include> includedBy(const Request& request);
    // The scripts and notecards near a script that may be what it
    // includes or requires and are not in hand, at most `most`: those in
    // its object, those in its inventory folder, then those in the
    // folders of the inventory's includes already in hand -- a scripter
    // keeps a library together. Scripts of its language, notecards of
    // either. Only from the places a name is looked in, and nothing while
    // includes are not taken from the world.
    std::vector<std::string> nearby(const Request& request, size_t most);
    // Each fetched into the cache as a run would fetch it, and `done`
    // called once, when every one has come or failed.
    void                     prefetch(const std::vector<std::string>& paths, std::function<void()> done);
    // The folders on disk an include or a require from a script reads,
    // each with what a name under it starts with: the scripter's include
    // folders while disk includes are on, bare; for LSL, what a `.lslrc` in
    // one of those lists, and the nearest `.lslrc` up from a script on
    // disk; for SLua, each alias of a `.luaurc` on disk that governs the
    // script, as `@alias/`. What a configuration in the world says blesses
    // nothing, and is not here.
    std::vector<std::pair<std::string, std::string>> moduleFolders(const Request& request);

private:
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> wanted_t;
    struct Job;
    ALPreprocessor::Options optionsFor(const Request& request, bool optimize);
    // What is in hand for a job, on the main thread: the settings and
    // the agent read off, and every include the script is known to ask
    // for resolved. `wanted` gathers what is in the world and not in
    // hand, which the job fetches before it expands anything.
    ALScriptSnapshot        snapshotFor(const std::shared_ptr<Job>& job, wanted_t& wanted);
    // `fresh` asks the region what the object holds before anything
    // else, rather than going by what it last said.
    // `check` is an expansion for the analyzers, which gives way to a run.
    void                    start(const Request& request, callback_t callback, bool fresh, bool check = false);
    void                    attemptJob(const std::shared_ptr<Job>& job);
    // Whether a check is stood in for by a later check of the same script,
    // started since and not answered yet: its answers handed on to that
    // one, which answers them with its own result, as the worker's lane
    // hands on one still waiting there.
    bool                    handedOn(const std::shared_ptr<Job>& job);
    // What the worker made of a job, back on the main thread: another
    // round where the run asked for an include nobody had looked up
    // yet, else the optimizer and then the answer.
    void                    expandedJob(const std::shared_ptr<Job>& job, ALPreprocessor::Result result,
                                        std::vector<ALPreprocessor::Ask> missed);
    // The optimizer and the compression over what a job expanded, on
    // the worker: the optimizer parses the whole script and goes round
    // until nothing changes, and the inliner parses it again each
    // round, which is far too much to do between two frames. The answer
    // comes back on the main thread and the job is done. Where there is
    // nothing to do the job finishes here and now.
    void                    optimizeAndFinish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result);
    void                    finish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result);
    void                    ensureWorker();
    // Work for the worker thread, put in order as it waits: a run's
    // first, then a check's; a check whose script is asked to be checked
    // again while it waits dropped for the later one, and one under way
    // told to stop (Job::superseded).
    void                    toWorker(const std::shared_ptr<Job>& job, std::function<void()> work);
    // What waits for the worker, on the worker's side.
    std::shared_ptr<ALScriptJobLane<Job>> mLane = std::make_shared<ALScriptJobLane<Job>>();
    // An include by its identity, loaded into the cache -- or noted as
    // failed -- and `done` called either way. Or an inventory folder a
    // require walks into, by the name an alias gives one, waited for
    // until its contents are in or it has had long enough.
    void                    fetch(const std::string& path, std::function<void()> done);

    // The texts fetched, and what failed to come; and what an include or
    // a require names, found and read.
    ALScriptTextCache                                                               mTexts;
    std::unique_ptr<ALScriptIncludeResolver>                                        mResolver;
    // What each script asked for the last time it was expanded, by the
    // script's identity: a snapshot resolves these before the run goes
    // out, so that a script whose includes have not changed -- which is
    // every script between one keystroke and the next -- is expanded in
    // one round rather than one round for each level of include.
    boost::unordered_flat_map<std::string, std::vector<ALPreprocessor::Ask>, ll::string_hash, std::equal_to<>> mAsked;
    // The newest check of each script, by its identity (handedOn).
    boost::unordered_flat_map<std::string, std::weak_ptr<Job>, ll::string_hash, std::equal_to<>> mNewestCheck;
    // What waits on each inventory folder being fetched (fetch).
    boost::unordered_flat_map<LLUUID, std::vector<std::function<void()>>> mFoldersWaiting;
    // Where a run happens: one thread, so that two scripts saved at
    // once are expanded one after another rather than fighting over the
    // builtins. Closed at cleanup, or as the viewer starts to quit, and
    // kept closed: nothing starts another.
    std::unique_ptr<ALSerialWorker>                                                  mThread;
    void                                                                            cleanupSingleton() override;
};

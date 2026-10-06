/**
 * @file alscriptpreprocessor.cpp
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


#include "llviewerprecompiledheaders.h"

#include "alscriptpreprocessor.h"

#include "alincludeidentity.h"
#include "alincludesearch.h"
#include "alscriptincluderesolver.h"

#include "llappviewer.h"
#include "alserialworker.h"

#include "aldiskincludes.h"
#include "allslservice.h"
#include "alscriptanalysis.h"
#include "alluauconfig.h"
#include "alscriptenvelope.h"
#include "alscriptstack.h"
#include "alscriptworkspace.h"
#include "llagent.h"
#include "lldir.h"
#include "llsdjson.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llagentui.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    // How many times a run fetches, or expands and asks for more, before
    // it answers with what it has. A round is one or the other now, so a
    // script whose includes are nowhere in hand takes two for each level
    // of them.
    constexpr S32 MAX_ROUNDS = 16;
    // How many include names a script is remembered as asking for. One
    // that builds its names out of macros could otherwise have a longer
    // list every time it is expanded.
    constexpr size_t MAX_REMEMBERED = 128;
    // How many scripts' asks are remembered at once.
    constexpr size_t MAX_SCRIPTS_REMEMBERED = 256;

} // namespace

struct ALScriptPreprocessor::Job
{
    Request    request;
    callback_t callback;
    S32        rounds      = 0;
    S32        outstanding = 0;
    // The first round of a run tries again for what failed before: an
    // include that was not there may be there now. Only what this job's
    // own asks reach, rather than every failure every script ever had.
    bool       retry       = false;
    // Every include this run knows the script asks for: what it asked
    // for the last time it was expanded, and whatever this run's
    // expansions turned up on top. Resolved afresh each round, since a
    // fetch may have brought one in.
    std::vector<ALPreprocessor::Ask> asks;
    wanted_t                         askKeys;
    // The folders an alias of a `.luaurc` on disk has blessed in this run,
    // so that a module one brought in may require the modules beside it.
    std::vector<std::string>         aliasFolders;
    // An expansion for the analyzers, and the answers of those of the
    // same script it stood in for, which take its result.
    bool                             check = false;
    std::vector<callback_t>          alsoAnswer;
    // A check's: raised once a later check of the same script is asked
    // for, which takes its answers; the run then stops where it stands.
    std::shared_ptr<std::atomic<bool>> superseded;

    bool stale() const { return superseded && superseded->load(std::memory_order_relaxed); }
};

ALScriptPreprocessor::ALScriptPreprocessor() : mResolver(std::make_unique<ALScriptIncludeResolver>(mTexts)) {}
ALScriptPreprocessor::~ALScriptPreprocessor() = default;

bool ALScriptPreprocessor::heldText(const std::string& path, std::string& text) const
{
    return mResolver->heldText(path, text);
}

std::vector<std::string> ALScriptPreprocessor::heldPaths() const
{
    return mResolver->heldPaths();
}

ALPreprocessor::Found ALScriptPreprocessor::lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out)
{
    return mResolver->lookUp(request, ask, out);
}

std::vector<ALPreprocessor::Include> ALScriptPreprocessor::includedBy(const Request& request)
{
    return mResolver->includedBy(request);
}

std::vector<std::string> ALScriptPreprocessor::nearby(const Request& request, size_t most)
{
    return mResolver->nearby(request, most);
}

std::vector<std::pair<std::string, std::string>> ALScriptPreprocessor::moduleFolders(const Request& request)
{
    return mResolver->moduleFolders(request);
}

bool ALScriptPreprocessor::configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base)
{
    return mResolver->configOf(request, out, base);
}

// static
bool ALScriptPreprocessor::enabled()
{
    static LLCachedControl<bool> on(gSavedSettings, "ALScriptPreprocEnabled", false);
    return on;
}

// static
bool ALScriptPreprocessor::worldIncludes()
{
    static LLCachedControl<bool> on(gSavedSettings, "ALScriptPreprocWorldIncludes", false);
    return on;
}

// static
bool ALScriptPreprocessor::refOf(const std::string& path, ALScriptRef& ref)
{
    return ALIncludeIdentity::itemOf(path, ref.object, ref.item);
}

// static
std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return ALIncludeIdentity::ofItem(ref.object, ref.item);
}

// static
bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    return ALIncludeIdentity::fileOf(path, file);
}

void ALScriptPreprocessor::prefetch(const std::vector<std::string>& paths, std::function<void()> done)
{
    if (paths.empty())
    {
        return;
    }
    auto left = std::make_shared<size_t>(paths.size());
    for (const std::string& path : paths)
    {
        fetch(path, [left, done]() {
            if (--*left == 0 && done)
            {
                done();
            }
        });
    }
}

ALPreprocessor::Options ALScriptPreprocessor::optionsFor(const Request& request, bool optimize)
{
    static LLCachedControl<bool> switches(gSavedSettings, "ALScriptPreprocSwitch", false);
    static LLCachedControl<bool> lazy(gSavedSettings, "ALScriptPreprocLazyLists", false);
    static LLCachedControl<bool> compress(gSavedSettings, "ALScriptPreprocCompress", false);
    static LLCachedControl<bool> optimizer(gSavedSettings, "ALScriptPreprocOptimizer", false);
    static LLCachedControl<bool> shrink(gSavedSettings, "ALScriptPreprocOptimizerShrinkNames", false);
    static LLCachedControl<bool> addstrings(gSavedSettings, "ALScriptPreprocOptimizerAddStrings", false);
    static LLCachedControl<bool> inlining(gSavedSettings, "ALScriptPreprocOptimizerInlining", false);
    static LLCachedControl<bool> extensions(gSavedSettings, "ALScriptPreprocExtensions", false);
    ALPreprocessor::Options      options;
    options.lua        = request.lua;
    options.apart      = request.lua && request.apart;
    options.switches   = switches;
    options.lazyLists  = lazy;
    options.compress   = compress;
    options.extensions = extensions;
    // The analyzers see the expanded text before the optimizer has been
    // at it, so that their positions stay the author's.
    options.optimize              = optimize && request.optimize && optimizer && !request.lua && ALLSLService::builtinsLoaded();
    // Weighed before and after where that is asked, so that its notes say
    // what each change saved in code and not only in characters.
    options.weigh                 = options.optimize && request.weigh;
    // Nobody reads the notes of a run whose text is only read: the
    // optimizer prints what a fold was and became to say it.
    options.optimizer.notes       = request.optimize;
    options.optimizer.shrinknames = shrink;
    options.optimizer.addstrings  = addstrings;
    options.optimizer.inlining    = inlining;
    options.optimizer.target      = request.compileTarget == "lsl2"       ? ALLSLOptimizer::Target::LSO
                                    : request.compileTarget == "lsl-luau" ? ALLSLOptimizer::Target::Luau
                                                                          : ALLSLOptimizer::Target::Mono;
    options.agentId   = gAgentID.asString();
    LLAgentUI::buildFullname(options.agentName);
    options.assetId   = request.assetId.isNull() ? std::string() : request.assetId.asString();
    options.fileName  = request.name;
    // The scripter's own macros, one to a line.
    std::istringstream defines(gSavedSettings.getString("ALScriptPreprocDefines"));
    for (std::string line; std::getline(defines, line);)
    {
        LLStringUtil::trim(line);
        if (!line.empty())
        {
            options.defines.push_back(line);
        }
    }
    return options;
}

// static
std::vector<std::string> ALScriptPreprocessor::includeFolders()
{
    // One to a line, in the order looked in; a setting from before there
    // could be several is the one folder it named.
    std::vector<std::string> folders;
    std::istringstream       lines(gSavedSettings.getString("ALScriptPreprocDiskIncludeFolder"));
    for (std::string line; std::getline(lines, line);)
    {
        LLStringUtil::trim(line);
        if (!line.empty() && std::find(folders.begin(), folders.end(), line) == folders.end())
        {
            folders.push_back(line);
        }
    }
    return folders;
}

// static
void ALScriptPreprocessor::setIncludeFolders(const std::vector<std::string>& folders)
{
    std::string joined;
    for (const std::string& folder : folders)
    {
        joined += (joined.empty() ? "" : "\n") + folder;
    }
    gSavedSettings.setString("ALScriptPreprocDiskIncludeFolder", joined);
}

// static
std::vector<ALScriptPreprocessor::StudioAlias> ALScriptPreprocessor::studioAliases()
{
    // One to a line, `name=folder`; the first of a name kept.
    std::vector<StudioAlias> aliases;
    std::istringstream       lines(gSavedSettings.getString("ALScriptSLuaAliases"));
    for (std::string line; std::getline(lines, line);)
    {
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        StudioAlias one{ line.substr(0, equals), line.substr(equals + 1) };
        LLStringUtil::trim(one.name);
        LLStringUtil::trim(one.folder);
        const bool taken = std::any_of(aliases.begin(), aliases.end(), [&one](const StudioAlias& other) {
            return LLStringUtil::compareInsensitive(other.name, one.name) == 0;
        });
        if (ALLuauConfig::studioAliasName(one.name) && !one.folder.empty() && !taken)
        {
            aliases.push_back(std::move(one));
        }
    }
    return aliases;
}

// static
void ALScriptPreprocessor::setStudioAliases(const std::vector<StudioAlias>& aliases)
{
    std::string joined;
    for (const StudioAlias& alias : aliases)
    {
        joined += (joined.empty() ? "" : "\n") + alias.name + "=" + alias.folder;
    }
    gSavedSettings.setString("ALScriptSLuaAliases", joined);
}

// static
std::string ALScriptPreprocessor::keyOf(const Request& request)
{
    return request.path.empty() ? pathOf(request.ref) : request.path;
}

ALScriptSnapshot ALScriptPreprocessor::snapshotFor(const std::shared_ptr<Job>& job, wanted_t& wanted)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const Request&   request  = job->request;
    ALScriptSnapshot snapshot;
    snapshot.options() = optionsFor(request, /*optimize*/ false);
    // The optimizer and the compression are the job's last step, once
    // every include is in: a round whose text is thrown away the moment
    // one arrives should not pay for either.
    snapshot.options().compress = false;
    snapshot.options().resolve  = nullptr;
    // A check is asked for at every pause in typing, and nothing it makes
    // is saved: held to a quarter of what a save may make, still far more
    // than a script may be.
    if (job->check)
    {
        snapshot.options().byteBudget  = 4u * ALScriptEnvelope::MAX_ASSET_BYTES;
        snapshot.options().tokenBudget = 1000u * 1000u;
        // And stopped part way once a later check of it is asked for.
        snapshot.options().superseded  = job->superseded.get();
    }
    for (const ALPreprocessor::Ask& ask : job->asks)
    {
        ALPreprocessor::Include     include;
        const ALPreprocessor::Found found = mResolver->resolve(ask, include, request, &wanted, job->retry, &job->aliasFolders);
        if (found == ALPreprocessor::Found::Pending)
        {
            // In the world and not in hand: fetched, and the snapshot
            // taken again once it is.
            continue;
        }
        snapshot.answer(ask, found, std::move(include));
    }
    if (request.lua)
    {
        // The script's own `.luaurc` fetched with its includes, whether
        // or not a require goes through it: its mode is wanted anyway.
        std::vector<ALScriptIncludeResolver::Config> configs;
        mResolver->configsFor(keyOf(request), request, &wanted, job->retry, configs);
    }
    return snapshot;
}

void ALScriptPreprocessor::run(const Request& request, callback_t callback)
{
    start(request, std::move(callback), /*fresh*/ true);
}

void ALScriptPreprocessor::expand(const Request& request, callback_t callback)
{
    Request without  = request;
    without.optimize = false;
    start(without, std::move(callback), /*fresh*/ false, /*check*/ true);
}

void ALScriptPreprocessor::start(const Request& request, callback_t callback, bool fresh, bool check)
{
    auto job      = std::make_shared<Job>();
    job->request  = request;
    job->callback = std::move(callback);
    job->check    = check;
    if (check)
    {
        job->superseded = std::make_shared<std::atomic<bool>>(false);
    }
    // What this script's own includes failed at before may come now;
    // another script's failures are its own, and clearing them would
    // have every other tab fetch its missing include again.
    job->retry = true;
    // What it asked for the last time it was expanded, so that a script
    // being typed in is expanded in one round rather than one for each
    // level of its includes.
    if (const auto asked = mAsked.find(keyOf(request)); asked != mAsked.end())
    {
        job->asks = asked->second;
        for (const ALPreprocessor::Ask& ask : job->asks)
        {
            job->askKeys.insert(ALScriptSnapshot::keyOf(ask));
        }
    }
    // Nothing to ask an object where includes are not taken from it.
    if (request.ref.inInventory() || !worldIncludes())
    {
        attemptJob(job);
        return;
    }
    // The object's contents first, since they are where a name is looked
    // for. Asked of the region again only where a save wants them or
    // nobody has told us yet: a check runs a moment after every
    // keystroke, and asking a prim what it holds that often is a message
    // a keystroke for an answer that hardly ever changes.
    const LLUUID prim = request.ref.object;
    if (!fresh && (mResolver->listed(prim) || mResolver->unanswered(prim)))
    {
        attemptJob(job);
        return;
    }
    ALScriptWorkspace::instance().listContents(prim, [this, job, prim](const ALScriptContents& contents) {
        if (contents.fetched)
        {
            mResolver->heard(prim, contents.items);
        }
        else if (!mResolver->listed(prim))
        {
            // Nothing to go by, not even what it said before.
            mResolver->notAnswered(prim);
        }
        attemptJob(job);
    });
}


void ALScriptPreprocessor::attemptJob(const std::shared_ptr<Job>& job)
{
    // Everything the viewer has to say about this script, gathered here
    // on the main thread: the settings, the agent, and each include
    // looked up in the inventory, the object's contents or the disk.
    wanted_t         wanted;
    ALScriptSnapshot snapshot = snapshotFor(job, wanted);
    job->retry                = false;
    if (!wanted.empty() && ++job->rounds <= MAX_ROUNDS)
    {
        // Something the script includes is in the world and not in hand:
        // fetched before anything is expanded, since expanding without
        // it would only ask for it again.
        job->outstanding = S32(wanted.size()) + 1;
        for (const std::string& path : wanted)
        {
            fetch(path, [this, job]() {
                if (--job->outstanding == 0)
                {
                    attemptJob(job);
                }
            });
        }
        // The one the loop holds, so that a fetch answered on the spot
        // cannot start the next round from inside it.
        if (--job->outstanding == 0)
        {
            attemptJob(job);
        }
        return;
    }
    // And the expansion itself on a thread of its own: it tokenizes the
    // whole script and rescans what its macros make, which is the one
    // thing here that has nothing of the viewer in it.
    toWorker(job, [this, job, snapshot = std::make_shared<ALScriptSnapshot>(std::move(snapshot))]() {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("preprocessor expand job");
        // On a stack as deep as a script needs: the expansion recurses on
        // how the script nests, and a pool's thread on a Mac has half a
        // megabyte. Whatever it throws is answered here, as a run that ran
        // away is: uncaught on this thread, it would be thrown again on
        // the main one, and the job would never answer.
        ALPreprocessor::Result           result;
        std::vector<ALPreprocessor::Ask> missed;
        try
        {
            alScriptOnLargeStack([&]() { result = snapshot->run(job->request.sourceText()); });
            missed = snapshot->missed();
        }
        catch (const std::exception& e)
        {
            result = ALPreprocessor::failed(job->request.sourceText(), snapshot->options(), e.what());
            missed.clear();
        }
        LLAppViewer::instance()->postToMainCoro([this, job, result = std::move(result), missed = std::move(missed)]() mutable {
            expandedJob(job, std::move(result), std::move(missed));
        });
    });
}

void ALScriptPreprocessor::toWorker(const std::shared_ptr<Job>& job, std::function<void()> work)
{
    ensureWorker();
    // Another check of the same script is for text that has moved on
    // since: its answers this one's. Still waiting, it is not made; under
    // way, it is told to stop. Its callbacks are the main thread's alone,
    // which the worker never touches.
    const bool start = mLane->put({ job, keyOf(job->request), job->check, std::move(work) }, [&job](Job& older, bool running) {
        if (running)
        {
            if (!older.superseded)
            {
                return;
            }
            older.superseded->store(true, std::memory_order_relaxed);
        }
        if (older.callback)
        {
            job->alsoAnswer.push_back(std::move(older.callback));
            older.callback = nullptr;
        }
        std::move(older.alsoAnswer.begin(), older.alsoAnswer.end(), std::back_inserter(job->alsoAnswer));
        older.alsoAnswer.clear();
    });
    if (!start)
    {
        return;
    }
    const std::shared_ptr<ALScriptJobLane<Job>> lane = mLane;
    if (!mThread->post([lane]() { lane->drain(); }))
    {
        // Closed -- the viewer going -- and what waits goes with it, as
        // cleanup lets it go.
        lane->notStarted();
    }
}

void ALScriptPreprocessor::expandedJob(const std::shared_ptr<Job>& job, ALPreprocessor::Result result, std::vector<ALPreprocessor::Ask> missed)
{
    // Stood in for by a later check of the same script, which answers for
    // it: nothing more made of this one.
    if (job->stale())
    {
        return;
    }
    // An include nobody had looked up yet: looked up now, and the run
    // made again with it in. The run is what says a name was asked for
    // at all -- an `#include` inside an `#if`, or one a macro made, is
    // asked for only where the expansion reaches it.
    bool learned = false;
    for (ALPreprocessor::Ask& ask : missed)
    {
        if (job->askKeys.insert(ALScriptSnapshot::keyOf(ask)).second)
        {
            job->asks.push_back(std::move(ask));
            learned = true;
        }
    }
    if (learned && ++job->rounds <= MAX_ROUNDS)
    {
        attemptJob(job);
        return;
    }
    // What this script asks for, for the next run over it.
    if (!job->asks.empty())
    {
        if (job->asks.size() > MAX_REMEMBERED)
        {
            job->asks.resize(MAX_REMEMBERED);
        }
        // For as many scripts as a session works in at once: past that, all
        // of them asked again, each in a round or two more.
        const std::string key = keyOf(job->request);
        if (mAsked.size() >= MAX_SCRIPTS_REMEMBERED && !mAsked.contains(key))
        {
            mAsked.clear();
        }
        mAsked[key] = job->asks;
    }
    optimizeAndFinish(job, std::move(result));
}

void ALScriptPreprocessor::ensureWorker()
{
    if (!mThread)
    {
        mThread = std::make_unique<ALSerialWorker>("ScriptPreproc");
    }
}

void ALScriptPreprocessor::cleanupSingleton()
{
    mLane->close();
    if (mThread)
    {
        mThread->close();
    }
}

void ALScriptPreprocessor::finish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result)
{
    if (!job->callback && job->alsoAnswer.empty())
    {
        return;
    }
    // Why a name was not found, where the viewer knows: the object never
    // said what it holds, the world or the disk not looked in.
    static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    ALIncludeSearch::Missing     missing;
    std::string                  file;
    missing.objectUnanswered = !job->request.ref.inInventory() && mResolver->unanswered(job->request.ref.object);
    missing.world            = worldIncludes();
    missing.disk             = disk;
    missing.noFolders        = includeFolders().empty();
    missing.fromDisk         = fileOf(job->request.path, file);
    missing.inWorld          = [this, &job](const std::string& name) { return mResolver->inWorld(job->request, name); };
    ALIncludeSearch::explainMissing(result.problems, missing);
    alTranslateScriptProblems(result.problems);
    if (job->callback)
    {
        job->callback(result);
    }
    // Then those it stood in for, whose text has moved on since, and who
    // learn nothing from it but that they were answered.
    for (const callback_t& also : job->alsoAnswer)
    {
        also(result);
    }
}

void ALScriptPreprocessor::optimizeAndFinish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result)
{
    // The optimizer parses the whole script and goes round until nothing
    // changes, and the inliner parses it again each round: far too much
    // to do between two frames, and nothing of the viewer's is in it --
    // the text goes in, the text comes out, and the builtins are the
    // process's. So it goes to the same thread the expansion did, with
    // the compression after it, which is where a run would have done
    // both.
    const ALPreprocessor::Options options = optionsFor(job->request, /*optimize*/ true);
    if (options.lua || (!options.optimize && !options.compress) || result.overran || result.text.empty())
    {
        finish(job, std::move(result));
        return;
    }
    // On the analysis thread, where all of Tailslide's work is done, so that
    // the optimizer never runs at once with a check and nothing is locked
    // (ALScriptAnalysis::runEngine). The preprocessor's own thread goes on
    // with the next expansion meanwhile.
    const auto made = std::make_shared<ALPreprocessor::Result>(std::move(result));
    ALScriptAnalysis::instance().runEngine(
        [job, made, options]() {
            LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("preprocessor optimize job");
            try
            {
                ALPreprocessor::finish(*made, options);
            }
            catch (const std::exception& e)
            {
                *made = ALPreprocessor::failed(job->request.sourceText(), options, e.what());
            }
        },
        [this, job, made]() { finish(job, std::move(*made)); });
}

void ALScriptPreprocessor::fetch(const std::string& path, std::function<void()> done)
{
    ALScriptRef ref;
    if (!refOf(path, ref))
    {
        mTexts.failed(path);
        done();
        return;
    }
    ALScriptWorkspace::instance().load(ref, [this, path, done](const ALScriptLoaded& loaded) {
        if (!loaded.error.empty())
        {
            mTexts.failed(path);
        }
        else
        {
            // An include saved with the preprocessor on is its source.
            std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(loaded.text);
            mTexts.put(path, loaded.assetId, envelope ? envelope->source : loaded.text);
        }
        done();
    });
}

void ALScriptPreprocessor::fetchConfig(const Request& request, std::function<void()> fetched)
{
    if (!request.lua)
    {
        return;
    }
    wanted_t            wanted;
    std::vector<ALScriptIncludeResolver::Config> configs;
    if (mResolver->configsFor(keyOf(request), request, &wanted, /*retry*/ false, configs) != ALPreprocessor::Found::Pending || wanted.empty())
    {
        return;
    }
    for (const std::string& want : wanted)
    {
        fetch(want, fetched);
    }
}

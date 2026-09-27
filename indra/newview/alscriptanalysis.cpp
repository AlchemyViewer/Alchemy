/**
 * @file alscriptanalysis.cpp
 * @brief The analyzers on a thread of their own, checking scripts as they are typed.
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

#include "alscriptanalysis.h"

#include "allslservice.h"
#include "alluauservice.h"
#include "alscriptstack.h"
#include "llappviewer.h"
#include "llviewercontrol.h"
#include "llfile.h"
#include "llsyntaxid.h"
#include "alsaid.h"
#include "lltrans.h"
#include "alserialworker.h"

#include <algorithm>
#include <optional>
#include <sstream>

// What lives on the worker: the two services and what they were loaded
// from, so that a request whose definitions moved on reloads before it
// checks.
struct ALScriptAnalysis::Worker
{
    ALLuauService luau;
    ALLSLService  lsl;
    std::string   luauDefinitionsPath;
    std::string   luauDocsPath;
    U32           luauGeneration = 0;
    std::string   luauError;
    bool          lslLoaded = false;
    std::string   lslError;

    static std::string readWhole(const std::string& path)
    {
        std::string text;
        if (!path.empty())
        {
            llifstream in(path, std::ios::in | std::ios::binary);
            if (in.is_open())
            {
                std::ostringstream buffer;
                buffer << in.rdbuf();
                text = buffer.str();
            }
        }
        return text;
    }

    void loadLuau(const std::string& path, const std::string& docs_path, U32 generation)
    {
        if (generation == luauGeneration && path == luauDefinitionsPath && docs_path == luauDocsPath)
        {
            return;
        }
        luauGeneration      = generation;
        luauDefinitionsPath = path;
        luauDocsPath        = docs_path;
        luauError.clear();
        const std::string source = readWhole(path);
        if (source.empty())
        {
            // Said with a key, for the main thread to put into the
            // viewer's language; what the engine says is said as it is.
            luauError = "\x01AnalysisNoLuauDefinitions\x01" + path;
            return;
        }
        std::string error;
        if (!luau.loadDefinitions(source, error))
        {
            luauError = error;
        }
        const std::string docs = readWhole(docs_path);
        if (!docs.empty() && !luau.loadDocs(docs, error))
        {
            LL_WARNS("ScriptAnalysis") << "The Luau docs did not load: " << error << LL_ENDL;
        }
    }

    // The solver the scripts are checked with: a change builds the front
    // end again and loads the definitions into it again.
    void useSolver(bool use_new)
    {
        if (luau.newSolver() == use_new)
        {
            return;
        }
        std::string error;
        const bool  loaded = luau.setNewSolver(use_new, error);
        if (!loaded || luau.hasDefinitions())
        {
            luauError = error;
        }
    }

    // Once for the session, from the first region's builtins: Tailslide
    // keeps them in a table of the whole process that it adds to and never
    // replaces, so a region whose LSL has more functions than the first's
    // is checked against the first's until the viewer starts again.
    // Luau's front end is the service's own, and is loaded again
    // (loadLuau).
    void loadLSL(const std::string& path)
    {
        if (lslLoaded)
        {
            return;
        }
        lslLoaded = true;
        lslError.clear();
        if (path.empty())
        {
            lslError = "\x01AnalysisNoLSLBuiltins\x01";
            return;
        }
        std::string error;
        if (!lsl.loadBuiltins(path, error))
        {
            lslError = error;
        }
    }
};

ALScriptAnalysis::ALScriptAnalysis() = default;

ALScriptAnalysis::~ALScriptAnalysis() = default;

void ALScriptAnalysis::cleanupSingleton()
{
    if (mThread)
    {
        mThread->close();
    }
}

void ALScriptAnalysis::ensureStarted()
{
    if (!mThread)
    {
        // Closing, a check still running is stopped rather than waited
        // for, and what waits is passed over.
        mThread = std::make_unique<ALSerialWorker>("ScriptAnalysis", [this]() {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            ALLuauService::cancel(mRunningStop);
        });
    }
}

void ALScriptAnalysis::definitionsChanged()
{
    ++mDefinitionsGeneration;
}

void ALScriptAnalysis::ask(Request request, callback_t callback)
{
    ensureStarted();
    Job job;
    // The paths are the syntax cache's, which is the main thread's; the
    // worker reads what they name.
    job.luauPath   = request.lua ? LLSyntaxDefCache::instance().getLuauDefinitionsPath() : std::string();
    job.docsPath   = request.lua ? LLSyntaxDefCache::instance().getLuauDocsPath() : std::string();
    job.lslPath    = request.lua ? std::string() : LLSyntaxDefCache::instance().getLSLBuiltinsPath();
    job.generation = mDefinitionsGeneration;
    // The solver and the time limit, which are settings, and so the main
    // thread's to read.
    static LLCachedControl<std::string> solver_setting(gSavedSettings, "ALScriptLuauSolver", "old");
    static LLCachedControl<F32>         seconds_setting(gSavedSettings, "ALScriptLuauCheckSeconds", 5.f);
    job.newSolver = std::string(solver_setting) == "new";
    job.seconds   = seconds_setting;
    // What is answered where the thread will not take the job -- the viewer
    // going -- so that nothing waits on it: nothing found.
    Result refused;
    refused.kind    = request.kind;
    refused.id      = request.id;
    refused.version = request.version;
    refused.lua     = request.lua;
    refused.line    = request.line;
    refused.column  = request.column;
    const auto answer = std::make_shared<callback_t>(std::move(callback));
    job.callback      = answer;
    // Waiting under the script and the kind of question -- and, for a
    // weigh, which of its weighs -- in place of what waited there.
    std::string key = request.id;
    key += '\x1f';
    key += static_cast<char>('0' + static_cast<int>(request.kind));
    if (request.kind == Kind::Weigh)
    {
        key += static_cast<char>('0' + static_cast<int>(request.weighing));
    }
    // By rank: the front tab's questions, which someone is waiting on, then
    // its check; weighing; everything else -- background tabs, lookups.
    const U8 rank = request.kind == Kind::Weigh    ? 2
                    : !request.front                ? 3
                    : request.kind == Kind::Check   ? 1
                                                    : 0;
    const std::string id      = request.id;
    const U32         version = request.version;
    job.request               = std::move(request);
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        if (mQueue.add(key, id, version, rank, std::move(job)))
        {
            // What runs is answering something no longer wanted.
            ALLuauService::cancel(mRunningStop);
        }
    }
    if (!mThread->post([this]() { runNext(); }))
    {
        {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.forget(id);
        }
        LLAppViewer::instance()->postToMainCoro([refused = std::move(refused), answer]() { (*answer)(refused); });
    }
}

void ALScriptAnalysis::forget(const std::string& id)
{
    const std::lock_guard<std::mutex> lock(mQueueMutex);
    mQueue.forget(id);
}

void ALScriptAnalysis::runNext()
{
    // The job as a whole, its definitions loaded included; what it asked
    // of the service is the zone inside.
    LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("script analysis job");
    std::optional<std::pair<std::string, Job>> next;
    // What stops it, where it is an SLua one.
    ALLuauService::Stop stop;
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        next = mQueue.take();
        if (!next)
        {
            return;
        }
        if (next->second.request.lua)
        {
            stop         = ALLuauService::newStop();
            mRunningStop = stop;
        }
    }
    const Job& job    = next->second;
    Result     result = run(job, stop);
    bool       unwanted = false;
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        unwanted = mQueue.superseded();
        mQueue.finished();
        if (mRunningStop == stop)
        {
            mRunningStop.reset();
        }
    }
    if (stop && mWorker)
    {
        unwanted = unwanted || mWorker->luau.stopped();
        mWorker->luau.setStop(nullptr);
    }
    if (unwanted)
    {
        // Stopped, or asked again while it ran: what was asked since
        // answers in its place.
        return;
    }
    const std::shared_ptr<callback_t> callback = job.callback;
    // The words in the viewer's language, on the main thread, where
    // the strings are.
    LLAppViewer::instance()->postToMainCoro([result = std::move(result), callback]() mutable {
        alTranslateScriptProblems(result.problems);
        for (ALScriptFix& action : result.actions)
        {
            action.title = alScriptKeyedWords(action.key, action.args, action.title);
        }
        // A definitions error of this code's own carries its key
        // between the marks, with what it is about after.
        if (!result.definitionsError.empty() && result.definitionsError[0] == '\x01')
        {
            const size_t end = result.definitionsError.find('\x01', 1);
            if (end != std::string::npos)
            {
                LLStringUtil::format_map_t args;
                args["[PATH]"] = result.definitionsError.substr(end + 1);
                result.definitionsError = LLTrans::getString(result.definitionsError.substr(1, end - 1), args);
            }
        }
        (*callback)(result);
    });
}

ALScriptAnalysis::Result ALScriptAnalysis::run(const Job& job, const ALLuauService::Stop& stop)
{
    const Request& request = job.request;
    // The engines recurse on how the script nests; the pool's thread has
    // what the platform gives a thread, which on a Mac is half a megabyte.
    // The work goes on a stack as deep as a script needs.
    Result result;
    alScriptOnLargeStack([&]() {
        if (!mWorker)
        {
            mWorker = std::make_unique<Worker>();
        }
        result.kind    = request.kind;
        result.id      = request.id;
        result.version = request.version;
        result.lua     = request.lua;
        result.line    = request.line;
        result.column  = request.column;
        // A text weighed for a target of its own language: SLua's for SLua,
        // LSL's others for LSL.
        const auto weighed = [&request](ALScriptWeight::Target target, const std::string& text) {
            if ((target == ALScriptWeight::Target::SLua) != request.lua)
            {
                return ALScriptWeight();
            }
            return target == ALScriptWeight::Target::SLua      ? ALScriptWeigh::slua(text)
                   : target == ALScriptWeight::Target::LSO     ? ALScriptWeigh::lso(text)
                   : target == ALScriptWeight::Target::Mono    ? ALScriptWeigh::mono(text)
                   : target == ALScriptWeight::Target::LSLLuau ? ALScriptWeigh::lslLuau(text)
                                                               : ALScriptWeight();
        };
        // The text weighed for each target asked for -- or, where there are
        // variants, each variant for the first -- by a weigh, or by a check
        // the script's own weigh was folded into.
        const auto weigh = [&]() {
            if (!request.variants.empty())
            {
                if (!request.targets.empty())
                {
                    for (const std::string& variant : request.variants)
                    {
                        result.variantTotals.push_back(weighed(request.targets.front(), variant).total);
                    }
                }
                return;
            }
            for (const ALScriptWeight::Target target : request.targets)
            {
                if ((target == ALScriptWeight::Target::SLua) == request.lua)
                {
                    result.weights.push_back(weighed(target, request.text));
                }
            }
        };
        if (request.lua)
        {
            mWorker->useSolver(job.newSolver);
            mWorker->loadLuau(job.luauPath, job.docsPath, job.generation);
            result.definitionsError = mWorker->luauError;
            mWorker->luau.setConfig(request.config);
            mWorker->luau.setTimeLimit(job.seconds);
            mWorker->luau.setStop(stop);
            switch (request.kind)
            {
                case Kind::Check:
                    result.problems = mWorker->luau.check(request.text);
                    if (mWorker->luau.stopped())
                    {
                        break;
                    }
                    result.outline  = mWorker->luau.outline(request.text);
                    if (request.semantics)
                    {
                        result.semantics = mWorker->luau.semanticTokens(request.text);
                    }
                    result.hints = mWorker->luau.inlayHints(request.text, request.hintParameters, request.hintTypes);
                    weigh();
                    break;
                case Kind::Complete:
                    result.completions = mWorker->luau.complete(request.text, request.line, request.column);
                    break;
                case Kind::Hover:
                case Kind::Inspect:
                    result.hover = mWorker->luau.hover(request.text, request.line, request.column);
                    break;
                case Kind::Signature:
                    result.signature = mWorker->luau.signature(request.text, request.line, request.column);
                    break;
                case Kind::References:
                    result.references = mWorker->luau.references(request.text, request.line, request.column);
                    break;
                case Kind::Actions:
                    result.actions = mWorker->luau.actions(request.text, request.line, request.column, request.endLine, request.endColumn);
                    break;
                case Kind::Weigh:
                    weigh();
                    break;
            }
        }
        else
        {
            mWorker->loadLSL(job.lslPath);
            result.definitionsError = mWorker->lslError;
            switch (request.kind)
            {
                case Kind::Check:
                    result.problems = mWorker->lsl.check(request.text, request.mono);
                    result.outline  = mWorker->lsl.outline(request.text);
                    if (request.semantics)
                    {
                        result.semantics = mWorker->lsl.semanticTokens(request.text);
                    }
                    result.hints = mWorker->lsl.inlayHints(request.text, request.hintParameters);
                    weigh();
                    break;
                case Kind::Complete:
                    result.completions = mWorker->lsl.symbols(request.text, request.line, request.column);
                    break;
                case Kind::Hover:
                case Kind::Inspect:
                    result.hover = mWorker->lsl.hover(request.text, request.line, request.column);
                    break;
                case Kind::Signature:
                    result.signature = mWorker->lsl.signature(request.text, request.line, request.column);
                    break;
                case Kind::References:
                    result.references = mWorker->lsl.references(request.text, request.line, request.column);
                    break;
                case Kind::Actions:
                    result.actions = mWorker->lsl.actions(request.text, request.line, request.column, request.endLine, request.endColumn);
                    break;
                case Kind::Weigh:
                    weigh();
                    break;
            }
            result.parsed     = mWorker->lsl.parsed();
            result.understood = mWorker->lsl.understood();
        }
    });
    return result;
}

std::string alScriptKeyedWords(const std::string& key, const std::vector<std::string>& args, const std::string& english)
{
    if (key.empty())
    {
        return english;
    }
    // The skin's text for the key, found once and kept; the words go in
    // by their marks, with no map made. A key the skin lacks is marked
    // so, and the words stay the code's own English.
    static const std::string MISSING("\x01");
    const std::string&       text = alSaidTemplate(key, MISSING);
    return text == MISSING ? english : ALScriptProblem::fill(text, args);
}

std::string alScriptProblemWords(const ALScriptProblem& problem)
{
    return alScriptKeyedWords(problem.key, problem.args, problem.message);
}

void alTranslateScriptProblems(ALScriptProblems& problems)
{
    for (ALScriptProblem& problem : problems)
    {
        if (!problem.key.empty())
        {
            problem.message = alScriptProblemWords(problem);
        }
        // What each fix does, as the problem's own words are said.
        for (ALScriptFix& fix : problem.fixes)
        {
            fix.title = alScriptKeyedWords(fix.key, fix.args, fix.title);
        }
    }
}

// --- the scripter's choice of lints ------------------------------------------------------

namespace ALScriptLints
{
    namespace
    {
        // The warnings Tailslide gives, by number: every one it has but
        // those it never says (20006, 20008, 20010).
        const char* const LSL_WARNINGS[] = { "20001", "20002", "20003", "20004", "20005", "20007", "20009", "20011", "20012",
                                             "20013", "20014", "20015", "20016", "20017", "20018", "20019", "20020" };

        std::string keyOf(bool lua, std::string_view id)
        {
            return std::string(lua ? "luau:" : "lsl:") + std::string(id);
        }

        const char* nameOf(Level level)
        {
            return level == Level::Off ? "off" : level == Level::Error ? "error" : "warning";
        }
    }

    const std::vector<Lint>& all()
    {
        static const std::vector<Lint> lints = [] {
            std::vector<Lint> out;
            for (const char* id : LSL_WARNINGS)
            {
                out.push_back(Lint{ id, false });
            }
            for (const std::string& name : ALLuauConfig::lintNames())
            {
                out.push_back(Lint{ name, true });
            }
            return out;
        }();
        return lints;
    }

    Level level(bool lua, std::string_view id)
    {
        const LLSD        levels = gSavedSettings.getLLSD("ALScriptLintLevels");
        const std::string said   = levels.has(keyOf(lua, id)) ? levels[keyOf(lua, id)].asString() : std::string();
        return said == "off" ? Level::Off : said == "error" ? Level::Error : Level::Warning;
    }

    void setLevel(bool lua, std::string_view id, Level to)
    {
        LLSD levels = gSavedSettings.getLLSD("ALScriptLintLevels");
        if (!levels.isMap())
        {
            levels = LLSD::emptyMap();
        }
        // Only what differs from the default is kept, so that a default
        // that changes reaches everyone who did not choose otherwise.
        if (to == Level::Warning)
        {
            levels.erase(keyOf(lua, id));
        }
        else
        {
            levels[keyOf(lua, id)] = nameOf(to);
        }
        gSavedSettings.setLLSD("ALScriptLintLevels", levels);
    }

    void setLevels(bool lua, const std::vector<std::pair<std::string, Level>>& chosen)
    {
        LLSD levels = gSavedSettings.getLLSD("ALScriptLintLevels");
        if (!levels.isMap())
        {
            levels = LLSD::emptyMap();
        }
        for (const auto& [id, to] : chosen)
        {
            if (to == Level::Warning)
            {
                levels.erase(keyOf(lua, id));
            }
            else
            {
                levels[keyOf(lua, id)] = nameOf(to);
            }
        }
        // The setting tells its listeners only of a value that changed.
        gSavedSettings.setLLSD("ALScriptLintLevels", levels);
    }

    void reset()
    {
        gSavedSettings.setLLSD("ALScriptLintLevels", LLSD::emptyMap());
        gSavedSettings.setString("ALScriptLuauMode", "auto");
        gSavedSettings.setString("ALScriptLuauSolver", "old");
    }

    void apply(ALScriptProblems& problems)
    {
        const LLSD levels = gSavedSettings.getLLSD("ALScriptLintLevels");
        if (!levels.isMap() || levels.size() == 0)
        {
            return;
        }
        problems.erase(std::remove_if(problems.begin(), problems.end(),
                                      [&levels](ALScriptProblem& problem) {
                                          if (problem.severity != ALScriptProblem::Severity::Warning || problem.code.empty())
                                          {
                                              return false;
                                          }
                                          const std::string said = levels.has("lsl:" + problem.code) ? levels["lsl:" + problem.code].asString() : std::string();
                                          if (said == "error")
                                          {
                                              problem.severity = ALScriptProblem::Severity::Error;
                                          }
                                          return said == "off";
                                      }),
                       problems.end());
    }

    ALLuauConfig luauBase()
    {
        ALLuauConfig base;
        for (const Lint& lint : all())
        {
            if (!lint.lua)
            {
                continue;
            }
            const uint64_t bit = ALLuauConfig::lintBit(lint.id);
            switch (level(true, lint.id))
            {
                case Level::Off:
                    base.lints &= ~bit;
                    break;
                case Level::Error:
                    base.lints |= bit;
                    base.fatalLints |= bit;
                    break;
                default:
                    base.lints |= bit;
                    break;
            }
        }
        // Left to the solver: the new one's nonstrict reports only a
        // checked function called wrongly, so a script it checks is
        // checked strict unless something says otherwise; the old one's
        // nonstrict is how the grid compiles.
        const std::string mode = gSavedSettings.getString("ALScriptLuauMode");
        if (mode == "strict" || mode == "nocheck" || mode == "nonstrict")
        {
            base.mode = mode;
        }
        else
        {
            base.mode = gSavedSettings.getString("ALScriptLuauSolver") == "new" ? "strict" : "nonstrict";
        }
        return base;
    }
}

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

#include "alscriptanalyzers.h"
#include "alscriptlintpass.h"
#include "alscriptstack.h"
#include "llappviewer.h"
#include "llviewercontrol.h"
#include "llsyntaxid.h"
#include "alsaid.h"
#include "lltrans.h"
#include "alserialworker.h"

#include <algorithm>
#include <optional>

// What lives on the worker: each language's analyzer, made the first time
// a question in its language comes -- a viewer that never opens an SLua
// script builds no Luau front end.
struct ALScriptAnalysis::Worker
{
    std::unique_ptr<ALLuauAnalyzer> luau;
    std::unique_ptr<ALLSLAnalyzer>  lsl;
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
        // What Luau keeps for the whole process, set here on the main
        // thread before the worker reads it.
        ALLuauService::setUpProcess();
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

void ALScriptAnalysis::runEngine(std::function<void()> work, std::function<void()> done)
{
    ensureStarted();
    Job job;
    job.engineWork = std::move(work);
    job.engineDone = std::move(done);
    // Each its own: one run's work does not stand in for another's.
    const std::string key  = "engine:" + std::to_string(++mEngineSerial);
    auto              then = std::make_shared<std::function<void()>>(job.engineDone);
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        mQueue.add(key, key, 0, 1, std::move(job));
    }
    if (!mThread->post([this]() { runNext(); }))
    {
        {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.forget(key);
        }
        LLAppViewer::instance()->postToMainCoro([then]() { (*then)(); });
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
    const Job& job = next->second;
    if (job.engineWork)
    {
        // No question: the work, on a stack as deep as it needs, and then
        // whoever waits on it told.
        try
        {
            alScriptOnLargeStack(job.engineWork);
        }
        catch (const std::exception& e)
        {
            // Whoever gave it answers for what it throws; told, all the same.
            LL_WARNS("ScriptAnalysis") << "Engine work failed: " << e.what() << LL_ENDL;
        }
        {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.finished();
        }
        LLAppViewer::instance()->postToMainCoro([done = job.engineDone]() { done(); });
        return;
    }
    Result result = run(job, stop);
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
    if (stop && mWorker && mWorker->luau)
    {
        unwanted = unwanted || mWorker->luau->stopped();
        mWorker->luau->forgetStop();
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
    static const std::string NOTHING;
    const std::string&       text = request.text ? *request.text : NOTHING;
    ALScriptAnalyzer::Setup  setup;
    setup.luauPath   = job.luauPath;
    setup.docsPath   = job.docsPath;
    setup.lslPath    = job.lslPath;
    setup.generation = job.generation;
    setup.newSolver  = job.newSolver;
    setup.seconds    = job.seconds;
    setup.stop       = stop;
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
        if (request.lua)
        {
            if (!mWorker->luau)
            {
                mWorker->luau = std::make_unique<ALLuauAnalyzer>();
            }
            mWorker->luau->answer(request, text, setup, result);
        }
        else
        {
            if (!mWorker->lsl)
            {
                mWorker->lsl = std::make_unique<ALLSLAnalyzer>();
            }
            mWorker->lsl->answer(request, text, setup, result);
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

        // What one is where nobody chose: on, but for the studio's own the
        // table has off.
        Level defaultOf(bool lua, std::string_view id)
        {
            const ALScriptLintPass::Rule* rule = ALScriptLintPass::rule(id);
            return rule && rule->lua == lua && !rule->on ? Level::Off : Level::Warning;
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
            // The studio's own after Tailslide's.
            for (const ALScriptLintPass::Rule& rule : ALScriptLintPass::rules())
            {
                if (!rule.lua)
                {
                    out.push_back(Lint{ rule.name, false });
                }
            }
            for (const std::string& name : ALLuauConfig::lintNames())
            {
                out.push_back(Lint{ name, true });
            }
            // The studio's own after Luau's.
            for (const ALScriptLintPass::Rule& rule : ALScriptLintPass::rules())
            {
                if (rule.lua)
                {
                    out.push_back(Lint{ rule.name, true });
                }
            }
            return out;
        }();
        return lints;
    }

    Level level(bool lua, std::string_view id)
    {
        const LLSD        levels = gSavedSettings.getLLSD("ALScriptLintLevels");
        const std::string said   = levels.has(keyOf(lua, id)) ? levels[keyOf(lua, id)].asString() : std::string();
        return said == "off" ? Level::Off : said == "error" ? Level::Error : said == "warning" ? Level::Warning : defaultOf(lua, id);
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
        if (to == defaultOf(lua, id))
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
            if (to == defaultOf(lua, id))
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
        const bool chosen = levels.isMap() && levels.size() > 0;
        problems.erase(std::remove_if(problems.begin(), problems.end(),
                                      [&](ALScriptProblem& problem) {
                                          // Tailslide's warnings, and the studio's own, which may be notes.
                                          const ALScriptLintPass::Rule* own = problem.source == ALScriptProblem::Source::Lint
                                                                                  ? ALScriptLintPass::rule(problem.code)
                                                                                  : nullptr;
                                          if ((problem.severity != ALScriptProblem::Severity::Warning && !own) || problem.code.empty())
                                          {
                                              return false;
                                          }
                                          const std::string key  = "lsl:" + problem.code;
                                          std::string       said = chosen && levels.has(key) ? levels[key].asString() : std::string();
                                          if (said.empty() && own && !own->on)
                                          {
                                              said = "off";
                                          }
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
        // Made once, and again only when a setting it reads changes: every
        // SLua question asks for it -- each check, hover and signature --
        // and made afresh it reads the lint levels once a lint.
        static std::optional<ALLuauConfig> made;
        static bool                         watching = false;
        if (!watching)
        {
            watching = true;
            for (const char* name : { "ALScriptLintLevels", "ALScriptLuauMode", "ALScriptLuauSolver" })
            {
                if (LLControlVariable* control = gSavedSettings.getControl(name))
                {
                    control->getSignal()->connect([](LLControlVariable*, const LLSD&, const LLSD&) { made.reset(); });
                }
            }
        }
        if (made)
        {
            return *made;
        }
        ALLuauConfig base;
        for (const Lint& lint : all())
        {
            if (!lint.lua)
            {
                continue;
            }
            // The studio's own, in masks of their own.
            if (const uint64_t own = ALScriptLintPass::bit(lint.id))
            {
                switch (level(true, lint.id))
                {
                    case Level::Off:
                        base.slLints &= ~own;
                        break;
                    case Level::Error:
                        base.slLints |= own;
                        base.slFatalLints |= own;
                        break;
                    default:
                        base.slLints |= own;
                        break;
                }
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
        made = base;
        return base;
    }
}

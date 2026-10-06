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

#include "alscriptanalysislane.h"
#include "alscriptanalyzers.h"
#include "alscriptlintpass.h"
#include "llappviewer.h"
#include "llviewercontrol.h"
#include "llsyntaxid.h"
#include "alsaid.h"
#include "lltrans.h"

#include <algorithm>
#include <optional>

namespace
{
    // An answer to whoever asked, on the main thread, in the viewer's
    // language, where the strings are.
    void answerOnMain(ALScriptAnalysis::Result result, std::shared_ptr<ALScriptAnalysis::callback_t> callback)
    {
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
}

ALScriptAnalysis::ALScriptAnalysis() = default;

ALScriptAnalysis::~ALScriptAnalysis() = default;

void ALScriptAnalysis::cleanupSingleton()
{
    for (ALScriptAnalysisLane* lane : { mLuau.get(), mTailslide.get() })
    {
        if (lane)
        {
            lane->close();
        }
    }
}

void ALScriptAnalysis::ensureStarted()
{
    if (mLuau)
    {
        return;
    }
    // What Luau keeps for the whole process, set here on the main thread
    // before a lane reads it.
    ALLuauService::setUpProcess();
    const ALScriptAnalysisLane::Main main{ answerOnMain, [](std::function<void()> done) {
                                              LLAppViewer::instance()->postToMainCoro([done = std::move(done)]() { done(); });
                                          } };
    mLuau      = std::make_unique<ALScriptAnalysisLane>("ScriptAnalysisLuau", [] { return std::make_unique<ALLuauAnalyzer>(); }, main);
    mTailslide = std::make_unique<ALScriptAnalysisLane>("ScriptAnalysisLSL", [] { return std::make_unique<ALLSLAnalyzer>(); }, main);
}

void ALScriptAnalysis::definitionsChanged()
{
    ++mDefinitionsGeneration;
}

void ALScriptAnalysis::ask(Request request, callback_t callback)
{
    ensureStarted();
    ALScriptAnalysisLane::Job job;
    // The paths are the syntax cache's, which is the main thread's; the
    // lane reads what they name.
    job.luauPath   = request.lua ? LLSyntaxDefCache::instance().getLuauDefinitionsPath() : std::string();
    job.docsPath   = request.lua ? LLSyntaxDefCache::instance().getLuauDocsPath() : std::string();
    job.lslPath    = request.lua ? std::string() : LLSyntaxDefCache::instance().getLSLBuiltinsPath();
    job.generation = mDefinitionsGeneration;
    // The solver, the time limit and whether typing is answered over a
    // fragment, which are settings, and so the main thread's to read.
    static LLCachedControl<std::string> solver_setting(gSavedSettings, "ALScriptLuauSolver", "old");
    static LLCachedControl<F32>         seconds_setting(gSavedSettings, "ALScriptLuauCheckSeconds", 5.f);
    static LLCachedControl<bool>        fragments_setting(gSavedSettings, "ALScriptFragmentCompletion", true);
    job.newSolver = std::string(solver_setting) == "new";
    job.seconds   = seconds_setting;
    job.fragments = fragments_setting;
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
    // Waiting under the script and the kind of question, in place of what
    // waited there; on SLua's lane or LSL's, weighing included.
    const std::string key  = ALScriptAnalysisLane::keyOf(request);
    const U8          rank = ALScriptAnalysisLane::rankOf(request);
    // Another tab's SLua check gives way to the front tab's questions:
    // stopped for one, and run again after it. LSL's are short, and the
    // rest is the front tab's own, or weighing.
    const bool            yields = request.lua && request.kind == Kind::Check && !request.front;
    ALScriptAnalysisLane& lane   = request.lua ? *mLuau : *mTailslide;
    job.request                  = std::move(request);
    if (!lane.post(key, rank, yields, std::move(job)))
    {
        LLAppViewer::instance()->postToMainCoro([refused = std::move(refused), answer]() { (*answer)(refused); });
    }
}

void ALScriptAnalysis::runEngine(std::function<void()> work, std::function<void()> done)
{
    ensureStarted();
    ALScriptAnalysisLane::Job job;
    job.engineWork = std::move(work);
    job.engineDone = std::move(done);
    // Each its own: one run's work does not stand in for another's. On
    // Tailslide's lane, which every use of Tailslide is on.
    const std::string key  = "engine:" + std::to_string(++mEngineSerial);
    auto              then = std::make_shared<std::function<void()>>(job.engineDone);
    job.request.id         = key;
    if (!mTailslide->post(key, 1, false, std::move(job)))
    {
        LLAppViewer::instance()->postToMainCoro([then]() { (*then)(); });
    }
}

void ALScriptAnalysis::forget(const std::string& id)
{
    for (ALScriptAnalysisLane* lane : { mLuau.get(), mTailslide.get() })
    {
        if (lane)
        {
            lane->forget(id);
        }
    }
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
            return rule && rule->in(lua) && !rule->on ? Level::Off : Level::Warning;
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
                if (rule.in(false))
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
                if (rule.in(true))
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
        // A mode the scripter chose; else none, which the service reads as
        // the solver's own (ALLuauService::setConfig): the old one's
        // nonstrict, as the grid compiles, and the new one's strict, its
        // nonstrict reporting only what is sure to fail.
        const std::string mode = gSavedSettings.getString("ALScriptLuauMode");
        if (mode == "strict" || mode == "nocheck" || mode == "nonstrict")
        {
            base.mode = mode;
        }
        made = base;
        return base;
    }
}

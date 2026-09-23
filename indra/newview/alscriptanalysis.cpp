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
#include "threadpool.h"
#include "workqueue.h"

#include <algorithm>
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
    if (mPool)
    {
        mPool->close();
        mPool.reset();
    }
}

void ALScriptAnalysis::ensureStarted()
{
    if (!mPool)
    {
        mPool = std::make_unique<LL::ThreadPool>("ScriptAnalysis", 1);
        mPool->start();
    }
}

void ALScriptAnalysis::definitionsChanged()
{
    ++mDefinitionsGeneration;
}

void ALScriptAnalysis::ask(Request request, callback_t callback)
{
    ensureStarted();
    // The paths are the syntax cache's, which is the main thread's; the
    // worker reads what they name.
    const std::string luau_path  = request.lua ? LLSyntaxDefCache::instance().getLuauDefinitionsPath() : std::string();
    const std::string docs_path  = request.lua ? LLSyntaxDefCache::instance().getLuauDocsPath() : std::string();
    const std::string lsl_path   = request.lua ? std::string() : LLSyntaxDefCache::instance().getLSLBuiltinsPath();
    const U32         generation = mDefinitionsGeneration;
    // Which check of this script this is: a later one asked for while
    // this one waits makes it stale, and the worker passes it over. Only
    // checks are numbered -- a hover or a completion is about a place,
    // and is cheap.
    U32 serial = 0;
    if (request.kind == Kind::Check)
    {
        const std::lock_guard<std::mutex> lock(mLatestMutex);
        serial                    = ++mAskSerial;
        mLatestCheck[request.id] = serial;
    }
    mPool->getQueue().post([this, request = std::move(request), callback = std::move(callback), luau_path, docs_path, lsl_path, generation, serial]() {
        if (serial != 0)
        {
            const std::lock_guard<std::mutex> lock(mLatestMutex);
            const auto                        latest = mLatestCheck.find(request.id);
            if (latest != mLatestCheck.end() && latest->second != serial)
            {
                // A newer check of this script is already waiting: this
                // one's answer would be thrown away on arrival.
                return;
            }
        }
        // The engines recurse on how the script nests; the pool's thread
        // has what the platform gives a thread, which on a Mac is half a
        // megabyte. The work goes on a stack as deep as a script needs.
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
                mWorker->loadLuau(luau_path, docs_path, generation);
                result.definitionsError = mWorker->luauError;
                mWorker->luau.setConfig(request.config);
                switch (request.kind)
                {
                    case Kind::Check:
                        result.problems = mWorker->luau.check(request.text);
                        result.outline  = mWorker->luau.outline(request.text);
                        if (request.semantics)
                        {
                            result.semantics = mWorker->luau.semanticTokens(request.text);
                        }
                        result.hints = mWorker->luau.inlayHints(request.text, request.hintParameters, request.hintTypes);
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
                }
            }
            else
            {
                mWorker->loadLSL(lsl_path);
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
                }
                result.parsed     = mWorker->lsl.parsed();
                result.understood = mWorker->lsl.understood();
            }
        });
        // The words in the viewer's language, on the main thread, where
        // the strings are.
        LLAppViewer::instance()->postToMainCoro([result = std::move(result), callback]() mutable {
            alTranslateScriptProblems(result.problems);
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
            callback(result);
        });
    });
}

std::string alScriptProblemWords(const ALScriptProblem& problem)
{
    if (problem.key.empty())
    {
        return problem.message;
    }
    // The skin's text for the key, found once and kept; the words go in
    // by their marks, with no map made. A key the skin lacks is marked
    // so, and the message stays the code's own English.
    static const std::string MISSING("\x01");
    const std::string&       text = alSaidTemplate(problem.key, MISSING);
    return text == MISSING ? problem.message : ALScriptProblem::fill(text, problem.args);
}

void alTranslateScriptProblems(ALScriptProblems& problems)
{
    for (ALScriptProblem& problem : problems)
    {
        if (!problem.key.empty())
        {
            problem.message = alScriptProblemWords(problem);
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
        gSavedSettings.setString("ALScriptLuauMode", "nonstrict");
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
        const std::string mode = gSavedSettings.getString("ALScriptLuauMode");
        base.mode              = mode == "strict" || mode == "nocheck" ? mode : std::string("nonstrict");
        return base;
    }
}

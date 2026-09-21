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
#include "llappviewer.h"
#include "llfile.h"
#include "llsyntaxid.h"
#include "threadpool.h"
#include "workqueue.h"

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
            luauError = "No Luau definitions at " + path;
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
            lslError = "No LSL builtins";
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
    mPool->getQueue().post([this, request = std::move(request), callback = std::move(callback), luau_path, docs_path, lsl_path, generation]() {
        if (!mWorker)
        {
            mWorker = std::make_unique<Worker>();
        }
        Result result;
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
        }
        LLAppViewer::instance()->postToMainCoro([result = std::move(result), callback]() { callback(result); });
    });
}

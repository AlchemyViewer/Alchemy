/**
 * @file alscriptanalyzers.cpp
 * @brief Each language's analyzer on the analysis thread: a question in, its answer out.
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

#include "alscriptanalyzers.h"

#include "alscriptweight.h"
#include "llfile.h"

#include <sstream>

// static
std::string ALScriptAnalyzer::readWhole(const std::string& path)
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

// static
void ALScriptAnalyzer::weigh(const Request& request, const std::string& text, Result& result)
{
    // A text weighed for a target of its own language: SLua's for SLua,
    // LSL's others for LSL.
    const auto weighed = [&request](ALScriptWeight::Target target, const std::string& of) {
        if ((target == ALScriptWeight::Target::SLua) != request.lua)
        {
            return ALScriptWeight();
        }
        return target == ALScriptWeight::Target::SLua      ? ALScriptWeigh::slua(of)
               : target == ALScriptWeight::Target::LSO     ? ALScriptWeigh::lso(of)
               : target == ALScriptWeight::Target::Mono    ? ALScriptWeigh::mono(of)
               : target == ALScriptWeight::Target::LSLLuau ? ALScriptWeigh::lslLuau(of)
                                                           : ALScriptWeight();
    };
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
    // The bundle where the text is the script apart from its modules:
    // what a save sends, and so what it weighs.
    const std::string& whole = request.bundle ? *request.bundle : text;
    for (const ALScriptWeight::Target target : request.targets)
    {
        if ((target == ALScriptWeight::Target::SLua) == request.lua)
        {
            result.weights.push_back(weighed(target, whole));
        }
    }
}

// --- LSL ---------------------------------------------------------------------------------

void ALLSLAnalyzer::load(const std::string& path, U32 generation)
{
    // Each region's builtins as it comes: a region with functions the last
    // lacked has them added (ALLSLService::loadBuiltins), so that a function
    // new on an RC region is no undeclared name there.
    if (mLoaded && path == mPath && generation == mGeneration)
    {
        return;
    }
    mLoaded     = true;
    mPath       = path;
    mGeneration = generation;
    mError.clear();
    if (path.empty())
    {
        mError = "\x01AnalysisNoLSLBuiltins\x01";
        return;
    }
    std::string error;
    if (!mService.loadBuiltins(path, error))
    {
        mError = error;
    }
}

void ALLSLAnalyzer::answer(const Request& request, const std::string& text, const Setup& setup, Result& result)
{
    using Kind = ALScriptAnalysis::Kind;
    load(setup.lslPath, setup.generation);
    result.definitionsError = mError;
    mService.setPassedOver(request.passedOver);
    switch (request.kind)
    {
        case Kind::Check:
            result.problems = mService.check(text, request.mono);
            result.outline  = mService.outline(text);
            if (request.semantics)
            {
                result.semantics = mService.semanticTokens(text);
            }
            result.hints = mService.inlayHints(text, request.hintParameters);
            weigh(request, text, result);
            break;
        case Kind::Complete:
            result.completions = mService.symbols(text, request.line, request.column);
            break;
        case Kind::Hover:
        case Kind::Inspect:
            result.hover = mService.hover(text, request.line, request.column);
            break;
        case Kind::Signature:
            result.signature = mService.signature(text, request.line, request.column);
            break;
        case Kind::References:
            result.references = mService.references(text, request.line, request.column);
            break;
        case Kind::Actions:
            result.actions = mService.actions(text, request.line, request.column, request.endLine, request.endColumn);
            break;
        case Kind::Weigh:
            // Weighed by the compiler, not asked of the service: what the
            // service last parsed may be another tab's.
            weigh(request, text, result);
            return;
        case Kind::Shape:
            // From a parse alone, mended where it must be; the tree kept for
            // the tab is left be.
            result.outline = mService.outline(text, false);
            break;
        case Kind::Warm:
            // SLua's alone.
            break;
    }
    result.parsed     = mService.parsed();
    result.understood = mService.understood();
}

// --- SLua --------------------------------------------------------------------------------

void ALLuauAnalyzer::load(const std::string& path, const std::string& docs_path, U32 generation)
{
    if (generation == mGeneration && path == mDefinitionsPath && docs_path == mDocsPath)
    {
        return;
    }
    mGeneration      = generation;
    mDefinitionsPath = path;
    mDocsPath        = docs_path;
    mError.clear();
    const std::string source = readWhole(path);
    if (source.empty())
    {
        // Said with a key, for the main thread to put into the viewer's
        // language; what the engine says is said as it is.
        mError = "\x01AnalysisNoLuauDefinitions\x01" + path;
        return;
    }
    std::string error;
    if (!mService.loadDefinitions(source, error))
    {
        mError = error;
    }
    const std::string docs = readWhole(docs_path);
    if (!docs.empty() && !mService.loadDocs(docs, error))
    {
        LL_WARNS("ScriptAnalysis") << "The Luau docs did not load: " << error << LL_ENDL;
    }
}

void ALLuauAnalyzer::useSolver(bool use_new)
{
    // A change builds the front end again and loads the definitions into it
    // again.
    if (mService.newSolver() == use_new)
    {
        return;
    }
    std::string error;
    const bool  loaded = mService.setNewSolver(use_new, error);
    if (!loaded || mService.hasDefinitions())
    {
        mError = error;
    }
}

void ALLuauAnalyzer::answer(const Request& request, const std::string& text, const Setup& setup, Result& result)
{
    using Kind = ALScriptAnalysis::Kind;
    if (request.kind == Kind::Shape)
    {
        // From a parse alone: nothing of the front end, which holds the
        // tabs' modules, nor of the definitions.
        result.outline = ALLuauService::shape(text);
        return;
    }
    useSolver(setup.newSolver);
    load(setup.luauPath, setup.docsPath, setup.generation);
    result.definitionsError = mError;
    // Each tab's script its own module, kept as it was left; a lookup
    // through another object's script shares the one of no name, rather
    // than letting the tabs' go.
    const bool looked_through = request.id.rfind("lookup:", 0) == 0;
    mService.setDocument(looked_through ? std::string_view() : std::string_view(request.id));
    mService.setConfig(request.config);
    mService.setPassedOver(request.passedOver);
    static const ALLuauService::Modules NONE;
    mService.setModules(request.modules ? *request.modules : NONE);
    mService.setTimeLimit(setup.seconds);
    mService.setFragments(setup.fragments);
    mService.setStop(setup.stop);
    switch (request.kind)
    {
        case Kind::Check:
            result.problems = mService.check(text);
            if (mService.stopped())
            {
                break;
            }
            result.outline = mService.outline(text);
            if (request.semantics)
            {
                result.semantics = mService.semanticTokens(text);
            }
            result.hints = mService.inlayHints(text, request.hintParameters, request.hintTypes);
            weigh(request, text, result);
            break;
        case Kind::Complete:
            result.completions = mService.complete(text, request.line, request.column);
            break;
        case Kind::Hover:
        case Kind::Inspect:
            result.hover = mService.hover(text, request.line, request.column);
            break;
        case Kind::Signature:
            result.signature = mService.signature(text, request.line, request.column);
            break;
        case Kind::References:
            result.references = mService.references(text, request.line, request.column);
            break;
        case Kind::Actions:
            result.actions = mService.actions(text, request.line, request.column, request.endLine, request.endColumn);
            break;
        case Kind::Weigh:
            weigh(request, text, result);
            break;
        case Kind::Warm:
            mService.warm(text);
            break;
        case Kind::Shape:
            break;
    }
}

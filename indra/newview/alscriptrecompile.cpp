/**
 * @file alscriptrecompile.cpp
 * @brief Scripts, prims and whole objects recompiled for a target from the Explorer, each result in Output and a closed script's problems in Problems.
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

#include "alscriptrecompile.h"

#include "alscriptstudioservices.h"
#include "alsourcemap.h"

#include <boost/unordered/unordered_flat_set.hpp>

ALScriptRecompile::ALScriptRecompile(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window) {}

void ALScriptRecompile::recompile(std::vector<One> scripts, std::vector<std::pair<LLUUID, std::string>> prims, const std::string& target)
{
    const U32 generation = ++mGeneration;
    mRunning             = true;
    mTarget              = target.empty() ? std::string("auto") : target;
    mDone                = Done();
    mDone.target         = mTarget;
    mLeft.clear();
    mNext     = 0;
    mUnderWay = 0;
    if (prims.empty())
    {
        listed(generation, std::move(scripts), Window::Listed());
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.listScripts(prims, [this, alive, generation, scripts = std::move(scripts)](Window::Listed listed) mutable {
        if (alive.lock())
        {
            this->listed(generation, std::move(scripts), std::move(listed));
        }
    });
}

void ALScriptRecompile::listed(U32 generation, std::vector<One> chosen, Window::Listed listed)
{
    if (generation != mGeneration || !mRunning)
    {
        return;
    }
    mDone.unlisted = listed.unlisted;
    // Each once: one chosen with its prim goes up with the prim's.
    boost::unordered_flat_set<std::pair<LLUUID, LLUUID>> taken;
    for (std::vector<One>* from : { &chosen, &listed.scripts })
    {
        for (One& one : *from)
        {
            if (taken.insert({ one.ref.object, one.ref.item }).second)
            {
                mLeft.push_back(std::move(one));
            }
        }
    }
    feed();
}

void ALScriptRecompile::feed()
{
    // One begun that is answered on the spot comes back here: the loop
    // below goes on with the next.
    if (mFeeding)
    {
        return;
    }
    mFeeding             = true;
    const U32 generation = mGeneration;
    while (mRunning && generation == mGeneration && mNext < mLeft.size() && mUnderWay < AT_ONCE)
    {
        const One one = mLeft[mNext++];
        ++mUnderWay;
        begin(one);
    }
    mFeeding = false;
    if (mRunning && generation == mGeneration && mNext >= mLeft.size() && mUnderWay == 0)
    {
        mRunning = false;
        mWindow.recompiled(mDone);
    }
}

void ALScriptRecompile::passed()
{
    --mUnderWay;
    feed();
}

namespace
{
    // A script as Output names it: with what it is in, where that is known.
    std::string named(const ALScriptStudioServices& services, const ALScriptRecompile::One& one)
    {
        if (one.object.empty())
        {
            return one.name;
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"]   = one.name;
        args["[OBJECT]"] = one.object;
        return services.words("ScriptInObject", args);
    }
}

void ALScriptRecompile::begin(const One& one)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = named(mServices, one);
    // A save on its way would have the server's text land over it: left
    // alone. Changes not saved in a tab are not what compiles: said so.
    if (mWindow.saving(one.ref))
    {
        mServices.report(mServices.words("RecompileSkippedSaving", args), true);
        ++mDone.skipped;
        passed();
        return;
    }
    if (mWindow.unsaved(one.ref))
    {
        mServices.report(mServices.words("RecompileUnsaved", args), true);
    }
    const std::weak_ptr<bool> alive      = mAlive;
    const U32                 generation = mGeneration;
    // An SLua script compiles as SLua: an LSL target asked is the LSL
    // scripts'.
    mWindow.recompile(one.ref, one.lua ? std::string("auto") : mTarget, mWindow.knownRunning(one.ref),
                      [this, alive, generation, one](const ALScriptCompileResult& result) {
                          if (alive.lock() && generation == mGeneration)
                          {
                              answered(one, result);
                          }
                      });
}

void ALScriptRecompile::answered(const One& one, const ALScriptCompileResult& result)
{
    Script script;
    script.one  = one;
    script.open = mWindow.isOpen(one.ref);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = named(mServices, one);
    if (!result.error.empty())
    {
        script.error    = result.error;
        args["[ERROR]"] = result.error;
        ++mDone.notSent;
        mServices.report(mServices.words("SaveFailed", args), true);
    }
    else if (result.success)
    {
        script.compiled = true;
        ++mDone.compiled;
        mServices.report(mServices.words("Compiled", args));
    }
    else
    {
        ++mDone.failed;
        // A tab holding it says what the compiler said, having heard it.
        if (!script.open)
        {
            script.rows = rowsOf(result, mServices.words("OriginCompiler"), mServices.words("InGeneratedCode"));
        }
        mServices.report(mServices.counted("CompileFailed", static_cast<S32>(result.diagnostics.size()), args), true);
    }
    mWindow.scriptRecompiled(script);
    passed();
}

// static
std::vector<ALScriptRecompile::Doc::Shown> ALScriptRecompile::rowsOf(const ALScriptCompileResult& result, const std::string& origin,
                                                                     const std::string& generated)
{
    std::vector<Doc::Shown> rows;
    const ALSourceMap*      map = result.sourceMap.get();
    for (const ALScriptDiagnostic& said : result.diagnostics)
    {
        Doc::Shown row;
        row.level     = Doc::levelOf(said.level);
        row.origin    = origin;
        row.message   = said.message;
        row.line      = said.line;
        row.column    = said.column;
        row.hasColumn = said.hasColumn;
        // The region counts the envelope's lines above the code; the map
        // is of the code.
        if (map)
        {
            const ALSourceMap::Loc loc = map->toSource(said.line - result.codeLine, said.column);
            if (loc.found())
            {
                row.line   = loc.line;
                row.column = loc.column;
                if (loc.file > 0)
                {
                    row.file     = map->files()[loc.file].path;
                    row.fileName = map->files()[loc.file].name;
                }
            }
            else
            {
                // In code the preprocessor made, at the expansion's line.
                row.line     = llmax(0, said.line - result.codeLine);
                row.file     = Doc::GENERATED;
                row.fileName = generated;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

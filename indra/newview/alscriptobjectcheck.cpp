/**
 * @file alscriptobjectcheck.cpp
 * @brief Every script of an object checked, the ones no tab holds read and asked of the analyzers as a tab is.
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

#include "alscriptobjectcheck.h"

#include "alscriptstudioanalysis.h"
#include "alscriptstudiochecking.h"
#include "alscriptstudioservices.h"

ALScriptObjectCheck::ALScriptObjectCheck(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window) : mServices(services), mAnalysis(analysis), mWindow(window) {}

void ALScriptObjectCheck::check(const LLUUID& root, const std::string& name)
{
    const U32 generation = ++mGeneration;
    mRunning             = true;
    mDone                = Done();
    mDone.root           = root;
    mDone.name           = name;
    mLeft.clear();
    mNext     = 0;
    mUnderWay = 0;
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.listScripts(root, [this, alive, generation](Window::Listed listed) {
        if (alive.lock())
        {
            this->listed(generation, std::move(listed));
        }
    });
}

void ALScriptObjectCheck::listed(U32 generation, Window::Listed listed)
{
    if (generation != mGeneration || !mRunning)
    {
        return;
    }
    mDone.present  = listed.present;
    mDone.unlisted = listed.unlisted;
    // A script a tab holds is checked there, as it stands, and the Problems
    // tab lists it from there.
    for (Window::Listed::One& one : listed.scripts)
    {
        if (mWindow.isOpen(one.ref))
        {
            ++mDone.open;
        }
        else
        {
            mLeft.push_back(std::move(one));
        }
    }
    feed();
}

void ALScriptObjectCheck::feed()
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
        const Window::Listed::One one = mLeft[mNext++];
        ++mUnderWay;
        begin(one);
    }
    mFeeding = false;
    if (mRunning && generation == mGeneration && mNext >= mLeft.size() && mUnderWay == 0)
    {
        mRunning = false;
        mWindow.objectChecked(mDone);
    }
}

void ALScriptObjectCheck::passed()
{
    --mUnderWay;
    feed();
}

void ALScriptObjectCheck::begin(const Window::Listed::One& one)
{
    const std::weak_ptr<bool> alive      = mAlive;
    const U32                 generation = mGeneration;
    mWindow.read(one.ref, [this, alive, generation, one](std::optional<Window::Read> read) {
        if (!alive.lock() || generation != mGeneration)
        {
            return;
        }
        if (!read)
        {
            mDone.unread.push_back(one.name);
            passed();
            return;
        }
        checkRead(one, *read);
    });
}

void ALScriptObjectCheck::checkRead(const Window::Listed::One& one, const Window::Read& read)
{
    // Read as a tab reads it: as the preprocessor makes it where it runs,
    // or where the script went up in its envelope; as written otherwise.
    if (!read.enveloped && !mWindow.preprocessing())
    {
        ask(one, read, nullptr);
        return;
    }
    ALScriptPreprocessor::Request request;
    request.ref           = one.ref;
    request.name          = one.name;
    request.assetId       = read.assetId;
    request.source        = std::make_shared<const std::string>(read.text);
    request.lua           = read.lua;
    request.compileTarget = read.compileTarget;
    // The script apart from its modules, for the analyzers to check each on
    // its own, as a tab's check has it.
    request.apart                        = true;
    const std::weak_ptr<bool> alive      = mAlive;
    const U32                 generation = mGeneration;
    mWindow.expand(request, [this, alive, generation, one, read](const ALPreprocessor::Result& result) {
        if (alive.lock() && generation == mGeneration)
        {
            ask(one, read, std::make_shared<const ALPreprocessor::Result>(result));
        }
    });
}

void ALScriptObjectCheck::ask(const Window::Listed::One& one, const Window::Read& read, std::shared_ptr<const ALPreprocessor::Result> expansion)
{
    ALScriptAnalysis::Request request;
    request.kind = ALScriptAnalysis::Kind::Check;
    request.id   = "objectcheck:" + ALScriptPreprocessor::pathOf(one.ref);
    request.lua  = read.lua;
    request.mono = read.compileTarget != "lsl2";
    request.text = std::make_shared<const std::string>(read.text);
    if (read.lua)
    {
        // What the script's `.luaurc` says, where one is in hand; the
        // scripter's own choice of lints and mode otherwise.
        ALScriptPreprocessor::Request root;
        root.ref  = one.ref;
        root.name = one.name;
        root.lua  = true;
        if (!mWindow.luauConfig(root, request.config))
        {
            request.config = ALScriptLints::luauBase();
        }
    }
    if (expansion)
    {
        if (expansion->apart.valid)
        {
            request.text = std::make_shared<const std::string>(expansion->apart.script.text);
            auto modules = std::make_shared<ALLuauService::Modules>();
            for (const ALPreprocessor::Result::Piece& piece : expansion->apart.modules)
            {
                modules->modules.push_back({ piece.key, piece.text });
            }
            for (const ALPreprocessor::Result::Resolved& resolved : expansion->resolved)
            {
                if (resolved.require)
                {
                    modules->reaches.push_back({ resolved.from, resolved.name, resolved.path });
                }
            }
            request.modules = std::move(modules);
            request.bundle  = std::make_shared<const std::string>(expansion->text);
        }
        else
        {
            request.text = std::make_shared<const std::string>(expansion->text);
        }
        request.passedOver = (expansion->apart.valid ? expansion->apart.script.map : expansion->map).othersLines();
    }
    const std::weak_ptr<bool> alive      = mAlive;
    const U32                 generation = mGeneration;
    const bool                lua        = read.lua;
    mAnalysis.askAnalysis(std::move(request), [this, alive, generation, one, lua, expansion](const ALScriptAnalysis::Result& result) {
        if (alive.lock() && generation == mGeneration)
        {
            answered(one, lua, result, expansion);
        }
    });
}

void ALScriptObjectCheck::answered(const Window::Listed::One& one, bool lua, const ALScriptAnalysis::Result& result,
                                   std::shared_ptr<const ALPreprocessor::Result> expansion)
{
    std::vector<ALScriptProblem> problems = result.problems;
    // LSL's warnings as the scripter chose them; Luau's lints were chosen
    // in the configuration the check ran with.
    if (!lua)
    {
        ALScriptLints::apply(problems);
    }
    const ALSourceMap* map = nullptr;
    if (expansion)
    {
        map = expansion->apart.valid ? &expansion->apart.script.map : &expansion->map;
        std::vector<std::pair<std::string, ALSourceMap>> module_maps;
        for (const ALPreprocessor::Result::Piece& piece : expansion->apart.modules)
        {
            module_maps.emplace_back(piece.key, piece.map);
        }
        ALScriptStudioChecking::mapBack(problems, *map, module_maps);
    }
    // An include's rows under its name; code the preprocessor made, said so.
    const auto nameOf = [this, map](const std::string& file) {
        if (file == Doc::GENERATED)
        {
            return mServices.words("InGeneratedCode");
        }
        const S32 at = map ? map->fileOf(file) : -1;
        return at >= 0 ? map->files()[at].name : file;
    };
    Script script;
    script.ref  = one.ref;
    script.name = one.name;
    script.lua  = lua;
    // The preprocessor's own word, then the analyzers'. No fix is offered:
    // a fix is made in a tab, over the text it was made of.
    if (expansion)
    {
        for (const ALScriptProblem& problem : expansion->problems)
        {
            Doc::Shown row;
            row.level     = Doc::levelOf(problem.severity);
            row.origin    = mServices.words(problem.source == ALScriptProblem::Source::Optimizer ? "OriginOptimizer" : "OriginPreprocessor");
            row.message   = problem.message;
            row.line      = problem.line;
            row.column    = problem.column;
            row.hasColumn = true;
            row.endLine   = problem.endLine;
            row.endColumn = problem.endColumn;
            row.file      = problem.file;
            row.fileName  = problem.file.empty() ? std::string() : nameOf(problem.file);
            row.key       = problem.key;
            script.rows.push_back(std::move(row));
        }
    }
    for (const ALScriptProblem& problem : problems)
    {
        Doc::Shown row = Doc::analysisRow(problem, lua, mServices);
        row.fileName   = problem.file.empty() ? std::string() : nameOf(problem.file);
        script.rows.push_back(std::move(row));
    }
    ++mDone.checked;
    mWindow.scriptChecked(script);
    passed();
}

/**
 * @file alluaufrontend.cpp
 * @brief What the SLua analyzer keeps between questions: Luau's front end and all it is given.
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

#include "linden_common.h"

#include "alluaufrontend.h"

#include <algorithm>

#include "Luau/Ast.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Cancellation.h"

namespace
{
    Luau::FrontendOptions frontendOptions()
    {
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        // The types of every term stay with the module, which is what a
        // hover or a signature reads.
        options.retainFullTypeGraphs = true;
        return options;
    }
}

std::optional<Luau::ModuleInfo> ALLuauFrontend::ScriptResolver::resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* expr,
                                                                               const Luau::TypeCheckLimits&)
{
    const Luau::AstExprConstantString* said = expr ? expr->as<Luau::AstExprConstantString>() : nullptr;
    if (!context || !said)
    {
        return std::nullopt;
    }
    std::string key = context->name;
    key += '\x1f';
    key.append(said->value.data, said->value.size);
    const auto found = leadsTo.find(key);
    if (found == leadsTo.end())
    {
        return std::nullopt;
    }
    return Luau::ModuleInfo{ found->second, false };
}

std::optional<Luau::SourceCode> ALLuauFrontend::ScriptResolver::readSource(const Luau::ModuleName& name)
{
    const auto found = texts.find(name);
    if (found == texts.end())
    {
        return std::nullopt;
    }
    // What a require reaches is a module, which may be required; a
    // script is not.
    return Luau::SourceCode{ found->second, name.rfind("module:", 0) == 0 ? Luau::SourceCode::Module : Luau::SourceCode::Script };
}

const Luau::Config& ALLuauFrontend::ModeResolver::getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits&) const
{
    const auto found = configs.find(name);
    return found != configs.end() ? found->second : fallback;
}

Luau::Mode ALLuauFrontend::checkedIn(std::optional<Luau::Mode> asked) const
{
    return asked.value_or(solver == Luau::SolverMode::New ? Luau::Mode::Strict : Luau::Mode::Nonstrict);
}

void ALLuauFrontend::remode()
{
    for (auto& [name, config] : configs.configs)
    {
        const auto asked = askedModes.find(name);
        config.mode      = checkedIn(asked != askedModes.end() ? asked->second : std::nullopt);
    }
    configs.fallback.mode = checkedIn(std::nullopt);
}

Luau::FrontendOptions ALLuauFrontend::limited() const
{
    Luau::FrontendOptions options = frontendOptions();
    if (timeLimit > 0.0)
    {
        options.moduleTimeLimitSec = timeLimit;
    }
    options.cancellationToken = stop;
    // Each module as Luau checks it, for its lints: noted as the script's
    // whose check it is, and nothing more, since it may be on a thread of
    // the pool's.
    options.customModuleCheck = [this, script = moduleName](const Luau::SourceModule& source, const Luau::Module&) {
        const std::lock_guard<std::mutex> lock(checkedMutex);
        std::vector<std::string>&         noted = checkedNow[script];
        if (std::find(noted.begin(), noted.end(), source.name) == noted.end())
        {
            noted.push_back(source.name);
        }
    };
    return options;
}

std::vector<std::string> ALLuauFrontend::takeChecked()
{
    const std::lock_guard<std::mutex> lock(checkedMutex);
    const auto                        noted = checkedNow.find(moduleName);
    if (noted == checkedNow.end())
    {
        return std::vector<std::string>();
    }
    std::vector<std::string> out = std::move(noted->second);
    checkedNow.erase(noted);
    return out;
}

void ALLuauFrontend::forgetChecked(const std::string& name)
{
    const std::lock_guard<std::mutex> lock(checkedMutex);
    checkedNow.erase(name);
}

Luau::FrontendOptions ALLuauFrontend::autocompleteOptions() const
{
    Luau::FrontendOptions options = limited();
    options.runLintChecks         = false;
    options.forAutocomplete       = true;
    options.customModuleCheck     = nullptr;
    return options;
}

Luau::ModulePtr ALLuauFrontend::base() const
{
    return solver == Luau::SolverMode::New ? frontend->moduleResolver.getModule(moduleName)
                                           : frontend->moduleResolverForAutocomplete.getModule(moduleName);
}

bool ALLuauFrontend::baseCurrent() const
{
    return !frontend->isDirty(moduleName, /*forAutocomplete*/ solver == Luau::SolverMode::Old);
}

Luau::FrontendOptions ALLuauFrontend::baseOptions() const
{
    return solver == Luau::SolverMode::New ? limited() : autocompleteOptions();
}

const std::string* ALLuauFrontend::baseText() const
{
    const auto found = baseTexts.find(moduleName);
    return found != baseTexts.end() ? &found->second : nullptr;
}

bool ALLuauFrontend::stopRequested() const
{
    return stop && stop->requested();
}

void ALLuauFrontend::sync(std::string_view source)
{
    std::string& text = files.texts[moduleName];
    if (text != source)
    {
        text.assign(source);
        frontend->markDirty(moduleName);
    }
}

void ALLuauFrontend::timedOut(const Luau::ModulePtr& module)
{
    if (module && module->timeout)
    {
        frontend->markDirty(moduleName);
    }
}

bool ALLuauFrontend::stoppedIn(bool checked)
{
    if (!checked || !stopRequested())
    {
        return false;
    }
    wasStopped = true;
    frontend->markDirty(moduleName);
    return true;
}

Luau::CheckResult ALLuauFrontend::checkWithModules(const Luau::FrontendOptions& options)
{
    // Autocomplete's module is the old solver's alone.
    const bool autocomplete = options.forAutocomplete && solver == Luau::SolverMode::Old;
    // Several modules the script requires, to be checked now: else as
    // Luau's own check, which is the same on one thread. Its own, not
    // every kept script's.
    const auto   required = requiredBy.find(moduleName);
    const size_t modules  = required == requiredBy.end() ? 0 : required->second.size();
    if (modules < 2 || !frontend->isDirty(moduleName, autocomplete))
    {
        return frontend->check(moduleName, options);
    }
    if (!modulePool)
    {
        modulePool = std::make_unique<ALLuauTaskPool>(ALLuauTaskPool::threadsWanted());
    }
    // Luau tells its one internal error reporter the name of each module
    // as it begins to check it, on whichever thread that is. Given room
    // for the longest name it could be told first -- every module Luau
    // can check is one it can read -- no thread's copy reallocates the
    // string another is copying into or reading. The threads still race
    // over its characters, so which name it holds is only as sure as their
    // order: what nothing reads but the message of an internal error.
    size_t longest = moduleName.size();
    for (const auto& [name, text] : files.texts)
    {
        longest = std::max(longest, name.size());
    }
    frontend->iceHandler.moduleName.reserve(longest);
    frontend->queueModuleCheck(moduleName);
    const std::vector<Luau::ModuleName> checked =
        frontend->checkQueuedModules(options, [this](std::vector<std::function<void()>> tasks) { modulePool->run(std::move(tasks)); });
    // What each module checked now found, as Luau's check reports it.
    Luau::CheckResult              result;
    Luau::FrontendModuleResolver& resolver = autocomplete ? frontend->moduleResolverForAutocomplete : frontend->moduleResolver;
    for (const Luau::ModuleName& name : checked)
    {
        const Luau::ModulePtr module = resolver.getModule(name);
        if (!module)
        {
            continue;
        }
        if (module->cancelled)
        {
            return Luau::CheckResult();
        }
        if (module->timeout)
        {
            result.timeoutHits.push_back(name);
        }
        result.errors.insert(result.errors.end(), module->errors.begin(), module->errors.end());
        if (name == moduleName)
        {
            result.lintResult = module->lintResult;
        }
    }
    return result;
}

bool ALLuauFrontend::checkScript(Luau::CheckResult* result)
{
    const bool        dirty = frontend->isDirty(moduleName);
    Luau::CheckResult made  = checkWithModules(limited());
    if (dirty)
    {
        ++checks;
        timedOut(frontend->moduleResolver.getModule(moduleName));
        // The new solver's base is this module.
        if (solver == Luau::SolverMode::New)
        {
            if (stopRequested())
            {
                baseTexts.erase(moduleName);
            }
            else
            {
                baseTexts[moduleName] = files.texts[moduleName];
            }
        }
    }
    if (result)
    {
        *result = std::move(made);
    }
    return dirty;
}

Luau::ModulePtr ALLuauFrontend::queried(std::string_view source, bool completion)
{
    wasStopped = false;
    sync(source);
    if (solver == Luau::SolverMode::New)
    {
        return stoppedIn(checkScript()) ? nullptr : frontend->moduleResolver.getModule(moduleName);
    }
    if (!completion)
    {
        // The mode the script's check has, or will have: its own hot
        // comment, else its configuration's. Parsed only, which the
        // check then uses.
        frontend->parse(moduleName);
        const Luau::SourceModule* parsed = frontend->getSourceModule(moduleName);
        if (parsed && parsed->mode.value_or(configs.getConfig(moduleName, {}).mode) == Luau::Mode::Strict)
        {
            return stoppedIn(checkScript()) ? nullptr : frontend->moduleResolver.getModule(moduleName);
        }
    }
    if (frontend->isDirty(moduleName, /*forAutocomplete*/ true))
    {
        checkWithModules(autocompleteOptions());
        ++checks;
        if (stoppedIn(true))
        {
            baseTexts.erase(moduleName);
            return nullptr;
        }
        baseTexts[moduleName] = std::string(source);
        timedOut(frontend->moduleResolverForAutocomplete.getModule(moduleName));
    }
    return frontend->moduleResolverForAutocomplete.getModule(moduleName);
}

const ALLuauFrontend::Doc* ALLuauFrontend::docFor(const std::optional<std::string>& symbol) const
{
    if (!symbol)
    {
        return nullptr;
    }
    const auto it = docs.find(*symbol);
    return it == docs.end() ? nullptr : &it->second;
}

// static
std::unique_ptr<Luau::Frontend> ALLuauFrontend::plainFrontend(ScriptResolver& files, ModeResolver& configs, Luau::SolverMode solver)
{
    auto frontend = std::make_unique<Luau::Frontend>(solver, &files, &configs, frontendOptions());
    Luau::registerBuiltinGlobals(*frontend, frontend->globals);
    Luau::registerBuiltinGlobals(*frontend, frontend->globalsForAutocomplete, /*typeCheckForAutocomplete*/ true);
    return frontend;
}

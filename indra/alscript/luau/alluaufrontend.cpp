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
    return options;
}

Luau::FrontendOptions ALLuauFrontend::autocompleteOptions() const
{
    Luau::FrontendOptions options = limited();
    options.runLintChecks         = false;
    options.forAutocomplete       = true;
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

bool ALLuauFrontend::checkScript(Luau::CheckResult* result)
{
    const bool        dirty = frontend->isDirty(moduleName);
    Luau::CheckResult made  = frontend->check(moduleName, limited());
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
        frontend->check(moduleName, autocompleteOptions());
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

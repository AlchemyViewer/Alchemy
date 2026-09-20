/**
 * @file alluauservice.cpp
 * @brief The SLua analyzer over Second Life's fork of Luau.
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

#include "alluauservice.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/Linter.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/TypeArena.h"

namespace
{
    // The script under analysis is the only module there is.
    const char* const SCRIPT_MODULE = "script";

    // The package the definitions are loaded as. luau-lsp names it this
    // way, and secondlife.docs.json keys its entries "@sl-slua/global/...",
    // so the two line up when hover documentation arrives.
    const char* const DEFINITIONS_PACKAGE = "@sl-slua";

    // The one script, served from memory. Anything it might require does
    // not exist yet; that is phase 3's preprocessor.
    struct ScriptResolver final : public Luau::FileResolver
    {
        std::string text;

        std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override
        {
            if (name != SCRIPT_MODULE)
            {
                return std::nullopt;
            }
            return Luau::SourceCode{ text, Luau::SourceCode::Script };
        }
    };

    ALScriptProblem problemAt(const Luau::Location& where,
                              ALScriptProblem::Severity severity,
                              ALScriptProblem::Source source,
                              std::string code,
                              std::string message)
    {
        ALScriptProblem problem;
        problem.severity  = severity;
        problem.source    = source;
        problem.line      = static_cast<S32>(where.begin.line);
        problem.column    = static_cast<S32>(where.begin.column);
        problem.endLine   = static_cast<S32>(where.end.line);
        problem.endColumn = static_cast<S32>(where.end.column);
        problem.code      = std::move(code);
        problem.message   = std::move(message);
        return problem;
    }

    Luau::FrontendOptions frontendOptions()
    {
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        return options;
    }
}

struct ALLuauService::Impl
{
    ScriptResolver                  files;
    Luau::NullConfigResolver        configs;
    std::unique_ptr<Luau::Frontend> frontend;
    bool                            definitions = false;

    // A front end with Luau's own globals in it, ready for the definitions
    // or for a script. One is built for each set of definitions, because
    // the global type arena is frozen once they are in and a frozen arena
    // takes nothing more.
    static std::unique_ptr<Luau::Frontend> plainFrontend(ScriptResolver& files, Luau::NullConfigResolver& configs)
    {
        auto frontend = std::make_unique<Luau::Frontend>(&files, &configs, frontendOptions());
        Luau::registerBuiltinGlobals(*frontend, frontend->globals);
        return frontend;
    }
};

ALLuauService::ALLuauService()
:   mImpl(std::make_unique<Impl>())
{
    mImpl->frontend = Impl::plainFrontend(mImpl->files, mImpl->configs);
    Luau::freeze(mImpl->frontend->globals.globalTypes);
}

ALLuauService::~ALLuauService() = default;

bool ALLuauService::loadDefinitions(std::string_view source, std::string& error)
{
    std::unique_ptr<Luau::Frontend> frontend = Impl::plainFrontend(mImpl->files, mImpl->configs);
    Luau::LoadDefinitionFileResult loaded = frontend->loadDefinitionFile(
        frontend->globals, frontend->globals.globalScope, source, DEFINITIONS_PACKAGE, /*captureComments*/ false);
    if (!loaded.success)
    {
        // The first thing that went wrong, which is the one worth reading.
        if (!loaded.parseResult.errors.empty())
        {
            const Luau::ParseError& first = loaded.parseResult.errors.front();
            error = llformat("%u:%u: %s",
                             first.getLocation().begin.line + 1,
                             first.getLocation().begin.column + 1,
                             first.getMessage().c_str());
        }
        else if (loaded.module && !loaded.module->errors.empty())
        {
            error = Luau::toString(loaded.module->errors.front());
        }
        else
        {
            error = "the definitions did not load";
        }
        return false;
    }
    Luau::freeze(frontend->globals.globalTypes);
    mImpl->frontend    = std::move(frontend);
    mImpl->definitions = true;
    error.clear();
    return true;
}

bool ALLuauService::hasDefinitions() const
{
    return mImpl->definitions;
}

ALScriptProblems ALLuauService::check(std::string_view source)
{
    Impl& impl = *mImpl;
    impl.files.text.assign(source);
    impl.frontend->markDirty(SCRIPT_MODULE);
    Luau::CheckResult result = impl.frontend->check(SCRIPT_MODULE);

    ALScriptProblems problems;
    problems.reserve(result.errors.size() + result.lintResult.errors.size() + result.lintResult.warnings.size());
    for (const Luau::TypeError& error : result.errors)
    {
        const bool syntax = Luau::get_if<Luau::SyntaxError>(&error.data) != nullptr;
        problems.push_back(problemAt(error.location,
                                     ALScriptProblem::Severity::Error,
                                     syntax ? ALScriptProblem::Source::Parser : ALScriptProblem::Source::Types,
                                     std::string(),
                                     Luau::toString(error)));
    }
    for (const Luau::LintWarning& warning : result.lintResult.errors)
    {
        problems.push_back(problemAt(warning.location,
                                     ALScriptProblem::Severity::Error,
                                     ALScriptProblem::Source::Lint,
                                     Luau::LintWarning::getName(warning.code),
                                     warning.text));
    }
    for (const Luau::LintWarning& warning : result.lintResult.warnings)
    {
        problems.push_back(problemAt(warning.location,
                                     ALScriptProblem::Severity::Warning,
                                     ALScriptProblem::Source::Lint,
                                     Luau::LintWarning::getName(warning.code),
                                     warning.text));
    }
    return problems;
}

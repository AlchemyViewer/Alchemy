/**
 * @file allslservice.cpp
 * @brief The LSL analyzer over Tailslide.
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

#include "allslservice.h"

#include "llfile.h"

#include <tailslide/tailslide.hh>

#include <algorithm>

namespace
{
    // Which of Tailslide's numbers the parser itself reports; the rest are
    // the semantic passes'.
    bool fromParser(Tailslide::ErrorCode code)
    {
        return code == Tailslide::E_SYNTAX_ERROR || code == Tailslide::E_PARSER_STACK_DEPTH;
    }

    // Tailslide counts lines and columns from one.
    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }
}

struct ALLSLService::Impl
{
    bool builtins = false;
};

ALLSLService::ALLSLService()
:   mImpl(std::make_unique<Impl>())
{
}

ALLSLService::~ALLSLService() = default;

bool ALLSLService::loadBuiltins(const std::string& path, std::string& error)
{
    // Tailslide exits the process over a file it cannot open, so the file
    // is opened here first. A line it cannot read it reports on stderr and
    // skips, which the process survives.
    LLFILE* file = LLFile::fopen(path, "rb");
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    fclose(file);
    Tailslide::tailslide_init_builtins(path.c_str());
    mImpl->builtins = true;
    error.clear();
    return true;
}

bool ALLSLService::hasBuiltins() const
{
    return mImpl->builtins;
}

ALScriptProblems ALLSLService::check(std::string_view source, bool mono)
{
    // The passes in the order Tailslide's own tool runs them. The tree is
    // checked even after errors: the messages are the point here, and the
    // optimizer, which is what a broken tree would upset, is not run.
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript* script = parser.parseLSLBytes(source.data(), static_cast<int>(source.size()));
    if (script)
    {
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        script->propagateValues();
        script->finalPass();
        script->validateGlobals(mono);
        script->checkSymbols();
    }

    ALScriptProblems problems;
    for (Tailslide::LogMessage* message : parser.logger.getMessages())
    {
        ALScriptProblem problem;
        switch (message->getType())
        {
            case Tailslide::LOG_ERROR:
            case Tailslide::LOG_INTERNAL_ERROR:
                problem.severity = ALScriptProblem::Severity::Error;
                break;
            case Tailslide::LOG_WARN:
                problem.severity = ALScriptProblem::Severity::Warning;
                break;
            default:
                // What it is up to, which is not a problem.
                continue;
        }
        const Tailslide::ErrorCode code = message->getError();
        if (fromParser(code))
        {
            problem.source = ALScriptProblem::Source::Parser;
        }
        else if (problem.severity == ALScriptProblem::Severity::Warning)
        {
            problem.source = ALScriptProblem::Source::Lint;
        }
        else
        {
            problem.source = ALScriptProblem::Source::Types;
        }
        const auto* where  = message->getLoc();
        problem.line       = zeroBased(where->first_line);
        problem.column     = zeroBased(where->first_column);
        problem.endLine    = zeroBased(where->last_line);
        problem.endColumn  = zeroBased(where->last_column);
        problem.code       = std::to_string(static_cast<int>(code));
        problem.message    = message->getMessage();
        problems.push_back(std::move(problem));
    }
    return problems;
}

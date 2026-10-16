/**
 * @file alscriptstudiocommands.cpp
 * @brief Script Studio's commands by name: what each does, whether it can now, and whether it is on.
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

#include "alscriptstudiocommands.h"

bool ALScriptStudioCommands::add(const std::string& name, run_t run, test_t enabled, test_t checked)
{
    if (!mCommands.try_emplace(name, Command{ std::move(run), std::move(enabled), std::move(checked) }).second)
    {
        LL_WARNS("ScriptStudio") << "The command " << name << " is registered already" << LL_ENDL;
        return false;
    }
    return true;
}

bool ALScriptStudioCommands::addUnlisted(const std::string& name, run_t run, test_t enabled, test_t checked)
{
    return add(name, std::move(run), std::move(enabled), std::move(checked));
}

bool ALScriptStudioCommands::has(std::string_view name) const
{
    return mCommands.find(name) != mCommands.end();
}

void ALScriptStudioCommands::run(std::string_view name)
{
    if (const Command* command = find(name); command && command->run)
    {
        // A copy: what it does may register commands, which moves the
        // table's own.
        const run_t run = command->run;
        run();
    }
}

bool ALScriptStudioCommands::enabled(std::string_view name)
{
    const Command* command = find(name);
    return command && (!command->enabled || command->enabled());
}

bool ALScriptStudioCommands::checked(std::string_view name)
{
    const Command* command = find(name);
    return command && command->checked && command->checked();
}

bool ALScriptStudioCommands::runIfEnabled(std::string_view name)
{
    if (!enabled(name))
    {
        return false;
    }
    run(name);
    return true;
}

std::vector<std::string> ALScriptStudioCommands::names() const
{
    std::vector<std::string> out;
    out.reserve(mCommands.size());
    for (const auto& [name, command] : mCommands)
    {
        out.push_back(name);
    }
    return out;
}

const ALScriptStudioCommands::Command* ALScriptStudioCommands::find(std::string_view name)
{
    const auto found = mCommands.find(name);
    if (found != mCommands.end())
    {
        return &found->second;
    }
    if (mUnknownSaid.emplace(name).second)
    {
        LL_WARNS("ScriptStudio") << "No command is registered as " << name << LL_ENDL;
        mUnknownAsked.emplace_back(name);
    }
    return nullptr;
}

/**
 * @file alscript_test_checks.cpp
 * @brief What every binary of alscript's tests sets up for the process once,
 *        as the viewer has it before any script is checked, and the state
 *        guard's checks on what the script engines keep for the process.
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

#include "../core/alscriptengine.h"
#include "../lsl/allslservice.h"
#include "../luau/alluauservice.h"

#include "alteststateguard.h"

#include "Luau/Common.h"

#include <tailslide/tailslide.hh>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // Every one of Luau's flags, name=value a line, sorted.
    std::string luauFlags()
    {
        std::vector<std::string> lines;
        for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        {
            lines.push_back(std::string(flag->name) + "=" + (flag->value ? "true" : "false"));
        }
        for (Luau::FValue<int>* flag = Luau::FValue<int>::list; flag; flag = flag->next)
        {
            lines.push_back(std::string(flag->name) + "=" + std::to_string(flag->value));
        }
        std::sort(lines.begin(), lines.end());
        std::string text;
        for (const std::string& line : lines)
        {
            text += line + "\n";
        }
        return text;
    }

    // Whether a thread is still inside Tailslide.
    std::string engineOwner()
    {
        return al_script_engine::owner().load() == std::thread::id() ? "free" : "held by a thread";
    }

    // How many names Tailslide's builtins table holds: one table for the
    // process, only ever added to.
    std::string builtinCount()
    {
        AL_SCRIPT_ENGINE_HELD;
        Tailslide::ScopedScriptParser probe(nullptr);
        Tailslide::LSLSymbolTable* table = probe.context.builtins;
        return table ? std::to_string(table->getMap().size()) : std::string("none");
    }

    class ALScriptChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // The process as the viewer has it before it checks a script:
            // Luau's flags set as the grid's SLua needs them, which a
            // .config.luau run before any service was made would not see;
            // and the grid's builtins loaded first, so that every file of
            // builtins a test loads after them only adds to them, whatever
            // order the test files run in.
            ALLuauService::setUpProcess();
            ALLSLService lsl;
            std::string  error;
            if (!lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error))
            {
                LL_WARNS() << "The grid's builtins did not load: " << error << LL_ENDL;
            }

            // A case flips a flag of Luau's only for itself.
            ll_test::StateGuard::add("Luau's flags", luauFlags);
            // A case leaves no thread inside Tailslide, where the next
            // case's would count as a second thread in at once.
            ll_test::StateGuard::add("Tailslide's engine", engineOwner);
            // Nor has it put a second thread in while one was in: the count
            // grows with each clash, and names the case whose it was.
            ll_test::StateGuard::add("Tailslide's clashes", [] { return std::to_string(al_script_engine::clashes().load()); });
            // A case that loads builtins of its own says so: they stay for
            // every case after it.
            ll_test::StateGuard::add("Tailslide's builtins", builtinCount);
        }
    };
}

CATCH_REGISTER_LISTENER(ALScriptChecks)

/**
 * @file alscriptengine.h
 * @brief The one thread the LSL engine is used on, and the check that it is.
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

#pragma once

#include "llerror.h"

#include <atomic>
#include <thread>

// Tailslide's parser and lexer are reentrant and every parse owns its
// tree, but the builtins are one table for the whole process and a
// script's identifiers point straight into it: resolving a script adds
// to each builtin symbol's reference count, so two scripts resolved at
// once write the same counters. So every use of Tailslide is on one
// thread -- in the viewer the analysis thread, which the optimizer's runs
// are posted to as well (ALScriptAnalysis::runEngine) -- and nothing is
// locked. What stands at each way in is a check that no other thread is
// in: two at once is a mistake in whoever called, said where it happens
// rather than found later as a count gone wrong.
namespace al_script_engine
{
    // The thread in the engine, if any; how deep this thread is in it; and
    // how many times a thread came in while another was in, for the test
    // that says so.
    inline std::atomic<std::thread::id>& owner()
    {
        static std::atomic<std::thread::id> id{};
        return id;
    }
    inline int& depth()
    {
        thread_local int held = 0;
        return held;
    }
    inline std::atomic<unsigned>& clashes()
    {
        static std::atomic<unsigned> count{ 0 };
        return count;
    }
} // namespace al_script_engine

class ALScriptEngineHeld
{
public:
    ALScriptEngineHeld()
    {
        using namespace al_script_engine;
        if (depth()++ == 0)
        {
            std::thread::id none;
            if (!owner().compare_exchange_strong(none, std::this_thread::get_id()))
            {
                ++clashes();
                LL_WARNS_ONCE("ScriptEngine") << "Tailslide entered from a second thread while another is in it" << LL_ENDL;
            }
        }
    }
    ~ALScriptEngineHeld()
    {
        using namespace al_script_engine;
        if (--depth() == 0)
        {
            std::thread::id self = std::this_thread::get_id();
            owner().compare_exchange_strong(self, std::thread::id());
        }
    }
    ALScriptEngineHeld(const ALScriptEngineHeld&)            = delete;
    ALScriptEngineHeld& operator=(const ALScriptEngineHeld&) = delete;
};

// What every entry point writes at its top.
#define AL_SCRIPT_ENGINE_HELD const ALScriptEngineHeld al_engine_held

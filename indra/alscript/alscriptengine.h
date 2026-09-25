/**
 * @file alscriptengine.h
 * @brief The one lock the LSL engine is entered under.
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

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// Tailslide's parser and lexer are reentrant and every parse owns its
// tree, but the builtins are one table for the whole process and a
// script's identifiers point straight into it: resolving a script adds
// to each builtin symbol's reference count, so two scripts resolved at
// once write the same counters. The analyzer has a thread, the
// optimizer has another, and a test may call from wherever it likes, so
// every way into the engine is taken under this -- recursive, so that
// one way in may go by another.
//
// Held for a whole call rather than a whole parse: a service keeps its
// last tree and answers questions from it, and those answers read what
// the resolution wrote.
inline std::recursive_mutex& alScriptEngineLock()
{
    static std::recursive_mutex lock;
    return lock;
}

namespace al_script_engine
{
    // How many threads wait for the engine; how many times it has been
    // taken by a thread that did not hold it; how deep this thread's
    // hold is.
    inline std::atomic<int>& waiting()
    {
        static std::atomic<int> count{ 0 };
        return count;
    }
    inline std::atomic<unsigned>& taken()
    {
        static std::atomic<unsigned> count{ 0 };
        return count;
    }
    inline int& depth()
    {
        thread_local int held = 0;
        return held;
    }
} // namespace al_script_engine

// A hold of the engine: counted among those waiting while it waits, so
// that a long holder knows to let go between its rounds.
class ALScriptEngineHeld
{
public:
    ALScriptEngineHeld()
    {
        std::recursive_mutex& lock = alScriptEngineLock();
        if (!lock.try_lock())
        {
            ++al_script_engine::waiting();
            lock.lock();
            --al_script_engine::waiting();
        }
        if (al_script_engine::depth()++ == 0)
        {
            ++al_script_engine::taken();
        }
    }
    ~ALScriptEngineHeld()
    {
        --al_script_engine::depth();
        alScriptEngineLock().unlock();
    }
    ALScriptEngineHeld(const ALScriptEngineHeld&)            = delete;
    ALScriptEngineHeld& operator=(const ALScriptEngineHeld&) = delete;
};

// What every entry point writes at its top.
#define AL_SCRIPT_ENGINE_HELD const ALScriptEngineHeld al_engine_held

// Between the rounds of a long hold -- the optimizer's, the inliner's --
// the engine let go of while another thread waits for it, and taken
// again once that thread has had it: a check is not held up for the
// whole of an optimizer run over a large script, only for a round. The
// other thread may resolve a script in the meantime, as it might have
// between two calls, so what the caller keeps across must be its own
// tree's, not a builtin's count. Nothing where this thread's hold is not
// its only one, since letting go of an inner hold lets go of nothing.
inline void alScriptEngineYield()
{
    using namespace al_script_engine;
    if (depth() != 1 || waiting().load() == 0)
    {
        return;
    }
    const unsigned        was  = taken().load();
    std::recursive_mutex& lock = alScriptEngineLock();
    --depth();
    lock.unlock();
    // Not for ever, should the waiter have gone some other way.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (taken().load() == was && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::yield();
    }
    lock.lock();
    ++depth();
    ++taken();
}

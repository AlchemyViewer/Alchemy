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

#include <mutex>

// Tailslide's parser and lexer are reentrant and every parse owns its
// tree, but the builtins are one table for the whole process and a
// script's identifiers point straight into it: resolving a script adds
// to each builtin symbol's reference count, so two scripts resolved at
// once write the same counters. The analyzer has a thread, the
// optimizer has another, and a test may call from wherever it likes, so
// every way into the engine is taken under this -- recursive, since the
// optimizer runs the inliner within its own hold.
//
// Held for a whole call rather than a whole parse: a service keeps its
// last tree and answers questions from it, and those answers read what
// the resolution wrote.
inline std::recursive_mutex& alScriptEngineLock()
{
    static std::recursive_mutex lock;
    return lock;
}

// What every entry point writes at its top.
#define AL_SCRIPT_ENGINE_HELD const std::lock_guard<std::recursive_mutex> al_engine_held(alScriptEngineLock())

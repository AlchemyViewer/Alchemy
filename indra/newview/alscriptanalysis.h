/**
 * @file alscriptanalysis.h
 * @brief The analyzers on a thread of their own, checking scripts as they are typed.
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

#include "alscriptproblem.h"
#include "alscriptsymbol.h"
#include "llsingleton.h"

#include <functional>
#include <memory>
#include <string>

namespace LL
{
    template <class T> class ThreadPoolUsing;
    class WorkQueue;
}

// The LSL and SLua analyzers, each owned by one worker thread and asked
// about one script at a time, as doc/SCRIPT_STUDIO.md section 4.3 has it:
// what is wrong with it, what could go at a position, what is at one, and
// what a call there takes. A request carries the document's version, the
// answer comes back on the main thread, and whoever asked drops it when
// the document has moved on. The definitions the analyzers work from are
// the region's, by the paths the syntax cache gives, read on the worker.
//
// Tailslide's builtins are a table the library holds once for the whole
// process and adds to rather than replaces, so the LSL builtins are loaded
// once; the Luau definitions are read again when the region's change.
class ALScriptAnalysis : public LLSingleton<ALScriptAnalysis>
{
    LLSINGLETON(ALScriptAnalysis);
    ~ALScriptAnalysis() override;

public:
    enum class Kind : U8
    {
        Check,
        Complete,
        Hover,
        Signature
    };
    struct Request
    {
        Kind        kind    = Kind::Check;
        std::string id;
        U32         version = 0;
        bool        lua     = false;
        // LSL only: Mono's rules for a global initialiser, else LSO's.
        bool        mono    = true;
        std::string text;
        // Where, for anything but a check.
        S32         line   = 0;
        S32         column = 0;
    };
    struct Result
    {
        Kind                            kind    = Kind::Check;
        std::string                     id;
        U32                             version = 0;
        bool                            lua     = false;
        S32                             line    = 0;
        S32                             column  = 0;
        ALScriptProblems                problems;
        std::vector<ALScriptCompletion> completions;
        ALScriptHover                   hover;
        ALScriptSignature               signature;
        // Why the analyzer ran without its definitions, or nothing.
        std::string                     definitionsError;
    };
    typedef std::function<void(const Result&)> callback_t;

    // Asks the worker and answers on the main thread.
    void ask(Request request, callback_t callback);

    // The region's definitions changed: the Luau ones are read again
    // before the next check.
    void definitionsChanged();

private:
    void cleanupSingleton() override;
    void ensureStarted();

    struct Worker;
    std::unique_ptr<LL::ThreadPoolUsing<LL::WorkQueue>> mPool;
    // Touched only from the worker's own tasks, which one thread runs one
    // after another.
    std::unique_ptr<Worker>                             mWorker;
    U32                                                 mDefinitionsGeneration = 1;
};

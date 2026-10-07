/**
 * @file alscriptanalyzers.h
 * @brief Each language's analyzer on the analysis thread: a question in, its answer out.
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

#include "alscriptanalysis.h"
#include "allslservice.h"
#include "alluauservice.h"

#include <string>

// A language's analyzer, owned by the analysis thread: a question in, its
// answer out, over the service it keeps between questions and the
// definitions it was loaded with. What the main thread read for a question
// -- where the definitions are, the settings -- comes with it as Setup.
class ALScriptAnalyzer
{
public:
    typedef ALScriptAnalysis::Request Request;
    typedef ALScriptAnalysis::Result  Result;

    struct Setup
    {
        std::string         luauPath;
        std::string         docsPath;
        std::string         lslPath;
        U32                 generation = 0;
        bool                newSolver  = false;
        bool                fragments  = false;
        F32                 seconds    = 0.f;
        ALLuauService::Stop stop;
    };

    virtual ~ALScriptAnalyzer() = default;
    // `request` answered of `text` into `result`, which has the request's
    // own fields already: its kind's answer, and a check's or a weigh's
    // weights.
    virtual void answer(const Request& request, const std::string& text, const Setup& setup, Result& result) = 0;
    // Whether the last question was stopped part way by Setup::stop; the
    // stop let go of. Only SLua's are stopped.
    virtual bool stopped() const { return false; }
    virtual void forgetStop() {}

    // A file's whole text, or nothing where it cannot be read.
    static std::string readWhole(const std::string& path);
    // The weights a request asks for, of `text`: each target's, or where
    // there are variants, each variant's total for the first target --
    // for a weigh, or a check the script's own weigh was folded into.
    static void weigh(const Request& request, const std::string& text, Result& result);
};

// LSL's, over Tailslide: its builtins loaded as each region's come, what a
// region adds put into the table Tailslide keeps for the whole process,
// which is only ever added to.
class ALLSLAnalyzer final : public ALScriptAnalyzer
{
public:
    void answer(const Request& request, const std::string& text, const Setup& setup, Result& result) override;

private:
    void load(const std::string& path, U32 generation);

    ALLSLService mService;
    bool         mLoaded = false;
    std::string  mPath;
    U32          mGeneration = 0;
    std::string  mError;
};

// SLua's, over Luau: its definitions loaded again when the region's
// change, and its front end built again for the other solver.
class ALLuauAnalyzer final : public ALScriptAnalyzer
{
public:
    void answer(const Request& request, const std::string& text, const Setup& setup, Result& result) override;
    bool stopped() const override { return mService.stopped(); }
    void forgetStop() override { mService.setStop(nullptr); }

private:
    void load(const std::string& path, const std::string& docs_path, U32 generation);
    void useSolver(bool use_new);

    ALLuauService mService;
    std::string   mDefinitionsPath;
    std::string   mDocsPath;
    U32           mGeneration = 0;
    std::string   mError;
};

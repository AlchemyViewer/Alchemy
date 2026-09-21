/**
 * @file alscriptpreprocessor.h
 * @brief The preprocessor with its includes found and fetched from the world.
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

#include "alpreprocessor.h"
#include "alscriptworkspace.h"
#include "llsingleton.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// The preprocessor as the viewer runs it: the settings for what it does,
// the agent and the asset for its predefined macros, and its includes
// found by name -- in the object holding the script, in the agent's
// inventory (scripts and notecards alike), or in a folder on disk, in
// the order the setting says -- and fetched from the region when they
// are not in hand yet. A run over a script whose includes are not all in
// fetches them and runs again, and answers once, on the main thread,
// when every include has come or failed to. The texts fetched are kept
// for the session, by the asset they were, so that the analyzers can
// preprocess a script as it is typed without waiting on anything.
//
// An include's identity, as the source map names it, says where it came
// from: `object:<prim>:<item>`, `inventory:<item>`, or `disk:<path>`.
class ALScriptPreprocessor : public LLSingleton<ALScriptPreprocessor>
{
    LLSINGLETON(ALScriptPreprocessor);
    ~ALScriptPreprocessor() override;

public:
    // Whether a save preprocesses a plain script. One that came wrapped
    // in the envelope is preprocessed on saving whatever this says, so
    // that its source stays its source.
    static bool enabled();

    struct Request
    {
        ALScriptRef ref;
        // The script's name and asset, for `__SHORTFILE__` and `__ASSETID__`.
        std::string name;
        LLUUID      assetId;
        std::string source;
        bool        lua = false;
    };
    typedef std::function<void(const ALPreprocessor::Result&)> callback_t;

    // Runs, fetching whatever is missing and running again until nothing
    // is, then answers once.
    void run(const Request& request, callback_t callback);
    // Runs with what is in hand, the rest noted as pending: for the
    // analyzers, which cannot wait.
    ALPreprocessor::Result runNow(const Request& request);

    // An include's identity back to the item it names, or the file.
    static bool refOf(const std::string& path, ALScriptRef& ref);
    static bool fileOf(const std::string& path, std::string& file);

private:
    struct Job;
    struct Cached
    {
        LLUUID      assetId;
        std::string text;
    };
    // Something an include name could mean, in the order tried.
    struct Candidate
    {
        std::string path;
        std::string name;
        ALScriptRef ref;
        LLUUID      assetId;
        // A file on disk, read on the spot.
        std::string file;
    };
    // `unknown` says the object's contents have not been listed yet, so
    // a name not found may still be there.
    std::vector<Candidate> candidatesFor(const ALPreprocessor::Ask& ask, const Request& request, bool& unknown) const;
    ALPreprocessor::Found  resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Request& request, std::set<std::string>* wanted);
    ALPreprocessor::Result attempt(const Request& request, std::set<std::string>* wanted);
    ALPreprocessor::Options optionsFor(const Request& request);
    void                    attemptJob(const std::shared_ptr<Job>& job);

    std::map<std::string, Cached>                          mTexts;
    std::set<std::string>                                  mFailed;
    // What each prim was last said to hold.
    std::map<LLUUID, std::vector<ALScriptWorkspace::Item>> mContents;
};

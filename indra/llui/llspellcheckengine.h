/**
 * @file llspellcheckengine.h
 * @brief Abstract spell-check engine backend interface
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LLSPELLCHECKENGINE_H
#define LLSPELLCHECKENGINE_H

#include "stdtypes.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

// Platform spell-check backend used by LLSpellChecker. An engine only knows how to check and
// suggest words for a single active primary language, addressed by the viewer dictionary basename
// (e.g. "en_us"); everything else (dictionary metadata, the custom/ignore/glossary "accepted word"
// set, persistence, and the settings-change signal) lives in LLSpellChecker. Exactly one concrete
// engine is compiled per build (selected by CMake), and it provides the create() factory.
class LLSpellCheckEngine
{
public:
    virtual ~LLSpellCheckEngine() = default;

    // Activate the primary dictionary named by 'name' (a dictionaries.xml basename, e.g. "en_us").
    // Passing an empty string deactivates the engine. Returns true if a usable checker is active.
    virtual bool setLanguage(const std::string& name) = 0;

    // True when a usable language checker is currently active.
    virtual bool isActive() const = 0;

    // True if the word is spelled correctly according to the active language. Only called when the
    // engine is active and the word is at least 3 characters; the custom/ignore/glossary words are
    // handled by LLSpellChecker, not here.
    virtual bool checkWord(const std::string& word) const = 0;

    // Fill 'suggestions' with replacement candidates for a misspelled word; returns the count.
    virtual S32 getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const = 0;

    // Of the given candidate primary dictionary names, return those this engine can spell-check on
    // this system. The engine performs any (potentially expensive) OS query once for the whole
    // batch, so LLSpellChecker::refreshDictionaryMap() resolves all primaries in a single call.
    virtual std::set<std::string> getInstalledLanguages(const std::vector<std::string>& candidate_names) const = 0;

    // Construct the platform engine compiled into this build (Hunspell, NSSpellChecker, or the
    // Windows Spell Checking API). Defined in the selected engine translation unit.
    static std::unique_ptr<LLSpellCheckEngine> create();

    // Shared helper for the OS-list engines (NSSpellChecker, Windows): match a viewer dictionary
    // basename (e.g. "en_us") against the platform's available locale tags, joining language and
    // region with 'separator' ('_' for NSSpellChecker, '-' for BCP-47). Three passes: an exact
    // region-qualified match, then the bare language, then any installed variant of the same
    // language. Returns the matched platform tag (preserving the platform's casing) or "" if none.
    static std::string matchLanguage(const std::string& name, const std::vector<std::string>& available_tags, char separator);
};

#endif // LLSPELLCHECKENGINE_H

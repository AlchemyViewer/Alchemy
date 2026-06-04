/**
 * @file llspellcheckengine_hunspell.cpp
 * @brief Hunspell spell-check engine backend (default, cross-platform)
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

#include "linden_common.h"

#include "lldir.h"
#include "llspellcheck.h"
#include "llspellcheckengine.h"

#include <hunspell/hunspell.hxx>

#include <memory>

namespace
{
    class LLHunspellEngine final : public LLSpellCheckEngine
    {
    public:
        bool setLanguage(const std::string& name) override;
        bool isActive() const override { return (bool)mHunspell; }
        bool checkWord(const std::string& word) const override;
        S32  getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const override;
        std::set<std::string> getInstalledLanguages(const std::vector<std::string>& candidate_names) const override;
    private:
        std::unique_ptr<Hunspell> mHunspell;
    };

    bool LLHunspellEngine::setLanguage(const std::string& name)
    {
        mHunspell.reset();
        if (name.empty())
        {
            return false;
        }

        const std::string app_path = LLSpellChecker::getDictionaryAppPath();
        const std::string user_path = LLSpellChecker::getDictionaryUserPath();
        const std::string filename_aff = name + ".aff";
        const std::string filename_dic = name + ".dic";

        if ( (gDirUtilp->fileExists(user_path + filename_aff)) && (gDirUtilp->fileExists(user_path + filename_dic)) )
        {
            mHunspell = std::make_unique<Hunspell>((user_path + filename_aff).c_str(), (user_path + filename_dic).c_str());
        }
        else if ( (gDirUtilp->fileExists(app_path + filename_aff)) && (gDirUtilp->fileExists(app_path + filename_dic)) )
        {
            mHunspell = std::make_unique<Hunspell>((app_path + filename_aff).c_str(), (app_path + filename_dic).c_str());
        }

        return (bool)mHunspell;
    }

    bool LLHunspellEngine::checkWord(const std::string& word) const
    {
        return (mHunspell) && (0 != mHunspell->spell(word));
    }

    S32 LLHunspellEngine::getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const
    {
        suggestions.clear();
        if (!mHunspell)
        {
            return 0;
        }
        suggestions = mHunspell->suggest(word);
        return static_cast<S32>(suggestions.size());
    }

    std::set<std::string> LLHunspellEngine::getInstalledLanguages(const std::vector<std::string>& candidate_names) const
    {
        // Hunspell dictionaries are plain files, so availability is simply file presence.
        std::set<std::string> installed;
        const std::string app_path = LLSpellChecker::getDictionaryAppPath();
        const std::string user_path = LLSpellChecker::getDictionaryUserPath();
        for (const std::string& name : candidate_names)
        {
            if ( (gDirUtilp->fileExists(user_path + name + ".dic")) || (gDirUtilp->fileExists(app_path + name + ".dic")) )
            {
                installed.insert(name);
            }
        }
        return installed;
    }
}

// static
std::unique_ptr<LLSpellCheckEngine> LLSpellCheckEngine::create()
{
    return std::make_unique<LLHunspellEngine>();
}

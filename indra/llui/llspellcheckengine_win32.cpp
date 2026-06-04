/**
 * @file llspellcheckengine_win32.cpp
 * @brief Spell-check engine backed by the native Windows Spell Checking API
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

#include "llstring.h"
#include "llthread.h"

#include "llspellcheckengine.h"

#include <objbase.h>
#include <spellcheck.h>

#include <cwchar>
#include <string>

namespace
{
    // std::string (UTF-8) -> std::wstring (UTF-16). The default code page is CP_UTF8.
    std::wstring toWide(const std::string& str)
    {
        return ll_convert_string_to_wide(str.c_str(), str.length());
    }

    // A null-terminated wide string (e.g. an LPOLESTR returned by COM) -> UTF-8 std::string.
    std::string fromWide(const wchar_t* wstr)
    {
        return (wstr) ? ll_convert_wide_to_string(wstr, wcslen(wstr)) : std::string();
    }

    class LLWinSpellEngine final : public LLSpellCheckEngine
    {
    public:
        LLWinSpellEngine();
        ~LLWinSpellEngine() override;
        bool setLanguage(const std::string& name) override;
        bool isActive() const override { return (nullptr != mChecker); }
        bool checkWord(const std::string& word) const override;
        S32  getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const override;
        std::set<std::string> getInstalledLanguages(const std::vector<std::string>& candidate_names) const override;
    private:
        // The factory's supported language tags (e.g. "en-US"), as UTF-8 strings.
        std::vector<std::string> availableTags() const;

        ISpellCheckerFactory* mFactory = nullptr;
        ISpellChecker*        mChecker = nullptr;
        HRESULT               mComInitResult = E_FAIL;   // gates the matching CoUninitialize
    };

    LLWinSpellEngine::LLWinSpellEngine()
    {
        // The Windows Spell Checking API is COM-based and the viewer keeps no standing COM apartment
        // on the main thread, so own one for this engine's lifetime. Apartment-threaded (STA) matches
        // every existing scoped COM site and is reference-counted, so it nests safely.
        mComInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(mComInitResult) || (RPC_E_CHANGED_MODE == mComInitResult))
        {
            if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mFactory))))
            {
                mFactory = nullptr;
            }
        }
    }

    LLWinSpellEngine::~LLWinSpellEngine()
    {
        // Release COM interfaces before tearing down the apartment.
        if (mChecker)
        {
            mChecker->Release();
            mChecker = nullptr;
        }
        if (mFactory)
        {
            mFactory->Release();
            mFactory = nullptr;
        }
        // Balance CoInitializeEx only when we actually initialized (S_OK or S_FALSE); never on
        // RPC_E_CHANGED_MODE (a thread already in another apartment that we did not initialize).
        if (SUCCEEDED(mComInitResult))
        {
            CoUninitialize();
        }
    }

    std::vector<std::string> LLWinSpellEngine::availableTags() const
    {
        std::vector<std::string> tags;
        if (!mFactory)
        {
            return tags;
        }
        IEnumString* langs = nullptr;
        if (SUCCEEDED(mFactory->get_SupportedLanguages(&langs)) && langs)
        {
            LPOLESTR psz = nullptr;
            ULONG fetched = 0;
            while ((langs->Next(1, &psz, &fetched) == S_OK) && (1 == fetched) && psz)
            {
                tags.push_back(fromWide(psz));
                CoTaskMemFree(psz);
                psz = nullptr;
            }
            langs->Release();
        }
        return tags;
    }

    bool LLWinSpellEngine::setLanguage(const std::string& name)
    {
        llassert(on_main_thread());
        if (mChecker)
        {
            mChecker->Release();
            mChecker = nullptr;
        }
        if ( (!mFactory) || (name.empty()) )
        {
            return false;
        }

        const std::string tag = LLSpellCheckEngine::matchLanguage(name, availableTags(), '-');
        if (tag.empty())
        {
            return false;
        }

        const std::wstring wtag = toWide(tag);
        // CreateSpellChecker returns E_INVALIDARG when no checker is available for the language.
        if (FAILED(mFactory->CreateSpellChecker(wtag.c_str(), &mChecker)) || (!mChecker))
        {
            mChecker = nullptr;
            return false;
        }
        return true;
    }

    bool LLWinSpellEngine::checkWord(const std::string& word) const
    {
        llassert(on_main_thread());
        if (!mChecker)
        {
            return true;
        }

        const std::wstring wword = toWide(word);
        if (wword.empty())
        {
            return true;
        }

        IEnumSpellingError* errors = nullptr;
        if (FAILED(mChecker->Check(wword.c_str(), &errors)) || (!errors))
        {
            return true;
        }

        // A correctly spelled word yields an empty enumeration: Next() returns S_FALSE. Any fetched
        // ISpellingError (S_OK) means the word is misspelled.
        ISpellingError* error = nullptr;
        const bool misspelled = (S_OK == errors->Next(&error)) && (nullptr != error);
        if (error)
        {
            error->Release();
        }
        errors->Release();

        return !misspelled;
    }

    S32 LLWinSpellEngine::getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const
    {
        llassert(on_main_thread());
        suggestions.clear();
        if (!mChecker)
        {
            return 0;
        }

        const std::wstring wword = toWide(word);
        if (wword.empty())
        {
            return 0;
        }

        IEnumString* enum_str = nullptr;
        const HRESULT hr = mChecker->Suggest(wword.c_str(), &enum_str);
        if (FAILED(hr) || (!enum_str))
        {
            return 0;
        }

        // Suggest() returns S_FALSE when the word is already spelled correctly, in which case the
        // enumeration just echoes the input word - skip it so we don't offer the word as its own fix.
        if (S_OK == hr)
        {
            LPOLESTR psz = nullptr;
            ULONG fetched = 0;
            while ((enum_str->Next(1, &psz, &fetched) == S_OK) && (1 == fetched) && psz)
            {
                suggestions.push_back(fromWide(psz));
                CoTaskMemFree(psz);
                psz = nullptr;
            }
        }
        enum_str->Release();

        return static_cast<S32>(suggestions.size());
    }

    std::set<std::string> LLWinSpellEngine::getInstalledLanguages(const std::vector<std::string>& candidate_names) const
    {
        llassert(on_main_thread());
        std::set<std::string> installed;
        if (!mFactory)
        {
            return installed;
        }
        const std::vector<std::string> available = availableTags();   // one OS query for the batch
        for (const std::string& name : candidate_names)
        {
            if (!LLSpellCheckEngine::matchLanguage(name, available, '-').empty())
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
    return std::make_unique<LLWinSpellEngine>();
}

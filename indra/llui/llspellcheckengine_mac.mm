/**
 * @file llspellcheckengine_mac.mm
 * @brief Spell-check engine backed by the native macOS NSSpellChecker
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

#include "llspellcheckengine.h"

#import <AppKit/AppKit.h>

namespace
{
    NSSpellChecker* nsChecker()
    {
        return [NSSpellChecker sharedSpellChecker];
    }

    // std::string (UTF-8) -> NSString. Returns nil on invalid UTF-8 (callers must guard).
    NSString* toNS(const std::string& str)
    {
        return [NSString stringWithUTF8String:str.c_str()];
    }

    std::string fromNS(NSString* str)
    {
        if (!str)
        {
            return std::string();
        }
        const char* utf8 = [str UTF8String];
        return (utf8) ? std::string(utf8) : std::string();
    }

    class LLNSSpellEngine final : public LLSpellCheckEngine
    {
    public:
        bool setLanguage(const std::string& name) override;
        bool isActive() const override { return mEnabled; }
        bool checkWord(const std::string& word) const override;
        S32  getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const override;
        std::set<std::string> getInstalledLanguages(const std::vector<std::string>& candidate_names) const override;
    private:
        // The spell checker's installed language codes (e.g. "en_US"), as UTF-8 strings.
        static std::vector<std::string> availableTags();

        bool mEnabled = false;
        std::string mNSLanguage;   // Resolved NSSpellChecker language code (e.g. "en_US")
    };

    // static
    std::vector<std::string> LLNSSpellEngine::availableTags()
    {
        std::vector<std::string> tags;
        @autoreleasepool
        {
            for (NSString* lang in [nsChecker() availableLanguages])
            {
                tags.push_back(fromNS(lang));
            }
        }
        return tags;
    }

    bool LLNSSpellEngine::setLanguage(const std::string& name)
    {
        llassert([NSThread isMainThread]);
        mEnabled = false;
        mNSLanguage.clear();
        if (name.empty())
        {
            return false;
        }

        const std::string ns_lang = LLSpellCheckEngine::matchLanguage(name, availableTags(), '_');
        if (ns_lang.empty())
        {
            return false;
        }

        @autoreleasepool
        {
            NSString* ns_lang_str = toNS(ns_lang);
            // setLanguage: returns NO if the language can't be matched; treat that as disabled.
            if ( (!ns_lang_str) || (![nsChecker() setLanguage:ns_lang_str]) )
            {
                return false;
            }
        }
        mNSLanguage = ns_lang;
        mEnabled = true;
        return true;
    }

    bool LLNSSpellEngine::checkWord(const std::string& word) const
    {
        // NSSpellChecker is AppKit; use it only on the main thread (where text widgets lay out).
        llassert([NSThread isMainThread]);
        if (!mEnabled)
        {
            return true;
        }

        @autoreleasepool
        {
            NSString* ns_word = toNS(word);
            if (!ns_word)
            {
                return true;
            }
            NSRange range = [nsChecker() checkSpellingOfString:ns_word
                                                    startingAt:0
                                                      language:toNS(mNSLanguage)
                                                          wrap:NO
                                        inSpellDocumentWithTag:0
                                                     wordCount:NULL];
            return (range.location == NSNotFound);
        }
    }

    S32 LLNSSpellEngine::getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const
    {
        llassert([NSThread isMainThread]);
        suggestions.clear();
        if (!mEnabled)
        {
            return 0;
        }

        @autoreleasepool
        {
            NSString* ns_word = toNS(word);
            if (!ns_word)
            {
                return 0;
            }
            // The range must be expressed in NSString (UTF-16) units, not std::string bytes.
            NSArray* guesses = [nsChecker() guessesForWordRange:NSMakeRange(0, [ns_word length])
                                                       inString:ns_word
                                                       language:toNS(mNSLanguage)
                                         inSpellDocumentWithTag:0];
            for (NSString* guess in guesses)
            {
                suggestions.push_back(fromNS(guess));
            }
        }
        return static_cast<S32>(suggestions.size());
    }

    std::set<std::string> LLNSSpellEngine::getInstalledLanguages(const std::vector<std::string>& candidate_names) const
    {
        llassert([NSThread isMainThread]);
        std::set<std::string> installed;
        const std::vector<std::string> available = availableTags();   // one OS query for the batch
        for (const std::string& name : candidate_names)
        {
            if (!LLSpellCheckEngine::matchLanguage(name, available, '_').empty())
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
    return std::make_unique<LLNSSpellEngine>();
}

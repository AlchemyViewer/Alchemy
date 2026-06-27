/**
 * @file llspellcheck.cpp
 * @brief Spell checking functionality
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
#include "llsdserialize.h"

#include "llspellcheck.h"
#include "llspellcheckengine.h"

#include <algorithm>
#include <set>

static const std::string DICT_DIR = "dictionaries";
static const std::string DICT_FILE_CUSTOM = "user_custom.dic";
static const std::string DICT_FILE_IGNORE = "user_ignore.dic";

static const std::string DICT_FILE_MAIN = "dictionaries.xml";
static const std::string DICT_FILE_USER = "user_dictionaries.xml";

LLSpellChecker::settings_change_signal_t LLSpellChecker::sSettingsChangeSignal;

namespace
{
    // Read a viewer ".dic" word list (first line is a word count) into 'out', appending each word.
    void readDictWords(const std::string& dict_path, std::vector<std::string>& out)
    {
        if (!gDirUtilp->fileExists(dict_path))
        {
            return;
        }
        llifstream file_in(dict_path.c_str(), std::ios::in);
        if (!file_in.is_open())
        {
            return;
        }
        std::string word; int line_num = 0;
        while (std::getline(file_in, word))
        {
            // Skip over the first line since that's just a line count
            if ((0 != line_num) && (!word.empty()))
            {
                out.push_back(word);
            }
            line_num++;
        }
    }
}

// static
std::string LLSpellCheckEngine::matchLanguage(const std::string& name, const std::vector<std::string>& available_tags, char separator)
{
    // Build the region-qualified candidate: lowercased language, uppercased region, joined by
    // 'separator' (e.g. "en_us" -> "en" + sep + "US"); a name without a region stays bare.
    std::string lang(name);
    std::string candidate;
    const size_t us = name.find('_');
    if (us == std::string::npos)
    {
        LLStringUtil::toLower(lang);
        candidate = lang;
    }
    else
    {
        lang = name.substr(0, us);
        std::string region = name.substr(us + 1);
        LLStringUtil::toLower(lang);
        LLStringUtil::toUpper(region);
        candidate = lang + separator + region;
    }
    std::string candidate_lower(candidate);
    LLStringUtil::toLower(candidate_lower);

    // Pass 1: exact region-qualified match (case-insensitive).
    for (const std::string& tag : available_tags)
    {
        std::string tag_lower(tag);
        LLStringUtil::toLower(tag_lower);
        if (tag_lower == candidate_lower)
        {
            return tag;
        }
    }
    // Pass 2: bare language match (the OS often exposes only region-specific tags, so this may miss).
    for (const std::string& tag : available_tags)
    {
        std::string tag_lower(tag);
        LLStringUtil::toLower(tag_lower);
        if (tag_lower == lang)
        {
            return tag;
        }
    }
    // Pass 3: any installed variant of the same primary language (e.g. want "en_au", have "en-GB").
    for (const std::string& tag : available_tags)
    {
        std::string tag_lower(tag);
        LLStringUtil::toLower(tag_lower);
        const size_t sep = tag_lower.find_first_of("-_");
        const std::string tag_lang = (sep == std::string::npos) ? tag_lower : tag_lower.substr(0, sep);
        if (tag_lang == lang)
        {
            return tag;
        }
    }
    return std::string();
}

LLSpellChecker::LLSpellChecker()
{
    mEngine = LLSpellCheckEngine::create();

    // Load initial dictionary information
    refreshDictionaryMap();
}

// Defined here (not defaulted in the header) so unique_ptr<LLSpellCheckEngine> can destroy a
// complete engine type. (pImpl idiom.)
LLSpellChecker::~LLSpellChecker() = default;

bool LLSpellChecker::checkSpelling(const std::string& word) const
{
    if ( (!mEngine) || (!mEngine->isActive()) || (word.length() < 3) )
    {
        return true;
    }
    // The engine checks only the active language; custom/ignore/glossary words are accepted by us.
    if (mEngine->checkWord(word))
    {
        return true;
    }
    return isAccepted(word);
}

S32 LLSpellChecker::getSuggestions(const std::string& word, std::vector<std::string>& suggestions) const
{
    suggestions.clear();
    if ( (!mEngine) || (!mEngine->isActive()) || (word.length() < 3) )
    {
        return 0;
    }
    return mEngine->getSuggestions(word, suggestions);
}

bool LLSpellChecker::isAccepted(const std::string& word) const
{
    if (mAcceptedWords.empty())
    {
        return false;
    }
    std::string word_lower(word);
    LLStringUtil::toLower(word_lower);
    return (mAcceptedWords.find(word_lower) != mAcceptedWords.end());
}

const LLSD LLSpellChecker::getDictionaryData(const std::string& dict_language)
{
    for (LLSD::array_const_iterator it = mDictMap.beginArray(); it != mDictMap.endArray(); ++it)
    {
        const LLSD& dict_entry = *it;
        if (dict_language == dict_entry["language"].asString())
        {
            return dict_entry;
        }
    }
    return LLSD();
}

bool LLSpellChecker::hasDictionary(const std::string& dict_language, bool check_installed)
{
    const LLSD dict_info = getDictionaryData(dict_language);
    return dict_info.has("language") && ( (!check_installed) || (dict_info["installed"].asBoolean()) );
}

void LLSpellChecker::setDictionaryData(const LLSD& dict_info)
{
    const std::string dict_language = dict_info["language"].asString();
    if (dict_language.empty())
    {
        return;
    }

    for (LLSD::array_iterator it = mDictMap.beginArray(); it != mDictMap.endArray(); ++it)
    {
        LLSD& dict_entry = *it;
        if (dict_language == dict_entry["language"].asString())
        {
            dict_entry = dict_info;
            return;
        }
    }
    mDictMap.append(dict_info);
    return;
}

void LLSpellChecker::refreshDictionaryMap()
{
    const std::string app_path = getDictionaryAppPath();
    const std::string user_path = getDictionaryUserPath();

    // Load dictionary information (file name, friendly name, ...)
    std::string user_filename(user_path + DICT_FILE_MAIN);
    llifstream user_file(user_filename.c_str(), std::ios::binary);
    if ( (!user_file.is_open())
        || (LLSDParser::PARSE_FAILURE == LLSDSerialize::fromXMLDocument(mDictMap, user_file))
        || (0 == mDictMap.size()) )
    {
        std::string app_filename(app_path + DICT_FILE_MAIN);
        llifstream app_file(app_filename.c_str(), std::ios::binary);
        if ( (!app_file.is_open())
            || (LLSDParser::PARSE_FAILURE == LLSDSerialize::fromXMLDocument(mDictMap, app_file))
            || (0 == mDictMap.size()) )
        {
            return;
        }
    }

    // Load user installed dictionary information
    user_filename = user_path + DICT_FILE_USER;
    llifstream custom_file(user_filename.c_str(), std::ios::binary);
    if (custom_file.is_open())
    {
        LLSD custom_dict_map;
        LLSDSerialize::fromXMLDocument(custom_dict_map, custom_file);
        for (LLSD::array_iterator it = custom_dict_map.beginArray(); it != custom_dict_map.endArray(); ++it)
        {
            LLSD& dict_info = *it;
            dict_info["user_installed"] = true;
            setDictionaryData(dict_info);
        }
        custom_file.close();
    }

    // Determine which dictionaries are usable. Primary dictionaries are resolved by the active
    // engine in a single batch (file presence for Hunspell, OS language availability for the native
    // backends, each of which queries the OS at most once); non-primary glossaries (e.g. the SL
    // glossary) are detected purely by file presence.
    std::vector<std::string> primary_names;
    for (LLSD::array_const_iterator it = mDictMap.beginArray(); it != mDictMap.endArray(); ++it)
    {
        const LLSD& sdDict = *it;
        if ((sdDict.has("name")) && (sdDict["is_primary"].asBoolean()))
        {
            primary_names.push_back(sdDict["name"].asString());
        }
    }
    const std::set<std::string> installed_primary =
        (mEngine) ? mEngine->getInstalledLanguages(primary_names) : std::set<std::string>();

    for (LLSD::array_iterator it = mDictMap.beginArray(); it != mDictMap.endArray(); ++it)
    {
        LLSD& sdDict = *it;
        if (!sdDict.has("name"))
        {
            sdDict["installed"] = false;
            continue;
        }

        const std::string name = sdDict["name"].asString();
        if (sdDict["is_primary"].asBoolean())
        {
            sdDict["installed"] = (installed_primary.find(name) != installed_primary.end());
        }
        else
        {
            sdDict["installed"] =
                (gDirUtilp->fileExists(user_path + name + ".dic")) || (gDirUtilp->fileExists(app_path + name + ".dic"));
        }
    }

    sSettingsChangeSignal();
}

void LLSpellChecker::addToCustomDictionary(const std::string& word)
{
    std::string word_lower(word);
    LLStringUtil::toLower(word_lower);
    if (!mAcceptedWords.contains(word_lower))
    {
        addToDictFile(getDictionaryUserPath() + DICT_FILE_CUSTOM, word);

        mAcceptedWords.insert(word_lower);
        sSettingsChangeSignal();
    }
}

void LLSpellChecker::addToIgnoreList(const std::string& word)
{
    std::string word_lower(word);
    LLStringUtil::toLower(word_lower);
    if (mIgnoreList.end() == std::find(mIgnoreList.begin(), mIgnoreList.end(), word_lower))
    {
        mIgnoreList.push_back(word_lower);
        addToDictFile(getDictionaryUserPath() + DICT_FILE_IGNORE, word_lower);
        mAcceptedWords.insert(word_lower);
        sSettingsChangeSignal();
    }
}

void LLSpellChecker::addToDictFile(const std::string& dict_path, const std::string& word)
{
    std::vector<std::string> word_list;
    readDictWords(dict_path, word_list);
    word_list.push_back(word);

    llofstream file_out(dict_path.c_str(), std::ios::out | std::ios::trunc);
    if (file_out.is_open())
    {
        file_out << word_list.size() << std::endl;
        for (std::vector<std::string>::const_iterator itWord = word_list.begin(); itWord != word_list.end(); ++itWord)
        {
            file_out << *itWord << std::endl;
        }
        file_out.close();
    }
}

bool LLSpellChecker::isActiveDictionary(const std::string& dict_language) const
{
    return
        (mDictLanguage == dict_language) ||
        (mDictSecondary.end() != std::find(mDictSecondary.begin(), mDictSecondary.end(), dict_language));
}

void LLSpellChecker::setSecondaryDictionaries(dict_list_t dict_list)
{
    if (!getUseSpellCheck())
    {
        return;
    }

    if (mDictSecondary == dict_list)
    {
        return;
    }

    // Secondary dictionaries are accepted-word sources (not separate engine languages), so a change
    // just rebuilds the accepted-word set - no need to recreate the engine.
    mDictSecondary = dict_list;
    rebuildAcceptedWords();
    sSettingsChangeSignal();
}

void LLSpellChecker::rebuildAcceptedWords()
{
    mIgnoreList.clear();
    mAcceptedWords.clear();

    const std::string app_path = getDictionaryAppPath();
    const std::string user_path = getDictionaryUserPath();

    // Session ignore list (user_ignore.dic) - kept lowercased for dedup and accepted-word lookup.
    std::vector<std::string> ignore_words;
    readDictWords(user_path + DICT_FILE_IGNORE, ignore_words);
    for (std::string& word : ignore_words)
    {
        LLStringUtil::toLower(word);
        mIgnoreList.push_back(word);
        mAcceptedWords.insert(word);
    }

    // User custom dictionary (user_custom.dic).
    std::vector<std::string> custom_words;
    readDictWords(user_path + DICT_FILE_CUSTOM, custom_words);
    for (std::string& word : custom_words)
    {
        LLStringUtil::toLower(word);
        mAcceptedWords.insert(word);
    }

    // Secondary dictionaries (e.g. the SL glossary "sl") contribute their whole word list.
    for (const std::string& dict_language : mDictSecondary)
    {
        const LLSD dict_entry = getDictionaryData(dict_language);
        if ( (!dict_entry.isDefined()) || (!dict_entry["installed"].asBoolean()) || (!dict_entry.has("name")) )
        {
            continue;
        }

        const std::string filename_dic = dict_entry["name"].asString() + ".dic";
        std::vector<std::string> words;
        if (gDirUtilp->fileExists(user_path + filename_dic))
        {
            readDictWords(user_path + filename_dic, words);
        }
        else if (gDirUtilp->fileExists(app_path + filename_dic))
        {
            readDictWords(app_path + filename_dic, words);
        }
        for (std::string& word : words)
        {
            LLStringUtil::toLower(word);
            mAcceptedWords.insert(word);
        }
    }
}

void LLSpellChecker::activateDictionary(const std::string& dict_language)
{
    mDictLanguage.clear();
    mIgnoreList.clear();
    mAcceptedWords.clear();

    const LLSD dict_entry = (!dict_language.empty()) ? getDictionaryData(dict_language) : LLSD();
    const bool usable =
        (dict_entry.isDefined()) && (dict_entry["installed"].asBoolean()) && (dict_entry["is_primary"].asBoolean());
    // An empty name deactivates the engine; otherwise it activates the named primary dictionary.
    const std::string name = (usable) ? dict_entry["name"].asString() : std::string();

    if ( (mEngine) && (mEngine->setLanguage(name)) )
    {
        mDictLanguage = dict_language;
        rebuildAcceptedWords();
    }

    sSettingsChangeSignal();
}

// static
const std::string LLSpellChecker::getDictionaryAppPath()
{
    std::string dict_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, DICT_DIR, "");
    return dict_path;
}

// static
const std::string LLSpellChecker::getDictionaryUserPath()
{
    std::string dict_path = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, DICT_DIR, "");
    LLFile::mkdir(dict_path);
    return dict_path;
}

// static
bool LLSpellChecker::getUseSpellCheck()
{
    return (LLSpellChecker::instanceExists()) && (LLSpellChecker::instance().mEngine) && (LLSpellChecker::instance().mEngine->isActive());
}

bool LLSpellChecker::canRemoveDictionary(const std::string& dict_language)
{
    // Only user-installed inactive dictionaries can be removed (native backends never set
    // "user_installed", so removal is naturally unavailable for system spell-check languages).
    const LLSD dict_info = getDictionaryData(dict_language);
    return
        (dict_info["user_installed"].asBoolean()) &&
        ( (!getUseSpellCheck()) || (!isActiveDictionary(dict_language)) );
}

void LLSpellChecker::removeDictionary(const std::string& dict_language)
{
    if (!canRemoveDictionary(dict_language))
    {
        return;
    }

    LLSD dict_map = loadUserDictionaryMap();
    for (LLSD::array_const_iterator it = dict_map.beginArray(); it != dict_map.endArray(); ++it)
    {
        const LLSD& dict_info = *it;
        if (dict_info["language"].asString() == dict_language)
        {
            const std::string dict_dic = getDictionaryUserPath() + dict_info["name"].asString() + ".dic";
            if (gDirUtilp->fileExists(dict_dic))
            {
                LLFile::remove(dict_dic);
            }
            const std::string dict_aff = getDictionaryUserPath() + dict_info["name"].asString() + ".aff";
            if (gDirUtilp->fileExists(dict_aff))
            {
                LLFile::remove(dict_aff);
            }
            dict_map.erase((LLSD::Integer)(it - dict_map.beginArray()));
            break;
        }
    }
    saveUserDictionaryMap(dict_map);

    refreshDictionaryMap();
}

// static
LLSD LLSpellChecker::loadUserDictionaryMap()
{
    LLSD dict_map;
    std::string dict_filename(getDictionaryUserPath() + DICT_FILE_USER);
    llifstream dict_file(dict_filename.c_str(), std::ios::binary);
    if (dict_file.is_open())
    {
        LLSDSerialize::fromXMLDocument(dict_map, dict_file);
        dict_file.close();
    }
    return dict_map;
}

// static
void LLSpellChecker::saveUserDictionaryMap(const LLSD& dict_map)
{
    llofstream dict_file((getDictionaryUserPath() + DICT_FILE_USER).c_str(), std::ios::trunc);
    if (dict_file.is_open())
    {
        LLSDSerialize::toPrettyXML(dict_map, dict_file);
        dict_file.close();
    }
}

// static
boost::signals2::connection LLSpellChecker::setSettingsChangeCallback(const settings_change_signal_t::slot_type& cb)
{
    return sSettingsChangeSignal.connect(cb);
}

// static
void LLSpellChecker::setUseSpellCheck(const std::string& dict_language)
{
    if ( (((dict_language.empty()) && (getUseSpellCheck())) || (!dict_language.empty())) &&
         (LLSpellChecker::instance().mDictLanguage != dict_language) )
    {
        LLSpellChecker::instance().activateDictionary(dict_language);
    }
}

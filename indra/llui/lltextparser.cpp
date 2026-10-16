/**
 * @file lltextparser.cpp
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Kitty Barnett
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

#include "lltextparser.h"

#include "llsd.h"
#include "llsdserialize.h"
#include "llerror.h"
#include "lluuid.h"
#include "llstring.h"
#include "message.h"
#include "llmath.h"
#include "v4color.h"
#include "lldir.h"
#include "lluicolor.h"

#include <algorithm>
#include <boost/algorithm/string.hpp>

//
// LLHighlightEntry
//

LLHighlightEntry::LLHighlightEntry()
    : mCategoryMask(CAT_NONE)
    , mCondition(CONTAINS)
    , mCaseSensitive(false)
    , mColor(LLColor4::white)
    , mColorReadOnly(true)
    , mHighlightType(PART)
    , mFlashWindow(false)
    , mId(LLUUID::generateNewID())
{
}

LLHighlightEntry::LLHighlightEntry(const LLSD& sdEntry)
    : mCategoryMask(CAT_NONE)
    , mCondition(CONTAINS)
    , mCaseSensitive(false)
    , mColor(LLColor4::white)
    , mColorReadOnly(true)
    , mHighlightType(PART)
    , mFlashWindow(false)
    , mId(LLUUID::generateNewID())
{
    if (sdEntry.has("id"))
        mId = sdEntry["id"].asUUID();
    if (sdEntry.has("category_mask"))
        mCategoryMask = sdEntry["category_mask"].asInteger();
    if (sdEntry.has("condition"))
        mCondition = (EConditionType)sdEntry["condition"].asInteger();
    if (sdEntry.has("pattern"))
        mPattern = sdEntry["pattern"].asString();
    if (sdEntry.has("case_sensitive"))
        mCaseSensitive = sdEntry["case_sensitive"].asBoolean();
    if (sdEntry.has("color"))
        mColor.setValue(sdEntry["color"]);
    if (sdEntry.has("color_readonly"))
        mColorReadOnly = sdEntry["color_readonly"].asBoolean();
    if (sdEntry.has("highlight"))
        mHighlightType = (EHighlightType)sdEntry["highlight"].asInteger();
    if (sdEntry.has("sound_asset"))
        mSoundAsset = sdEntry["sound_asset"].asUUID();
    if (sdEntry.has("sound_item"))
        mSoundItem = sdEntry["sound_item"].asUUID();
    if (sdEntry.has("flash_window"))
        mFlashWindow = sdEntry["flash_window"].asBoolean();
}

LLSD LLHighlightEntry::toLLSD() const
{
    LLSD sdEntry;
    sdEntry["id"] = mId;
    sdEntry["category_mask"] = mCategoryMask;
    sdEntry["condition"] = (S32)mCondition;
    sdEntry["pattern"] = mPattern;
    sdEntry["case_sensitive"] = mCaseSensitive;
    sdEntry["color"] = mColor.getValue();
    sdEntry["color_readonly"] = mColorReadOnly;
    sdEntry["highlight"] = (S32)mHighlightType;
    if (mSoundAsset.notNull())
        sdEntry["sound_asset"] = mSoundAsset;
    if (mSoundItem.notNull())
        sdEntry["sound_item"] = mSoundItem;
    sdEntry["flash_window"] = mFlashWindow;
    return sdEntry;
}

S32 LLHighlightEntry::findPattern(const std::string& text, S32 cat_mask) const
{
    if ((mPattern.empty()) || ((mCategoryMask & cat_mask) == 0))
        return -1;

    size_t idxFound = std::string::npos;
    switch (mCondition)
    {
        case CONTAINS:
        {
            boost::iterator_range<std::string::const_iterator> itRange = (mCaseSensitive) ? boost::find_first(text, mPattern) : boost::ifind_first(text, mPattern);
            if (!itRange.empty())
                idxFound = itRange.begin() - text.begin();
            break;
        }
        case MATCHES:
        {
            if (((mCaseSensitive) && (boost::equals(text, mPattern))) || (!mCaseSensitive && boost::iequals(text, mPattern)))
                idxFound = 0;
            break;
        }
        case STARTS_WITH:
        {
            if (((mCaseSensitive) && (boost::starts_with(text, mPattern))) || (!mCaseSensitive && boost::istarts_with(text, mPattern)))
                idxFound = 0;
            break;
        }
        case ENDS_WITH:
        {
            if (((mCaseSensitive) && (boost::ends_with(text, mPattern))) || (!mCaseSensitive && boost::iends_with(text, mPattern)))
                idxFound = text.length() - mPattern.length();
            break;
        }
    }
    return (idxFound != std::string::npos) ? static_cast<S32>(idxFound) : -1;
}

//
// LLTextParser
//

LLTextParser::LLTextParser()
:   mLoaded(false)
{}

void LLTextParser::buildSets() const
{
    llassert(on_main_thread());
    mSetsDirty = false;
    // A condition is an anchor: the keyword at the text's start, at its end,
    // or both, which ^ and $ are with the text read as one line
    mCaseSet    = ALRegexSet(ALRegex::NO_MULTILINE);
    mAnyCaseSet = ALRegexSet(ALRegex::NO_MULTILINE | ALRegex::ICASE);
    mCaseEntries.clear();
    mAnyCaseEntries.clear();
    mAlwaysTried.assign(mHighlightEntries.size(), false);
    for (size_t i = 0; i < mHighlightEntries.size(); ++i)
    {
        const LLHighlightEntry& entry = mHighlightEntries[i];
        // An entry with no keyword holds nowhere
        if (entry.mPattern.empty())
            continue;

        std::string pattern = ALRegex::escape(entry.mPattern);
        if ((entry.mCondition == LLHighlightEntry::STARTS_WITH) || (entry.mCondition == LLHighlightEntry::MATCHES))
            pattern.insert(0, "^");
        if ((entry.mCondition == LLHighlightEntry::ENDS_WITH) || (entry.mCondition == LLHighlightEntry::MATCHES))
            pattern += "$";

        ALRegexSet& set = (entry.mCaseSensitive) ? mCaseSet : mAnyCaseSet;
        if (set.add(pattern) >= 0)
            ((entry.mCaseSensitive) ? mCaseEntries : mAnyCaseEntries).push_back(i);
        else
            mAlwaysTried[i] = true;
    }
    if ( ((!mCaseEntries.empty()) && (!mCaseSet.compile())) || ((!mAnyCaseEntries.empty()) && (!mAnyCaseSet.compile())) )
        LL_WARNS() << "The highlight keywords could not be made one set; each is looked for in turn" << LL_ENDL;
}

bool LLTextParser::mayHold(const std::string& text, std::vector<bool>& may) const
{
    if (mSetsDirty)
        buildSets();

    // A set with no keywords holds none; one that cannot say leaves any entry
    // able to
    std::vector<S32> case_hits, any_case_hits;
    if ( ((!mCaseEntries.empty()) && (!mCaseSet.match(text, case_hits))) ||
         ((!mAnyCaseEntries.empty()) && (!mAnyCaseSet.match(text, any_case_hits))) )
    {
        return false;
    }

    may = mAlwaysTried;
    for (const S32 hit : case_hits)
        may[mCaseEntries[hit]] = true;
    for (const S32 hit : any_case_hits)
        may[mAnyCaseEntries[hit]] = true;
    return true;
}

LLTextParser::parser_out_vec_t LLTextParser::parsePartialLineHighlights(const std::string& text, S32 cat_mask, EHighlightPosition part, S32 index)
{
    parser_out_vec_t result;

    // Only the entries the text can hold are looked for in it
    std::vector<bool> may;
    const bool        sets_say = mayHold(text, may);

    for (S32 i = index, size = static_cast<S32>(mHighlightEntries.size()); i < size; i++)
    {
        const LLHighlightEntry& entry = mHighlightEntries[i];
        if ((entry.mHighlightType != LLHighlightEntry::PART) || (entry.mCondition == LLHighlightEntry::MATCHES))
            continue;
        if ((sets_say) && (!may[i]))
            continue;

        if (!((entry.mCondition == LLHighlightEntry::STARTS_WITH && part == START) ||
              (entry.mCondition == LLHighlightEntry::ENDS_WITH   && part == END)   ||
              (entry.mCondition == LLHighlightEntry::CONTAINS)                     ||
              (part == WHOLE)))
        {
            continue;
        }

        S32 start = entry.findPattern(text, cat_mask);
        if (start < 0)
            continue;

        const S32 end = static_cast<S32>(entry.mPattern.length());
        const S32 len = static_cast<S32>(text.length());
        EHighlightPosition newpart;

        parser_out_vec_t resStart, resMiddle, resEnd;
        if (start == 0)
        {
            resStart.emplace_back(text.substr(0, end), &entry);

            if (end < len)
            {
                newpart = (part == END || part == WHOLE) ? END : MIDDLE;
                resEnd = parsePartialLineHighlights(text.substr(end), cat_mask, newpart, i);
            }
        }
        else
        {
            newpart = (part == START || part == WHOLE) ? START : MIDDLE;
            resStart = parsePartialLineHighlights(text.substr(0, start), cat_mask, newpart, i + 1);

            if (end < len)
            {
                resMiddle.emplace_back(text.substr(start, end), &entry);

                newpart = (part == END || part == WHOLE) ? END : MIDDLE;
                resEnd = parsePartialLineHighlights(text.substr(start + end), cat_mask, newpart, i);
            }
            else
            {
                resEnd.emplace_back(text.substr(start, end), &entry);
            }
        }

        result.reserve(resStart.size() + resMiddle.size() + resEnd.size());
        result.insert(result.end(), resStart.begin(), resStart.end());
        result.insert(result.end(), resMiddle.begin(), resMiddle.end());
        result.insert(result.end(), resEnd.begin(), resEnd.end());
        return result;
    }

    // No patterns matched — send back the text untouched
    result.emplace_back(text, static_cast<const LLHighlightEntry*>(nullptr));
    return result;
}

bool LLTextParser::parseFullLineHighlights(const std::string& text, S32 cat_mask, const LLHighlightEntry** ppEntry) const
{
    // Only the entries the text can hold are looked for in it
    std::vector<bool> may;
    const bool        sets_say = mayHold(text, may);

    for (size_t i = 0; i < mHighlightEntries.size(); ++i)
    {
        const LLHighlightEntry& entry = mHighlightEntries[i];
        if ((sets_say) && (!may[i]))
            continue;
        if ((entry.mHighlightType == LLHighlightEntry::ALL) || (entry.mCondition == LLHighlightEntry::MATCHES))
        {
            if (entry.findPattern(text, cat_mask) >= 0)
            {
                if (ppEntry)
                    *ppEntry = &entry;
                return true;
            }
        }
    }
    return false;
}

std::string LLTextParser::getFileName() const
{
    std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "");

    if (!path.empty())
    {
        path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "highlights.xml");
    }
    return path;
}

bool LLTextParser::loadKeywords()
{
    const std::string filename = getFileName();
    if (filename.empty())
        return false;

    llifstream fileHighlights(filename.c_str());
    if (!fileHighlights.is_open())
    {
        LL_INFOS() << "No highlights file present" << LL_ENDL;
        return false;
    }

    mHighlightEntries.clear();
    mSetsDirty = true;

    LLSD sdIn;
    if (LLSDSerialize::fromXML(sdIn, fileHighlights) == LLSDParser::PARSE_FAILURE)
    {
        LL_WARNS() << "Failed to parse highlights file" << LL_ENDL;
        return false;
    }

    if (sdIn.isArray())
    {
        for (LLSD::array_const_iterator it = sdIn.beginArray(); it != sdIn.endArray(); ++it)
        {
            mHighlightEntries.emplace_back(*it);
        }
    }

    mSetsDirty = true;
    mLoaded = true;
    return true;
}

void LLTextParser::saveToDisk() const
{
    const std::string filename = getFileName();
    if (filename.empty())
    {
        LL_WARNS() << "LLTextParser::saveToDisk() no valid user directory." << LL_ENDL;
        return;
    }

    llofstream fileHighlights(filename.c_str());
    if (!fileHighlights.is_open())
    {
        LL_WARNS() << "Can't open highlights file for writing" << LL_ENDL;
        return;
    }

    LLSD out = LLSD::emptyArray();
    for (const auto& entry : mHighlightEntries)
    {
        out.append(entry.toLLSD());
    }
    LLSDSerialize::toPrettyXML(out, fileHighlights);
}

void LLTextParser::addHighlight(const LLHighlightEntry& entry)
{
    if (getHighlightById(entry.getId()) != nullptr)
        return;
    mHighlightEntries.push_back(entry);
    mSetsDirty = true;
}

LLHighlightEntry* LLTextParser::getHighlightById(const LLUUID& idEntry)
{
    // What is handed out may be changed where it is
    mSetsDirty = true;
    auto it = std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(),
        [&idEntry](const LLHighlightEntry& e) { return e.getId() == idEntry; });
    return (it != mHighlightEntries.end()) ? &(*it) : nullptr;
}

const LLHighlightEntry* LLTextParser::getHighlightById(const LLUUID& idEntry) const
{
    auto it = std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(),
        [&idEntry](const LLHighlightEntry& e) { return e.getId() == idEntry; });
    return (it != mHighlightEntries.end()) ? &(*it) : nullptr;
}

const LLTextParser::highlight_list_t& LLTextParser::getHighlights() const
{
    return mHighlightEntries;
}

void LLTextParser::removeHighlight(const LLUUID& idEntry)
{
    auto it = std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(),
        [&idEntry](const LLHighlightEntry& e) { return e.getId() == idEntry; });
    if (it != mHighlightEntries.end())
    {
        mHighlightEntries.erase(it);
        mSetsDirty = true;
    }
}

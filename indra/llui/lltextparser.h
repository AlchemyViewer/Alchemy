/**
 * @file llTextParser.h
 * @brief GUI for user-defined highlights
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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
 *
 */

#ifndef LL_LLTEXTPARSER_H
#define LL_LLTEXTPARSER_H

#include "llsd.h"
#include "llsingleton.h"
#include "lluicolor.h"
#include "v4color.h"
#include "lluuid.h"

#include <vector>

class LLVector3d;

class LLHighlightEntry
{
public:
    LLHighlightEntry();
    LLHighlightEntry(const LLSD& sdEntry);

    S32           findPattern(const std::string& text, S32 cat_mask) const;
    const LLUUID& getId() const { return mId; }
    LLSD          toLLSD() const;

public:
    enum ECategory      { CAT_NONE = 0x00, CAT_GENERAL = 0x01, CAT_NEARBYCHAT = 0x02, CAT_IM = 0x04, CAT_GROUP = 0x08, CAT_ALL = 0xFF };
    enum EConditionType { CONTAINS, MATCHES, STARTS_WITH, ENDS_WITH };
    enum EHighlightType { PART, ALL };
    S32            mCategoryMask;
    EConditionType mCondition;
    std::string    mPattern;
    bool           mCaseSensitive;
    LLColor4       mColor;
    bool           mColorReadOnly;     // If true, the highlight color is also applied as the read-only text color
    EHighlightType mHighlightType;
    // Other actions
    LLUUID         mSoundAsset;        // Asset UUID of the sound
    LLUUID         mSoundItem;         // Item UUID of the sound
    bool           mFlashWindow;
protected:
    LLUUID         mId;
};

class LLTextParser final : public LLSingleton<LLTextParser>
{
    LLSINGLETON(LLTextParser);
public:
    typedef std::vector<LLHighlightEntry> highlight_list_t;
    void                    addHighlight(const LLHighlightEntry& entry);
    LLHighlightEntry*       getHighlightById(const LLUUID& idEntry);
    const LLHighlightEntry* getHighlightById(const LLUUID& idEntry) const;
    S32                     getHighlightCount() const { return static_cast<S32>(mHighlightEntries.size()); }
    const highlight_list_t& getHighlights() const;
    void                    removeHighlight(const LLUUID& idEntry);

    // Each entry pairs a text segment with the matched highlight entry, or nullptr for unmatched runs.
    using parser_out_vec_t = std::vector<std::pair<std::string, const LLHighlightEntry*>>;
    typedef enum e_highlight_position { WHOLE, START, MIDDLE, END } EHighlightPosition;
    parser_out_vec_t parsePartialLineHighlights(const std::string& text, S32 cat_mask, EHighlightPosition part = WHOLE, S32 index = 0);
    bool parseFullLineHighlights(const std::string& text, S32 cat_mask, const LLHighlightEntry** ppEntry = nullptr) const;

    bool loadKeywords();
    void saveToDisk() const;

protected:
    std::string getFileName() const;

protected:
    bool             mLoaded;
    highlight_list_t mHighlightEntries;
};

#endif

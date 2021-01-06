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
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
#include "v4color.h"

#include <list>
// [/SL:KB]

class LLUUID;
class LLVector3d;
//class LLColor4;

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
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
	bool           mColorReadOnly;     // If TRUE, the highlight color will also be set as the read-only text color
	EHighlightType mHighlightType;
	// Other actions
	LLUUID         mSoundAsset;        // Asset UUID of the sound
	LLUUID         mSoundItem;         // Item UUID of the sound
	bool           mFlashWindow;
protected:
	LLUUID         mId;
};
// [/SL:KB]

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
class LLTextParser : public LLSingleton<LLTextParser>
{
	LLSINGLETON(LLTextParser);
public:
	typedef std::vector<LLHighlightEntry> highlight_list_t;
	void                    addHighlight(const LLHighlightEntry& entry);
	LLHighlightEntry*       getHighlightById(const LLUUID& idEntry);
	const LLHighlightEntry* getHighlightById(const LLUUID& idEntry) const;
	S32                     getHighlightCount() const { return mHighlightEntries.size(); }
	const highlight_list_t& getHighlights() const;
	void                    removeHighlight(const LLUUID& idEntry);

	typedef std::pair<std::string, const LLHighlightEntry*> partial_result_t;
	typedef std::list<partial_result_t> partial_results_t;
	typedef enum e_highlight_position { WHOLE, START, MIDDLE, END } EHighlightPosition;
	partial_results_t parsePartialLineHighlights(const std::string &text, S32 cat_mask, EHighlightPosition part, S32 index = 0);
	bool parseFullLineHighlights(const std::string& text, S32 cat_mask, const LLHighlightEntry** ppEntry = NULL) const;

	bool loadKeywords();
	void saveToDisk() const;
protected:
	std::string getFileName() const;

protected:
	bool	mLoaded;
	highlight_list_t mHighlightEntries;
};
// [/SL:KB]
//class LLTextParser : public LLSingleton<LLTextParser>
//{
//	LLSINGLETON(LLTextParser);
//
//public:
//	typedef enum e_condition_type { CONTAINS, MATCHES, STARTS_WITH, ENDS_WITH } EConditionType;
//	typedef enum e_highlight_type { PART, ALL } EHighlightType;
//	typedef enum e_highlight_position { WHOLE, START, MIDDLE, END } EHighlightPosition;
//	typedef enum e_dialog_action { ACTION_NONE, ACTION_CLOSE, ACTION_ADD, ACTION_COPY, ACTION_UPDATE } EDialogAction;
//
//	LLSD parsePartialLineHighlights(const std::string &text,const LLColor4 &color, EHighlightPosition part=WHOLE, S32 index=0);
//	bool parseFullLineHighlights(const std::string &text, LLColor4 *color);
//
//private:
//	S32  findPattern(const std::string &text, LLSD highlight);
//	std::string getFileName();
//	void loadKeywords();
//	bool saveToDisk(LLSD highlights);
//
//public:
//	LLSD	mHighlights;
//	bool	mLoaded;
//};

#endif

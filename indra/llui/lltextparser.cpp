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

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
// [/SL:KB]

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
LLHighlightEntry::LLHighlightEntry()
	: mId(LLUUID::generateNewID())
	, mCategoryMask(CAT_NONE)
	, mCondition(CONTAINS)
	, mCaseSensitive(false)
	, mColor(LLColor4::white)
	, mColorReadOnly(true)
	, mHighlightType(PART)
	, mFlashWindow(false)
{
}

LLHighlightEntry::LLHighlightEntry(const LLSD& sdEntry)
	: mId(LLUUID::generateNewID())
	, mCategoryMask(CAT_NONE)
	, mCondition(CONTAINS)
	, mCaseSensitive(false)
	, mColor(LLColor4::white)
	, mColorReadOnly(true)
	, mHighlightType(PART)
	, mFlashWindow(false)
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
// [/SL:KB]

//
// Member Functions
//

LLTextParser::LLTextParser()
:	mLoaded(false)
{}

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
S32 LLHighlightEntry::findPattern(const std::string& text, S32 cat_mask) const
{
	if ( (mPattern.empty()) || ((mCategoryMask & cat_mask) == 0) )
		return -1;
	
	size_t idxFound = std::string::npos;
	switch (mCondition)
	{
		case CONTAINS:
			{
				boost::iterator_range<std::string::const_iterator> itRange = (mCaseSensitive) ? boost::find_first(text, mPattern) : boost::ifind_first(text, mPattern);
				if (!itRange.empty())
					idxFound = itRange.begin() - text.begin();
			}
			break;
		case MATCHES:
			{
				if ( ((mCaseSensitive) && (boost::equals(text, mPattern))) || (boost::iequals(text, mPattern)) )
					idxFound = 0;
			}
			break;
		case STARTS_WITH:
			{
				if ( ((mCaseSensitive) && (boost::starts_with(text, mPattern))) || (boost::istarts_with(text, mPattern)) )
					idxFound = 0;
			}
			break;
		case ENDS_WITH:
			{
				if ( ((mCaseSensitive) && (boost::ends_with(text, mPattern))) || (boost::iends_with(text, mPattern)) )
					idxFound = text.length() - mPattern.length();
			}
			break;
	}
	return idxFound;
}
// [/SL:KB]
//S32 LLTextParser::findPattern(const std::string &text, LLSD highlight)
//{
//	if (!highlight.has("pattern")) return -1;
//	
//	std::string pattern=std::string(highlight["pattern"]);
//	std::string ltext=text;
//	
//	if (!(bool)highlight["case_sensitive"])
//	{
//		ltext   = utf8str_tolower(text);
//		pattern= utf8str_tolower(pattern);
//	}
//
//	size_t found=std::string::npos;
//	
//	switch ((S32)highlight["condition"])
//	{
//		case CONTAINS:
//			found = ltext.find(pattern); 
//			break;
//		case MATCHES:
//		    found = (! ltext.compare(pattern) ? 0 : std::string::npos);
//			break;
//		case STARTS_WITH:
//			found = (! ltext.find(pattern) ? 0 : std::string::npos);
//			break;
//		case ENDS_WITH:
//			S32 pos = ltext.rfind(pattern); 
//			if (pos >= 0 && (ltext.length()-pattern.length()==pos)) found = pos;
//			break;
//	}
//	return found;
//}

//LLSD LLTextParser::parsePartialLineHighlights(const std::string &text, const LLColor4 &color, EHighlightPosition part, S32 index)
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
LLTextParser::partial_results_t LLTextParser::parsePartialLineHighlights(const std::string &text, S32 cat_mask, EHighlightPosition part, S32 index)
// [/SL:KB]
{
//	loadKeywords();
//
//	//evil recursive string atomizer.
//	LLSD ret_llsd, start_llsd, middle_llsd, end_llsd;
//
//	for (S32 i=index;i<mHighlights.size();i++)
//	{
//		S32 condition = mHighlights[i]["condition"];
//		if ((S32)mHighlights[i]["highlight"]==PART && condition!=MATCHES)
//		{
//			if ( (condition==STARTS_WITH && part==START) ||
//			     (condition==ENDS_WITH   && part==END)   ||
//				  condition==CONTAINS    || part==WHOLE )
//			{
//				S32 start = findPattern(text,mHighlights[i]);
//				if (start >= 0 )
//				{
//					S32 end =  std::string(mHighlights[i]["pattern"]).length();
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
	partial_results_t resStart, resMiddle, resEnd;

	for (S32 i = index; i < mHighlightEntries.size(); i++)
	{
		const LLHighlightEntry& entry = mHighlightEntries[i];
		if ( (entry.mHighlightType == LLHighlightEntry::PART) && (entry.mCondition != LLHighlightEntry::MATCHES) )
		{
			if ( (entry.mCondition == LLHighlightEntry::STARTS_WITH && part == START) || 
			     (entry.mCondition == LLHighlightEntry::ENDS_WITH   && part == END)   ||
				  entry.mCondition == LLHighlightEntry::CONTAINS    || part == WHOLE )
			{
				S32 start = entry.findPattern(text, cat_mask);
				if (start >= 0 )
				{
					S32 end = entry.mPattern.length();
// [/SL:KB]
					S32 len = text.length();
					EHighlightPosition newpart;
					if (start==0)
					{
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
						resStart.push_back(partial_result_t(text.substr(0, end), &entry));
// [/SL:KB]
//						start_llsd[0]["text"] =text.substr(0,end);
//						start_llsd[0]["color"]=mHighlights[i]["color"];
						
						if (end < len)
						{
							if (part==END   || part==WHOLE) newpart=END; else newpart=MIDDLE;
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
							resEnd = parsePartialLineHighlights(text.substr(end), cat_mask, newpart, i);
// [/SL:KB]
//							end_llsd=parsePartialLineHighlights(text.substr( end ),color,newpart,i);
						}
					}
					else
					{
						if (part==START || part==WHOLE) newpart=START; else newpart=MIDDLE;

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
						resStart = parsePartialLineHighlights(text.substr(0,start), cat_mask, newpart, i+1);
// [/SL:KB]
//						start_llsd=parsePartialLineHighlights(text.substr(0,start),color,newpart,i+1);
						
						if (end < len)
						{
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
							resMiddle.push_back(partial_result_t(text.substr(start,end), &entry));
// [/SL:KB]
//							middle_llsd[0]["text"] =;
//							middle_llsd[0]["color"]=mHighlights[i]["color"];
						
							if (part==END   || part==WHOLE) newpart=END; else newpart=MIDDLE;

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
							resEnd = parsePartialLineHighlights(text.substr(start + end), cat_mask, newpart, i);
// [/SL:KB]
//							end_llsd=parsePartialLineHighlights(text.substr( (start+end) ),color,newpart,i);
						}
						else
						{
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
							resEnd.push_back(partial_result_t(text.substr(start,end), &entry));
// [/SL:KB]
//							end_llsd[0]["text"] =text.substr(start,end);
//							end_llsd[0]["color"]=mHighlights[i]["color"];
						}
					}
						
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
					partial_results_t resFinal(resStart.size() + resMiddle.size() + resEnd.size());
					resFinal.splice(resFinal.end(), resStart);
					resFinal.splice(resFinal.end(), resMiddle);
					resFinal.splice(resFinal.end(), resEnd);
					return resFinal;
// [/SL:KB]
//					S32 retcount=0;
//					
//					//FIXME These loops should be wrapped into a subroutine.
//					for (LLSD::array_iterator iter = start_llsd.beginArray();
//						 iter != start_llsd.endArray();++iter)
//					{
//						LLSD highlight = *iter;
//						ret_llsd[retcount++]=highlight;
//					}
//						   
//					for (LLSD::array_iterator iter = middle_llsd.beginArray();
//						 iter != middle_llsd.endArray();++iter)
//					{
//						LLSD highlight = *iter;
//						ret_llsd[retcount++]=highlight;
//					}
//						   
//					for (LLSD::array_iterator iter = end_llsd.beginArray();
//						 iter != end_llsd.endArray();++iter)
//					{
//						LLSD highlight = *iter;
//						ret_llsd[retcount++]=highlight;
//					}
//						   
//					return ret_llsd;
				}
			}
		}
	}
	
	//No patterns found.  Just send back what was passed in.
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
	partial_results_t resFinal;
	resFinal.push_back(partial_result_t(text, (const LLHighlightEntry*)NULL));
	return resFinal;
// [/SL:KB]
//	ret_llsd[0]["text"] =text;
//	LLSD color_sd = color.getValue();
//	ret_llsd[0]["color"]=color_sd;
//	return ret_llsd;
}

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
bool LLTextParser::parseFullLineHighlights(const std::string& text, S32 cat_mask, const LLHighlightEntry** ppEntry) const
{
	for (const LLHighlightEntry& entry : mHighlightEntries)
	{
		if ( (entry.mHighlightType == LLHighlightEntry::ALL) || (entry.mCondition == LLHighlightEntry::MATCHES) )
		{
			if (std::string::npos != entry.findPattern(text, cat_mask))
			{
				if (ppEntry)
					*ppEntry = &entry;
				return TRUE;
			}
		}
	}
	return FALSE;	//No matches found.
}
// [/SL:KB]
//bool LLTextParser::parseFullLineHighlights(const std::string &text, LLColor4 *color)
//{
//	loadKeywords();
//
//	for (S32 i=0;i<mHighlights.size();i++)
//	{
//		if ((S32)mHighlights[i]["highlight"]==ALL || (S32)mHighlights[i]["condition"]==MATCHES)
//		{
//			if (findPattern(text,mHighlights[i]) >= 0 )
//			{
//				LLSD color_llsd = mHighlights[i]["color"];
//				color->setValue(color_llsd);
//				return TRUE;
//			}
//		}
//	}
//	return FALSE;	//No matches found.
//}

//std::string LLTextParser::getFileName()
// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
std::string LLTextParser::getFileName() const
// [/SL:KB]
{
	std::string path=gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "");
	
	if (!path.empty())
	{
		path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "highlights.xml");
	}
	return path;  
}

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
bool LLTextParser::loadKeywords()
{
	llifstream fileHighlights(getFileName());
	if (!fileHighlights.is_open())
	{
		LL_WARNS() << "Can't open highlights file for reading" << LL_ENDL;
		return false;
	}

	mHighlightEntries.clear();

	LLSD sdIn;
	if (LLSDSerialize::fromXML(sdIn, fileHighlights) == LLSDParser::PARSE_FAILURE)
	{
		LL_WARNS() << "Failed to parse highlights file" << LL_ENDL;
		return false;
	}

	for (const LLSD& sdEntry : sdIn.array())
	{
		mHighlightEntries.push_back(LLHighlightEntry(sdEntry));
	}

	return true;
}
// [/SL:KB]
//void LLTextParser::loadKeywords()
//{
//	if (mLoaded)
//	{// keywords already loaded
//		return;
//	}
//	std::string filename=getFileName();
//	if (!filename.empty())
//	{
//		llifstream file;
//		file.open(filename.c_str());
//		if (file.is_open())
//		{
//			LLSDSerialize::fromXML(mHighlights, file);
//		}
//		file.close();
//		mLoaded = true;
//	}
//}

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-10 (Catznip-3.3)
void LLTextParser::saveToDisk() const
{
	llofstream fileHighlights(getFileName());
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
// [/SL:KB]
//bool LLTextParser::saveToDisk(LLSD highlights)
//{
//	mHighlights=highlights;
//	std::string filename=getFileName();
//	if (filename.empty())
//	{
//		LL_WARNS() << "LLTextParser::saveToDisk() no valid user directory." << LL_ENDL; 
//		return FALSE;
//	}	
//	llofstream file;
//	file.open(filename.c_str());
//	LLSDSerialize::toPrettyXML(mHighlights, file);
//	file.close();
//	return TRUE;
//}

// [SL:KB] - Patch: Control-TextParser | Checked: 2012-07-17 (Catznip-3.3)
void LLTextParser::addHighlight(const LLHighlightEntry& entry)
{
	// Make sure we don't already have it added
	if (NULL != getHighlightById(entry.getId()))
		return;
	mHighlightEntries.push_back(entry);
}

LLHighlightEntry* LLTextParser::getHighlightById(const LLUUID& idEntry)
{
	highlight_list_t::iterator itEntry =
		std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(), boost::bind(&LLHighlightEntry::getId, _1) == idEntry);
	return (mHighlightEntries.end() != itEntry) ? &(*itEntry) : NULL;
}

const LLHighlightEntry* LLTextParser::getHighlightById(const LLUUID& idEntry) const
{
	highlight_list_t::const_iterator itEntry =
		std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(), boost::bind(&LLHighlightEntry::getId, _1) == idEntry);
	return (mHighlightEntries.end() != itEntry) ? &(*itEntry) : NULL;
}

const LLTextParser::highlight_list_t& LLTextParser::getHighlights() const
{
	return mHighlightEntries;
}

void LLTextParser::removeHighlight(const LLUUID& idEntry)
{
	highlight_list_t::iterator itEntry =
		std::find_if(mHighlightEntries.begin(), mHighlightEntries.end(), boost::bind(&LLHighlightEntry::getId, _1) == idEntry);
	if (mHighlightEntries.end() != itEntry)
		mHighlightEntries.erase(itEntry);
}
// [/SL:KB]

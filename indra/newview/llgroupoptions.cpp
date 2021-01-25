/**
 *
 * Copyright (c) 2012-2014, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */

#include "llviewerprecompiledheaders.h"

#include "llagent.h"
#include "llgroupoptions.h"
#include "llsdserialize.h"

// ============================================================================
// LLGroupOptions
//

LLGroupOptions::LLGroupOptions(const LLUUID& idGroup)
	: mGroupId(idGroup)
	, mReceiveGroupChat(true)
{
}

LLGroupOptions::LLGroupOptions(const LLSD& sdData)
{
	mGroupId = (sdData.has("group_id")) ? sdData["group_id"].asUUID() : LLUUID::null;
	mReceiveGroupChat = (sdData.has("receive_chat")) ? sdData["receive_chat"].asBoolean() : true;
	mSnoozeOnClose = (sdData.has("snooze_chat")) ? sdData["snooze_chat"].asBoolean() : false;
	mSnoozeDuration = (sdData.has("snooze_chat_duration")) ? sdData["snooze_chat_duration"].asInteger() : -1;
}

bool LLGroupOptions::isValid() const
{
	return (mGroupId.notNull()) && (gAgent.isInGroup(mGroupId, true));
}

LLSD LLGroupOptions::toLLSD() const
{
	LLSD sdData;
	sdData["group_id"] = mGroupId;
	sdData["receive_chat"] = mReceiveGroupChat;
	sdData["snooze_chat"] = mSnoozeOnClose;
	sdData["snooze_chat_duration"] = mSnoozeDuration;
	return sdData;
}

// ============================================================================
// LLGroupOptionsMgr
//

static const char* GROUP_OPTIONS_FILENAME = "group_options.xml";

LLGroupOptionsMgr::LLGroupOptionsMgr()
{
	// Try to load the new format first, fallback to legacy otherwise
	load();
}

LLGroupOptionsMgr::~LLGroupOptionsMgr()
{
}

void LLGroupOptionsMgr::clearOptions(const LLUUID& idGroup)
{
	options_map_t::iterator itOption = mGroupOptions.find(idGroup);
	if (mGroupOptions.end() != itOption)
	{
		mGroupOptions.erase(itOption);
		save();
	}
}

void LLGroupOptionsMgr::setOptionReceiveChat(const LLUUID& idGroup, bool fReceiveChat)
{
	if (LLGroupOptions* pOptions = getOptions(idGroup))
	{
		pOptions->mReceiveGroupChat = fReceiveChat;
		save();
	}
}

void LLGroupOptionsMgr::setOptionSnoozeOnClose(const LLUUID& idGroup, bool fSnoozeOnClose)
{
	if (LLGroupOptions* pOptions = getOptions(idGroup))
	{
		pOptions->mSnoozeOnClose = fSnoozeOnClose;
		save();
	}
}

void LLGroupOptionsMgr::setOptionSnoozeDuration(const LLUUID& idGroup, int nSnoozeDuration)
{
	if (LLGroupOptions* pOptions = getOptions(idGroup))
	{
		pOptions->mSnoozeDuration = nSnoozeDuration;
		save();
	}
}

LLGroupOptions* LLGroupOptionsMgr::getOptions(const LLUUID& idGroup)
{
	options_map_t::iterator itOption = mGroupOptions.find(idGroup);
	if (mGroupOptions.end() != itOption)
		return itOption->second.get();

	if (gAgent.isInGroup(idGroup))
	{
		auto ret = mGroupOptions.emplace(idGroup, std::make_unique<LLGroupOptions>(idGroup));
		return ret.first->second.get();
	}
	return nullptr;
}

bool LLGroupOptionsMgr::load()
{
	const std::string strFile = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, GROUP_OPTIONS_FILENAME);
	if (!gDirUtilp->fileExists(strFile))
	{
		return false;
	}

	llifstream fileGroupOptions(strFile);
	if (!fileGroupOptions.is_open())
	{
		LL_WARNS() << "Can't open group options file" << LL_ENDL;
		return false;
	}

	LLSD sdGroupOptions;
	LLSDSerialize::fromXMLDocument(sdGroupOptions, fileGroupOptions);
	fileGroupOptions.close();

	mGroupOptions.clear();

	for (LLSD::array_const_iterator itOption = sdGroupOptions.beginArray(), endOption = sdGroupOptions.endArray(); itOption != endOption; ++itOption)
	{
		std::unique_ptr<LLGroupOptions> pOptions = std::make_unique<LLGroupOptions>(*itOption);
		if (!pOptions->isValid())
		{
			continue;
		}
		mGroupOptions.emplace(pOptions->mGroupId, std::move(pOptions));
	}

	return true;
}

//bool LLGroupOptionsMgr::loadLegacy()
//{
//	const std::string strFile = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, GROUP_OPTIONS_FILENAME_LEGACY);
//	if (!gDirUtilp->fileExists(strFile))
//	{
//		return false;
//	}
//
//	llifstream fileGroupOptions(strFile);
//	if (!fileGroupOptions.is_open())
//	{
//		LL_WARNS() << "Can't open group options file" << LL_ENDL;
//		return false;
//	}
//
//	for_each(mGroupOptions.begin(), mGroupOptions.end(), DeletePairedPointer());
//	mGroupOptions.clear();
//
//	LLPointer<LLSDNotationParser> sdParser = new LLSDNotationParser(); std::string strLine;
//	while (std::getline(fileGroupOptions, strLine))
//	{
//		std::istringstream iss(strLine); LLSD sdData;
//		if (LLSDParser::PARSE_FAILURE == sdParser->parse(iss, sdData, strLine.length()))
//		{
//			LL_INFOS() << "Failed to parse group option entry" << LL_ENDL;
//			continue;
//		}
//
//		LLGroupOptions* pOptions = new LLGroupOptions(sdData);
//		if (!pOptions->isValid())
//		{
//			delete pOptions;
//			continue;
//		}
//		mGroupOptions.insert(std::pair<LLUUID, LLGroupOptions*>(pOptions->mGroupId, pOptions));
//	}
//
//	fileGroupOptions.close();
//
//	// Save in the new format and delete the legacy file
//	if (save())
//	{
//		LLFile::remove(strFile);
//	}
//
//	return true;
//}

bool LLGroupOptionsMgr::save()
{
	llofstream fileGroupOptions(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, GROUP_OPTIONS_FILENAME));
	if (!fileGroupOptions.is_open())
	{
		LL_WARNS() << "Can't open group options file" << LL_ENDL;
		return false;
	}

	LLSD sdGroupOptions;
	for (options_map_t::const_iterator itOption = mGroupOptions.begin(); itOption != mGroupOptions.end(); ++itOption)
	{
		sdGroupOptions.append(itOption->second->toLLSD());
	}
	LLSDSerialize::toPrettyXML(sdGroupOptions, fileGroupOptions);

	fileGroupOptions.close();
	return true;
}

// ============================================================================

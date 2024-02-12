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

#ifndef LL_LLGROUPOPTIONS_H
#define LL_LLGROUPOPTIONS_H

#include "boost/unordered/unordered_flat_map.hpp"

#include "llsd.h"
#include "llsingleton.h"
#include "lluuid.h"

// ============================================================================
// LLGroupOptions
//
struct LLGroupOptions
{
	LLGroupOptions(const LLUUID& idGroup);
	LLGroupOptions(const LLSD& sdData);
	bool isValid() const;
	LLSD toLLSD() const;

	LLUUID mGroupId;
	bool   mReceiveGroupChat = true;
	bool   mSnoozeOnClose = false;
	int    mSnoozeDuration = -1;
};

// ============================================================================
// LLGroupOptionsMgr
//
class LLGroupOptionsMgr final : public LLSingleton<LLGroupOptionsMgr>
{
	/*
	 * Constructor
	 */
	LLSINGLETON(LLGroupOptionsMgr);
protected:
	~LLGroupOptionsMgr() override = default;

	/*
	 * Member functions
	 */
public:
	void            clearOptions(const LLUUID& idGroup);
	LLGroupOptions* getOptions(const LLUUID& idGroup);
	void            setOptionReceiveChat(const LLUUID& idGroup, bool fReceiveChat);
	void            setOptionSnoozeOnClose(const LLUUID& idGroup, bool fSnoozeOnClose);
	void            setOptionSnoozeDuration(const LLUUID& idGroup, int nSnoozeDuration);
private:
	bool            load();
	bool            save();

	/*
	 * Member variables
	 */
	typedef boost::unordered_flat_map<LLUUID, std::unique_ptr<LLGroupOptions>> options_map_t;
	options_map_t mGroupOptions;
};

// ============================================================================

#endif // LL_LLGROUPOPTIONS_H

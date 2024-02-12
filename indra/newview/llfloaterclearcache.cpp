/**
 * @file llfloaterclearcache.cpp
 * @brief Floater to save a camera preset
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2023, Rye Mutt <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterclearcache.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterpreference.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "llpresetsmanager.h"
#include "llradiogroup.h"
#include "lltrans.h"
#include "llvoavatarself.h"

LLFloaterClearCache::LLFloaterClearCache(const LLSD& key)
	: LLModalDialog(key)
{
}

// virtual
BOOL LLFloaterClearCache::postBuild()
{
	mOkButton = getChild<LLButton>("clear_selected");
	mOkButton->setCommitCallback(boost::bind(&LLFloaterClearCache::onBtnClearSelected, this));

	getChild<LLCheckBoxCtrl>("clear_textures")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));
	getChild<LLCheckBoxCtrl>("clear_assets")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));
	getChild<LLCheckBoxCtrl>("clear_inventory")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));
	getChild<LLCheckBoxCtrl>("clear_regions")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));
	getChild<LLCheckBoxCtrl>("clear_web")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));
	getChild<LLCheckBoxCtrl>("clear_userdata")->setCommitCallback(boost::bind(&LLFloaterClearCache::refreshButtons, this));

	getChild<LLButton>("clear_all")->setCommitCallback(boost::bind(&LLFloaterClearCache::onBtnClearAll, this));
	getChild<LLButton>("cancel")->setCommitCallback(boost::bind(&LLFloaterClearCache::onBtnCancel, this));

	refreshButtons();

	return LLModalDialog::postBuild();
}


void LLFloaterClearCache::onOpen(const LLSD& key)
{
	LLModalDialog::onOpen(key);
}

void LLFloaterClearCache::onBtnClearSelected()
{
	LLSD caches;
	if (getChild<LLCheckBoxCtrl>("clear_textures")->get())
	{
		caches.insert("textures", "true");
	}
	if (getChild<LLCheckBoxCtrl>("clear_assets")->get())
	{
		caches.insert("assets", "true");
	}
	if (getChild<LLCheckBoxCtrl>("clear_inventory")->get())
	{
		caches.insert("inventory", "true");
	}
	if (getChild<LLCheckBoxCtrl>("clear_regions")->get())
	{
		caches.insert("regions", "true");
	}
	if (getChild<LLCheckBoxCtrl>("clear_web")->get())
	{
		caches.insert("web", "true");
	}
	if (getChild<LLCheckBoxCtrl>("clear_userdata")->get())
	{
		caches.insert("userdata", "true");
	}

	gSavedSettings.setLLSD("PurgeCacheSelectiveData", caches);
	gSavedSettings.setBOOL("PurgeCacheSelective", true);

	LLNotificationsUtil::add("CacheWillClear");
	closeFloater();
}

void LLFloaterClearCache::onBtnClearAll()
{
	gSavedSettings.setBOOL("PurgeCacheOnNextStartup", TRUE);
	LLNotificationsUtil::add("CacheWillClear");
	closeFloater();
}

void LLFloaterClearCache::onBtnCancel()
{
	closeFloater();
}

void LLFloaterClearCache::refreshButtons()
{
	mOkButton->setEnabled(getChild<LLCheckBoxCtrl>("clear_textures")->get() ||
		getChild<LLCheckBoxCtrl>("clear_assets")->get() ||
		getChild<LLCheckBoxCtrl>("clear_inventory")->get() ||
		getChild<LLCheckBoxCtrl>("clear_regions")->get() ||
		getChild<LLCheckBoxCtrl>("clear_web")->get() ||
		getChild<LLCheckBoxCtrl>("clear_userdata")->get()
	);
}
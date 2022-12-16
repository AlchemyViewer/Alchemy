/**
 * @file lllegacynotificationwellwindow.cpp
 * @brief Notification well intended for managing notifications
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"
#include "lllegacynotificationwellwindow.h"

#include "llchiclet.h"
#include "llflatlistview.h"
#include "llfloaterreg.h"
#include "llnotificationhandler.h"
#include "lltoastpanel.h"

LLNotificationWellWindow::WellNotificationChannel::WellNotificationChannel(LLNotificationWellWindow* well_window)
:	LLNotificationChannel(LLNotificationChannel::Params().name(well_window->getPathname()))
,	mWellWindow(well_window)
{
	connectToChannel("Notifications");
	connectToChannel("Group Notifications");
	connectToChannel("Offer");
}

LLNotificationWellWindow::LLNotificationWellWindow(const LLSD& key)
:	LLSysWellWindow(key)
{
	mNotificationUpdates.reset(new WellNotificationChannel(this));
}

// static
LLNotificationWellWindow* LLNotificationWellWindow::getInstance(const LLSD& key /*= LLSD()*/)
{
	return LLFloaterReg::getTypedInstance<LLNotificationWellWindow>("legacy_notification_well_window", key);
}

// virtual
BOOL LLNotificationWellWindow::postBuild()
{
	BOOL rv = LLSysWellWindow::postBuild();
	setTitle(getString("title_notification_well_window"));
	return rv;
}

// virtual
void LLNotificationWellWindow::setVisible(BOOL visible)
{
	if (visible)
	{
		// when Notification channel is cleared, storable toasts will be added into the list.
		clearScreenChannels();
	}
	
	LLSysWellWindow::setVisible(visible);
}

void LLNotificationWellWindow::addItem(const LLSysWellItem::Params& p)
{
	LLSD value = p.notification_id;
	// do not add clones
	if( mMessageList->getItemByValue(value))
		return;
	
	LLSysWellItem* new_item = new LLSysWellItem(p);
	if (mMessageList->addItem(new_item, value, ADD_TOP))
	{
		mSysWellChiclet->updateWidget(isWindowEmpty());
		reshapeWindow();
		new_item->setOnItemCloseCallback(boost::bind(&LLNotificationWellWindow::onItemClose, this, _1));
		new_item->setOnItemClickCallback(boost::bind(&LLNotificationWellWindow::onItemClick, this, _1));
	}
	else
	{
		LL_WARNS() << "Unable to add Notification into the list, notification ID: " << p.notification_id
		<< ", title: " << p.title
		<< LL_ENDL;
		
		new_item->die();
	}
}

void LLNotificationWellWindow::closeAll()
{
	// Need to clear notification channel, to add storable toasts into the list.
	clearScreenChannels();
	std::vector<LLPanel*> items;
	mMessageList->getItems(items);
	for (std::vector<LLPanel*>::iterator
		 iter = items.begin(),
		 iter_end = items.end();
		 iter != iter_end; ++iter)
	{
		LLSysWellItem* sys_well_item = dynamic_cast<LLSysWellItem*>(*iter);
		if (sys_well_item)
			onItemClose(sys_well_item);
	}
}

void LLNotificationWellWindow::initChannel()
{
	LLSysWellWindow::initChannel();
	if(mChannel)
	{
		mChannel->addOnStoreToastCallback(boost::bind(&LLNotificationWellWindow::onStoreToast, this, _1, _2));
	}
}

void LLNotificationWellWindow::clearScreenChannels()
{
	// 1 - remove StartUp toast and channel if present
	if(!LLNotificationsUI::LLScreenChannel::getStartUpToastShown())
	{
		LLNotificationsUI::LLChannelManager::getInstance()->onStartUpToastClose();
	}
	
	// 2 - remove toasts in Notification channel
	if(mChannel)
	{
		mChannel->removeAndStoreAllStorableToasts();
	}
}

void LLNotificationWellWindow::onStoreToast(LLPanel* info_panel, LLUUID id)
{
	LLSysWellItem::Params p;
	p.notification_id = id;
	p.title = static_cast<LLToastPanel*>(info_panel)->getTitle();
	addItem(p);
}

void LLNotificationWellWindow::onItemClick(LLSysWellItem* item)
{
	LLUUID id = item->getID();
	LLFloaterReg::showInstance("inspect_toast", id);
}

void LLNotificationWellWindow::onItemClose(LLSysWellItem* item)
{
	LLUUID id = item->getID();
	
	if(mChannel)
	{
		// removeItemByID() is invoked from killToastByNotificationID() and item will removed;
		mChannel->killToastByNotificationID(id);
	}
	else
	{
		// removeItemByID() should be called one time for each item to remove it from notification well
		removeItemByID(id);
	}
	
}

void LLNotificationWellWindow::onAdd( LLNotificationPtr notify )
{
	removeItemByID(notify->getID());
}

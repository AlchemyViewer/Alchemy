/**
 * @file lllegacynotificationwellwindow.h
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

#ifndef LL_NOTIFICATIONWELLWINDOW_H
#define LL_NOTIFICATIONWELLWINDOW_H

#include "llnotificationptr.h"
#include "llnotifications.h"
#include "llsyswellwindow.h"

class LLPanel;

class LLLegacyNotificationWellWindow : public LLSysWellWindow
{
public:
	LLLegacyNotificationWellWindow(const LLSD& key);
	static LLLegacyNotificationWellWindow* getInstance(const LLSD& key = LLSD());
	
	BOOL postBuild() override;
	void setVisible(BOOL visible) override;
	void onAdd(LLNotificationPtr notify);
	// Operating with items
	void addItem(const LLSysWellItem::Params& p);
	
	// Closes all notifications and removes them from the Notification Well
	void closeAll();
	
protected:
	struct WellNotificationChannel : public LLNotificationChannel
	{
		WellNotificationChannel(LLLegacyNotificationWellWindow*);
		void onDelete(LLNotificationPtr notify)
		{
			mWellWindow->removeItemByID(notify->getID());
		}
		
		LLLegacyNotificationWellWindow* mWellWindow;
	};
	
	LLNotificationChannelPtr mNotificationUpdates;
	const std::string& getAnchorViewName() override { return NOTIFICATION_WELL_ANCHOR_NAME; }
	
private:
	// init Window's channel
	void initChannel() override;
	void clearScreenChannels();
	
	void onStoreToast(LLPanel* info_panel, LLUUID id);
	
	// Handlers
	void onItemClick(LLSysWellItem* item);
	void onItemClose(LLSysWellItem* item);
	
	// ID of a toast loaded by user (by clicking notification well item)
	LLUUID mLoadedToastId;
};

#endif // LL_NOTIFICATIONWELLWINDOW_H

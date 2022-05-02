/**
 * @file alpanelradaralert.cpp
 * @brief Radar alert tip toast
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2014, Cinder Roxley
 * Copyright (C) 2022, Rye Mutt <rye@alchemyviewer.org>
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

#include "alpanelradaralert.h"

#include "llnotifications.h"
#include "lltextbox.h"
#include "llviewercontrol.h" // for gSavedSettings

ALPanelRadarAlert::ALPanelRadarAlert(const LLNotificationPtr& notification) :
    LLPanelTipToast(notification)
{
	buildFromFile("panel_radar_alert_toast.xml");
	
	getChild<LLUICtrl>("message")->setValue(notification->getMessage());
	
	if (notification->getPayload().has("respond_on_mousedown")
		&& notification->getPayload()["respond_on_mousedown"])
	{
		setMouseDownCallback(boost::bind(&LLNotification::respond,
										 notification, notification->getResponseTemplate()));
	}
	
	S32 max_line_count =  gSavedSettings.getS32("TipToastMessageLineCount");
	snapToMessageHeight(getChild<LLTextBox> ("message"), max_line_count);
	
}

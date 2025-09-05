/*
 * @file alpanelstreaminfo.cpp
 * @brief Toast tip panel for stream info
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (c) 2015, Cinder Roxley <cinder@sdf.org>
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

#include "llviewerprecompiledheaders.h"
#include "alpanelstreaminfo.h"

#include "llnotifications.h"
#include "lltextbox.h"
#include "llviewercontrol.h" // for gSavedSettings

ALPanelStreamInfo::ALPanelStreamInfo(const LLNotificationPtr& notification)
:   LLPanelTipToast(notification)
{
    buildFromFile("panel_stream_info_toast.xml");

    //getChild<LLUICtrl>("music_icon")->setValue(notification->getPayload()["FROM_ID"]);
    getChild<LLUICtrl>("message")->setValue(notification->getMessage());

    if (notification->getPayload().has("respond_on_mousedown")
        && notification->getPayload()["respond_on_mousedown"])
    {
        setMouseDownCallback(boost::bind(&LLNotification::respond,
                                         notification, notification->getResponseTemplate()));
    }

    S32 max_line_count = gSavedSettings.getS32("TipToastMessageLineCount");
    snapToMessageHeight(getChild<LLTextBox>("message"), max_line_count);
}

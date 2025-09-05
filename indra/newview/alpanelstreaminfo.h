/*
 * @file alpanelstreaminfo.h
 * @brief Toast tip panel for stream info prototypes
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

#ifndef AL_PANELSTREAMINFO_H
#define AL_PANELSTREAMINFO_H

#include "llpaneltiptoast.h"

/**
 * Represents audio steam info tip toast panel.
 */
class ALPanelStreamInfo final : public LLPanelTipToast
{
    // disallow instantiation of this class
private:
    // grant privileges to instantiate this class to LLToastPanel
    friend class LLToastPanel;

    ALPanelStreamInfo(const LLNotificationPtr& notification);
    virtual ~ALPanelStreamInfo() = default;
};

#endif // AL_PANELSTREAMINFO_H

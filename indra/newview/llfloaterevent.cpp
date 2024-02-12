/** 
 * @file llfloaterevent.cpp
 * @brief Display for events in the finder
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2014, Cinder Roxley @ Second Life
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

#include "llfloaterevent.h"
#include "llpaneleventinfo.h"


LLFloaterEvent::LLFloaterEvent(const LLSD& key)
:	LLFloater(key)
,	mEventId(0)
{
	
}

void LLFloaterEvent::setEventID(const U32 event_id)
{
	mEventId = event_id;
	if (event_id == 0) closeFloater();
	
	getChild<LLPanel>("event_panel")->onOpen(mEventId);
}

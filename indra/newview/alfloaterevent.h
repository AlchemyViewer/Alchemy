/**
 * @file alfloaterevent.h
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

#ifndef AL_LLFLOATEREVENT_H
#define AL_LLFLOATEREVENT_H

#include "llfloater.h"

class ALFloaterEvent final : public LLFloater

{
public:
    ALFloaterEvent(const LLSD& key);
    /*virtual*/ ~ALFloaterEvent() = default;
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void setEventID(const U32 event_id);

private:
    LLPanel* mPanel;
};

#endif // AL_LLFLOATEREVENT_H

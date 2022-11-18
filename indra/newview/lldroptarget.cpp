/*
 * @file lldroptarget.cpp
 * @brief Base drop target class
 * @author Cinder Roxley, based on code from llpanelavatar.cpp
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
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

#include "lldroptarget.h"
#include "lltooldraganddrop.h"

static LLDefaultChildRegistry::Register<LLDropTarget> r("drop_target");

LLDropTarget::LLDropTarget(const LLDropTarget::Params& p)
:	LLView(p),
mAgentID(p.agent_id)
{}

void LLDropTarget::doDrop(EDragAndDropType cargo_type, void* cargo_data)
{
	LL_INFOS("") << "LLDropTarget::doDrop()" << LL_ENDL;
}

BOOL LLDropTarget::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop,
									 EDragAndDropType cargo_type,
									 void* cargo_data,
									 EAcceptance* accept,
									 std::string& tooltip_msg)
{
	if(getParent())
	{
		LLToolDragAndDrop::handleGiveDragAndDrop(mAgentID, LLUUID::null, drop,
												 cargo_type, cargo_data, accept);
		
		return TRUE;
	}
	
	return FALSE;
}

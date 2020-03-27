/**
* @file alviewermenu.cpp
* @brief Builds menus out of items. Imagine the fast, easy, fun Alchemy style
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) 2013 Alchemy Developer Group
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
* $/LicenseInfo$
**/

#include "llviewerprecompiledheaders.h"
#include "alviewermenu.h"

// library
#include "llview.h"

// newview
#include "alavataractions.h"
#include "llselectmgr.h"
#include "llviewermenu.h"
#include "llviewerobject.h"
#include "llvoavatar.h"

namespace
{
	bool avatar_copy_data(const LLSD& userdata)
	{
		LLViewerObject* objectp = LLSelectMgr::getInstance()->getSelection()->getPrimaryObject();
		if (!objectp)
			return false;

		LLVOAvatar* avatarp = find_avatar_from_object(objectp);
		if (avatarp)
		{
			ALAvatarActions::copyData(avatarp->getID(), userdata);
			return true;
		}
		return false;
	}
}

////////////////////////////////////////////////////////

void ALViewerMenu::initialize_menus()
{
	//LLUICtrl::EnableCallbackRegistry::Registrar& enable = LLUICtrl::EnableCallbackRegistry::currentRegistrar();
	LLUICtrl::CommitCallbackRegistry::Registrar& commit = LLUICtrl::CommitCallbackRegistry::currentRegistrar();
	commit.add("Avatar.CopyData", [](LLUICtrl* ctrl, const LLSD& param) { avatar_copy_data(param); });
}

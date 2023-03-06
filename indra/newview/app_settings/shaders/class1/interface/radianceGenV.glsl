<<<<<<<< HEAD:indra/newview/llfloaternewlocalinventory.h
/**
 * @file llfloaternewlocalinventory.h
 * @brief Create fake local inventory item
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Cinder Roxley
 *
========
/** 
 * @file radianceGenV.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 * 
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/interface/radianceGenV.glsl
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

#include "llfloater.h"

<<<<<<<< HEAD:indra/newview/llfloaternewlocalinventory.h
class LLLineEditor;

class LLFloaterNewLocalInventory
:	public LLFloater
{
public:
	LLFloaterNewLocalInventory(const LLSD& key);
	BOOL postBuild() override;
========
ATTRIBUTE vec3 position;

VARYING vec3 vary_dir;

void main()
{
	gl_Position = vec4(position, 1.0);

	vary_dir = vec3(modelview_matrix * vec4(position, 1.0)).xyz;
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/interface/radianceGenV.glsl

	void onClickOK();
	static LLUUID sLastCreatorId;

private:
    ~LLFloaterNewLocalInventory() override = default;
};

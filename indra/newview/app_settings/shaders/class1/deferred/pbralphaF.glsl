<<<<<<<< HEAD:indra/newview/algamemode.h
/**
 * @file algamemode.h
 * @brief Support for FeralInteractive's GameMode
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (C) 2021, XenHat <me@xenh.at>
 *
========
/** 
 * @file pbralphaF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 * 
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/pbralphaF.glsl
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
 * $/LicenseInfo$
 */

<<<<<<<< HEAD:indra/newview/algamemode.h
#ifndef AL_GAMEMODE_H
#define AL_GAMEMODE_H

class ALGameMode final
{

public:
	static ALGameMode &instance();

	void enable(bool enable);
	void init();
	static void shutdown();

protected:
	void onToggleGameModeControl();
	bool mEnabled = false;
	short mStatus = 0;
};

#endif // AL_GAMEMODE_H
========
 // debug stub

out vec4 frag_color;

void main()
{
    frag_color = vec4(1.0, 0, 0.5, 0.5);
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/pbralphaF.glsl

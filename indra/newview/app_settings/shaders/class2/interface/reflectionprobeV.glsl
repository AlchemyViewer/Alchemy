<<<<<<<< HEAD:indra/newview/alpanelquicksettingspulldown.h
/**
 * @file alpanelquicksettingspulldown.h
 * @brief Quick Settings popdown panel
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2013-2014, Alchemy Viewer Project.
 *
========
/** 
 * @file reflectionprobeV.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 * 
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class2/interface/reflectionprobeV.glsl
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
 * $/LicenseInfo$
 */

<<<<<<<< HEAD:indra/newview/alpanelquicksettingspulldown.h
#ifndef AL_ALPANELQUICKSETTINGSPULLDOWN_H
#define AL_ALPANELQUICKSETTINGSPULLDOWN_H

#include "llpanelpulldown.h"

class LLFrameTimer;
========
 
in vec3 position;

out vec2 vary_fragcoord;
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class2/interface/reflectionprobeV.glsl

class ALPanelQuickSettingsPulldown final : public LLPanelPulldown
{
<<<<<<<< HEAD:indra/newview/alpanelquicksettingspulldown.h
public:
	ALPanelQuickSettingsPulldown();
};

#endif // AL_ALPANELQUICKSETTINGSPULLDOWN_H
========
    //transform vertex
    vec4 pos = vec4(position.xyz, 1.0);
    gl_Position = pos; 

    vary_fragcoord = (pos.xy*0.5+0.5);
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class2/interface/reflectionprobeV.glsl

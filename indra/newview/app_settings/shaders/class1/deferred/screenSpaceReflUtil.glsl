/**
<<<<<<<< HEAD:indra/newview/alpanelradaralert.h
 * @file alpanelonlinestatus.h
 * @brief Radar alert tip toasts
========
 * @file class3/deferred/screenSpaceReflUtil.glsl
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/screenSpaceReflUtil.glsl
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
<<<<<<<< HEAD:indra/newview/alpanelradaralert.h
 * Copyright (C) 2010, Linden Research, Inc.
 * COpyright (C) 2014, Cinder Roxley
========
 * Copyright (C) 2007, Linden Research, Inc.
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/screenSpaceReflUtil.glsl
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

<<<<<<<< HEAD:indra/newview/alpanelradaralert.h
#ifndef LL_PANELRADARALERT_H
#define LL_PANELRADARALERT_H

#include "llpaneltiptoast.h"

/**
 * Represents radar alert toast panel.
 */
class ALPanelRadarAlert final : public LLPanelTipToast
{
	// disallow instantiation of this class
private:
	// grant privileges to instantiate this class to LLToastPanel
	friend class LLToastPanel;
	
	ALPanelRadarAlert(const LLNotificationPtr& notification);
	virtual ~ALPanelRadarAlert() = default;
};

#endif // LL_PANELRADARALERT_H
========
// debug stub

float random (vec2 uv) 
{
    return 0;
}

float tapScreenSpaceReflection(int totalSamples, vec2 tc, vec3 viewPos, vec3 n, inout vec4 collectedColor, sampler2D source)
{
    collectedColor = vec4(0);
    return 0;
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/screenSpaceReflUtil.glsl

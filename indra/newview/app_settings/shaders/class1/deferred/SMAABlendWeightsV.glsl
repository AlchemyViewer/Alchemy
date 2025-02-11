/**
<<<<<<<< HEAD:indra/newview/alpanelradaralert.h
 * @file alpanelonlinestatus.h
 * @brief Radar alert tip toasts
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * COpyright (C) 2014, Cinder Roxley
========
 * @file SMAABlendWeightsV.glsl
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
>>>>>>>> Second_Life_Release#bb5fa35-ExtraFPS:indra/newview/app_settings/shaders/class1/deferred/SMAABlendWeightsV.glsl
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
/*[EXTRA_CODE_HERE]*/

uniform mat4 modelview_projection_matrix;

in vec3 position;

out vec2 vary_texcoord0;
out vec2 vary_pixcoord;
out vec4 vary_offset[3];

#define float4 vec4
#define float2 vec2
void SMAABlendingWeightCalculationVS(float2 texcoord,
                                     out float2 pixcoord,
                                     out float4 offset[3]);

void main()
{
    gl_Position = vec4(position.xyz, 1.0);
    vary_texcoord0 = (gl_Position.xy*0.5+0.5);

    SMAABlendingWeightCalculationVS(vary_texcoord0,
                                    vary_pixcoord,
                                    vary_offset);
}

>>>>>>>> Second_Life_Release#bb5fa35-ExtraFPS:indra/newview/app_settings/shaders/class1/deferred/SMAABlendWeightsV.glsl

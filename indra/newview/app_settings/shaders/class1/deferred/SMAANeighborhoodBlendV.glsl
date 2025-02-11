/**
<<<<<<<< HEAD:indra/media_plugins/cef/dummy_volume_catcher.cpp
 * @file dummy_volume_catcher.cpp
 * @brief A null implementation of the "VolumeCatcher" class for platforms where it's not implemented yet.
 *
 * @cond
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
========
 * @file SMAANeighborhoodBlendV.glsl
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
>>>>>>>> Second_Life_Release#bb5fa35-ExtraFPS:indra/newview/app_settings/shaders/class1/deferred/SMAANeighborhoodBlendV.glsl
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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
 * @endcond
 */

<<<<<<<< HEAD:indra/media_plugins/cef/dummy_volume_catcher.cpp
#include "volume_catcher.h"


class VolumeCatcherImpl
{
};

/////////////////////////////////////////////////////

VolumeCatcher::VolumeCatcher()
{
    pimpl = NULL;
}

VolumeCatcher::~VolumeCatcher()
{
}

void VolumeCatcher::setVolume(F32 volume)
{
}

void VolumeCatcher::setPan(F32 pan)
{
}

void VolumeCatcher::pump()
{
========
/*[EXTRA_CODE_HERE]*/

uniform mat4 modelview_projection_matrix;

in vec3 position;

out vec2 vary_texcoord0;
out vec4 vary_offset;

#define float4 vec4
#define float2 vec2
void SMAANeighborhoodBlendingVS(float2 texcoord,
                                out float4 offset);

void main()
{
    gl_Position = vec4(position.xyz, 1.0);
    vary_texcoord0 = (gl_Position.xy*0.5+0.5);

    SMAANeighborhoodBlendingVS(vary_texcoord0, vary_offset);
>>>>>>>> Second_Life_Release#bb5fa35-ExtraFPS:indra/newview/app_settings/shaders/class1/deferred/SMAANeighborhoodBlendV.glsl
}


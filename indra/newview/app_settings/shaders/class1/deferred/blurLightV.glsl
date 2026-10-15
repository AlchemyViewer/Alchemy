/**
 * @file blurLightF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

in vec3 position;

out vec2 vary_fragcoord;
uniform vec2 screen_res;

void main()
{
    // On the far plane, as the light map's own pass is (sunLightV.glsl): only geometry is blurred, and the sky keeps
    // the clear's white, which the blur's plane test never takes in anyway. Reverse-Z puts the far plane at ndc z 0.
#ifdef REVERSE_Z
    gl_Position = vec4(position.xy, 0.0, 1.0);
#else
    gl_Position = vec4(position.xy, 1.0, 1.0);
#endif
    vary_fragcoord = (position.xy*0.5+0.5);
}

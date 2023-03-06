/** 
<<<<<<<< HEAD:indra/newview/app_settings/shaders/class1/alchemy/postNoTCV.glsl
 * @file postNoTCV.glsl
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2021, Rye Mutt<rye@alchemyviewer.org>
========
 * @file class3\deferred\genbrdflutV.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/genbrdflutV.glsl
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
 * $/LicenseInfo$
 */

uniform mat4 modelview_projection_matrix;

ATTRIBUTE vec3 position;

<<<<<<<< HEAD:indra/newview/app_settings/shaders/class1/alchemy/postNoTCV.glsl
VARYING vec2 vary_fragcoord;

uniform vec2 screen_res = vec2(1.f, 1.f);
========
VARYING vec2 vary_uv;
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/genbrdflutV.glsl

void main()
{
	//transform vertex
	vec4 pos = modelview_projection_matrix * vec4(position.xyz, 1.0);
<<<<<<<< HEAD:indra/newview/app_settings/shaders/class1/alchemy/postNoTCV.glsl
	gl_Position = pos;

	vary_fragcoord = (pos.xy*0.5+0.5) * screen_res;
========
	vary_uv = position.xy*0.5+0.5;

	gl_Position = vec4(position.xyz, 1.0);
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class1/deferred/genbrdflutV.glsl
}

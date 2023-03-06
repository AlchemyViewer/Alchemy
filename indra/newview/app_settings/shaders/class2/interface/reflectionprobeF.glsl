<<<<<<<< HEAD:indra/llimage/llimagewebp.h
/*
 * @file llimagewebp.h
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2020, Rye Mutt <rye@alchemyviewer.org>
 *
========
/** 
 * @file reflectionprobeF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 * 
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class2/interface/reflectionprobeF.glsl
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

<<<<<<<< HEAD:indra/llimage/llimagewebp.h
#ifndef AL_ALIMAGEWEBP_H
#define AL_ALIMAGEWEBP_H

#include "stdtypes.h"
#include "llimage.h"

class LLImageWebP final : public LLImageFormatted
{
protected:
	~LLImageWebP() = default;

public:
    LLImageWebP();

	/*virtual*/ std::string getExtension() { return std::string("webp"); }
	/*virtual*/ bool updateData();
	/*virtual*/ bool decode(LLImageRaw* raw_image, F32 decode_time);
	/*virtual*/ bool encode(const LLImageRaw* raw_image, F32 encode_time);
};

#endif
========
out vec4 frag_color;

in vec2 vary_fragcoord;

vec4 getPositionWithDepth(vec2 pos_screen, float depth);
float getDepth(vec2 pos_screen);

vec4 sampleReflectionProbesDebug(vec3 pos);

void main()
{
    vec2  tc           = vary_fragcoord.xy;
    float depth        = getDepth(tc.xy);
    vec4  pos          = getPositionWithDepth(tc, depth);

    frag_color = sampleReflectionProbesDebug(pos.xyz);
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/class2/interface/reflectionprobeF.glsl

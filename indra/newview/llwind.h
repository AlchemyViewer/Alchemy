/** 
 * @file llwind.h
 * @brief LLWind class header file
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLWIND_H
#define LL_LLWIND_H

#include "llmath.h"
#include "v3math.h"
#include "v3dmath.h"

class LLVector3;
class LLBitPack;
class LLGroupHeader;

class LLWind  
{
	static const size_t WIND_SIZE = 16;
	static const size_t ARRAY_SIZE = WIND_SIZE * WIND_SIZE;
public:
	static constexpr F32 WIND_SCALE_HACK = 2.0f; // hack to make wind speeds more realistic

	LLWind();
	~LLWind() = default;
	void renderVectors(); // defined in llglsandbox.cpp
	LLVector3 getVelocity(const LLVector3 &location); // "location" is region-local
	LLVector3 getVelocityNoisy(const LLVector3 &location, const F32 dim);	// "location" is region-local

	void decompress(LLBitPack &bitpack, LLGroupHeader *group_headerp);
	LLVector3 getAverage();

	void setOriginGlobal(const LLVector3d &origin_global);
private:
	std::array<F32, ARRAY_SIZE> mVelX;
	std::array<F32, ARRAY_SIZE> mVelY;

	LLVector3d mOriginGlobal;
	void init();

	LOG_CLASS(LLWind);
};

#endif

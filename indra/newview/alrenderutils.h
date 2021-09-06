/**
* @file alrenderutils.h
* @brief Alchemy Render Utility
*
* $LicenseInfo:firstyear=2021&license=viewerlgpl$
* Alchemy Viewer Source Code
* Copyright (C) 2021, Alchemy Viewer Project.
* Copyright (C) 2021, Rye Mutt <rye@alchemyviewer.org>
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

#pragma once

#include "llpointer.h"

#define AL_TONEMAP_COUNT 10

class LLRenderTarget;
class LLVertexBuffer;

class ALRenderUtil
{
public:
	ALRenderUtil();
	~ALRenderUtil() = default;

	void restoreVertexBuffers();
	void resetVertexBuffers();

	void releaseGLBuffers();

	void refreshState();

	// Deferred Only Functions
	enum ALTonemap : uint32_t
	{
		NONE = 0,
		LINEAR,
		REINHARD,
		REINHARD2,
		FILMIC,
		UNREAL,
		ACES,
		UCHIMURA,
		LOTTES,
		UNCHARTED,
		TONEMAP_COUNT
	};
	bool setupTonemap();
	void renderTonemap(LLRenderTarget* src, LLRenderTarget* dst);
	// End Deferred Only

	bool setupColorGrade();

private:
	// Parameters
	F32 mTonemapExposure = 1.f;

	// State
	U32 mTonemapType = ALTonemap::NONE;
	LLVector3 mToneLottesParamA;
	LLVector3 mToneLottesParamB;
	LLVector3 mToneUchimuraParamA;
	LLVector3 mToneUchimuraParamB;
	LLVector3 mToneUnchartedParamA;
	LLVector3 mToneUnchartedParamB;
	LLVector3 mToneUnchartedParamC;

	// Texture Data
	U32 mCGLut;
	LLVector4 mCGLutSize;

	// Vertex Buffers
	LLPointer<LLVertexBuffer> mRenderBuffer;
};

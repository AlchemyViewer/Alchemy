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

#define AL_TONEMAP_COUNT 9

class LLRenderTarget;
class LLVertexBuffer;

class ALRenderUtil
{
public:
	ALRenderUtil() = default;
	~ALRenderUtil() = default;

	void restoreVertexBuffers();
	void resetVertexBuffers();

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
		TONEMAP_COUNT
	};
	void renderTonemap(LLRenderTarget* src, LLRenderTarget* dst);


private:
	LLPointer<LLVertexBuffer> mRenderBuffer;
};

/** 
 * @file listener_fmodstudio.h
 * @brief Description of LISTENER class abstracting the audio support
 * as an FMOD Studio implementation (windows and Linux)
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_LISTENER_FMODSTUDIO_H
#define LL_LISTENER_FMODSTUDIO_H

#include "lllistener.h"

//Stubs
namespace FMOD
{
	class System;
}

//Interfaces
class LLListener_FMODSTUDIO final : public LLListener
{
 public:  
	LLListener_FMODSTUDIO(FMOD::System *system);
	virtual ~LLListener_FMODSTUDIO() = default;

	void translate(const LLVector3& offset) final override;
	void setPosition(const LLVector3& pos) final override;
	void setVelocity(const LLVector3& vel) final override;
	void orient(const LLVector3& up, const LLVector3& at) final override;
	void commitDeferredChanges() final override;

	void setDopplerFactor(F32 factor) final override;
	F32 getDopplerFactor() final override;
	void setRolloffFactor(F32 factor) final override;
	F32 getRolloffFactor() final override;
 protected:
	 FMOD::System *mSystem;
	 F32 mDopplerFactor;
	 F32 mRolloffFactor;
};

#endif



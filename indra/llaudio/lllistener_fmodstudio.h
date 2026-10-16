/**
 * @file listener_fmodstudio.h
 * @brief Description of LISTENER class abstracting the audio support
 * as an FMOD 3D implementation
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Linden Research, Inc.
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
class LLListener_FMODSTUDIO : public LLListener
{
public:
    LLListener_FMODSTUDIO(FMOD::System *system);
    ~LLListener_FMODSTUDIO() override;
    void init() override;

    void translate(LLVector3 offset) override;
    void setPosition(LLVector3 pos) override;
    void setVelocity(LLVector3 vel) override;
    void orient(LLVector3 up, LLVector3 at) override;
    void commitDeferredChanges() override;

    void setDopplerFactor(F32 factor) override;
    F32 getDopplerFactor() override;
    void setRolloffFactor(F32 factor) override;
    F32 getRolloffFactor() override;
protected:
    FMOD::System *mSystem;
    F32 mDopplerFactor;
    F32 mRolloffFactor;
};

#endif



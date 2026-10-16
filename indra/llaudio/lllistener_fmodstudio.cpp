/**
 * @file listener_fmodstudio.cpp
 * @brief Implementation of LISTENER class abstracting the audio
 * support as a FMODSTUDIO implementation
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

#include "linden_common.h"
#include "llaudioengine.h"
#include "lllistener_fmodstudio.h"
#include <fmod.hpp>

// An LLVector3 as FMOD takes it. The two are laid out alike, but reading one
// through a pointer to the other breaks strict aliasing, which GCC rejects.
static FMOD_VECTOR to_fmod_vector(const LLVector3& v)
{
    return FMOD_VECTOR{ v.mV[VX], v.mV[VY], v.mV[VZ] };
}

//-----------------------------------------------------------------------
// constructor
//-----------------------------------------------------------------------
LLListener_FMODSTUDIO::LLListener_FMODSTUDIO(FMOD::System *system)
{
    mSystem = system;
    init();
}

//-----------------------------------------------------------------------
LLListener_FMODSTUDIO::~LLListener_FMODSTUDIO()
{
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::init(void)
{
    // do inherited
    LLListener::init();
    mDopplerFactor = 1.0f;
    mRolloffFactor = 1.0f;
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::translate(LLVector3 offset)
{
    LLListener::translate(offset);

    const FMOD_VECTOR position = to_fmod_vector(mPosition);
    const FMOD_VECTOR forward = to_fmod_vector(mListenAt);
    const FMOD_VECTOR up = to_fmod_vector(mListenUp);
    mSystem->set3DListenerAttributes(0, &position, nullptr, &forward, &up);
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::setPosition(LLVector3 pos)
{
    LLListener::setPosition(pos);

    const FMOD_VECTOR position = to_fmod_vector(mPosition);
    const FMOD_VECTOR forward = to_fmod_vector(mListenAt);
    const FMOD_VECTOR up = to_fmod_vector(mListenUp);
    mSystem->set3DListenerAttributes(0, &position, nullptr, &forward, &up);
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::setVelocity(LLVector3 vel)
{
    LLListener::setVelocity(vel);

    const FMOD_VECTOR velocity = to_fmod_vector(mVelocity);
    const FMOD_VECTOR forward = to_fmod_vector(mListenAt);
    const FMOD_VECTOR up = to_fmod_vector(mListenUp);
    mSystem->set3DListenerAttributes(0, nullptr, &velocity, &forward, &up);
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::orient(LLVector3 up, LLVector3 at)
{
    LLListener::orient(up, at);

    // at = -at; by default Fmod studio is 'left-handed' but we are providing
    // flag FMOD_INIT_3D_RIGHTHANDED so no correction are needed

    const FMOD_VECTOR fmod_at = to_fmod_vector(at);
    const FMOD_VECTOR fmod_up = to_fmod_vector(up);
    mSystem->set3DListenerAttributes(0, nullptr, nullptr, &fmod_at, &fmod_up);
}

//-----------------------------------------------------------------------
void LLListener_FMODSTUDIO::commitDeferredChanges()
{
    if (!mSystem)
    {
        return;
    }

    mSystem->update();
}


void LLListener_FMODSTUDIO::setRolloffFactor(F32 factor)
{
    //An internal FMOD optimization skips 3D updates if there have not been changes to the 3D sound environment.
    // (this was true for FMODex, looks to be still true for FMOD STUDIO, but needs a recheck)
    //Sadly, a change in rolloff is not accounted for, thus we must touch the listener properties as well.
    //In short: Changing the position ticks a dirtyflag inside fmod, which makes it not skip 3D processing next update call.
    if (mRolloffFactor != factor)
    {
        LLVector3 pos = mPosition - LLVector3(0.f, 0.f, .1f);
        const FMOD_VECTOR nudged = to_fmod_vector(pos);
        const FMOD_VECTOR position = to_fmod_vector(mPosition);
        mSystem->set3DListenerAttributes(0, &nudged, nullptr, nullptr, nullptr);
        mSystem->set3DListenerAttributes(0, &position, nullptr, nullptr, nullptr);
    }
    mRolloffFactor = factor;
    mSystem->set3DSettings(mDopplerFactor, 1.f, mRolloffFactor);
}


F32 LLListener_FMODSTUDIO::getRolloffFactor()
{
    return mRolloffFactor;
}


void LLListener_FMODSTUDIO::setDopplerFactor(F32 factor)
{
    mDopplerFactor = factor;
    mSystem->set3DSettings(mDopplerFactor, 1.f, mRolloffFactor);
}


F32 LLListener_FMODSTUDIO::getDopplerFactor()
{
    return mDopplerFactor;
}

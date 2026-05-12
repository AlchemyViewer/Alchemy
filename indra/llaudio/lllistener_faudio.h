/**
 * @file lllistener_faudio.h
 * @brief LISTENER class abstracting the audio support
 * as an FAudio (X3DAudio) 3D implementation.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#ifndef LL_LISTENER_FAUDIO_H
#define LL_LISTENER_FAUDIO_H

#include "lllistener.h"

#include <F3DAudio.h>

class LLAudioEngine_FAudio;
class LLAudioChannelFAudio;

class LLListener_FAudio : public LLListener
{
public:
    explicit LLListener_FAudio(LLAudioEngine_FAudio* engine);
    ~LLListener_FAudio() override;

    void translate(LLVector3 offset) override;
    void setPosition(LLVector3 pos) override;
    void setVelocity(LLVector3 vel) override;
    void orient(LLVector3 up, LLVector3 at) override;
    void commitDeferredChanges() override;

    void setDopplerFactor(F32 factor) override;
    F32  getDopplerFactor() override;
    void setRolloffFactor(F32 factor) override;
    F32  getRolloffFactor() override;

    // Compute and apply the 3D output matrix + Doppler ratio for a single
    // playing channel. Called from LLAudioChannelFAudio::update3DPosition().
    void apply3D(LLAudioChannelFAudio* channel);

    // Apply a flat, omnidirectional, no-Doppler matrix for forced-priority
    // sounds (UI / preview). Routes the source to all output speakers via
    // F3DAudio's near-field diffusion, so 5.1/7.1 setups get full coverage
    // instead of just the front pair.
    void applyForcedPriority(LLAudioChannelFAudio* channel);

private:
    void syncListenerPose();

    LLAudioEngine_FAudio* mEnginep    = nullptr;
    F3DAUDIO_LISTENER     mF3DListener{};
    F32                   mDopplerFactor = 1.0f;
    F32                   mRolloffFactor = 1.0f;
};

#endif

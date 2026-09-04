/**
 * @file altestcharacter.h
 * @brief LLCharacter stub for llcharacter tests: a five-bone chain under a
 *        root so motions can resolve joints by name, and a settable answer
 *        for every pure virtual.
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
 * $/LicenseInfo$
 */

#pragma once

#include "llcharacter.h"
#include "lljoint.h"
#include "llquaternion.h"
#include "lluuid.h"
#include "v3dmath.h"
#include "v3math.h"

#include <array>
#include <string>
#include <vector>

class ALTestCharacter final : public LLCharacter
{
public:
    static constexpr U32 NUM_JOINTS = 5;
    static constexpr const char* JOINT_NAMES[NUM_JOINTS] = { "mPelvis", "mTorso", "mChest", "mNeck", "mHead" };

    ALTestCharacter()
    {
        mRoot.setup("mRoot", nullptr);
        LLJoint* parent = &mRoot;
        for (U32 i = 0; i < NUM_JOINTS; ++i)
        {
            mJoints[i].setup(JOINT_NAMES[i], parent);
            mJoints[i].setJointNum(i);
            parent = &mJoints[i];
        }
    }

    const char* getAnimationPrefix() override { return "test"; }
    LLJoint* getRootJoint() override { return &mRoot; }
    LLVector3 getCharacterPosition() override { return mPosition; }
    LLQuaternion getCharacterRotation() override { return mRotation; }
    LLVector3 getCharacterVelocity() override { return mVelocity; }
    LLVector3 getCharacterAngularVelocity() override { return mAngularVelocity; }

    void getGround(const LLVector3& in_pos, LLVector3& out_pos, LLVector3& out_norm) override
    {
        out_pos = in_pos;
        out_pos.mV[VZ] = mGroundHeight;
        out_norm = LLVector3::z_axis;
    }

    LLJoint* getCharacterJoint(U32 i) override { return i < NUM_JOINTS ? &mJoints[i] : nullptr; }
    F32 getTimeDilation() override { return mTimeDilation; }
    F32 getPixelArea() const override { return mPixelArea; }
    LLPolyMesh* getHeadMesh() override { return nullptr; }
    LLPolyMesh* getUpperBodyMesh() override { return nullptr; }
    LLVector3d getPosGlobalFromAgent(const LLVector3& position) override { return LLVector3d(position); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d& position) override { return LLVector3(position); }
    void addDebugText(const std::string& text) override { mDebugText.push_back(text); }
    const LLUUID& getID() const override { return mID; }

    LLJoint mRoot;
    std::array<LLJoint, NUM_JOINTS> mJoints;

    LLVector3 mPosition;
    LLQuaternion mRotation;
    LLVector3 mVelocity;
    LLVector3 mAngularVelocity;
    F32 mGroundHeight = 0.f;
    F32 mTimeDilation = 1.f;
    F32 mPixelArea = 10000.f;
    LLUUID mID = LLUUID::generateNewID();
    std::vector<std::string> mDebugText;
};

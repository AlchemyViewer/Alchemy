/** 
* @file   llskinningutil.h
* @brief  Functions for mesh object skinning
* @author vir@lindenlab.com
*
* $LicenseInfo:firstyear=2015&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2015, Linden Research, Inc.
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
#ifndef LLSKINNINGUTIL_H
#define LLSKINNINGUTIL_H

#define DEBUG_SKINNING  LL_DEBUG

#include "lljoint.h"
#include "v2math.h"
#include "v4math.h"
#include "llvector4a.h"
#include "llmatrix4a.h"
#include "llmodel.h"

class LLVOAvatar;
class LLMeshSkinInfo;
class LLVolumeFace;
class LLJointRiggingInfoTab;

namespace LLSkinningUtil
{
    inline U32 getMaxJointCount()
    {
        return LL_MAX_JOINTS_PER_MESH_OBJECT;
    }
    U32 getMeshJointCount(const LLMeshSkinInfo *skin);
    void scrubInvalidJoints(LLVOAvatar *avatar, LLMeshSkinInfo* skin);

    void initJointNums(LLMeshSkinInfo* skin, LLVOAvatar *avatar);
    void initSkinningMatrixPalette(LLMatrix4a* mat, S32 count, const LLMeshSkinInfo* skin, LLVOAvatar *avatar);
    void checkSkinWeights(LLVector4a* weights, U32 num_vertices, const LLMeshSkinInfo* skin);
    void getPerVertexSkinMatrix(F32* weights, LLMatrix4a* mat, bool handle_bad_scale, LLMatrix4a& final_mat);

    void updateRiggingInfo(const LLMeshSkinInfo* skin, LLVOAvatar *avatar, LLVolumeFace& vol_face);

    inline void scrubSkinWeights(LLVector4a* weights, U32 num_vertices, const LLMeshSkinInfo* skin)
    {
        const S32 max_joints = skin->mJointNames.size();
        LLIVector4a max_joint((S16)max_joints - 1);
        LLIVector4a cur_joint;
        LLVector4a weight;
        for (U32 j = 0; j < num_vertices; j++)
        {
            cur_joint.setFloatTrunc(weights[j]);
            weight.setSub(weights[j], cur_joint);
            cur_joint.min16(max_joint);
            cur_joint.max16(LLIVector4a::getZero());
            weights[j].setAdd(weight, cur_joint);
        }
#if DEBUG_SKINNING
        checkSkinWeights(weights, num_vertices, skin);
#endif
    }

    inline void getPerVertexSkinMatrixChecked(const LLVector4a& weights, LLMatrix4a* mat, LLMatrix4a& final_mat)
    {
#if DEBUG_SKINNING
        bool valid_weights = true;
#endif
        alignas(16) S32 idx[4];

        LLIVector4a max_joint_count((S16)(getMaxJointCount() - 1));
        LLIVector4a current_joint_index;
        current_joint_index.setFloatTrunc(weights);

        LLVector4a weight;
        weight.setSub(weights, current_joint_index);

        current_joint_index.min16(max_joint_count);
        current_joint_index.max16(_mm_setzero_si128());
        current_joint_index.store128a(idx);

        LLVector4a scale;
        scale.setMoveHighLow(weight);
        scale.add(weight);
        scale.addFirst(scale.getVectorAt<1>());
        scale.splat<0>(scale);

        bool scale_invalid = scale.lessEqual(LLVector4a::getEpsilon()).areAnySet(LLVector4Logical::MASK_XYZW);
        if (scale_invalid)
        {
            weight = LLVector4a(1.0f, 0.0f, 0.0f, 0.0f);
#if DEBUG_SKINNING
            valid_weights = false;
#endif
        }
        else
        {
            // This is enforced  in unpackVolumeFaces()
            weight.div(scale);
        }

        final_mat.setMul(mat[idx[0]], weight.getVectorAt<0>());
        final_mat.setMulAdd(mat[idx[1]], weight.getVectorAt<1>());
        final_mat.setMulAdd(mat[idx[2]], weight.getVectorAt<2>());
        final_mat.setMulAdd(mat[idx[3]], weight.getVectorAt<3>());
#if DEBUG_SKINNING
        // SL-366 - with weight validation/cleanup code, it should no longer be
        // possible to hit the bad scale case.
        llassert(valid_weights);
#endif
    }

    inline void getPerVertexSkinMatrixUnchecked(const LLVector4a& weights, LLMatrix4a* mat, LLMatrix4a& final_mat)
    {
        alignas(16) S32 idx[4];

        LLIVector4a max_joint_count((S16)(getMaxJointCount() - 1));
        LLIVector4a current_joint_index;
        current_joint_index.setFloatTrunc(weights);

        LLVector4a weight;
        weight.setSub(weights, current_joint_index);

        current_joint_index.min16(max_joint_count);
        current_joint_index.max16(_mm_setzero_si128());
        current_joint_index.store128a(idx);

        LLVector4a scale;
        scale.setMoveHighLow(weight);
        scale.add(weight);
        scale.addFirst(scale.getVectorAt<1>());
        scale.splat<0>(scale);

        weight.div(scale);

        final_mat.setMul(mat[idx[0]], weight.getVectorAt<0>());
        final_mat.setMulAdd(mat[idx[1]], weight.getVectorAt<1>());
        final_mat.setMulAdd(mat[idx[2]], weight.getVectorAt<2>());
        final_mat.setMulAdd(mat[idx[3]], weight.getVectorAt<3>());
    }

    // This is used for extracting rotation from a bind shape matrix that
    // already has scales baked in
	inline LLQuaternion getUnscaledQuaternion(const LLMatrix4& mat4)
    {
        LLMatrix3 bind_mat = mat4.getMat3();
        for (auto i = 0; i < 3; i++)
        {
            F32 len = 0.0f;
            for (auto j = 0; j < 3; j++)
            {
                len += bind_mat.mMatrix[i][j] * bind_mat.mMatrix[i][j];
            }
            if (len > 0.0f)
            {
                len = sqrt(len);
                for (auto j = 0; j < 3; j++)
                {
                    bind_mat.mMatrix[i][j] /= len;
                }
            }
        }
        bind_mat.invert();
        LLQuaternion bind_rot = bind_mat.quaternion();
        bind_rot.normalize();
        return bind_rot;
    }
};

#endif

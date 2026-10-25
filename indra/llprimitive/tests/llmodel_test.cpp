/**
 * @file llmodel_test.cpp
 * @brief A model's generated normals and levels of detail against its skin
 *        weights.
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

#include "linden_common.h"

#include "llmodel.h"
#include "almeshsimplifier.h"
#include "llmath.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    LLVolumeParams meshParams()
    {
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        return params;
    }

    // A latitude-longitude sphere of one face, its seam column twice, and a
    // skin weight at each of its positions, as a loader keys them.
    LLPointer<LLModel> riggedSphere(S32 columns, S32 rows)
    {
        LLPointer<LLModel> model = new LLModel(meshParams(), 0.f);
        model->setNumVolumeFaces(1);
        LLVolumeFace& face = model->getVolumeFace(0);
        const S32 vertices = (columns + 1) * (rows + 1);
        face.resizeVertices(vertices);
        for (S32 r = 0; r <= rows; ++r)
        {
            for (S32 c = 0; c <= columns; ++c)
            {
                const F32 theta = F_PI * r / rows;
                const F32 phi = F_TWO_PI * (c % columns) / columns;
                const F32 ring = (r == 0 || r == rows) ? 0.f : sinf(theta);
                const F32 height = r == 0 ? 1.f : (r == rows ? -1.f : cosf(theta));
                const S32 v = c + r * (columns + 1);
                face.mPositions[v].set(0.5f * ring * cosf(phi), 0.5f * ring * sinf(phi), 0.5f * height, 0.f);
                face.mNormals[v].set(ring * cosf(phi), ring * sinf(phi), height, 0.f);
                face.mTexCoords[v].set((F32)c / columns, 1.f - (F32)r / rows);
            }
        }
        std::vector<U16> indices;
        auto at = [&](S32 c, S32 r) { return (U16)(c + r * (columns + 1)); };
        for (S32 r = 0; r < rows; ++r)
        {
            for (S32 c = 0; c < columns; ++c)
            {
                if (r > 0)
                {
                    indices.insert(indices.end(), { at(c, r), at(c, r + 1), at(c + 1, r) });
                }
                if (r < rows - 1)
                {
                    indices.insert(indices.end(), { at(c + 1, r), at(c, r + 1), at(c + 1, r + 1) });
                }
            }
        }
        face.resizeIndices((S32)indices.size());
        std::copy(indices.begin(), indices.end(), face.mIndices);

        for (S32 v = 0; v < vertices; ++v)
        {
            const LLVector3 position(face.mPositions[v].getF32ptr());
            const F32 lean = 0.5f + position.mV[2];
            model->mSkinWeights[position] = { LLModel::JointWeight(0, 1.f - lean), LLModel::JointWeight(1, lean) };
        }
        return model;
    }

    // Every vertex finds its weights by its position as it is, without the
    // nearest-point search that a moved vertex would fall back on.
    bool weightsFound(LLModel& model)
    {
        for (S32 f = 0; f < model.getNumVolumeFaces(); ++f)
        {
            const LLVolumeFace& face = model.getVolumeFace(f);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                if (model.mSkinWeights.find(LLVector3(face.mPositions[v].getF32ptr())) == model.mSkinWeights.end())
                {
                    return false;
                }
            }
        }
        return true;
    }

    TEST_CASE("llmodel: generated normals and a simplified level of detail keep each vertex's skin weights", "[llmodel]")
    {
        LLPointer<LLModel> model = riggedSphere(32, 16);
        REQUIRE(weightsFound(*model));

        model->generateNormals(30.f * DEG_TO_RAD);
        CHECK(model->getVolumeFace(0).mNumVertices > 0);
        CHECK(weightsFound(*model));

        using Seams = ALMeshSimplifier::Seams;
        for (Seams seams : { Seams::ALL, Seams::NONE, Seams::SLOPPY })
        {
            LLPointer<LLModel> lod = new LLModel(meshParams(), 0.f);
            lod->setNumVolumeFaces(1);
            ALMeshSimplifier::Target goal;
            goal.decimator = 9.f;
            REQUIRE(ALMeshSimplifier(*model).simplifyModel(*lod, goal, seams) > 0.f);
            lod->mSkinWeights = model->mSkinWeights;
            CHECK(weightsFound(*lod));
        }
    }
}

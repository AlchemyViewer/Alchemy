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
#include "hbxxh.h"
#include "llmath.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "almeshfacepack.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

    // An asset as writeModel writes it, with the header it returns and the
    // bytes that header takes at the front of the stream.
    struct WrittenAsset
    {
        std::string stream;
        LLSD header;
        size_t header_bytes = 0;

        std::string block(const char* name) const
        {
            const LLSD& entry = header[name];
            return stream.substr(header_bytes + (size_t)entry["offset"].asInteger(), (size_t)entry["size"].asInteger());
        }
    };

    // The triangles with area a loaded face draws, each as its corners'
    // positions, normals, texture coordinates and weights from its lowest
    // corner, its winding kept, sorted.
    std::vector<std::vector<F32>> drawnTriangles(const LLVolumeFace& face)
    {
        std::vector<std::vector<F32>> triangles;
        for (S32 t = 0; t + 2 < face.mNumIndices; t += 3)
        {
            std::vector<std::vector<F32>> corners;
            for (S32 i = 0; i < 3; ++i)
            {
                const U16 v = face.mIndices[t + i];
                std::vector<F32> corner(face.mPositions[v].getF32ptr(), face.mPositions[v].getF32ptr() + 3);
                corner.insert(corner.end(), face.mNormals[v].getF32ptr(), face.mNormals[v].getF32ptr() + 3);
                corner.insert(corner.end(), face.mTexCoords[v].mV, face.mTexCoords[v].mV + 2);
                if (face.mWeights)
                {
                    corner.insert(corner.end(), face.mWeights[v].getF32ptr(), face.mWeights[v].getF32ptr() + 4);
                }
                corners.push_back(std::move(corner));
            }
            auto same = [&](S32 a, S32 b) { return std::equal(corners[a].begin(), corners[a].begin() + 3, corners[b].begin()); };
            if (same(0, 1) || same(1, 2) || same(0, 2))
            {
                continue;
            }
            std::rotate(corners.begin(), std::min_element(corners.begin(), corners.end()), corners.end());
            std::vector<F32> triangle;
            for (const auto& corner : corners)
            {
                triangle.insert(triangle.end(), corner.begin(), corner.end());
            }
            triangles.push_back(std::move(triangle));
        }
        std::sort(triangles.begin(), triangles.end());
        return triangles;
    }

    std::string binaryOf(const LLSD& sd)
    {
        std::ostringstream out;
        LLSDSerialize::toBinary(sd, out);
        return out.str();
    }

    TEST_CASE("llmodel: an asset packed small holds the plain asset's faces repacked", "[llmodel]")
    {
        LLPointer<LLModel> model = riggedSphere(48, 24);
        LLModel::Decomposition decomp;
        decomp.mBaseHull = { LLVector3(-0.5f, -0.5f, -0.5f), LLVector3(0.5f, -0.5f, -0.5f), LLVector3(0.f, 0.5f, -0.5f),
                             LLVector3(0.f, 0.f, 0.5f) };

        auto write = [&](LLModel::EBlockPacking packing, ALMeshFacePack::Weights weights = ALMeshFacePack::Weights::KEEP)
        {
            std::ostringstream ostr;
            WrittenAsset asset;
            asset.header = LLModel::writeModel(ostr, nullptr, model, model, model, model, decomp, true, false, false, LLModel::WRITE_BINARY,
                                               false, 0, packing, weights);
            asset.stream = ostr.str();
            asset.header_bytes = binaryOf(asset.header).size();
            return asset;
        };
        const WrittenAsset plain = write(LLModel::PACK_PLAIN);
        const WrittenAsset small = write(LLModel::PACK_SMALL);
        const WrittenAsset snapped_12 = write(LLModel::PACK_SMALL_NORMALS_12);
        const WrittenAsset snapped_10 = write(LLModel::PACK_SMALL_NORMALS_10);
        const WrittenAsset rounded = write(LLModel::PACK_SMALL, ALMeshFacePack::Weights::ROUND_8_BITS);

        // Packed plain, the weights are written as they are.
        CHECK(write(LLModel::PACK_PLAIN, ALMeshFacePack::Weights::ROUND_8_BITS).stream == plain.stream);

        const char* const lods[] = { "lowest_lod", "low_lod", "medium_lod", "high_lod" };
        const char* const blocks[] = { "skin", "physics_convex", "lowest_lod", "low_lod", "medium_lod", "high_lod" };
        for (const WrittenAsset* asset : { &plain, &small, &snapped_12, &snapped_10, &rounded })
        {
            // The blocks lie one after another from the header to the end.
            std::vector<std::pair<S64, S64>> spans;
            for (const char* name : blocks)
            {
                CAPTURE(name);
                REQUIRE(asset->header.has(name));
                spans.emplace_back(asset->header[name]["offset"].asInteger(), asset->header[name]["size"].asInteger());
            }
            std::sort(spans.begin(), spans.end());
            S64 at = 0;
            for (const auto& [offset, size] : spans)
            {
                CHECK(offset == at);
                CHECK(size > 0);
                at = offset + size;
            }
            CHECK(asset->header_bytes + (size_t)at == asset->stream.size());
        }

        // Each block is what its own zip makes of its value. The skin and
        // the hull hold the same value every way; a LOD packed small holds
        // the plain LOD's faces repacked, and one packed small and snapped
        // holds them repacked with their normals snapped to that grid, and
        // one packed small with its weights rounded holds them repacked so.
        for (const char* name : blocks)
        {
            CAPTURE(name);
            const std::string plain_block = plain.block(name);
            LLSD plain_value;
            REQUIRE(LLUZipHelper::unzip_llsd(plain_value, (const U8*)plain_block.data(), (S32)plain_block.size()) == LLUZipHelper::ZR_OK);
            CHECK(zip_llsd(plain_value) == plain_block);
            const bool lod = std::any_of(std::begin(lods), std::end(lods), [&](const char* l) { return std::strcmp(l, name) == 0; });
            using Normals = ALMeshFacePack::Normals;
            using Weights = ALMeshFacePack::Weights;
            for (const auto& [asset, normals, weights] : { std::tuple{ &small, Normals::KEEP, Weights::KEEP },
                                                           std::tuple{ &snapped_12, Normals::SNAP_12_BITS, Weights::KEEP },
                                                           std::tuple{ &snapped_10, Normals::SNAP_10_BITS, Weights::KEEP },
                                                           std::tuple{ &rounded, Normals::KEEP, Weights::ROUND_8_BITS } })
            {
                CAPTURE((int)normals, (int)weights);
                const std::string packed_block = asset->block(name);
                LLSD packed_value;
                REQUIRE(LLUZipHelper::unzip_llsd(packed_value, (const U8*)packed_block.data(), (S32)packed_block.size()) ==
                        LLUZipHelper::ZR_OK);
                CHECK(zip_llsd_model(packed_value) == packed_block);
                LLSD expected = llsd_clone(plain_value);
                if (lod)
                {
                    for (LLSD::array_iterator face = expected.beginArray(); face != expected.endArray(); ++face)
                    {
                        CHECK(ALMeshFacePack::repack(*face, normals, weights));
                    }
                }
                CHECK(binaryOf(packed_value) == binaryOf(expected));
            }
        }

        // The LOD blocks, which the download weight charges, are smaller
        // packed small. This sphere's positions, at radius 0.5 over a domain
        // of -0.5 to 0.5, quantize to the bytes its normals do, so the zip
        // takes the normals as a copy of the positions; snapping them breaks
        // that, and how much it saves on a real mesh is almeshfacepack_test's
        // and the repack benchmark's to show.
        S64 plain_lod_bytes = 0, small_lod_bytes = 0;
        for (const char* name : lods)
        {
            plain_lod_bytes += plain.header[name]["size"].asInteger();
            small_lod_bytes += small.header[name]["size"].asInteger();
        }
        INFO("LOD blocks: " << small_lod_bytes << " bytes small, " << plain_lod_bytes << " plain");
        CHECK(small_lod_bytes < plain_lod_bytes);

        // Read back as the mesh repository reads an asset, the high LOD packed
        // small draws the same triangles as packed plain.
        auto load = [](const WrittenAsset& asset)
        {
            LLPointer<LLModel> back = new LLModel(meshParams(), (F32)LLModel::LOD_HIGH);
            std::istringstream is(asset.stream);
            REQUIRE(back->loadModel(is));
            return back;
        };
        LLPointer<LLModel> plain_back = load(plain);
        LLPointer<LLModel> small_back = load(small);
        REQUIRE(plain_back->getNumVolumeFaces() == 1);
        REQUIRE(small_back->getNumVolumeFaces() == 1);
        const auto plain_drawn = drawnTriangles(plain_back->getVolumeFace(0));
        CHECK(plain_drawn.size() == (size_t)(model->getVolumeFace(0).mNumIndices / 3));
        CHECK(drawnTriangles(small_back->getVolumeFace(0)) == plain_drawn);
        CHECK(small_back->mSkinWeights.size() == plain_back->mSkinWeights.size());
    }

    // A skin block of two joints, each matrix its sixteen elements row by
    // row: an inverse bind matrix and a joint position override for each
    // joint, and the bind shape matrix.
    LLSD twoJointSkin()
    {
        auto matrix = [](std::initializer_list<F64> elements)
        {
            LLSD out = LLSD::emptyArray();
            for (F64 element : elements)
            {
                out.append(element);
            }
            return out;
        };
        LLSD skin;
        skin["joint_names"] = llsd::array("mPelvis", "mTorso");
        skin["inverse_bind_matrix"] = llsd::array(matrix({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.33333, 0, -1.0625, 1 }),
                                                  matrix({ 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0.125, 0, -1.5, 1 }));
        skin["bind_shape_matrix"] = matrix({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0.25, 1 });
        skin["alt_inverse_bind_matrix"] = llsd::array(matrix({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -1.125, 1 }),
                                                      matrix({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -1.625, 1 }));
        skin["pelvis_offset"] = 0.0625;
        skin["lock_scale_if_joint_position"] = true;
        return skin;
    }

    bool sameMatrices(const LLMeshSkinInfo::matrix_list_t& a, const LLMeshSkinInfo::matrix_list_t& b)
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](const LLMatrix4a& x, const LLMatrix4a& y) { return std::memcmp(&x, &y, sizeof(LLMatrix4a)) == 0; });
    }

    TEST_CASE("llmodel: a skin without joints hashes nothing", "[llmodel]")
    {
        // The skin writeModel writes for a model its loader found no joints
        // in: the bind shape matrix alone.
        LLSD jointless;
        jointless["bind_shape_matrix"] = twoJointSkin()["bind_shape_matrix"];
        const LLMeshSkinInfo none(jointless);
        CHECK(none.mJointNames.empty());
        CHECK(none.mInvBindMatrix.empty());
        CHECK(none.mHash == HBXXH64::digest(std::string()));

        // Joint names and inverse bind matrices that disagree in number are
        // dropped together, which leaves the same.
        LLSD mismatched = twoJointSkin();
        mismatched["inverse_bind_matrix"].erase(1);
        const LLMeshSkinInfo dropped(mismatched);
        CHECK(dropped.mJointNames.empty());
        CHECK(dropped.mInvBindMatrix.empty());
        CHECK(dropped.mHash == none.mHash);
    }

    TEST_CASE("llmodel: a skin read back from an asset keeps its joints and its hash", "[llmodel]")
    {
        LLSD skin = twoJointSkin();
        LLPointer<LLModel> model = riggedSphere(8, 4);
        model->mSkinInfo.fromLLSD(skin);
        const LLMeshSkinInfo& written = model->mSkinInfo;

        // Render batches are keyed by the hash of the joint names, their
        // numbers and the inverse bind matrices to four decimal places.
        CHECK(written.mHash == 0x973f7b5efcbd0155ULL);

        std::ostringstream ostr;
        LLModel::writeModel(ostr, nullptr, model, model, model, model, LLModel::Decomposition(), true, true, true);
        LLPointer<LLModel> back = new LLModel(meshParams(), (F32)LLModel::LOD_HIGH);
        std::istringstream is(ostr.str());
        REQUIRE(back->loadModel(is));
        const LLMeshSkinInfo& read = back->mSkinInfo;
        CHECK(read.mJointNames == written.mJointNames);
        CHECK(sameMatrices(read.mInvBindMatrix, written.mInvBindMatrix));
        CHECK(sameMatrices(read.mAlternateBindMatrix, written.mAlternateBindMatrix));
        CHECK(sameMatrices({ read.mBindShapeMatrix }, { written.mBindShapeMatrix }));
        CHECK(read.mPelvisOffset == written.mPelvisOffset);
        CHECK(read.mLockScaleIfJointPosition);
        CHECK(read.mHash == written.mHash);
    }

    TEST_CASE("llmodel: an slm keeps the box its model was normalized from", "[llmodel]")
    {
        // The uploader's pivot finds the file's origin within the model's box
        // from these two, so a model read back from an .slm needs them too.
        LLPointer<LLModel> model = riggedSphere(8, 4);
        model->mNormalizedScale.set(2.5f, 0.75f, 4.f);
        model->mNormalizedTranslation.set(0.5f, -0.125f, 2.f);

        auto load = [&model](bool as_slm)
        {
            std::ostringstream ostr;
            LLModel::writeModel(ostr, nullptr, model, model, model, model, LLModel::Decomposition(), false, false, false,
                                LLModel::WRITE_BINARY, as_slm);
            LLPointer<LLModel> back = new LLModel(meshParams(), (F32)LLModel::LOD_HIGH);
            std::istringstream is(ostr.str());
            REQUIRE(back->loadModel(is));
            return back;
        };

        LLPointer<LLModel> slm = load(true);
        CHECK(slm->mNormalizedScale == model->mNormalizedScale);
        CHECK(slm->mNormalizedTranslation == model->mNormalizedTranslation);

        // An asset written for an upload carries neither.
        LLPointer<LLModel> asset = load(false);
        CHECK(asset->mNormalizedScale == LLVector3(1.f, 1.f, 1.f));
        CHECK(asset->mNormalizedTranslation == LLVector3::zero);
    }
}

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
#include "llsdserialize.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <string>
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

    TEST_CASE("llmodel: an asset whose blocks zip_llsd_model zipped holds what zip_llsd's does", "[llmodel]")
    {
        LLPointer<LLModel> model = riggedSphere(48, 24);
        LLModel::Decomposition decomp;
        decomp.mBaseHull = { LLVector3(-0.5f, -0.5f, -0.5f), LLVector3(0.5f, -0.5f, -0.5f), LLVector3(0.f, 0.5f, -0.5f),
                             LLVector3(0.f, 0.f, 0.5f) };

        auto write = [&](LLModel::EBlockZip block_zip)
        {
            std::ostringstream ostr;
            WrittenAsset asset;
            asset.header = LLModel::writeModel(ostr, nullptr, model, model, model, model, decomp, true, false, false, LLModel::WRITE_BINARY,
                                               false, 0, block_zip);
            asset.stream = ostr.str();
            std::ostringstream header;
            LLSDSerialize::toBinary(asset.header, header);
            asset.header_bytes = header.str().size();
            return asset;
        };
        const WrittenAsset by_zlib = write(LLModel::ZIP_LLSD);
        const WrittenAsset by_model = write(LLModel::ZIP_LLSD_MODEL);

        const char* const lods[] = { "lowest_lod", "low_lod", "medium_lod", "high_lod" };
        const char* const blocks[] = { "skin", "physics_convex", "lowest_lod", "low_lod", "medium_lod", "high_lod" };
        for (const WrittenAsset* asset : { &by_zlib, &by_model })
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

        // Each block unzips to the same value either way, and is what its
        // own zip makes of that value.
        for (const char* name : blocks)
        {
            CAPTURE(name);
            const std::string zlib_block = by_zlib.block(name);
            const std::string model_block = by_model.block(name);
            LLSD zlib_value, model_value;
            REQUIRE(LLUZipHelper::unzip_llsd(zlib_value, (const U8*)zlib_block.data(), (S32)zlib_block.size()) == LLUZipHelper::ZR_OK);
            REQUIRE(LLUZipHelper::unzip_llsd(model_value, (const U8*)model_block.data(), (S32)model_block.size()) == LLUZipHelper::ZR_OK);
            CHECK(zip_llsd(zlib_value) == zlib_block);
            CHECK(zip_llsd_model(model_value) == model_block);
            std::ostringstream zlib_binary, model_binary;
            LLSDSerialize::toBinary(zlib_value, zlib_binary);
            LLSDSerialize::toBinary(model_value, model_binary);
            CHECK(zlib_binary.str() == model_binary.str());
        }

        // The LOD blocks, which the download weight charges, are smaller.
        S64 zlib_lod_bytes = 0, model_lod_bytes = 0;
        for (const char* name : lods)
        {
            zlib_lod_bytes += by_zlib.header[name]["size"].asInteger();
            model_lod_bytes += by_model.header[name]["size"].asInteger();
        }
        INFO("LOD blocks: zip_llsd_model " << model_lod_bytes << " bytes, zip_llsd " << zlib_lod_bytes);
        CHECK(model_lod_bytes < zlib_lod_bytes);

        // Read back as the mesh repository reads an asset, the high LOD has
        // the same faces either way.
        auto load = [](const WrittenAsset& asset)
        {
            LLPointer<LLModel> back = new LLModel(meshParams(), (F32)LLModel::LOD_HIGH);
            std::istringstream is(asset.stream);
            REQUIRE(back->loadModel(is));
            return back;
        };
        LLPointer<LLModel> zlib_back = load(by_zlib);
        LLPointer<LLModel> model_back = load(by_model);
        REQUIRE(zlib_back->getNumVolumeFaces() == 1);
        REQUIRE(model_back->getNumVolumeFaces() == 1);
        const LLVolumeFace& zlib_face = zlib_back->getVolumeFace(0);
        const LLVolumeFace& model_face = model_back->getVolumeFace(0);
        REQUIRE(zlib_face.mNumVertices > 0);
        REQUIRE(model_face.mNumVertices == zlib_face.mNumVertices);
        REQUIRE(model_face.mNumIndices == zlib_face.mNumIndices);
        CHECK(std::equal(model_face.mIndices, model_face.mIndices + model_face.mNumIndices, zlib_face.mIndices));
        bool positions_match = true;
        for (S32 v = 0; v < model_face.mNumVertices; ++v)
        {
            positions_match &= std::equal(model_face.mPositions[v].getF32ptr(), model_face.mPositions[v].getF32ptr() + 3,
                                          zlib_face.mPositions[v].getF32ptr());
        }
        CHECK(positions_match);
        CHECK(model_back->mSkinWeights.size() == zlib_back->mSkinWeights.size());
    }
}

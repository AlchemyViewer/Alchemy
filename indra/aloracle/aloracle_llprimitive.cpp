/**
 * @file aloracle_llprimitive.cpp
 * @brief aloracle's functions over llprimitive's texture entries, shapes,
 * extra parameters and texture animations.
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

#include "aloracleboundary.h"

#include "lldatapacker.h"
#include "llmaterialid.h"
#include "llprimitive.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "lltextureanim.h"
#include "lltextureentry.h"
#include "llvolume.h"
#include "llvolumemessage.h"
#include "message.h"

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // The bytes of a field as a data packer holds a binary one: its length,
    // then the bytes, which is how the compressed update carries a texture
    // entry and a texture animation.
    std::vector<U8> lengthThen(const std::string& bytes)
    {
        std::vector<U8> field(4 + bytes.size());
        const S32 size = (S32)bytes.size();
        htolememcpy(field.data(), &size, MVT_S32, 4);
        if (!bytes.empty())
        {
            std::memcpy(field.data() + 4, bytes.data(), bytes.size());
        }
        return field;
    }

    // An entry's length must fit an S32, and the field its length too.
    bool fitsField(size_t size)
    {
        return size <= (size_t)std::numeric_limits<S32>::max() - 4;
    }

    std::string_view viewOf(const std::vector<U8>& bytes, size_t from, size_t to)
    {
        return std::string_view(reinterpret_cast<const char*>(bytes.data()) + from, to - from);
    }

    LLSD faceOf(const LLTextureEntry& te)
    {
        LLSD face;
        face["image"] = te.getID();
        face["color"] = ll_sd_from_color4(te.getColor());
        face["scale_s"] = te.getScaleS();
        face["scale_t"] = te.getScaleT();
        face["offset_s"] = te.getOffsetS();
        face["offset_t"] = te.getOffsetT();
        face["rotation"] = te.getRotation();
        face["bump"] = (S32)te.getBumpShinyFullbright();
        face["media_tex_gen"] = (S32)te.getMediaTexGen();
        face["glow"] = te.getGlow();
        face["material_id"] = te.getMaterialID().asUUID();
        return face;
    }

    void setFace(LLPrimitive& primitive, U8 index, const LLSD& face)
    {
        primitive.setTETexture(index, face["image"].asUUID());
        primitive.setTEColor(index, ll_color4_from_sd(face["color"]));
        primitive.setTEScale(index, (F32)face["scale_s"].asReal(), (F32)face["scale_t"].asReal());
        primitive.setTEOffset(index, (F32)face["offset_s"].asReal(), (F32)face["offset_t"].asReal());
        primitive.setTERotation(index, (F32)face["rotation"].asReal());
        primitive.setTEBumpShinyFullbright(index, (U8)face["bump"].asInteger());
        primitive.setTEMediaTexGen(index, (U8)face["media_tex_gen"].asInteger());
        primitive.setTEGlow(index, (F32)face["glow"].asReal());
        primitive.setTEMaterialID(index, LLMaterialID(face["material_id"].asUUID()));
    }

    // The most faces packTEMessage(LLDataPacker&) packs, each differing in
    // every field, inside its 4096 bytes: 45 such faces take 4166.
    constexpr size_t MAX_PACKED_FACES = 44;

    LLSD shapeOf(const LLVolumeParams& params, bool ok)
    {
        const LLProfileParams& profile = params.getProfileParams();
        const LLPathParams& path = params.getPathParams();

        LLSD shape;
        shape["ok"] = ok;
        shape["profile"]["curve"] = (S32)profile.getCurveType();
        shape["profile"]["begin"] = profile.getBegin();
        shape["profile"]["end"] = profile.getEnd();
        shape["profile"]["hollow"] = profile.getHollow();
        shape["path"]["curve"] = (S32)path.getCurveType();
        shape["path"]["begin"] = path.getBegin();
        shape["path"]["end"] = path.getEnd();
        shape["path"]["scale_x"] = path.getScaleX();
        shape["path"]["scale_y"] = path.getScaleY();
        shape["path"]["shear_x"] = path.getShearX();
        shape["path"]["shear_y"] = path.getShearY();
        shape["path"]["twist_begin"] = path.getTwistBegin();
        shape["path"]["twist_end"] = path.getTwistEnd();
        shape["path"]["radius_offset"] = path.getRadiusOffset();
        shape["path"]["taper_x"] = path.getTaperX();
        shape["path"]["taper_y"] = path.getTaperY();
        shape["path"]["revolutions"] = path.getRevolutions();
        shape["path"]["skew"] = path.getSkew();
        return shape;
    }

    constexpr size_t SHAPE_SIZE = 23;

    std::unique_ptr<LLNetworkData> newParameter(U16 type)
    {
        switch (type)
        {
        case LLNetworkData::PARAMS_FLEXIBLE:
            return std::make_unique<LLFlexibleObjectData>();
        case LLNetworkData::PARAMS_LIGHT:
            return std::make_unique<LLLightParams>();
        case LLNetworkData::PARAMS_SCULPT:
            return std::make_unique<LLSculptParams>();
        case LLNetworkData::PARAMS_LIGHT_IMAGE:
            return std::make_unique<LLLightImageParams>();
        case LLNetworkData::PARAMS_EXTENDED_MESH:
            return std::make_unique<LLExtendedMeshParams>();
        case LLNetworkData::PARAMS_RENDER_MATERIAL:
            return std::make_unique<LLRenderMaterialParams>();
        case LLNetworkData::PARAMS_REFLECTION_PROBE:
            return std::make_unique<LLReflectionProbeParams>();
        default:
            return nullptr;
        }
    }

    // The bytes a type's unpack reads into locals it leaves uninitialized
    // where they do not come: the rest it reads into members, which keep the
    // constructor's values.
    size_t unpackedSize(U16 type)
    {
        switch (type)
        {
        case LLNetworkData::PARAMS_FLEXIBLE:
            return 4; // the four bytes; the force only where bytes are left
        case LLNetworkData::PARAMS_LIGHT:
            return 16;
        case LLNetworkData::PARAMS_SCULPT:
            return 17;
        case LLNetworkData::PARAMS_RENDER_MATERIAL:
            return 1; // the count; the entries are value-initialized
        case LLNetworkData::PARAMS_REFLECTION_PROBE:
            return 8; // ambiance and clip distance; the flags are a member
        default:
            return 0;
        }
    }

    // What pack writes, given room for any parameter: a render material
    // writes 17 bytes an entry after its count, however many it has.
    std::string packed(const LLNetworkData& parameter, size_t entries)
    {
        std::vector<U8> buffer(64 + 17 * entries);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        parameter.pack(dp);
        return std::string(viewOf(buffer, 0, dp.getCurrentSize()));
    }

    LLSD describe(const LLNetworkData& parameter)
    {
        switch (parameter.mType)
        {
        case LLNetworkData::PARAMS_FLEXIBLE:
            return static_cast<const LLFlexibleObjectData&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_LIGHT:
            return static_cast<const LLLightParams&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_SCULPT:
            return static_cast<const LLSculptParams&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_LIGHT_IMAGE:
            return static_cast<const LLLightImageParams&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_EXTENDED_MESH:
            return static_cast<const LLExtendedMeshParams&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_REFLECTION_PROBE:
            return static_cast<const LLReflectionProbeParams&>(parameter).asLLSD();
        case LLNetworkData::PARAMS_RENDER_MATERIAL:
            break;
        }

        // Its entries are its own, and pack is the one way to see them all:
        // every entry after the count, in order, whatever the count says.
        const std::string bytes = packed(parameter, 256);
        LLSD entries = LLSD::emptyArray();
        for (size_t at = 1; at + 17 <= bytes.size(); at += 17)
        {
            LLUUID id;
            std::memcpy(id.mData, bytes.data() + at + 1, 16);
            LLSD entry;
            entry["te_idx"] = (S32)(U8)bytes[at];
            entry["id"] = id;
            entries.append(entry);
        }
        LLSD value;
        value["entries"] = entries;
        return value;
    }

    bool fromValue(LLNetworkData& parameter, LLSD& value)
    {
        switch (parameter.mType)
        {
        case LLNetworkData::PARAMS_FLEXIBLE:
            return static_cast<LLFlexibleObjectData&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_LIGHT:
            return static_cast<LLLightParams&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_SCULPT:
            return static_cast<LLSculptParams&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_LIGHT_IMAGE:
            return static_cast<LLLightImageParams&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_EXTENDED_MESH:
            return static_cast<LLExtendedMeshParams&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_REFLECTION_PROBE:
            return static_cast<LLReflectionProbeParams&>(parameter).fromLLSD(value);
        case LLNetworkData::PARAMS_RENDER_MATERIAL:
            break;
        }

        const LLSD& entries = value["entries"];
        if (!entries.isArray())
        {
            return false;
        }
        auto& material = static_cast<LLRenderMaterialParams&>(parameter);
        for (const LLSD& entry : llsd::inArray(entries))
        {
            material.setMaterial((U8)entry["te_idx"].asInteger(), entry["id"].asUUID());
        }
        return true;
    }

    // Whether LLProfileParams' setters can cut a value to five places, which
    // they do through an int: where 100000 times it is within an int's range.
    bool cutsThroughInt(F32 value)
    {
        const double scaled = (double)value * 100000.0;
        return scaled > (double)std::numeric_limits<int>::min() - 1.0
            && scaled < (double)std::numeric_limits<int>::max() + 1.0;
    }
}

int32_t aloracle_texture_entry_unpack(const uint8_t* entry, size_t size, uint32_t face_count, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || face_count == 0 || face_count > LLTEContents::MAX_TES || !fitsField(size)
            || !aloracle::take(entry, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::vector<U8> field = lengthThen(bytes);
        LLDataPackerBinaryBuffer dp(field.data(), (S32)field.size());

        LLPrimitive primitive;
        primitive.setNumTEs((U8)face_count);
        for (U8 face = 0; face < face_count; ++face)
        {
            primitive.setTEGlow(face, 2.f);
        }
        const S32 result = primitive.unpackTEMessage(dp);

        LLSD faces = LLSD::emptyArray();
        for (U8 face = 0; face < face_count; ++face)
        {
            faces.append(faceOf(*primitive.getTE(face)));
        }
        LLSD out;
        out["result"] = result;
        out["faces"] = faces;
        return aloracle::giveValue(out, value);
    });
}

int32_t aloracle_texture_entry_pack(const uint8_t* faces, size_t size, aloracle_bytes* entry)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD list;
        if (!aloracle::clear(entry) || !aloracle::takeValue(faces, size, list) || !list.isArray()
            || list.size() > MAX_PACKED_FACES)
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLPrimitive primitive;
        primitive.setNumTEs((U8)list.size());
        for (U8 face = 0; face < list.size(); ++face)
        {
            setFace(primitive, face, list[face]);
        }

        std::vector<U8> buffer(4 + LLTEContents::MAX_TE_BUFFER);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        primitive.packTEMessage(dp);
        return aloracle::give(viewOf(buffer, 4, dp.getCurrentSize()), entry);
    });
}

int32_t aloracle_volume_params_unpack(const uint8_t* shape, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || size != SHAPE_SIZE || !aloracle::take(shape, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLDataPackerBinaryBuffer dp(reinterpret_cast<U8*>(bytes.data()), (S32)bytes.size());
        LLVolumeParams params;
        const bool ok = LLVolumeMessage::unpackVolumeParams(&params, dp);
        return aloracle::giveValue(shapeOf(params, ok), value);
    });
}

int32_t aloracle_volume_params_pack(const uint8_t* params, size_t size, aloracle_bytes* shape)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD sd;
        if (!aloracle::clear(shape) || !aloracle::takeValue(params, size, sd) || !sd.isMap())
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        const LLSD& p = sd["profile"];
        const F32 begin = (F32)p["begin"].asReal();
        const F32 end = (F32)p["end"].asReal();
        const F32 hollow = (F32)p["hollow"].asReal();
        // setBegin takes 1 and over, and setEnd 0 and under, without a cut.
        if (!(begin >= 1.0f || cutsThroughInt(begin)) || !(end <= 0.0f || cutsThroughInt(end))
            || !cutsThroughInt(hollow))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLVolumeParams volume;
        LLProfileParams& profile = volume.getProfileParams();
        profile.setCurveType((U8)p["curve"].asInteger());
        profile.setBegin(begin);
        profile.setEnd(end);
        profile.setHollow(hollow);

        LLPathParams& path = volume.getPathParams();
        const LLSD& q = sd["path"];
        path.setCurveType((U8)q["curve"].asInteger());
        path.setBegin((F32)q["begin"].asReal());
        path.setEnd((F32)q["end"].asReal());
        path.setScale((F32)q["scale_x"].asReal(), (F32)q["scale_y"].asReal());
        path.setShear((F32)q["shear_x"].asReal(), (F32)q["shear_y"].asReal());
        path.setTwistBegin((F32)q["twist_begin"].asReal());
        path.setTwistEnd((F32)q["twist_end"].asReal());
        path.setRadiusOffset((F32)q["radius_offset"].asReal());
        path.setTaper((F32)q["taper_x"].asReal(), (F32)q["taper_y"].asReal());
        path.setRevolutions((F32)q["revolutions"].asReal());
        path.setSkew((F32)q["skew"].asReal());

        std::vector<U8> buffer(SHAPE_SIZE);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        LLVolumeMessage::packVolumeParams(&volume, dp);
        return aloracle::give(viewOf(buffer, 0, dp.getCurrentSize()), shape);
    });
}

int32_t aloracle_extra_param_unpack(uint16_t type, const uint8_t* data, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        if (LLNetworkData::PARAMS_MESH == type)
        {
            type = LLNetworkData::PARAMS_SCULPT;
        }
        std::string bytes;
        std::unique_ptr<LLNetworkData> parameter = newParameter(type);
        if (!aloracle::clear(value) || !parameter || size < unpackedSize(type)
            || size > (size_t)std::numeric_limits<S32>::max() || !aloracle::take(data, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLDataPackerBinaryBuffer dp(reinterpret_cast<U8*>(bytes.data()), (S32)bytes.size());
        parameter->unpack(dp);
        return aloracle::giveValue(describe(*parameter), value);
    });
}

int32_t aloracle_extra_param_pack(uint16_t type, const uint8_t* value, size_t size, aloracle_bytes* data)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD sd;
        std::unique_ptr<LLNetworkData> parameter = newParameter(type);
        if (!aloracle::clear(data) || !parameter || !aloracle::takeValue(value, size, sd) || !sd.isMap()
            || !fromValue(*parameter, sd))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::give(packed(*parameter, sd["entries"].size()), data);
    });
}

int32_t aloracle_texture_anim_unpack(const uint8_t* data, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || !fitsField(size) || !aloracle::take(data, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::vector<U8> field = lengthThen(bytes);
        LLDataPackerBinaryBuffer dp(field.data(), (S32)field.size());
        LLTextureAnim anim;
        anim.unpackTAMessage(dp);
        return aloracle::giveValue(anim.asLLSD(), value);
    });
}

int32_t aloracle_texture_anim_pack(const uint8_t* value, size_t size, aloracle_bytes* data)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD sd;
        LLTextureAnim anim;
        if (!aloracle::clear(data) || !aloracle::takeValue(value, size, sd) || !sd.isMap() || !anim.fromLLSD(sd))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::vector<U8> buffer(4 + 16);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        anim.packTAMessage(dp);
        return aloracle::give(viewOf(buffer, 4, dp.getCurrentSize()), data);
    });
}

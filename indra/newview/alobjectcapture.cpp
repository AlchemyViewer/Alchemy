/**
 * @file alobjectcapture.cpp
 * @brief The session recording's start and its snapshots of objects.
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

#include "llviewerprecompiledheaders.h"

// Compiled out of Release, with every hook that calls it.
#if AL_SESSION_CAPTURE

#include "alobjectcapture.h"

#include "alsessioncapture.h"
#include "llcallbacklist.h"
#include "llframetimer.h"
#include "llgltfmaterial.h"
#include "llnamevalue.h"
#include "llpartdata.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llstartup.h"
#include "llsys.h"
#include "lltextureentry.h"
#include "llversioninfo.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerpartsource.h"
#include "llviewerregion.h"
#include "llviewertextureanim.h"
#include "llvovolume.h"
#include "llworld.h"

#include <map>

namespace
{
    // Seconds between snapshots once logged in, and when the next is due.
    F64 sInterval = 30.0;
    F64 sNextSnapshot = 0.0;

    LLSD describeParticles(const LLPartSysData& system)
    {
        const LLPartData& part = system.mPartData;

        LLSD value;
        value["crc"] = (S32)system.mCRC;
        value["flags"] = (S32)system.mFlags;
        value["pattern"] = (S32)system.mPattern;
        value["max_age"] = system.mMaxAge;
        value["start_age"] = system.mStartAge;
        value["inner_angle"] = system.mInnerAngle;
        value["outer_angle"] = system.mOuterAngle;
        value["burst_rate"] = system.mBurstRate;
        value["burst_radius"] = system.mBurstRadius;
        value["burst_speed_min"] = system.mBurstSpeedMin;
        value["burst_speed_max"] = system.mBurstSpeedMax;
        value["burst_part_count"] = (S32)system.mBurstPartCount;
        value["angular_velocity"] = system.mAngularVelocity.getValue();
        value["accel"] = system.mPartAccel.getValue();
        value["image"] = system.mPartImageID;
        value["target"] = system.mTargetUUID;
        value["part"]["flags"] = (S32)part.mFlags;
        value["part"]["max_age"] = part.mMaxAge;
        value["part"]["start_color"] = ll_sd_from_color4(part.mStartColor);
        value["part"]["end_color"] = ll_sd_from_color4(part.mEndColor);
        value["part"]["start_scale"] = part.mStartScale.getValue();
        value["part"]["end_scale"] = part.mEndScale.getValue();
        value["part"]["start_glow"] = part.mStartGlow;
        value["part"]["end_glow"] = part.mEndGlow;
        value["part"]["blend_source"] = (S32)part.mBlendFuncSource;
        value["part"]["blend_dest"] = (S32)part.mBlendFuncDest;
        return value;
    }
}

void ALObjectCapture::start()
{
    LLSD viewer;
    viewer["channel"] = LLVersionInfo::instance().getChannel();
    viewer["version"] = LLVersionInfo::instance().getVersion();
    viewer["platform"] = LLOSInfo::instance().getOSStringSimple();
    ALSessionCapture::startFromEnvironment(viewer);
    if (!ALSessionCapture::capturing())
    {
        return;
    }

    const std::string interval = LLStringUtil::getenv("ALCHEMY_SESSION_CAPTURE_INTERVAL");
    F64 seconds = 0.0;
    if (!interval.empty() && LLStringUtil::convertToF64(interval, seconds))
    {
        sInterval = seconds;
    }
    if (sInterval > 0.0)
    {
        gIdleCallbacks.addFunction(onIdle);
    }
}

void ALObjectCapture::onIdle(void*)
{
    if (LLStartUp::getStartupState() < STATE_STARTED)
    {
        sNextSnapshot = 0.0;
        return;
    }
    const F64 now = LLFrameTimer::getTotalSeconds();
    if (sNextSnapshot == 0.0)
    {
        sNextSnapshot = now + sInterval;
    }
    else if (now >= sNextSnapshot)
    {
        snapshot("interval");
        sNextSnapshot = now + sInterval;
    }
}

void ALObjectCapture::snapshot(const std::string& reason)
{
    if (!ALSessionCapture::capturing() || !LLWorld::instanceExists())
    {
        return;
    }

    std::map<const LLViewerRegion*, LLSD> objects;
    for (S32 index = 0; index < gObjectList.getNumObjects(); ++index)
    {
        const LLViewerObject* object = gObjectList.getObject(index);
        if (object && !object->isDead() && object->getRegion())
        {
            objects[object->getRegion()].append(describe(*object));
        }
    }

    for (const LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
    {
        LLSD record;
        record["reason"] = reason;
        record["region"]["name"] = region->getName();
        record["region"]["id"] = region->getRegionID();
        record["region"]["host"] = region->getHost().getIPandPort();
        record["region"]["handle"] = ll_sd_from_U64(region->getHandle());
        const auto found = objects.find(region);
        record["objects"] = found != objects.end() ? found->second : LLSD::emptyArray();
        ALSessionCapture::instance().record("objects", record);
    }
    ALSessionCapture::instance().flush();
}

LLSD ALObjectCapture::describe(const LLViewerObject& object)
{
    LLSD value;
    value["id"] = object.getID();
    value["local_id"] = (S32)object.getLocalID();
    value["pcode"] = (S32)object.getPCode();
    if (const LLViewerObject* parent = static_cast<const LLViewerObject*>(object.getParent()))
    {
        value["parent_id"] = parent->getID();
        value["parent_local_id"] = (S32)parent->getLocalID();
    }
    value["position"] = object.getPosition().getValue();
    value["rotation"] = object.getRotation().getValue();
    value["scale"] = object.getScale().getValue();
    value["velocity"] = object.getVelocity().getValue();
    value["angular_velocity"] = object.getAngularVelocity().getValue();
    value["acceleration"] = object.getAcceleration().getValue();
    value["flags"] = (S32)object.mFlags;
    if (const LLVolume* volume = object.getVolume())
    {
        value["volume"] = volume->getParams().asLLSD();
    }

    LLSD textures = LLSD::emptyArray();
    LLSD overrides = LLSD::emptyArray();
    for (U8 face = 0; face < object.getNumTEs(); ++face)
    {
        const LLTextureEntry* entry = object.getTE(face);
        LLSD override_data;
        if (entry && entry->getGLTFMaterialOverride())
        {
            LLGLTFMaterial::sDefault.getOverrideLLSD(*entry->getGLTFMaterialOverride(), override_data);
        }
        textures.append(entry ? entry->asLLSD() : LLSD());
        overrides.append(override_data);
    }
    value["textures"] = textures;
    value["material_overrides"] = overrides;

    LLSD extra_params = LLSD::emptyMap();
    if (const LLFlexibleObjectData* flexible = object.getFlexibleObjectData())
    {
        extra_params["flexible"] = flexible->asLLSD();
    }
    if (const LLLightParams* light = object.getLightParams())
    {
        extra_params["light"] = light->asLLSD();
    }
    if (const LLSculptParams* sculpt = object.getSculptParams())
    {
        extra_params["sculpt"] = sculpt->asLLSD();
    }
    if (const LLLightImageParams* light_image = object.getLightImageParams())
    {
        extra_params["light_image"] = light_image->asLLSD();
    }
    if (const LLExtendedMeshParams* extended_mesh = object.getExtendedMeshParams())
    {
        extra_params["extended_mesh"] = extended_mesh->asLLSD();
    }
    if (const LLReflectionProbeParams* probe = object.getReflectionProbeParams())
    {
        extra_params["reflection_probe"] = probe->asLLSD();
    }
    if (const LLRenderMaterialParams* materials = object.getRenderMaterialParams())
    {
        // Its entries are kept by face, a byte.
        LLSD entries = LLSD::emptyArray();
        for (S32 te = 0; te <= 255; ++te)
        {
            const LLUUID& id = materials->getMaterial((U8)te);
            if (id.notNull())
            {
                LLSD entry;
                entry["te"] = te;
                entry["id"] = id;
                entries.append(entry);
            }
        }
        extra_params["render_material"] = entries;
    }
    value["extra_params"] = extra_params;

    LLSD name_values = LLSD::emptyArray();
    for (const auto& [name, name_value] : object.mNameValuePairs)
    {
        if (name_value)
        {
            name_values.append(name_value->printNameValue());
        }
    }
    value["name_values"] = name_values;

    value["text"] = object.mHudText;
    value["text_color"] = ll_sd_from_color4(object.mHudTextColor);
    if (object.mPartSourcep)
    {
        value["particles"] = describeParticles(object.mPartSourcep->mPartSysData);
    }
    const LLVOVolume* volume = dynamic_cast<const LLVOVolume*>(&object);
    if (volume && volume->mTextureAnimp)
    {
        value["texture_anim"] = volume->mTextureAnimp->asLLSD();
    }
    return value;
}

#endif // AL_SESSION_CAPTURE

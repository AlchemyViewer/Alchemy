/**
 * @file alobjectproperties.cpp
 * @brief Shared per-object metric record for the Scene Explorer, Inspect and detail views
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "llviewerprecompiledheaders.h"

#include "alobjectproperties.h"

#include "message.h"

#include "llagent.h"
#include "llprimitive.h"
#include "llselectmgr.h"
#include "lltextureentry.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llvovolume.h"

namespace ALObjectProperties
{

void fillFromObject(Record& rec, LLViewerObject* obj)
{
    if (!obj)
        return;

    rec.mLocalId = obj->getLocalID();
    if (LLViewerRegion* regionp = obj->getRegion())
    {
        rec.mRegionHandle = regionp->getHandle();
    }

    // Spatial
    rec.mPosRegion = obj->getPositionRegion();
    rec.mPosGlobal = obj->getPositionGlobal();
    rec.mScale     = obj->getScale();
    rec.mDistance  = (F32)(rec.mPosGlobal - gAgent.getPositionGlobal()).magVec();

    // Structure / geometry
    rec.mPrimCount   = obj->numChildren() + 1;
    rec.mNumFaces    = obj->getNumFaces();
    rec.mNumVertices = obj->getNumVertices();
    rec.mNumTriangles = obj->getNumIndices() / 3;

    U32 flags = FLAG_NONE;

    if (obj->asAvatar())
    {
        rec.mGeom = GEOM_AVATAR;
        flags |= FLAG_AVATAR;
    }
    else if (obj->isMesh())
    {
        rec.mGeom = GEOM_MESH;
    }
    else if (obj->isSculpted())
    {
        rec.mGeom = GEOM_SCULPT;
    }
    else
    {
        rec.mGeom = GEOM_PRIM;
    }

    if (obj->flagScripted())        flags |= FLAG_SCRIPTED;
    if (obj->flagUsePhysics())      flags |= FLAG_PHYSICS;
    if (obj->flagPhantom())         flags |= FLAG_PHANTOM;
    if (obj->flagTemporaryOnRez())  flags |= FLAG_TEMPORARY;
    if (obj->isFlexible())          flags |= FLAG_FLEXIBLE;
    if (obj->hasLightTexture())     flags |= FLAG_PROJECTOR;
    if (obj->isReflectionProbe())   flags |= FLAG_REFLECTION_PROBE;
    if (obj->isParticleSource())    flags |= FLAG_PARTICLES;
    if (obj->isAttachment())        flags |= FLAG_ATTACHMENT;
    if (obj->isAnimatedObject())    flags |= FLAG_ANIMATED;
    if (obj->hasRenderMaterialParams()) flags |= FLAG_PBR_MATERIAL;

    // Volume-only features.
    if (obj->getPCode() == LL_PCODE_VOLUME)
    {
        LLVOVolume* vobj = static_cast<LLVOVolume*>(obj);

        if (vobj->getIsLight())
        {
            flags |= FLAG_LIGHT;
            if (vobj->isLightSpotlight())
                flags |= FLAG_SPOTLIGHT;
        }
        if (vobj->hasMedia())
            flags |= FLAG_MEDIA;

        LLVOVolume::texture_cost_t textures;
        rec.mRenderCost = (F32)vobj->getRenderCost(textures);
    }

    // Per-face appearance flags.
    const U8 num_tes = obj->getNumTEs();
    for (U8 i = 0; i < num_tes; ++i)
    {
        const LLTextureEntry* te = obj->getTE(i);
        if (!te)
            continue;
        if (te->getFullbright())
            flags |= FLAG_FULLBRIGHT;
        if (te->getGlow() > 0.f)
            flags |= FLAG_GLOW;
        if (te->getColor().mV[VALPHA] < 0.999f)
            flags |= FLAG_ALPHA;
    }

    rec.mFlags = flags;

    // Cost (cached values; trigger async refresh as a side effect).
    rec.mObjectCost    = obj->getObjectCost();
    rec.mLandImpact    = obj->getLinksetCost();
    rec.mPhysicsCost   = obj->getPhysicsCost();
    rec.mStreamingCost = obj->getStreamingCost();
}

Record fromObject(LLViewerObject* obj)
{
    Record rec;
    if (obj)
    {
        rec.mId = obj->getID();
        fillFromObject(rec, obj);
    }
    return rec;
}

std::string flagsToString(U32 flags)
{
    static const std::pair<EFlag, const char*> labels[] = {
        { FLAG_SCRIPTED,         "scripted"   },
        { FLAG_PHYSICS,          "physics"    },
        { FLAG_PHANTOM,          "phantom"    },
        { FLAG_TEMPORARY,        "temporary"  },
        { FLAG_FLEXIBLE,         "flexi"      },
        { FLAG_LIGHT,            "light"      },
        { FLAG_SPOTLIGHT,        "spotlight"  },
        { FLAG_PROJECTOR,        "projector"  },
        { FLAG_GLOW,             "glow"       },
        { FLAG_FULLBRIGHT,       "fullbright" },
        { FLAG_MEDIA,            "media"      },
        { FLAG_PARTICLES,        "particles"  },
        { FLAG_REFLECTION_PROBE, "probe"      },
        { FLAG_ANIMATED,         "animated"   },
        { FLAG_ATTACHMENT,       "attachment" },
        { FLAG_PBR_MATERIAL,     "PBR"        },
        { FLAG_ALPHA,            "alpha"      },
    };

    std::string out;
    for (const auto& [bit, name] : labels)
    {
        if (flags & bit)
        {
            if (!out.empty())
                out += ", ";
            out += name;
        }
    }
    return out;
}

const std::string& iconName(const Record& rec)
{
    static const std::string s_object("Inv_Object");
    static const std::string s_multi("Inv_Object_Multi");
    static const std::string s_mesh("Inv_Mesh");
    static const std::string s_avatar("Generic_Person");

    if (rec.mGeom == GEOM_AVATAR)
        return s_avatar;
    // Multi-prim linksets read as "multi" regardless of geometry; single prims
    // split mesh vs everything else.
    if (rec.mPrimCount > 1)
        return s_multi;
    if (rec.mGeom == GEOM_MESH)
        return s_mesh;
    return s_object;
}

} // namespace ALObjectProperties

// ============================================================================
// ALObjectPropertiesCache
// ============================================================================
ALObjectPropertiesCache::ALObjectPropertiesCache()
{
}

ALObjectPropertiesCache::~ALObjectPropertiesCache()
{
}

const ALObjectPropertiesCache::ServerProps* ALObjectPropertiesCache::get(const LLUUID& id) const
{
    auto it = mCache.find(id);
    return (it != mCache.end()) ? &it->second : nullptr;
}

ALObjectPropertiesCache::ServerProps& ALObjectPropertiesCache::lookupOrCreate(const LLUUID& id)
{
    auto found = mCache.find(id);
    if (found != mCache.end())
        return found->second;

    ServerProps& p = mCache[id];
    mCacheOrder.push_back(id);
    trimCache(); // evicts from the front, never the entry just added
    return p;
}

void ALObjectPropertiesCache::trimCache()
{
    while (mCache.size() > MAX_CACHE_ENTRIES && !mCacheOrder.empty())
    {
        mCache.erase(mCacheOrder.front());
        mCacheOrder.pop_front();
    }
}

// static
void ALObjectPropertiesCache::processObjectPropertiesFamily(LLMessageSystem* msg, void** user_data)
{
    LLUUID id;
    msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, id);
    if (id.notNull())
    {
        ALObjectPropertiesCache& self = instance();
        ServerProps& p = self.lookupOrCreate(id);
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, p.mOwnerId);
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, p.mGroupId);
        msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Name, p.mName);
        msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Description, p.mDescription);
        p.mGroupOwned = p.mOwnerId.isNull() && p.mGroupId.notNull();
        self.mChangeSignal(id);
    }

    // Preserve normal hover/selection behaviour.
    LLSelectMgr::processObjectPropertiesFamily(msg, user_data);
}

// static
void ALObjectPropertiesCache::processObjectProperties(LLMessageSystem* msg, void** user_data)
{
    ALObjectPropertiesCache& self = instance();
    const S32 count = msg->getNumberOfBlocksFast(_PREHASH_ObjectData);
    uuid_vec_t ids;
    ids.reserve(count);
    for (S32 i = 0; i < count; ++i)
    {
        LLUUID id;
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, id, i);
        if (id.isNull())
            continue;

        ServerProps& p = self.lookupOrCreate(id);
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_CreatorID, p.mCreatorId, i);
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, p.mOwnerId, i);
        msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, p.mGroupId, i);
        U64 creation_date = 0;
        msg->getU64Fast(_PREHASH_ObjectData, _PREHASH_CreationDate, creation_date, i);
        p.mCreationDate = (time_t)(creation_date / 1000000);
        msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_BaseMask, p.mBaseMask, i);
        msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_OwnerMask, p.mOwnerMask, i);
        msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_GroupMask, p.mGroupMask, i);
        msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_EveryoneMask, p.mEveryoneMask, i);
        msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_NextOwnerMask, p.mNextOwnerMask, i);
        msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Name, p.mName, i);
        msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Description, p.mDescription, i);
        p.mGroupOwned = p.mOwnerId.isNull() && p.mGroupId.notNull();
        p.mHasFullData = true;
        ids.push_back(id);
        self.mChangeSignal(id);
    }

    // Preserve normal selection / inspect / build behaviour. Forward before
    // retiring the in-flight markers: LLSelectMgr consults isExpectedReply()
    // to decide whether a reply block with no select node warrants a warning.
    LLSelectMgr::processObjectProperties(msg, user_data);

    for (const LLUUID& id : ids)
    {
        self.mPendingRequests.erase(id);
    }
}

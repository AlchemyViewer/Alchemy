/**
 * @file alrenderisolation.cpp
 * @brief Which of the world a snapshot cutout keeps
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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
#include "alrenderisolation.h"

#include "llcontrolavatar.h"
#include "lldrawable.h"
#include "llspatialpartition.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "llvovolume.h"

#include <algorithm>

LLUUID                   ALRenderIsolation::sArmed;
LLViewerObject*          ALRenderIsolation::sRoot = nullptr;
std::vector<LLVOAvatar*> ALRenderIsolation::sAvatars;

// static
LLUUID ALRenderIsolation::targetFromPick(const LLUUID& picked_id)
{
    LLViewerObject* object = gObjectList.findObject(picked_id);
    if (!object || object->isDead() || object->isHUDAttachment())
    {
        return LLUUID::null;
    }

    if (LLVOAvatar* avatar = object->asAvatar())
    {
        if (avatar->isControlAvatar())
        {
            // An animesh's skeleton stands for its linkset -- or, worn, for
            // its wearer.
            LLVOVolume* root = static_cast<LLControlAvatar*>(avatar)->mRootVolp;
            return root ? targetFromPick(root->getID()) : LLUUID::null;
        }
        return avatar->getID();
    }

    // Anything worn is its wearer.
    if (LLVOAvatar* wearer = object->getAvatarAncestor())
    {
        return wearer->getID();
    }

    switch (object->getPCode())
    {
    case LL_PCODE_VOLUME:
    case LL_PCODE_LEGACY_TREE:
        return object->getRootEdit()->getID();
    default:
        // Land, water, sky, grass and particles: batched per region, with
        // nothing of their own to isolate.
        return LLUUID::null;
    }
}

// static
void ALRenderIsolation::arm(const LLUUID& target_id)
{
    sArmed = target_id;
    giveOwnSpatialBridge(findArmedObject());
}

// static
void ALRenderIsolation::giveOwnSpatialBridge(LLViewerObject* object)
{
    if (object && !object->asAvatar() && object->getPCode() == LL_PCODE_VOLUME)
    {
        LLDrawable* drawable = object->getRootEdit()->mDrawable;
        if (drawable && drawable->isStatic())
        {
            drawable->makeActive();
        }
    }
}

// static
void ALRenderIsolation::disarm()
{
    sArmed.setNull();
}

// static
bool ALRenderIsolation::isArmedAvatar(const LLVOAvatar* avatar)
{
    // Armed is not the same as waiting: the Cutout box can be cleared without
    // disarming, from Debug Settings, and nothing will cut the avatar out then.
    static LLCachedControl<bool> cutout_on(gSavedSettings, "ALSnapshotCutout", false);
    if (!avatar || sArmed.isNull() || !cutout_on())
    {
        return false;
    }
    if (avatar->getID() == sArmed)
    {
        return true;
    }

    // Seated on the armed linkset, with the cutout taking seated avatars.
    static LLCachedControl<bool> include_seated(gSavedSettings, "ALSnapshotCutoutIncludeSeated", false);
    const LLViewerObject* seat = static_cast<const LLViewerObject*>(avatar->getParent());
    return include_seated() && seat && !seat->isAvatar() && seat->getRootEdit()->getID() == sArmed;
}

// static
LLViewerObject* ALRenderIsolation::findArmedObject()
{
    if (sArmed.isNull())
    {
        return nullptr;
    }
    LLViewerObject* object = gObjectList.findObject(sArmed);
    return (object && !object->isDead()) ? object : nullptr;
}

ALRenderIsolation::Scope::Scope(const LLUUID& target_id, bool include_seated)
{
    sRoot = nullptr;
    sAvatars.clear();

    LLViewerObject* object = target_id.notNull() ? gObjectList.findObject(target_id) : nullptr;
    if (object && !object->isDead())
    {
        if (LLVOAvatar* avatar = object->asAvatar())
        {
            sAvatars.push_back(avatar);
        }
        else
        {
            sRoot = object->getRootEdit();
            // Made anew since it was armed, it is static again.
            giveOwnSpatialBridge(sRoot);
            if (include_seated)
            {
                collectSeatedAvatars(sRoot);
            }
        }
        mHasSubject = true;
    }

    // Screen-space reflections trace the last frame's scene map, and a cutout
    // renders offscreen at its own size, which reallocates that map with
    // whatever the driver left in it. Traced, that is noise; cleared, the
    // march finds nothing and the probes answer, as they do off-screen.
    gPipeline.clearSceneMap();

    LLPipeline::sCutoutRender = true;
}

ALRenderIsolation::Scope::~Scope()
{
    LLPipeline::sCutoutRender = false;
    sRoot = nullptr;
    sAvatars.clear();
}

// static
void ALRenderIsolation::collectSeatedAvatars(LLViewerObject* object)
{
    // A seated avatar is a child of the prim it sits on, wherever in the
    // linkset that is.
    for (LLViewerObject* child : object->getChildren())
    {
        if (LLVOAvatar* avatar = child->asAvatar())
        {
            sAvatars.push_back(avatar);
        }
        else
        {
            collectSeatedAvatars(child);
        }
    }
}

// static
bool ALRenderIsolation::includesAvatar(const LLVOAvatar* avatar)
{
    return std::find(sAvatars.begin(), sAvatars.end(), avatar) != sAvatars.end();
}

// static
bool ALRenderIsolation::includes(LLViewerObject* vobj)
{
    if (!vobj || vobj->isHUDAttachment())
    {
        return false;
    }

    if (LLVOAvatar* avatar = vobj->asAvatar())
    {
        if (avatar->isControlAvatar())
        {
            // An animesh's skeleton goes with its linkset, rezzed or worn.
            LLVOVolume* root = static_cast<LLControlAvatar*>(avatar)->mRootVolp;
            return root && includes(root);
        }
        return includesAvatar(avatar);
    }

    // Worn, including worn animesh: whatever an included avatar wears. Asked
    // before the linkset, because a seated avatar's own root edit is the seat's
    // root -- but that is an avatar, and avatars were answered above.
    if (LLVOAvatar* wearer = vobj->getAvatarAncestor())
    {
        return includesAvatar(wearer);
    }

    return sRoot && vobj->getRootEdit() == sRoot;
}

// static
bool ALRenderIsolation::includes(LLDrawable* drawable)
{
    if (drawable && drawable->isSpatialBridge())
    {
        drawable = static_cast<LLSpatialBridge*>(drawable)->mDrawable;
    }
    return drawable && includes(drawable->getVObj().get());
}

// static
bool ALRenderIsolation::includes(LLSpatialGroup* group)
{
    LLSpatialPartition* partition = group ? group->getSpatialPartition() : nullptr;
    LLSpatialBridge* bridge = partition ? partition->asBridge() : nullptr;
    return bridge && includes(bridge->mDrawable);
}

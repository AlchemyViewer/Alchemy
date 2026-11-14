/**
 * @file skcombatlockout.cpp
 * @brief Mutes debug rendering that sees through walls while the player is aiming
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Martin "Nexii" Pitt <martin.r.pitt@gmail.com>
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

#include "skcombatlockout.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "lldrawpoolalpha.h"
#include "llframetimer.h"
#include "llselectmgr.h"
#include "llviewerparcelmgr.h"
#include "pipeline.h"

namespace
{
    // Long enough that flicking out of mouselook and back gains nothing, short enough not to slow down building.
    constexpr F64 TAIL_SECONDS = 5.0;

    // Collision, bounds, joints and raycast targets, most of them drawn with no depth test.
    constexpr U64 LOCKED_DEBUG_MASK =
          LLPipeline::RENDER_DEBUG_BBOXES
        | LLPipeline::RENDER_DEBUG_OCTREE
        | LLPipeline::RENDER_DEBUG_OCCLUSION
        | LLPipeline::RENDER_DEBUG_RAYCAST
        | LLPipeline::RENDER_DEBUG_AVATAR_DRAW_INFO
        | LLPipeline::RENDER_DEBUG_AVATAR_VOLUME
        | LLPipeline::RENDER_DEBUG_AVATAR_JOINTS
        | LLPipeline::RENDER_DEBUG_AGENT_TARGET
        | LLPipeline::RENDER_DEBUG_PHYSICS_SHAPES
        | LLPipeline::RENDER_DEBUG_IMPOSTORS;

    // Render types that hold world geometry: any of them off is walls off with avatars still drawn.
    // Avatars, particles, sky and glow stay the user's to hide, since hiding them only hurts the one who does.
    constexpr U32 LOCKED_RENDER_TYPES[] =
    {
        LLPipeline::RENDER_TYPE_SIMPLE,
        LLPipeline::RENDER_TYPE_MATERIALS,
        LLPipeline::RENDER_TYPE_ALPHA,
        LLPipeline::RENDER_TYPE_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_FULLBRIGHT_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_FULLBRIGHT,
        LLPipeline::RENDER_TYPE_TREE,
        LLPipeline::RENDER_TYPE_TERRAIN,
        LLPipeline::RENDER_TYPE_WATER,
        LLPipeline::RENDER_TYPE_VOIDWATER,
        LLPipeline::RENDER_TYPE_VOLUME,
        LLPipeline::RENDER_TYPE_GRASS,
        LLPipeline::RENDER_TYPE_BUMP,
        LLPipeline::RENDER_TYPE_GLTF_PBR,
    };

    bool sActive = false;
    F64 sLastAimSeconds = -1.0e9;

    // The user's intent for each forced toggle, valid while active.
    bool sWireframe = false;
    bool sHighlightTransparent = false;
    bool sHiddenSelections = false;
    bool sRenderTypes[LLPipeline::NUM_RENDER_TYPES] = {};

    bool isLockedRenderType(U32 type)
    {
        for (U32 locked : LOCKED_RENDER_TYPES)
        {
            if (locked == type)
            {
                return true;
            }
        }
        return false;
    }

    void engage()
    {
        sWireframe = gUseWireframe;
        gUseWireframe = false;

        sHighlightTransparent = LLDrawPoolAlpha::sShowDebugAlpha;
        LLDrawPoolAlpha::sShowDebugAlpha = false;

        sHiddenSelections = LLSelectMgr::sRenderHiddenSelections;
        LLSelectMgr::sRenderHiddenSelections = false;

        for (U32 type : LOCKED_RENDER_TYPES)
        {
            sRenderTypes[type] = gPipeline.isRenderTypeEnabled(type);
            if (!sRenderTypes[type])
            {
                gPipeline.setRenderTypeMask(type, LLPipeline::END_RENDER_TYPES);
            }
        }

        // Invisible faces only build render batches while highlighted, so rebuild on the flip as the menu toggle does.
        if (sHighlightTransparent)
        {
            gPipeline.rebuildDrawInfo();
        }
    }

    void release()
    {
        gUseWireframe = sWireframe;
        LLDrawPoolAlpha::sShowDebugAlpha = sHighlightTransparent;
        LLSelectMgr::sRenderHiddenSelections = sHiddenSelections;

        for (U32 type : LOCKED_RENDER_TYPES)
        {
            if (!sRenderTypes[type])
            {
                gPipeline.clearRenderTypeMask(type, LLPipeline::END_RENDER_TYPES);
            }
        }

        if (sHighlightTransparent)
        {
            gPipeline.rebuildDrawInfo();
        }
    }
}

void SKCombatLockout::update()
{
    const F64 now = LLFrameTimer::getTotalSeconds();
    if (gAgentCamera.cameraMouselook())
    {
        sLastAimSeconds = now;
    }

    // Only where the agent can take damage, as the top bar's health shows.
    LLViewerParcelMgr* vpm = LLViewerParcelMgr::getInstance();
    const bool damage = vpm->allowAgentDamage(gAgent.getRegion(), vpm->getAgentParcel());
    const bool want = damage && (now - sLastAimSeconds) < TAIL_SECONDS;
    if (want == sActive)
    {
        return;
    }

    sActive = want;
    if (sActive)
    {
        engage();
    }
    else
    {
        release();
    }
}

bool SKCombatLockout::active()
{
    return sActive;
}

U64 SKCombatLockout::debugMask()
{
    return sActive ? LOCKED_DEBUG_MASK : 0;
}

void SKCombatLockout::setWireframe(bool on)
{
    if (sActive)
    {
        sWireframe = on;
    }
    else
    {
        gUseWireframe = on;
    }
}

bool SKCombatLockout::getWireframe()
{
    return sActive ? sWireframe : gUseWireframe;
}

void SKCombatLockout::setHighlightTransparent(bool on)
{
    if (sActive)
    {
        sHighlightTransparent = on;
    }
    else
    {
        LLDrawPoolAlpha::sShowDebugAlpha = on;
        gPipeline.rebuildDrawInfo();
    }
}

bool SKCombatLockout::getHighlightTransparent()
{
    return sActive ? sHighlightTransparent : LLDrawPoolAlpha::sShowDebugAlpha;
}

void SKCombatLockout::setHiddenSelections(bool on)
{
    if (sActive)
    {
        sHiddenSelections = on;
    }
    else
    {
        LLSelectMgr::sRenderHiddenSelections = on;
    }
}

bool SKCombatLockout::getHiddenSelections()
{
    return sActive ? sHiddenSelections : LLSelectMgr::sRenderHiddenSelections;
}

void SKCombatLockout::toggleRenderType(U32 type)
{
    if (sActive && isLockedRenderType(type))
    {
        // Water carries void water with it, as LLPipeline::toggleRenderType does.
        sRenderTypes[type] = !sRenderTypes[type];
        if (type == LLPipeline::RENDER_TYPE_WATER)
        {
            sRenderTypes[LLPipeline::RENDER_TYPE_VOIDWATER] = !sRenderTypes[LLPipeline::RENDER_TYPE_VOIDWATER];
        }
        return;
    }
    LLPipeline::toggleRenderType(type);
}

bool SKCombatLockout::hasRenderType(U32 type)
{
    if (sActive && isLockedRenderType(type))
    {
        return sRenderTypes[type];
    }
    return gPipeline.isRenderTypeEnabled(type);
}

/**
 * @file alparceliconstrip.cpp
 * @brief The row of icons naming what the parcel underfoot forbids
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

#include "alparceliconstrip.h"

#include <fmt/format.h>

#include "lliconctrl.h"
#include "llnotificationsutil.h"
#include "llparcel.h"
#include "llregionflags.h"
#include "lltextbox.h"
#include "lltrans.h"

#include "llagent.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"

namespace
{
    // The tooltip for each icon, in enum order. The pathfinding pair is here
    // too: only their click handlers are the owner's business.
    const char* const ICON_TOOLTIPS[ALParcelIconStrip::ICON_COUNT] =
    {
        "LocationCtrlVoiceTooltip",
        "LocationCtrlFlyTooltip",
        "LocationCtrlPushTooltip",
        "LocationCtrlBuildTooltip",
        "LocationCtrlScriptsTooltip",
        "LocationCtrlDamageTooltip",
        "LocationCtrlSeeAVsTooltip",
        "LocationCtrlPathfindingDirtyTooltip",
        "LocationCtrlPathfindingDisabledTooltip"
    };

    // Places one control at `edge` and returns the far edge, spending padding
    // only if there was something to place.
    S32 place(LLUICtrl* ctrl, S32 edge, ALParcelIconStrip::EDirection direction, S32 pad)
    {
        if (!ctrl || !ctrl->getVisible())
        {
            return edge;
        }

        // Moved, never resized. Assigning one edge and then asking the rect how
        // wide it is measures it half-way through the move -- getWidth is
        // mRight - mLeft -- and stretches the control across to wherever it
        // was standing. translate cannot express that mistake.
        LLRect rect = ctrl->getRect();

        if (direction == ALParcelIconStrip::LAYOUT_RIGHTWARD)
        {
            rect.translate(edge - rect.mLeft, 0);
            ctrl->setRect(rect);
            return rect.mRight + pad;
        }

        rect.translate(edge - rect.mRight, 0);
        ctrl->setRect(rect);
        return rect.mLeft - pad;
    }
}

void ALParcelIconStrip::setIcon(EIcon icon, LLIconCtrl* ctrl)
{
    llassert(icon >= 0 && icon < ICON_COUNT);
    mIcons[icon] = ctrl;
}

void ALParcelIconStrip::initIcons()
{
    for (S32 i = 0; i < ICON_COUNT; ++i)
    {
        LLIconCtrl* ctrl = mIcons[i];
        if (!ctrl)
        {
            continue;
        }

        ctrl->setToolTip(LLTrans::getString(ICON_TOOLTIPS[i]));

        // The pathfinding pair answers for itself: rebaking the navmesh needs a
        // notification callback bound to the panel that owns the region
        // listener, which is not something this can supply.
        if (i != ICON_PATHFINDING_DIRTY && i != ICON_PATHFINDING_DISABLED)
        {
            const EIcon icon = static_cast<EIcon>(i);
            ctrl->setMouseDownCallback([icon](LLUICtrl*, S32, S32, MASK) { onIconClicked(icon); });
        }
    }
}

void ALParcelIconStrip::update(bool show)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if (!show)
    {
        for (LLIconCtrl* ctrl : mIcons)
        {
            if (ctrl)
            {
                ctrl->setVisible(false);
            }
        }
        if (mDamageText)
        {
            mDamageText->setVisible(false);
        }
        return;
    }

    LLViewerParcelMgr* vpm = LLViewerParcelMgr::getInstance();
    LLViewerRegion* agent_region = gAgent.getRegion();
    LLParcel* agent_parcel = vpm->getAgentParcel();
    if (!agent_region || !agent_parcel)
    {
        return;
    }

    // If agent is in selected parcel we use its properties because
    // they are updated more often by LLViewerParcelMgr than agent parcel properties.
    // See LLViewerParcelMgr::processParcelProperties().
    // This is needed to reflect parcel restrictions changes without having to leave
    // the parcel and then enter it again. See EXT-2987
    LLParcel* selected_parcel = vpm->getParcelSelection()->getParcel();
    LLParcel* current_parcel =
        (selected_parcel
         && selected_parcel->getLocalID() == agent_parcel->getLocalID()
         && vpm->getSelectionRegion() == agent_region)
            ? selected_parcel
            : agent_parcel;

    const bool allow_damage = vpm->allowAgentDamage(agent_region, current_parcel);

    // Most icons are "block this ability"
    setVisibleIf(ICON_VOICE,        !vpm->allowAgentVoice(agent_region, current_parcel));
    setVisibleIf(ICON_FLY,          !vpm->allowAgentFly(agent_region, current_parcel));
    setVisibleIf(ICON_PUSH,         !vpm->allowAgentPush(agent_region, current_parcel));
    // allowAgentBuild is true when anyone is allowed to build. See EXT-4610.
    setVisibleIf(ICON_BUILD,        !vpm->allowAgentBuild(current_parcel));
    setVisibleIf(ICON_SCRIPTS,      !vpm->allowAgentScripts(agent_region, current_parcel));
    setVisibleIf(ICON_DAMAGE,       allow_damage);
    setVisibleIf(ICON_SEE_AVATARS,  !current_parcel->getSeeAVs());

    setVisibleIf(ICON_PATHFINDING_DIRTY, mNavMeshDirty);
    setVisibleIf(ICON_PATHFINDING_DISABLED,
                 !mNavMeshDirty && !agent_region->dynamicPathfindingEnabled());

    if (mDamageText)
    {
        mDamageText->setVisible(allow_damage);
    }
}

void ALParcelIconStrip::setVisibleIf(EIcon icon, bool visible)
{
    if (LLIconCtrl* ctrl = mIcons[icon])
    {
        ctrl->setVisible(visible);
    }
}

S32 ALParcelIconStrip::layout(S32 edge, EDirection direction, S32 icon_pad) const
{
    // Both panels put the same thing in the same place: the damage percentage
    // furthest from the location text, then the icons in reverse index order
    // running back towards it. Starting from the other end therefore means
    // walking the list the other way, not just changing which edge of each
    // control is pinned -- laying them out in one order from both ends would
    // mirror the strip.
    if (direction == LAYOUT_RIGHTWARD)
    {
        edge = place(mDamageText, edge, direction, icon_pad);
        for (S32 i = ICON_COUNT - 1; i >= 0; --i)
        {
            edge = place(mIcons[i], edge, direction, icon_pad);
        }
        return edge;
    }

    for (S32 i = 0; i < ICON_COUNT; ++i)
    {
        edge = place(mIcons[i], edge, direction, icon_pad);
    }
    return place(mDamageText, edge, direction, icon_pad);
}

void ALParcelIconStrip::setHealth(S32 health)
{
    if (health == mLastHealth || !mDamageText)
    {
        return;
    }
    mLastHealth = health;
    mDamageText->setText(fmt::format("{}%", health));
}

//static
void ALParcelIconStrip::onIconClicked(EIcon icon)
{
    switch (icon)
    {
    case ICON_VOICE:
        LLNotificationsUtil::add("NoVoice");
        break;
    case ICON_FLY:
        LLNotificationsUtil::add("NoFly");
        break;
    case ICON_PUSH:
        LLNotificationsUtil::add("PushRestricted");
        break;
    case ICON_BUILD:
        LLNotificationsUtil::add("NoBuild");
        break;
    case ICON_SCRIPTS:
    {
        LLViewerRegion* region = gAgent.getRegion();
        if (region && region->getRegionFlag(REGION_FLAGS_ESTATE_SKIP_SCRIPTS))
        {
            LLNotificationsUtil::add("ScriptsStopped");
        }
        else if (region && region->getRegionFlag(REGION_FLAGS_SKIP_SCRIPTS))
        {
            LLNotificationsUtil::add("ScriptsNotRunning");
        }
        else
        {
            LLNotificationsUtil::add("NoOutsideScripts");
        }
        break;
    }
    case ICON_DAMAGE:
        LLNotificationsUtil::add("NotSafe");
        break;
    case ICON_SEE_AVATARS:
        LLNotificationsUtil::add("SeeAvatars");
        break;
    case ICON_PATHFINDING_DIRTY:
    case ICON_PATHFINDING_DISABLED:
    case ICON_COUNT:
        // Answered by the owner, or not an icon at all.
        break;
    // no default to get compiler warning when a new icon gets added
    }
}

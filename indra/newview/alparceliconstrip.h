/**
 * @file alparceliconstrip.h
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

#ifndef AL_ALPARCELICONSTRIP_H
#define AL_ALPARCELICONSTRIP_H

#include "stdtypes.h"

class LLIconCtrl;
class LLTextBox;
class LLUICtrl;

// The strip of icons that says what the parcel underfoot forbids, and the
// damage percentage beside them.
//
// This was written twice -- once in LLLocationInputCtrl and once in
// LLPanelTopInfoBar -- with the same enum, the same parcel queries, the same
// visibility rules and the same notifications, laid out in opposite
// directions. Two copies is how the health readout ended up keyed on a
// process-wide static in both, and how the parcel observer ended up wired to
// the icons but not to the location text in both.
//
// What is NOT shared is how the controls come into existence: the navigation
// bar builds them from parameter blocks on its own widget, the top info bar
// takes them as XUI children. So this owns the behaviour and is handed the
// controls, rather than owning the controls too. Any slot may be left null --
// the top info bar fills seven of the nine.
class ALParcelIconStrip
{
public:
    enum EIcon
    {
        ICON_VOICE = 0,
        ICON_FLY,
        ICON_PUSH,
        ICON_BUILD,
        ICON_SCRIPTS,
        ICON_DAMAGE,
        ICON_SEE_AVATARS,
        // Navigation bar only; the top info bar leaves these null.
        ICON_PATHFINDING_DIRTY,
        ICON_PATHFINDING_DISABLED,
        ICON_COUNT
    };

    // Which way the strip runs from the edge handed to layout(). What lands
    // where is the same either way -- the damage percentage furthest from the
    // location text, then the icons in reverse index order running back
    // towards it -- so the two panels differ only in which end they start
    // from, and layout() walks its list to match.
    enum EDirection
    {
        LAYOUT_RIGHTWARD,
        LAYOUT_LEFTWARD
    };

    ALParcelIconStrip() = default;

    // Hand over the controls. Null is allowed and means "this panel does not
    // show that one".
    void setIcon(EIcon icon, LLIconCtrl* ctrl);
    void setDamageText(LLTextBox* text) { mDamageText = text; }

    // Tooltips, and click handlers for every icon whose response is the same
    // in both panels. Call once the controls are all set. The two pathfinding
    // icons are left alone: one of their responses needs a callback bound to
    // the navigation bar, so that panel wires them itself.
    void initIcons();

    // Read the parcel and write the icons' visibility. `show` false hides
    // every one of them without asking the parcel anything.
    void update(bool show);

    void setNavMeshDirty(bool dirty) { mNavMeshDirty = dirty; }
    bool isNavMeshDirty() const { return mNavMeshDirty; }

    // Place the visible controls starting from `edge`, returning the far edge.
    // Padding is spent only on controls actually placed.
    S32 layout(S32 edge, EDirection direction, S32 icon_pad) const;

    // Per instance. Both panels used a function-local static for this, so one
    // rebuilt after the value moved kept the stale compare and never wrote its
    // label again.
    void setHealth(S32 health);

private:
    // What the notification for one icon is. Shared by both panels; the
    // pathfinding pair is not here because its owner answers for it.
    static void onIconClicked(EIcon icon);

    // Writes one slot's visibility if the panel filled it.
    void setVisibleIf(EIcon icon, bool visible);

    LLIconCtrl* mIcons[ICON_COUNT] = {};
    LLTextBox*  mDamageText = nullptr;
    bool        mNavMeshDirty = false;
    S32         mLastHealth = -1;
};

#endif // AL_ALPARCELICONSTRIP_H

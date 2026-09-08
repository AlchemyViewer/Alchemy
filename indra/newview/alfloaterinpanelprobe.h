/**
 * @file alfloaterinpanelprobe.h
 * @brief Builds a floater inside a panel and reports what that costs.
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

#pragma once

#include "llfloater.h"
#include "llframetimer.h"

#include <string>

class LLCheckBoxCtrl;
class LLComboBox;
class LLPanel;
class LLTextEditor;

// A floater built from its file, living as a child of a plain panel rather
// than of the floater view, and a report of what that changes: what adopts
// it, what moves it, what it still believes about itself, and which of its
// own gestures keep working where an editor's canvas would rather they did
// not. The multi_floater case asks the same question of a host.
class ALFloaterInPanelProbe final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterInPanelProbe, LLFloater);

    ALFloaterInPanelProbe(const LLSD& key);
    ~ALFloaterInPanelProbe() override;

    bool postBuild() override;
    void draw() override;

private:
    void onBuild();
    void onClear();
    void onFocusBuilt();
    void tame(LLFloater* built);
    void writeLive();

    LLPanel*        mStage = nullptr;
    LLComboBox*     mFile = nullptr;
    LLCheckBoxCtrl* mTame = nullptr;
    LLCheckBoxCtrl* mMulti = nullptr;
    LLTextEditor*   mReport = nullptr;
    LLTextEditor*   mLive = nullptr;

    LLHandle<LLFloater> mBuilt;
    LLHandle<LLFloater> mHost;

    LLFrameTimer    mLiveTimer;
};

/**
 * @file llfloaterlocalassets.h
 * @brief Unified "Local Assets" floater (mesh / animation / texture / material previews)
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

#ifndef LL_LLFLOATERLOCALASSETS_H
#define LL_LLFLOATERLOCALASSETS_H

#include "llfloater.h"

#include <string>
#include <vector>

class LLTabContainer;

class LLFloaterLocalAssets final : public LLFloater
{
public:
    LLFloaterLocalAssets(const LLSD& key);
    ~LLFloaterLocalAssets() override;

    bool postBuild() override;

    // Load OS-dropped files into the matching tabs, by extension (routed here from
    // LLViewerWindow::handleDragNDropFile when the drop lands on this floater).
    void dropFiles(const std::vector<std::string>& paths);

private:
    LLTabContainer* mTabs { nullptr };
};

#endif // LL_LLFLOATERLOCALASSETS_H

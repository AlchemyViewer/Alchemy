/**
 * @file alpanelscriptaliases.h
 * @brief Script Studio's SLua aliases, on the Build tab of its preferences.
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

#include "llpanel.h"
#include "lluuid.h"

#include <boost/signals2.hpp>

#include <string>
#include <vector>

class ALPaneList;
class LLLineEditor;
class LLTextBox;

// Script Studio's SLua aliases (LA22), on the Build tab of its preferences:
// each a name a require says after the @ and a folder on disk it stands
// for, which naming it lets a require read -- how a script in an object
// reaches a library on disk, and any script one kept apart from it -- or
// an inventory folder dropped on it, which a require reads while world
// includes are on. A folder added is named after itself; the name chosen
// in the list can be typed over. What it writes is the setting
// (ALScriptPreprocessor::studioAliases), which the window's Cancel puts
// back.
class ALPanelScriptAliases final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALPanelScriptAliases, LLPanel);

    ALPanelScriptAliases();
    ~ALPanelScriptAliases() override;

    bool postBuild() override;
    // The list as the setting has it, the one chosen kept chosen.
    void refresh() override;
    // An inventory folder dropped on it named an alias too.
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                           std::string& tooltip_msg) override;

private:
    void onAdd();
    void addInventoryFolder(const LLUUID& folder, const std::string& name);
    void onRemove();
    void onRename();
    // The name field and the buttons as the list and the disk's switch
    // say.
    void refreshChosen();

    ALPaneList*   mList = nullptr;
    LLLineEditor* mName = nullptr;
    LLTextBox*    mSaid = nullptr;
    std::vector<boost::signals2::scoped_connection> mWatches;
};

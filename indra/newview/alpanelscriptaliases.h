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

#include <boost/signals2.hpp>

#include <string>
#include <vector>

class ALPaneList;
class LLLineEditor;
class LLTextBox;

// Script Studio's SLua aliases (LA22), on the Build tab of its preferences:
// each a name a require says after the @ and a folder on disk it stands
// for, which naming it lets a require read -- how a script in an object
// reaches a library on disk, and any script one kept apart from it -- or,
// while world includes are on, an inventory folder dropped on it, which a
// require reads only then. A folder added is named after itself; the name
// chosen in the list can be typed over. What it writes is the setting
// (ALScriptPreprocessor::studioAliases), which the window's Cancel puts
// back.
class ALPanelScriptAliases final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALPanelScriptAliases, LLPanel);

    ALPanelScriptAliases();
    ~ALPanelScriptAliases() override;

    bool postBuild() override;
    // The list as the setting has it, the one chosen kept chosen: an
    // inventory folder by its name now, a folder on disk not there said so.
    void refresh() override;
    // An inventory folder dropped on it named an alias too, while world
    // includes are on; refused while they are off, when nothing reads it.
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                           std::string& tooltip_msg) override;

    // A folder named an alias in the setting -- the one way one is, from
    // here or from a fix: under `name` itself where `exact`, as a require
    // a fix rewrites says it; else under one made from it clear of the
    // names taken, as a folder added is named after itself, and not again
    // where the folder has one already. True where the setting then has
    // the folder under an alias, false where an exact name is already
    // another folder's. The folder is one on disk, or an inventory folder
    // as ALScriptPreprocessor::inventoryAliasFolder names it.
    static bool addStudioAlias(const std::string& name, const std::string& folder, bool exact);
    // Whether two aliases' folders are one: an inventory folder by its
    // id; a folder on disk by its path made plain, `C:\lib` and `C:\lib\`
    // one, or, where the two spell it apart, as the disk has them.
    static bool sameFolder(const std::string& a, const std::string& b);

private:
    void onAdd();
    void onRemove();
    void onRename();
    // The name field and the buttons as the list and the disk's switch
    // say.
    void refreshChosen();

    ALPaneList*   mList = nullptr;
    LLLineEditor* mName = nullptr;
    // Why a name typed was not taken: kept as the row chosen changes -- a
    // name committed by clicking another row is said after it -- and
    // cleared by the next name tried, or the setting changed.
    LLTextBox*    mSaid = nullptr;
    std::vector<boost::signals2::scoped_connection> mWatches;
};

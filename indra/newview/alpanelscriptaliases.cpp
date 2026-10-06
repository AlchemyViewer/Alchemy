/**
 * @file alpanelscriptaliases.cpp
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


#include "llviewerprecompiledheaders.h"

#include "alpanelscriptaliases.h"

#include "alluauconfig.h"
#include "alpanelist.h"
#include "alscriptpreprocessor.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "lllineeditor.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

#include <algorithm>

static LLPanelInjector<ALPanelScriptAliases> t_script_aliases("al_panel_script_aliases");

ALPanelScriptAliases::ALPanelScriptAliases() = default;

ALPanelScriptAliases::~ALPanelScriptAliases() = default;

bool ALPanelScriptAliases::postBuild()
{
    mList = getChild<ALPaneList>("aliases");
    mName = getChild<LLLineEditor>("alias_name");
    mSaid = getChild<LLTextBox>("said");
    mList->setEmpty(getString("NoAlias"), LLStringUtil::null);
    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshChosen(); });
    mName->setCommitOnFocusLost(true);
    mName->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRename(); });
    getChild<LLButton>("add_alias")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAdd(); });
    getChild<LLButton>("remove_alias")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemove(); });
    // The setting changed from anywhere -- a fix that named a folder, a
    // Cancel -- and the disk's switch, which the aliases read under.
    for (const char* setting : { "ALScriptSLuaAliases", "ALScriptPreprocDiskIncludes", "ALScriptPreprocWorldIncludes" })
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mWatches.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) { refresh(); }));
        }
    }
    refresh();
    return true;
}

void ALPanelScriptAliases::refresh()
{
    const std::string chosen = mList->getFirstSelected() ? mList->getFirstSelected()->getValue().asString() : std::string();
    mList->deleteAllItems();
    const bool disk  = gSavedSettings.getBOOL("ALScriptPreprocDiskIncludes");
    const bool world = ALScriptPreprocessor::worldIncludes();
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        // A folder on disk by its path; an inventory folder by its name,
        // as the inventory has it now.
        LLUUID      inventory;
        std::string shown = alias.folder;
        std::string tip   = alias.folder;
        if (ALScriptPreprocessor::inventoryAliasFolder(alias.folder, inventory))
        {
            const LLViewerInventoryCategory* category = gInventory.getCategory(inventory);
            shown = category ? getString("InventoryFolder", { { "[NAME]", category->getName() } }) : getString("InventoryFolderGone");
            tip   = getString(world ? "InventoryFolderTip" : "InventoryFolderOffTip");
        }
        else if (!disk)
        {
            tip = getString("DiskFolderOffTip", { { "[FOLDER]", alias.folder } });
        }
        LLSD row;
        row["value"]                  = alias.name;
        row["columns"][0]["column"]   = "alias";
        row["columns"][0]["value"]    = "@" + alias.name;
        row["columns"][1]["column"]   = "folder";
        row["columns"][1]["value"]    = shown;
        row["columns"][1]["tool_tip"] = tip;
        mList->addElement(row);
    }
    if (!chosen.empty())
    {
        mList->selectByValue(chosen);
    }
    refreshChosen();
}

void ALPanelScriptAliases::refreshChosen()
{
    // Each alias whichever switch it is read under: an inventory folder's
    // is world includes', a folder on disk's the disk's, as its tip says.
    LLScrollListItem* chosen = mList->getFirstSelected();
    getChildView("remove_alias")->setEnabled(chosen != nullptr);
    mName->setEnabled(chosen != nullptr);
    if (!mName->hasFocus())
    {
        mName->setText(chosen ? chosen->getValue().asString() : std::string());
    }
    mSaid->setText(LLStringUtil::null);
}

void ALPanelScriptAliases::onAdd()
{
    const std::vector<std::string> folders = ALScriptPreprocessor::includeFolders();
    (new LLDirPickerThread(
         [](const std::vector<std::string>& picked, std::string) {
             if (picked.empty())
             {
                 return;
             }
             std::vector<ALScriptPreprocessor::StudioAlias> aliases = ALScriptPreprocessor::studioAliases();
             std::vector<std::string>                       taken;
             for (const ALScriptPreprocessor::StudioAlias& alias : aliases)
             {
                 if (alias.folder == picked.front())
                 {
                     return;
                 }
                 taken.push_back(alias.name);
             }
             aliases.push_back({ ALLuauConfig::studioAliasFor(gDirUtilp->getBaseFileName(picked.front()), taken), picked.front() });
             ALScriptPreprocessor::setStudioAliases(aliases);
             // Nothing on disk is read while the disk's switch is off.
             gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
         },
         folders.empty() ? std::string() : folders.front()))
        ->getFile();
}

bool ALPanelScriptAliases::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data,
                                             EAcceptance* accept, std::string& tooltip_msg)
{
    if (cargo_type != DAD_CATEGORY || !cargo_data)
    {
        return LLPanel::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
    }
    const LLInventoryCategory* category = static_cast<const LLInventoryCategory*>(cargo_data);
    *accept                             = ACCEPT_YES_SINGLE;
    tooltip_msg                         = getString("DropFolder");
    if (drop)
    {
        addInventoryFolder(category->getUUID(), category->getName());
    }
    return true;
}

void ALPanelScriptAliases::addInventoryFolder(const LLUUID& folder, const std::string& name)
{
    // Named after itself, as a folder on disk is; never twice. World
    // includes are the scripter's to turn on, not the drop's.
    const std::string                              id      = ALScriptPreprocessor::inventoryAliasFolder(folder);
    std::vector<ALScriptPreprocessor::StudioAlias> aliases = ALScriptPreprocessor::studioAliases();
    std::vector<std::string>                       taken;
    for (const ALScriptPreprocessor::StudioAlias& alias : aliases)
    {
        if (alias.folder == id)
        {
            return;
        }
        taken.push_back(alias.name);
    }
    aliases.push_back({ ALLuauConfig::studioAliasFor(name, taken), id });
    ALScriptPreprocessor::setStudioAliases(aliases);
}

void ALPanelScriptAliases::onRemove()
{
    LLScrollListItem* chosen = mList->getFirstSelected();
    if (!chosen)
    {
        return;
    }
    const std::string                              name    = chosen->getValue().asString();
    std::vector<ALScriptPreprocessor::StudioAlias> aliases = ALScriptPreprocessor::studioAliases();
    aliases.erase(std::remove_if(aliases.begin(), aliases.end(), [&name](const ALScriptPreprocessor::StudioAlias& alias) { return alias.name == name; }),
                  aliases.end());
    ALScriptPreprocessor::setStudioAliases(aliases);
}

void ALPanelScriptAliases::onRename()
{
    LLScrollListItem* chosen = mList->getFirstSelected();
    if (!chosen)
    {
        return;
    }
    const std::string was = chosen->getValue().asString();
    std::string       now = mName->getText();
    LLStringUtil::trim(now);
    if (now == was)
    {
        return;
    }
    std::vector<ALScriptPreprocessor::StudioAlias> aliases = ALScriptPreprocessor::studioAliases();
    LLStringUtil::format_map_t                     args;
    args["[NAME]"] = now;
    if (!ALLuauConfig::studioAliasName(now))
    {
        mName->setText(was);
        mSaid->setText(getString("NameBad"));
        return;
    }
    if (std::any_of(aliases.begin(), aliases.end(), [&now, &was](const ALScriptPreprocessor::StudioAlias& alias) {
            return alias.name != was && LLStringUtil::compareInsensitive(alias.name, now) == 0;
        }))
    {
        mName->setText(was);
        mSaid->setText(getString("NameTaken", args));
        return;
    }
    for (ALScriptPreprocessor::StudioAlias& alias : aliases)
    {
        if (alias.name == was)
        {
            alias.name = now;
        }
    }
    ALScriptPreprocessor::setStudioAliases(aliases);
    mList->selectByValue(now);
    refreshChosen();
}

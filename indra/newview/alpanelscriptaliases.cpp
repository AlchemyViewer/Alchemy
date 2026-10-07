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
#include "fsyspath.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "lllineeditor.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

static LLPanelInjector<ALPanelScriptAliases> t_script_aliases("al_panel_script_aliases");

ALPanelScriptAliases::ALPanelScriptAliases() = default;

ALPanelScriptAliases::~ALPanelScriptAliases() = default;

bool ALPanelScriptAliases::postBuild()
{
    mList = getChild<ALPaneList>("aliases");
    mName = getChild<LLLineEditor>("alias_name");
    mSaid = getChild<LLTextBox>("said");
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
    // An inventory folder offered only while one dropped would be read.
    mList->setEmpty(getString(world ? "NoAlias" : "NoAliasWorldOff"), LLStringUtil::null);
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        // A folder on disk by its path, said not found where it is not
        // there now; an inventory folder by its name, as the inventory has
        // it now.
        LLUUID      inventory;
        std::string shown = alias.folder;
        std::string tip   = alias.folder;
        if (ALScriptPreprocessor::inventoryAliasFolder(alias.folder, inventory))
        {
            const LLViewerInventoryCategory* category = gInventory.getCategory(inventory);
            shown = category ? getString("InventoryFolder", { { "[NAME]", category->getName() } }) : getString("InventoryFolderGone");
            tip   = getString(world ? "InventoryFolderTip" : "InventoryFolderOffTip");
        }
        else
        {
            std::error_code error;
            const bool      there = std::filesystem::is_directory(fsyspath(alias.folder), error);
            if (!there)
            {
                shown = getString("DiskFolderGone", { { "[FOLDER]", alias.folder } });
            }
            if (!disk)
            {
                tip = getString("DiskFolderOffTip", { { "[FOLDER]", alias.folder } });
            }
            else if (!there)
            {
                tip = getString("DiskFolderGoneTip", { { "[FOLDER]", alias.folder } });
            }
        }
        // The name whole, which the narrow column may cut, with what it
        // stands for.
        LLSD row;
        row["value"]                  = alias.name;
        row["columns"][0]["column"]   = "alias";
        row["columns"][0]["value"]    = "@" + alias.name;
        row["columns"][0]["tool_tip"] = getString("AliasTip", { { "[NAME]", alias.name }, { "[FOLDER]", shown } });
        row["columns"][1]["column"]   = "folder";
        row["columns"][1]["value"]    = shown;
        row["columns"][1]["tool_tip"] = tip;
        mList->addElement(row);
    }
    if (!chosen.empty())
    {
        mList->selectByValue(chosen);
    }
    // What was said of a name tried is past once the setting has changed.
    mSaid->setText(LLStringUtil::null);
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
    // What was said of a name tried stays: a name committed by clicking
    // another row is turned down just before that row is chosen.
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
             // Nothing on disk is read while the disk's switch is off.
             if (addStudioAlias(gDirUtilp->getBaseFileName(picked.front()), picked.front(), false))
             {
                 gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
             }
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
    // An inventory folder's alias is read only while world includes are
    // on, which are the scripter's to turn on and not the drop's: while
    // they are off, one dropped would be an alias nothing reads.
    if (!ALScriptPreprocessor::worldIncludes())
    {
        *accept     = ACCEPT_NO;
        tooltip_msg = getString("DropFolderWorldOff");
        return true;
    }
    const LLInventoryCategory* category = static_cast<const LLInventoryCategory*>(cargo_data);
    *accept                             = ACCEPT_YES_SINGLE;
    tooltip_msg                         = getString("DropFolder");
    if (drop)
    {
        // Named after itself, as a folder on disk is; never twice.
        addStudioAlias(category->getName(), ALScriptPreprocessor::inventoryAliasFolder(category->getUUID()), false);
    }
    return true;
}

// static
bool ALPanelScriptAliases::addStudioAlias(const std::string& name, const std::string& folder, bool exact)
{
    std::vector<ALScriptPreprocessor::StudioAlias> aliases = ALScriptPreprocessor::studioAliases();
    std::vector<std::string>                       taken;
    for (const ALScriptPreprocessor::StudioAlias& alias : aliases)
    {
        // An exact name is looked for by itself, a made one by its folder.
        const bool same = sameFolder(alias.folder, folder);
        if (exact ? LLStringUtil::compareInsensitive(alias.name, name) == 0 : same)
        {
            return same;
        }
        taken.push_back(alias.name);
    }
    aliases.push_back({ exact ? name : ALLuauConfig::studioAliasFor(name, taken), folder });
    ALScriptPreprocessor::setStudioAliases(aliases);
    return true;
}

// static
bool ALPanelScriptAliases::sameFolder(const std::string& a, const std::string& b)
{
    if (a == b)
    {
        return true;
    }
    LLUUID     inventory_a;
    LLUUID     inventory_b;
    const bool in_a = ALScriptPreprocessor::inventoryAliasFolder(a, inventory_a);
    const bool in_b = ALScriptPreprocessor::inventoryAliasFolder(b, inventory_b);
    if (in_a || in_b)
    {
        return in_a && in_b && inventory_a == inventory_b;
    }
    // The path made plain -- its `.` and `..` gone, a separator at its end
    // dropped -- and where the two still differ, the disk asked whether
    // they are one folder: a case apart on Windows, a link.
    const auto plain = [](const std::string& folder) {
        std::filesystem::path path = fsyspath(folder).lexically_normal();
        if (!path.has_filename())
        {
            path = path.parent_path();
        }
        return path;
    };
    const std::filesystem::path path_a = plain(a);
    const std::filesystem::path path_b = plain(b);
    if (path_a == path_b)
    {
        return true;
    }
    std::error_code error;
    return std::filesystem::equivalent(path_a, path_b, error) && !error;
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
    // Each name tried says anew whether it was taken.
    mSaid->setText(LLStringUtil::null);
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

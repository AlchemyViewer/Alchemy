/**
 * @file llfloaterlocalassets.cpp
 * @brief Unified "Local Assets" floater implementation
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

#include "llfloaterlocalassets.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llfilepicker.h"
#include "llinventoryicon.h"
#include "llmenugl.h"
#include "llpanel.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "lltabcontainer.h"
#include "lluictrlfactory.h"
#include "llviewermenufile.h"   // LLFilePickerReplyThread

#include "llagentcamera.h"
#include "llcontrolavatar.h"
#include "lllistcontextmenu.h"
#include "lllocalanim.h"
#include "lllocalassetpaths.h"
#include "lllocalbitmaps.h"
#include "lllocalgltfmaterials.h"
#include "lllocalmesh.h"
#include "llselectmgr.h"
#include "llviewerjointattachment.h"
#include "llviewermenu.h"       // get_selected_animesh_control_avatar
#include "llviewerobject.h"
#include "llvoavatarself.h"     // gAgentAvatarp, isAgentAvatarValid, mAttachmentPoints

#include <boost/signals2.hpp>

// ============================================================================
//  LLPanelLocalAssetBase
//
//  Shared scroll-list behaviour for the asset tabs. The list shows both decoded
//  units (fed by the backing manager) and saved-but-undecoded file paths (from
//  LLLocalAssetPaths, dimmed). Files decode lazily: double-clicking an undecoded
//  row -- or an action that needs it -- loads it. Each concrete tab plugs in its
//  manager via a small set of virtuals; everything else lives here.
// ============================================================================
namespace
{

class LLPanelLocalAssetBase : public LLPanel
{
public:
    bool postBuild() override;

    // Rebuild the visible list (decoded units + dimmed undecoded saved paths).
    void refresh();

    // Decode + add a file into this tab's backing manager (used by the floater's
    // OS drag-and-drop routing). Public wrapper over the protected loadPath().
    void loadFile(const std::string& path) { loadPath(path); }

protected:
    // Backing-manager hooks, implemented per asset type.
    virtual void feedList() = 0;                                   // decoded units -> rows
    virtual void delUnit(const LLUUID& tracking_id) = 0;          // unload a decoded unit
    virtual void loadPath(const std::string& path) = 0;          // decode a file (lazy / add)
    virtual LLUUID unitForPath(const std::string& path) const = 0; // decoded? -> tracking id
    virtual std::string pathForUnit(const LLUUID& tracking_id) const = 0; // tracking id -> path
    virtual std::string iconName() const = 0;                    // row icon for this type
    virtual LLLocalAssetPaths::EType assetType() const = 0;
    virtual LLFilePicker::ELoadFilter getLoadFilter() const = 0;
    // Subscribe to the backing manager's "units changed" signal for reactive refresh.
    virtual boost::signals2::connection connectChanged(const std::function<void()>& cb) = 0;

    // Optional per-type extra buttons (Rez/Attach, Play/Stop, ...). Shown and wired
    // by overrides; the base keeps them hidden.
    virtual void initExtraButtons() {}
    virtual void updateExtraButtons(bool /*has_selection*/) {}

    // Placeholder shown over the list while it's empty (per asset type).
    virtual std::string emptyHint() const { return LLStringUtil::null; }

    LLUUID              getSelectedID() const;   // null if the selection is undecoded
    std::vector<LLUUID> getSelectedIDs() const;  // decoded selections only
    std::string         getSelectedPath() const; // decoded or undecoded

    LLScrollListCtrl* mList { nullptr };
    LLButton*         mAddBtn { nullptr };
    LLButton*         mDelBtn { nullptr };

private:
    void appendUnloaded();
    void selectByPath(const std::string& path);
    void onAddBtn();
    void onDelBtn();
    void onDoubleClick();
    void onSelectionChange();
    static void onFilesPicked(const std::vector<std::string>& filenames,
                              LLHandle<LLPanelLocalAssetBase> handle);

    boost::signals2::scoped_connection mChangedConn;
};

bool LLPanelLocalAssetBase::postBuild()
{
    mList   = getChild<LLScrollListCtrl>("l_name_list");
    mAddBtn = getChild<LLButton>("add_btn");
    mDelBtn = getChild<LLButton>("del_btn");

    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onSelectionChange, this));
    mList->setDoubleClickCallback(boost::bind(&LLPanelLocalAssetBase::onDoubleClick, this));
    mAddBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onAddBtn, this));
    mDelBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onDelBtn, this));

    // Reactive refresh: the backing manager signals us on any unit change (decode,
    // remove, and for mesh spawn/derez), whoever made it -- us, the texture picker,
    // an in-world Delete. scoped_connection drops on panel teardown.
    mChangedConn = connectChanged(boost::bind(&LLPanelLocalAssetBase::refresh, this));

    initExtraButtons();
    refresh();
    return true;
}

void LLPanelLocalAssetBase::refresh()
{
    if (!mList)
    {
        return;
    }
    const std::string prev = getSelectedPath();
    mList->clearRows();
    feedList();        // decoded units (with their icons / mesh bold etc.)
    appendUnloaded();  // saved-but-undecoded paths, dimmed
    selectByPath(prev);
    onSelectionChange();
    // Hint over an empty list (LLScrollListCtrl shows the comment only when empty).
    mList->setCommentText(mList->getItemCount() == 0 ? emptyHint() : LLStringUtil::null);
}

void LLPanelLocalAssetBase::appendUnloaded()
{
    const LLSD paths = LLLocalAssetPaths::getInstance()->getPaths(assetType());
    const std::string icon = iconName();
    for (LLSD::array_const_iterator it = paths.beginArray(); it != paths.endArray(); ++it)
    {
        const std::string path = it->asString();
        if (unitForPath(path).notNull())
        {
            continue; // already decoded -> shown by feedList()
        }
        LLSD element;
        element["columns"][0]["column"] = "icon";
        element["columns"][0]["type"]   = "icon";
        element["columns"][0]["value"]  = icon;

        element["columns"][1]["column"] = "unit_name";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = gDirUtilp->getBaseFileName(path, true);
        element["columns"][1]["font"]["style"] = "ITALIC"; // dimmed: not loaded yet

        LLSD data;
        data["path"] = path; // no "id" -> undecoded
        element["value"] = data;

        mList->addElement(element);
    }
}

std::string LLPanelLocalAssetBase::getSelectedPath() const
{
    if (mList)
    {
        if (LLScrollListItem* item = mList->getFirstSelected())
        {
            const LLSD v = item->getValue();
            if (v.has("path"))
            {
                return v["path"].asString();
            }
            const LLUUID id = v["id"].asUUID();
            if (id.notNull())
            {
                return pathForUnit(id);
            }
        }
    }
    return std::string();
}

void LLPanelLocalAssetBase::selectByPath(const std::string& path)
{
    if (!mList || path.empty())
    {
        return;
    }
    const std::vector<LLScrollListItem*>& items = mList->getAllData();
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i])
        {
            continue;
        }
        const LLSD v = items[i]->getValue();
        const std::string rowpath = v.has("path") ? v["path"].asString() : pathForUnit(v["id"].asUUID());
        if (rowpath == path)
        {
            mList->selectNthItem((S32)i);
            break;
        }
    }
}

LLUUID LLPanelLocalAssetBase::getSelectedID() const
{
    if (mList)
    {
        if (LLScrollListItem* item = mList->getFirstSelected())
        {
            return item->getValue()["id"].asUUID();
        }
    }
    return LLUUID::null;
}

std::vector<LLUUID> LLPanelLocalAssetBase::getSelectedIDs() const
{
    std::vector<LLUUID> ids;
    if (mList)
    {
        for (LLScrollListItem* item : mList->getAllSelected())
        {
            if (item)
            {
                const LLUUID id = item->getValue()["id"].asUUID();
                if (id.notNull())
                {
                    ids.push_back(id);
                }
            }
        }
    }
    return ids;
}

void LLPanelLocalAssetBase::onSelectionChange()
{
    const bool has_selection = mList && !mList->getAllSelected().empty();
    if (mDelBtn)
    {
        mDelBtn->setEnabled(has_selection);
    }
    updateExtraButtons(has_selection);
}

void LLPanelLocalAssetBase::onDoubleClick()
{
    // Double-clicking a dimmed (undecoded) row loads it on demand.
    const std::string path = getSelectedPath();
    if (!path.empty() && unitForPath(path).isNull())
    {
        loadPath(path); // decode -> manager signal -> refresh()
    }
}

void LLPanelLocalAssetBase::onAddBtn()
{
    LLHandle<LLPanelLocalAssetBase> handle = getDerivedHandle<LLPanelLocalAssetBase>();
    LLFilePickerReplyThread::startPicker(
        boost::bind(&LLPanelLocalAssetBase::onFilesPicked, _1, handle),
        getLoadFilter(), true);
}

void LLPanelLocalAssetBase::onDelBtn()
{
    if (!mList)
    {
        return;
    }
    // Snapshot (path, id) first: removePath()/delUnit() mutate state and free the
    // LLScrollListItems we'd otherwise be iterating.
    std::vector<std::pair<std::string, LLUUID> > selected;
    for (LLScrollListItem* item : mList->getAllSelected())
    {
        if (!item)
        {
            continue;
        }
        const LLSD v = item->getValue();
        const LLUUID id = v["id"].asUUID();
        const std::string path = v.has("path") ? v["path"].asString() : pathForUnit(id);
        selected.emplace_back(path, id);
    }

    for (const auto& entry : selected)
    {
        if (!entry.first.empty())
        {
            LLLocalAssetPaths::getInstance()->removePath(assetType(), entry.first); // forget the path
        }
        if (entry.second.notNull())
        {
            delUnit(entry.second); // unload the decoded unit (fires the manager signal)
        }
    }
    refresh(); // removePath() alone (undecoded rows) doesn't fire a manager signal
}

// static
void LLPanelLocalAssetBase::onFilesPicked(const std::vector<std::string>& filenames,
                                          LLHandle<LLPanelLocalAssetBase> handle)
{
    // The picker runs on its own thread and posts back here; the panel may have
    // been torn down (floater closed) in the meantime.
    if (handle.isDead() || filenames.empty())
    {
        return;
    }
    LLPanelLocalAssetBase* self = handle.get();
    for (const std::string& filename : filenames)
    {
        if (!filename.empty())
        {
            // Decode now (the user just chose it); the manager signal both refreshes
            // us and records the path in LLLocalAssetPaths for persistence.
            self->loadPath(filename);
        }
    }
}

// ============================================================================
//  Mesh tab -- Rez/Derez, an attach-point combo + Attach, Select, joint toggle.
// ============================================================================
class LLPanelLocalMesh final : public LLPanelLocalAssetBase
{
public:
    ~LLPanelLocalMesh() override;

    // Actions shared by the side buttons and the right-click row menu (decoded units).
    void doSpawn(const LLUUID& tracking_id);
    void doAttach(const LLUUID& tracking_id, S32 attach_point);
    void doDetach(const LLUUID& tracking_id);
    void doDelete(const LLUUID& tracking_id);
    void menuAttach(const LLUUID& tracking_id, const LLSD& point) { doAttach(tracking_id, point.asInteger()); }
    void doSelect(const LLUUID& tracking_id);
    void doDerez(const LLUUID& tracking_id);
    bool isUnitAttached(const LLUUID& tracking_id) const;
    bool isUnitSpawned(const LLUUID& tracking_id) const;

protected:
    void feedList() override
    {
        LLLocalMeshMgr::getInstance()->feedScrollList(mList);
    }
    void delUnit(const LLUUID& tracking_id) override
    {
        LLLocalMeshMgr::getInstance()->delUnit(tracking_id);
    }
    void loadPath(const std::string& path) override
    {
        // Decode with the joint-position-override flag the artist saved for this file.
        LLLocalMeshMgr::getInstance()->addUnit(path, LLLocalAssetPaths::getInstance()->getMeshJoints(path));
    }
    LLUUID unitForPath(const std::string& path) const override
    {
        return LLLocalMeshMgr::getInstance()->getUnitID(path);
    }
    std::string pathForUnit(const LLUUID& tracking_id) const override
    {
        return LLLocalMeshMgr::getInstance()->getFilename(tracking_id);
    }
    std::string iconName() const override
    {
        return LLInventoryIcon::getIconName(LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT);
    }
    LLLocalAssetPaths::EType assetType() const override { return LLLocalAssetPaths::TYPE_MESH; }
    LLFilePicker::ELoadFilter getLoadFilter() const override { return LLFilePicker::FFLOAD_MODEL; }
    std::string emptyHint() const override { return getString("empty_hint_mesh"); }
    boost::signals2::connection connectChanged(const std::function<void()>& cb) override
    {
        return LLLocalMeshMgr::getInstance()->setUnitsChangedCallback(cb);
    }

    void initExtraButtons() override;
    void updateExtraButtons(bool has_selection) override;

private:
    void onRez();
    void onAttach();
    void onSelect();
    void onToggleJoints();
    void onRowRightClick(S32 x, S32 y);
    void populateAttachPoints();
    void refreshActionButtons();
    S32  getComboAttachPoint() const;

    LLButton*          mRezBtn { nullptr };
    LLButton*          mSelectBtn { nullptr };
    LLButton*          mAttachBtn { nullptr };
    LLComboBox*        mAttachCombo { nullptr };
    LLCheckBoxCtrl*    mJointsCheck { nullptr };
    LLListContextMenu* mRowMenu { nullptr };
};

// Right-click menu for a decoded mesh row: Rez, Attach To > (points), Detach, Delete.
// Built in code (the attach-point submenu is per-avatar dynamic) but wired the
// blessed way -- a ScopedRegistrar binds the menu's function names to this panel.
class LLLocalMeshRowMenu final : public LLListContextMenu
{
public:
    explicit LLLocalMeshRowMenu(LLPanelLocalMesh* panel) : mPanel(panel) {}

protected:
    LLContextMenu* createMenu() override
    {
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar reg;
        LLUICtrl::EnableCallbackRegistry::ScopedRegistrar ereg;
        const LLUUID id = mUUIDs.empty() ? LLUUID::null : mUUIDs.front();

        reg.add("LocalMesh.Spawn",  boost::bind(&LLPanelLocalMesh::doSpawn,    mPanel, id));
        reg.add("LocalMesh.Derez",  boost::bind(&LLPanelLocalMesh::doDerez,    mPanel, id));
        reg.add("LocalMesh.Select", boost::bind(&LLPanelLocalMesh::doSelect,   mPanel, id));
        reg.add("LocalMesh.Attach", boost::bind(&LLPanelLocalMesh::menuAttach, mPanel, id, _2));
        reg.add("LocalMesh.Detach", boost::bind(&LLPanelLocalMesh::doDetach,   mPanel, id));
        reg.add("LocalMesh.Delete", boost::bind(&LLPanelLocalMesh::doDelete,   mPanel, id));
        ereg.add("LocalMesh.IsAttached", boost::bind(&LLPanelLocalMesh::isUnitAttached, mPanel, id));
        ereg.add("LocalMesh.IsSpawned",  boost::bind(&LLPanelLocalMesh::isUnitSpawned,  mPanel, id));

        LLContextMenu* menu = createFromFile("menu_local_mesh.xml");
        if (!menu)
        {
            return nullptr;
        }

        // Fill the (empty in XUI) "Attach To" submenu with this avatar's points,
        // ordered by attachment-point id -- the same id render order sorts by.
        LLMenuGL* submenu = menu->findChildMenuByName("attach_to", true);
        if (submenu && isAgentAvatarValid())
        {
            for (const auto& pair : gAgentAvatarp->mAttachmentPoints)
            {
                LLViewerJointAttachment* attachment = pair.second;
                if (!attachment || attachment->getIsHUDAttachment())
                {
                    continue;
                }
                LLMenuItemCallGL::Params p;
                const std::string label = llformat("%s (%d)", attachment->getName().c_str(), pair.first);
                p.name = label;
                p.label = label;
                p.on_click.function_name = "LocalMesh.Attach";
                p.on_click.parameter = (S32)pair.first;
                submenu->addChild(LLUICtrlFactory::create<LLMenuItemCallGL>(p));
            }
        }
        return menu;
    }

private:
    LLPanelLocalMesh* mPanel;
};

LLPanelLocalMesh::~LLPanelLocalMesh()
{
    delete mRowMenu;
}

void LLPanelLocalMesh::initExtraButtons()
{
    // Mesh rows carry a Status column (rezzed / attached + point). The shared XUI
    // gives every tab just icon + name, so rebuild this one list's columns to add it.
    mList->clearColumns();
    {
        LLScrollListColumn::Params c;
        c.name = "icon";
        c.width.pixel_width = 20;
        mList->addColumn(c);
    }
    {
        LLScrollListColumn::Params c;
        c.name = "unit_name";
        c.header.label = getString("col_name");
        c.width.relative_width = 1.f;
        mList->addColumn(c);
    }
    {
        LLScrollListColumn::Params c;
        c.name = "status";
        c.header.label = getString("col_status");
        c.width.pixel_width = 120;
        mList->addColumn(c);
    }

    mRezBtn      = getChild<LLButton>("spawn_btn");
    mSelectBtn   = getChild<LLButton>("select_btn");
    mAttachBtn   = getChild<LLButton>("attach_btn");
    mAttachCombo = getChild<LLComboBox>("attach_point_combo");
    mJointsCheck = getChild<LLCheckBoxCtrl>("include_joints_check");

    mRezBtn->setVisible(true);
    mSelectBtn->setVisible(true);
    mAttachBtn->setVisible(true);
    mAttachCombo->setVisible(true);
    mJointsCheck->setVisible(true);

    mRezBtn->setCommitCallback(boost::bind(&LLPanelLocalMesh::onRez, this));
    mSelectBtn->setCommitCallback(boost::bind(&LLPanelLocalMesh::onSelect, this));
    mAttachBtn->setCommitCallback(boost::bind(&LLPanelLocalMesh::onAttach, this));
    mJointsCheck->setCommitCallback(boost::bind(&LLPanelLocalMesh::onToggleJoints, this));

    mRowMenu = new LLLocalMeshRowMenu(this);
    mList->setRightMouseDownCallback(boost::bind(&LLPanelLocalMesh::onRowRightClick, this, _2, _3));

    populateAttachPoints();
}

void LLPanelLocalMesh::populateAttachPoints()
{
    // The floater can outlive a logout or be opened before login; (re)fill the
    // combo the first time the agent avatar is available.
    if (!mAttachCombo || !isAgentAvatarValid() || mAttachCombo->getItemCount() > 0)
    {
        return;
    }

    // mAttachmentPoints is keyed (and thus iterated) by attachment-point id, the
    // same id render order is sorted by.
    for (const auto& pair : gAgentAvatarp->mAttachmentPoints)
    {
        LLViewerJointAttachment* attachment = pair.second;
        if (!attachment || attachment->getIsHUDAttachment())
        {
            continue;
        }
        const std::string label = llformat("%s (%d)", attachment->getName().c_str(), pair.first);
        mAttachCombo->add(label, LLSD((S32)pair.first));
    }
    mAttachCombo->selectByValue(LLSD((S32)1)); // default to chest
}

void LLPanelLocalMesh::updateExtraButtons(bool has_selection)
{
    populateAttachPoints();
    const LLUUID id = getSelectedID(); // null when an undecoded row is selected
    const bool loaded = id.notNull();
    const bool can_attach = loaded && isAgentAvatarValid() &&
                            mAttachCombo && mAttachCombo->getItemCount() > 0;
    if (mAttachBtn)   { mAttachBtn->setEnabled(can_attach); }
    if (mAttachCombo) { mAttachCombo->setEnabled(can_attach); }
    if (mJointsCheck)
    {
        mJointsCheck->setEnabled(loaded);
        mJointsCheck->set(loaded && LLLocalMeshMgr::getInstance()->getIncludeJointPositions(id));
    }
    refreshActionButtons();
}

void LLPanelLocalMesh::refreshActionButtons()
{
    const bool has_selection = mList && !mList->getAllSelected().empty();
    const LLUUID id = getSelectedID();
    const bool spawned = id.notNull() && LLLocalMeshMgr::getInstance()->getSpawnedRoot(id) != nullptr;
    if (mRezBtn)
    {
        // Rez works on a decoded unit or (auto-loading) an undecoded one; once it's
        // in-world the same button becomes Derez.
        mRezBtn->setEnabled(has_selection);
        mRezBtn->setLabel(getString(spawned ? "derez_label" : "rez_label"));
    }
    if (mSelectBtn)
    {
        mSelectBtn->setEnabled(spawned);
    }
}

S32 LLPanelLocalMesh::getComboAttachPoint() const
{
    return mAttachCombo ? mAttachCombo->getValue().asInteger() : 0;
}

void LLPanelLocalMesh::onRez()
{
    const LLUUID id = getSelectedID();
    if (id.notNull())
    {
        // Decoded: Rez, or Derez if already in-world.
        if (LLLocalMeshMgr::getInstance()->getSpawnedRoot(id))
        {
            doDerez(id);
        }
        else
        {
            doSpawn(id);
        }
        return;
    }
    // Undecoded: load it and rez once it finishes (addAndSpawn handles the async load).
    const std::string path = getSelectedPath();
    if (!path.empty())
    {
        LLLocalMeshMgr::getInstance()->addAndSpawn(std::vector<std::string>(1, path));
    }
}

void LLPanelLocalMesh::onAttach()
{
    doAttach(getSelectedID(), getComboAttachPoint());
}

void LLPanelLocalMesh::onSelect()
{
    doSelect(getSelectedID());
}

void LLPanelLocalMesh::onToggleJoints()
{
    if (!mJointsCheck)
    {
        return;
    }
    const bool include = mJointsCheck->get();
    for (const LLUUID& id : getSelectedIDs())
    {
        LLLocalMeshMgr::getInstance()->setIncludeJointPositions(id, include);
    }
}

void LLPanelLocalMesh::onRowRightClick(S32 x, S32 y)
{
    if (!mList || !mRowMenu)
    {
        return;
    }
    mList->selectItemAt(x, y, MASK_NONE); // also refreshes the side buttons
    const LLUUID id = getSelectedID();
    if (id.isNull())
    {
        return; // undecoded row: use Rez (auto-loads) or double-click to load first
    }
    uuid_vec_t ids;
    ids.push_back(id);
    mRowMenu->show(mList, ids, x, y);
}

void LLPanelLocalMesh::doSpawn(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        LLLocalMeshMgr::getInstance()->spawnInWorld(tracking_id); // units-changed signal -> refresh()
    }
}

void LLPanelLocalMesh::doDerez(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        LLLocalMeshMgr::getInstance()->despawn(tracking_id); // out of world, keep the file; signal -> refresh()
    }
}

void LLPanelLocalMesh::doAttach(const LLUUID& tracking_id, S32 attach_point)
{
    if (tracking_id.isNull())
    {
        return;
    }
    LLLocalMeshMgr* mgr = LLLocalMeshMgr::getInstance();
    LLViewerObject* root = mgr->getSpawnedRoot(tracking_id);
    if (!root)
    {
        root = mgr->spawnInWorld(tracking_id); // not in-world yet: rez it, then wear it
    }
    if (root)
    {
        mgr->attachPreviewToAvatar(root, attach_point); // if it spawned, the signal refreshes us
    }
}

void LLPanelLocalMesh::doDetach(const LLUUID& tracking_id)
{
    LLLocalMeshMgr* mgr = LLLocalMeshMgr::getInstance();
    if (LLViewerObject* root = mgr->getSpawnedRoot(tracking_id))
    {
        mgr->detachPreviewFromAvatar(root);
    }
}

void LLPanelLocalMesh::doDelete(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        LLLocalAssetPaths::getInstance()->removePath(LLLocalAssetPaths::TYPE_MESH,
                                                     pathForUnit(tracking_id));
        delUnit(tracking_id); // units-changed signal -> refresh()
    }
}

bool LLPanelLocalMesh::isUnitAttached(const LLUUID& tracking_id) const
{
    LLLocalMeshMgr* mgr = LLLocalMeshMgr::getInstance();
    LLViewerObject* root = mgr->getSpawnedRoot(tracking_id);
    return root && mgr->isPreviewAttached(root);
}

bool LLPanelLocalMesh::isUnitSpawned(const LLUUID& tracking_id) const
{
    return LLLocalMeshMgr::getInstance()->getSpawnedRoot(tracking_id) != nullptr;
}

void LLPanelLocalMesh::doSelect(const LLUUID& tracking_id)
{
    LLViewerObject* root = LLLocalMeshMgr::getInstance()->getSpawnedRoot(tracking_id);
    if (!root)
    {
        return;
    }
    // Select the linkset in-world and point the camera at it.
    LLSelectMgr::getInstance()->deselectAll();
    LLSelectMgr::getInstance()->selectObjectAndFamily(root);
    gAgentCamera.setFocusOnAvatar(false, false);
    gAgentCamera.setFocusGlobal(root->getPositionGlobal(), root->getID());
}

// ============================================================================
//  Animations tab -- Play/Stop on the user's avatar or the selected animesh.
// ============================================================================
class LLPanelLocalAnim final : public LLPanelLocalAssetBase
{
public:
    void draw() override;

protected:
    void feedList() override
    {
        LLLocalAnimMgr::getInstance()->feedScrollList(mList);
    }
    void delUnit(const LLUUID& tracking_id) override
    {
        LLLocalAnimMgr::getInstance()->delUnit(tracking_id);
    }
    void loadPath(const std::string& path) override
    {
        LLLocalAnimMgr::getInstance()->addUnit(path);
    }
    LLUUID unitForPath(const std::string& path) const override
    {
        return LLLocalAnimMgr::getInstance()->getUnitID(path);
    }
    std::string pathForUnit(const LLUUID& tracking_id) const override
    {
        return LLLocalAnimMgr::getInstance()->getFilename(tracking_id);
    }
    std::string iconName() const override
    {
        return LLInventoryIcon::getIconName(LLAssetType::AT_ANIMATION, LLInventoryType::IT_ANIMATION);
    }
    LLLocalAssetPaths::EType assetType() const override { return LLLocalAssetPaths::TYPE_ANIM; }
    LLFilePicker::ELoadFilter getLoadFilter() const override { return LLFilePicker::FFLOAD_ANIM; }
    std::string emptyHint() const override { return getString("empty_hint_anim"); }
    boost::signals2::connection connectChanged(const std::function<void()>& cb) override
    {
        return LLLocalAnimMgr::getInstance()->setUnitsChangedCallback(cb);
    }

    void initExtraButtons() override;
    void updateExtraButtons(bool has_selection) override;

private:
    void onPlay();
    void onStop();
    void refreshPlayStop();
    // The avatar Play/Stop act on: the user's own avatar, or the selected in-world
    // animesh's control avatar, per the target combo.
    LLVOAvatar* getTargetAvatar() const;

    enum ETarget { TARGET_SELF = 0, TARGET_SELECTED = 1 };

    LLComboBox* mTargetCombo { nullptr };
    LLButton*   mPlayBtn { nullptr };
    LLButton*   mStopBtn { nullptr };
};

void LLPanelLocalAnim::initExtraButtons()
{
    mTargetCombo = getChild<LLComboBox>("anim_target_combo");
    mPlayBtn     = getChild<LLButton>("play_btn");
    mStopBtn     = getChild<LLButton>("stop_btn");

    mTargetCombo->setVisible(true);
    mTargetCombo->setEnabled(true); // a mode selector -- always usable
    mPlayBtn->setVisible(true);
    mStopBtn->setVisible(true);

    mTargetCombo->add(getString("target_self"),     LLSD((S32)TARGET_SELF));
    mTargetCombo->add(getString("target_selected"), LLSD((S32)TARGET_SELECTED));
    mTargetCombo->selectByValue(LLSD((S32)TARGET_SELF));
    mTargetCombo->setCommitCallback(boost::bind(&LLPanelLocalAnim::refreshPlayStop, this));

    mPlayBtn->setCommitCallback(boost::bind(&LLPanelLocalAnim::onPlay, this));
    mStopBtn->setCommitCallback(boost::bind(&LLPanelLocalAnim::onStop, this));
}

LLVOAvatar* LLPanelLocalAnim::getTargetAvatar() const
{
    const S32 target = mTargetCombo ? mTargetCombo->getValue().asInteger() : (S32)TARGET_SELF;
    if (target == TARGET_SELECTED)
    {
        return get_selected_animesh_control_avatar();
    }
    return isAgentAvatarValid() ? gAgentAvatarp.get() : nullptr;
}

void LLPanelLocalAnim::refreshPlayStop()
{
    // The target avatar (self, or the selected in-world animesh) changes
    // independently of this list, so keep Play/Stop enabled state live.
    LLVOAvatar* target = getTargetAvatar();
    const bool has_anim = mList && !mList->getAllSelected().empty();
    if (mPlayBtn) { mPlayBtn->setEnabled(has_anim && target != nullptr); }
    if (mStopBtn) { mStopBtn->setEnabled(target != nullptr); }
}

void LLPanelLocalAnim::updateExtraButtons(bool /*has_selection*/)
{
    refreshPlayStop();
}

void LLPanelLocalAnim::draw()
{
    refreshPlayStop();
    LLPanel::draw();
}

void LLPanelLocalAnim::onPlay()
{
    LLVOAvatar* target = getTargetAvatar();
    if (!target)
    {
        return;
    }
    LLUUID id = getSelectedID();
    if (id.isNull())
    {
        // Undecoded selection: anim decode is synchronous, so load then play now.
        const std::string path = getSelectedPath();
        if (path.empty())
        {
            return;
        }
        loadPath(path);
        id = unitForPath(path);
    }
    if (id.notNull())
    {
        LLLocalAnimMgr::getInstance()->playOnAvatar(target, id);
    }
}

void LLPanelLocalAnim::onStop()
{
    if (LLVOAvatar* target = getTargetAvatar())
    {
        LLLocalAnimMgr::getInstance()->stopOnAvatar(target);
    }
}

// Apply a local GLTF material (world id) to the current in-world selection. Real
// objects go through the normal server path. A client-only (isLocalOnly) preview
// selection is handled locally: set the render material per selected face without a
// server round-trip, then mark the render-material param in use so it survives the
// drawable rebuild (client-only objects get no server echo to do that -- same fix as
// applyPartGeometry in lllocalmesh.cpp). Selections never mix local + real.
void apply_local_material_to_selection(const LLUUID& world_id)
{
    LLObjectSelectionHandle sel = LLSelectMgr::getInstance()->getSelection();
    bool any_local = false;
    for (LLObjectSelection::iterator it = sel->begin(); it != sel->end(); ++it)
    {
        LLViewerObject* obj = (*it)->getObject();
        if (obj && obj->isLocalOnly())
        {
            any_local = true;
            break;
        }
    }
    if (!any_local)
    {
        LLSelectMgr::getInstance()->selectionSetGLTFMaterial(world_id);
        return;
    }

    struct ApplyFunc final : public LLSelectedTEFunctor
    {
        LLUUID mId;
        bool apply(LLViewerObject* obj, S32 te) override
        {
            obj->setRenderMaterialID(te, mId, /*update_server=*/false, /*local_origin=*/true);
            return true;
        }
    } func;
    func.mId = world_id;
    sel->applyToTEs(&func);

    for (LLObjectSelection::iterator it = sel->begin(); it != sel->end(); ++it)
    {
        LLViewerObject* obj = (*it)->getObject();
        if (obj && obj->isLocalOnly())
        {
            obj->setHasRenderMaterialParams(true);
        }
    }
}

// ============================================================================
//  Apply-to-face base -- shared by the Textures and Materials tabs. Reuses the
//  hidden "spawn_btn" side slot as an "Apply to Face" button that applies the
//  selected local asset to the current in-world face selection.
// ============================================================================
class LLPanelLocalApplyAsset : public LLPanelLocalAssetBase
{
protected:
    virtual std::string applyLabel() = 0;                       // button label
    virtual LLUUID      worldIdFor(const LLUUID& tracking_id) = 0; // unit -> world id
    virtual void        applyWorldId(const LLUUID& world_id) = 0;  // apply to selection

    void initExtraButtons() override
    {
        mApplyBtn = getChild<LLButton>("spawn_btn"); // per-tab instance; reuse the slot
        mApplyBtn->setLabel(applyLabel());
        mApplyBtn->setVisible(true);
        mApplyBtn->setCommitCallback(boost::bind(&LLPanelLocalApplyAsset::onApply, this));
    }
    void updateExtraButtons(bool has_selection) override
    {
        if (mApplyBtn)
        {
            // Need both a chosen asset row and an in-world selection to apply to.
            const bool has_target = LLSelectMgr::getInstance()->getSelection()->getNumNodes() > 0;
            mApplyBtn->setEnabled(has_selection && has_target);
        }
    }
    void draw() override
    {
        // In-world selection changes independently of the list, so keep this live.
        updateExtraButtons(mList && mList->getFirstSelected() != nullptr);
        LLPanel::draw();
    }

private:
    void onApply()
    {
        LLUUID id = getSelectedID();
        if (id.isNull())
        {
            // Undecoded row: decode it (bitmap/material loads are synchronous), then apply.
            const std::string path = getSelectedPath();
            if (path.empty())
            {
                return;
            }
            loadPath(path);
            id = unitForPath(path);
        }
        if (id.isNull())
        {
            return;
        }
        const LLUUID world_id = worldIdFor(id);
        if (world_id.notNull())
        {
            applyWorldId(world_id);
        }
    }

    LLButton* mApplyBtn { nullptr };
};

// ============================================================================
//  Textures tab -- list + "Apply to Face" (applies a local texture to selection).
// ============================================================================
class LLPanelLocalTexture final : public LLPanelLocalApplyAsset
{
protected:
    void feedList() override
    {
        LLLocalBitmapMgr::getInstance()->feedScrollList(mList);
    }
    void delUnit(const LLUUID& tracking_id) override
    {
        LLLocalBitmapMgr::getInstance()->delUnit(tracking_id);
    }
    void loadPath(const std::string& path) override
    {
        LLLocalBitmapMgr::getInstance()->addUnit(path);
    }
    LLUUID unitForPath(const std::string& path) const override
    {
        return LLLocalBitmapMgr::getInstance()->getUnitID(path);
    }
    std::string pathForUnit(const LLUUID& tracking_id) const override
    {
        return LLLocalBitmapMgr::getInstance()->getFilename(tracking_id);
    }
    std::string iconName() const override
    {
        return LLInventoryIcon::getIconName(LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE);
    }
    LLLocalAssetPaths::EType assetType() const override { return LLLocalAssetPaths::TYPE_TEXTURE; }
    LLFilePicker::ELoadFilter getLoadFilter() const override { return LLFilePicker::FFLOAD_IMAGE; }
    std::string emptyHint() const override { return getString("empty_hint_tex"); }
    boost::signals2::connection connectChanged(const std::function<void()>& cb) override
    {
        return LLLocalBitmapMgr::getInstance()->setUnitsChangedCallback(cb);
    }

    std::string applyLabel() override { return getString("apply_texture_label"); }
    LLUUID worldIdFor(const LLUUID& tracking_id) override
    {
        return LLLocalBitmapMgr::getInstance()->getWorldID(tracking_id);
    }
    void applyWorldId(const LLUUID& world_id) override
    {
        // setTEImage + sendTEUpdate; sendTEUpdate is isLocalOnly-guarded, so this is
        // safe on both real objects and client-only previews.
        LLSelectMgr::getInstance()->selectionSetImage(world_id);
    }
};

// ============================================================================
//  GLTF Materials tab -- one row per material (a .gltf can hold several).
//  List + "Apply to Face" (applies a local material to the in-world selection).
// ============================================================================
class LLPanelLocalMaterial final : public LLPanelLocalApplyAsset
{
protected:
    void feedList() override
    {
        LLLocalGLTFMaterialMgr::getInstance()->feedScrollList(mList);
    }
    void delUnit(const LLUUID& tracking_id) override
    {
        LLLocalGLTFMaterialMgr::getInstance()->delUnit(tracking_id);
    }
    void loadPath(const std::string& path) override
    {
        LLLocalGLTFMaterialMgr::getInstance()->addUnit(path);
    }
    LLUUID unitForPath(const std::string& path) const override
    {
        // A file holds >= 1 material; treat it as loaded if its first material is.
        return LLLocalGLTFMaterialMgr::getInstance()->getUnitID(path, 0);
    }
    std::string pathForUnit(const LLUUID& tracking_id) const override
    {
        std::string filename;
        S32 index = 0;
        LLLocalGLTFMaterialMgr::getInstance()->getFilenameAndIndex(tracking_id, filename, index);
        return filename;
    }
    std::string iconName() const override
    {
        return LLInventoryIcon::getIconName(LLAssetType::AT_MATERIAL, LLInventoryType::IT_MATERIAL);
    }
    LLLocalAssetPaths::EType assetType() const override { return LLLocalAssetPaths::TYPE_MATERIAL; }
    LLFilePicker::ELoadFilter getLoadFilter() const override { return LLFilePicker::FFLOAD_MATERIAL; }
    std::string emptyHint() const override { return getString("empty_hint_mat"); }
    boost::signals2::connection connectChanged(const std::function<void()>& cb) override
    {
        return LLLocalGLTFMaterialMgr::getInstance()->setUnitsChangedCallback(cb);
    }

    std::string applyLabel() override { return getString("apply_material_label"); }
    LLUUID worldIdFor(const LLUUID& tracking_id) override
    {
        return LLLocalGLTFMaterialMgr::getInstance()->getWorldID(tracking_id);
    }
    void applyWorldId(const LLUUID& world_id) override
    {
        apply_local_material_to_selection(world_id);
    }
};

// Build a panel from XUI and add it as a tab.
LLPanelLocalAssetBase* add_asset_tab(LLTabContainer* tabs, LLPanelLocalAssetBase* panel,
                                     const std::string& name, const std::string& label,
                                     const std::string& xml, bool select)
{
    panel->buildFromFile(xml);
    panel->setName(name); // AFTER build: the shared XML's name would otherwise clobber it,
                          // leaving all tabs identically named (breaks getPanelByName routing)
    tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(panel).label(label).select_tab(select));
    return panel;
}

} // anonymous namespace

// ============================================================================
//  LLFloaterLocalAssets
// ============================================================================
LLFloaterLocalAssets::LLFloaterLocalAssets(const LLSD& key)
:   LLFloater(key)
{
}

LLFloaterLocalAssets::~LLFloaterLocalAssets()
{
}

bool LLFloaterLocalAssets::postBuild()
{
    mTabs = getChild<LLTabContainer>("asset_tabs");

    add_asset_tab(mTabs, new LLPanelLocalMesh(),     "mesh_tab", getString("tab_mesh"),
                  "panel_local_asset_list.xml", true);
    add_asset_tab(mTabs, new LLPanelLocalAnim(),     "anim_tab", getString("tab_anim"),
                  "panel_local_asset_list.xml", false);
    add_asset_tab(mTabs, new LLPanelLocalTexture(),  "tex_tab",  getString("tab_textures"),
                  "panel_local_asset_list.xml", false);
    add_asset_tab(mTabs, new LLPanelLocalMaterial(), "mat_tab",  getString("tab_materials"),
                  "panel_local_asset_list.xml", false);

    return true;
}

void LLFloaterLocalAssets::dropFiles(const std::vector<std::string>& paths)
{
    if (!mTabs)
    {
        return;
    }
    for (const std::string& path : paths)
    {
        std::string ext = gDirUtilp->getExtension(path);
        LLStringUtil::toLower(ext);

        std::string tab_name;
        if (ext == "dae")
        {
            tab_name = "mesh_tab";
        }
        else if (ext == "gltf" || ext == "glb")
        {
            // A glTF can be a mesh or a material: honor the active tab if it's one of
            // those, else default to Mesh.
            LLPanel* cur = mTabs->getCurrentPanel();
            tab_name = (cur && cur->getName() == "mat_tab") ? "mat_tab" : "mesh_tab";
        }
        else if (ext == "bvh" || ext == "anim")
        {
            tab_name = "anim_tab";
        }
        else if (ext == "bmp" || ext == "jpg" || ext == "jpeg" || ext == "png" ||
                 ext == "tga" || ext == "webp" || ext == "avif" || ext == "j2c" || ext == "jp2")
        {
            tab_name = "tex_tab";
        }
        else
        {
            continue; // not something the Local Assets tabs handle
        }

        // The tab panels are LLPanelLocalAssetBase (anon-namespace, visible here).
        if (LLPanelLocalAssetBase* panel = dynamic_cast<LLPanelLocalAssetBase*>(mTabs->getPanelByName(tab_name)))
        {
            mTabs->selectTabPanel(panel);
            panel->loadFile(path); // decode + add (+ persist) via the manager
        }
    }
}

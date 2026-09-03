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
#include "llviewermenufile.h"   // LLFilePickerReplyThread, upload_single_file
#include "llfloaterreg.h"       // LLFloaterReg::showInstance (mesh Upload)
#include "llfloatermodelpreview.h" // mesh Upload -> the Model upload floater
#include "llmodel.h"            // LLModel::LOD_HIGH

#include "llagentcamera.h"
#include "llcontrolavatar.h"
#include "llgltfmateriallist.h" // LLGLTFMaterialList::flushUpdates (apply local material)
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
#include "llviewertexture.h"    // LLViewerTextureManager::getFetchedTexture (apply local texture)
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
    AL_VIEW_TYPE(LLPanelLocalAssetBase, LLPanel);
    bool postBuild() override;

    // Rebuild the visible list (decoded units + dimmed undecoded saved paths).
    void refresh() override;

    // Decode + add a file into this tab's backing manager (used by the floater's
    // OS drag-and-drop routing). Public wrapper over the protected loadPath(); the
    // backing manager dedups, so a file that's already loaded is never added twice.
    void loadFile(const std::string& path) { loadPath(path); }

protected:
    // Backing-manager hooks, implemented per asset type.
    virtual void feedList() = 0;                                   // decoded units -> rows
    virtual void delUnit(const LLUUID& tracking_id) = 0;          // unload a decoded unit
    virtual void loadPath(const std::string& path) = 0;          // decode a file (lazy / add)
    virtual LLUUID unitForPath(const std::string& path) const = 0; // decoded? -> tracking id
    virtual std::string pathForUnit(const LLUUID& tracking_id) const = 0; // tracking id -> path
    // All decoded unit ids backing a saved path. Default: the single unit unitForPath()
    // resolves. The GLTF Materials panel overrides this -- one .gltf file can decode to
    // several material units, and removing its row must unload all of them.
    virtual void unitsForPath(const std::string& path, std::vector<LLUUID>& out) const
    {
        const LLUUID id = unitForPath(path);
        if (id.notNull()) { out.push_back(id); }
    }
    virtual std::string iconName() const = 0;                    // row icon for this type
    virtual LLLocalAssetPaths::EType assetType() const = 0;
    virtual LLFilePicker::ELoadFilter getLoadFilter() const = 0;
    // Subscribe to the backing manager's "units changed" signal for reactive refresh.
    virtual boost::signals2::connection connectChanged(const std::function<void()>& cb) = 0;
    // Send the file at `path` through the viewer's standard upload flow for this type.
    virtual void doUpload(const std::string& path) = 0;

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
    LLButton*         mUnloadBtn { nullptr }; // free the asset, keep the saved path (dimmed row)
    LLButton*         mRemoveBtn { nullptr }; // forget the file entirely (row disappears)
    LLButton*         mUploadBtn { nullptr }; // upload the selected file to Second Life

private:
    void appendUnloaded();
    void selectByPath(const std::string& path);
    bool anySelectedMeshOwned() const; // a model-loaded (read-only) row is selected
    void onAddBtn();
    void onUnloadBtn();
    void onRemoveBtn();
    void onUploadBtn();
    void onDoubleClick();
    void onSelectionChange();
    static void onFilesPicked(const std::vector<std::string>& filenames,
                              LLHandle<LLPanelLocalAssetBase> handle);

    boost::signals2::scoped_connection mChangedConn;
};

bool LLPanelLocalAssetBase::postBuild()
{
    mList      = getChild<LLScrollListCtrl>("l_name_list");
    mAddBtn    = getChild<LLButton>("add_btn");
    mUnloadBtn = getChild<LLButton>("unload_btn");
    mRemoveBtn = getChild<LLButton>("remove_btn");
    mUploadBtn = getChild<LLButton>("upload_btn");

    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onSelectionChange, this));
    mList->setDoubleClickCallback(boost::bind(&LLPanelLocalAssetBase::onDoubleClick, this));
    mAddBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onAddBtn, this));
    mUnloadBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onUnloadBtn, this));
    mRemoveBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onRemoveBtn, this));
    mUploadBtn->setCommitCallback(boost::bind(&LLPanelLocalAssetBase::onUploadBtn, this));

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

bool LLPanelLocalAssetBase::anySelectedMeshOwned() const
{
    if (mList)
    {
        for (LLScrollListItem* item : mList->getAllSelected())
        {
            if (item && item->getValue()["mesh_owned"].asBoolean())
            {
                return true; // a model-loaded (read-only) row is in the selection
            }
        }
    }
    return false;
}

void LLPanelLocalAssetBase::onSelectionChange()
{
    const bool has_selection = mList && !mList->getAllSelected().empty();
    // Unload only makes sense for a decoded row (an undecoded one is already
    // unloaded); Remove forgets the saved path, so it works on either.
    const bool has_decoded = !getSelectedIDs().empty();
    // Model-loaded (mesh-owned) rows are read-only -- they belong to the mesh that
    // imported them, so block Unload/Remove while one is selected.
    const bool read_only = anySelectedMeshOwned();
    if (mUnloadBtn)
    {
        mUnloadBtn->setEnabled(has_decoded && !read_only);
    }
    if (mRemoveBtn)
    {
        mRemoveBtn->setEnabled(has_selection && !read_only);
    }
    if (mUploadBtn)
    {
        // Upload acts on the selected file (decoded or not), so a row is all it needs.
        mUploadBtn->setEnabled(has_selection);
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

void LLPanelLocalAssetBase::onUnloadBtn()
{
    if (!mList)
    {
        return;
    }
    // Free each selected decoded unit but keep its saved path, so the row stays in
    // the list as a dimmed, reloadable entry. For a mesh this also derezzes its
    // in-world copies. delUnit() fires the manager signal, which refreshes us; an
    // undecoded selection has no unit to unload. Snapshot first -- delUnit() frees
    // the LLScrollListItems we'd otherwise be iterating.
    const std::vector<LLUUID> ids = getSelectedIDs();
    for (const LLUUID& id : ids)
    {
        delUnit(id);
    }
}

void LLPanelLocalAssetBase::onUploadBtn()
{
    // Upload the selected file through the standard per-type upload flow (with its
    // L$ cost confirmation). Works on a decoded or saved-but-undecoded row -- it's
    // the file on disk we upload, not the in-memory preview.
    const std::string path = getSelectedPath();
    if (!path.empty())
    {
        doUpload(path);
    }
}

void LLPanelLocalAssetBase::onRemoveBtn()
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
        // Unload every decoded unit backing this row FIRST and forget the path
        // only once they are all gone. delUnit() fires the manager signals
        // synchronously, and the add-only LLLocalAssetPaths::onUnitsChanged()
        // re-records any path a still-loaded unit reports -- removePath() up
        // front would be undone by the very first delUnit (mesh teardown, or a
        // multi-material glTF file's surviving sibling units).
        std::vector<LLUUID> ids;
        if (!entry.first.empty())
        {
            unitsForPath(entry.first, ids); // all of the file's units, not just the row
        }
        if (ids.empty() && entry.second.notNull())
        {
            ids.push_back(entry.second);
        }
        for (const LLUUID& id : ids)
        {
            delUnit(id); // unload the decoded unit (fires the manager signal)
        }
        if (!entry.first.empty())
        {
            LLLocalAssetPaths::getInstance()->removePath(assetType(), entry.first); // forget the path
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
            // us and records the path in LLLocalAssetPaths for persistence. The
            // manager dedups, so re-picking a loaded file won't add it twice.
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
    AL_VIEW_TYPE(LLPanelLocalMesh, LLPanelLocalAssetBase);
    ~LLPanelLocalMesh() override;

    // Actions shared by the side buttons and the right-click row menu (decoded units).
    void doSpawn(const LLUUID& tracking_id);
    void doAttach(const LLUUID& tracking_id, S32 attach_point);
    void doDetach(const LLUUID& tracking_id);
    void doUnload(const LLUUID& tracking_id); // free + derez copies, keep the file in the list
    void doRemove(const LLUUID& tracking_id); // forget the file entirely
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
    void doUpload(const std::string& path) override
    {
        // Hand the file to the standard Model upload floater (LOD/physics/cost).
        if (LLFloaterModelPreview* fmp =
                dynamic_cast<LLFloaterModelPreview*>(LLFloaterReg::showInstance("upload_model")))
        {
            fmp->loadModel(LLModel::LOD_HIGH, path);
        }
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
        reg.add("LocalMesh.Unload", boost::bind(&LLPanelLocalMesh::doUnload,   mPanel, id));
        reg.add("LocalMesh.Remove", boost::bind(&LLPanelLocalMesh::doRemove,   mPanel, id));
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
        // Fill the space LEFT OVER by the fixed icon/status columns. relative_width
        // would instead claim that fraction of the WHOLE list width, pushing the
        // fixed Status column off the right edge (invisible).
        c.width.dynamic_width = true;
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
    mRezBtn->setToolTip(getString("rez_tooltip")); // spawn_btn slot is repurposed per tab
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
    // Attach works on an undecoded row too -- it loads then attaches -- so enable on
    // any selection, not just a decoded one.
    const bool can_attach = has_selection && isAgentAvatarValid() &&
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
        // Rez always spawns a NEW copy -- it no longer toggles to Derez. Copies are
        // managed per-instance (Spawned tab / in-world Delete) or via Derez All.
        mRezBtn->setEnabled(has_selection);
        mRezBtn->setLabel(getString("rez_label"));
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
        doSpawn(id); // always rez a new copy
        return;
    }
    // Undecoded: load it and rez once it finishes (addAndSpawn handles the async
    // load). Decode with the joint-position flag the artist saved for this file,
    // like loadPath -- defaulting it would also make onUnitsChanged erase the
    // saved flag.
    const std::string path = getSelectedPath();
    if (!path.empty())
    {
        LLLocalMeshMgr::getInstance()->addAndSpawn(std::vector<std::string>(1, path),
                                                   LLLocalAssetPaths::getInstance()->getMeshJoints(path));
    }
}

void LLPanelLocalMesh::onAttach()
{
    const LLUUID id = getSelectedID();
    if (id.notNull())
    {
        doAttach(id, getComboAttachPoint());
        return;
    }
    // Undecoded row: load it and attach once it finishes loading (mirrors how Rez
    // handles an undecoded row via addAndSpawn), honoring the saved joint flag.
    const std::string path = getSelectedPath();
    if (!path.empty())
    {
        LLLocalMeshMgr::getInstance()->addAndAttach(path, getComboAttachPoint(),
                                                    LLLocalAssetPaths::getInstance()->getMeshJoints(path));
    }
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
    // Attach always wears a fresh copy (Rez and Attach both spawn a new instance now);
    // wearing a specific already-rezzed copy is a per-instance op on the Spawned tab.
    if (LLViewerObject* root = mgr->spawnInWorld(tracking_id))
    {
        mgr->attachPreviewToAvatar(root, attach_point); // the spawn signal refreshes us
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

void LLPanelLocalMesh::doUnload(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        // Free the unit (and derez its in-world copies) but keep the saved path, so
        // the mesh stays in the list as a dimmed, reloadable entry.
        delUnit(tracking_id); // units-changed signal -> refresh()
    }
}

void LLPanelLocalMesh::doRemove(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        // Resolve the path before the unit dies, but forget it only AFTER
        // delUnit: the units-changed listeners fire during teardown and the
        // add-only LLLocalAssetPaths::onUnitsChanged would re-record a path
        // removed up front (see onRemoveBtn).
        const std::string path = pathForUnit(tracking_id);
        delUnit(tracking_id); // units-changed signal -> refresh()
        LLLocalAssetPaths::getInstance()->removePath(LLLocalAssetPaths::TYPE_MESH, path);
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
    // Select the linkset and open Build to edit it. Deliberately does NOT move the
    // camera (artists found Select yanking the view jarring); framing a copy is the
    // separate Focus Camera action on the Spawned tab.
    LLSelectMgr::getInstance()->deselectAll();
    LLSelectMgr::getInstance()->selectObjectAndFamily(root);
    handle_object_edit();
}

// ============================================================================
//  Animations tab -- Play/Stop on the user's avatar or the selected animesh.
// ============================================================================
class LLPanelLocalAnim final : public LLPanelLocalAssetBase
{
public:
    AL_VIEW_TYPE(LLPanelLocalAnim, LLPanelLocalAssetBase);
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
    void doUpload(const std::string& path) override
    {
        // .anim / .bvh -> the standard animation upload floater.
        upload_single_file(std::vector<std::string>(1, path), LLFilePicker::FFLOAD_ANIM, LLUUID::null);
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

// Collect the faces to apply a local asset to on one selected object. Honors an
// explicit Select-Face pick (a strict, non-empty subset of the object's faces); any
// other state -- a whole-object selection, or nothing picked -- means every face.
//
// Iterates getNumTEs() (the texture-entry count), NOT getNumFaces() (the drawable's
// *built* face count). For a freshly spawned or hot-swapped local mesh the volume
// realizes a frame or more after the TEs are set, so getNumFaces() can still read a
// placeholder count -- and applyToTEs()/selectionSetImage(), which clamp to
// llmin(getNumTEs, getNumFaces), then cover only the first face(s). That clamp is
// exactly why Apply had to be clicked twice: the first click hit one face, the apply
// forced the volume to realize, and only then did a second click reach the rest.
static void collect_apply_tes(const LLSelectNode* node, const LLViewerObject* obj, std::vector<S32>& out)
{
    out.clear();
    const S32 num = (S32)obj->getNumTEs();
    if (num <= 0)
    {
        return;
    }
    S32 picked = 0;
    for (S32 te = 0; te < num; ++te)
    {
        if (node->isTESelected(te))
        {
            ++picked;
        }
    }
    const bool subset = (picked > 0 && picked < num); // genuine per-face pick
    for (S32 te = 0; te < num; ++te)
    {
        if (!subset || node->isTESelected(te))
        {
            out.push_back(te);
        }
    }
}

// Apply a local texture (world id) to the current in-world selection -- every face
// unless specific faces are picked. Mirrors LLToolDragAndDrop::dropTextureAllFaces
// (a plain setTEImage over getNumTEs()), so it isn't subject to the getNumFaces
// clamp described on collect_apply_tes(). sendTEUpdate() is isLocalOnly-guarded, so
// this is safe on both real objects and client-only previews.
void apply_local_texture_to_selection(const LLUUID& world_id)
{
    LLViewerTexture* image = LLViewerTextureManager::getFetchedTexture(
        world_id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    LLObjectSelectionHandle sel = LLSelectMgr::getInstance()->getSelection();
    std::vector<S32> tes;
    for (LLObjectSelection::iterator it = sel->begin(); it != sel->end(); ++it)
    {
        LLSelectNode* node = *it;
        LLViewerObject* obj = node ? node->getObject() : nullptr;
        if (!obj || !obj->permModify())
        {
            continue;
        }
        collect_apply_tes(node, obj, tes);
        for (S32 te : tes)
        {
            obj->setTEImage(te, image);
        }
        obj->sendTEUpdate(); // isLocalOnly-guarded; a no-op for client-only previews
    }
}

// Apply a local GLTF material (world id) to the current in-world selection -- every
// face unless specific faces are picked. Real objects update the server (queued,
// flushed once at the end); client-only (isLocalOnly) previews update local render
// state only and then mark the render-material param in use so it survives the
// drawable rebuild (no server echo does that for them -- same fix as
// applyPartGeometry in lllocalmesh.cpp). Iterates getNumTEs() to dodge the
// getNumFaces clamp (see collect_apply_tes).
void apply_local_material_to_selection(const LLUUID& world_id)
{
    LLObjectSelectionHandle sel = LLSelectMgr::getInstance()->getSelection();
    std::vector<S32> tes;
    bool any_server = false;
    for (LLObjectSelection::iterator it = sel->begin(); it != sel->end(); ++it)
    {
        LLSelectNode* node = *it;
        LLViewerObject* obj = node ? node->getObject() : nullptr;
        if (!obj || !obj->permModify())
        {
            continue;
        }
        const bool local = obj->isLocalOnly();
        collect_apply_tes(node, obj, tes);
        // For a client-only preview, mark the render-material param IN USE *before*
        // setting per-face ids. setRenderMaterialID() creates a throwaway param block
        // with in_use=false whenever the block isn't already in use (llviewerobject
        // createNewParameterEntry), so without this each face's call would replace the
        // block and only the LAST face would keep its material -- every other face
        // renders untextured. Real objects get a per-face block from the server echo
        // instead. (Same ordering as applyPartGeometry in lllocalmesh.cpp.)
        if (local)
        {
            obj->setHasRenderMaterialParams(true);
        }
        else
        {
            any_server = true;
        }
        for (S32 te : tes)
        {
            obj->setRenderMaterialID(te, world_id, /*update_server=*/!local, /*local_origin=*/true);
        }
    }
    if (any_server)
    {
        LLGLTFMaterialList::flushUpdates();
    }
}

// ============================================================================
//  Apply-to-face base -- shared by the Textures and Materials tabs. Reuses the
//  hidden "spawn_btn" side slot as an "Apply to Face" button that applies the
//  selected local asset to the current in-world face selection.
// ============================================================================
class LLPanelLocalApplyAsset : public LLPanelLocalAssetBase
{
public:
    AL_VIEW_TYPE(LLPanelLocalApplyAsset, LLPanelLocalAssetBase);

protected:
    virtual std::string applyLabel() = 0;                       // button label
    virtual LLUUID      worldIdFor(const LLUUID& tracking_id) = 0; // unit -> world id
    virtual void        applyWorldId(const LLUUID& world_id) = 0;  // apply to selection

    void initExtraButtons() override
    {
        mApplyBtn = getChild<LLButton>("spawn_btn"); // per-tab instance; reuse the slot
        mApplyBtn->setLabel(applyLabel());
        mApplyBtn->setToolTip(getString("apply_tooltip")); // spawn_btn slot is repurposed per tab
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
            // Whole object -> all faces; specific Select-Face pick -> just those.
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
public:
    AL_VIEW_TYPE(LLPanelLocalTexture, LLPanelLocalApplyAsset);

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
        // User units only: a mesh-owned import of the same file is a distinct,
        // read-only unit this tab must neither claim as loaded nor delete.
        return LLLocalBitmapMgr::getInstance()->getUnitID(path, /*mesh_owned=*/false);
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
    void doUpload(const std::string& path) override
    {
        upload_single_file(std::vector<std::string>(1, path), LLFilePicker::FFLOAD_IMAGE, LLUUID::null);
    }

    std::string applyLabel() override { return getString("apply_texture_label"); }
    LLUUID worldIdFor(const LLUUID& tracking_id) override
    {
        return LLLocalBitmapMgr::getInstance()->getWorldID(tracking_id);
    }
    void applyWorldId(const LLUUID& world_id) override
    {
        apply_local_texture_to_selection(world_id);
    }
};

// ============================================================================
//  GLTF Materials tab -- one row per material (a .gltf can hold several).
//  List + "Apply to Face" (applies a local material to the in-world selection).
// ============================================================================
class LLPanelLocalMaterial final : public LLPanelLocalApplyAsset
{
public:
    AL_VIEW_TYPE(LLPanelLocalMaterial, LLPanelLocalApplyAsset);

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
        // A file holds >= 1 material; treat it as loaded if its first USER material
        // is (a mesh-owned import of the same file is a distinct, read-only set).
        return LLLocalGLTFMaterialMgr::getInstance()->getUnitID(path, 0, /*mesh_owned=*/false);
    }
    std::string pathForUnit(const LLUUID& tracking_id) const override
    {
        std::string filename;
        S32 index = 0;
        LLLocalGLTFMaterialMgr::getInstance()->getFilenameAndIndex(tracking_id, filename, index);
        return filename;
    }
    void unitsForPath(const std::string& path, std::vector<LLUUID>& out) const override
    {
        // One .gltf/.glb can decode to several material units; act on all of the
        // USER's. Mesh-owned imports of the same file belong to a loaded mesh and
        // must not be deleted from this tab.
        LLLocalGLTFMaterialMgr::getInstance()->getTrackingIDs(path, out, /*mesh_owned=*/false);
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
    void doUpload(const std::string& path) override
    {
        // FFLOAD_GLTF routes to LLMaterialEditor::loadMaterialFromFile (the upload path).
        upload_single_file(std::vector<std::string>(1, path), LLFilePicker::FFLOAD_GLTF, LLUUID::null);
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

// ============================================================================
//  Spawned Objects tab -- one row per rezzed copy across all meshes, with per-copy
//  Select / Derez and a Derez All. Not a file list (not an LLPanelLocalAssetBase):
//  its rows are in-world copies, fed from LLLocalMeshMgr::getSpawnedInstances(), and
//  the row value is the copy's instance id.
// ============================================================================
class LLPanelLocalSpawned final : public LLPanel
{
public:
    AL_VIEW_TYPE(LLPanelLocalSpawned, LLPanel);
    bool postBuild() override;
    void draw() override;

private:
    void   refresh() override;
    void   onSelectionChange();
    void   onSelect();
    void   onFocus();
    void   onDerez();
    void   onDerezAll();
    LLUUID firstSelectedInstance() const;

    LLScrollListCtrl* mList { nullptr };
    LLButton*         mSelectBtn { nullptr };
    LLButton*         mFocusBtn { nullptr };
    LLButton*         mDerezBtn { nullptr };
    LLButton*         mDerezAllBtn { nullptr };
    boost::signals2::scoped_connection mChangedConn;
};

bool LLPanelLocalSpawned::postBuild()
{
    mList        = getChild<LLScrollListCtrl>("spawned_list");
    mSelectBtn   = getChild<LLButton>("select_btn");
    mFocusBtn    = getChild<LLButton>("focus_btn");
    mDerezBtn    = getChild<LLButton>("derez_btn");
    mDerezAllBtn = getChild<LLButton>("derez_all_btn");

    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback(boost::bind(&LLPanelLocalSpawned::onSelectionChange, this));
    mList->setDoubleClickCallback(boost::bind(&LLPanelLocalSpawned::onSelect, this));
    mSelectBtn->setCommitCallback(boost::bind(&LLPanelLocalSpawned::onSelect, this));
    mFocusBtn->setCommitCallback(boost::bind(&LLPanelLocalSpawned::onFocus, this));
    mDerezBtn->setCommitCallback(boost::bind(&LLPanelLocalSpawned::onDerez, this));
    mDerezAllBtn->setCommitCallback(boost::bind(&LLPanelLocalSpawned::onDerezAll, this));

    // Reactive: the mesh manager signals on any spawn / despawn / attach change.
    mChangedConn = LLLocalMeshMgr::getInstance()->setUnitsChangedCallback(
        boost::bind(&LLPanelLocalSpawned::refresh, this));

    refresh();
    return true;
}

void LLPanelLocalSpawned::refresh()
{
    if (!mList)
    {
        return;
    }
    const LLUUID prev = firstSelectedInstance();
    mList->clearRows();

    LLLocalMeshMgr* mgr = LLLocalMeshMgr::getInstance();
    const std::string icon = LLInventoryIcon::getIconName(LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT);
    for (const LLLocalMeshMgr::SpawnedInstance& inst : mgr->getSpawnedInstances())
    {
        LLLocalMesh* unit = mgr->getUnit(inst.mTrackingID);

        LLSD element;
        element["columns"][0]["column"] = "icon";
        element["columns"][0]["type"]   = "icon";
        element["columns"][0]["value"]  = icon;
        element["columns"][1]["column"] = "unit_name";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = unit ? unit->getShortName() : LLStringUtil::null;
        element["columns"][2]["column"] = "status";
        element["columns"][2]["type"]   = "text";
        element["columns"][2]["value"]  = mgr->statusText(inst.mRoot);

        element["value"] = inst.mInstanceID; // identify the row by its copy
        mList->addElement(element);
    }

    if (prev.notNull())
    {
        mList->selectByValue(LLSD(prev));
    }
    onSelectionChange();
    mList->setCommentText(mList->getItemCount() == 0 ? getString("empty_hint") : LLStringUtil::null);
}

void LLPanelLocalSpawned::onSelectionChange()
{
    const bool has_sel = mList && !mList->getAllSelected().empty();
    const bool any     = mList && mList->getItemCount() > 0;
    if (mSelectBtn)   { mSelectBtn->setEnabled(has_sel); }
    if (mFocusBtn)    { mFocusBtn->setEnabled(has_sel); }
    if (mDerezBtn)    { mDerezBtn->setEnabled(has_sel); }
    if (mDerezAllBtn) { mDerezAllBtn->setEnabled(any); }
}

void LLPanelLocalSpawned::draw()
{
    // In-world derez (the Delete key) changes the set independently of this list,
    // so keep the buttons' enabled state live.
    onSelectionChange();
    LLPanel::draw();
}

LLUUID LLPanelLocalSpawned::firstSelectedInstance() const
{
    if (mList)
    {
        if (LLScrollListItem* item = mList->getFirstSelected())
        {
            return item->getValue().asUUID();
        }
    }
    return LLUUID::null;
}

void LLPanelLocalSpawned::onSelect()
{
    LLViewerObject* root = LLLocalMeshMgr::getInstance()->getInstanceRoot(firstSelectedInstance());
    if (!root)
    {
        return;
    }
    // Select + open Build to edit. Deliberately does NOT move the camera; framing the
    // copy is the separate Focus Camera action below (artists dislike Select yanking
    // the view, especially when picking through copies).
    LLSelectMgr::getInstance()->deselectAll();
    LLSelectMgr::getInstance()->selectObjectAndFamily(root);
    handle_object_edit();
}

void LLPanelLocalSpawned::onFocus()
{
    LLViewerObject* root = LLLocalMeshMgr::getInstance()->getInstanceRoot(firstSelectedInstance());
    if (!root)
    {
        return;
    }
    // Frame the copy: select it and point the camera at it (the deliberate move).
    LLSelectMgr::getInstance()->deselectAll();
    LLSelectMgr::getInstance()->selectObjectAndFamily(root);
    gAgentCamera.setFocusOnAvatar(false, false);
    gAgentCamera.setFocusGlobal(root->getPositionGlobal(), root->getID());
}

void LLPanelLocalSpawned::onDerez()
{
    if (!mList)
    {
        return;
    }
    // Snapshot ids first: despawnInstance() fires the manager signal -> refresh()
    // rebuilds the list and frees the LLScrollListItems we'd be iterating.
    std::vector<LLUUID> ids;
    for (LLScrollListItem* item : mList->getAllSelected())
    {
        if (item)
        {
            const LLUUID id = item->getValue().asUUID();
            if (id.notNull())
            {
                ids.push_back(id);
            }
        }
    }
    for (const LLUUID& id : ids)
    {
        LLLocalMeshMgr::getInstance()->despawnInstance(id);
    }
}

void LLPanelLocalSpawned::onDerezAll()
{
    LLLocalMeshMgr::getInstance()->despawnAll();
}

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

    // Rezzed tab first (the in-world scene overview): one row per rezzed copy. Its
    // own panel/XML, not a file list. setName() after buildFromFile so the XML's name
    // doesn't clobber it. Mesh stays the default-selected tab (load assets first).
    {
        LLPanelLocalSpawned* spawned = new LLPanelLocalSpawned();
        spawned->buildFromFile("panel_local_spawned.xml");
        spawned->setName("spawned_tab");
        mTabs->addTabPanel(LLTabContainer::TabPanelParams().panel(spawned)
                           .label(getString("tab_rezzed")).select_tab(false));
    }

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

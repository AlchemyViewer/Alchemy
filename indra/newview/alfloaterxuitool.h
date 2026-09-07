/**
 * @file alfloaterxuitool.h
 * @brief The XUI tool: catalog, preview, hierarchy, inspectors and diagnostics for XUI files.
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

#include "alxuicatalog.h"
#include "alxuidiagnostics.h"
#include "alxuioverlay.h"
#include "alxuiselection.h"
#include "alxuisourcemap.h"
#include "alxuitreemodel.h"
#include "llfloater.h"
#include "llframetimer.h"
#include "llxmlnode.h"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ALXUILiveFile;
class ALXUIPreviewHost;
class LLCheckBoxCtrl;
class LLComboBox;
class LLFilterEditor;
class LLFolderView;
class LLFolderViewFolder;
class LLFolderViewItem;
class LLLineEditor;
class LLScrollListCtrl;
class LLTabContainer;
class LLTextBox;
class LLTextEditor;

// Three panes that agree about one selection: the catalog of files with a
// search across them, the hierarchy of the previewed file with its
// diagnostics, and the inspectors. The previews are floaters of their own
// in the floater view, built from the file's layered document in the
// chosen skin and language, and drawn over with the hover and selection.
class ALFloaterXUITool final : public LLFloater
{
    friend class LLFloaterReg;
public:
    AL_VIEW_TYPE(ALFloaterXUITool, LLFloater);

    static constexpr S32 PRIMARY = 0;
    static constexpr S32 SECONDARY = 1;
    static constexpr S32 PREVIEWS = 2;

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

    // What a preview host reports: the view under the mouse, a modifier
    // click, and its own closing.
    void canvasHover(S32 which, const LLView* view);
    void canvasSelect(S32 which, const LLView* view);
    void hostClosed(S32 which);
    bool hoverHighlight() const { return mHoverHighlight; }
    const ALXUISelection& selection() const { return mSelection; }
    LLView* previewRoot(S32 which) const { return mPreviews[which].root; }
    const ALXUISourceMap& sourceMap(S32 which) const { return mPreviews[which].sourceMap; }

    // A file of the primary preview changed on disk.
    void fileChanged();

private:
    ALFloaterXUITool(const LLSD& key);
    ~ALFloaterXUITool() override;

    struct Preview
    {
        LLHandle<LLFloater>                         host;
        LLView*                                     root = nullptr;
        LLXMLNodePtr                                node;
        ALXUISourceMap                              sourceMap;
        ALXUIOverlay                                overlay;
        std::string                                 skin;
        std::string                                 language;
        std::vector<std::unique_ptr<ALXUILiveFile>> liveFiles;
        std::vector<ALXUIDiagnostics::Entry>        diagnostics;
        S32                                         views = 0;
        F32                                         seconds = 0.f;
    };

    // --- the catalog pane ---------------------------------------------------
    void scanCatalog();
    void fillCatalog();
    void fillSkinsAndLanguages();
    void onCatalogFilter();
    void onFileSelected();
    void onFind();
    void onFindResult();
    void onSkinOrLanguage();

    // --- previews ------------------------------------------------------------
    void showPreviews();
    void showPreview(S32 which);
    void closePreview(S32 which);
    void closePreviews();
    void showGallery();
    void placeHost(S32 which, LLFloater* host);
    void watchFiles(const ALXUICatalog::Entry& entry);
    LLView* buildRoot(S32 which, const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr& node);

    // --- the tree pane -------------------------------------------------------
    void rebuildTree();
    void clearTree();
    void createRows(ALXUITreeItem* item, LLFolderViewFolder* parent_widget);
    void onTreeFilter();
    void onTreeSelection(const std::deque<LLFolderViewItem*>& items, bool user_action);
    void onTreeAction(const LLSD& param);
    bool onTreeActionEnabled(const LLSD& param);
    void onTreeHover(const ALXUITreeItem* item);
    void fillDiagnostics();
    void onDiagnosticSelected();
    void refreshBreadcrumb();

    // --- the selection -------------------------------------------------------
    void onSelectionChanged();
    void onHoverChanged();
    LLView* selectedView() const;
    ALXUITreeItem* selectedItem() const;
    pugi::xml_node authoredElement(const ALXUICatalog::Layer*& layer) const;

    // --- the inspectors ------------------------------------------------------
    void refreshInspectors();
    void refreshAttributes(LLView* view);
    void refreshLayout(LLView* view);
    void refreshSource(LLView* view);
    void refreshBindings(LLView* view);
    void refreshState(LLView* view);
    void onJumpToSource();
    void openInEditor(const std::string& path, S32 line);

    // --- state ---------------------------------------------------------------
    void setStatus(const std::string& text);
    void saveState();
    void loadState();
    void onToggleHover();
    void onToggleCodeBuilt();
    void onToggleSecondary();

    static LLSD row(const LLSD& id, std::initializer_list<std::pair<const char*, std::string>> cells);
    std::string layerLabel(S32 which, S32 layer) const;

    ALXUICatalog        mCatalog;
    ALXUISelection      mSelection;
    ALXUITreeModel      mModel;
    Preview             mPreviews[PREVIEWS];

    std::string         mFile;
    std::string         mSkin = "default";
    std::string         mLanguage = "en";
    std::string         mLanguage2 = "en";
    bool                mShowSecondary = false;
    bool                mHoverHighlight = true;
    bool                mShowCodeBuilt = true;
    bool                mSyncingTree = false;
    bool                mReloadPending = false;
    S32                 mLastX = -1;
    S32                 mLastY = -1;
    LLFrameTimer        mStateTimer;
    std::string         mSourcePath;     // what the jump button opens
    S32                 mSourceLine = 0;

    LLFilterEditor*     mCatalogFilter = nullptr;
    LLScrollListCtrl*   mFileList = nullptr;
    LLComboBox*         mSkinCombo = nullptr;
    LLComboBox*         mLanguageCombo = nullptr;
    LLComboBox*         mLanguageCombo2 = nullptr;
    LLCheckBoxCtrl*     mSecondaryCheck = nullptr;
    LLLineEditor*       mFindQuery = nullptr;
    LLComboBox*         mFindField = nullptr;
    LLScrollListCtrl*   mFindResults = nullptr;
    LLFilterEditor*     mTreeFilter = nullptr;
    LLPanel*            mTreePanel = nullptr;
    LLFolderView*       mTree = nullptr;
    LLPanel*            mBreadcrumb = nullptr;
    LLScrollListCtrl*   mDiagnostics = nullptr;
    LLTabContainer*     mInspectors = nullptr;
    LLScrollListCtrl*   mAttributes = nullptr;
    LLScrollListCtrl*   mLayout = nullptr;
    LLTextBox*          mSourceLayers = nullptr;
    LLTextEditor*       mSourceText = nullptr;
    LLScrollListCtrl*   mBindings = nullptr;
    LLScrollListCtrl*   mState = nullptr;
    LLTextBox*          mStatus = nullptr;

    std::unordered_map<std::string, LLFolderViewItem*> mRows;
};

/**
 * @file alfloaterxuistudio.h
 * @brief The XUI Studio: catalog, preview, hierarchy, inspectors and diagnostics for XUI files.
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
#include "alxuiedit.h"
#include "alxuilint.h"
#include "alxuioverlay.h"
#include "alxuiselection.h"
#include "alxuisourcemap.h"
#include "alxuitranslate.h"
#include "alxuitreemodel.h"
#include "llevents.h"
#include "llfloater.h"
#include "llframetimer.h"
#include "llnotificationptr.h"
#include "llxmlnode.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/unordered_map.hpp>

class ALXUILiveFile;
class ALPropertyGrid;
class ALDockPanel;
class ALXUICanvas;
class ALCanvasRow;
class ALXUIPreviewHost;
class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
class LLImageRaw;
class LLFilterEditor;
class LLFolderView;
class LLFolderViewFolder;
class LLFolderViewItem;
class LLLineEditor;
class LLScrollContainer;
class LLScrollListCtrl;
class LLScrollListItem;
class LLSpinCtrl;
class LLMenuBarGL;
class LLTabContainer;
class LLTextBox;
class LLTextEditor;

// Four regions that agree about one selection. The navigator, whose modes
// are the files, a search across them, the findings, the translation table,
// the notification templates, the library and the channels. The outline of
// the previewed file. The canvas, where the file is built from its layered
// document in the chosen skin and language, once per variant, and drawn
// over with the hover and the selection. And the inspectors.
class ALFloaterXUIStudio final : public LLFloater
{
    friend class LLFloaterReg;
public:
    AL_VIEW_TYPE(ALFloaterXUIStudio, LLFloater);

    static constexpr S32 PRIMARY = 0;
    static constexpr S32 SECONDARY = 1;
    static constexpr S32 PREVIEWS = 2;

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

    // This window has a menu bar of its own, so its shortcuts are asked
    // before the viewer's. Without saying so the viewer's menu answers
    // first and quietly keeps every key it also binds -- Control+0 is Zoom
    // In out there, and the pane it folds here was never reached.
    bool hasAccelerators() const override { return true; }

    // What a preview host reports: the view under the mouse, a modifier
    // click, a handle dragged, and its own closing.
    void canvasHover(S32 which, const LLView* view);
    void canvasSelect(S32 which, const LLView* view);
    void canvasSelectAlso(S32 which, const LLView* view);
    void canvasDeselect();
    void canvasDrag(S32 which, S32 dl, S32 db, S32 dr, S32 dt);
    void hostClosed(S32 which);

    // Where a held element would land: the nearest container over the
    // point that the schema says takes its tag, and never the element
    // itself, anything inside it, or the parent it already has.
    LLView* dropTarget(S32 which, LLView* under, const LLView* moving) const;

    // The same drag, landing somewhere else. The rect is converted into
    // the new parent's coordinates and written outright, since a form
    // measured from one parent means another thing under the next.
    void canvasReparent(S32 which, LLView* parent, S32 dx, S32 dy);

    // A panel that carries a file of its own: the file it names, or empty.
    std::string nestedFile(const LLView* view) const;
    void openNestedFile();

    // An arrow key moves the selection by a pixel, ten with shift, and
    // Control+Z puts the last write back the way it was.
    bool nudge(KEY key, MASK mask);
    bool undoEdit();
    bool redoEdit();

    // Which edge of its parent the selected element is tied to, from the
    // anchor the canvas draws outside that edge.
    void toggleFollows(S32 edge);

    // How much bigger than life the canvas draws, as a percentage, and one
    // step of it: the wheel over the canvas takes the step the bar's own
    // spinner takes, so the two cannot disagree about how far a step is.
    void setZoom(S32 percent);
    void zoomBy(S32 steps);

    // --- the document under edit ---------------------------------------------
    // Edits go into a document the tool holds, and the preview is built
    // from it: the disk hears nothing until a save.
    std::vector<ALXmlLayerMerge::Source> sourcesFor(const std::string& file) const;
    ALXUIEdit* document(const ALXUICatalog::Layer& layer);
    void documentChanged(const std::string& status);
    void saveDocument();
    void revertDocument();

    // What the unsaved edits would strand: a value a language wrote that
    // the base no longer has a place for. Per language, before writing.
    struct Impact
    {
        std::string                 language;
        S32                         stranded = 0;
        std::vector<std::string>    what;       // the first few, for the log
    };
    S32 translationImpact(std::vector<Impact>& out) const;
    void reportTranslationImpact();
    void saveAndRepair();
    bool documentDirty() const { return mDocument.dirty(); }
    bool hoverHighlight() const { return mHoverHighlight; }

    // What a drag lands on, and what the preview is measured with.
    S32 gridSize() const { return mGrid; }
    bool snapToGrid() const { return mSnap && mGrid > 1; }
    bool showRulers() const { return mRulers; }
    const ALXUISelection& selection() const { return mSelection; }
    LLView* previewRoot(S32 which) const { return mPreviews[which].root; }
    const ALXUISourceMap& sourceMap(S32 which) const { return mPreviews[which].sourceMap; }

    // A file of the primary preview changed on disk.
    void fileChanged();

    // Read the catalog and the previews again, for the button that says so.
    void reloadAll();

    // What has been read of the skin. The library asks for this rather than
    // reading the tree a second time to answer how often a tag is used.
    const ALXUICatalog& catalog() const { return mCatalog; }

private:
    ALFloaterXUIStudio(const LLSD& key);
    ~ALFloaterXUIStudio() override;

    struct Preview
    {
        // Where it is drawn: the window's own canvas, or one belonging to a
        // floater of its own -- and that one dies with its floater, whether
        // the tool closed it or the developer did, so it is held the same
        // way the floater is.
        LLHandle<LLFloater>                         host;
        LLHandle<LLView>                            canvas;
        LLView*                                     root = nullptr;
        LLXMLNodePtr                                node;
        ALXUISourceMap                              sourceMap;
        ALXUIOverlay                                overlay;
        ALXUILint                                   lint;
        std::string                                 skin;
        std::string                                 language;
        std::vector<std::unique_ptr<ALXUILiveFile>> liveFiles;
        std::vector<ALXUIDiagnostics::Entry>        diagnostics;
        S32                                         views = 0;
        F32                                         seconds = 0.f;
    };

    // --- the navigator -------------------------------------------------------
    // One pane, one mode at a time: which one is showing, and what each has
    // to say for itself on its own button.
    void onMode();
    static std::string modeName(LLTabContainer* tabs);
    void refreshModeCounts();

    // --- the files and find modes --------------------------------------------
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
    // The registered floater itself, rather than a shell of what the file
    // describes: the name to build under, or empty when this file is not
    // one, or the author has not asked, or the viewer says not now.
    std::string realFloaterName(const ALXUICatalog::Entry& entry) const;
    LLFloater* buildRealFloater(S32 which, const ALXUICatalog::Entry& entry,
                                const std::string& name, LLXMLNodePtr& node);
    static void detachHost(LLFloater* host);

    // --- notifications -------------------------------------------------------
    // A template built as the panel it would produce, without posting it.
    LLView* buildNotification(ALXUICanvas* canvas);
    void fillNotifications();
    void onNotificationSelected();
    void onPostNotification();

    // --- channels ------------------------------------------------------------
    // What the notification system did with what was posted, which is the
    // half a preview cannot show.
    void watchChannels();
    bool onChannelChanged(const std::string& channel, const LLSD& payload);
    void onChannelSelected();
    void onRespondToNotification();
    LLNotificationPtr selectedChannelNotification() const;
    void closePreview(S32 which);
    ALXUICanvas* canvasOf(const Preview& pv) const;
    void closePreviews();
    // The variants side by side, after any of them has been built: a canvas
    // is as big as what it holds, so where the next one starts is not known
    // until the one before it has something on it.
    // A region the developer is not using, folded away and brought back with
    // everything it held.
    void togglePane(std::string_view name);

    // A region in a window of its own, and back. The canvas is not one of
    // these: it holds a built view tree with hit-testing of its own.
    ALDockPanel* paneOf(std::string_view name) const;
    bool paneOut(std::string_view name) const;
    void togglePaneOut(std::string_view name);
    void dockPanes();
    void setPaneCollapsed(std::string_view name, bool collapsed);
    bool paneCollapsed(std::string_view name) const;
    void refreshPaneButtons();
    void layoutCanvases();
    // The pill: what is on the canvas, whether it is being shown, and
    // whether it is being held on this file.
    void refreshCanvasHead();
    void onToggleCanvasShown();
    void onToggleCanvasPinned();
    void showGallery();
    void placeHost(S32 which, LLFloater* host);
    void watchFiles(const ALXUICatalog::Entry& entry);
    LLView* buildRoot(S32 which, const ALXUICatalog::Entry& entry, ALXUICanvas* canvas, LLXMLNodePtr& node);
    LLView* buildFromNode(const ALXUICatalog::Entry& entry, ALXUICanvas* canvas, LLXMLNodePtr node);

    // --- the tree pane -------------------------------------------------------
    void rebuildTree();
    void clearTree();
    LLFolderViewItem* createRow(ALXUITreeItem* item, LLFolderViewFolder* parent_widget);
    void createRows(ALXUITreeItem* item, LLFolderViewFolder* parent_widget);
    void onTreeFilter();
    void onTreeSelection(const std::deque<LLFolderViewItem*>& items, bool user_action);
    void onTreeAction(const LLSD& param);
    bool onTreeActionEnabled(const LLSD& param);
    void onTreeHover(const ALXUITreeItem* item);
    void runLint();
    void fillFindings();
    void onFindingSelected();
    void refreshBreadcrumb();

    // Lint all: every file in the catalog, a few per frame so the viewer
    // keeps drawing, with a report beside the log.
    void startLintAll();
    void stepLintAll();
    void finishLintAll();
    S32 lintOneFile(const ALXUICatalog::Entry& entry, std::vector<std::string>& lines);

    // The preview as an image, which is what a review of a translation
    // needs without the viewer. The picker names the file and its
    // extension chooses the format.
    void capturePreview();
    void writeCapture(const std::vector<std::string>& filenames);

    // --- the translation table -----------------------------------------------
    // One row per field a translator writes, with what the language has
    // for it and what the merge does with that. Writing a row puts the
    // value where the merge looks, which is the whole point of the table.
    void fillTranslation();
    void onTranslationSelected();
    void onTranslationWrite();
    void onTranslationLanguage();
    const ALXUICatalog::Layer* overlayLayer(const ALXUICatalog::Entry& entry, const std::string& language) const;
    bool overlayPath(const ALXUICatalog::Entry& entry, const std::string& language,
                     std::string& path, bool& created, std::string& error) const;
    S32 repairFile(const ALXUICatalog::Entry& entry, const std::string& language, std::string& error);
    void onRepairFile();
    void startRepairAll();
    void stepRepairAll();

    // The root name is the one thing a whole file is matched on, so a
    // file that disagrees about it is repaired before anything in it is.
    void onRepairRoots();

    // The overlay census over every file and language, which is the
    // instrument every repair pass is measured with: a few files per
    // frame, a table beside the log.
    void startCensus();
    void stepCensus();
    void finishCensus();

    // The widget vocabulary as an XSD, written into the skins directory the
    // catalog was read from, which is the source tree in a developer build.
    void onExportSchema();

    // --- edits ---------------------------------------------------------------
    // A move or a resize of the selected element, as the movement of its
    // four edges, written into the layer that positions it.
    bool applyEdges(S32 dl, S32 db, S32 dr, S32 dt);
    // Line every other selected element up with the one the handles are on.
    void alignSelection(const std::string& how);
    const ALXUICatalog::Layer* editTarget() const;
    void refreshEditTarget();

    // --- the selection -------------------------------------------------------
    void onSelectionChanged();
    void onHoverChanged();
    LLView* selectedView() const;
    ALXUITreeItem* selectedItem() const;
    pugi::xml_node authoredElement(const ALXUICatalog::Layer*& layer) const;

    // --- the inspectors ------------------------------------------------------
    void refreshInspectors();
    void refreshAttributes(LLView* view);
    void onFieldCommit(const std::string& name, const std::string& value);
    void onFieldRemove(const std::string& name);

    // The gutter mark beside a row, clicked: the row is marked because more
    // than one layer writes it, and the Source inspector is where every
    // layer's own words about the element already are.
    void onFieldGutter(const std::string& name);

    // --- the shape of the file -----------------------------------------------
    // Order among siblings, reparenting and removal, and the palette of
    // what may go under the selection.
    void restructure(const std::string& action, const ALXUISelection::path_t& path);
    bool siblingOf(const ALXUIEdit& document, const ALXUISelection::path_t& path,
                   bool before, ALXUISelection::path_t& out) const;
    void fillPalette();
    void onInsertFromPalette();
    void onTreeMove(const std::string& action);
    void revealInContainers(LLView* view);
    void refreshLayout(LLView* view);
    void refreshSource(LLView* view);
    void refreshBindings(LLView* view);
    void refreshState(LLView* view);
    void refreshSelectionFindings();
    // Which inspectors are offered for what is selected, and what each has
    // to say about it before it is opened.
    void refreshInspectorStrip();
    void onJumpToSource();
    void openInEditor(const std::string& path, S32 line);

    // --- state ---------------------------------------------------------------
    void setStatus(const std::string& text);
    void saveState();
    void loadState();
    // The menu bar: an action by name, and whether a switch is on.
    void onMenuAction(const LLSD& param);
    bool onMenuCheck(const LLSD& param);
    bool onMenuEnable(const LLSD& param);
    void onToggleSecondary();

    static LLSD row(const LLSD& id, std::initializer_list<std::pair<const char*, std::string>> cells);
    std::string layerLabel(S32 which, S32 layer) const;

    // Every list in the tool is a table someone will want in a message or
    // a bug report: shift and control extend the selection, and the right
    // button copies it as a table with a heading and a line saying what it
    // is about, or copies the one cell it was over.
    void watchList(LLScrollListCtrl* list);
    void onListRightClick(LLUICtrl* ctrl, S32 x, S32 y, MASK mask);
    void onListAction(const LLSD& param);
    bool onListActionEnabled(const LLSD& param);
    std::string listCaption(const LLScrollListCtrl* list) const;
    std::string listAsText(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const;
    void copyList(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const;
    LLScrollListCtrl* focusedList() const;

    ALXUICatalog        mCatalog;
    ALXUITranslate      mTranslate;
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
    bool                mSnap = false;
    bool                mRulers = false;
    // Build the registered class rather than a shell of the file. Off, and
    // remembered off: what it costs is the viewer's own crashes.
    bool                mRealFloater = false;
    S32                 mGrid = 4;
    // How much bigger than life the canvas draws, as a percentage.
    S32                 mZoom = 100;
    bool                mSyncingTree = false;
    bool                mReloadPending = false;
    bool                mReloadEntryOnly = false;   // one file changed, not the tree
    bool                mReloadFromDisk = false;    // and someone else changed it
    bool                mKeepPlace = false;         // a rebuild leaves the preview where it is
    std::string         mPendingStatus;             // what to say once the rebuild is done
    // The layer under edit, held between operations: its undo stack, its
    // dirty flag and the text the preview is built from are all its own.
    ALXUIEdit           mDocument;
    std::string         mDocumentPath;
    // The translation table is written through, so its last write is kept
    // as the file it replaced rather than as a step of the document.
    std::string         mWroteThroughPath;
    std::string         mWroteThroughText;
    S32                 mLastX = -1;
    S32                 mLastY = -1;

    // The window's own canvases, in the region between the outline and the
    // inspector: one per variant, side by side on the row, unless the
    // developer has asked for the preview in a window of its own.
    LLScrollContainer*  mCanvasArea = nullptr;
    ALCanvasRow*        mCanvasRow = nullptr;
    std::vector<ALDockPanel*> mPanes;
    ALXUICanvas*        mCanvases[PREVIEWS] = {};
    bool                mFloatPreview = false;
    // The pill above the canvas. Hidden, the region is given over to the
    // file it is about; pinned, the canvas keeps the file it has while the
    // catalog is read, and takes the one waiting for it when unpinned.
    bool                mPreviewHidden = false;
    bool                mPinned = false;
    std::string         mPendingFile;
    LLFrameTimer        mStateTimer;
    std::string         mSourcePath;     // what the jump button opens
    S32                 mSourceLine = 0;

    LLPointer<LLImageRaw>           mCapture;       // taken before the picker opens
    LLScrollListCtrl*               mMenuList = nullptr;    // the list the right button was over
    std::string                     mMenuCell;      // the cell it was over
    LLHandle<LLView>                mListMenu;
    std::vector<LLScrollListCtrl*>  mLists;

    std::deque<std::string>         mRepairQueue;   // files still to repair
    S32                             mRepairFiles = 0;
    S32                             mRepairMoves = 0;

    std::deque<std::string>         mCensusQueue;   // files still to count
    boost::unordered_map<std::string, std::map<std::string, S32>> mCensus;
    S32                             mCensusFiles = 0;

    std::deque<std::string>         mLintQueue;     // files still to check
    std::vector<std::string>        mLintReport;
    std::map<std::string, S32>      mLintByRule;
    S32                             mLintFiles = 0;
    S32                             mLintTotal = 0;
    S32                             mLintFindings = 0;

    LLFilterEditor*     mCatalogFilter = nullptr;
    LLScrollListCtrl*   mFileList = nullptr;
    LLComboBox*         mSkinCombo = nullptr;
    LLComboBox*         mLanguageCombo = nullptr;
    LLComboBox*         mLanguageCombo2 = nullptr;
    LLComboBox*         mGridCombo = nullptr;
    LLSpinCtrl*         mZoomSpin = nullptr;
    LLCheckBoxCtrl*     mSecondaryCheck = nullptr;
    LLTextBox*          mCanvasTitle = nullptr;
    LLButton*           mCanvasShown = nullptr;
    LLButton*           mCanvasPinned = nullptr;
    LLLineEditor*       mFindQuery = nullptr;
    LLComboBox*         mFindField = nullptr;
    LLScrollListCtrl*   mFindResults = nullptr;
    LLFilterEditor*     mTreeFilter = nullptr;
    LLPanel*            mTreePanel = nullptr;
    LLFolderView*       mTree = nullptr;
    LLPanel*            mBreadcrumb = nullptr;
    LLScrollListCtrl*   mFindings = nullptr;
    LLTabContainer*     mInspectors = nullptr;
    ALPropertyGrid*     mAttributeGrid = nullptr;
    LLTextBox*          mAttributeWhat = nullptr;       // what is selected, in its own words
    LLFilterEditor*     mAttributeFilter = nullptr;
    LLScrollListCtrl*   mPalette = nullptr;
    // The element named for reparenting, until somewhere is chosen for it.
    ALXUISelection::path_t mCutPath;
    LLScrollListCtrl*   mLayout = nullptr;
    LLTextBox*          mSourceLayers = nullptr;
    LLTextEditor*       mSourceText = nullptr;
    LLScrollListCtrl*   mBindings = nullptr;
    LLScrollListCtrl*   mState = nullptr;
    LLScrollListCtrl*   mSelectionFindings = nullptr;
    LLMenuBarGL*        mMenuBar = nullptr;
    LLTabContainer*     mModes = nullptr;
    LLTabContainer*     mBottom = nullptr;
    LLScrollListCtrl*   mNotifications = nullptr;
    LLFilterEditor*     mNotificationFilter = nullptr;
    // The template being previewed, when the file is notifications.xml.
    std::string         mNotification;

    LLScrollListCtrl*   mChannels = nullptr;
    LLComboBox*         mChannelResponse = nullptr;
    // The listeners are disconnected here: a channel outlives this floater,
    // and a signal still bound to a closed one calls into freed memory.
    std::vector<LLBoundListener>                            mChannelListeners;
    boost::unordered_map<std::string, LLNotificationPtr>     mChannelNotifications;
    LLComboBox*         mTranslateLanguage = nullptr;
    LLScrollListCtrl*   mTranslateList = nullptr;
    LLLineEditor*       mTranslateValue = nullptr;
    LLTextBox*          mTranslateCounts = nullptr;
    LLTextBox*          mEditTarget = nullptr;
    LLTextBox*          mStatus = nullptr;

    boost::unordered_map<std::string, LLFolderViewItem*> mRows;
};

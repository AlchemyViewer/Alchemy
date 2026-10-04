/**
 * @file alfloaterscriptstudio.h
 * @brief Script Studio: the scripts open in the viewer, edited, saved and compiled in one window.
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

#include "alcodeeditor.h"
#include "alscriptexplorerpane.h"
#include "alnotecardembedded.h"
#include "alscriptoutputpane.h"
#include "alscriptproblemspane.h"
#include "alscriptsearchpane.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudiocommands.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiohistory.h"
#include "alscriptstudiorecovery.h"
#include "alscriptstudiosaving.h"
#include "alscriptstudiotabs.h"
#include "alscriptstudiovim.h"
#include "alscriptexternaleditor.h"
#include "alscriptstudiofiles.h"
#include "alscriptstudioweighing.h"
#include "alscriptstudiowords.h"
#include "alscriptstudioorphans.h"
#include "alscriptnavigation.h"
#include "alscriptlookup.h"
#include "alscriptobjectcheck.h"
#include "alscriptrecompile.h"
#include "alscriptreferencespane.h"
#include "alscriptoutlinepane.h"
#include "alscriptcrumbsbar.h"
#include "alscriptinspectorpane.h"
#include "alscriptstudiocaret.h"
#include "alscriptstudiochecking.h"
#include "alscriptstudioplaces.h"
#include "alfindings.h"
#include "almenuslot.h"
#include "aloutputview.h"
#include "alscriptanalysis.h"
#include "alscriptsnippets.h"
#include "alscriptstudio.h"
#include "alscriptenvelope.h"
#include "alscriptpreprocessor.h"
#include "alrecoverystore.h"
#include "alscripttempfiles.h"
#include "alscripttypes.h"
#include "alsourcemap.h"
#include "alstudiofloater.h"
#include "altabstrip.h"
#include "alvimkeymap.h"

#include "llstl.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class ALEmptyState;
class ALJumpBar;
class ALPaneList;
class ALScriptWeightsPane;
class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
class LLFilterEditor;
class LLLayoutPanel;
class LLLineEditor;
class LLMenuItemGL;
class LLPanel;
class LLScrollListCtrl;
class LLContextMenu;
class ALScopeBar;
class LLEditMenuHandler;
class LLInventoryObserver;
class LLTabContainer;
class LLTextEditor;
class LLViewerObject;

// The scripting studio:
// scripts from inventory and from objects open in tabs over code editors,
// saved and compiled through the workspace, with the compiler's problems in
// a pane that jumps to the line, the region's vocabulary colouring,
// completing and explaining the text, the analyzers checking it as it is
// typed and answering where a name lives -- go to definition, find
// references, rename -- an outline in the inspector and a breadcrumb over
// the editor, what scripts say in an Output tab with their run-time errors
// marked in the gutter, an explorer of the objects in hand with their
// scripts to open, start, stop and reset, a check before every save, and
// a script the preprocessor wrapped shown as the code the server compiled
// with the author's source in a tab beside it. Its regions fold and come
// out as any studio's do.
class ALFloaterScriptStudio final : public ALStudioFloater, public ALScriptStudioServices, public ALScriptStudioTabs, public ALScriptStudioAnalysis,
                                    public ALScriptOutputPane::Window, public ALScriptProblemsPane::Window,
                                    public ALScriptSearchPane::Window, public ALScriptExplorerPane::Window, public ALScriptStudioRecovery::Window,
                                    public ALScriptStudioSaving::Window, public ALScriptStudioVim::Window,
                                    public ALScriptExternalEditor::Window, public ALScriptStudioFiles::Window,
                                    public ALScriptStudioWeighing::Window, public ALScriptStudioOrphans::Window,
                                    public ALScriptNoticeBar::Window, public ALScriptNavigation::Window, public ALScriptLookup::Window,
                                    public ALScriptReferencesPane::Window, public ALScriptOutlinePane::Window,
                                    public ALScriptCrumbsBar::Window, public ALScriptInspectorPane::Window,
                                    public ALScriptStudioCaret::Window, public ALScriptStudioChecking::Window,
                                    public ALScriptObjectCheck::Window, public ALScriptRecompile::Window, public ALScriptStudioHistory::Window
{
    friend class LLFloaterReg;

public:
    AL_VIEW_TYPE(ALFloaterScriptStudio, ALStudioFloater);

    // The ways in (alscriptstudio.h), which reach into every window.
    friend void ALScriptStudio::open(const ALScriptRef& ref, const std::string& name, bool take_focus);
    friend void ALScriptStudio::explore(const LLUUID& root);
    friend void ALScriptStudio::itemRemoved(const ALScriptRef& ref);
    friend bool ALScriptStudio::unsavedIn(const ALScriptRef& ref);
    friend void ALScriptStudio::offerRecovery();
    friend void ALScriptStudio::editSnippets(bool lua);
    friend void ALScriptStudio::openVimrc();

    // Whether this is the main window, which keeps the state and is
    // hidden rather than destroyed when closed; the others are the
    // scripts popped out into windows of their own, gone when closed.
    bool isMainWindow() const { return mMain; }

    bool postBuild() override;
    bool matchesKey(const LLSD& key) override;
    // A popped-out window closes once every script in it has been closed,
    // each asked about if it has unsaved changes; the main window hides.
    bool canClose() override;
    // The viewer quitting: the main window, hidden or not, asks about what
    // is unsaved in it first, and the quit waits on the answer.
    void closeFloater(bool app_quitting = false) override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool undo() override;
    bool redo() override;

    // A script opened here; with text carried from another window, put
    // in place of what the server has once that has loaded, as one step
    // to undo, and the caret at a line; and the keyboard given to it, or
    // left where it is.
    void openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried = std::nullopt, S32 line = -1,
                    bool focus = true) override;
    void holdTabs() override { ++mTabsHeld; }
    void letGoOfTabs() override
    {
        if (mTabsHeld > 0 && --mTabsHeld == 0)
        {
            releaseTabs();
        }
    }
    // The active script moved to a window of its own, its unsaved text
    // going with it; or to another studio window already open. False where
    // it could not go.
    // At a point of the screen where one is given -- where a tab torn off
    // the strip was let go of -- else beside this window.
    void popOut(std::optional<LLCoordGL> screen = std::nullopt);
    bool moveActiveTo(ALFloaterScriptStudio* window);
    // The next script closed on the way to closing the window, or the
    // window closed once none is left.
    void continueClosing() override;
    // The options every editor shares -- font, keys, wrap, gutter, map --
    // put on all of them again, when a setting behind one changes; and
    // on every studio open, which the preferences ask for
    // (ALScriptStudio::refreshAll).
    void applyEditorOptions();

    // A word of the language, as the region defines it (ALScriptStudioWords).
    typedef ALScriptStudioWords::Vocab Vocab;
    // The least and the most a zoom takes the text to, in points
    // (ALScriptStudio::editorFont).
    static constexpr F32 MIN_TEXT_POINTS = 6.f;
    static constexpr F32 MAX_TEXT_POINTS = 48.f;
    // The text a step larger, or smaller, every window's; none, as the
    // size chosen. Said in points.
    void zoomText(S32 steps);

private:
    ALFloaterScriptStudio(const LLSD& key);
    ~ALFloaterScriptStudio() override;

    // --- types -------------------------------------------------------------------------

    // One tab (alscriptstudiodoc.h).
    using Doc = ALScriptStudioDoc;
    static constexpr size_t NONE = static_cast<size_t>(-1);
    using FixPick = Doc::FixPick;
    using ProblemTraits = Doc::ProblemTraits;
    // A snippet: a body with placeholders, offered by name from the
    // Insert menu and by prefix among the completions; the viewer's and
    // the scripter's own (ALScriptSnippets).
    typedef ALScriptSnippets::Snippet Snippet;
    static const std::vector<Snippet>& snippets(bool lua) { return ALScriptSnippets::all(lua); }
    // Where the analyzer says a name is declared (ALScriptPlaces), as the
    // hover card and the inspector say it.
    typedef ALScriptPlaces::Declared Declared;
    // A place jumped from, to go back to and forward again (ALNavHistory).
    typedef ALNavHistory::Place NavPlace;
    // What a file's name says it holds (ALScriptStudioFiles); the
    // document's language set by it, the editor taught or untaught.
    typedef ALScriptStudioFiles::Language FileLanguage;

    // --- building, and the state kept --------------------------------------------------

    // postBuild's parts, in the order it takes them.
    void buildMenus();
    void findPanes();
    void wirePanes();
    void listenToWorkspace();
    void listenToSettings();
    void listenToWorld();
    void openAsLeft();
    // The first open, with nothing kept: the text given the room -- the
    // Inspector folded, the bottom panel folded until it has something to
    // show (mBottomWaiting, refreshBottomTabs), the window taller than the
    // skin's where the screen has room.
    void firstOpen();
    // The empty editor's words: Go to Script with its keys as they are now,
    // New Script beside it, and Open File as a link.
    void sayNoDocs();
    // The tabs open, as the state keeps them: an item by its object and
    // id, a file by its path, and which is in front.
    LLSD openTabs() const;
    void writeState(LLSD& state) const override;
    // What writeState writes before the account's own is taken out of it.
    void writeSharedState(LLSD& state) const;
    void readState(const LLSD& state) override;
    // The View menu's options, vim's among them: kept in the state, and
    // taken by a window popped out of another -- which keeps no state of
    // its own -- from the one it came out of, before its first tab.
    void writeViewOptions(LLSD& state) const;
    void readViewOptions(const LLSD& state);
    void takeViewOptions(const ALFloaterScriptStudio& from);
    // Due writings of what is unsaved, a lost connection, and what holds
    // each tab, looked at a few times a second, whether the window is shown
    // or not.
    void pumpRecovery();
    // What a tab needs from the moment it is made: its text's changes
    // heard, its places slid.
    void wireDoc(Doc& doc);

    // --- the tabs ----------------------------------------------------------------------

    Doc*   active();
    // Whether the tab in front is a notecard in the world, whose own view
    // options the View menu then sets.
    bool   frontIsNotecard()
    {
        const Doc* doc = active();
        return doc && doc->itemNotecard();
    }
    size_t indexOf(const ALScriptRef& ref) const;
    size_t indexOf(std::string_view id) const;
    // The tab in front, its editor given the keyboard where asked.
    void   activate(size_t index, bool focus = true);
    void onTabChosen(const std::string& value);
    // The tab pressed with the right button: a menu about it.
    void   showTabMenu(const std::string& value, S32 x, S32 y);
    // A tab torn off the strip and let go of at a point of the screen: into
    // the studio window it was dropped on, else a window of its own there --
    // or this one moved there, where it was the only tab.
    void onTabTorn(const std::string& id, S32 screen_x, S32 screen_y);
    void   onTabAction(const std::string& action);
    // The tab after or before the active one, round the ends.
    void   cycleTab(S32 direction);
    // The tab shown, one place along the strip.
    void   moveTab(S32 direction);
    // The documents in the order the tabs were dragged into.
    void   onTabsReordered(const std::vector<std::string>& order);
    // A script's tab, its window's title and the bar called what it is now
    // called: renamed here, in the inventory, or found so when opened again.
    void        renameDoc(Doc& doc, const std::string& name);
    // Whether a script may move between windows now: not while a save of it
    // is on its way, which is said.
    bool        movable(const Doc& doc);
    // The window, of all of them, that has a script or a file open.
    static ALFloaterScriptStudio* holderOf(const ALScriptRef& ref, const std::string& file);
    // The studio window the keyboard was last in, while it is open: where a
    // script opened from outside -- the inventory, an object -- goes, as a
    // script window used to open over the last one.
    static ALFloaterScriptStudio* lastWorkedIn();
    void openOrphan(const ALRecoveryEntry& entry, Doc::Orphan orphan) override;
    void closeDocument(std::string_view id) override;
    void closeDocumentAnswered(const std::string& id, S32 option);
    // The quit's question about what is unsaved: saved, kept to be opened
    // again next time, let go of, or the quit called off.
    void quitAnswered(S32 option);
    // Several tabs closed at once -- the others, all of them, `:qa` -- the
    // unsaved among them asked about in one question, not one each.
    void closeMany(const std::vector<std::string>& ids) override;
    // Close Others, Close Saved and Close All, about the tab `keep` is.
    void closeTabs(std::string_view which, const Doc* keep);
    // A close of the window, or of several tabs, that is waited on no
    // longer; and a quit waiting on it called off.
    void stopClosing() override;
    // Whether the viewer is quitting on this window's answer.
    bool quittingOnUs() const override;
    // A tab let go of: its unsaved text set aside among the discarded,
    // where it can be had back for a while, unless it is kept -- moved to
    // another window, kept for next time, or kept as the viewer goes.
    void letGoOf(size_t index, bool keep = false);
    void letGoOf(Doc& doc) override;
    // The window's close, with several scripts unsaved, asked about all of
    // them at once: saved, let go of, or the window kept.
    void closeWindowAnswered(S32 option);

    // --- a tab's views and editors -----------------------------------------------------

    ALCodeEditor*             makeEditor(const std::string& id, bool read_only);
    // What an editor of the studio's is made with: its colours by the
    // table's names, the studio's settings.
    ALCodeEditor::Params      editorParams(const std::string& id, bool read_only) const;
    // The options every editor shares, put on one.
    // A notecard's editor takes the notecards' own wrap and line numbers,
    // counted from 0 where that is asked for.
    void                      applyEditorOptions(ALCodeEditor& editor, bool notecard = false);
    // The expanded text put in the document's other editor, which is shown
    // once there is one where the tab asked for it.
    void                      showExpanded(Doc& doc, const std::string& text) override;
    // The other editor gone, where what it holds is of no text the
    // script is now, and the source shown in its place.
    void                      dropExpanded(Doc& doc);
    void                      toggleExpanded();
    // A view of the tab put in front, and the pane, the bars and the
    // toolbar shown as it now is; the keyboard with it where asked, or
    // where the view it replaces had it.
    void                      showView(Doc& doc, Doc::View view, bool focus = false);
    // The source put in front, for going to a place in it: every place a
    // list, a card or a jump names is the source's.
    ALCodeEditor&             sourceInFront(Doc& doc);
    // The keyboard to the view in front.
    static void               focusShown(Doc& doc);
    // A view as the studio's state writes it, and back.
    static const char*        viewName(Doc::View view);
    static Doc::View          viewNamed(const std::string& name);
    // Which view the pane shows for the active document, and every other
    // editor hidden.
    void                      showEditors();
    // The region's words for colouring and completing, and the analyzer
    // behind completion, hover and signature help.
    void                      teachEditor(Doc& doc);
    // The tab's text against the text it was last saved as; asked again,
    // the source back.
    void compareWithSaved();
    // Edit > Undo and Redo named for the step they take, where it has one.
    void refreshUndoLabels();
    // The formatter over the text as it stands, or the lines selected:
    // every line put right as one step to undo, the caret keeping its
    // place.
    void format(Doc& doc, bool selection_only) override;
    // The blanks at every line's end taken away, as one step to undo.
    void trimTrailing(Doc& doc);
    // Copy, from whichever list or editor has the keyboard.
    static LLEditMenuHandler* focusedEditHandler();
    // The tab's script read as the other language from here: its grammar,
    // its words, its checks; what was made of it as the one it was, gone.
    void readAs(Doc& doc, bool lua);
    void onRunning();

    // --- loading, saving and files -----------------------------------------------------

    void loaded(const ALScriptLoaded& answer);
    // loaded's parts: a revert that could not be had; a kept text whose
    // script could not be; the prim asked again for a script it did not
    // list; and a notecard's and a script's text taken in.
    void revertFailed(Doc& doc, size_t index, bool could_change, const std::string& error);
    void keptTextNotHad(Doc& doc, const ALScriptLoaded& answer);
    void askPrimAgain(Doc& doc);
    void loadedNotecard(Doc& doc, const ALScriptLoaded& answer);
    void loadedScript(Doc& doc, const ALScriptLoaded& answer);
    // The caret to the line, or the stretch, asked for before the text had
    // loaded, once it has; and the Compare asked for then.
    void goToPending(Doc& doc);
    void comparePending(Doc& doc);
    // A notecard's items (ALNotecardEmbedded), made for a tab loaded or
    // kept as a notecard, afresh where `fresh`.
    ALNotecardEmbedded& notecardItems(Doc& doc, bool fresh = false);
    // The preprocessor: whether it applies to a script; its run over the
    // text as it stands, for the analyzers, with the way back; and its
    // run ahead of a save, fetching includes, then the upload in the
    // envelope. Positions the analyzers answer with are mapped back to
    // the source, and what falls in an include is listed by its file.
    // A run of it over the text as it stands, fetching its includes, for
    // saving (ALScriptStudioSaving::preprocess).
    void                          runPreprocessor(const Doc& doc, std::function<void(const ALPreprocessor::Result&)> answer) override;
    void                          chooseIncludeFolder();
    // A file on disk opened in a tab of its own, or brought forward, at
    // a place in it where one is given; read as the language its
    // extension says, or the one it was included from.
    void                          openFile(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    // Opened in this window whatever another has open: where a tab is
    // being moved here from it.
    void                          openFileHere(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    void                                  speakFileLanguage(Doc& doc, const FileLanguage& language);
    // Revert to Saved, asked about where something would be lost; and
    // whether there is anything to read again.
    void askRevert(Doc& doc);
    bool revertible(const Doc& doc) const override;
    // A script made in the inventory: what it starts from chosen first --
    // the scripter's own template, the grid's own, or one of the templates
    // shipped and kept (templatesOf) -- then named, and opened here once
    // the inventory has it, with that in it.
    void newInventoryScript(bool lua);
    void nameNewInventoryScript(bool lua, const std::string& opening);
    // The templates of a language, as New Script offers them: the viewer's
    // from app_settings/script_templates, then the scripter's own from the
    // settings folder's, each a name, a line about it and the script.
    static std::vector<ALScriptSnippets::Snippet> templatesOf(bool lua);
    // What a tab holds saved as a new item in the inventory -- a script or
    // a notecard, its items with it -- and the tab closed once it is.
    void saveCopyToInventory(Doc& doc) override;
    // An LSL tab written again as SLua (ALLSLToSLua) in a new SLua script
    // beside it -- in the prim the LSL is in, or the inventory's scripts
    // folder for one in the inventory -- its text put in unsaved and set
    // beside the LSL; the LSL script left as it is.
    void convertToSLua(Doc& doc);
    // A script written as SLua into a prim, waited for until the prim lists
    // it: by the id the region gave, or by its name where it gave none, as
    // one of that name that was not there before.
    struct ConvertedWaiting
    {
        LLUUID              prim;
        LLUUID              item;
        std::string         name;
        std::vector<LLUUID> before;
        std::string         text;
        Doc::PendingCompare compare;
    };
    std::vector<ConvertedWaiting>      mConvertedWaiting;
    boost::signals2::scoped_connection mConvertedContents;
    void convertedMade(const ALScriptCreated& made, ConvertedWaiting waiting);
    void convertedListed(const ALScriptContents& contents);
    // The new SLua script opened with its text unsaved, and set beside the
    // LSL once it has loaded.
    void openConverted(const ALScriptRef& ref, const std::string& name, const std::string& text, const Doc::PendingCompare& compare);

    // --- problems and checks -----------------------------------------------------------

    // The caret to the next problem of the script after it, or the one
    // before, round past the ends, with what it says in a card; of what is
    // left from LSL alone, where asked, and once none is, a word that the
    // script may be made --!strict.
    void                goToProblem(Doc& doc, S32 direction, bool migration = false);
    // The script's own problems' places, each once and in order -- those
    // left from LSL, where asked; the caret taken to one; and to one by its
    // number (ALScriptStudioVim::Window).
    std::vector<ALTextPos> problemPlaces(const Doc& doc, bool migration = false) const;
    // An SLua script with nothing left from LSL: said, with --!strict
    // offered where it is not yet (make_strict).
    void                   migrationDone(Doc& doc);
    void                   goToProblemAt(Doc& doc, const ALTextPos& to);
    bool                   goToProblemNumber(Doc& doc, S32 number) override;
    // A fix made, the script checked again (ALScriptStudioChecking).
    bool                applyFix(Doc& doc, const ALScriptFix& fix, U32 version) override { return mChecking.applyFix(doc, fix, version); }
    // What is wrong at a place of a script, in the card the mouse would
    // bring up there.
    void showProblemCard(Doc& doc, const ALTextPos& at);
    // The bottom tabs' titles: how many problems and places each lists,
    // and whether a script has said something the Output tab has not
    // shown yet.
    void refreshBottomTabs();
    // Every script of the object with `root` checked, those no tab holds
    // listed in Problems with the open scripts' (ALScriptObjectCheck); and
    // what the check asks of the window.
    void checkObject(const LLUUID& root);
    void listScripts(const LLUUID& root, std::function<void(ALScriptObjectCheck::Window::Listed)> told) override;
    bool isOpen(const ALScriptRef& ref) override;
    void read(const ALScriptRef& ref, std::function<void(std::optional<ALScriptObjectCheck::Window::Read>)> told) override;
    bool preprocessing() const override;
    bool luauConfig(const ALScriptPreprocessor::Request& root, ALLuauConfig& config) const override;
    void scriptChecked(const ALScriptObjectCheck::Script& script) override;
    void objectChecked(const ALScriptObjectCheck::Done& done) override;
    // Scripts, prims and objects recompiled from the Explorer, each said in
    // Output and a closed one's problems listed (ALScriptRecompile); and
    // what the recompile asks of the window.
    void recompileScripts(std::vector<ALScriptRecompile::One> scripts, std::vector<std::pair<LLUUID, std::string>> prims,
                          const std::string& target) override;
    void listScripts(const std::vector<std::pair<LLUUID, std::string>>& prims, std::function<void(ALScriptRecompile::Window::Listed)> told) override;
    bool saving(const ALScriptRef& ref) override;
    bool unsaved(const ALScriptRef& ref) override { return unsavedAnywhere(ref); }
    std::optional<bool> knownRunning(const ALScriptRef& ref) override;
    void recompile(const ALScriptRef& ref, const std::string& target, std::optional<bool> running, ALScriptCompileCallback told) override;
    void scriptRecompiled(const ALScriptRecompile::Script& script) override;
    void recompiled(const ALScriptRecompile::Done& done) override;
    // Completion's link numbers: where the call being typed wants one, the
    // prims of the script's object by name, each putting in its number.
    void completeLinks(const Doc& doc, const ALTextPos& at, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out);
    // What scripts say, from the workspace: listed in the Output tab, and
    // a run-time error in a script that is open marked on its line.
    void runtimeEvent(const ALScriptRuntimeEvent& event);
    // A line of code the preprocessor made, shown in the Preprocessed view.
    void showGenerated(Doc& doc, S32 line, S32 column);
    static ALScriptStudioDoc::RuntimeProblem runtimeProblemOf(const ALScriptRuntimeEvent& event);
    // Whether a run-time error's line waits for the map what runs is read
    // back by; and a tab's own, said while it was closed, taken as it loads.
    bool holdsRuntime(const Doc& doc) const;
    void recallRuntime(Doc& doc);
    // Whether a script runs, what it compiles for: the region's word, on a
    // tab that has it open.
    void runningState(const ALScriptRunningState& state);

    // --- going places ------------------------------------------------------------------

    // A line, or line:column, typed into the same popover, the editor
    // showing the line as it is typed and going back on escape.
    void goToLine();
    void goToSymbol();
    // Every open tab, to pick one from by name.
    void showAllTabs();
    // Every command the menus hold, to give one by name.
    void showCommandPalette();
    // One field for going anywhere, as Visual Studio Code's: a script by
    // name -- a tab open in any studio window, one in an object the
    // explorer shows, one opened lately -- or, after a `>`, a command, which
    // is how the command palette opens it; the list follows the `>` as it
    // is typed or taken away.
    void showQuickOpen(bool commands);
    std::vector<ALQuickOpen::Candidate> paletteCommands();
    struct GoTo
    {
        enum class Kind : U8
        {
            Tab,
            Script,
            File
        };
        Kind                kind = Kind::Tab;
        LLHandle<LLFloater> window;
        std::string         id;
        ALScriptRef         ref;
        std::string         name;
        std::string         path;
    };
    std::vector<ALQuickOpen::Candidate> paletteScripts(std::vector<GoTo>& targets);
    // A bottom tab shown; and the keyboard put in its list, where asked.
    void        showBottom(const char* tab, bool focus = false);
    // The window's regions, in the order F6 goes round them: whether each
    // is shown (in the window or out of it), whether the keyboard is in it,
    // and the keyboard given to it -- its list, the text in front, the
    // tab's own control.
    enum class Region : U8
    {
        Explorer,
        Editor,
        Bottom,
        Inspector,
        COUNT
    };
    bool        regionShown(Region region) const;
    bool        regionHasKeys(Region region) const;
    void        focusRegion(Region region);
    // The next region shown after the one with the keyboard, or the one
    // before; from none, the text.
    void        cycleRegion(S32 direction);
    // What a region's key does: shown and given the keyboard, or, from its
    // key when it has the keyboard already, folded, the keyboard back to
    // the text. From the menu, with the mouse, the check mark folds it.
    void        regionKey(bool showing, bool has_keys, const std::function<void()>& show, const std::function<void()>& fold);
    std::string kindName(ALScriptSymbolKind kind) const;
    // A place in an include opened in a tab of its own where the include
    // is a script or a notecard in the world; one on disk is only named.
    void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length) override;
    // The file an include or a module the script names is, opened: the one
    // the last run of the preprocessor found, else one on disk where the
    // preprocessor would look. An include's, a module's, or either. False
    // where it is nowhere known.
    bool openIncluded(Doc& doc, const std::string& name, std::optional<bool> require) override;
    // A notecard a script names where it reads one, opened: the one of that
    // name in the script's own object, where a script reads it from. False,
    // and said, where there is none, or the script is in no object.
    bool openNotecardNamed(const Doc& doc, const std::string& name);
    // A notecard's References: every place the scripts of its object name
    // it where they read a notecard, each script read as it stands here or
    // as the region has it.
    void findNotecardReaders(const Doc& doc);
    // Find in files: words looked for across the scripts open, one
    // object's contents or every object the explorer lists, said as a
    // sentence over the Search tab; each place found a row, which opens
    // its script there. A script that is open is searched as it stands
    // in the editor, any other as the region has it, fetched if need be;
    // what arrives after another search has begun is dropped.
    void findInFiles();
    // The reference, in the inspector rather than a web page: a word of
    // the vocabulary shown with its declaration, the keyword file's
    // words about it and a link to its wiki page; F1 for the word at the
    // caret, or any word picked by name.
    void        showReference(const Vocab& word, bool lua) override;
    void        reference(Doc& doc);
    void        browseReference();
    // What an include is, as a mark: a file on disk, a notecard, a script.
    const char* includeImage(const std::string& path, bool lua) const;

    // --- menus, commands and keys ------------------------------------------------------

    // The keymap's keys beside the menu's editor commands that have none.
    void                showEditorKeys();
    // The Insert menu: snippets, functions, events or constants picked
    // by name and put in at the caret.
    void insertFromLibrary(const std::string& what);
    // The base's quick open, in the active editor's colours: what floats
    // over the text reads as the text's.
    ALQuickOpen* quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                           std::function<void(const std::string&)> chose, LLView* anchor = nullptr, S32 width = 0, S32 height = 0,
                           std::function<void()> escaped = {}, std::function<void(const std::string&)> hold = {},
                           std::function<void()> left = {});
    // The editor commands' keys as the keymap has them, and the menus'
    // own as a person rebound them, on the menus and the tips that say
    // them.
    void applyMenuKeys();
    void refreshKeyTips();
    // The window's own commands in the table, by the menu that gives them;
    // a unit split out of the window registers its own. An editor's own
    // command, on the view in front: `changes` for one that changes the
    // text, which a tab that may not be changed cannot do.
    void addCommands();
    // The keys the window answers to: the menus' commands, and the tab and
    // line keys no menu shows.
    void addKeys();
    // The menus' commands at the keys a person gave them.
    std::vector<ALKeyChord> keysOf(const KeyedCommand& command) const override;
    UndoKey                 undoKeyOf(KEY key, MASK mask) const override;
    void addFileCommands();
    void addEditCommands();
    void addInsertCommands();
    void addGoCommands();
    void addViewCommands();
    void addBuildCommands();
    void addHelpCommands();
    void addEditorCommand(const std::string& name, ALEditorCommand command, bool changes);
    // One of the text's own that no item of the menus gives: reached by its
    // keys alone.
    void addUnlistedEditorCommand(const std::string& name, ALEditorCommand command, bool changes);
    void editorCommand(const std::string& name, ALEditorCommand command, bool changes, bool listed);
    void onCompileTarget();
    // A notecard's grammar picked from the strip.
    void onNotecardGrammar();
    // The strip's experience, for the script in front: shown where it has
    // one or the agent has any to give it.
    void askExperienceOf(Doc& doc);
    void refreshExperience();
    // The strip under the editor laid out for what shows on it and the
    // width it has.
    void layStrip();
    void onExperience();
    void onReset();
    void revert(Doc& doc) override;

    // --- what every unit asks: the services, the tabs and the analysis -----------------

    // The services (ALScriptStudioServices).
    // A floater string in the form its count takes in the viewer's
    // language: the name with LLTrans's suffix -- A for one, B for many
    // in English, C where a language counts a third way -- with [COUNT]
    // filled in and whatever else the map holds; the plain name where
    // the skin has no such form, for a skin that has not been brought
    // up to the forms.
    std::string counted(const char* name, S32 count, LLStringUtil::format_map_t args = LLStringUtil::format_map_t()) const override;
    // The rest of them.
    void               setStatus(const std::string& text, bool failure = false) override { ALStudioFloater::setStatus(text, failure); }
    std::string        words(const std::string& name, const LLStringUtil::format_map_t& args = LLStringUtil::format_map_t()) const override;
    ALScriptStudioDoc* frontDoc() override { return active(); }
    S32                shownLine(S32 line, bool notecard) const override { return line + (notecard && mNotecardFromZero ? 0 : 1); }
    ALScriptStudioDoc* findDoc(std::string_view id) override;
    ALScriptStudioDoc* findDoc(const ALScriptRef& ref) override;
    std::vector<ALScriptStudioDoc*> openDocs() override;
    // What the studio did, said in the status line and kept in the
    // Output tab, where it can be read again: saving, compiling,
    // preprocessing, renaming. The script's name in it is a link to it;
    // and after it a link for each thing to be done about it: save anyway,
    // try again, save a copy, export.
    void report(const std::string& text, bool failure = false, const Doc* doc = nullptr, const std::vector<std::string>& actions = {}) override;
    // Where to go in a script once it is open, or now.
    void goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length) override;
    void revealed(LLUICtrl* list, bool to_editor) override { mNavigation.revealed(list, to_editor); }
    // The tabs (ALScriptStudioTabs).
    // The strip filled from the docs, and the toolbar put right. Each does
    // its work only when what it shows has actually changed since the
    // last (TabFacts, ToolbarFacts). One tab's facts moved -- a
    // keystroke's unsaved dot, a check's count -- that tab alone is made
    // again, where the strip is otherwise as it was.
    void   fillTabs() override;
    void   fillTabs(const Doc& doc);
    void   refreshToolbar() override;
    // The strip under the editor's right-hand words: the caret's place,
    // what is selected, and how many problems the script has.
    void   refreshTrailer(Doc& doc) override { mCrumbsBar->showTrailer(doc); }
    // The notice over the editor, for the tab in front.
    void refreshNotice() override { mOrphans.refreshNotice(); }
    Doc* openFileTab(const std::string& path, bool lua) override;
    void activate(Doc& doc) override;
    void takeCarriedText(Doc& doc) override;
    // The analysis (ALScriptStudioAnalysis), over the checking and the Problems
    // tab.
    void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at) override { mChecking.ask(doc, kind, at, at); }
    void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override;
    // The analyzers (ALScriptStudioChecking): a check is due a moment
    // after the last keystroke, sent from draw, answered whenever the
    // worker gets to it, and kept only if the text has not moved on.
    void scheduleAnalysis(Doc& doc, bool now = false) override { mChecking.schedule(doc, now); }
    bool lslFragment(const Doc& doc) const override { return mChecking.lslFragment(doc); }
    // The marks, the squiggles and the pane, from the compiler's problems
    // and the analyzer's together: asked for, and made once before the
    // next frame is drawn however often they were asked for meanwhile --
    // a check, its preprocessor run and its weighing each ask, one after
    // another (docChanged). Made now, where they are waiting to be, for
    // what is about to read them: a fix, the first error, the next problem.
    void refreshProblems(Doc& doc) override { docChanged(doc, CHANGED_PROBLEMS); }
    bool                          preprocessed(const Doc& doc) const override { return mChecking.preprocessed(doc); }
    std::string includeName(const Doc& doc, const std::string& path) const override { return mChecking.includeName(doc, path); }
    // An include's lines as it reads -- in its tab where it is open, else
    // as the preprocessor last read it -- found once for all its places;
    // none where neither has it.
    ALScriptPlaces::Lines         sourceLines(const std::string& path) const override;

    // --- what each unit asks of the window ---------------------------------------------

    // What checking asks of the window (ALScriptStudioChecking::Window):
    // the settings every question carries, an answer that is another
    // unit's, the outline shown, the editor in front, and whether to make
    // many fixes at once.
    void askingOptions(ALScriptAnalysis::Request& request) const override;
    void answeredElsewhere(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at) override;
    void showOutline(Doc& doc) override { mOutlinePane->show(doc); }
    ALCodeEditor&                       editorInFront(Doc& doc) override { return sourceInFront(doc); }
    void                                confirmFixAll(const LLSD& args, std::function<void()> yes, std::function<void()> preview) override;
    // What weighing (ALScriptStudioWeighing) asks of the window: its
    // weights asked for, the settings it goes by, and the Weights tab.
    void                                  askWeights(Doc& doc) override { askAnalyzer(doc, ALScriptAnalysis::Kind::Weigh, ALTextPos()); }
    bool optimizing() const override;
    std::string programVersion() const override;
    bool        weightNotes() const override { return mWeightNotes; }
    bool        weightHeat() const override { return mWeightHeat; }
    ALScriptWeightsPane* weightsPane() override { return mWeightsPane; }
    std::optional<ALScriptRegionUsage::Usage> regionOf(const Doc& doc) override;
    // What saving (ALScriptStudioSaving) asks of the window: how saves go,
    // as the settings say; a tab tidied, its text sent, and the Problems
    // tab shown.
    ALScriptStudioSaving::Options saveOptions() const override;
    void tidy(Doc& doc, bool fix, bool format, bool trim) override;
    bool send(const Doc& doc, const std::string& text, const ALScriptSaveOptions& options, std::string& error) override;
    bool sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& items, std::string& error,
                      U64 request) override;
    U64  newRequest() override;
    void takeLoaded(Doc& doc, const std::string& text) override;
    void worldAsset(const Doc& doc, std::function<void(std::optional<LLUUID>)> told) override;
    void takeCarried(Doc& doc) override { takeCarriedText(doc); }
    void showProblems() override;
    void selectFirstError(bool checkers_only) override;
    void                      runningKnown(Doc& doc) override;
    // What the files (ALScriptStudioFiles) ask of the window. A file's text
    // is what is on disk now, however it got there: the editor is clean,
    // and the scripts that include it are expanded again.
    void                          fileSettled(Doc& doc) override;
    // The viewer's pickers, the question over a file changed outside, a
    // file written that the studio reads, a tab become another file, Open
    // Recent.
    void                          pickFilesToOpen(bool several, std::function<void(const std::vector<std::string>& files)> chosen) override;
    void pickFileToSave(const std::string& name, std::function<void(const std::vector<std::string>& files)> chosen) override;
    void askReload(const Doc& doc, std::function<void(bool reload)> answered) override;
    void fileWritten(const std::string& path) override;
    // A file of a tab changed on disk, or went: what is in reach looked at.
    void reachChanged() override { mOrphansDirty = true; }
    void becomeFile(Doc& doc, const std::string& path) override;
    LLMenuGL* recentMenu() override;
    void      recentChanged() override { saveState(); }
    // A name looked for beyond the script (ALScriptLookup): the object's
    // other scripts in its language, read as the region has them and
    // expanded; the places found shown; the new name asked for in a
    // popover over the window, with a row saying what return will do as
    // it is typed.
    void candidates(const Doc& doc, std::function<void(ALScriptLookup::Candidates)> told) override;
    void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID& asset, const std::optional<std::string>& source)> loaded) override;
    void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) override;
    void showFound(Doc& doc, const ALScriptLookup::Found& found) override;
    void askNewName(Doc& doc, std::function<std::string(const std::string& typed)> hint, std::function<void(const std::string& name)> chosen,
                    std::function<void(const std::string& name)> previewed) override;
    void previewRename(Doc& doc, const ALScriptLookup::Found& found, const std::string& new_name, const std::string& said,
                       std::function<void(const std::vector<size_t>& kept)>                          apply,
                       std::function<void(const std::string& file, const std::vector<size_t>& kept)> changes) override;
    void applyPendingEdits(Doc& doc) override { mLookup.applyPendingEdits(doc); }
    // What the external editor (ALScriptExternalEditor) asks of the
    // window: the bridge and the editor's launch, which are the window's --
    // VS Code itself under tight integration, else the command the
    // ExternalEditor setting gives.
    std::string bridgeId(const Doc& doc) const override;
    std::shared_ptr<ALScriptTempFiles::Claim> holdCopy(const std::string& path) override;
    bool        subscribe(Doc& doc) override;
    void        unsubscribe(const Doc& doc) override;
    void        startEditor(Doc& doc, const std::string& file, bool on_disk) override;
    // What navigation (ALScriptNavigation) asks of the window.
    void showPlace(Doc& doc, Doc::View view, const ALTextPos& at) override;
    bool pathOpen(const std::string& path) const override;
    void choosePreview(ALPaneList* list) override;
    bool workedFrom(const Doc& doc) const override;
    void focusDoc(Doc& doc) override { focusShown(doc); }
    // What the orphans (ALScriptStudioOrphans) ask of the window: what is
    // in reach of a tab, which is the world's; its place; its script
    // loaded; the notice; and the notice's actions.
    ALScriptStudioOrphans::Reach reach(const Doc& doc) override;
    void                         refreshPlace(Doc& doc) override;
    void                         loadScript(const ALScriptRef& ref) override;
    ALScriptNoticeBar*           noticeBar() override { return mNoticeBar; }
    void                         discardRecovery(const ALRecoveryEntry& entry) override;
    void                         takeOffer(Doc& doc, const std::string& action) override { outputAction(doc, action); }
    void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                 const std::string& right_title) override;
    // A comparison shown in the tab's editor's place, lined up at the
    // anchors where there are any; what is typed in it goes to the source,
    // at the line the caret is on.
    void showCompare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title, const std::string& right_title,
                     const std::vector<std::pair<S32, S32>>& anchors);
    // A comparison whose right is the tab's text titled again, unsaved or
    // not as the tab now is.
    void retitleCompare(const Doc& doc) const;
    void endCompare(Doc& doc) override;
    // A tab made to hold a kept text with nothing loaded under it: unsaved,
    // with whatever its script or file was.
    void becomeOrphan(Doc& doc, const ALRecoveryEntry& entry, Doc::Orphan orphan) override;
    // What recovery asks of the window (ALScriptStudioRecovery::Window).
    Doc::Orphan failedAs(const Doc& doc, ALScriptLoaded::Failure failure) const override;
    bool recoverElsewhere(const ALRecoveryEntry& entry) override;
    bool scriptInHand(const ALScriptRef& ref) const override;
    void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
              std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) override;
    // What vim (ALScriptStudioVim) asks of the window: an entry in the
    // Output tab and the tab in sight, and a line picked from a list over
    // the editors.
    void output(const ALOutputView::Entry& entry) override;
    void showOutput() override;
    void reorderTabs(const std::vector<std::string>& order) override;
    bool readFile(const std::string& path, std::string& text) override;
    bool writeFile(const std::string& path, const std::string& text) override;
    std::vector<std::string> fileFolders(const Doc& doc) const override;
    void pickLine(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title, S32 rows,
                  std::function<void(const std::string& line)> chosen, std::function<void(const std::string& line)> shifted,
                  std::function<void()> cancelled) override;
    // Vim's vimrc: its text and where it is, opened here to be edited; and
    // every editor's options set again once it is read.
    std::string vimrc(std::string& whence) override;
    void        editVimrc() override;
    // A new vimrc's text: the skin's words as vim comments.
    std::string vimrcNewFile() const;
    void        refreshEditors() override { applyEditorOptions(); }
    // The name at the caret (ALScriptStudioCaret): a place gone to, the
    // bar's path, the inspector's problems, and whether the inspector is
    // out.
    void goTo(Doc& doc, const ALTextRange& range) override;
    void showPath(Doc& doc, bool changed) override
    {
        mCrumbsBar->showPath(doc);
        if (changed)
        {
            mOutlinePane->followCaret(doc);
        }
    }
    bool showProblemsAt(Doc& doc, const ALTextPos& at) override;
    bool inspectorShown() const override;

    // --- what each pane asks of the window ---------------------------------------------

    // The notice bar's buttons (ALScriptNoticeBar::Window).
    void noticeAction(const std::string& action) override { mOrphans.noticeAction(action); }
    // A row, a reference, a place found, an outline entry chosen: the
    // place shown in its script, and the keyboard left in the list to walk
    // on through it; or, asked for with return or a double-click, taken
    // to the script to type there. Tabs opened on the way leave the panes
    // on what they were listing.
    void problemChosen(const ALScriptProblemsPane::Place& place, bool to_editor) override;
    void settleProblems(Doc& doc) override;
    void runtimeCleared(Doc& doc) override;
    // The rest of what the Problems tab asks of the window
    // (ALScriptProblemsPane::Window).
    void                 problemCountsChanged() override { refreshBottomTabs(); }
    void                 problemFiltersChanged() override { saveState(); }
    std::string          problemIcon(const Doc& doc, const std::string& include) const override;
    std::string          scriptIcon(bool lua, const std::string& include) const override;
    void                 fixAll(Doc& doc, const FixPick& pick) override;
    bool                 isLint(bool lua, const std::string& id) const override;
    ALScriptLints::Level lintLevel(bool lua, const std::string& id) const override;
    void                 setLintLevel(bool lua, const std::string& id, ALScriptLints::Level level) override;
    void                 showLintSettings() override;
    // The places last found, whichever tab is in front (the References
    // tab): following them opens other scripts, and the list stays what
    // it was.
    void referenceChosen(const ALScriptReferencesPane::Found& found, const Doc::Place& place, bool to_editor) override;
    void referencesCounted() override { refreshBottomTabs(); }
    // The Weights tab: whether it is looked at; and a part chosen there,
    // gone to.
    bool weightsShown() const override;
    void onWeightChosen(bool to_editor);
    // The outline (ALScriptOutlinePane): shown, which the bar at the
    // bottom is told of; a symbol chosen, gone to; its sort kept.
    void        outlineShown(Doc& doc) override { mCaret.placePath(doc); }
    void        outlineChosen(Doc& doc, const ALScriptOutlineEntry& entry, bool to_editor) override;
    void        outlineSortChanged() override { saveState(); }
    // The bar under the editor (ALScriptCrumbsBar): a step chosen, gone
    // to; a word past the path pressed -- the caret's place opens Go to
    // Line, the counts the problems, the view the other view; and vim's
    // word.
    void        crumbChosen(Doc& doc, std::optional<ALTextRange> at) override;
    void        trailerChosen(const std::string& value) override;
    std::string vimBanner() const override { return mVim.banner(); }
    std::optional<ALScriptWeight::Target> weightTarget(const Doc& doc) const override { return mWeighing.target(doc); }
    ALPreprocessor::Wanted                preprocessedWhy(const Doc& doc) const override { return mChecking.preprocessedWhy(doc); }
    // How many errors and warnings a script shows.
    void   problemCounts(const Doc& doc, S32& errors, S32& warnings) const override;
    // What the Output tab asks of the window (ALScriptOutputPane::Window).
    bool outputInSight() const override;
    void outputUnreadChanged() override { refreshBottomTabs(); }
    bool ownsObject(const LLUUID& root) const override;
    void outputAction(Doc& doc, const std::string& action) override;
    void outputShowDoc(Doc& doc, bool problems) override;
    void outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, bool running) override;
    void outputGoToInclude(const std::string& file, const std::string& file_name, S32 line, S32 column) override;
    // What the explorer asks of the window (ALScriptExplorerPane::Window).
    void showExplorer() override;
    void explorerPinsChanged() override { saveState(); }
    void checkScripts(const LLUUID& root) override { checkObject(root); }
    void itemRenamed(const ALScriptRef& ref, const std::string& name) override;
    void itemDeleted(const ALScriptRef& ref) override;
    bool unsavedAnywhere(const ALScriptRef& ref) const override;
    void compareItems(const ALScriptRef& first, const std::string& name, const std::string& first_title, const ALScriptRef& second,
                      const std::string& second_title) override;
    void showHistory(const ALScriptRef& ref, const std::string& name) override;
    // What the Search tab asks of the window (ALScriptSearchPane::Window).
    void listObjects(const LLUUID& only, std::function<void(std::vector<ALScriptSearchPane::Window::Object>)> told) override;
    std::string                                     objectName(const LLUUID& root) const override;
    std::string                                     whereIs(const Doc& doc) const override;
    LLUUID                                          objectInHand() const override;
    LLUUID                                          rootOf(const ALScriptRef& ref) const override;
    Doc*                                            openElsewhere(const ALScriptRef& ref) override;
    void fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where) override;
    std::vector<InventoryItem> inventoryItems() override;
    void matchApart(std::shared_ptr<const std::string> text, const std::string& query, const ALTextSearchOptions& options,
                    std::function<void(ALScriptSearch::Matched)> matched) override;
    void confirmReplaceAll(const LLSD& args, std::function<void()> yes) override;
    void searchResultChosen(const ALScriptSearch::Found& one, const ALTextRange& place, bool to_editor) override;
    std::vector<ALScriptSearchPane::Window::Included> includesOf(const ALScriptRef& ref, const std::string& file, const std::string& name,
                                                                 const std::string& text, bool lua) override;
    // Where a hover or the inspector says a name was declared, gone to: in
    // the script, or in the include.
    void goToDeclared(const LLSD& value) override;

    // Held while the quick open follows its `>`; and whether the bottom
    // panel waits folded for something to show (firstOpen).
    boost::signals2::scoped_connection mQuickModeConnection;
    bool mBottomWaiting = false;
    std::vector<std::unique_ptr<Doc>>  mDocs;
    size_t                             mActive = NONE;
    // What the strip and the toolbar last stood for: a keystroke asks
    // for both, and neither changes with most of them.
    struct TabFacts
    {
        std::string id;
        std::string name;
        bool        dirty  = false;
        bool        preview = false;
        bool        readOnly = false;
        S32         errors = 0;
        S32         warnings = 0;
        const char* image  = nullptr;
        friend bool operator==(const TabFacts& a, const TabFacts& b)
        {
            return a.id == b.id && a.name == b.name && a.dirty == b.dirty && a.preview == b.preview && a.readOnly == b.readOnly && a.errors == b.errors &&
                   a.warnings == b.warnings && a.image == b.image;
        }
        friend bool operator!=(const TabFacts& a, const TabFacts& b) { return !(a == b); }
    };
    TabFacts tabFactsOf(const Doc& doc) const;
    // The tab a doc is drawn as, from its facts.
    ALTabStrip::Tab tabOf(const Doc& doc, const TabFacts& facts) const;
    // What the toolbar is made from, as it was when last put right: it is
    // put right again only where one of these has moved. Whatever
    // refreshToolbar reads is here.
    struct ToolbarFacts
    {
        std::string         id;
        bool                loaded     = false;
        bool                modifiable = false;
        bool                notecard   = false;
        bool                file       = false;
        bool                inventory  = false;
        bool                sending    = false;
        bool                anyDirty   = false;
        bool                canUndo    = false;
        bool                canRedo    = false;
        bool                expandable = false;
        U8                  view       = 0;
        S32                 running    = -1;
        bool                publicObject = false;
        bool                regionLua    = false;
        bool                lua          = false;
        std::string         target;
        std::string         grammar;
        bool                experienceKnown  = false;
        bool                experienceChosen = false;
        bool                experienceAsking = false;
        LLUUID              experience;
        std::vector<LLUUID> ownExperiences;
        bool                operator==(const ToolbarFacts&) const = default;
    };
    ToolbarFacts                toolbarFactsOf() const;
    std::optional<ToolbarFacts> mToolbarFacts;
    std::vector<TabFacts>              mTabFacts;
    size_t                             mTabFactsActive = NONE;
    // What a tab's tip says of where its script is, read from the skin
    // once rather than for every tab at every refill.
    struct TabTips
    {
        std::string notecard;
        std::string inventory;
        std::string object;
        std::string readOnly;
    };
    TabTips                            mTabTips;
    // What changed of a tab that the window shows of it, made good once
    // before the next frame is drawn, however often it was asked for in
    // between (docChanged); and made good now, all of it or one tab's.
    enum DocChange : U8
    {
        CHANGED_PROBLEMS = 1 << 0,
        // A run-time error heard: its problems, made no more often than
        // RUNTIME_EVERY while a looping script says one after another --
        // the first at once, the rest together at the end of the turn.
        CHANGED_RUNTIME = 1 << 1,
    };
    static constexpr F64 RUNTIME_EVERY = 0.25;
    void                 docChanged(Doc& doc, U8 what);
    void                 settleChanges(bool all_now = false);
    void                 makeProblems(Doc& doc);
    struct DocChanges
    {
        U8  what = 0;
        // Not to be made before then.
        F64 due  = 0.0;
        // When its problems were last made, for a run-time error's turn.
        F64 made = -RUNTIME_EVERY;
    };
    boost::unordered_flat_map<std::string, DocChanges, ll::string_hash, std::equal_to<>> mDocChanges;
    bool                                                                                 mDocsChanged = false;
    // Tabs opened, restored or closed many at once: while one of these is
    // held, what each tab would make again -- the strip, the toolbar, the
    // tab in front shown, the explorer's list, Output's filter -- waits,
    // and is made once as the last lets go. Which tab is in front is
    // decided as it goes; showing it waits.
    class TabsHeld
    {
    public:
        explicit TabsHeld(ALFloaterScriptStudio& window) : mWindow(window) { ++mWindow.mTabsHeld; }
        ~TabsHeld()
        {
            if (--mWindow.mTabsHeld == 0)
            {
                mWindow.releaseTabs();
            }
        }
        TabsHeld(const TabsHeld&)            = delete;
        TabsHeld& operator=(const TabsHeld&) = delete;

    private:
        ALFloaterScriptStudio& mWindow;
    };
    struct HeldTabs
    {
        bool tabs     = false;
        bool toolbar  = false;
        bool output   = false;
        bool relist   = false;
        bool activate = false;
        bool focus    = false;
    };
    void     releaseTabs();
    S32      mTabsHeld = 0;
    HeldTabs mHeld;
    // Tabs coming back from the last session open beside what is open: not
    // brought in front of what is being worked on, and never given the
    // keyboard (F19).
    bool     mOpeningBehind = false;
    // The explorer listed again, now or as the hold lets go.
    void     relistExplorer();
    // The docs by id, and the scripts' by what they are, for the lookups
    // every answer makes; kept as tabs come and go, and Output's list of
    // what the open scripts said filtered again only where which scripts
    // are open changed.
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> mByDocId;
    boost::unordered_flat_map<ALScriptRef, size_t>                                   mByRef;
    void                               reindexDocs();
    // A tab known by another id from here on -- a file saved under
    // another name: the index, its editors' names, and whatever holds it
    // by its id, the panes and the history, told.
    void                               rekeyDoc(Doc& doc, const std::string& id);
    // The tabs open when the state was last written, opened again once the
    // window is built; and those whose object or item was not in hand yet,
    // opened once it is, until a while after.
    LLSD                               mRestoreTabs;
    struct PendingRestore
    {
        ALScriptRef ref;
        F64         until = 0.0;
        bool        asked = false;
        Doc::View   view  = Doc::View::Source;
    };
    std::vector<PendingRestore>        mPendingRestores;
    void                               pumpRestores();
    void                               restoreListed(const ALScriptRef& ref, const ALScriptContents& contents);
    // The viewer is quitting and this window was asked to close; and the
    // tabs as they were when it was, which the state keeps for next time.
    bool                               mAppQuitting = false;
    LLSD                               mTabsAtQuit;
    // The main window's: the windows popped out of it as the viewer went,
    // each what it had open and where it was, made again next time.
    LLSD                               mWindowsAtQuit = LLSD::emptyArray();
    LLSD                               mRestoreWindows;
    void                               adoptWindowAtQuit(const LLSD& window);
    // A popped-out window made again to restore what it had: closed if
    // nothing of it comes.
    bool                               mRestoring = false;
    // The tabs of a window's state opened in it; the popped-out windows
    // made again; and what was kept on purpose at the quit opened again.
    // How many it opened or waits for.
    S32                                restoreTabs(const LLSD& open);
    void                               restoreWindows(const LLSD& windows);
    void                               reopenKept();
    // The tips that say a menu item's keys, as the skin wrote them, to be
    // said again when a key is rebound.
    // The menu bar's items by name, found once.
    boost::unordered_flat_map<std::string, LLMenuItemGL*, ll::string_hash, std::equal_to<>> mMenuItems;
    LLMenuItemGL*                                                                           menuItem(std::string_view id) const;
    std::vector<std::pair<std::string, std::vector<std::string>>> mKeyTips;
    std::map<std::string, std::string> mKeyTipTexts;
    // What Edit > Undo and Redo were last named for, and what that was
    // worked out from: the tab, the field with the keyboard, the text
    // shown, its history's revision, and what another field could undo.
    std::string                        mUndoSaid;
    std::string                        mRedoSaid;
    struct UndoSaidOf
    {
        const void* doc      = nullptr;
        const void* field    = nullptr;
        const void* text     = nullptr;
        U32         revision = 0;
        bool        canUndo  = false;
        bool        canRedo  = false;
        bool operator==(const UndoSaidOf&) const = default;
    };
    UndoSaidOf                         mUndoSaidOf;
    // The notice over the editor.
    ALScriptNoticeBar*                 mNoticeBar = nullptr;
    // Whether the connection was seen lost; and when what holds each tab
    // was last looked at.
    bool                               mOffline        = false;
    // Whether what is in reach of a tab may have changed since the orphans
    // were last looked at, which the inventory, objects coming and going,
    // prims' contents and files say; when they asked to be looked at again
    // for time alone; and a look now and then all the same, for what says
    // nothing -- a name heard.
    bool                               mOrphansDirty    = true;
    F64                                mOrphansDue      = 0.0;
    F64                                mOrphansBackstop = 0.0;
    std::unique_ptr<LLInventoryObserver> mInventoryHeard;
    boost::signals2::scoped_connection mOrphansPresence;
    boost::signals2::scoped_connection mOrphansContents;
    // Whether a tab holds a script of the prim.
    bool                               holdsScriptOf(const LLUUID& prim) const;
    // What the editors' vim keymaps share: the : and / lines entered in
    // any of them, and the settings a :set changes.
    bool                               mWordWrap    = false;
    // A comparison shown inline rather than side by side.
    bool mCompareInline = false;
    bool                               mLineNumbers = true;
    bool                               mIndentGuides    = true;
    // Where the blanks are drawn as marks; under the selection to begin
    // with, which is where they are wanted and nowhere else.
    ALCodeEditor::Whitespace           mWhitespace      = ALCodeEditor::Whitespace::Selection;
    bool                               mRelativeNumbers = false;
    // A notecard's own: it wraps, and numbers its lines as a script reads
    // them, from 0.
    bool                               mNotecardWrap        = true;
    bool                               mNotecardLineNumbers = true;
    bool                               mNotecardFromZero    = true;
    bool                               mRainbowBrackets = true;
    // What the analyzers add to the picture: every name coloured by what
    // it is, and the words shown beside the text.
    bool                               mSemanticColors  = true;
    bool                               mInlayParameters = true;
    bool                               mInlayTypes      = true;
    // What the code weighs, beside it: each function's bytes after the
    // line it is declared on, and the gutter's strip of heat by line. Off
    // until asked for.
    bool                               mWeightNotes     = false;
    bool                               mWeightHeat      = false;
    // The words the dictionary lacks squiggled: in comments and strings
    // of a script, throughout a notecard.
    bool                               mSpellCheck      = true;
    // Vim over every editor: the mode in the bottom strip, the : line
    // and what it says in the status line, and w, q and the rest
    // answered here.
    bool                               mVimMode      = false;
    bool                               mStickyHeaders   = true;
    // The scrollbar as a map: whether, how wide, whether it previews the
    // lines under the mouse, and on which side.
    bool                               mScrollMap        = false;
    S32                                mScrollMapWidth   = 90;
    bool                               mScrollMapPreview = true;
    bool                               mScrollMapLeft    = false;
    LLPanel*                           mEditorHost    = nullptr;
    // What the window says with no script open, over the room the
    // editors would be in: it is the only pane with no list of its own to
    // carry a sentence, and it was the one showing nothing at all.
    ALEmptyState*                      mNoDocs        = nullptr;
    ALTabStrip*                        mTabs          = nullptr;
    ALScriptCrumbsBar*                 mCrumbsBar     = nullptr;
    LLTabContainer*                    mBottomTabs    = nullptr;
    ALScriptProblemsPane*              mProblemsPane = nullptr;
    ALScriptReferencesPane*            mReferencesPane = nullptr;
    ALScriptOutlinePane*               mOutlinePane    = nullptr;
    ALScriptInspectorPane*             mInspectorPane = nullptr;
    // Held while a pane's row is followed into a tab, so that the panes
    // go on listing what they were rather than the tab's.
    S32                                mHoldPanes       = 0;
    // Whether the trailer last said a check is out, for the tab in front.
    bool                               mTrailerChecking = false;
    ALScriptOutputPane*                mOutputPane = nullptr;
    ALScriptSearchPane*                mSearchPane    = nullptr;
    // The Weights tab, and its list of parts.
    ALScriptWeightsPane*               mWeightsPane = nullptr;
    ALPaneList*                        mWeightsParts      = nullptr;
    ALScriptExplorerPane*              mExplorerPane  = nullptr;
    // What each of the menus' items does, whether it can, and whether it
    // is on, by the item's name.
    ALScriptStudioCommands             mCommands;
    // What is unsaved in the tabs, kept against the viewer going.
    ALScriptStudioRecovery             mRecovery{ *this, *this, *this };
    // What its items were saved as before, to compare and put back.
    ALScriptStudioHistory              mHistory{ *this, *this };
    // What the region said an object reserves, heard.
    boost::signals2::scoped_connection mRegionUsageConnection;
    // Saving and compiling the tabs.
    ALScriptStudioSaving               mSaving{ *this, *this, *this, mNavigation, mExternal, mWeighing, mRecovery, mFiles, mOrphans, *this };
    // The window's side of vim, over its editors.
    ALScriptStudioVim                  mVim{ *this, *this, mSaving, mNavigation, mCommands, *this };
    // Its tabs held open in an editor outside.
    ALScriptExternalEditor             mExternal{ *this, *this, mSaving, mFiles, *this };
    // Its files on disk, and the recent lists.
    ALScriptStudioFiles                mFiles{ *this, *this, *this, mSaving, *this };
    // What its scripts weigh.
    ALScriptStudioWeighing             mWeighing{ *this, *this, mSaving, *this };
    // Its tabs whose script is gone or out of reach, and the notice.
    ALScriptStudioOrphans              mOrphans{ *this, *this, mSaving, mRecovery, mFiles, *this };
    // The places gone from, and the previews a list opens as it is walked.
    ALScriptNavigation                 mNavigation{ *this, *this, *this };
    // Its names looked up across the object's scripts, and renamed.
    ALScriptLookup                     mLookup{ *this, *this, *this, mNavigation, *this };
    // Every script of an object checked, and what it is called while it is.
    ALScriptObjectCheck                mObjectCheck{ *this, *this, *this };
    // Recompiles from the Explorer.
    ALScriptRecompile                  mRecompile{ *this, *this };
    std::string                        mCheckingWhere;
    S32                                mCheckedErrors   = 0;
    S32                                mCheckedWarnings = 0;
    // The name at its caret, and the caret watched.
    ALScriptStudioCaret                mCaret{ *this, *this, mNavigation, mLookup, *this };
    // Its checking: the analyzers asked and answered, and fixes.
    ALScriptStudioChecking             mChecking{ *this, *this, mSaving, mWeighing, *this };
    ALMenuSlot                         mTabMenu;
    bool                               mMain = true;
    bool                               mClosingWindow = false;
    LLComboBox*                        mCompileTarget = nullptr;
    LLComboBox*                        mNotecardGrammar = nullptr;
    // Which notecard's readers were asked for last: an answer for an
    // earlier one is let go of.
    U32                                mReadersAsked = 0;
    LLCheckBoxCtrl*                    mRunning       = nullptr;
    LLComboBox*                        mExperience    = nullptr;
    LLButton*                          mExperienceProfile = nullptr;
    // How wide the skin made the experience's box, which is as wide as it
    // gets.
    S32                                mExperienceWidth   = 0;
    // The strip's width when it was last laid out.
    S32                                mStripLaid         = -1;
    // What the experience list was last made of, so that it is made again
    // only when that changes; and the experiences whose names are asked.
    std::string                        mExperienceMadeOf;
    boost::unordered_flat_set<LLUUID>  mExperienceNamesAsked;
    LLButton*                          mResetButton   = nullptr;
    LLButton*                          mSaveButton    = nullptr;
    LLButton*                          mSaveAllButton = nullptr;
    LLButton*                          mUndoButton    = nullptr;
    LLButton*                          mRedoButton    = nullptr;
    LLButton*                          mFindButton    = nullptr;
    LLButton*                          mFormatButton  = nullptr;
    LLButton*                          mExpandedButton = nullptr;
    boost::signals2::scoped_connection mCompiledConnection;
    boost::signals2::scoped_connection mSavedConnection;
    boost::signals2::scoped_connection mDefinitionsConnection;
    // The vimrc changed: read again into the studio's vim, which every
    // window shares, and this window's editors set again from it.
    boost::signals2::scoped_connection mVimrcConnection;
    // The settings the window follows as they change: the lints and the
    // Luau mode, the preprocessor's, vim's clipboard.
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    boost::signals2::scoped_connection mRuntimeConnection;
    boost::signals2::scoped_connection mRunningConnection;
};

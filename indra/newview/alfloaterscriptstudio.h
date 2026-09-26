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
#include "alscriptnotecardtab.h"
#include "alscriptoutputpane.h"
#include "alscriptproblemspane.h"
#include "alscriptsearchpane.h"
#include "alscriptstudiocommands.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiorecovery.h"
#include "alscriptstudiosaving.h"
#include "alscriptstudiovim.h"
#include "alscriptexternaleditor.h"
#include "alscriptstudiofiles.h"
#include "alscriptstudioweighing.h"
#include "alscriptstudiowords.h"
#include "alscriptstudioorphans.h"
#include "alscriptnavigation.h"
#include "alscriptlookup.h"
#include "alscriptreferencespane.h"
#include "alscriptoutlinepane.h"
#include "alscriptcrumbsbar.h"
#include "alscriptinspectorpane.h"
#include "alscriptstudiocaret.h"
#include "alscriptstudiochecking.h"
#include "alscriptstudioplaces.h"
#include "alfindings.h"
#include "aloutputview.h"
#include "alscriptanalysis.h"
#include "alscriptsnippets.h"
#include "alscriptenvelope.h"
#include "alscriptpreprocessor.h"
#include "alscriptrecovery.h"
#include "alscriptworkspace.h"
#include "alsourcemap.h"
#include "alstudiofloater.h"
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
class ALTabStrip;
class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
class LLFilterEditor;
class LLLayoutPanel;
class LLLineEditor;
class LLPanel;
class LLScrollListCtrl;
class LLContextMenu;
class ALScopeBar;
class LLEditMenuHandler;
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
class ALFloaterScriptStudio final : public ALStudioFloater, public ALScriptStudioServices, public ALScriptOutputPane::Window, public ALScriptProblemsPane::Window,
                                    public ALScriptSearchPane::Window, public ALScriptExplorerPane::Window, public ALScriptStudioRecovery::Window,
                                    public ALScriptStudioSaving::Window, public ALScriptStudioVim::Window,
                                    public ALScriptExternalEditor::Window, public ALScriptStudioFiles::Window,
                                    public ALScriptStudioWeighing::Window, public ALScriptStudioOrphans::Window,
                                    public ALScriptNoticeBar::Window, public ALScriptNavigation::Window, public ALScriptLookup::Window,
                                    public ALScriptReferencesPane::Window, public ALScriptOutlinePane::Window,
                                    public ALScriptCrumbsBar::Window, public ALScriptInspectorPane::Window,
                                    public ALScriptStudioCaret::Window, public ALScriptStudioChecking::Window
{
    friend class LLFloaterReg;

public:
    AL_VIEW_TYPE(ALFloaterScriptStudio, ALStudioFloater);

    // Whether scripts open here rather than in the legacy floaters: the
    // ALScriptStudioEnabled setting, on unless someone turned it off,
    // which is how the two share a viewer for a release.
    static bool wantsScripts();
    // The studio, with this script open in it: the window that has it
    // open already, else the main one; null where a restriction keeps
    // the studio from opening.
    static ALFloaterScriptStudio* open(const ALScriptRef& ref, const std::string& name = std::string(), bool take_focus = true);
    // The scripter's own snippets for a language, opened in the studio as
    // the XML they are kept in: made with an example in it where there is
    // none yet. What is saved there is offered at once.
    static void editSnippets(bool lua);
    // The vimrc opened in the studio to be edited: the notecard where one
    // is the vimrc, else the file, made with a few lines saying what it
    // takes where there is none yet.
    static void openVimrc();
    // The studio, with this object pinned in its explorer and chosen
    // there: what the build tool's Explore in IDE button means when no
    // external editor is listening.
    static ALFloaterScriptStudio* explore(const LLUUID& root);
    // At login: what an earlier session left unsaved -- a crash, a lost
    // connection -- offered back, to open, to leave for later, or to
    // discard.
    static void offerRecovery();
    // A script saved from outside the studio -- by an external editor
    // over the bridge -- with this text: the tab that holds it, if one
    // does, shows the text as saved. A tab with unsaved changes takes
    // the text as one more step to undo, so that nothing typed is lost.
    // The asset the save made, where the saver knows it.
    static void savedElsewhere(const ALScriptRef& ref, const std::string& text, const LLUUID& asset_id = LLUUID::null);
    // A script or notecard gone from its object, so that a tab holding
    // it goes too.
    static void itemRemoved(const ALScriptRef& ref);
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
    // on every studio open, which the preferences ask for.
    void        applyEditorOptions();
    static void refreshAll();

    // A word of the language, as the region defines it (ALScriptStudioWords).
    typedef ALScriptStudioWords::Vocab Vocab;
    // The typing settings put on an editor: tabs, completion, pairs, the
    // caret, the hover card. The studio's editors and the preferences'
    // preview share them.
    static void applyTypingOptions(ALCodeEditor& editor);
    // The font the settings name, or the monospace default.
    static const LLFontGL* editorFont();

private:
    ALFloaterScriptStudio(const LLSD& key);
    ~ALFloaterScriptStudio() override;

    // One tab (alscriptstudiodoc.h).
    using Doc = ALScriptStudioDoc;
    static constexpr size_t NONE = static_cast<size_t>(-1);
    // The problems of every open script, in the store all the studios'
    // findings live in, keyed by the script's id; the pane lists the
    // active script's through its filters.
    // The caret to the next problem of the script after it, or the one
    // before, round past the ends, with what it says in a card.
    void                goToProblem(Doc& doc, S32 direction);
    // The script's own problems' places, each once and in order; the caret
    // taken to one; and to one by its number (ALScriptStudioVim::Window).
    std::vector<ALTextPos> problemPlaces(const Doc& doc) const;
    void                   goToProblemAt(Doc& doc, const ALTextPos& to);
    bool                   goToProblemNumber(Doc& doc, S32 number) override;
    // A fix made, the script checked again (ALScriptStudioChecking).
    bool                applyFix(Doc& doc, const ALScriptFix& fix, U32 version) override { return mChecking.applyFix(doc, fix, version); }
    using FixPick = Doc::FixPick;
    // The keymap's keys beside the menu's editor commands that have none.
    void                showEditorKeys();

    using ProblemTraits = Doc::ProblemTraits;

    // A snippet: a body with placeholders, offered by name from the
    // Insert menu and by prefix among the completions; the viewer's and
    // the scripter's own (ALScriptSnippets).
    typedef ALScriptSnippets::Snippet Snippet;
    static const std::vector<Snippet>& snippets(bool lua) { return ALScriptSnippets::all(lua); }
    // The Insert menu: snippets, functions, events or constants picked
    // by name and put in at the caret.
    void insertFromLibrary(const std::string& what);

    // The base's quick open, in the active editor's colours: what floats
    // over the text reads as the text's.
    ALQuickOpen* quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                           std::function<void(const std::string&)> chose, LLView* anchor = nullptr, S32 width = 0, S32 height = 0,
                           std::function<void()> escaped = {}, std::function<void(const std::string&)> hold = {},
                           std::function<void()> left = {});

    // A floater string in the form its count takes in the viewer's
    // language: the name with LLTrans's suffix -- A for one, B for many
    // in English, C where a language counts a third way -- with [COUNT]
    // filled in and whatever else the map holds; the plain name where
    // the skin has no such form, for a skin that has not been brought
    // up to the forms.
    std::string counted(const char* name, S32 count, LLStringUtil::format_map_t args = LLStringUtil::format_map_t()) const override;
    // The rest of what the units split out of the window ask of it
    // (ALScriptStudioServices).
    void               setStatus(const std::string& text, bool failure = false) override { ALStudioFloater::setStatus(text, failure); }
    std::string        words(const std::string& name, const LLStringUtil::format_map_t& args = LLStringUtil::format_map_t()) const override;
    ALScriptStudioDoc* frontDoc() override { return active(); }
    ALScriptStudioDoc* findDoc(std::string_view id) override;
    ALScriptStudioDoc* findDoc(const ALScriptRef& ref) override;
    std::vector<ALScriptStudioDoc*> openDocs() override;

    Doc*   active();
    size_t indexOf(const ALScriptRef& ref) const;
    size_t indexOf(std::string_view id) const;
    // The tab in front, its editor given the keyboard where asked.
    void   activate(size_t index, bool focus = true);
    // The strip filled from the docs, and the toolbar put right. Both
    // are asked for on every keystroke; each does its work only when
    // what it shows has actually changed since the last.
    void   fillTabs() override;
    void   refreshToolbar() override;
    // The strip under the editor's right-hand words: the caret's place,
    // what is selected, and how many problems the script has.
    void   refreshTrailer(Doc& doc) override { mCrumbsBar->showTrailer(doc); }
    // What vim (ALScriptStudioVim) asks of the window: an entry in the
    // Output tab and the tab in sight, and a line picked from a list over
    // the editors.
    void output(const ALOutputView::Entry& entry) override;
    void showOutput() override;
    void reorderTabs(const std::vector<std::string>& order) override;
    bool readFile(const std::string& path, std::string& text) override;
    bool writeFile(const std::string& path, const std::string& text) override;
    std::vector<std::string> fileFolders(const Doc& doc) const override;
    void jumpedFrom(Doc& doc, const ALTextView& view, const ALTextPos& from) override;
    void pickLine(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title, S32 rows,
                  std::function<void(const std::string& line)> chosen, std::function<void(const std::string& line)> shifted,
                  std::function<void()> cancelled) override;
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
    // How many errors and warnings a script shows.
    void   problemCounts(const Doc& doc, S32& errors, S32& warnings) const override;

    ALCodeEditor*             makeEditor(const std::string& id, bool read_only);
    // What an editor of the studio's is made with: its colours by the
    // table's names, the studio's settings.
    ALCodeEditor::Params      editorParams(const std::string& id, bool read_only) const;
    // The options every editor shares, put on one.
    void                      applyEditorOptions(ALCodeEditor& editor);
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
    void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at) override { mChecking.ask(doc, kind, at, at); }
    // What checking asks of the window (ALScriptStudioChecking::Window):
    // the settings every question carries, an answer that is another
    // unit's, the outline shown, the targets weighed for, a run for a
    // save, the editor in front, and whether to make many fixes at once.
    void askingOptions(ALScriptAnalysis::Request& request) const override;
    void answeredElsewhere(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at) override;
    void showOutline(Doc& doc) override { mOutlinePane->show(doc); }
    std::vector<ALScriptWeight::Target> weightTargets(const Doc& doc) override { return mWeighing.targets(doc); }
    void                                preprocessForSave(Doc& doc) override { mSaving.preprocess(doc); }
    ALCodeEditor&                       editorInFront(Doc& doc) override { return sourceInFront(doc); }
    void                                confirmFixAll(const LLSD& args, std::function<void()> yes) override;
    // Weighing (ALScriptStudioWeighing): saving's calls to it, and what
    // it asks of the window.
    std::optional<ALScriptWeight::Target> weightTarget(const Doc& doc) const override { return mWeighing.target(doc); }
    void                                  weigh(Doc& doc) override { mWeighing.weigh(doc); }
    void                                  weighSent(Doc& doc) override { mWeighing.weighSent(doc); }
    void                                  keepSavedWeights(Doc& doc) override { mWeighing.keepSaved(doc); }
    void                                  askWeights(Doc& doc) override { askAnalyzer(doc, ALScriptAnalysis::Kind::Weigh, ALTextPos()); }
    void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override;
    bool optimizing() const override;
    std::string programVersion() const override;
    bool        weightNotes() const override { return mWeightNotes; }
    bool        weightHeat() const override { return mWeightHeat; }
    void        warnOverWeight(Doc& doc) override { mSaving.warnOverWeight(doc); }
    ALScriptWeightsPane* weightsPane() override { return mWeightsPane; }

    // The preprocessor: whether it applies to a script; its run over the
    // text as it stands, for the analyzers, with the way back; and its
    // run ahead of a save, fetching includes, then the upload in the
    // envelope. Positions the analyzers answer with are mapped back to
    // the source, and what falls in an include is listed by its file.
    bool                          preprocessed(const Doc& doc) const override { return mChecking.preprocessed(doc); }
    // A run of it over the text as it stands, fetching its includes, for
    // saving (ALScriptStudioSaving::preprocess).
    void                          runPreprocessor(const Doc& doc, std::function<void(const ALPreprocessor::Result&)> answer) override;
    std::string includeName(const Doc& doc, const std::string& path) const override { return mChecking.includeName(doc, path); }
    void                          chooseIncludeFolder();
    // A file on disk opened in a tab of its own, or brought forward, at
    // a place in it where one is given; read as the language its
    // extension says, or the one it was included from.
    void                          openFile(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    // Opened in this window whatever another has open: where a tab is
    // being moved here from it.
    void                          openFileHere(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    // A file's text written back where it came from, and the file watched
    // for changes made outside the studio (ALScriptStudioFiles).
    void                          saveFile(Doc& doc) override { mFiles.write(doc); }
    void                          watchFile(Doc& doc) override { mFiles.watch(doc); }
    // A file's text is what is on disk now, however it got there: the
    // editor is clean, and the scripts that include it are expanded again.
    void                          fileSettled(Doc& doc) override;
    // The files' asks of the window: the viewer's pickers, the question
    // over a file changed outside, a save that did not go, a file written
    // that the studio reads, a tab become another file, Open Recent.
    void                          pickFilesToOpen(bool several, std::function<void(const std::vector<std::string>& files)> chosen) override;
    void pickFileToSave(const std::string& name, std::function<void(const std::vector<std::string>& files)> chosen) override;
    void askReload(const Doc& doc, std::function<void(bool reload)> answered) override;
    void saveStopped(Doc& doc) override;
    void fileWritten(const std::string& path) override;
    void becomeFile(Doc& doc, const std::string& path) override;
    LLMenuGL* recentMenu() override;
    void      recentChanged() override { saveState(); }
    // Where the analyzer says a name is declared (ALScriptPlaces), as the
    // hover card and the inspector say it.
    typedef ALScriptPlaces::Declared Declared;
    // A line of an include as it reads -- in its tab where it is open,
    // else as the preprocessor last read it -- untrimmed; false where
    // neither has it.
    bool                          sourceLine(const std::string& path, S32 line, std::string& out) const override;

    void loaded(const ALScriptWorkspace::Loaded& answer);
    // The caret to the line, or the stretch, asked for before the text had
    // loaded, once it has.
    void goToPending(Doc& doc);
    void takeCarriedText(Doc& doc) override;
    void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                 const std::string& right_title) override;
    void endCompare(Doc& doc) override;
    // The tab's text against the text it was last saved as; asked again,
    // the source back.
    void compareWithSaved();
    // A notecard's items (ALScriptNotecardTab), made for a tab loaded or
    // kept as a notecard, afresh where `fresh`.
    ALScriptNotecardTab& notecardItems(Doc& doc, bool fresh = false);
    // What saving (ALScriptStudioSaving) asks of the window: how saves go,
    // as the settings say; a tab tidied, its text sent, what is kept of it
    // against a crash written again, and the Problems tab shown.
    ALScriptStudioSaving::Options saveOptions() const override;
    void tidy(Doc& doc, bool fix, bool format, bool trim) override;
    bool send(const Doc& doc, const std::string& text, const ALScriptWorkspace::SaveOptions& options, std::string& error) override;
    bool sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& items,
                      std::string& error) override;
    void keepForRecovery(Doc& doc) override;
    void showProblems() override;
    void selectFirstError(bool checkers_only) override;
    // A row, a reference, a place found, an outline entry chosen: the
    // place shown in its script, and the keyboard left in the list to walk
    // on through it; or, asked for with return or a double-click, taken
    // to the script to type there. Tabs opened on the way leave the panes
    // on what they were listing.
    void problemChosen(const ALScriptProblemsPane::Place& place, bool to_editor) override;
    // Navigation (ALScriptNavigation): the services' and saving's calls to
    // it, and what it asks of the window.
    void revealed(LLUICtrl* list, bool to_editor) override { mNavigation.revealed(list, to_editor); }
    void holdPreview(Doc& doc) override { mNavigation.holdPreview(doc); }
    void showPlace(Doc& doc, Doc::View view, const ALTextPos& at) override;
    bool pathOpen(const std::string& path) const override;
    void choosePreview(ALPaneList* list) override;
    bool workedFrom(const Doc& doc) const override;
    void focusDoc(Doc& doc) override { focusShown(doc); }
    // What an include is, as a mark: a file on disk, a notecard, a script.
    const char* includeImage(const std::string& path, bool lua) const;
    // Where a hover or the inspector says a name was declared, gone to: in
    // the script, or in the include.
    void goToDeclared(const LLSD& value) override;
    // What is wrong at a place of a script, in the card the mouse would
    // bring up there.
    void showProblemCard(Doc& doc, const ALTextPos& at);
    // The bottom tabs' titles: how many problems and places each lists,
    // and whether a script has said something the Output tab has not
    // shown yet.
    void refreshBottomTabs();
    // What the studio did, said in the status line and kept in the
    // Output tab, where it can be read again: saving, compiling,
    // preprocessing, renaming. The script's name in it is a link to it;
    // and after it a link for each thing to be done about it: save anyway,
    // try again, save a copy, export.
    void report(const std::string& text, bool failure = false, const Doc* doc = nullptr, const std::vector<std::string>& actions = {}) override;

    // The analyzers (ALScriptStudioChecking): a check is due a moment
    // after the last keystroke, sent from draw, answered whenever the
    // worker gets to it, and kept only if the text has not moved on.
    void scheduleAnalysis(Doc& doc, bool now = false) override { mChecking.schedule(doc, now); }
    bool lslFragment(const Doc& doc) const override { return mChecking.lslFragment(doc); }
    // The marks, the squiggles and the pane, from the compiler's problems
    // and the analyzer's together.
    void refreshProblems(Doc& doc) override;
    // The rest of what the Problems tab asks of the window
    // (ALScriptProblemsPane::Window).
    void                 problemCountsChanged() override { refreshBottomTabs(); }
    void                 problemFiltersChanged() override { saveState(); }
    std::string          problemIcon(const Doc& doc, const std::string& include) const override;
    void                 fixAllOfKind(Doc& doc, const std::string& key) override;
    bool                 isLint(bool lua, const std::string& id) const override;
    ALScriptLints::Level lintLevel(bool lua, const std::string& id) const override;
    void                 setLintLevel(bool lua, const std::string& id, ALScriptLints::Level level) override;
    void                 showLintSettings() override;

    // The name at the caret (ALScriptStudioCaret): a jump noted, a walk
    // down a list over, a place gone to, the lookup started, the bar's
    // path and the inspector's problems.
    void noteJump() override { mNavigation.noteJump(); }
    void keyboardInText() override { mNavigation.walked(); }
    void goTo(Doc& doc, const ALTextRange& range) override;
    void startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
                     const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version) override
    {
        mLookup.start(doc, command, refs, has_definition, home_path, definition, std::move(places), version);
    }
    void showPath(Doc& doc) override { mCrumbsBar->showPath(doc); }
    bool showProblemsAt(Doc& doc, const ALTextPos& at) override;
    // A name looked for beyond the script (ALScriptLookup): the object's
    // other scripts in its language, read as the region has them and
    // expanded; the places found shown; the new name asked for in a
    // popover over the window, with a row saying what return will do as
    // it is typed.
    std::vector<ALScriptLookup::Candidate> candidates(const Doc& doc) override;
    void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID& asset, const std::string& source)> loaded) override;
    void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) override;
    void showFound(Doc& doc, const ALScriptLookup::Found& found) override;
    void askNewName(Doc& doc, std::function<std::string(const std::string& typed)> hint,
                    std::function<void(const std::string& name)> chosen) override;
    void applyPendingEdits(Doc& doc) override { mLookup.applyPendingEdits(doc); }
    // The script handed to an external editor (ALScriptExternalEditor):
    // saving's calls to it, a save from outside run here, and the bridge
    // and the editor's launch, which are the window's -- VS Code itself
    // under tight integration, else the command the ExternalEditor setting
    // gives.
    void        syncExternal(Doc& doc) override { mExternal.sync(doc); }
    void        logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result) override { mExternal.log(doc, result); }
    void        save(Doc& doc) override;
    std::string bridgeId(const Doc& doc) const override;
    bool        subscribe(Doc& doc) override;
    void        unsubscribe(const Doc& doc) override;
    void        startEditor(Doc& doc, const std::string& file, bool on_disk) override;
    // The places last found, whichever tab is in front (the References
    // tab): following them opens other scripts, and the list stays what
    // it was.
    void referenceChosen(const ALScriptReferencesPane::Found& found, const Doc::Place& place, bool to_editor) override;
    void referencesCounted() override { refreshBottomTabs(); }
    // The Weights tab: whether it is looked at; and a part chosen there,
    // gone to.
    bool weightsShown() const override;
    void onWeightChosen(bool to_editor);
    // A place in an include opened in a tab of its own where the include
    // is a script or a notecard in the world; one on disk is only named.
    void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length) override;
    // The file an include or a module the script names is, opened: the one
    // the last run of the preprocessor found, else one on disk where the
    // preprocessor would look. An include's, a module's, or either. False
    // where it is nowhere known.
    bool openIncluded(Doc& doc, const std::string& name, std::optional<bool> require) override;
    // Vim's vimrc: its text and where it is, opened here to be edited; and
    // every editor's options set again once it is read.
    std::string vimrc(std::string& whence) override;
    void        editVimrc() override;
    void        refreshEditors() override { applyEditorOptions(); }
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
    boost::signals2::scoped_connection mQuickModeConnection;

    // The outline (ALScriptOutlinePane): shown, which the bar at the
    // bottom is told of; a symbol chosen, gone to; its sort kept.
    void        outlineShown(Doc& doc) override { mCrumbsBar->showPath(doc); }
    void        outlineChosen(Doc& doc, const ALScriptOutlineEntry& entry, bool to_editor) override;
    void        outlineSortChanged() override { saveState(); }
    // The bar under the editor (ALScriptCrumbsBar): its path moved, which
    // the outline follows; a step chosen, gone to; a word past the path
    // pressed -- the caret's place opens Go to Line, the counts the
    // problems, the view the other view; and vim's word.
    void        pathChanged(Doc& doc) override { mOutlinePane->followCaret(doc); }
    void        crumbChosen(Doc& doc, std::optional<ALTextRange> at) override;
    void        trailerChosen(const std::string& value) override;
    std::string vimBanner() const override { return mVim.banner(); }
    // A script's tab, its window's title and the bar called what it is now
    // called: renamed here, in the inventory, or found so when opened again.
    void        renameDoc(Doc& doc, const std::string& name);
    // Whether a script may move between windows now: not while a save of it
    // is on its way, which is said.
    bool        movable(const Doc& doc);
    // A bottom tab shown; and the keyboard put in its list, where asked.
    void        showBottom(const char* tab, bool focus = false);
    std::string kindName(ALScriptSymbolKind kind) const;

    // What scripts say, from the workspace: listed in the Output tab, and
    // a run-time error in a script that is open marked on its line.
    void runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event);
    // What the Output tab asks of the window (ALScriptOutputPane::Window).
    bool outputInSight() const override;
    void outputUnreadChanged() override { refreshBottomTabs(); }
    bool ownsObject(const LLUUID& root) const override;
    void outputAction(Doc& doc, const std::string& action) override;
    void outputShowDoc(Doc& doc, bool problems) override;
    void outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column) override;
    void outputGoToInclude(const std::string& file, const std::string& file_name, S32 line, S32 column) override;

    // What the explorer asks of the window (ALScriptExplorerPane::Window).
    void showExplorer() override;
    void explorerPinsChanged() override { saveState(); }
    void itemRenamed(const ALScriptRef& ref, const std::string& name) override;
    void itemDeleted(const ALScriptRef& ref) override;
    bool unsavedAnywhere(const ALScriptRef& ref) const override;
    // Whether a script runs, what it compiles for: the region's word, on a
    // tab that has it open.
    void runningState(const ALScriptWorkspace::RunningState& state);

    // Find in files: words looked for across the scripts open, one
    // object's contents or every object the explorer lists, said as a
    // sentence over the Search tab; each place found a row, which opens
    // its script there. A script that is open is searched as it stands
    // in the editor, any other as the region has it, fetched if need be;
    // what arrives after another search has begun is dropped.
    void findInFiles();
    // What the Search tab asks of the window (ALScriptSearchPane::Window).
    std::vector<ALScriptSearchPane::Window::Object> objectsListed() const override;
    std::string                                     objectName(const LLUUID& root) const override;
    std::string                                     whereIs(const Doc& doc) const override;
    LLUUID                                          objectInHand() const override;
    LLUUID                                          rootOf(const ALScriptRef& ref) const override;
    Doc*                                            openElsewhere(const ALScriptRef& ref) override;
    void fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where) override;
    void confirmReplaceAll(const LLSD& args, std::function<void()> yes) override;
    void searchResultChosen(const ALScriptSearch::Found& one, const ALTextRange& place, bool to_editor) override;
    // Where to go in a script once it is open, or now.
    void goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length) override;

    // The reference, in the inspector rather than a web page: a word of
    // the vocabulary shown with its declaration, the keyword file's
    // words about it and a link to its wiki page; F1 for the word at the
    // caret, or any word picked by name.
    void        showReference(const Vocab& word, bool lua) override;
    void        reference(Doc& doc);
    void        browseReference();

    // The formatter over the text as it stands, or the lines selected:
    // every line put right as one step to undo, the caret keeping its
    // place.
    void format(Doc& doc, bool selection_only) override;
    // The blanks at every line's end taken away, as one step to undo.
    void trimTrailing(Doc& doc);

    // Copy, from whichever list or editor has the keyboard.
    static LLEditMenuHandler* focusedEditHandler();

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
    // A tab saved, and closed once its save comes back.
    void saveToClose(const std::string& id) override;
    // The window's close, with several scripts unsaved, asked about all of
    // them at once: saved, let go of, or the window kept.
    void closeWindowAnswered(S32 option);

    void onTabChosen(const std::string& value);
    // A script made in the inventory, named first and opened here once the
    // inventory has it, with the scripter's template in it.
    void newInventoryScript(bool lua);

    // Due writings of what is unsaved, a lost connection, and what holds
    // each tab, looked at a few times a second, whether the window is shown
    // or not.
    void pumpRecovery();
    // The orphans (ALScriptStudioOrphans): recovery's and saving's calls to
    // them, and what they ask of the window -- what is in reach of a tab,
    // which is the world's; its place; its script loaded; the notice; and
    // the notice's actions.
    Doc::Orphan failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const override;
    void        reattach(Doc& doc) override { mOrphans.reattach(doc); }
    ALScriptStudioOrphans::Reach reach(const Doc& doc) override;
    void                         refreshPlace(Doc& doc) override;
    void                         loadScript(const ALScriptRef& ref) override;
    ALScriptNoticeBar*           noticeBar() override { return mNoticeBar; }
    void                         saveCopyToFile() override { mFiles.saveCopy(); }
    void                         saveAgain(Doc& doc) override { mSaving.saveAsked(doc); }
    void                         takeUpRecovery(Doc& doc, const ALScriptRecoveryEntry& entry) override { mRecovery.takeUp(doc, entry); }
    void                         discardRecovery(const ALScriptRecoveryEntry& entry) override;
    void                         noticeAction(const std::string& action) override { mOrphans.noticeAction(action); }
    // The window, of all of them, that has a script or a file open.
    static ALFloaterScriptStudio* holderOf(const ALScriptRef& ref, const std::string& file);
    // The studio window the keyboard was last in, while it is open: where a
    // script opened from outside -- the inventory, an object -- goes, as a
    // script window used to open over the last one.
    static ALFloaterScriptStudio* lastWorkedIn();
    void openOrphan(const ALScriptRecoveryEntry& entry, Doc::Orphan orphan) override;
    // A tab made to hold a kept text with nothing loaded under it: unsaved,
    // with whatever its script or file was.
    void becomeOrphan(Doc& doc, const ALScriptRecoveryEntry& entry, Doc::Orphan orphan) override;
    // What recovery asks of the window (ALScriptStudioRecovery::Window).
    bool recoverElsewhere(const ALScriptRecoveryEntry& entry) override;
    Doc* openFileTab(const std::string& path, bool lua) override;
    bool scriptInHand(const ALScriptRef& ref) const override;
    void activate(Doc& doc) override;
    void tabsChanged() override;
    void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
              std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) override;
    // What a tab needs from the moment it is made: its text's changes
    // heard, its places slid.
    void wireDoc(Doc& doc);
    // The notice over the editor, for the tab in front.
    void refreshNotice() override { mOrphans.refreshNotice(); }
    // What a tab holds saved as a new item in the inventory -- a script or
    // a notecard, its items with it -- and the tab closed once it is.
    void saveCopyToInventory(Doc& doc) override;
    // Revert to Saved, asked about where something would be lost; and
    // whether there is anything to read again.
    void askRevert(Doc& doc);
    bool revertible(const Doc& doc) const override;
    // A place jumped from, to go back to and forward again (ALNavHistory).
    typedef ALNavHistory::Place NavPlace;
    // The editor commands' keys as the keymap has them, and the menus'
    // own as a person rebound them, on the menus and the tips that say
    // them.
    void applyMenuKeys();
    void refreshKeyTips();
    // Edit > Undo and Redo named for the step they take, where it has one.
    void refreshUndoLabels();
    // The window's own commands in the table, by the menu that gives them;
    // a unit split out of the window registers its own. An editor's own
    // command, on the view in front: `changes` for one that changes the
    // text, which a tab that may not be changed cannot do.
    void addCommands();
    // The keys the window answers to: the menus' commands, and the tab and
    // line keys no menu shows.
    void addKeys();
    // The menus' commands at the keys a person gave them.
    ALKeyChord keyOf(const KeyedCommand& command) const override;
    void addFileCommands();
    void addEditCommands();
    void addInsertCommands();
    void addGoCommands();
    void addViewCommands();
    void addBuildCommands();
    void addHelpCommands();
    void addEditorCommand(const std::string& name, ALEditorCommand command, bool changes);
    void onCompileTarget();
    void onRunning();
    // The strip's experience, for the script in front: shown where it has
    // one or the agent has any to give it.
    void askExperienceOf(Doc& doc);
    void refreshExperience();
    void onExperience();
    void onReset();
    void revert(Doc& doc) override;
    // What a file's name says it holds (ALScriptStudioFiles); the
    // document's language set by it, the editor taught or untaught.
    typedef ALScriptStudioFiles::Language FileLanguage;
    void                                  speakFileLanguage(Doc& doc, const FileLanguage& language);

    // The tabs open, as the state keeps them: an item by its object and
    // id, a file by its path, and which is in front.
    LLSD openTabs() const;
    void writeState(LLSD& state) const override;
    void readState(const LLSD& state) override;

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
    std::vector<TabFacts>              mTabFacts;
    size_t                             mTabFactsActive = NONE;
    // The docs by id, for the lookups every answer makes.
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> mByDocId;
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
    void                               restoreListed(const ALScriptRef& ref, const ALScriptWorkspace::Contents& contents);
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
    void                               restoreTabs(const LLSD& open);
    void                               restoreWindows(const LLSD& windows);
    void                               reopenKept();
    // The tips that say a menu item's keys, as the skin wrote them, to be
    // said again when a key is rebound.
    std::vector<std::pair<std::string, std::vector<std::string>>> mKeyTips;
    std::map<std::string, std::string> mKeyTipTexts;
    // What Edit > Undo and Redo were last named for.
    std::string                        mUndoSaid;
    std::string                        mRedoSaid;
    // The notice over the editor.
    ALScriptNoticeBar*                 mNoticeBar = nullptr;
    // Whether the connection was seen lost; and when what holds each tab
    // was last looked at.
    bool                               mOffline        = false;
    F64                                mOrphansChecked = 0.0;
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
    ALScriptStudioRecovery             mRecovery{ *this, *this };
    // Saving and compiling the tabs.
    ALScriptStudioSaving               mSaving{ *this, *this };
    // The window's side of vim, over its editors.
    ALScriptStudioVim                  mVim{ *this, mCommands, *this };
    // Its tabs held open in an editor outside.
    ALScriptExternalEditor             mExternal{ *this, *this };
    // Its files on disk, and the recent lists.
    ALScriptStudioFiles                mFiles{ *this, *this };
    // What its scripts weigh.
    ALScriptStudioWeighing             mWeighing{ *this, *this };
    // Its tabs whose script is gone or out of reach, and the notice.
    ALScriptStudioOrphans              mOrphans{ *this, *this };
    // The places gone from, and the previews a list opens as it is walked.
    ALScriptNavigation                 mNavigation{ *this, *this };
    // Its names looked up across the object's scripts, and renamed.
    ALScriptLookup                     mLookup{ *this, *this };
    // The name at its caret, and the caret watched.
    ALScriptStudioCaret                mCaret{ *this, *this };
    // Its checking: the analyzers asked and answered, and fixes.
    ALScriptStudioChecking             mChecking{ *this, *this };
    LLHandle<LLContextMenu>            mTabMenuHandle;
    bool                               mMain = true;
    bool                               mClosingWindow = false;
    LLComboBox*                        mCompileTarget = nullptr;
    LLCheckBoxCtrl*                    mRunning       = nullptr;
    LLComboBox*                        mExperience    = nullptr;
    LLButton*                          mExperienceProfile = nullptr;
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
    boost::signals2::scoped_connection mDefinitionsConnection;
    // The vimrc changed: read again into this window's vim.
    boost::signals2::scoped_connection mVimrcConnection;
    // The settings the window follows as they change: the lints and the
    // Luau mode, the preprocessor's, vim's clipboard.
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    boost::signals2::scoped_connection mRuntimeConnection;
    boost::signals2::scoped_connection mRunningConnection;
};

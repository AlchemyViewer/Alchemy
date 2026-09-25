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
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiorecovery.h"
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
                                    public ALScriptSearchPane::Window, public ALScriptExplorerPane::Window, public ALScriptStudioRecovery::Window
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
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
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
    void continueClosing();
    // The options every editor shares -- font, keys, wrap, gutter, map --
    // put on all of them again, when a setting behind one changes; and
    // on every studio open, which the preferences ask for.
    void        applyEditorOptions();
    static void refreshAll();

    // A word of the language, as the region defines it: what colours and
    // completes.
    struct Vocab
    {
        std::string  text;
        std::string  detail;
        std::string  tooltip;
        ALSyntaxKind kind = ALSyntaxKind::Text;
        bool         deprecated = false;
    };
    static const std::vector<Vocab>& vocabulary(bool lua);
    // Built again on the next ask: the definitions changed.
    static void forgetVocabulary();
    // The region's words put in an editor's tables, so that they colour:
    // what the studio's editors and the preferences' preview share.
    static void teachWords(ALCodeEditor& editor, bool lua);
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
    // The compiler's and the run's problems moved along with an edit; and
    // the outline, until the next check says it again.
    void                slideProblems(Doc& doc, const ALTextDocument::Edit& edit);
    void                slideOutline(Doc& doc, const ALTextDocument::Edit& edit);
    // The caret to the next problem of the script after it, or the one
    // before, round past the ends, with what it says in a card.
    void                goToProblem(Doc& doc, S32 direction);
    // What a comment says is wanted dropped from the script's problems,
    // and a comment that would say so offered for every lint left.
    void                noLint(Doc& doc);
    // A name the script does not know given what a module in reach gives:
    // a SLua global a require, where a module is so named or exports it;
    // an LSL name an `#include`, where an include declares it
    // (ALScriptModules). Put in at the top; preferred where it is the one
    // offered.
    void                offerImports(Doc& doc);
    // A fix made, as one step to undo, and the script checked again at
    // once; refused where the text has moved on since `version`, the one
    // it was made over, whose places it is in.
    bool                applyFix(Doc& doc, const ALScriptFix& fix, U32 version) override;
    using FixPick = Doc::FixPick;
    // The preferred fix of every problem picked, made as one step, once
    // asked; and made, the asking done. True where anything was made.
    void                askFixAll(Doc& doc, const FixPick& pick);
    bool                fixAll(Doc& doc, const FixPick& pick);
    // The problem a row of the pane is, in its script's list, where its
    // fixes are.
    const Doc::Shown*   shownOf(const LLSD& value) const;
    // The fixes of the problems on a line, as the editor lists them, each
    // with the value that finds it again; none where the text has moved on
    // since the check they were made in.
    void                fixesOn(const Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out) const;
    // The keymap's keys beside the menu's editor commands that have none.
    void                showEditorKeys();

    using ProblemTraits = Doc::ProblemTraits;

    // A snippet: a body with placeholders, offered by name from the
    // Insert menu and by prefix among the completions; the viewer's and
    // the scripter's own (ALScriptSnippets).
    typedef ALScriptSnippets::Snippet Snippet;
    static const std::vector<Snippet>& snippets(bool lua) { return ALScriptSnippets::all(lua); }
    // Whether a position of an LSL script is straight inside a state,
    // where an event's handler goes.
    static bool                 inStateBody(ALCodeEditor& editor, const ALTextPos& at);
    // The grammar a file that is no script is read with: XML, JSON, text.
    static std::string          textSyntaxOf(const std::string& path);
    // A word of the vocabulary as a completion: a function with its
    // call, an event as a handler to fill in, a constant as itself.
    ALCodeEditor::Completion completionFor(const Vocab& word, bool lua) const;
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
    void   fillTabs();
    void   refreshToolbar();
    // The strip under the editor's right-hand words: the caret's place,
    // what is selected, and how many problems the script has.
    void   refreshTrailer(Doc& doc);
    // What a save of a script would send, measured again where the text or
    // its expansion has changed since: the text as it stands, or wrapped
    // with the analyzers' expansion where the preprocessor runs -- before
    // the optimizer and the compression a save adds, near enough to say
    // how near the limit it is. The save measures what it sends exactly.
    void   measureAsset(Doc& doc);
    // The vim mode's : commands the mode does not answer itself, and
    // its = over lines; and its mode and its words shown as they change.
    bool vimCommand(ALTextView& view, const std::string& name, const std::string& args);
    // q: q/ and q?: the lines entered, in a quick-open over the editor,
    // the one picked going back onto the line.
    void vimHistoryWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool run)> chosen);
    void vimFormat(ALTextView& view, S32 first, S32 last);
    // The words Tab completes on the : line: the studio's command names,
    // its :set options, :history's kinds.
    void vimComplete(ALTextView& view, const std::string& command, std::vector<std::string>& out);
    void pumpVim();
    Doc* docOf(const ALTextView& view);
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
    void   problemCounts(const Doc& doc, S32& errors, S32& warnings) const;

    ALCodeEditor*             makeEditor(const std::string& id, bool read_only);
    // The options every editor shares, put on one.
    void                      applyEditorOptions(ALCodeEditor& editor) const;
    // The expanded text put in the document's other editor, which is shown
    // once there is one where the tab asked for it.
    void                      showExpanded(Doc& doc, const std::string& text);
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
    void                      askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at) { askAnalyzer(doc, kind, at, at); }
    // The refactors are asked about a stretch, from `at` to `to`.
    void                      askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to);
    // `expansion` is the expansion the question was asked over, or zero
    // for the text as it stands.
    void                      answered(const ALScriptAnalysis::Result& result, U32 expansion);
    // The refactors at the caret, kept on the Doc and handed to the editor
    // to join the fixes it lists.
    void                      actionsAnswered(Doc& doc, const ALScriptAnalysis::Result& result, U32 expansion);
    // What a script is weighed for: its compile target's, where there is a
    // weigher for it; nothing for a notecard or an include.
    std::optional<ALScriptWeight::Target> weightTarget(const Doc& doc) const;
    // The script weighed a moment after its check, of what a save would
    // compile as nearly as the check has it; and the answer kept.
    void                      weigh(Doc& doc);
    void                      weighed(Doc& doc, const ALScriptAnalysis::Result& result);
    // What a preprocessor's run made to be sent -- optimized, compressed,
    // every include in; or the text as written where it is off -- weighed
    // as it is, after every run: `sent` is what was weighed, however many
    // runs have come since.
    void                      weighSent(Doc& doc);
    void                      weighedSent(Doc& doc, const ALScriptAnalysis::Result& result, const Doc::Expanded& sent);
    // What a save sends weighed as it goes, not before it: over its
    // target's limit it is said, once the weight is known, and goes up
    // anyway -- the numbers assume how the region compiles, and are not
    // its word.
    void                      weighForSave(Doc& doc);
    void                      warnOverWeight(Doc& doc);
    // The targets a script is weighed for: its own; and for an LSL script
    // in front while the Weights tab is looked at, the other two beside it.
    std::vector<ALScriptWeight::Target> weighedTargets(const Doc& doc) const;
    // What the text weighs while it is the text saved, kept for the Weights
    // tab to count from.
    void                      keepSavedWeights(Doc& doc);
    // What the script's own target's code weighs, put beside its text as
    // the view asks: each part's bytes after the line it is declared on,
    // and each line's heat in the gutter. Only of a weight of the text as
    // it stands; what is there already slides with the edits until then.
    void                      showWeightsInEditor(Doc& doc);
    // What each fix and refactor listed would make the script weigh, said
    // after it in the list: its edits made to a copy of what the analyzers
    // read, and the copies weighed beside that for the script's own target
    // on the analyzer's thread, the words put in when they come. A copy is
    // of the expansion where the preprocessor runs, a fix's edits taken
    // into it (ALScriptFixes::intoExpansion); false for a fix whose edits
    // cannot be, or overlap.
    void                      weighFixes(Doc& doc, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes);
    bool                      editedCopy(const Doc& doc, const std::vector<std::pair<ALTextRange, std::string>>& edits, std::string& out) const;

    // The preprocessor: whether it applies to a script; its run over the
    // text as it stands, for the analyzers, with the way back; and its
    // run ahead of a save, fetching includes, then the upload in the
    // envelope. Positions the analyzers answer with are mapped back to
    // the source, and what falls in an include is listed by its file.
    bool                          preprocessed(const Doc& doc) const;
    // The expansion of the text as it stands, asked for where it is not
    // in hand: it is a thread's work now, so a question that needs it
    // waits on the Doc and is asked again when it comes.
    void                          expandFor(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to);
    void                          expandedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result);
    // Without the source where only where the script is matters.
    ALScriptPreprocessor::Request preprocessRequest(const Doc& doc, bool with_source = true) const;
    // A run of the preprocessor over the text as it stands, fetching its
    // includes; none where one is on its way already. A save waiting on it
    // goes on when it answers (ALScriptSaveFlow::preprocessed).
    void                          preprocess(Doc& doc);
    void                          preprocessedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result);
    // What a run made, uploaded: in the envelope with the source as
    // written, or as written alone where the run was switched off.
    void                          sendPreprocessed(Doc& doc, const Doc::Expanded& sent);
    // The text sent to be saved and compiled, with the map it was expanded
    // through where it was.
    void                          upload(Doc& doc, const std::string& text, const ALSourceMap* map = nullptr);
    static S32                    mapSpan(const ALSourceMap& map, ALScriptSpan& span);
    std::string                   includeName(const Doc& doc, const std::string& path) const;
    void                          chooseIncludeFolder();
    // A file on disk opened in a tab of its own, or brought forward, at
    // a place in it where one is given; read as the language its
    // extension says, or the one it was included from.
    void                          openFile(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    // Opened in this window whatever another has open: where a tab is
    // being moved here from it.
    void                          openFileHere(const std::string& path, bool lua, S32 line = -1, S32 column = -1, S32 length = 0);
    // A file's text written back where it came from; the scripts that
    // include it are expanded again.
    void                          saveFile(Doc& doc);
    // A file's text is what is on disk now, however it got there: the
    // editor is clean, and the scripts that include it are expanded again.
    void                          fileSettled(Doc& doc);
    // The file watched for changes made outside the studio: taken in
    // where the editor is clean, told of where it is not.
    void                          watchFile(Doc& doc);
    void                          fileChangedOutside(const std::string& id, const std::string& file);
    // A file chosen from disk, opened in a tab of its own.
    void                          openFileFromDisk();
    // The inspector's words about the symbol at the caret: the text, with
    // every URL in it a link, and the line it says the symbol is
    // declared on a link to the place, where it says one -- in the
    // script, or in the include it was declared in. The lines given are
    // code, styled as the active script's editor would colour them.
    struct Declared
    {
        // As a link's value: where to go.
        LLSD        value() const;
        S32         line   = -1;
        S32         column = -1;
        // The include it is in, by identity and by name; empty for the
        // script itself.
        std::string path;
        std::string name;
    };
    void                          showSymbol(const std::string& text, const Declared& declared, const std::vector<S32>& code_lines);
    void                          showSymbol(const std::string& text) { showSymbol(text, Declared(), {}); }
    Declared                      declaredOf(const Doc& doc, const ALScriptAnalysis::Result& result) const;
    // A line of an include as it reads -- in its tab where it is open,
    // else as the preprocessor last read it -- untrimmed; false where
    // neither has it.
    bool                          sourceLine(const std::string& path, S32 line, std::string& out) const;
    // A place's line as the pane lists it: trimmed, with where the name
    // stands in what is left.
    static void                   placeText(Doc::Place& place, const std::string& line);

    void loaded(const ALScriptWorkspace::Loaded& answer);
    // The caret to the line, or the stretch, asked for before the text had
    // loaded, once it has.
    void goToPending(Doc& doc);
    void takeCarriedText(Doc& doc) override;
    // A notecard's items (ALScriptNotecardTab), made for a tab loaded or
    // kept as a notecard, afresh where `fresh`.
    ALScriptNotecardTab& notecardItems(Doc& doc, bool fresh = false);
    void save(Doc& doc);
    // A save the author asked for: past the one check that stopped the last
    // save of the same text, and then a save.
    void saveAsked(Doc& doc);
    // The save asked for while the last was on its way, made now where
    // anything is still unsaved; true where one is under way again.
    bool sendQueuedSave(Doc& doc);
    void saveAll();
    void compiled(const ALScriptWorkspace::CompileResult& result);
    void compiledHere(const ALScriptWorkspace::CompileResult& result);
    // A row, a reference, a place found, an outline entry chosen: the
    // place shown in its script, and the keyboard left in the list to walk
    // on through it; or, asked for with return or a double-click, taken
    // to the script to type there. Tabs opened on the way leave the panes
    // on what they were listing.
    void problemChosen(const ALScriptProblemsPane::Place& place, bool to_editor) override;
    void revealed(LLUICtrl* list, bool to_editor) override;
    // A place, by the identity of the script or file it is in, in one that
    // is not open, chosen by walking the list: opened a moment later as a
    // preview, if the list is still on it, rather than a tab for every row
    // passed -- each fetched from the region. True where it is put off.
    bool deferOpen(ALPaneList* list, const std::string& path);
    void pumpSettle();
    // The preview in hand let go of, for the next; and one held.
    void closePreview();
    void holdPreview(Doc& doc);
    // The places found moved with an edit to the script they are in, as the
    // problems are; gone where the edit touched the name.
    void slidePlaces(Doc& doc, const ALTextDocument::Edit& edit);
    // What an include is, as a mark: a file on disk, a notecard, a script.
    const char* includeImage(const std::string& path, bool lua) const;
    // Where a hover or the inspector says a name was declared, gone to: in
    // the script, or in the include.
    void goToDeclared(const LLSD& value);
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

    // The analyzers: a check is due a moment after the last keystroke,
    // sent from draw, answered whenever the worker gets to it, and kept
    // only if the text has not moved on.
    void scheduleAnalysis(Doc& doc, bool now = false);
    // An LSL file on disk with no default state: an include's functions
    // and globals, which the parser takes for no script at all until a
    // state is put after them, and whose declarations are for others.
    bool lslFragment(const Doc& doc) const;
    void pumpAnalysis();
    // The preprocessor's settings changed, here or in the preferences:
    // every script expanded and checked again, a moment after the last.
    void pumpPreprocessor();
    void requestAnalysis(Doc& doc);
    void analysed(const ALScriptAnalysis::Result& result);
    // A parse error on a preprocessor's word, with its transform off, told
    // so: over the problems in the source's places.
    void explainTransformWords(Doc& doc);
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

    // The name at the caret: asked about on a key or a menu item, and
    // answered by going there, lighting its places, or asking for a new
    // name and putting it everywhere as one step.
    void askSymbol(Doc& doc, ALEditorCommand command, const ALTextRange& word);
    // `at` is where the question was about in the source, which the
    // result's own place is not where the analyzer read the expansion.
    void symbolAnswered(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at);
    // A name looked for beyond the script: in every other script of the
    // object that includes the script declaring it, or is that script,
    // each expanded as the compiler would see it and asked where the
    // name stands, then answered together -- the places listed, or the
    // new name asked for and put in every script, the ones not open
    // opened with the change unsaved.
    void startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
                     const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version);
    void lookupCandidate(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id,
                         const std::string& text);
    void lookupExpanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const std::string& source,
                        const ALPreprocessor::Result& result);
    void lookupAnswered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                        const std::string& source, const std::string& expanded, const ALScriptAnalysis::Result& result);
    void lookupSettled(Doc& doc);
    static void addPlace(Doc::Lookup& lookup, Doc::Place place);
    // The new name asked for in a popover over the window, with a row
    // saying what return will do as it is typed, and put everywhere as
    // one step if the text has not moved on.
    void askNewName(Doc& doc);
    void renameTo(const std::string& id, U32 generation, const std::string& new_name);
    void applyPendingEdits(Doc& doc) override;
    // The script handed to an external editor: written to a file under
    // the temp folder and watched, so that the editor's saves are taken
    // as the text and saved from here; the bridge told, so that VS Code
    // can subscribe to it and hear the compiler; and the editor launched
    // -- VS Code itself under tight integration, else the command the
    // ExternalEditor setting gives. A save made here writes the file
    // again, and what the compiler says goes in a log beside it, as the
    // old editor did. Closing the tab ends it.
    void               editExternally(Doc& doc);
    // `settled` where the file was seen empty and is asked about again, to
    // take it as empty if it still is.
    void               externalChanged(const std::string& id, const std::string& file, bool settled = false);
    void               syncExternal(Doc& doc);
    // What the external editor saved, put in as one step to undo and
    // saved from here.
    void               takeExternal(Doc& doc, const std::string& text);
    void               logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result);
    void               stopExternal(Doc& doc);
    static std::string externalFileName(const Doc& doc);
    // The places last found, whichever tab is in front: following them
    // opens other scripts, and the list stays what it was.
    void fillReferences();
    void onReferenceChosen(bool to_editor);
    // The Weights tab: whether it is looked at; the script in front's
    // weights put in it; and a part chosen there, gone to.
    bool weightsShown() const;
    void refreshWeights();
    void onWeightChosen(bool to_editor);
    // A place in an include opened in a tab of its own where the include
    // is a script or a notecard in the world; one on disk is only named.
    void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length);
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

    // The outline and the breadcrumb, from what the check said the script
    // declares; the inspector, from what is at the caret, a moment after
    // it has settled.
    void        pumpCaret();
    void        inspected(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at);
    // What is squiggled under a position, from the checkers and the
    // compiler, each with what it says; empty where nothing is.
    std::string problemsAt(const Doc& doc, const ALTextPos& at) const;
    void        refreshOutline(Doc& doc);
    // The outline's row for the innermost symbol the caret is in, chosen
    // without going anywhere, and scrolled to where it is out of sight.
    void        followCaretInOutline(Doc& doc);
    // An outline row's arrow, under a point of the list; and a symbol
    // folded shut or opened, by its row.
    bool        outlineArrowAt(S32 x, S32 y, size_t& index);
    void        foldOutline(size_t index, std::optional<bool> folded = std::nullopt);
    void        refreshBreadcrumb(Doc& doc);
    // A script's tab, its window's title and the bar called what it is now
    // called: renamed here, in the inventory, or found so when opened again.
    void        renameDoc(Doc& doc, const std::string& name);
    // Whether a script may move between windows now: not while a save of it
    // is on its way, which is said.
    bool        movable(const Doc& doc);
    // The words past the breadcrumb: where the caret is, what is
    // selected, how many problems; the caret's place opens Go to Line and
    // the counts the problems.
    void        onTrailerChosen(const std::string& value);
    void        onCrumbChosen(size_t at, const std::string& value);
    // An outline entry as a picker's value, and back: by where it was and
    // what it is called, so that an outline made again while a list is up
    // -- a check answering -- still finds it, or nothing.
    static std::string outlineValue(const Doc& doc, size_t index);
    static size_t      outlineEntryOf(const Doc& doc, const std::string& value);
    void        onOutlineChosen(bool to_editor);
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
    void        showReference(const Vocab& word, bool lua);
    void        reference(Doc& doc);
    void        browseReference();
    static const Vocab* vocabWord(bool lua, std::string_view name);
    static std::string helpUrl(bool lua, const std::string& word);

    // The formatter over the text as it stands, or the lines selected:
    // every line put right as one step to undo, the caret keeping its
    // place.
    void format(Doc& doc, bool selection_only);
    // The blanks at every line's end taken away, as one step to undo.
    void trimTrailing(Doc& doc);

    // The errors the analyzers found in the text as they last checked it,
    // and the preprocessor in the expansion they read: what holds a save,
    // where saves are held on them.
    S32  checkerErrors(const Doc& doc) const;
    void reportOverWeight(const Doc& doc, const ALScriptWeight& weight);

    // Copy, from whichever list or editor has the keyboard; and a
    // right-click menu on a list for the same.
    static LLEditMenuHandler* focusedEditHandler();
    void                      listMenuFor(LLScrollListCtrl* list);
    void                      showListMenu(LLScrollListCtrl* list, S32 x, S32 y);

    void closeDocument(std::string_view id);
    void closeDocumentAnswered(const std::string& id, S32 option);
    // The quit's question about what is unsaved: saved, kept to be opened
    // again next time, let go of, or the quit called off.
    void quitAnswered(S32 option);
    // Several tabs closed at once -- the others, all of them, `:qa` -- the
    // unsaved among them asked about in one question, not one each.
    void closeMany(const std::vector<std::string>& ids);
    // A close of the window, or of several tabs, that is waited on no
    // longer; and a quit waiting on it called off.
    void stopClosing();
    // Whether the viewer is quitting on this window's answer.
    bool quittingOnUs() const;
    // A tab let go of: its unsaved text set aside among the discarded,
    // where it can be had back for a while, unless it is kept -- moved to
    // another window, kept for next time, or kept as the viewer goes.
    void letGoOf(size_t index, bool keep = false);
    // A save that did not go through -- refused over errors, failed, or
    // compiled with errors: a close waiting on it waits no longer, and a
    // window closing stops, the tab left for the author to look at.
    void saveStopped(Doc& doc);
    // A tab saved to be closed once its save comes back: where the save
    // cannot begin, the tab is left as it was, and a close waiting on it
    // stops -- rather than closing at whatever save comes next.
    void saveToClose(const std::string& id);
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
    void checkOrphans();
    Doc::Orphan orphanOf(const Doc& doc) const;
    // What a tab holding a kept text is where its script could not be
    // loaded, by why: one that may not be changed, one that could not be
    // loaded, or one whose item or object is gone or out of sight.
    Doc::Orphan failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const override;
    // A detached tab's item loaded under it now that it is in reach, what
    // it holds carried over with its history; or loaded again after a load
    // that failed, where a person asks or the next try is due.
    void reattach(Doc& doc);
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
    // The notice over the editor, for the tab in front, and what its
    // buttons do.
    void refreshNotice() override;
    void onNoticeAction(const std::string& action);
    // What a tab holds saved as a new item in the inventory -- a script or
    // a notecard, its items with it -- and the tab closed once it is.
    void saveCopyToInventory(Doc& doc);
    // Revert to Saved, asked about where something would be lost; and
    // whether there is anything to read again.
    void askRevert(Doc& doc);
    bool revertible(const Doc& doc) const;
    // The places jumped from, to go back to and forward again: a place in
    // a tab by its id. Walking a pane's list is one jump, from where the
    // caret was before the walk began.
    struct NavPlace
    {
        std::string doc;
        ALTextPos   at;
        // Which of the tab's views the place is in: a line gone to in
        // the expansion is gone back to there.
        Doc::View   view = Doc::View::Source;
    };
    void noteJump(bool walking = false);
    // A place to go back to, the way forward from it gone: once for a line,
    // and the fifty latest.
    void rememberPlace(const NavPlace& place);
    void goBack(bool forward);
    // The editor commands' keys as the keymap has them, and the menus'
    // own as a person rebound them, on the menus and the tips that say
    // them.
    void applyMenuKeys();
    void refreshKeyTips();
    // Edit > Undo and Redo named for the step they take, where it has one.
    void refreshUndoLabels();
    // A name that a rename may not take: one of the language's own.
    bool reservedName(const Doc& doc, const std::string& name) const;
    void onMenuAction(const LLSD& param);
    bool onMenuEnable(const LLSD& param);
    bool onMenuCheck(const LLSD& param);
    void onCompileTarget();
    void onRunning();
    // The strip's experience, for the script in front: shown where it has
    // one or the agent has any to give it.
    void askExperienceOf(Doc& doc);
    void refreshExperience();
    void onExperience();
    void onReset();
    void revert(Doc& doc);
    void loadFromFile();
    void saveToFile();
    // The pickers' answers, for the tab each was asked from, by its id.
    void fileChosenToLoad(const std::string& id, const std::vector<std::string>& files);
    void fileChosenToSave(const std::string& id, const std::vector<std::string>& files);
    // A disk tab saved under another name: the tab is that file from
    // then on, in the language its name says.
    void saveFileAs();
    void fileChosenToSaveAs(const std::string& id, const std::vector<std::string>& files);
    // What a file's name says it holds: an LSL or a Lua script, or, with
    // neither extension, what it was asked for as, else plain text.
    struct FileLanguage
    {
        bool script = false;
        bool lua    = false;
        // Whether the name said anything: it has an extension.
        bool said   = false;
    };
    static FileLanguage languageOfFile(const std::string& path, bool lua_hint);
    // The document's language set by it, the editor taught or untaught.
    void                speakFileLanguage(Doc& doc, const FileLanguage& language);
    // The files opened from disk lately, newest first, under File ▸
    // Open Recent; kept with the state. The scripts and notecards opened
    // from the world and the inventory lately, beside them.
    void noteRecentFile(const std::string& path);
    void noteRecentScript(const Doc& doc);
    void fillRecentMenu();
    // The marks: what a document is, on its tab; what a symbol's kind
    // is, in the outline -- by texture name, as a scroll list wants it.
    static const char* imageNameOf(const Doc& doc);
    static const char* imageNameOf(ALScriptSymbolKind kind);

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
    std::vector<std::string>           mRecentFiles;
    struct Recent
    {
        ALScriptRef ref;
        std::string name;
    };
    std::vector<Recent>                mRecentScripts;
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
    // The places jumped from and back from, and whether a pane's list is
    // being walked, which is one jump however many rows it passes.
    std::vector<NavPlace>              mBack;
    std::vector<NavPlace>              mForward;
    bool                               mWalking = false;
    // The tips that say a menu item's keys, as the skin wrote them, to be
    // said again when a key is rebound.
    std::vector<std::pair<std::string, std::vector<std::string>>> mKeyTips;
    std::map<std::string, std::string> mKeyTipTexts;
    // What Edit > Undo and Redo were last named for.
    std::string                        mUndoSaid;
    std::string                        mRedoSaid;
    // The notice over the editor.
    LLLayoutPanel*                     mNoticePanel  = nullptr;
    LLTextBox*                         mNoticeText   = nullptr;
    LLButton*                          mNoticeFirst  = nullptr;
    LLButton*                          mNoticeSecond = nullptr;
    std::string                        mNoticeActions[2];
    // Whether the connection was seen lost; and when what holds each tab
    // was last looked at.
    bool                               mOffline        = false;
    F64                                mOrphansChecked = 0.0;
    // What the editors' vim keymaps share: the : and / lines entered in
    // any of them, and the settings a :set changes.
    std::shared_ptr<ALVimKeymap::Shared> mVimShared = std::make_shared<ALVimKeymap::Shared>();
    bool                               mWordWrap    = false;
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
    std::string                        mVimBanner;
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
    ALJumpBar*                         mBreadcrumb    = nullptr;
    LLTabContainer*                    mBottomTabs    = nullptr;
    ALScriptProblemsPane*              mProblemsPane = nullptr;
    ALPaneList*                        mReferences    = nullptr;
    ALPaneList*                        mOutline       = nullptr;
    ALTextView*                        mSymbol        = nullptr;
    // Held while a pane's row is followed into a tab, so that the panes
    // go on listing what they were rather than the tab's.
    S32                                mHoldPanes       = 0;
    // The places the last Find References found, and what was looked up
    // from where: the script's own places are its, whichever is in front.
    struct References
    {
        std::string             from;
        std::string             fromName;
        std::string             name;
        std::vector<Doc::Place> places;
        bool                    hasDefinition = false;
        std::string             home;
        ALScriptSpan            definition;
    };
    References                         mFound;
    // Whether an edit has moved the places since the list was filled.
    bool                               mPlacesStale    = false;
    LLTextBox*                         mReferencesHead = nullptr;
    LLFilterEditor*                    mOutlineFilter  = nullptr;
    // What the outline's rows last said, and whose they were: a check
    // that changes none of it leaves the list as it is.
    std::vector<std::string>           mOutlineSaid;
    LLComboBox*                        mOutlineSort    = nullptr;
    // Each outline entry's key -- the names down to it -- and whether it
    // holds others, as the rows were last made.
    std::vector<std::string>           mOutlineKeys;
    std::vector<bool>                  mOutlineParents;
    // A preview being opened, and a place chosen in a list that is to be
    // opened as one once the list has stayed on it.
    S32                                mOpenPreview = 0;
    bool                               mSettled     = false;
    ALPaneList*                        mSettleList  = nullptr;
    LLSD                               mSettleValue;
    F64                                mSettleDue   = 0.0;
    std::string                        mTrailerLineTip;
    std::string                        mTrailerProblemsTip;
    std::string                        mTrailerSourceTip;
    std::string                        mTrailerExpandedTip;
    ALScriptOutputPane*                mOutputPane = nullptr;
    ALScriptSearchPane*                mSearchPane    = nullptr;
    // The Weights tab, and whether it was looked at last frame and has
    // been told of everything since: it is filled while it is looked at.
    // The fix list last shown, to be weighed a frame later -- once the
    // refactors a quick fix asked for have joined it, where they are coming,
    // so that the weighing does not keep them waiting on the analyzer's
    // thread.
    struct FixesToWeigh
    {
        std::string                    id;
        U32                            shown = 0;
        std::vector<ALCodeEditor::Fix> fixes;
    };
    std::optional<FixesToWeigh>        mFixesToWeigh;
    ALScriptWeightsPane*               mWeightsPane = nullptr;
    ALPaneList*                        mWeightsParts      = nullptr;
    bool                               mWeightsWereShown  = false;
    bool                               mWeightsStale      = true;
    // Which lookup across scripts the answers arriving belong to.
    U32                                mLookupGeneration = 0;
    ALScriptExplorerPane*              mExplorerPane  = nullptr;
    // What is unsaved in the tabs, kept against the viewer going.
    ALScriptStudioRecovery             mRecovery{ *this, *this };
    LLHandle<LLContextMenu>            mTabMenuHandle;
    bool                               mMain = true;
    // Whose path the bar at the bottom shows, so that a tab come to the
    // front is shown there whatever its own path was when last shown.
    std::string                        mCrumbsShownFor;
    bool                               mClosingWindow = false;
    LLHandle<LLContextMenu>            mListMenuHandle;
    LLScrollListCtrl*                  mListMenuFor   = nullptr;
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
    // The settings the window follows as they change: the lints and the
    // Luau mode, the preprocessor's, vim's clipboard.
    std::vector<boost::signals2::scoped_connection> mSettingConnections;
    // When every script is expanded and checked again after the
    // preprocessor's settings changed, or zero; and whether the
    // transforms' words are coloured again with it.
    F64                                mPreprocessorDue   = 0.0;
    bool                               mPreprocessorWords = false;
    boost::signals2::scoped_connection mRuntimeConnection;
    boost::signals2::scoped_connection mRunningConnection;
};

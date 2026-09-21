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
#include "alscriptanalysis.h"
#include "alscriptenvelope.h"
#include "alscriptpreprocessor.h"
#include "alscriptworkspace.h"
#include "alsourcemap.h"
#include "alstudiofloater.h"
#include "lllivefile.h"

#include <boost/signals2.hpp>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class ALJumpBar;
class ALOutputList;
class ALTabStrip;
class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
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
class ALFloaterScriptStudio final : public ALStudioFloater
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
    // The studio, with this object pinned in its explorer and chosen
    // there: what the build tool's Explore in IDE button means when no
    // external editor is listening.
    static ALFloaterScriptStudio* explore(const LLUUID& root);
    // A script saved from outside the studio -- by an external editor
    // over the bridge -- with this text: the tab that holds it, if one
    // does, shows the text as saved. A tab with unsaved changes takes
    // the text as one more step to undo, so that nothing typed is lost.
    static void savedElsewhere(const ALScriptRef& ref, const std::string& text);
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
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool undo() override;
    bool redo() override;

    // A script opened here; with text carried from another window, put
    // in place of what the server has once that has loaded, as one step
    // to undo, and the caret at a line.
    void openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried = std::nullopt, S32 line = -1);
    // The active script moved to a window of its own, its unsaved text
    // going with it.
    void popOut();
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
    // The font the settings name, or the monospace default.
    static const LLFontGL* editorFont();

private:
    ALFloaterScriptStudio(const LLSD& key);
    ~ALFloaterScriptStudio() override;

    // One tab of the studio: a script, or the author's source of one that
    // the preprocessor wrapped, read-only beside it.
    struct Doc
    {
        ALScriptRef                                ref;
        // The script's id, or the script's id and ":source".
        std::string                                id;
        std::string                                name;
        ALCodeEditor*                              editor = nullptr;
        ALScriptWorkspace::Language                language;
        LLUUID                                     assetId;
        bool                                       loaded     = false;
        bool                                       modifiable = false;
        bool                                       saving     = false;
        bool                                       closeAfterSave = false;
        // What the preprocessor made of the source, in a read-only editor
        // of its own that the pane can swap to and back; made once there
        // is expanded text to show.
        ALCodeEditor*                              expandedEditor  = nullptr;
        bool                                       showingExpanded = false;
        // A notecard rather than a script: plain text, saved as a
        // notecard with the items it came with, never analysed.
        bool                                       notecard = false;
        std::vector<LLPointer<LLInventoryItem>>    embedded;
        // The envelope the asset came in, whose source the editor holds
        // and whose expanded code the other editor shows; a save runs
        // the preprocessor over the source and wraps both again.
        std::optional<ALScriptEnvelope>            envelope;
        // The preprocessor's run over the text at a version: what the
        // analyzers see, and what the last save uploaded, each with the
        // way back to the source and what the run said.
        struct Expanded
        {
            bool             valid    = false;
            bool             disabled = false;
            U32              version  = 0;
            std::string      text;
            ALSourceMap      map;
            ALScriptProblems problems;
        };
        Expanded                                   expanded;
        Expanded                                   uploaded;
        // A save waiting on the preprocessor.
        bool                                       preprocessing = false;
        // What the compiler said of the last save.
        std::vector<ALScriptWorkspace::Diagnostic> problems;
        // What the script said as it ran, since it was last saved or
        // edited: a run-time error's place, or -1 for none, and its words.
        struct RuntimeProblem
        {
            S32         line   = -1;
            S32         column = -1;
            std::string message;
        };
        std::vector<RuntimeProblem>                runtime;
        // Text brought from another window, put in place of the server's
        // once that has loaded.
        std::optional<std::string>                 carriedText;
        // A line to go to once the script has loaded, or -1; and a
        // stretch of it to select, where a column is given.
        S32                                        pendingLine   = -1;
        S32                                        pendingColumn = -1;
        S32                                        pendingLength = 0;
        // Whether the script runs in its object, as the region last
        // said: -1 until it has.
        S32                                        running = -1;
        // A save waiting on a check of the text as it stands; and until
        // when a save goes ahead over what the check found.
        bool                                       saveAfterCheck  = false;
        F64                                        saveAnywayUntil = 0.0;
        // What the analyzer said of the text at analysisVersion; when the
        // next check is due, or zero; the version last asked about.
        ALScriptProblems                           analysis;
        U32                                        analysisVersion  = 0;
        U32                                        requestedVersion = 0;
        F64                                        analysisDue      = 0.0;
        std::string                                definitionsError;
        // Both, in the order the pane lists them.
        struct Shown
        {
            S32         line      = 0;
            S32         column    = 0;
            bool        hasColumn = false;
            std::string level;
            std::string origin;
            std::string message;
            // An included file the problem is in, by identity and by
            // name; empty for the script itself.
            std::string file;
            std::string fileName;
        };
        std::vector<Shown>                         shown;
        // What the analyzer said the script declares, at analysisVersion.
        std::vector<ALScriptOutlineEntry>          outline;
        // The name last asked about -- its definition, its references, a
        // new name -- where, and of which text.
        ALEditorCommand                            symbolCommand = ALEditorCommand::None;
        U32                                        symbolVersion = 0;
        ALTextPos                                  symbolAt;
        // A place a name stands: in this script, or in another -- an
        // include of it, or a script that includes it -- named by the
        // identity the source map gives the other and what to call it,
        // with the line as it reads there.
        struct Place
        {
            ALScriptSpan span;
            std::string  file;
            std::string  fileName;
            std::string  text;
        };
        // The places last found, listed in the pane.
        std::vector<Place>                         places;
        // The name being looked up across the object's scripts: what
        // was asked, the script that declares it (this one, or the
        // include, by identity) and where, how many scripts are still
        // to answer, the places gathered so far, each once, and the
        // version of each open script's text as it was read, so that a
        // rename knows it still holds.
        struct Lookup
        {
            U32                            generation = 0;
            ALEditorCommand                command    = ALEditorCommand::None;
            std::string                    name;
            bool                           hasDefinition = false;
            std::string                    homePath;
            ALScriptSpan                   definition;
            bool                           renamable = false;
            U32                            version   = 0;
            S32                            pending   = 0;
            std::vector<Place>             places;
            std::set<std::string>          seen;
            std::map<std::string, U32>     versions;
        };
        Lookup                                     lookup;
        // Edits to make once the script has loaded: a rename that reached
        // it from another script, each place with the name that must
        // still stand there and the one to put in its stead.
        struct PendingEdit
        {
            ALScriptSpan span;
            std::string  was;
            std::string  now;
        };
        std::vector<PendingEdit>                   pendingEdits;
        // The script held open in an external editor: the file under
        // the temp folder the editor was given, watched for the editor's
        // saves, and the log beside it the compiler's words go to;
        // whether the bridge was told, so that VS Code can subscribe;
        // and whether the save under way came from the editor, which
        // does not write the file back.
        std::unique_ptr<LLLiveFile>                liveFile;
        std::string                                liveLog;
        bool                                       subscribed   = false;
        bool                                       externalSave = false;
        // Where the caret was last seen; when the inspector is due to be
        // told what it is on, or zero; and what it was last told about.
        ALTextPos                                  caretSeen{ -1, -1 };
        F64                                        inspectDue     = 0.0;
        ALTextPos                                  inspectAt{ -1, -1 };
        U32                                        inspectVersion = 0;
        boost::signals2::scoped_connection         changed;
    };
    static constexpr size_t NONE = static_cast<size_t>(-1);

    // A snippet: a body with placeholders, offered by name from the
    // Insert menu and by prefix among the completions. From the files
    // under app_settings/snippets/, and a person's own under the
    // settings folder.
    struct Snippet
    {
        std::string name;
        std::string prefix;
        std::string detail;
        std::string body;
    };
    const std::vector<Snippet>& snippets(bool lua);
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
                           std::function<void()> escaped = {});

    Doc*   active();
    size_t indexOf(const ALScriptRef& ref) const;
    size_t indexOf(std::string_view id) const;
    void   activate(size_t index);
    void   fillTabs();
    void   refreshToolbar();
    // The strip under the editor's right-hand words: the caret's place,
    // what is selected, and how many problems the script has.
    void   refreshTrailer(Doc& doc);
    // The vim mode's : commands the mode does not answer itself, and
    // its = over lines; and its mode and its words shown as they change.
    bool vimCommand(ALTextView& view, const std::string& name, const std::string& args);
    void vimFormat(ALTextView& view, S32 first, S32 last);
    void pumpVim();
    Doc* docOf(const ALTextView& view);
    // The tab pressed with the right button: a menu about it.
    void   showTabMenu(const std::string& value, S32 x, S32 y);
    void   onTabAction(const std::string& action);
    // The tab after or before the active one, round the ends.
    void   cycleTab(S32 direction);
    // The documents in the order the tabs were dragged into.
    void   onTabsReordered(const std::vector<std::string>& order);
    // How many errors and warnings a script shows.
    void   problemCounts(const Doc& doc, S32& errors, S32& warnings) const;

    ALCodeEditor*             makeEditor(const std::string& id, bool read_only);
    // The options every editor shares, put on one.
    void                      applyEditorOptions(ALCodeEditor& editor) const;
    // The expanded text put in the document's other editor, and the pane
    // swapped between the two.
    void                      showExpanded(Doc& doc, const std::string& text);
    void                      toggleExpanded();
    // Which editor the pane shows for the active document, and every
    // other editor hidden.
    void                      showEditors();
    // The region's words for colouring and completing, and the analyzer
    // behind completion, hover and signature help.
    void                      teachEditor(Doc& doc);
    void                      askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at);
    void                      answered(const ALScriptAnalysis::Result& result);

    // The preprocessor: whether it applies to a script; its run over the
    // text as it stands, for the analyzers, with the way back; and its
    // run ahead of a save, fetching includes, then the upload in the
    // envelope. Positions the analyzers answer with are mapped back to
    // the source, and what falls in an include is listed by its file.
    bool                          preprocessed(const Doc& doc) const;
    const Doc::Expanded&          expandedFor(Doc& doc);
    ALScriptPreprocessor::Request preprocessRequest(const Doc& doc) const;
    void                          preprocess(Doc& doc, bool then_save);
    void                          preprocessedAnswer(const std::string& id, U32 version, bool then_save, const ALPreprocessor::Result& result);
    void                          upload(Doc& doc, const std::string& text);
    static S32                    mapSpan(const ALSourceMap& map, ALScriptSpan& span);
    std::string                   includeName(const Doc& doc, const std::string& path) const;
    void                          chooseIncludeFolder();

    void loaded(const ALScriptWorkspace::Loaded& answer);
    void takeCarriedText(Doc& doc);
    void save(Doc& doc);
    void saveAll();
    void compiled(const ALScriptWorkspace::CompileResult& result);
    void fillProblems(const Doc* doc);
    void onProblemSelected();

    // The analyzers: a check is due a moment after the last keystroke,
    // sent from draw, answered whenever the worker gets to it, and kept
    // only if the text has not moved on.
    void scheduleAnalysis(Doc& doc, bool now = false);
    void pumpAnalysis();
    void requestAnalysis(Doc& doc);
    void analysed(const ALScriptAnalysis::Result& result);
    // The marks, the squiggles and the pane, from the compiler's problems
    // and the analyzer's together.
    void refreshProblems(Doc& doc);

    // The name at the caret: asked about on a key or a menu item, and
    // answered by going there, lighting its places, or asking for a new
    // name and putting it everywhere as one step.
    void askSymbol(Doc& doc, ALEditorCommand command, const ALTextRange& word);
    void symbolAnswered(Doc& doc, const ALScriptAnalysis::Result& result);
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
    void lookupExpanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALPreprocessor::Result& result);
    void lookupAnswered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                        const std::string& expanded, const ALScriptAnalysis::Result& result);
    void lookupSettled(Doc& doc);
    static void addPlace(Doc::Lookup& lookup, Doc::Place place);
    // The new name asked for in a popover over the window, with a row
    // saying what return will do as it is typed, and put everywhere as
    // one step if the text has not moved on.
    void askNewName(Doc& doc);
    void renameTo(const std::string& id, U32 generation, const std::string& new_name);
    void applyPendingEdits(Doc& doc);
    // The script handed to an external editor: written to a file under
    // the temp folder and watched, so that the editor's saves are taken
    // as the text and saved from here; the bridge told, so that VS Code
    // can subscribe to it and hear the compiler; and the editor launched
    // -- VS Code itself under tight integration, else the command the
    // ExternalEditor setting gives. A save made here writes the file
    // again, and what the compiler says goes in a log beside it, as the
    // old editor did. Closing the tab ends it.
    void               editExternally(Doc& doc);
    void               externalChanged(const std::string& id, const std::string& file);
    void               syncExternal(Doc& doc);
    void               logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result);
    void               stopExternal(Doc& doc);
    static std::string externalFileName(const Doc& doc);
    void fillReferences(const Doc* doc);
    void onReferenceChosen();
    // A place in an include opened in a tab of its own where the include
    // is a script or a notecard in the world; one on disk is only named.
    void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length);
    // A line, or line:column, typed into the same popover, the editor
    // showing the line as it is typed and going back on escape.
    void goToLine();
    void goToSymbol();

    // The outline and the breadcrumb, from what the check said the script
    // declares; the inspector, from what is at the caret, a moment after
    // it has settled.
    void        pumpCaret();
    void        inspected(Doc& doc, const ALScriptAnalysis::Result& result);
    // What is squiggled under a position, from the checkers and the
    // compiler, each with what it says; empty where nothing is.
    std::string problemsAt(const Doc& doc, const ALTextPos& at) const;
    void        refreshOutline(Doc& doc);
    void        refreshBreadcrumb(Doc& doc);
    void        onCrumbChosen(size_t at, const std::string& value);
    void        onOutlineChosen();
    void        showBottom(const char* tab);
    std::string kindName(ALScriptSymbolKind kind) const;

    // What scripts say, from the workspace: listed in the Output tab, and
    // a run-time error in a script that is open marked on its line.
    void runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event);
    void onOutputFilter();
    void onOutputChosen();

    // The explorer: the objects in hand -- pinned, selected in world, or
    // holding a script that is open -- each prim's scripts and notecards
    // listed as they are fetched, with whether each script runs. A pinned
    // object stays listed when it is neither, and across sessions; one
    // that is not around is listed by the name it had.
    struct ExplorerPrim
    {
        LLUUID                            id;
        std::string                       name;
        bool                              fetched = false;
        std::vector<ALScriptWorkspace::Item> items;
    };
    struct ExplorerObject
    {
        LLUUID                    root;
        std::string               name;
        bool                      pinned  = false;
        bool                      present = true;
        std::vector<ExplorerPrim> prims;
    };
    struct Pinned
    {
        LLUUID      root;
        std::string name;
    };
    // What a row of the explorer stands for: an object, a prim of one, or
    // a script or notecard in a prim.
    struct ExplorerRow
    {
        LLUUID      root;
        LLUUID      prim;
        LLUUID      item;
        std::string name;
        bool        script = false;
        bool        lua    = false;
        bool        isItem() const { return item.notNull(); }
        ALScriptRef ref() const { return ALScriptRef(prim, item); }
    };
    void pumpExplorer();
    void refreshExplorer();
    void explorerContents(const ALScriptWorkspace::Contents& contents);
    void fillExplorer();
    // The rows chosen, in the list's order.
    std::vector<ExplorerRow> explorerChoice() const;
    // The prims the rows chosen as prims or objects stand for, each once,
    // with the name the queues report under; an object row means every
    // prim of it. A script's own prim is not among them.
    std::vector<std::pair<LLUUID, std::string>> containerPrims(const std::vector<ExplorerRow>& rows) const;
    void                     onExplorerChosen();
    void                     onExplorerAction(const std::string& action);
    void                     showExplorerMenu(S32 x, S32 y);
    bool                     explorerActionEnabled(const std::string& action) const;
    // A script or notecard made in a prim, named through a dialog and
    // opened once the region lists it.
    void explorerCreate(const LLUUID& prim, bool notecard, bool lua);
    void explorerCreated(const ALScriptWorkspace::Created& made);
    void explorerRename(const ExplorerRow& row);
    void explorerDelete(const std::vector<ExplorerRow>& rows);
    void explorerRecompile(const std::vector<ExplorerRow>& rows);
    // An object pinned, the explorer shown, and the object's row chosen.
    void exploreObject(const LLUUID& root);
    bool isPinned(const LLUUID& root) const;
    // Pinned or let go; the state and the list are the caller's to bring
    // up to date.
    void togglePinned(const LLUUID& root, const std::string& name);
    void runningState(const ALScriptWorkspace::RunningState& state);

    // Find in files: words looked for across the scripts open, one
    // object's contents or every object the explorer lists, said as a
    // sentence over the Search tab; each place found a row, which opens
    // its script there. A script that is open is searched as it stands
    // in the editor, any other as the region has it, fetched if need be;
    // what arrives after another search has begun is dropped.
    void buildSearchBar();
    void findInFiles();
    void onSearchChanged();
    void search();
    void searchDocument(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text);
    void searchLoaded(U32 generation, const std::string& where, const ALScriptWorkspace::Loaded& loaded);
    void searchSettled();
    void onSearchResult();
    // Where to go in a script once it is open, or now.
    void goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length);

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

    // Whether a save may go ahead: the analyzers' check of the text as it
    // stands found no errors, or the person asked twice.
    bool preflight(Doc& doc);

    // Copy, from whichever list or editor has the keyboard; and a
    // right-click menu on a list for the same.
    static LLEditMenuHandler* focusedEditHandler();
    void                      listMenuFor(LLScrollListCtrl* list);
    void                      showListMenu(LLScrollListCtrl* list, S32 x, S32 y);

    void closeDocument(std::string_view id);
    void closeDocumentAnswered(const std::string& id, S32 option);
    void letGoOf(size_t index);

    void onTabChosen(const std::string& value);
    void onMenuAction(const LLSD& param);
    bool onMenuEnable(const LLSD& param);
    bool onMenuCheck(const LLSD& param);
    void onCompileTarget();
    void onRunning();
    void onReset();
    void revert(Doc& doc);
    void loadFromFile();
    void saveToFile();
    void fileChosenToLoad(const std::vector<std::string>& files);
    void fileChosenToSave(const std::vector<std::string>& files);

    void writeState(LLSD& state) const override;
    void readState(const LLSD& state) override;

    std::vector<std::unique_ptr<Doc>>  mDocs;
    size_t                             mActive = NONE;
    std::vector<Snippet>               mSnippets[2];
    bool                               mSnippetsLoaded[2] = { false, false };
    bool                               mWordWrap    = false;
    bool                               mLineNumbers = true;
    bool                               mIndentGuides    = true;
    bool                               mRelativeNumbers = false;
    bool                               mRainbowBrackets = true;
    // What the analyzers add to the picture: every name coloured by what
    // it is, and the words shown beside the text.
    bool                               mSemanticColors  = true;
    bool                               mInlayParameters = true;
    bool                               mInlayTypes      = true;
    // Vim over every editor: the mode in the bottom strip, the : line
    // and what it says in the status line, and w, q and the rest
    // answered here.
    bool                               mVimMode      = false;
    U32                                mVimSeen      = 0;
    std::string                        mVimBanner;
    bool                               mStickyHeaders   = true;
    // The scrollbar as a map: whether, how wide, whether it previews the
    // lines under the mouse, and on which side.
    bool                               mScrollMap        = false;
    S32                                mScrollMapWidth   = 90;
    bool                               mScrollMapPreview = true;
    bool                               mScrollMapLeft    = false;
    LLPanel*                           mEditorHost    = nullptr;
    ALTabStrip*                        mTabs          = nullptr;
    ALJumpBar*                         mBreadcrumb    = nullptr;
    LLTabContainer*                    mBottomTabs    = nullptr;
    LLScrollListCtrl*                  mProblems      = nullptr;
    LLScrollListCtrl*                  mReferences    = nullptr;
    LLScrollListCtrl*                  mOutline       = nullptr;
    LLTextEditor*                      mSymbol        = nullptr;
    ALOutputList*                      mOutput        = nullptr;
    LLComboBox*                        mOutputFilter  = nullptr;
    ALScopeBar*                        mSearchBar     = nullptr;
    LLScrollListCtrl*                  mSearchResults = nullptr;
    LLTextBox*                         mSearchCount   = nullptr;
    // Which search the answers arriving belong to; how many files are
    // still to answer; what was found so far, and in how many files; the
    // words last searched for, so that a changed dropdown asks again
    // about the same words while typing waits for return.
    U32                                mSearchGeneration = 0;
    U32                                mLookupGeneration = 0;
    S32                                mSearchPending    = 0;
    S32                                mSearchHits       = 0;
    S32                                mSearchFiles      = 0;
    std::string                        mSearchQuery;
    LLScrollListCtrl*                  mExplorer      = nullptr;
    std::vector<ExplorerObject>        mExplorerModel;
    std::vector<Pinned>                mPinned;
    // A prim whose new item is to be opened once its contents list it,
    // by name where the region gave no id.
    LLUUID                             mOpenWhenListedPrim;
    LLUUID                             mOpenWhenListedItem;
    std::string                        mOpenWhenListedName;
    LLHandle<LLContextMenu>            mExplorerMenuHandle;
    LLHandle<LLContextMenu>            mTabMenuHandle;
    // The roots selected in world when last looked, and when.
    std::vector<LLUUID>                mExplorerRoots;
    F64                                mExplorerPolled = 0.0;
    // What the region said runs, by prim and item.
    std::map<std::pair<LLUUID, LLUUID>, bool> mRunningKnown;
    bool                               mMain = true;
    bool                               mClosingWindow = false;
    LLHandle<LLContextMenu>            mListMenuHandle;
    LLScrollListCtrl*                  mListMenuFor   = nullptr;
    // The objects heard from, offered in the filter.
    std::map<LLUUID, std::string>      mOutputObjects;
    LLComboBox*                        mCompileTarget = nullptr;
    LLCheckBoxCtrl*                    mRunning       = nullptr;
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
    boost::signals2::scoped_connection mRuntimeConnection;
    boost::signals2::scoped_connection mRunningConnection;
};

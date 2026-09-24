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
#include "lllivefile.h"

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
                    bool focus = true);
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
    // The typing settings put on an editor: tabs, completion, pairs, the
    // caret, the hover card. The studio's editors and the preferences'
    // preview share them.
    static void applyTypingOptions(ALCodeEditor& editor);
    // The font the settings name, or the monospace default.
    static const LLFontGL* editorFont();

private:
    ALFloaterScriptStudio(const LLSD& key);
    ~ALFloaterScriptStudio() override;

    // One tab of the studio: a script, a notecard or a file, and the views
    // of it the pane can show -- its source, and what the preprocessor made
    // of that where it made anything.
    struct Doc
    {
        ALScriptRef                                ref;
        // A file on disk rather than an item in the world -- an include
        // the preprocessor read from a folder -- by its path; `ref` is
        // then nobody's. Its tab is its base name, a save writes it
        // back and expands the scripts that include it again, and
        // nothing in the world hears of it. A Lua file is analysed as
        // the module it is; an LSL one is a fragment, and is not.
        std::string                                file;
        // The script's id; a file's is `disk:` and its path.
        std::string                                id;
        std::string                                name;
        // The source: the text the author types, which is what is saved,
        // checked, kept against a crash and gone to by every place a list,
        // a card or a jump names, whichever view is in front.
        ALCodeEditor*                              editor = nullptr;
        ALScriptWorkspace::Language                language;
        LLUUID                                     assetId;
        bool                                       loaded     = false;
        // Why the last load came back with no text, and what it said: nothing
        // is coming to put a kept text over until it is loaded again.
        ALScriptWorkspace::Loaded::Failure         loadFailure = ALScriptWorkspace::Loaded::Failure::None;
        std::string                                loadError;
        bool                                       modifiable = false;
        bool                                       saving     = false;
        bool                                       closeAfterSave = false;
        // A save asked for while one was on its way, made when it answers.
        bool                                       saveAgain  = false;
        // Where the editor's journal stood when the text went up: what the
        // answer marks saved, whatever was typed while it came.
        ALTextUndo::SavePoint                      sentAt;
        // What the preprocessor made of the source, in a read-only editor
        // of its own; made once there is expanded text to show.
        ALCodeEditor*                              expandedEditor = nullptr;
        // The views of a tab the pane can show, and the one asked for.
        // What was asked for stands through a reload, into a window of its
        // own and across sessions; while it has nothing to show, the
        // source is shown in its place.
        enum class View : U8
        {
            Source,
            Expanded
        };
        View                                       view = View::Source;
        // The view in front, and its text: what the view's own commands --
        // find, go to a line, fold, copy, undo -- act on, and whose caret
        // the trailer reads.
        View          shownView() const { return view == View::Expanded && expandedEditor ? View::Expanded : View::Source; }
        ALCodeEditor* shownText() const { return shownView() == View::Expanded ? expandedEditor : editor; }
        // A notecard rather than a script: plain text, saved as a
        // notecard with the items it came with, never analysed.
        bool                                       notecard = false;
        // Opened by walking a pane's list past a place in it: looked at
        // without being held, and replaced by the next one looked at, until
        // it is typed in, saved, gone to or double-clicked.
        bool                                       preview = false;
        // A file changed on disk while this tab had unsaved changes, and
        // the author asked what to do: once, however often it changes.
        bool                                       askingReload = false;
        // What keeps the unsaved text against a crash (ALScriptRecoveryStore):
        // whose text it is; when it is next written there, or zero; and
        // whether writing it failed, which is said once.
        std::string                                recoveryKey;
        F64                                        recoveryDue    = 0.0;
        bool                                       recoveryFailed = false;
        // What an earlier session left of this, found as it opened, offered
        // in the notice until it is restored or discarded; and one being
        // taken up here, whose file goes once this tab's own is written.
        std::optional<ALScriptRecoveryEntry>       recoverable;
        std::optional<ALScriptRecoveryEntry>       recovering;
        // A recovered or copied notecard's items, in place of what it loads
        // with, since its text says them by their places in this list.
        std::optional<std::vector<LLPointer<LLInventoryItem>>> carriedEmbedded;
        // A copy saved into the inventory from another tab: wrapped as that
        // one was, saved as soon as it is in, and that tab closed once the
        // copy is saved -- by its id, and the version its text was at when
        // the copy was made, since what is typed there meanwhile is not in
        // the copy.
        bool                                       wrapOnLoad = false;
        bool                                       saveOnLoad = false;
        std::string                                copyOf;
        U32                                        copyOfVersion = 0;
        // What it is to compile for once it has loaded, which the load
        // would otherwise say: a copy compiles for what its original did.
        std::string                                targetOnLoad;
        // Where it stands with what holds it: its object out of sight --
        // deleted, returned, far away -- the item gone from the object or
        // the inventory, the connection lost, the item in the Trash, the
        // file gone from disk; or a kept text over a script that may no
        // longer be changed, or that could not be loaded. Said in the
        // notice over the editor, which a person may hide until it changes.
        enum class Orphan : U8
        {
            None,
            Away,
            Removed,
            Offline,
            Trashed,
            FileGone,
            Locked,
            Unloaded
        };
        Orphan                                     orphan          = Orphan::None;
        bool                                       noticeDismissed = false;
        // A kept text with nothing loaded under it -- its item out of reach
        // as it was opened -- whose script is known only as the entry said:
        // loaded under it once the item is in reach, before it is saved.
        // How many loads have failed on the way since the last that went
        // through, and when the next may be tried: further apart each time,
        // and a few times only unless a person asks.
        bool                                       detached        = false;
        S32                                        reattachTries   = 0;
        F64                                        nextReattach    = 0.0;
        // Since when its object has been out of sight, or zero: an object
        // at the edge of what is in view comes and goes, and is taken for
        // gone only once it has been gone a moment.
        F64                                        awaySince       = 0.0;
        // What its object and region were called, while they were in sight:
        // what a kept text says it came from once they are not.
        std::string                                objectName;
        std::string                                regionName;
        // A reload a revert asked for, with whether the tab could be
        // changed before it: one that fails puts the tab back as it was.
        std::optional<bool>                        reverting;
        // Where the caret and the view were before the text was loaded
        // again -- a revert, an external editor's save -- to be put back;
        // a line of -1 for none.
        ALTextPos                                  keepCaret{ -1, -1 };
        S32                                        keepScroll = 0;
        // The outline's symbols folded shut, each by the names from the
        // outermost down to it.
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> outlineFolded;
        std::vector<LLPointer<LLInventoryItem>>    embedded;
        // The items the saved asset carries, by id: what the server can
        // copy out of it. An item dropped since is only here once a save
        // has taken it.
        boost::unordered_flat_set<LLUUID>          inAsset;
        std::vector<LLUUID>                        saving_items;
        // An edit that put a placeholder back -- an undo of a deletion,
        // a redo of a drop -- has the items placed again.
        boost::signals2::scoped_connection         embeddedEdits;
        // Where the last drop's placeholder ended, and in which frame:
        // several items dropped at once come one call each, at one point,
        // and each goes after the one before.
        ALTextPos                                  dropEnd{ -1, -1 };
        U32                                        dropFrame = 0;
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
            // Which expansion of the text this is, counted from one: a
            // question asked over it is answered in its places, and an
            // answer asked over one since dropped or replaced -- the
            // includes came in, a setting changed -- is of places no
            // longer read that way.
            U32              generation = 0;
            std::string      text;
            ALSourceMap      map;
            ALScriptProblems problems;
        };
        Expanded                                   expanded;
        Expanded                                   uploaded;
        U32                                        expansions = 0;
        // The map the last upload went with, where it was expanded: what
        // the compiler's answer is read back through, whatever has been
        // expanded since.
        std::optional<ALSourceMap>                 sentMap;
        // The version an expansion has been asked for, or none -- not a
        // zero, which an empty text's version is: the preprocessor answers
        // on the main thread a moment later, and one text is expanded once
        // however many questions wait on it.
        std::optional<U32>                         expanding;
        // The questions held until it comes. A question of a kind
        // replaces the one of that kind still waiting: a second hover
        // is a hover of somewhere else, and only the last is wanted.
        struct Waiting
        {
            ALScriptAnalysis::Kind kind = ALScriptAnalysis::Kind::Check;
            ALTextPos              at;
            // Where a stretch chosen from `at` ends: the refactors'.
            ALTextPos              to;
        };
        std::vector<Waiting>                       waiting;
        // A run of the preprocessor on its way, for a save or not; and a
        // save asked for while one that was not for a save was, taken up
        // when it answers as though it had been.
        bool                                       preprocessing       = false;
        bool                                       saveAfterPreprocess = false;
        // Whether the script's `.luaurc` was asked for once, so that a
        // script with none is not asked for it at every check.
        bool                                       configAsked = false;
        // What the compiler said of the last save, in the source's places
        // -- back through the preprocessor's map, where it ran -- or an
        // include's, by its path.
        struct Compiled
        {
            S32         line      = 0;
            S32         column    = 0;
            bool        hasColumn = false;
            std::string file;
            std::string level;
            std::string message;
        };
        std::vector<Compiled>                      problems;
        // What the script said as it ran, since it was last saved: a
        // run-time error's place, or -1 for none, the include it is in,
        // and its words.
        struct RuntimeProblem
        {
            S32         line   = -1;
            S32         column = -1;
            std::string file;
            std::string message;
            // How many times it was said: a script failing in a timer
            // says the same thing every tick, and is one problem.
            S32         count  = 1;
        };
        std::vector<RuntimeProblem>                runtime;
        // Both were said of the text as it was: each moves with the edits
        // since, and goes when one touches its line, the text there being
        // no longer what was compiled or run.
        boost::signals2::scoped_connection         placedEdits;
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
        // A compile target picked here since the last save, which the
        // region's word on what the script compiles for does not put back.
        bool                                       targetChosen = false;
        // A save waiting on a check of the text as it stands; and the
        // version of the text a save goes ahead for over what the check or
        // the preprocessor found -- the one a save was refused over, so
        // that asking again saves it, however long after, and a change
        // asks the question afresh -- or -1 for none.
        bool                                       saveAfterCheck    = false;
        // What the script weighs for its target, as the last weighing said
        // of the text at weightVersion; whether what was weighed is what a
        // save compiles -- not where the optimizer changes it after; and a
        // weighing on its way, and a save waiting on it.
        std::optional<ALScriptWeight>              weight;
        U32                                        weightVersion     = 0;
        bool                                       weightExact       = false;
        bool                                       weighing          = false;
        bool                                       saveAfterWeigh    = false;
        // The safe fixes made ahead of the save under way, once: a fix that
        // left its problem standing would be made again at every check the
        // save waits on.
        bool                                       fixedForSave      = false;
        S64                                        saveAnywayVersion = -1;
        // What the analyzer said of the text at analysisVersion; when the
        // next check is due, or zero; the version last asked about.
        ALScriptProblems                           analysis;
        U32                                        analysisVersion  = 0;
        // What a save would send, in bytes, as the last check measured it,
        // and the text and the expansion it was measured of: what the
        // trailer says once it is past half of what a script may be.
        size_t                                     assetBytes       = 0;
        std::optional<std::pair<U32, U32>>         assetMeasured;
        U32                                        requestedVersion = 0;
        F64                                        analysisDue      = 0.0;
        std::string                                definitionsError;
        // How bad a problem is: what the marks, the counts, the filters
        // and the compiler's own words all go by, rather than a word
        // compared as text in six places.
        enum class Level : U8
        {
            Note,
            Warning,
            Error
        };
        // Both, in the order the pane lists them.
        struct Shown
        {
            S32         line      = 0;
            S32         column    = 0;
            bool        hasColumn = false;
            // Where what it is about ends, as the squiggle has it; -1 for
            // a place with no stretch to it.
            S32         endLine   = -1;
            S32         endColumn = -1;
            Level       level     = Level::Note;
            std::string origin;
            std::string message;
            // An included file the problem is in, by identity and by
            // name; empty for the script itself.
            std::string file;
            std::string fileName;
            // A lint's name or an LSL warning's number, where the problem
            // is one a scripter may turn off or make an error.
            std::string lint;
            // The analyzer's key for its kind, and what would put it
            // right, in the source's places at analysisVersion: the
            // preferred first, a suppression last.
            std::string              key;
            std::vector<ALScriptFix> fixes;
            // The version of the text the fixes are in the places of: the
            // check's, or the preprocessor run's that made the note.
            U32                      fixesFor = 0;
        };
        std::vector<Shown>                         shown;
        // The refactors last offered at the caret, in the source's places
        // at actionsVersion, and the stretch they were asked about.
        std::vector<ALScriptFix>                   actions;
        U32                                        actionsVersion = 0;
        ALTextRange                                actionsAsked;
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
            // Where the name is in the text as listed, in bytes, or -1
            // where the text is not the line the place is on.
            S32          at = -1;
        };
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
            boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>      seen;
            boost::unordered_flat_map<std::string, U32, ll::string_hash, std::equal_to<>> versions;
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
            // A replace across scripts rather than a rename, which is said
            // so where a place has moved.
            bool         replace = false;
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
        // The crumb path the bar was last told, by the outline it was
        // read from: a caret that stays within the same symbols asks for
        // no new crumbs, and it is asked on every key.
        std::vector<size_t>                        crumbPath;
        U32                                        crumbsOf = 0;
        std::string                                crumbName;
        // Where the caret was last seen; when the inspector is due to be
        // told what it is on, or zero; and what it was last told about.
        ALTextPos                                  caretSeen{ -1, -1 };
        F64                                        inspectDue     = 0.0;
        ALTextPos                                  inspectAt{ -1, -1 };
        U32                                        inspectVersion = 0;
        boost::signals2::scoped_connection         changed;
    };
    static constexpr size_t NONE = static_cast<size_t>(-1);
    // The problems of every open script, in the store all the studios'
    // findings live in, keyed by the script's id; the pane lists the
    // active script's through its filters.
    // What the pane's rows say a level is, and what the compiler's own
    // word for one means.
    static const char*  levelName(Doc::Level level);
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
    bool                applyFix(Doc& doc, const ALScriptFix& fix, U32 version);
    // Which fixes to make at once: every problem's preferred one, or one
    // kind's, or only those that change nothing a script does.
    struct FixPick
    {
        std::string key;
        bool        safeOnly = false;
    };
    // The preferred fix of every problem picked, made as one step, once
    // asked; and made, the asking done. True where anything was made.
    void                askFixAll(Doc& doc, const FixPick& pick);
    bool                fixAll(Doc& doc, const FixPick& pick);
    // The preferred fixes picked, of the script's own problems, none of
    // whose edits overlap another taken before it.
    static std::vector<const ALScriptFix*> pickFixes(const Doc& doc, const FixPick& pick);
    // The problem a row of the pane is, in its script's list, where its
    // fixes are.
    const Doc::Shown*   shownOf(const LLSD& value) const;
    // The fixes of the problems on a line, as the editor lists them, each
    // with the value that finds it again; none where the text has moved on
    // since the check they were made in.
    void                fixesOn(const Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out) const;
    // The keymap's keys beside the menu's editor commands that have none.
    void                showEditorKeys();
    static Doc::Level   levelOf(const std::string& said);
    static Doc::Level   levelOf(ALScriptProblem::Severity severity);
    static ALCodeEditor::Mark markOf(Doc::Level level);

    struct ProblemTraits
    {
        static ALFindingLevel level(const Doc::Shown& p)
        {
            return p.level == Doc::Level::Error ? ALFindingLevel::Error : p.level == Doc::Level::Warning ? ALFindingLevel::Warning : ALFindingLevel::Note;
        }
        static std::string    rule(const Doc::Shown& p) { return p.origin; }
        // One with a fix that changes the script, not only a suppression.
        static bool           fixable(const Doc::Shown& p)
        {
            return std::any_of(p.fixes.begin(), p.fixes.end(), [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
        }
        static bool           mentions(const Doc::Shown& p, std::string_view text)
        {
            return ALStringMatch::containsNoCase(p.message, text) || ALStringMatch::containsNoCase(p.fileName, text) ||
                   ALStringMatch::containsNoCase(p.origin, text) || ALStringMatch::containsNoCase(levelName(p.level), text);
        }
    };

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
    std::string counted(const char* name, S32 count, LLStringUtil::format_map_t args = LLStringUtil::format_map_t()) const;

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
    void                          preprocess(Doc& doc, bool then_save);
    void                          preprocessedAnswer(const std::string& id, U32 version, bool then_save, const ALPreprocessor::Result& result);
    // The text sent to be saved and compiled, with the map it was expanded
    // through where it was.
    void                          upload(Doc& doc, const std::string& text, const ALSourceMap* map = nullptr);
    static S32                    mapSpan(const ALSourceMap& map, ALScriptSpan& span);
    // The map the text the region compiled and runs was expanded through:
    // what the compiler's lines and a run-time error's are read back by.
    // Null where the text went up as written.
    static const ALSourceMap*     runningMap(const Doc& doc);
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
    void takeCarriedText(Doc& doc);
    // What a notecard carries, each item a button in the text where its
    // placeholder is -- the character that stands for it in the format
    // -- that opens the item or offers a copy of it; and the opening.
    void             placeEmbeddedItems(Doc& doc);
    // The buttons for the placeholders on a stretch of lines that have
    // none yet: what an edit that put one back asks for.
    void             placeEmbeddedItems(Doc& doc, S32 first_line, S32 last_line);
    ALTextView::Atom embeddedAtom(Doc& doc, const ALTextPos& at, size_t index);
    // The text and the items as a save takes them: only the items the
    // text still stands somewhere, numbered afresh in the text. The
    // editor's own text and list are left as they are.
    void             carriedForSave(Doc& doc, std::string& text, std::vector<LLPointer<LLInventoryItem>>& items);
    // An inventory item dragged onto a notecard: taken where it is
    // dropped, as the legacy notecard takes one, if it may be given on.
    bool             dropOnNotecard(Doc& doc, S32 x, S32 y, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip);
    void             openEmbeddedItem(Doc& doc, LLPointer<LLInventoryItem> item);
    // A copy of an embedded item taken into the inventory by the server,
    // into a folder or the one it picks; what the server says of it
    // reaches the status bar. False, said why, for an item the saved
    // asset does not carry, which the server could not find.
    bool             copyEmbeddedItem(Doc& doc, LLPointer<LLInventoryItem> item, const LLUUID& folder, U32 callback_id = 0);
    void save(Doc& doc);
    // The save asked for while the last was on its way, made now where
    // anything is still unsaved; true where one is under way again.
    bool sendQueuedSave(Doc& doc);
    void saveAll();
    void compiled(const ALScriptWorkspace::CompileResult& result);
    void compiledHere(const ALScriptWorkspace::CompileResult& result);
    void fillProblems(const Doc* doc);
    // The pane's filters as a query over the store, for one script.
    ALFindings<Doc::Shown, ProblemTraits>::Query problemQuery(const Doc& doc) const;
    // The scripts the pane lists: the one it is about, and the others
    // open when it lists every script's, in the tabs' order.
    std::vector<const Doc*> problemDocs(const Doc* doc) const;
    // The script the pane is about: the one in front, unless the pane was
    // left on another's while a row of it was followed into another tab.
    Doc* problemsDoc();
    // A row, a reference, a place found, an outline entry chosen: the
    // place shown in its script, and the keyboard left in the list to walk
    // on through it; or, asked for with return or a double-click, taken
    // to the script to type there. Tabs opened on the way leave the panes
    // on what they were listing.
    void onProblemSelected(bool to_editor);
    void revealed(LLUICtrl* list, bool to_editor);
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
    // The first error in the list, chosen and shown; the checkers' own,
    // passing over the compiler's, where asked.
    void selectFirstError(bool checkers_only);
    // The bottom tabs' titles: how many problems and places each lists,
    // and whether a script has said something the Output tab has not
    // shown yet.
    void refreshBottomTabs();
    // The filters' row laid out again for the counts their labels carry.
    void layoutProblemFilters();
    // What the studio did, said in the status line and kept in the
    // Output tab, where it can be read again: saving, compiling,
    // preprocessing, renaming. The script's name in it is a link to it;
    // and after it a link for each thing to be done about it: save anyway,
    // try again, save a copy, export.
    void report(const std::string& text, bool failure = false, const Doc* doc = nullptr, const std::vector<std::string>& actions = {});

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
    void refreshProblems(Doc& doc);

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
    // `settled` where the file was seen empty and is asked about again, to
    // take it as empty if it still is.
    void               externalChanged(const std::string& id, const std::string& file, bool settled = false);
    void               syncExternal(Doc& doc);
    void               logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result);
    void               stopExternal(Doc& doc);
    static std::string externalFileName(const Doc& doc);
    // The places last found, whichever tab is in front: following them
    // opens other scripts, and the list stays what it was.
    void fillReferences();
    void onReferenceChosen(bool to_editor);
    // A place in an include opened in a tab of its own where the include
    // is a script or a notecard in the world; one on disk is only named.
    void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length);
    // A line, or line:column, typed into the same popover, the editor
    // showing the line as it is typed and going back on escape.
    void goToLine();
    void goToSymbol();
    // Every open tab, to pick one from by name.
    void showAllTabs();
    // An object's or a prim's row in the explorer folded shut or opened,
    // by what it stands for; and the one under a point of the explorer,
    // where the point is on its arrow.
    void explorerFold(const LLUUID& id, bool prim, std::optional<bool> folded = std::nullopt);
    bool explorerArrowAt(S32 x, S32 y, LLUUID& id, bool& prim);
    // Every command the menus hold, to give one by name.
    void showCommandPalette();

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
    // The Output tab's filters, over whose words, of what kind, with what
    // in them, as one filter.
    void onOutputFilter();
    void onOutputChosen(const ALOutputView::Entry& entry);
    // An object offered in the Output tab's filter, the one least lately
    // heard from giving way past a few dozen.
    void offerOutputObject(const LLUUID& root, const std::string& name);

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
        // A prim of a linkset's own row, rather than its object's.
        bool        primRow = false;
        bool        isItem() const { return item.notNull(); }
        ALScriptRef ref() const { return ALScriptRef(prim, item); }
    };
    void pumpExplorer();
    // The objects in hand listed again; what each prim holds asked where
    // it is not known or has changed, and of every prim where `refetch` --
    // a person asked, or something was made, renamed or deleted in one.
    void refreshExplorer(bool refetch = false);
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
    // The problems list's right-click menu: the message copied, and the
    // lint it is turned off, made an error, or looked up in the settings.
    void                     showProblemMenu(S32 x, S32 y);
    void                     onProblemMenu(const std::string& action);
    bool                     explorerActionEnabled(const std::string& action) const;
    // A script or notecard made in a prim, named through a dialog and
    // opened once the region lists it.
    void explorerCreate(const LLUUID& prim, bool notecard, bool lua);
    void explorerCreated(const ALScriptWorkspace::Created& made, const std::optional<std::string>& opening);
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
    // Every place the last search found replaced by what the box says:
    // script by script, each script's own one step to undo, a script
    // that is not open opened with the change unsaved. A script whose
    // text has moved on since the search is left alone and searched
    // again.
    void replaceAllFound();
    // A script not open is searched as the region has it: its text kept,
    // for a replace to work over, and whether it is a notecard.
    void searchDocument(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text, U32 version = 0,
                        const std::string& doc_id = std::string(), bool keep_text = false, bool notecard = false);
    // A script's places as rows, after the rest, up to what a list holds;
    // and its rows told what it holds now, in place, where it holds as
    // many places as its rows are -- false where it does not.
    void addSearchRows(size_t index);
    bool refreshSearchRows(size_t index);
    void searchLoaded(U32 generation, const std::string& where, const ALScriptWorkspace::Loaded& loaded);
    void searchSettled();
    // The rows from what was found, the row chosen and the scroll kept.
    void fillSearchResults();
    // A script open here searched again as it stands now, a moment after
    // it was typed in, its rows replaced where they were.
    void researchOpen(Doc& doc);
    // What a search's row says an open script is in.
    std::string searchWhere(const Doc& doc) const;
    void pumpSearch();
    void onSearchResult(bool to_editor);
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
    // The blanks at every line's end taken away, as one step to undo.
    void trimTrailing(Doc& doc);

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

    // Recovery. The store for this account, made on first asking, and the
    // session every entry this process writes is under.
    static ALScriptRecoveryStore* recoveryStore();
    // A tab's text as an entry of the store.
    ALScriptRecoveryEntry recoveryEntryOf(const Doc& doc) const;
    // The tab's unsaved text written now, in the state given; a clean tab's
    // entry forgotten, and an entry it took up let go of. False where what
    // is unsaved could not be written.
    bool keepForRecovery(Doc& doc, ALScriptRecoveryEntry::State state = ALScriptRecoveryEntry::State::Unsaved);
    // What the tab holds unsaved put straight among the discarded, and
    // this session's entry for it gone once it is. False where it could
    // not be written, and nothing changed.
    bool setAside(Doc& doc);
    // Written a moment after the first change since the last writing,
    // whatever is typed meanwhile; a tab gone clean forgotten at once.
    void scheduleRecovery(Doc& doc);
    // Due writings, a lost connection, and what holds each tab, looked at
    // a few times a second, whether the window is shown or not.
    void pumpRecovery();
    void checkOrphans();
    Doc::Orphan orphanOf(const Doc& doc) const;
    // What a tab holding a kept text is where its script could not be
    // loaded, by why: one that may not be changed, one that could not be
    // loaded, or one whose item or object is gone or out of sight.
    Doc::Orphan failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const;
    // A detached tab's item loaded under it now that it is in reach, what
    // it holds carried over with its history; or loaded again after a load
    // that failed, where a person asks or the next try is due.
    void reattach(Doc& doc);
    // The window, of all of them, that has a script or a file open.
    static ALFloaterScriptStudio* holderOf(const ALScriptRef& ref, const std::string& file);
    // An entry put back: into the tab that has its script or file, opened
    // where it is not, or into a tab of its own where what it came from
    // is gone.
    void recoverEntry(const ALScriptRecoveryEntry& entry);
    void takeUpEntry(Doc& doc, const ALScriptRecoveryEntry& entry);
    // A kept text put back with its undo history and its caret, where the
    // history fits the text; false, and nothing changed, where it does not.
    bool restoreHistory(Doc& doc, const ALScriptRecoveryEntry& entry);
    void openOrphan(const ALScriptRecoveryEntry& entry, Doc::Orphan orphan);
    // A tab made to hold a kept text with nothing loaded under it: unsaved,
    // with whatever its script or file was.
    void becomeOrphan(Doc& doc, const ALScriptRecoveryEntry& entry, Doc::Orphan orphan);
    // File > Recover Unsaved Changes: what earlier sessions left and what
    // was discarded lately, to open or to discard.
    void showRecovery();
    // A notecard's items as an entry keeps them, and back.
    static LLSD                                    itemsAsLLSD(const std::vector<LLPointer<LLInventoryItem>>& items);
    static std::vector<LLPointer<LLInventoryItem>> itemsFrom(const LLSD& items);
    // What a tab needs from the moment it is made: its text's changes
    // heard, its places slid; and a notecard's drops and items.
    void wireDoc(Doc& doc);
    void wireNotecard(Doc& doc);
    // The notice over the editor, for the tab in front, and what its
    // buttons do.
    void refreshNotice();
    void onNoticeAction(const std::string& action);
    // What a tab holds saved as a new item in the inventory -- a script or
    // a notecard, its items with it -- and the tab closed once it is.
    void saveCopyToInventory(Doc& doc);
    // Revert to Saved, asked about where something would be lost; and
    // whether there is anything to read again.
    void askRevert(Doc& doc);
    bool revertible(const Doc& doc) const;
    // Replace All, asked about first.
    void askReplaceAll();
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
    // The explorer's buttons, as what is chosen allows.
    void refreshExplorerButtons();
    // Start, stop, reset or restart over the rows, asked about first where
    // it reaches more than one script.
    void runExplorerScripts(const std::string& action, const std::vector<ExplorerRow>& rows);
    S32  scriptsReached(const std::vector<ExplorerRow>& rows) const;
    // Whether a queue over these prims walks the row's script already.
    static bool walkedByQueue(const ExplorerRow& row, const std::vector<std::pair<LLUUID, std::string>>& prims);
    // The explorer's row for a script, unfolded to and chosen.
    void revealInExplorer(const Doc& doc);
    // A name that a rename may not take: one of the language's own.
    bool reservedName(const Doc& doc, const std::string& name) const;
    void onMenuAction(const LLSD& param);
    bool onMenuEnable(const LLSD& param);
    bool onMenuCheck(const LLSD& param);
    void onCompileTarget();
    void onRunning();
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
    // Whether the last search's pattern did not parse.
    bool                               mSearchBadPattern = false;
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
    LLFilterEditor*                    mExplorerFilter = nullptr;
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
    ALPaneList*                        mProblems      = nullptr;
    ALFindings<Doc::Shown, ProblemTraits> mProblemStore;
    LLCheckBoxCtrl*                    mProblemErrors   = nullptr;
    LLCheckBoxCtrl*                    mProblemWarnings = nullptr;
    LLCheckBoxCtrl*                    mProblemFixable = nullptr;
    LLCheckBoxCtrl*                    mProblemNotes    = nullptr;
    LLComboBox*                        mProblemOrigin   = nullptr;
    LLFilterEditor*                    mProblemFilter   = nullptr;
    ALPaneList*                        mReferences    = nullptr;
    ALPaneList*                        mOutline       = nullptr;
    ALTextView*                        mSymbol        = nullptr;
    // Whose problems the list holds, so that a refill of the same
    // script's keeps the row chosen and the scroll.
    std::string                        mProblemsShownFor;
    LLComboBox*                        mProblemScope    = nullptr;
    // How many problems the pane has to list before its filters.
    S32                                mProblemsHeld    = 0;
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
    // Whether a run-time error has come since the Output tab was last
    // looked at, which its title says until it is.
    bool                               mOutputUnread   = false;
    LLComboBox*                        mOutputKind     = nullptr;
    LLFilterEditor*                    mOutputFind     = nullptr;
    ALOutputView*                      mOutput        = nullptr;
    LLComboBox*                        mOutputFilter  = nullptr;
    ALScopeBar*                        mSearchBar     = nullptr;
    ALPaneList*                        mSearchResults = nullptr;
    LLTextBox*                         mSearchCount   = nullptr;
    LLLineEditor*                      mSearchReplacement = nullptr;
    LLButton*                          mSearchReplace     = nullptr;
    // What each script's text was when it was searched, so that a
    // replace knows whether the places it found still stand.
    struct Found
    {
        ALScriptRef              ref;
        // The tab it was searched in, where it was open.
        std::string              doc;
        std::string              name;
        // What the row says it is in: the object and the script.
        std::string              where;
        U32                      version = 0;
        std::vector<ALTextRange> places;
        // A script that was not open: the text it was searched in, which a
        // replace works out its replacements over, and whether it is a
        // notecard, which a replace leaves alone.
        std::string              text;
        bool                     notecard = false;
        // Each place's line as listed, trimmed, and where the words start
        // in it.
        std::vector<std::string> lines;
        std::vector<S32>         at;
    };
    std::vector<Found>                 mSearchFound;
    // The tab a script found is open in, or NONE; and whether Replace All
    // would change it, which its question counts by.
    size_t                             foundIndex(const Found& one) const;
    bool                               replaceable(const Found& one) const;
    // When the scripts typed in since the search are searched again, or
    // zero; and which.
    F64                                mSearchDue = 0.0;
    std::vector<std::string>           mSearchStale;
    // The object searched, for a search of one; null otherwise. And every
    // object the search was over, which what is typed later is searched
    // again within.
    LLUUID                             mSearchRoot;
    std::vector<LLUUID>                mSearchRoots;
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
    // Answers have come that the list does not show yet: it is filled with
    // the next frame.
    bool                               mExplorerStale = false;
    // The objects and the prims of linksets folded shut in the explorer,
    // each by its id -- apart, since a linkset's root prim has its
    // object's.
    boost::unordered_flat_set<LLUUID>  mExplorerFolded;
    boost::unordered_flat_set<LLUUID>  mExplorerFoldedPrims;
    std::vector<Pinned>                mPinned;
    // The new items to be opened once their prims list them: each by its
    // prim, and by its id, or its name where the region gave no id; with
    // the scripter's template for it, put in place of the region's.
    struct OpenWhenListed
    {
        LLUUID                     prim;
        LLUUID                     item;
        std::string                name;
        std::optional<std::string> text;
    };
    std::vector<OpenWhenListed>        mOpenWhenListed;
    LLHandle<LLContextMenu>            mExplorerMenuHandle;
    LLHandle<LLContextMenu>            mProblemMenuHandle;
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
    // The objects heard from, offered in the filter, the one heard from
    // most lately last.
    std::vector<std::pair<LLUUID, std::string>> mOutputObjects;
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

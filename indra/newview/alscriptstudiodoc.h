/**
 * @file alscriptstudiodoc.h
 * @brief One tab of Script Studio: a script, a notecard or a file, and everything the studio keeps about it.
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
#include "aldiffview.h"
#include "alfindings.h"
#include "alpreprocessor.h"
#include "alscriptanalysis.h"
#include "alscriptenvelope.h"
#include "alscriptproblem.h"
#include "alscriptrecovery.h"
#include "alscriptsaveflow.h"
#include "alscriptsymbol.h"
#include "alscriptweight.h"
#include "alscriptworkspace.h"
#include "alsourcemap.h"
#include "alstringmatch.h"
#include "alwatchedfile.h"
#include "llstl.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALScriptNotecardTab;

// One tab of the studio: a script, a notecard or a file, and the views
// of it the pane can show -- its source, and what the preprocessor made
// of that where it made anything.
struct ALScriptStudioDoc
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
    // A save of it: where it stands, what it waits on, what the checks let
    // past, and what went up (ALScriptSaveFlow).
    ALScriptSaveFlow                           save;
    // What the preprocessor made of the source, in a read-only editor
    // of its own; made once there is expanded text to show.
    ALCodeEditor*                              expandedEditor = nullptr;
    // The views of a tab the pane can show, and the one asked for.
    // What was asked for stands through a reload, into a window of its
    // own and across sessions; while it has nothing to show, the
    // source is shown in its place.
    // Compare is two texts compared (ALDiffView) -- the text against one
    // kept, a compiled half against what the source makes -- shown until
    // Escape, and never kept across sessions.
    enum class View : U8
    {
        Source,
        Expanded,
        Compare
    };
    View                                       view = View::Source;
    // The comparison, made the first time one is asked for.
    ALDiffView*                                compareView = nullptr;
    // The view in front, and its text: what the view's own commands --
    // find, go to a line, fold, copy, undo -- act on, and whose caret
    // the trailer reads. A comparison's is the side with the keyboard.
    View shownView() const
    {
        return view == View::Compare && compareView     ? View::Compare
               : view == View::Expanded && expandedEditor ? View::Expanded
                                                        : View::Source;
    }
    // Whether a save has anything to send: the text changed, or a
    // compile target or an experience picked for the next save to set --
    // what the tab's dot, Save All and a close all go by. What is kept
    // against a crash is the text alone.
    bool          unsaved() const { return (editor && editor->isDirty()) || targetChosen || experienceChosen; }
    // A save on its way: sent, or waiting on the preprocessor or a
    // check -- or the preprocessor busy with the tab for any reason,
    // whose answer a save may yet wait on.
    bool          saveUnderway() const { return save.underway() || preprocessing; }
    ALCodeEditor* shownText() const
    {
        switch (shownView())
        {
            case View::Compare:  return compareView->shown();
            case View::Expanded: return expandedEditor;
            default:             return editor;
        }
    }
    // Whether the keyboard is in one of its views, shown or not.
    bool          hasKeyboard() const;
    // The map the text the region compiled and runs was expanded through:
    // what the compiler's lines and a run-time error's are read back by.
    // Null where the text went up as written.
    const ALSourceMap* runningMap() const;
    // Where that expansion begins in what the region runs, a zero-based
    // line: the envelope's own lines, which the region counts in a line it
    // names, before it; none where it went up plain.
    S32                runningCodeLine() const;
    // The envelope a save sends an expansion in: the source as written,
    // its target, the program that wrote it and when.
    ALScriptEnvelope   envelopeFor(const std::string& expanded, const std::string& program) const;

    // An include or a module a place in a text names: anywhere on an
    // #include line, the name it includes; anywhere in a require call of
    // SLua, the module. The name as written, whether it is a require, and
    // the stretch it is named over -- the line, or the call.
    struct Named
    {
        std::string name;
        bool        require = false;
        ALTextRange range;
    };
    static std::optional<Named> namedIn(const ALTextDocument& text, const ALTextPos& at, bool lua,
                                        const std::vector<ALPreprocessor::Required>& calls);
    // The same in the tab's source, its require calls found once a text.
    std::optional<Named> namedAt(const ALTextPos& at) const;
    // The file an include or a module the script names is, as the last run
    // of the preprocessor over its text found it -- its identity: an
    // inventory path, a disk path -- or empty where no run did.
    std::string foundAs(const std::string& name, std::optional<bool> require = std::nullopt) const;
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
    // What was last written there -- the text's version, its history's
    // revision, the picks for its next save, as what, and whether forced
    // out to the disk -- so that nothing is written again where none has
    // moved.
    struct RecoveryWritten
    {
        bool                         valid   = false;
        U32                          text    = 0;
        U32                          history = 0;
        ALScriptRecoveryEntry::State state   = ALScriptRecoveryEntry::State::Unsaved;
        bool                         durable = false;
        // And what was picked for its next save.
        std::optional<std::string>   target;
        std::optional<LLUUID>        experience;
    };
    RecoveryWritten                            recoveryWritten;
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
    // What it is, and the rest of what ALScriptStudioOrphans keeps of it.
    struct Orphaned
    {
        Orphan kind            = Orphan::None;
        bool   noticeDismissed = false;
        // A kept text with nothing loaded under it -- its item out of
        // reach as it was opened -- whose script is known only as the
        // entry said: loaded under it once the item is in reach, before
        // it is saved. How many loads have failed on the way since the
        // last that went through, and when the next may be tried: further
        // apart each time, and a few times only unless a person asks.
        bool   detached      = false;
        S32    reattachTries = 0;
        F64    nextReattach  = 0.0;
        // Since when its object has been out of sight, or zero: an object
        // at the edge of what is in view comes and goes, and is taken for
        // gone only once it has been gone a moment.
        F64    awaySince = 0.0;
    };
    Orphaned                                   orphan;
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
    // A notecard's items (ALScriptNotecardTab), from the moment it is
    // loaded or kept as one; none for a script or a text file. Shared so
    // that an answer coming after the tab has gone finds nothing.
    std::shared_ptr<ALScriptNotecardTab>       items;
    // The envelope the asset came in, whose source the editor holds
    // and whose expanded code the other editor shows; a save runs
    // the preprocessor over the source and wraps both again.
    std::optional<ALScriptEnvelope>            envelope;
    // The version a wrapped script was loaded at, where the first run over
    // its source is to be held up to its compiled half; none once it has
    // been. And the compiled half, where the source could not have made
    // it (ALScriptEnvelope::compiledFrom) -- changed outside the
    // preprocessor, or what it includes changed since -- offered in the
    // notice as the source, since the next save replaces it.
    std::optional<U32>                         compareCompiledAt;
    std::optional<std::string>                 compiledDiffers;
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
        // What the code came to on the script's target before the
        // optimizer and after, where it ran and was weighed.
        size_t           codeBefore = 0;
        size_t           codeAfter  = 0;
        // What each include and module was found as (ALPreprocessor).
        std::vector<ALPreprocessor::Result::Resolved> resolved;
    };
    Expanded                                   expanded;
    Expanded                                   uploaded;
    // The source's require calls, of the text at requiresOf (namedAt).
    mutable std::vector<ALPreprocessor::Required> requiresFound;
    mutable std::optional<U32>                    requiresOf;
    // A question held until the expansion it asks about comes
    // (Check::waiting). A question of a kind replaces the one of that
    // kind still waiting: a second hover is a hover of somewhere else,
    // and only the last is wanted.
    struct Waiting
    {
        ALScriptAnalysis::Kind kind = ALScriptAnalysis::Kind::Check;
        ALTextPos              at;
        // Where a stretch chosen from `at` ends: the refactors'.
        ALTextPos              to;
    };
    // A run of the preprocessor on its way, for a save or not: a save
    // asked for meanwhile waits on it rather than starting another.
    bool                                       preprocessing       = false;
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
    // once that has loaded; and a compile target and an experience
    // picked there for the next save, picked again here once it has.
    std::optional<std::string>                 carriedText;
    std::optional<std::string>                 carriedTarget;
    std::optional<LLUUID>                      carriedExperience;
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
    // The experience a script in an object runs under -- the null one
    // for none -- as its region said, or as picked here for the next
    // save to set; a save that knows neither keeps whatever it is.
    LLUUID                                     experience;
    bool                                       experienceKnown  = false;
    bool                                       experienceChosen = false;
    bool                                       experienceAsking = false;
    // What the script weighs (ALScriptStudioWeighing).
    struct Weighing
    {
        // For its target, as the last weighing said of the text at
        // `version`; whether what was weighed is what a save compiles --
        // not where the optimizer changes it after -- and whether it is
        // what a preprocessor's run made to be sent, which a check's
        // weighing of the same text, before the optimizer, does not
        // replace; and a weighing on its way.
        std::optional<ALScriptWeight> weight;
        U32                           version = 0;
        bool                          exact   = false;
        bool                          sent    = false;
        bool                          asking  = false;
        // What the Weights tab lists: each target the last check's text
        // was weighed for, its own first, in the source's places, of the
        // text at `allVersion`; and each target's as the text was last
        // saved, where it was weighed while it was that text -- what
        // "since the save" counts from.
        std::vector<ALScriptWeight> all;
        U32                         allVersion = 0;
        std::vector<ALScriptWeight> saved;
        // What a save would send, in bytes, as the last check measured
        // it, and the text and the expansion it was measured of: what the
        // trailer says once it is past half of what a script may be.
        size_t                             assetBytes = 0;
        std::optional<std::pair<U32, U32>> assetMeasured;
    };
    Weighing                                   weighing;
    // The tab's part of checking (ALScriptStudioChecking): what the
    // analyzers said and when they are next asked, the expansion asked for
    // them and the questions waiting on it, the refactors offered at the
    // caret, and a Fix All waiting on a check.
    struct Check
    {
        // What the analyzer said of the text at analysisVersion; when the
        // next check is due, or zero; the version last asked about.
        ALScriptProblems analysis;
        U32              analysisVersion  = 0;
        U32              requestedVersion = 0;
        F64              analysisDue      = 0.0;
        std::string      definitionsError;
        // How many expansions were taken, which is how an answer about one
        // names it; the version one has been asked for, or none -- not a
        // zero, which an empty text's version is: the preprocessor answers
        // on the main thread a moment later, and one text is expanded once
        // however many questions wait on it.
        U32                expansions = 0;
        std::optional<U32> expanding;
        // The questions held until it comes.
        std::vector<Waiting> waiting;
        // Whether the script's `.luaurc` was asked for once, so that a
        // script with none is not asked for it at every check.
        bool configAsked = false;
        // The refactors last offered at the caret, in the source's places
        // at actionsVersion, and the stretch they were asked about.
        std::vector<ALScriptFix> actions;
        U32                      actionsVersion = 0;
        ALTextRange              actionsAsked;
        // A Fix All asked before the text as it stands was checked, made
        // once it is: of the problems of one kind, or of all where empty.
        std::optional<std::string> fixAllAfterCheck;
    };
    Check                                      check;
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
    // The problem listed at a place, in the script or an include, saying
    // what it says; null where the list has been made again without it.
    const Shown* findShown(S32 line, S32 column, const std::string& file, const std::string& message) const;
    // Which fixes to make at once: every problem's preferred one, or one
    // kind's, or only those that change nothing a script does.
    struct FixPick
    {
        std::string key;
        // Only what may be made on a save: safe, and taking nothing out.
        bool        forSave = false;
    };
    // The preferred fixes picked, of the script's own problems, none of
    // whose edits overlap another taken before it. Over the whole script,
    // only the safe ones: a cast, a call put for a deprecated one that
    // behaves otherwise, a guess at a name, a require, is each a choice,
    // counted in `left` and left to be made one by one. Of one kind, asked
    // for by it, every preferred one.
    std::vector<const ALScriptFix*> pickFixes(const FixPick& pick, size_t* left = nullptr) const;
    // What the analyzer said the script declares, at analysisVersion.
    std::vector<ALScriptOutlineEntry>          outline;
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
        // What a list of places knows this one by while it is shown,
        // counted from one as it is shown; kept as the places slide with
        // edits and some go, where a place in the list would not be.
        U32          id = 0;
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
    // The script held open in an external editor (ALScriptExternalEditor).
    // Whether the save under way came from the editor, which does not
    // write the file back, is the save's (ALScriptSaveFlow::external).
    struct External
    {
        // The file under the temp folder the editor was given, watched
        // for the editor's saves -- or the tab's file on disk, watched
        // for changes made to it outside; and the log beside the copy the
        // compiler's words go to.
        std::unique_ptr<ALWatchedFile> watch;
        std::string                    log;
        // Whether the bridge was told, so that VS Code can subscribe.
        bool                           subscribed = false;
        // What the copy held when the studio last wrote it or read it: a
        // save there over changes made here since is asked about rather
        // than taken, the text it brought held in `waiting` until the
        // author says which.
        std::string                    written;
        std::optional<std::string>     waiting;
    };
    External                                   external;
    // The tab's part of what is said of its caret: the outline's folds,
    // the name asked about, the path the bar shows, and the inspector's
    // question about where the caret is.
    struct Caret
    {
        // The outline's symbols folded shut, each by the names from the
        // outermost down to it.
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> outlineFolded;
        // The name last asked about -- its definition, its references, a
        // new name -- where, and of which text.
        ALEditorCommand symbolCommand = ALEditorCommand::None;
        U32             symbolVersion = 0;
        ALTextPos       symbolAt;
        // The crumb path the bar was last told, by the outline it was
        // read from: a caret that stays within the same symbols asks for
        // no new crumbs, and it is asked on every key.
        std::vector<size_t> crumbPath;
        U32                 crumbsOf = 0;
        std::string         crumbName;
        // Where the caret was last seen; when the inspector is due to be
        // told what it is on, or zero; and what it was last told about.
        ALTextPos seen{ -1, -1 };
        F64       inspectDue = 0.0;
        ALTextPos inspectAt{ -1, -1 };
        U32       inspectVersion = 0;
    };
    Caret                                      caret;
    boost::signals2::scoped_connection         changed;

    // What the Problems pane's rows say a level is, and what the compiler's
    // own word for one means; and the mark a level puts in the gutter.
    static const char*        levelName(Level level);
    static Level              levelOf(const std::string& said);
    static Level              levelOf(ALScriptProblem::Severity severity);
    static ALCodeEditor::Mark markOf(Level level);
    // How the store every studio's findings live in reads a problem.
    struct ProblemTraits
    {
        static ALFindingLevel level(const Shown& p)
        {
            return p.level == Level::Error ? ALFindingLevel::Error : p.level == Level::Warning ? ALFindingLevel::Warning : ALFindingLevel::Note;
        }
        static std::string    rule(const Shown& p) { return p.origin; }
        // One with a fix that changes the script, not only a suppression.
        static bool           fixable(const Shown& p)
        {
            return std::any_of(p.fixes.begin(), p.fixes.end(), [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
        }
        static bool           mentions(const Shown& p, std::string_view text)
        {
            return ALStringMatch::containsNoCase(p.message, text) || ALStringMatch::containsNoCase(p.fileName, text) ||
                   ALStringMatch::containsNoCase(p.origin, text) || ALStringMatch::containsNoCase(levelName(p.level), text);
        }
    };
};

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

#include "alfindings.h"
#include "alluauservice.h"
#include "alpreprocessor.h"
#include "alrecoverystore.h"
#include "alscriptenvelope.h"
#include "alscriptproblem.h"
#include "alscriptsaveflow.h"
#include "alscriptsymbol.h"
#include "alscripttypes.h"
#include "alsourcemap.h"
#include "alstringmatch.h"
#include "altextdocument.h"
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

class ALCodeEditor;
class ALDiffView;
class ALNotecardEmbedded;
class ALScriptStudioServices;
class ALWatchedFile;

// One tab of the studio: a script, a notecard or a file, and the views
// of it the pane can show -- its source, and what the preprocessor made
// of that where it made anything.
struct ALScriptStudioDoc
{
    ALScriptStudioDoc();
    ~ALScriptStudioDoc();
    ALScriptStudioDoc(const ALScriptStudioDoc&)            = delete;
    ALScriptStudioDoc& operator=(const ALScriptStudioDoc&) = delete;

    // A unit's part of the tab: what the unit keeps of it, which its header
    // defines and only it writes; other units read it, and ask the unit to
    // change it. Made with the tab and gone with it, and const wherever the
    // tab is.
    template <typename T>
    class Part
    {
    public:
        Part() : mPart(std::make_unique<T>()) {}
        T*       operator->() { return mPart.get(); }
        const T* operator->() const { return mPart.get(); }
        T&       operator*() { return *mPart; }
        const T& operator*() const { return *mPart; }

    private:
        std::unique_ptr<T> mPart;
    };

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
    ALScriptLanguage                           language;
    LLUUID                                     assetId;
    bool                                       loaded     = false;
    // Why the last load came back with no text, and what it said: nothing
    // is coming to put a kept text over until it is loaded again.
    ALScriptLoaded::Failure                    loadFailure = ALScriptLoaded::Failure::None;
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
    bool          unsaved() const;
    // A save on its way: sent, or waiting on the preprocessor or a
    // check -- or the preprocessor busy with the tab for any reason,
    // whose answer a save may yet wait on.
    bool          saveUnderway() const { return save.underway() || preprocessing; }
    ALCodeEditor* shownText() const;
    // Whether the keyboard is in one of its views, shown or not.
    bool          hasKeyboard() const;
    // Whether a check asked about is still unanswered at `now`, a while
    // after it was asked: one that answers as a key is typed is not said.
    bool          checkRunning(F64 now) const;
    // The map the text the region compiled and runs was expanded through:
    // what the compiler's lines and a run-time error's are read back by.
    // Null where the text went up as written.
    const ALSourceMap* runningMap() const;
    // Where that expansion begins in what the region runs, a zero-based
    // line: the envelope's own lines, which the region counts in a line it
    // names, before it; none where it went up plain.
    S32                runningCodeLine() const;
    // A place in what the region runs -- a line it names, counting the
    // envelope's -- read back to the source's, or an include's: as it
    // is where the text went up as written, or its map is not known.
    struct RunningPlace
    {
        S32         line   = -1;
        S32         column = -1;
        std::string file;
        std::string fileName;
        // In code the preprocessor made, which no line of the source or an
        // include stands for: the line is the expansion's, counted from
        // its code's first.
        bool        generated = false;
    };
    // The file a problem is said to be in where it is in such code: its
    // line the expansion's, which the Preprocessed view shows.
    static const std::string GENERATED;
    RunningPlace       placeOfRunning(S32 line, S32 column) const;
    // The envelope a save sends an expansion in: the source as written,
    // its target, the program that wrote it and when.
    ALScriptEnvelope   envelopeFor(const std::string& expanded, const std::string& program) const;
    // The text as it stands, for what takes it off the main thread: one
    // copy of a version, shared by every question asked of it while any of
    // them holds it, rather than one copy each.
    std::shared_ptr<const std::string> snapshot() const;
    // What this tab's items are carried as into another tab -- in another
    // window, or this one loaded again under a kept text: its items, where
    // it is a notecard's.
    void carryItemsTo(ALScriptStudioDoc& to) const;

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
    // A notecard in the world, rather than a text file on disk: what a
    // script reads by line, which is shown the way a notecard is.
    bool itemNotecard() const { return notecard && file.empty(); }
    // A notecard's grammar -- "text", "json" or "config" -- guessed from
    // its text as it loads (ALNotecardFormat::guess), or picked from the
    // strip, which a load keeps.
    std::string                                grammar;
    bool                                       grammarPicked = false;
    // Opened by walking a pane's list past a place in it: looked at
    // without being held, and replaced by the next one looked at, until
    // it is typed in, saved, gone to or double-clicked.
    bool                                       preview = false;
    // The file watched for changes made to it outside the studio: the
    // tab's own on disk (ALScriptStudioFiles), or the copy under the
    // temp folder an external editor was given, watched for the editor's
    // saves (ALScriptExternalEditor).
    std::unique_ptr<ALWatchedFile>             watch;
    // A file changed on disk while this tab had unsaved changes, and
    // the author asked what to do: once, however often it changes.
    bool                                       askingReload = false;
    // Whose text it is in what keeps the unsaved text against a crash
    // (ALRecoveryStore).
    std::string                                recoveryKey;
    // The rest of what ALScriptStudioRecovery keeps of it.
    struct Recovery;
    Part<Recovery>                             recovery;
    // What an earlier session left of this, found as it opened, offered
    // in the notice until it is restored or discarded; and one being
    // taken up here, whose file goes once this tab's own is written.
    std::optional<ALRecoveryEntry>       recoverable;
    std::optional<ALRecoveryEntry>       recovering;
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
    struct Orphaned;
    Part<Orphaned>                             orphan;
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
    // A notecard's items (ALNotecardEmbedded), from the moment it is
    // loaded or kept as one; none for a script or a text file. Shared so
    // that an answer coming after the tab has gone finds nothing.
    std::shared_ptr<ALNotecardEmbedded>       items;
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
    // What the last word about this tab offered to do about it -- a save
    // tried again or made over what stopped it, a copy, a file, one of two
    // texts taken -- said again in the notice while the tab is in front,
    // where the status line cannot be pressed and Output may be out of
    // sight; until one is done, from either, the notice is hidden, or a
    // save answers a save's.
    struct Offer
    {
        std::string              text;
        std::vector<std::string> actions;
        bool                     bySave() const
        {
            return std::ranges::any_of(actions, [](const std::string& action) { return action == "retry" || action == "save_anyway"; });
        }
        bool offers(const std::string& action) const { return std::ranges::find(actions, action) != actions.end(); }
    };
    std::optional<Offer>                       offer;
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
        // Shared with every question asked over it.
        std::shared_ptr<const std::string> text = std::make_shared<const std::string>();
        ALSourceMap      map;
        // Its lines that are wholly an include's or a module's
        // (ALSourceMap::othersLines), which the analyzers pass over.
        std::vector<std::pair<S32, S32>> elsewhere;
        // SLua with requires, apart (ALPreprocessor::Options::apart): the
        // text and map above are the script's alone, its requires calls;
        // these are the modules they reach, each module's map by its key
        // for what the checker says of it, and the bundle a save sends,
        // which is what it weighs. Empty where the text is the bundle.
        std::shared_ptr<const ALLuauService::Modules>  modules;
        std::vector<std::pair<std::string, ALSourceMap>> moduleMaps;
        std::shared_ptr<const std::string>              bundle;
        ALScriptProblems problems;
        // What the code came to on the script's target before the
        // optimizer and after, where it ran and was weighed.
        size_t           codeBefore = 0;
        size_t           codeAfter  = 0;
        // What each include and module was found as (ALPreprocessor).
        std::vector<ALPreprocessor::Result::Resolved> resolved;
        // What the script declared const, where each name is in the text,
        // which the analyzers read with the word taken off.
        std::vector<ALPreprocessor::Result::Const> consts;
    };
    Expanded                                   expanded;
    Expanded                                   uploaded;
    // The source's require calls, of the text at requiresOf (namedAt).
    mutable std::vector<ALPreprocessor::Required> requiresFound;
    mutable std::optional<U32>                    requiresOf;
    // The last snapshot made, while something holds it, and its version.
    mutable std::weak_ptr<const std::string>      snapshotHeld;
    mutable U32                                   snapshotVersion = 0;
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
    // Those heard before the map they are read back by was known -- in a
    // script closed then, or loaded and not yet expanded -- in the lines
    // the region counts, placed once it is (placeHeldRuntime).
    std::vector<RuntimeProblem>                runtimeHeld;
    // Whether this session's errors from before it was opened were taken.
    bool                                       runtimeRecalled = false;
    // A run-time error heard, as the region counts its lines: among the
    // run-time problems at its place, counted where it was said before --
    // a script failing in a timer says it every tick -- or, where `hold`,
    // kept among those held until the map is known.
    void heardRuntime(const RuntimeProblem& running, bool hold);
    // Those held, placed.
    void placeHeldRuntime();
    // Both were said of the text as it was: each moves with the edits
    // since, and goes when one touches its line, the text there being
    // no longer what was compiled or run.
    boost::signals2::scoped_connection         placedEdits;
    // Text brought from another window, put in place of the server's
    // once that has loaded; and a compile target and an experience
    // picked there for the next save, picked again here once it has.
    std::optional<std::string>                 carriedText;
    // A save of this item from elsewhere -- VS Code, another editor, a
    // queue -- that landed over changes made here: its text, until the
    // author says whose to keep.
    std::optional<std::string>                 savedThere;
    std::optional<std::string>                 carriedTarget;
    std::optional<LLUUID>                      carriedExperience;
    // A line to go to once the script has loaded, or -1; and a
    // stretch of it to select, where a column is given.
    S32                                        pendingLine   = -1;
    S32                                        pendingColumn = -1;
    S32                                        pendingLength = 0;
    // Whether that line is one the region counts, in what it runs, to be
    // read back to the source once the map is known.
    bool                                       pendingRunning = false;
    // Whether the script runs in its object, as the region last
    // said: -1 until it has.
    S32                                        running = -1;
    // What the Running box last asked of the region, until the region
    // says it is so or has been asked enough times; and how many times
    // more it is asked.
    std::optional<bool>                        runningAsked;
    S32                                        runningAsks = 0;
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
    struct Weighing;
    Part<Weighing>                             weighing;
    // The tab's part of checking (ALScriptStudioChecking).
    struct Check;
    Part<Check>                                check;
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
    // How many of them are errors and warnings, counted as they are put
    // in: the tab's dot and the trailer ask on every key.
    S32                                        shownErrors   = 0;
    S32                                        shownWarnings = 0;
    // The rows put in, and counted.
    void setShown(std::vector<Shown> rows);
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
    // The name being looked up across the object's scripts (ALScriptLookup).
    struct Lookup;
    Part<Lookup>                               lookup;
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
    struct External;
    Part<External>                             external;
    // The outline's symbols folded shut, each by the names from the
    // outermost down to it (ALScriptOutlinePane).
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> outlineFolded;
    // The tab's part of what is said of its caret (ALScriptStudioCaret).
    struct Caret;
    Part<Caret>                                caret;
    boost::signals2::scoped_connection         changed;

    // What the Problems pane's rows say a level is, and what the compiler's
    // own word for one means.
    static const char* levelName(Level level);
    static Level       levelOf(const std::string& said);
    static Level       levelOf(ALScriptProblem::Severity severity);
    // A problem the analyzers found as its row says it: its level, whose
    // word it is, the message with the lint's name after it, and the lint
    // where a scripter may turn it off. Its place and file are the
    // problem's own, which the caller reads it back to.
    static Shown analysisRow(const ALScriptProblem& problem, bool lua, const ALScriptStudioServices& services);
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

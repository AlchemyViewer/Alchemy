/**
 * @file alscriptanalysis.h
 * @brief The analyzers on a thread of their own, checking scripts as they are typed.
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

#include "alluauconfig.h"
#include "alluauservice.h"
#include "alscriptjobqueue.h"
#include "alscriptproblem.h"
#include "alscriptsymbol.h"
#include "alscriptweight.h"
#include "llsingleton.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

class ALSerialWorker;
namespace Luau
{
    struct FrontendCancellationToken;
}

// The LSL and SLua analyzers, each owned by one worker thread and asked
// about one script at a time:
// what is wrong with it and what it declares, what could go at a
// position, what is at one, what a call there takes, and where a name is
// bound and used. A request carries the document's version, the
// answer comes back on the main thread, and whoever asked drops it when
// the document has moved on. The definitions the analyzers work from are
// the region's, by the paths the syntax cache gives, read on the worker.
//
// Tailslide's builtins are a table the library holds once for the whole
// process and adds to rather than replaces, so a region's LSL builtins add
// what the table lacks; the Luau definitions are read again when the
// region's change, and when ALScriptLuauSolver picks the other of Luau's
// type solvers. An SLua
// type check is held to ALScriptLuauCheckSeconds, and one a newer check of
// its script has made pointless is stopped where it is.
// Words keyed as the library keys them, in the skin's language where it has
// the key, else the English they came with.
std::string alScriptKeyedWords(const std::string& key, const std::vector<std::string>& args, const std::string& english);
// A problem's words in the viewer's language: where the problem carries a
// key -- one of this code's own messages -- the skin's string for the
// key with the problem's words put in for [1], [2] and so on, else the
// message as the engine said it. And every problem of a list put into
// them, with what each of its fixes does, where they come off the
// analyzers and the preprocessor.
std::string alScriptProblemWords(const ALScriptProblem& problem);
void        alTranslateScriptProblems(ALScriptProblems& problems);

// What a scripter chose the linters say: each of LSL's warnings and each
// of Luau's lints off, a warning, or an error, by the setting
// ALScriptLintLevels, which holds only what differs from the default; and
// the mode an SLua script is checked in where it says none.
namespace ALScriptLints
{
    enum class Level : U8
    {
        Off,
        Warning,
        Error
    };
    // A lint by what names it: Tailslide's number for LSL, the name a
    // `.luaurc` uses for Luau.
    struct Lint
    {
        std::string id;
        bool        lua = false;
    };
    // Every one there is a choice about, LSL's then Luau's.
    const std::vector<Lint>& all();
    Level                    level(bool lua, std::string_view id);
    void                     setLevel(bool lua, std::string_view id, Level level);
    // A whole language's at once, so that what follows the setting --
    // every script checked again -- follows it once.
    void                     setLevels(bool lua, const std::vector<std::pair<std::string, Level>>& levels);
    // Everything back to the default, the mode and the solver too.
    void                     reset();
    // LSL's problems as chosen: a warning turned off dropped, one made an
    // error raised; the rest as they were. Errors are not a choice.
    void                     apply(ALScriptProblems& problems);
    // What an SLua script is checked with where no `.luaurc` says: the
    // lints on and the ones that are errors, and the mode.
    ALLuauConfig             luauBase();
}

class ALScriptAnalysis : public LLSingleton<ALScriptAnalysis>
{
    LLSINGLETON(ALScriptAnalysis);
    ~ALScriptAnalysis() override;

public:
    enum class Kind : U8
    {
        // What is wrong, and what the script declares.
        Check,
        Complete,
        // What is under the mouse, for a tip; what is at the caret, for
        // the inspector. The same question, told apart by who asked.
        Hover,
        Inspect,
        Signature,
        // Where the name at a position is bound and used.
        References,
        // What could be done at the caret, or to the stretch chosen from
        // it, that no problem asks for: the refactors.
        Actions,
        // What the script weighs for each target asked for: its code, by
        // part and by line (ALScriptWeight).
        Weigh,
        // What a text declares, from its parse alone and with no types
        // (ALLuauService::shape, or LSL's outline): a comparison's texts,
        // which need be no tab's, and whose functions it pairs.
        Shape,
        // No question: what an SLua fragment is checked against made the
        // text's (ALLuauService::warm), once the front tab's check has
        // landed, so the next keystroke starts from the text as it settled.
        // Asked by the thread itself, and answered to nobody.
        Warm
    };
    struct Request
    {
        Kind        kind    = Kind::Check;
        std::string id;
        U32         version = 0;
        bool        lua     = false;
        // LSL only: Mono's rules for a global initialiser, else LSO's.
        bool        mono    = true;
        // SLua only: what the script's `.luaurc` says -- the mode, the
        // lints, the globals -- or the defaults where it has none.
        ALLuauConfig config;
        // The text asked about, shared rather than copied: every question
        // of a version holds the one copy (ALScriptStudioDoc::snapshot, or
        // the expansion's own), which lives while any of them does.
        std::shared_ptr<const std::string> text;
        // Where, for anything but a check.
        S32         line   = 0;
        S32         column = 0;
        // Actions only: where a stretch chosen from (line, column) ends,
        // or the same place for the caret alone.
        S32         endLine   = 0;
        S32         endColumn = 0;
        // Weigh, or a check the script's own weigh is folded into: the
        // targets to weigh it for. A weigh only: or, where there are
        // any, texts to weigh in its place -- the script with a fix's edits
        // made, say -- each for the first target alone, and answered as
        // each one's total in order (Result::variantTotals).
        std::vector<ALScriptWeight::Target> targets;
        std::vector<std::string>            variants;
        // Weigh only: which of a script's weighs this is -- the text as it
        // stands, the text a save sends, or the fixes' costs -- none of
        // which stands in for another as they wait.
        enum class Weighing : U8
        {
            Text,
            Sent,
            Fixes
        };
        Weighing    weighing = Weighing::Text;
        // Whether it is about the tab in front: its questions go before
        // anything else, then its check, then its warm job; then weighing;
        // then the rest.
        bool        front = false;
        // The lines of the text nobody reads the names, hints and fixes of:
        // what an include put into an expansion (ALSourceMap::othersLines).
        std::vector<std::pair<S32, S32>> passedOver;
        // SLua over an expansion that keeps its requires as calls: the
        // modules they reach (ALLuauService::setModules); and the bundle a
        // save sends, which is what a check's weigh weighs.
        std::shared_ptr<const ALLuauService::Modules> modules;
        std::shared_ptr<const std::string>            bundle;
        // What a check says beyond the problems and the outline: what
        // every name is, and what the editor may show beside the text.
        bool        semantics      = false;
        bool        hintParameters = false;
        bool        hintTypes      = false;
    };
    struct Result
    {
        Kind                            kind    = Kind::Check;
        std::string                     id;
        U32                             version = 0;
        bool                            lua     = false;
        S32                             line    = 0;
        S32                             column  = 0;
        ALScriptProblems                   problems;
        std::vector<ALScriptOutlineEntry>  outline;
        std::vector<ALScriptSemanticToken> semantics;
        std::vector<ALScriptInlayHint>     hints;
        std::vector<ALScriptCompletion>   completions;
        ALScriptHover                     hover;
        ALScriptSignature                 signature;
        ALScriptReferences                references;
        std::vector<ALScriptFix>          actions;
        std::vector<ALScriptWeight>       weights;
        // Each variant's total as its target counts it, nothing where it
        // came to nothing.
        std::vector<size_t>               variantTotals;
        // Why the analyzer ran without its definitions, or nothing.
        std::string                     definitionsError;
        // Whether the text parsed at all, and whether there was a tree to
        // answer from even so: an LSL script mid-edit is answered from a
        // copy mended to parse, and one past mending answers nothing, where
        // what the editor shows should be what it last knew rather than
        // nothing.
        bool                            parsed     = true;
        bool                            understood = true;
    };
    typedef std::function<void(const Result&)> callback_t;

    // Asks the worker and answers on the main thread. Only the latest of
    // each kind of question about each script waits -- typing through a
    // slow check would otherwise queue one whole-script check per
    // keystroke, every answer but the last thrown away on arrival -- and
    // the next is picked by rank: the front tab's questions, its check,
    // weighing, then everything else. A question about a text older than
    // one asked about since is passed over, and not answered; so is one
    // whose answer a newer question makes pointless while it runs, which
    // is stopped where it is an SLua one. Nothing is answered for what is
    // passed over: whoever asked has asked again.
    void ask(Request request, callback_t callback);

    // A script let go of: nothing it has waiting is run.
    void forget(const std::string& id);

    // Tailslide's work that is no question -- the optimizer's run over what
    // a save sends -- done on this thread, which every use of Tailslide is
    // on: then none of it runs at once with another, and nothing need be
    // locked. At the rank of the front tab's check, since a save waits on
    // it; `done` on the main thread once `work` has run, or at once where
    // the thread is closing and will not.
    void runEngine(std::function<void()> work, std::function<void()> done);

    // The region's definitions changed: the Luau ones are read again
    // before the next check.
    void definitionsChanged();

private:
    void cleanupSingleton() override;
    void ensureStarted();

    struct Worker;
    // The one thread the services run on. Closed at cleanup, or as the
    // viewer starts to quit, and kept closed: an ask after is answered
    // with nothing rather than starting another.
    std::unique_ptr<ALSerialWorker>                     mThread;
    // Touched only from the worker's own tasks, which one thread runs one
    // after another.
    std::unique_ptr<Worker>                             mWorker;
    U32                                                 mDefinitionsGeneration = 1;
    // One job waiting: the question, who is answered, and what the main
    // thread read for it -- the definitions' paths, the settings.
    struct Job
    {
        Request                     request;
        std::shared_ptr<callback_t> callback;
        std::string                 luauPath;
        std::string                 docsPath;
        std::string                 lslPath;
        U32                         generation = 0;
        bool                        newSolver  = false;
        bool                        fragments  = false;
        F32                         seconds    = 0.f;
        // Tailslide's work that is no question (runEngine), where it is one.
        std::function<void()>       engineWork;
        std::function<void()>       engineDone;
    };
    // Where a question waits: under its script and its kind -- and, for a
    // weigh, which of its weighs. And how soon it goes, lower first: the
    // front tab's questions, which someone is waiting on; its check; its
    // warm job; weighing; everything else -- background tabs, lookups.
    static std::string keyOf(const Request& request);
    static U8          rankOf(const Request& request);
    // Takes the next job and runs it, on the worker: one is posted for
    // every question asked, and one that finds nothing waiting -- its
    // question replaced by a later one -- does nothing.
    void runNext();
    Result run(const Job& job, const std::shared_ptr<Luau::FrontendCancellationToken>& stop);

    // What waits, written on the main thread and taken on the worker, and
    // the stop of the SLua job running, if any: a question that makes its
    // answer pointless stops it, and so does the viewer closing. Both
    // under the lock.
    std::mutex                                       mQueueMutex;
    ALScriptJobQueue<Job>                            mQueue;
    std::shared_ptr<Luau::FrontendCancellationToken> mRunningStop;
    // Numbers the engine's work, each its own key.
    U32                                              mEngineSerial = 0;
};

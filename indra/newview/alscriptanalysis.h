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
// process and adds to rather than replaces, so the LSL builtins are loaded
// once; the Luau definitions are read again when the region's change, and
// when ALScriptLuauSolver picks the other of Luau's type solvers. An SLua
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
        Weigh
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
        std::string text;
        // Where, for anything but a check.
        S32         line   = 0;
        S32         column = 0;
        // Actions only: where a stretch chosen from (line, column) ends,
        // or the same place for the caret alone.
        S32         endLine   = 0;
        S32         endColumn = 0;
        // Weigh only: the targets to weigh it for. Or, where there are
        // any, texts to weigh in its place -- the script with a fix's edits
        // made, say -- each for the first target alone, and answered as
        // each one's total in order (Result::variantTotals).
        std::vector<ALScriptWeight::Target> targets;
        std::vector<std::string>            variants;
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

    // Asks the worker and answers on the main thread. A check of a
    // script whose newer check is already waiting is passed over rather
    // than run: typing through a slow check would otherwise queue one
    // whole-script check per keystroke, every answer but the last
    // thrown away on arrival, with a hover or a completion waiting
    // behind them all.
    void ask(Request request, callback_t callback);

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
    // The newest check asked for of each script with one waiting, by the
    // serial they were asked in: a check the worker reaches with a newer
    // one already asked for is passed over, and the newest, reached,
    // takes its script off. Written on the main thread, read on the
    // worker; both under the lock, since a check may take a moment and
    // the main thread goes on asking meanwhile.
    std::mutex                                          mLatestMutex;
    boost::unordered_flat_map<std::string, U32, ll::string_hash, std::equal_to<>> mLatestCheck;
    U32                                                 mAskSerial = 0;
    // The SLua check the worker is running, if any, and what stops it: a
    // newer check of the same script asked for stops it, its answer being
    // one that would be thrown away, and so does the viewer closing. Under
    // the same lock. Held as the Luau stop token, which only the service
    // knows the inside of.
    std::string                                         mRunningId;
    std::shared_ptr<Luau::FrontendCancellationToken>    mRunningStop;
};

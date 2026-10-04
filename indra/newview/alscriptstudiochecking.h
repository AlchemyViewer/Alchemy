/**
 * @file alscriptstudiochecking.h
 * @brief Script Studio's checking: the analyzers asked and answered, the expansion for them, imports offered, fixes made.
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
#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alscriptanalysis.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptstudiodoc.h"
#include "llsd.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioAnalysis;
class ALScriptStudioSaves;
class ALScriptStudioWeighing;
class ALScriptStudioServices;

// The tab's part of checking (ALScriptStudioChecking): what the
// analyzers said and when they are next asked, the expansion asked for
// them and the questions waiting on it, the refactors offered at the
// caret, and a Fix All waiting on a check.
struct ALScriptStudioDoc::Check
{
    // What the analyzer said of the text at analysisVersion; when the
    // next check is due, or zero; the version last asked about.
    ALScriptProblems analysis;
    // LSL's, as the analyzer said them, before the lints as chosen:
    // what a change of the lints filters again.
    ALScriptProblems unfiltered;
    U32              analysisVersion  = 0;
    U32              requestedVersion = 0;
    F64              analysisDue      = 0.0;
    // When the version last asked about was asked.
    F64              askedAt          = 0.0;
    std::string      definitionsError;
    // How many expansions were taken, which is how an answer about one
    // names it; the version one has been asked for, or none -- not a
    // zero, which an empty text's version is: the preprocessor answers
    // on the main thread a moment later, and one text is expanded once
    // however many questions wait on it.
    U32                expansions = 0;
    std::optional<U32> expanding;
    // The version the questions waiting were last asked about, while
    // an expansion of an older one was on its way: one expansion at a
    // time, and the next for the latest text, not one a key.
    std::optional<U32> wanted;
    // What the analyzers last said of a word -- for the tip under the
    // mouse or the inspector at the caret, which ask the same question
    // -- by the text's version, the expansion it was read through (none
    // for the text as it stands) and where the word starts: the other
    // asking of it is answered from here.
    struct Hovered
    {
        U32                      version   = 0;
        U32                      expansion = 0;
        ALTextPos                word;
        ALScriptAnalysis::Result said;
    };
    std::optional<Hovered> hovered;
    // Whether the text, at the version kept with it, has no default
    // state (ALScriptStudioChecking::lslFragment): asked on every
    // caret move and every frame Weights is shown, and a walk of the
    // whole text to answer.
    mutable std::optional<std::pair<U32, bool>> fragment;
    // A question held until the expansion it asks about comes. A
    // question of a kind replaces the one of that kind still waiting:
    // a second hover is a hover of somewhere else, and only the last is
    // wanted.
    struct Waiting
    {
        ALScriptAnalysis::Kind kind = ALScriptAnalysis::Kind::Check;
        ALTextPos              at;
        // Where a stretch chosen from `at` ends: the refactors'.
        ALTextPos              to;
    };
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
    std::optional<FixPick> fixAllAfterCheck;
    // A Fix All previewed: what it picked, over the text at that version,
    // made by Apply while the text is still that.
    struct FixAllPreview
    {
        FixPick pick;
        U32     version = 0;
    };
    std::optional<FixAllPreview> fixAllPreviewed;
};

// A Script Studio window's checking, as the tab's part `doc.check` keeps
// it: a check due a moment after the last keystroke and sent from the
// frame; the analyzers asked -- about what the compiler would see, the
// expansion asked for first where the preprocessor runs, the question
// waiting on it -- and every answer read back through the expansion to the
// source, or dropped where it is about a text or an expansion no longer
// read so. A check's answer is made into the tab's problems, outline,
// colours and hints; names nothing gives offered an import from a module
// in reach; what a comment says is not wanted dropped; the preprocessor's
// words explained where their transform is off. Fixes are made from here,
// one or all, over the text they were made in.
class ALScriptStudioChecking
{
public:
    typedef ALScriptStudioDoc      Doc;
    typedef ALScriptStudioDoc::FixPick FixPick;

    // What checking reads of the viewer's that is no window's -- the
    // preprocessor, its settings, its expansions, the .luaurc, includes
    // and modules -- is the viewer's to say (ALScriptStudioViewer).

    // What checking asks of the window itself, beyond what it is given.
    class Window
    {
    public:
        // What the window's settings add to every question: colours, hints.
        virtual void askingOptions(ALScriptAnalysis::Request& request) const = 0;
        // An answer that is not checking's -- a name's references, what is
        // at the caret, a weighing -- handed on, at the source's place.
        virtual void answeredElsewhere(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at) = 0;
        // The problems asked for with the next frame made now: what is
        // about to read them -- the fixes -- reads what the last answer
        // said. And the outline, which a check's answer is shown by too.
        virtual void settleProblems(Doc& doc) = 0;
        virtual void showOutline(Doc& doc)    = 0;
        // The editor of the view in front, which a fix is made in; and the
        // question of whether to make many fixes at once, `yes` where they
        // are to be.
        virtual ALCodeEditor& editorInFront(Doc& doc)                              = 0;
        // Or to see them first, `preview` where they are.
        virtual void          confirmFixAll(const LLSD& args, std::function<void()> yes, std::function<void()> preview) = 0;
        // Two texts side by side in a tab's place, each under its title.
        virtual void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                             const std::string& right_title) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioChecking(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, ALScriptStudioSaves& saves, ALScriptStudioWeighing& weighing, Window& window);

    // --- the analyzers ------------------------------------------------------------

    // Whether the preprocessor makes what a tab sends, and so what the
    // analyzers read; a question of it about a tab, without the source
    // where only where the script is matters; and what an include is
    // called.
    bool                          preprocessed(const Doc& doc) const;
    // And why (ALPreprocessor::wanted): No for a notecard, or one not yet
    // loaded.
    ALPreprocessor::Wanted        preprocessedWhy(const Doc& doc) const;
    ALScriptPreprocessor::Request preprocessRequest(const Doc& doc, bool with_source = true) const;
    std::string                   includeName(const Doc& doc, const std::string& path) const;
    // An LSL file on disk with no default state: an include's functions
    // and globals, which the parser takes for no script at all until a
    // state is put after them, and whose declarations are for others.
    bool lslFragment(const Doc& doc) const;
    // A check due a moment after the last keystroke, or at once. A
    // notecard's is its own (checkNotecard), which the analyzers have no
    // part in.
    void schedule(Doc& doc, bool now = false);
    // The analyzers asked about a place of a tab, or a stretch from `at`
    // to `to`: of what the compiler would see, the expansion asked for
    // first where it is not in hand.
    void ask(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to);
    // Each frame, at `now`: the checks that are due sent -- the tab in
    // front's, and of the others the one due longest -- and, where the
    // preprocessor's settings changed a moment ago, every tab expanded and
    // checked again, the editors taught its words where they changed.
    void pump(F64 now);
    void settingsChanged(bool words, F64 now);
    // The lints chosen again: an LSL tab's last check filtered again by
    // them, as they are applied after the analyzer rather than in it; an
    // SLua tab's, or one without a check of its text as it stands,
    // checked again.
    void relint(Doc& doc);
    // The compiler's and the run's problems moved along with an edit; and
    // the outline, until the next check says it again.
    void slideProblems(Doc& doc, const ALTextDocument::Edit& edit);
    void slideOutline(Doc& doc, const ALTextDocument::Edit& edit);

    // --- fixes ------------------------------------------------------------------------

    // A fix made, as one step to undo, and the script checked again at
    // once; refused where the text has moved on since `version`, the one
    // it was made over, whose places it is in.
    bool applyFix(Doc& doc, const ALScriptFix& fix, U32 version);
    // The preferred fix of every problem picked, made as one step, once
    // asked; and made, the asking done. True where anything was made.
    void askFixAll(Doc& doc, const FixPick& pick);
    bool fixAll(Doc& doc, const FixPick& pick);
    // The text as the fixes would make it, beside the text as it is, and
    // Apply offered over the tab (the action apply_fixes); and made by it,
    // where the text is still what was previewed.
    void previewFixAll(Doc& doc, const FixPick& pick);
    bool applyPreviewed(Doc& doc);
    // The fixes of the problems on a line, as the editor lists them, each
    // with the value that finds it again; none where the text has moved on
    // since the check they were made in. And the problem a value is.
    void              fixesOn(Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out);
    const Doc::Shown* shownOf(const LLSD& value) const;

    // A check's problems read back through the expansion it was made of to
    // the source: each in the script, in an include by its path, in a
    // module the script requires by that module's own map, or in code the
    // preprocessor made; and what an include declares and the script does
    // not use dropped. Fixes are kept only for the script's own.
    static void mapBack(std::vector<ALScriptProblem>& problems, const ALSourceMap& map,
                        const std::vector<std::pair<std::string, ALSourceMap>>& module_maps);

private:
    // What the analyzers said of a word, on the tip under the mouse.
    void showHover(Doc& doc, const ALScriptAnalysis::Result& said, const ALTextPos& at);
    // A check asked of a tab now.
    void askCheck(Doc& doc, F64 now);
    // A notecard's, made here and now: what scripts will not read of it --
    // none of it where it carries items, and past so many bytes of a line
    // -- and, read as JSON, its keys as its outline.
    void checkNotecard(Doc& doc);
    // The expansion asked for, a question waiting on it; and taken.
    void expandFor(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to);
    void expandedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result);
    // `expansion` is the expansion the question was asked over, or zero
    // for the text as it stands.
    void answered(const ALScriptAnalysis::Result& result, U32 expansion);
    void actionsAnswered(Doc& doc, const ALScriptAnalysis::Result& result, U32 expansion);
    // A check's answer, by what came back: the problems, the outline,
    // what every name is and what goes beside the text, all through the
    // expansion to the source; then what is made of the problems, and
    // who waited on the check.
    void analysed(const ALScriptAnalysis::Result& result);
    void takeProblems(Doc& doc, const ALScriptAnalysis::Result& result);
    // The problems taken as the scripter chose the lints, and those of a
    // fragment's own lines alone.
    void filterProblems(Doc& doc);
    // How many lines of what was checked are a fragment's own, before the
    // state put after it.
    S32  fragmentLines(const Doc& doc, U32 version) const;
    void takeColours(Doc& doc, const ALScriptAnalysis::Result& result, const ALSourceMap* map);
    void mapProblems(Doc& doc);
    void mapOutline(Doc& doc);
    void checked(Doc& doc);
    // What is made of the problems, and they shown.
    void showProblems(Doc& doc);
    // What is made of the problems: imports offered, what a comment says
    // is not wanted dropped, the preprocessor's words and a require
    // explained.
    void offerImports(Doc& doc);
    void noLint(Doc& doc);
    void explainTransformWords(Doc& doc);
    void explainRequires(Doc& doc);
    // Each "-- LSL:" comment the converter left in an SLua script, a note
    // to see to, with its lint's fix where there is one
    // (ALLSLToSLua::notesIn).
    void notesAsProblems(Doc& doc);

    ALScriptStudioServices& mServices;
    ALScriptStudioAnalysis& mAnalysis;
    ALScriptStudioSaves&    mSaves;
    ALScriptStudioWeighing& mWeighing;
    Window&                 mWindow;
    // When the preprocessor's settings are next taken, or zero; and
    // whether its words changed with them.
    F64                     mPreprocessorDue   = 0.0;
    bool                    mPreprocessorWords = false;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

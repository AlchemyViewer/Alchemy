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

class ALScriptStudioServices;

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

    // What checking reads of the viewer's that is no window's: whether the
    // preprocessor runs and each of its transforms is on, its expansion of
    // a script and a script's `.luaurc`, an include or a module looked up
    // by name, and the modules index. The viewer sets them once; a test
    // sets its own.
    struct Sources
    {
        std::function<bool()>                          preprocessing;
        std::function<bool(ALPreprocessor::Transform)> transformOn;
        std::function<void(ALScriptPreprocessor::Request, std::function<void(const ALPreprocessor::Result&)>)> expand;
        std::function<bool(const ALScriptPreprocessor::Request&, ALLuauConfig&, const ALLuauConfig*)>          configOf;
        std::function<void(const ALScriptPreprocessor::Request&, std::function<void()>)>                        fetchConfig;
        std::function<ALPreprocessor::Found(const ALScriptPreprocessor::Request&, const ALPreprocessor::Ask&, ALPreprocessor::Include&)>
            lookUp;
        std::function<std::vector<ALScriptModules::Module>(const ALScriptPreprocessor::Request&,
                                                           std::function<std::vector<ALScriptModules::Open>()>,
                                                           const std::vector<std::string>&)>
                                                                                          modules;
        std::function<void(const ALScriptPreprocessor::Request&, std::function<void()>)> fetchNearby;
    };
    static Sources& sources();

    // What checking asks of the window beyond its services.
    class Window
    {
    public:
        // The analyzers asked a question, answered later; and what the
        // window's settings add to every question: colours, hints.
        virtual void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) = 0;
        virtual void askingOptions(ALScriptAnalysis::Request& request) const = 0;
        // An answer that is not checking's -- a name's references, what is
        // at the caret, a weighing -- handed on, at the source's place.
        virtual void answeredElsewhere(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at) = 0;
        // What a check's answer is shown by: the problems, the outline,
        // and the weights the front tab's check weighed along with it; and
        // the targets a weighing is asked for.
        virtual void                                refreshProblems(Doc& doc)                                     = 0;
        virtual void                                showOutline(Doc& doc)                                         = 0;
        virtual void                                weighed(Doc& doc, const ALScriptAnalysis::Result& result)     = 0;
        virtual std::vector<ALScriptWeight::Target> weightTargets(const Doc& doc)                                 = 0;
        // A save that waited on the check, done; a run of the preprocessor
        // for a save, where the settings changed.
        virtual void save(Doc& doc)             = 0;
        virtual void preprocessForSave(Doc& doc) = 0;
        // The editor of the view in front, which a fix is made in; and the
        // question of whether to make many fixes at once, `yes` where they
        // are to be.
        virtual ALCodeEditor& editorInFront(Doc& doc)                              = 0;
        virtual void          confirmFixAll(const LLSD& args, std::function<void()> yes) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioChecking(ALScriptStudioServices& services, Window& window);

    // --- the analyzers ------------------------------------------------------------

    // Whether the preprocessor makes what a tab sends, and so what the
    // analyzers read; a question of it about a tab, without the source
    // where only where the script is matters; and what an include is
    // called.
    bool                          preprocessed(const Doc& doc) const;
    ALScriptPreprocessor::Request preprocessRequest(const Doc& doc, bool with_source = true) const;
    std::string                   includeName(const Doc& doc, const std::string& path) const;
    // An LSL file on disk with no default state: an include's functions
    // and globals, which the parser takes for no script at all until a
    // state is put after them, and whose declarations are for others.
    bool lslFragment(const Doc& doc) const;
    // A check due a moment after the last keystroke, or at once.
    void schedule(Doc& doc, bool now = false);
    // The analyzers asked about a place of a tab, or a stretch from `at`
    // to `to`: of what the compiler would see, the expansion asked for
    // first where it is not in hand.
    void ask(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to);
    // Each frame, at `now`: the checks that are due sent; and, where the
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
    // The fixes of the problems on a line, as the editor lists them, each
    // with the value that finds it again; none where the text has moved on
    // since the check they were made in. And the problem a value is.
    void              fixesOn(const Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out) const;
    const Doc::Shown* shownOf(const LLSD& value) const;

private:
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

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // When the preprocessor's settings are next taken, or zero; and
    // whether its words changed with them.
    F64                     mPreprocessorDue   = 0.0;
    bool                    mPreprocessorWords = false;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

/**
 * @file alscriptstudiosaving.h
 * @brief Saving and compiling in Script Studio: the checks before a save, the preprocessor's run, the upload and the compiler's answer.
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

#include "alpreprocessor.h"
#include "alscriptstudiodoc.h"
#include "alscriptweight.h"
#include "alscriptworkspace.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's saves, from the moment one is asked for to the
// compiler's answer. Where a save stands and where it goes next is the
// tab's own ALScriptSaveFlow (`doc.save`); this does what that says --
// tidies, checks, preprocesses, sends, reports, closes -- and tells it what
// came of each. What it asks of the viewer, the preprocessor and the upload
// among it, goes through the window, so that whole saves run in a test.
class ALScriptStudioSaving
{
public:
    typedef ALScriptStudioDoc Doc;

    // How saves go, as the settings say.
    struct Options
    {
        // Tidied before anything is sent: the safe fixes made, formatted,
        // the blanks at the lines' ends taken away.
        bool        fix    = false;
        bool        format = false;
        bool        trim   = false;
        // Held on the analyzers' errors, rather than sent with them said.
        bool        holdOnErrors = false;
        // An LSL expansion sent compressed: what one too large is told
        // would shrink it.
        bool        compress = false;
        // The viewer's channel and version, which the envelope says.
        std::string program;
    };

    // What saving asks of the window beyond its services.
    class Window
    {
    public:
        virtual Options saveOptions() const = 0;

        // --- tidying and checking ------------------------------------------------------

        // Tidied before a save, a step each to undo: the safe fixes,
        // formatting, the blanks at the lines' ends, where each is asked.
        virtual void tidy(Doc& doc, bool fix, bool format, bool trim) = 0;
        // What the tab shows held as a preview no longer: it is kept.
        virtual void holdPreview(Doc& doc) = 0;
        // The analyzers asked about the text, now or after a pause.
        virtual void scheduleAnalysis(Doc& doc, bool now) = 0;
        // The Problems tab said again for a tab, with the next frame;
        // brought into sight; and its first error chosen, of the checkers'
        // alone or of all, over what the tab says by then.
        virtual void refreshProblems(Doc& doc)            = 0;
        virtual void showProblems()                       = 0;
        virtual void selectFirstError(bool checkers_only) = 0;

        // --- the preprocessor ----------------------------------------------------------

        // Whether it runs over a tab.
        virtual bool preprocessed(const Doc& doc) const = 0;
        // A run over the text as it stands, fetching its includes, and
        // what it made once it has.
        virtual void runPreprocessor(const Doc& doc, std::function<void(const ALPreprocessor::Result&)> answer) = 0;
        // What it made, in the tab's expanded view.
        virtual void showExpanded(Doc& doc, const std::string& text) = 0;
        // The map what runs is read back by known, where there was none:
        // what waited on it -- a run-time error, a line to go to -- placed.
        virtual void runningKnown(Doc& doc) = 0;

        // --- weighing ------------------------------------------------------------------

        // The target a tab is weighed for; none for one that is not.
        virtual std::optional<ALScriptWeight::Target> weightTarget(const Doc& doc) const = 0;
        // Weighed as it stands, and as a run of the preprocessor made it to
        // be sent.
        virtual void weigh(Doc& doc)     = 0;
        virtual void weighSent(Doc& doc) = 0;
        // What it weighs kept as what it weighs saved.
        virtual void keepSavedWeights(Doc& doc) = 0;

        // --- sending -------------------------------------------------------------------

        // A script's text sent to be saved and compiled; a notecard's, with
        // its items. False, with why, where nothing was sent.
        // Each as the request `options.sender` or `request` names, which its
        // answer carries; a new one from newRequest().
        virtual bool send(const Doc& doc, const std::string& text, const ALScriptSaveOptions& options, std::string& error) = 0;
        virtual bool sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& items,
                                  std::string& error, U64 request)                                                                = 0;
        virtual U64  newRequest()                                                                                                 = 0;
        // A file's text written back where it came from.
        virtual void saveFile(Doc& doc) = 0;
        // The asset the world holds now for the tab's item: the region asked
        // for an object's, which keeps no word of a co-owner's save; the
        // inventory's as it stands. Nothing where it cannot be told.
        virtual void worldAsset(const Doc& doc, std::function<void(std::optional<LLUUID>)> told) = 0;

        // --- saves from elsewhere ------------------------------------------------------

        // What another save put up, taken in as if the tab had loaded so: a
        // script's text as it went up; a notecard fetched afresh, since its
        // text does not carry the items it holds. And the text carried in
        // (Doc::carriedText) put in as one step to undo.
        virtual void takeLoaded(Doc& doc, const std::string& text) = 0;
        virtual void takeCarried(Doc& doc)                        = 0;

        // --- the tab and the window ----------------------------------------------------

        // A kept text loaded under its item, what it holds carried over.
        virtual void reattach(Doc& doc) = 0;
        // The notice over the editor, the toolbar, the strip under the
        // editor and the tabs said again.
        virtual void refreshNotice()          = 0;
        virtual void refreshToolbar()         = 0;
        virtual void refreshTrailer(Doc& doc) = 0;
        virtual void fillTabs()               = 0;
        // Written to the recovery store as it stands: saved, what is kept
        // of it against a crash forgotten.
        virtual void keepForRecovery(Doc& doc) = 0;
        // The editor outside told of what was saved here, and of what the
        // compiler made of it.
        virtual void syncExternal(Doc& doc)                                                  = 0;
        virtual void logExternal(Doc& doc, const ALScriptCompileResult& result) = 0;

        // --- closing -------------------------------------------------------------------

        // Whether the viewer is quitting on this window's answer.
        virtual bool quittingOnUs() const = 0;
        // A close waiting on a save that stopped, waited on no longer.
        virtual void stopClosing() = 0;
        // A tab a save has done with, let go of as it stands: saved to be
        // closed, or the tab a copy saved was made of.
        virtual void letGoOf(Doc& doc) = 0;
        // A window's close that waits on its tabs' saves, gone on with;
        // nothing where the window is not closing.
        virtual void continueClosing() = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioSaving(ALScriptStudioServices& services, Window& window);

    // --- saving ------------------------------------------------------------------------

    // A tab saved: out of reach said, queued behind one on its way, or
    // tidied, checked, preprocessed and sent as its flow says.
    void save(Doc& doc);
    // A save the author asked for: past the one check that stopped the last
    // save of the same text, and then a save.
    void saveAsked(Doc& doc);
    // Every tab with anything unsaved.
    void saveAll();

    // A script or notecard saved elsewhere -- VS Code, another editor, a
    // queue -- which a tab holds: taken in where nothing was typed there;
    // where something was, the author asked whose to keep, Take Theirs,
    // Keep Mine or Compare -- but for a save of the text the tab last had,
    // which changes nothing it holds (ALScriptWorkspace::onSaved). And the
    // author's answer.
    void savedElsewhere(const ALScriptSaved& saved);
    void takeSaved(Doc& doc);
    void keepSaved(Doc& doc);
    // A tab saved to be closed once its save comes back: where the save
    // cannot begin, the tab is left as it was, and a close waiting on it
    // stops -- rather than closing at whatever save comes next.
    void saveToClose(const std::string& id);
    // A save that did not go through -- refused over errors, failed, or
    // compiled with errors: a close waiting on it waits no longer, and a
    // window closing stops, the tab left for the author to look at.
    void stopped(Doc& doc);

    // --- the preprocessor --------------------------------------------------------------

    // A run of the preprocessor over the text as it stands, fetching its
    // includes; none where one is on its way already. A save waiting on it
    // goes on when it answers (ALScriptSaveFlow::preprocessed).
    void preprocess(Doc& doc);

    // --- answers -----------------------------------------------------------------------

    // The compiler's answer, to a save of a tab's or a recompile from the
    // explorer; a copy saved closes the tab it was made of.
    void compiled(const ALScriptCompileResult& result);
    // A tab's weight known: over its target's limit, as a save sent it,
    // said once.
    void warnOverWeight(Doc& doc);

private:
    void preprocessedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result);
    // A wrapped script's first run as it loaded, held up to the compiled
    // half it came with: where the source could not have made that, the
    // half is offered in the notice (Doc::compiledDiffers).
    void compareCompiled(Doc& doc, U32 version, const ALPreprocessor::Result& result);
    // What a run made, uploaded: in the envelope with the source as
    // written, or as written alone where the run was switched off.
    void sendPreprocessed(Doc& doc, const Doc::Expanded& sent);
    // The text sent to be saved and compiled, with the map it was expanded
    // through where it was.
    void upload(Doc& doc, const std::string& text, const ALSourceMap* map = nullptr);
    // What a save sends weighed as it goes, not before it: over its
    // target's limit it is said, once the weight is known, and goes up
    // anyway -- the numbers assume how the region compiles, and are not
    // its word.
    void weighForSave(Doc& doc);
    void reportOverWeight(const Doc& doc, const ALScriptWeight& weight);
    void compiledHere(const ALScriptCompileResult& result);
    // The save asked for while the last was on its way, made now where
    // anything is still unsaved; true where one is under way again.
    bool sendQueuedSave(Doc& doc);
    // The errors the analyzers found in the text as they last checked it,
    // and the preprocessor in the expansion they read: what holds a save,
    // where saves are held on them.
    static S32 checkerErrors(const Doc& doc);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Held while this is, for what answers later to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

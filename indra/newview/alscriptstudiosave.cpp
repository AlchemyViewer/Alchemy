/**
 * @file alscriptstudiosave.cpp
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

#include "llviewerprecompiledheaders.h"
#include "alfloaterscriptstudio.h"
#include "alcodeeditor.h"
#include "alfilewrite.h"
#include "alnotecarditems.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptweightspane.h"
#include "alemptystate.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alobjectproperties.h"
#include "alpanelist.h"
#include "llsdutil.h"
#include "alscopebar.h"
#include "alscriptfixes.h"
#include "alscriptformatter.h"
#include "alscriptkeymap.h"
#include "alscriptmessages.h"
#include "altabstrip.h"
#include "altextsearch.h"
#include "alvimkeymap.h"
#include "llagent.h"
#include "llappviewer.h"
#include "lldate.h"
#include "lltimer.h"
#include "llsyntaxid.h"
#include "llversioninfo.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "alsaid.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llfilepicker.h"
#include "llfiltereditor.h"
#include "llfloaterperms.h"
#include "llexperiencecache.h"
#include "llfloaterreg.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "llselectmgr.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltooldraganddrop.h"
#include "lltrans.h"
#include "llexternaleditor.h"
#include "lllogchat.h"
#include "llscripteditorws.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewerassettype.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llweb.h"
#include "llviewermenufile.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "rlvhandler.h"
#include "rlvlocks.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void ALFloaterScriptStudio::preprocess(Doc& doc)
{
    if (doc.preprocessing)
    {
        // One on its way already -- the one a load starts, which fetches
        // an object's includes and can take a while: whatever waits on a
        // run, a save among them, waits on that one.
        return;
    }
    doc.preprocessing = true;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    setStatus(getString("Preprocessing", args));
    const LLHandle<LLFloater> handle  = getHandle();
    const std::string         id      = doc.id;
    const U32                 version = doc.editor->document().version();
    ALScriptPreprocessor::instance().run(preprocessRequest(doc), [handle, id, version](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->preprocessedAnswer(id, version, result);
        }
    });
}

void ALFloaterScriptStudio::preprocessedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc          = *mDocs[index];
    doc.preprocessing = false;
    // What a save would upload, shown; the analyzers' own expansion is
    // made again now that every include is in, without the optimizer.
    doc.uploaded.valid    = true;
    doc.uploaded.disabled = result.disabled;
    doc.uploaded.version  = version;
    doc.uploaded.text     = result.text;
    doc.uploaded.map      = result.map;
    doc.uploaded.problems = result.problems;
    doc.uploaded.codeBefore = result.codeBefore;
    doc.uploaded.codeAfter  = result.codeAfter;
    doc.expanded.valid    = false;
    showExpanded(doc, result.text);
    refreshProblems(doc);
    // And what it comes to for a save waiting on it.
    ALScriptSaveFlow::Run run;
    run.asked                             = version;
    run.now                               = doc.editor->document().version();
    run.errors                            = result.hasErrors();
    run.pending                           = !result.pending.empty();
    const ALScriptSaveFlow::Landed landed = doc.save.preprocessed(run);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (run.now != run.asked)
    {
        // The text moved on while the includes came: analysed again, and
        // saved again from the start if that was the point.
        doc.expanded.valid = false;
        scheduleAnalysis(doc, true);
        if (landed == ALScriptSaveFlow::Landed::MovedOn)
        {
            save(doc);
        }
        return;
    }
    scheduleAnalysis(doc, true);
    // The includes that never came, by the names the script gave them.
    std::string pending;
    for (const std::string& name : result.pending)
    {
        pending += (pending.empty() ? "" : ", ") + name;
    }
    args["[FILES]"] = pending;
    switch (landed)
    {
        case ALScriptSaveFlow::Landed::NotForSave:
            if (!result.pending.empty())
            {
                report(counted("PreprocessedPending", static_cast<S32>(result.pending.size()), args), true, &doc);
            }
            else
            {
                // Said where the status goes, not added to the output: the
                // view is expanded every time it is shown, and every open
                // script is when a file one of them may include is saved.
                setStatus(getString("Preprocessed", args));
                // Weighed as a save would send it; not with an include still
                // to come, which a save would wait for.
                weighSent(doc);
            }
            return;
        case ALScriptSaveFlow::Landed::StoppedByErrors:
        {
            S32 errors = 0;
            for (const ALScriptProblem& problem : result.problems)
            {
                errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
            }
            report(counted("PreprocessErrors", errors, args), true, &doc, { "save_anyway" });
            saveStopped(doc);
            showBottom("problems_tab");
            return;
        }
        case ALScriptSaveFlow::Landed::StoppedByPending:
            report(counted("PreprocessPendingSave", static_cast<S32>(result.pending.size()), args), true, &doc, { "save_anyway" });
            saveStopped(doc);
            return;
        case ALScriptSaveFlow::Landed::MovedOn:
        case ALScriptSaveFlow::Landed::Send:
            break;
    }
    // Weighed as it goes.
    weighForSave(doc);
    sendPreprocessed(doc, doc.uploaded);
}

void ALFloaterScriptStudio::sendPreprocessed(Doc& doc, const Doc::Expanded& sent)
{
    if (sent.disabled)
    {
        // `//fspreprocessor off`: the text goes up as it is, as
        // Firestorm sends it.
        upload(doc, doc.editor->text());
        return;
    }
    // In the envelope, with the source as written, so Firestorm opens
    // what we save; the lines that say who wrote it and when are ours.
    ALScriptEnvelope envelope;
    if (doc.envelope)
    {
        envelope = *doc.envelope;
    }
    envelope.lua           = doc.language.lua;
    envelope.source        = doc.editor->text();
    envelope.expanded      = sent.text;
    envelope.compileTarget = doc.language.compileTarget;
    envelope.programVersion = LLVersionInfo::instance().getChannelAndVersion();
    envelope.lastCompiled   = LLDate::now().asString();
    doc.envelope            = envelope;
    upload(doc, envelope.wrap(), &sent.map);
}

void ALFloaterScriptStudio::weighForSave(Doc& doc)
{
    if (!weightTarget(doc))
    {
        return;
    }
    doc.save.setWarnWeightFor(doc.editor->document().version());
    if (preprocessed(doc))
    {
        // What the run made to be sent, which is what goes.
        weighSent(doc);
    }
    else if (doc.weight && doc.weightVersion == doc.save.warnWeightFor())
    {
        warnOverWeight(doc);
    }
    else
    {
        weigh(doc);
    }
}

void ALFloaterScriptStudio::warnOverWeight(Doc& doc)
{
    if (doc.save.warnWeightFor() < 0 || !doc.weight || doc.weightVersion != doc.save.warnWeightFor())
    {
        return;
    }
    doc.save.setWarnWeightFor(-1);
    // Over the limit of a target counted as exact, as nearly as the studio
    // can tell: Mono's is an estimate at the best of times, and its
    // Problems row says so.
    const ALScriptWeight& weight = *doc.weight;
    if (doc.weightExact && !weight.estimate && weight.total > weight.limit)
    {
        reportOverWeight(doc, weight);
    }
}

void ALFloaterScriptStudio::save(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    // Where it cannot go -- its object out of sight, the item gone, the
    // connection lost -- said, with the notice back in sight to offer a
    // copy or a file.
    const bool out_of_reach = doc.orphan == Doc::Orphan::Away || doc.orphan == Doc::Orphan::Removed || doc.orphan == Doc::Orphan::Offline ||
                              doc.orphan == Doc::Orphan::Locked || doc.orphan == Doc::Orphan::Unloaded;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    switch (doc.save.ask(out_of_reach, doc.detached))
    {
        case ALScriptSaveFlow::Start::Queued:
            return;
        case ALScriptSaveFlow::Start::OutOfReach:
            setStatus(getString(doc.orphan == Doc::Orphan::Away       ? "SaveBlockedAway"
                                : doc.orphan == Doc::Orphan::Removed  ? "SaveBlockedRemoved"
                                : doc.orphan == Doc::Orphan::Locked   ? "SaveBlockedLocked"
                                : doc.orphan == Doc::Orphan::Unloaded ? "SaveBlockedUnloaded"
                                                                      : "SaveBlockedOffline",
                                args),
                      true);
            doc.noticeDismissed = false;
            saveStopped(doc);
            if (&doc == active())
            {
                refreshNotice();
            }
            return;
        case ALScriptSaveFlow::Start::Detached:
            // Its item in reach, and not loaded under it yet -- a try that
            // failed waits its turn: loaded now, what it holds carried over,
            // so that it is saved as what the item is once asked again.
            setStatus(getString("SaveWaitsForLoad", args), true);
            doc.reattachTries = 0;
            reattach(doc);
            saveStopped(doc);
            return;
        case ALScriptSaveFlow::Start::Go:
            break;
    }
    // Saved, a preview is held.
    holdPreview(doc);
    // Tidied as the scripter asked before anything is sent or checked: a
    // step each to undo, and nothing where the text is tidy already. The
    // safe fixes first, while the text is still the one they were made
    // for.
    if (!doc.notecard)
    {
        if (gSavedSettings.getBOOL("ALScriptFixOnSave") && doc.analysisVersion == doc.editor->document().version() && doc.save.fixOnce())
        {
            fixAll(doc, FixPick{ std::string(), true });
        }
        if (gSavedSettings.getBOOL("ALScriptFormatOnSave"))
        {
            format(doc, false);
        }
        if (gSavedSettings.getBOOL("ALScriptTrimOnSave"))
        {
            trimTrailing(doc);
        }
    }
    static LLCachedControl<bool> hold(gSavedSettings, "ALScriptStudioPreflight", false);
    ALScriptSaveFlow::Tab tab;
    tab.version          = doc.editor->document().version();
    tab.file             = !doc.file.empty();
    tab.notecard         = doc.notecard;
    tab.preprocessed     = !tab.file && !tab.notecard && preprocessed(doc);
    tab.preprocessorBusy = doc.preprocessing;
    tab.holdOnErrors     = hold;
    tab.checked          = doc.analysisVersion == tab.version;
    tab.checkerErrors    = checkerErrors(doc);
    const ALScriptSaveFlow::Route route = doc.save.route(tab);
    switch (route)
    {
        case ALScriptSaveFlow::Route::File:
            saveFile(doc);
            return;
        case ALScriptSaveFlow::Route::Notecard:
        {
            // What goes back: the text with the items it still stands
            // somewhere, numbered afresh, and those items alone, as the
            // legacy notecard prunes what an edit took out.
            std::string                             text;
            std::vector<LLPointer<LLInventoryItem>> items;
            if (doc.items)
            {
                doc.items->forSave(text, items);
            }
            else
            {
                text = doc.editor->text();
            }
            std::string                 error;
            const ALTextUndo::SavePoint at = doc.editor->savePoint();
            if (!ALScriptWorkspace::instance().saveNotecard(doc.ref, text, items, nullptr, error))
            {
                LLStringUtil::format_map_t failed;
                failed["[NAME]"]  = doc.name;
                failed["[ERROR]"] = error;
                report(getString("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
                saveStopped(doc);
                return;
            }
            std::vector<LLUUID> sent;
            for (const LLPointer<LLInventoryItem>& each : items)
            {
                sent.push_back(each->getUUID());
            }
            doc.save.sent(at, std::nullopt, std::move(sent));
            setStatus(getString("Saving", args));
            refreshToolbar();
            return;
        }
        case ALScriptSaveFlow::Route::Check:
            // Checked first; the save follows the answer.
            scheduleAnalysis(doc, true);
            setStatus(getString("Preflight", args));
            return;
        case ALScriptSaveFlow::Route::StoppedByAnalyzers:
            // Said as what stopped it, and only that let past by asking
            // again; the first of the checkers' errors in sight.
            report(counted("PreflightErrors", tab.checkerErrors, args), true, &doc, { "save_anyway" });
            saveStopped(doc);
            showBottom("problems_tab");
            selectFirstError(true);
            return;
        case ALScriptSaveFlow::Route::Preprocess:
        case ALScriptSaveFlow::Route::JoinPreprocessor:
        case ALScriptSaveFlow::Route::Send:
            break;
    }
    // Not held for the analyzers, who are not the region's compiler: what
    // the check of this text found said, where it is in.
    if (!hold && tab.checked)
    {
        const S32 errors = static_cast<S32>(std::count_if(doc.analysis.begin(), doc.analysis.end(),
                                                          [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
        if (errors > 0)
        {
            report(counted("SentWithErrors", errors, args), true, &doc);
        }
    }
    if (route == ALScriptSaveFlow::Route::Preprocess)
    {
        // Expanded first, with its includes fetched; the upload follows.
        preprocess(doc);
        return;
    }
    if (route == ALScriptSaveFlow::Route::JoinPreprocessor)
    {
        return;
    }
    doc.uploaded.valid = false;
    weighForSave(doc);
    upload(doc, doc.editor->text());
}

S32 ALFloaterScriptStudio::checkerErrors(const Doc& doc) const
{
    // The analyzers' errors, and the preprocessor's in the expansion they
    // read.
    S32 errors = 0;
    for (const ALScriptProblem& problem : doc.analysis)
    {
        errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
    }
    if (doc.expanded.valid && doc.expanded.version == doc.analysisVersion)
    {
        for (const ALScriptProblem& problem : doc.expanded.problems)
        {
            errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
        }
    }
    return errors;
}

void ALFloaterScriptStudio::upload(Doc& doc, const std::string& text, const ALSourceMap* map)
{
    // Longer than a script may be: refused here, saying by how much and
    // what would shrink it, rather than sent for the simulator to refuse.
    if (text.size() > ALScriptEnvelope::MAX_ASSET_BYTES)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = doc.name;
        args["[SIZE]"]  = std::to_string(text.size());
        args["[LIMIT]"] = std::to_string(ALScriptEnvelope::MAX_ASSET_BYTES);
        args["[OVER]"]  = std::to_string(text.size() - ALScriptEnvelope::MAX_ASSET_BYTES);
        // A preprocessed script goes as written and as expanded; LSL's
        // expansion may be compressed.
        const char* shrink = !map                                                                   ? "SaveTooLargePlain"
                             : !doc.language.lua && !gSavedSettings.getBOOL("ALScriptPreprocCompress") ? "SaveTooLargeCompress"
                                                                                                       : "SaveTooLargeWrapped";
        report(getString("SaveTooLarge", args) + " " + getString(shrink), true, &doc);
        doc.assetBytes = text.size();
        saveStopped(doc);
        if (&doc == active())
        {
            refreshTrailer(doc);
        }
        return;
    }
    // The script's own target and whether it runs, which the strip under
    // the editor says for the one in front: Save All saves the others by
    // theirs, not by the front one's.
    ALScriptWorkspace::SaveOptions options;
    options.compileTarget = doc.language.compileTarget;
    options.running       = doc.ref.inInventory() || doc.running != 0;
    // The experience picked here, or the one it runs under; not known yet,
    // the save asks the region first rather than send none.
    if (doc.experienceChosen || doc.experienceKnown)
    {
        options.experience = doc.experience;
    }
    std::string error;
    // Where the journal stands as the text goes, taken before anything
    // can be typed after it.
    const ALTextUndo::SavePoint at = doc.editor->savePoint();
    if (!ALScriptWorkspace::instance().save(doc.ref, text, options, nullptr, error))
    {
        LLStringUtil::format_map_t failed;
        failed["[NAME]"]  = doc.name;
        failed["[ERROR]"] = error;
        report(getString("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
        saveStopped(doc);
        return;
    }
    // With the map the compiler's lines are read back through.
    doc.save.sent(at, map ? std::optional<ALSourceMap>(*map) : std::nullopt, {});
    doc.problems.clear();
    refreshProblems(doc);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    setStatus(getString("Saving", args));
    refreshToolbar();
}

void ALFloaterScriptStudio::saveAll()
{
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->unsaved())
        {
            save(*doc);
        }
    }
}

void ALFloaterScriptStudio::compiled(const ALScriptWorkspace::CompileResult& result)
{
    // A copy of another tab, saved -- compiled or not, the text is up: the
    // tab it copied is safe in the inventory, and closes, once everything
    // below is done with the tabs as they stand.
    std::string copy_of;
    U32         copied_at = 0;
    if (const size_t index = indexOf(result.ref); index != NONE && result.error.empty())
    {
        copy_of.swap(mDocs[index]->copyOf);
        copied_at = mDocs[index]->copyOfVersion;
    }
    compiledHere(result);
    if (const size_t original = copy_of.empty() ? NONE : indexOf(copy_of); original != NONE)
    {
        Doc&                       from = *mDocs[original];
        LLStringUtil::format_map_t copied;
        copied["[NAME]"] = from.name;
        if (from.editor->document().version() != copied_at)
        {
            // Typed in since the copy was made: what was typed is not in the
            // copy, and the tab stays with it.
            report(getString("CopiedToKeptOpen", copied), false, &from);
            return;
        }
        report(getString("CopiedTo", copied));
        from.editor->resetDirty();
        letGoOf(original);
    }
}

void ALFloaterScriptStudio::compiledHere(const ALScriptWorkspace::CompileResult& result)
{
    const size_t index = indexOf(result.ref);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    // Whether it answers this tab's own save, rather than a recompile from
    // the explorer, and what that save comes to.
    ALScriptSaveFlow::Answer answer;
    answer.up                             = result.error.empty();
    answer.compiled                       = result.success;
    answer.quitting                       = quittingOnUs();
    const ALScriptSaveFlow::Landing landing = doc.save.compiled(answer);
    const bool                      ours    = landing.ours;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (!result.error.empty())
    {
        args["[ERROR]"] = result.error;
        // The region would not say what experience it runs under: the box
        // beside Save says so and offers what may be picked, which the
        // next save sets.
        report(getString(result.experienceUnknown ? "SaveExperienceUnknown" : "SaveFailed", args), true, &doc, { "retry", "copy", "export" });
        if (landing.stopped)
        {
            saveStopped(doc);
        }
        refreshToolbar();
        return;
    }
    // The text is the server's now, compiled or not -- when it was this
    // window that sent it, as it was sent, whatever was typed while the
    // answer came; a recompile from the explorer sent the asset as it
    // was, and what is typed here is still to be saved. The target it was
    // sent for is the region's from here.
    if (ours)
    {
        doc.editor->markSavedAt(doc.save.savePoint());
        doc.targetChosen = false;
        // The experience it was sent with is the one it runs under now:
        // picked here, or asked of the region by the save itself where
        // the box did not know it.
        if (doc.experienceChosen)
        {
            doc.experienceChosen = false;
            doc.experienceKnown  = true;
        }
        else if (result.experience && !doc.experienceKnown)
        {
            doc.experience      = *result.experience;
            doc.experienceKnown = true;
        }
        // Nothing picked waits on a save any more.
        fillTabs();
        keepSavedWeights(doc);
    }
    if (result.newAssetId.notNull())
    {
        doc.assetId = result.newAssetId;
    }
    // Saved: nothing of it to keep against a crash any more, or only what
    // was typed while the save was on its way.
    keepForRecovery(doc);
    if (result.notecard)
    {
        // The asset carries what was sent, and the server can copy it out.
        if (ours)
        {
            if (doc.items)
            {
                doc.items->saved(doc.save.sentItems());
            }
            doc.save.forgetSentItems();
        }
        report(getString("SavedNotecard", args), false, &doc);
        refreshToolbar();
        fillTabs();
        if (sendQueuedSave(doc))
        {
            return;
        }
        if (doc.save.closeAfter())
        {
            letGoOf(index);
            if (mClosingWindow)
            {
                continueClosing();
            }
        }
        return;
    }
    doc.problems.clear();
    const ALSourceMap* read = runningMap(doc);
    for (const ALScriptWorkspace::Diagnostic& said : result.diagnostics)
    {
        Doc::Compiled one;
        one.line      = said.line;
        one.column    = said.column;
        one.hasColumn = said.hasColumn;
        one.level     = said.level;
        one.message   = said.message;
        if (read)
        {
            const ALSourceMap::Loc loc = read->toSource(said.line, said.column);
            if (loc.found())
            {
                one.line   = loc.line;
                one.column = loc.column;
                if (loc.file > 0)
                {
                    one.file = read->files()[loc.file].path;
                }
            }
        }
        doc.problems.push_back(std::move(one));
    }
    if (result.success)
    {
        // A new script runs from here; what the old one said is past.
        doc.runtime.clear();
    }
    refreshProblems(doc);
    if (ours && doc.liveFile)
    {
        // The editor outside sees what was saved here, and what the
        // compiler made of it; its own save is not written back to it.
        if (!doc.save.external())
        {
            syncExternal(doc);
        }
        doc.save.endExternal();
        logExternal(doc, result);
    }

    if (result.success)
    {
        report(getString("Compiled", args), false, &doc);
    }
    else
    {
        report(counted("CompileFailed", static_cast<S32>(doc.problems.size()), args), true, &doc);
        // Saved, but not running: a close waiting on it leaves the tab
        // open with what the compiler said, rather than taking both away --
        // but for the viewer quitting, when the text is saved and that was
        // what was asked.
        if (landing.stopped)
        {
            saveStopped(doc);
        }
        // What the compiler said, in sight, as the checks before a save
        // show theirs: the first error chosen.
        if (ours && index == mActive && !doc.save.closeAfter())
        {
            showBottom("problems_tab");
            selectFirstError(false);
        }
    }
    if (index == mActive)
    {
        refreshToolbar();
    }
    fillTabs();
    if (sendQueuedSave(doc))
    {
        return;
    }
    if (doc.save.closeAfter())
    {
        letGoOf(index);
        if (mClosingWindow)
        {
            continueClosing();
        }
    }
}

// static
const ALSourceMap* ALFloaterScriptStudio::runningMap(const Doc& doc)
{
    // What the region compiled and runs is the expanded text that went up
    // from here, where one did; else -- a recompile, or a script loaded and
    // not saved since -- the text as it was last expanded, which is what
    // its envelope holds as far as this tab knows.
    if (doc.save.sentMap())
    {
        return &*doc.save.sentMap();
    }
    return doc.uploaded.valid && !doc.uploaded.disabled ? &doc.uploaded.map : nullptr;
}

bool ALFloaterScriptStudio::sendQueuedSave(Doc& doc)
{
    // Asked for while the last was on its way, and something is unsaved
    // still: sent now, and a close waiting on the save waits on this one.
    if (!doc.save.takeAgain() || !doc.unsaved())
    {
        return false;
    }
    save(doc);
    return doc.saveUnderway();
}

void ALFloaterScriptStudio::saveStopped(Doc& doc)
{
    doc.save.stopped();
    stopClosing();
}

void ALFloaterScriptStudio::saveToClose(const std::string& id)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    doc.save.setCloseAfter(true);
    save(doc);
    // Gone already -- a file, saved and let go of on the spot -- or on its
    // way: sent, or waiting on the preprocessor or a check.
    if (indexOf(id) == NONE || doc.saveUnderway())
    {
        return;
    }
    // It could not begin -- still loading, say: the tab stays for the
    // author, not set to close at whatever save comes next, and a close
    // waiting on it waits no longer.
    saveStopped(doc);
}

void ALFloaterScriptStudio::reportOverWeight(const Doc& doc, const ALScriptWeight& weight)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"]   = doc.name;
    args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
    args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
    args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
    report(getString("SaveOverWeight", args), true, &doc);
}

void ALFloaterScriptStudio::saveAsked(Doc& doc)
{
    // Asked for by the author: over the text the last save was stopped at,
    // past what stopped it.
    doc.save.letPast(doc.editor->document().version());
    save(doc);
}

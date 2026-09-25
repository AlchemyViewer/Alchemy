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

void ALFloaterScriptStudio::preprocess(Doc& doc, bool then_save)
{
    if (doc.preprocessing)
    {
        // One on its way already -- the one a load starts, which fetches
        // an object's includes and can take a while: the save waits on it
        // rather than going nowhere, which a close waiting on the save
        // would wait on for ever.
        doc.saveAfterPreprocess = doc.saveAfterPreprocess || then_save;
        return;
    }
    doc.preprocessing = true;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    setStatus(getString("Preprocessing", args));
    const LLHandle<LLFloater> handle  = getHandle();
    const std::string         id      = doc.id;
    const U32                 version = doc.editor->document().version();
    ALScriptPreprocessor::instance().run(preprocessRequest(doc), [handle, id, version, then_save](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->preprocessedAnswer(id, version, then_save, result);
        }
    });
}

void ALFloaterScriptStudio::preprocessedAnswer(const std::string& id, U32 version, bool then_save, const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc          = *mDocs[index];
    doc.preprocessing = false;
    then_save         = std::exchange(doc.saveAfterPreprocess, false) || then_save;
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
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (version != doc.editor->document().version())
    {
        // The text moved on while the includes came: analysed again, and
        // saved again from the start if that was the point.
        doc.expanded.valid = false;
        scheduleAnalysis(doc, true);
        if (then_save)
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
    if (!then_save)
    {
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
            // Weighed as a save would send it; not with an include still to
            // come, which a save would wait for.
            weighSent(doc);
        }
        return;
    }
    if (result.hasErrors() && !doc.letsPast(version, Doc::CheckPreprocessor))
    {
        S32 errors = 0;
        for (const ALScriptProblem& problem : result.problems)
        {
            errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
        }
        report(counted("PreprocessErrors", errors, args), true, &doc, { "save_anyway" });
        doc.stoppedBy(version, Doc::CheckPreprocessor);
        saveStopped(doc);
        showBottom("problems_tab");
        return;
    }
    // An include still on its way is one the upload would go without,
    // its line dropped from the text: the script as written is not what
    // would compile. Stopped, as an error stops it, unless asked again.
    if (!result.pending.empty() && !doc.letsPast(version, Doc::CheckPending))
    {
        report(counted("PreprocessPendingSave", static_cast<S32>(result.pending.size()), args), true, &doc, { "save_anyway" });
        doc.stoppedBy(version, Doc::CheckPending);
        saveStopped(doc);
        return;
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
    doc.warnWeightFor = doc.editor->document().version();
    if (preprocessed(doc))
    {
        // What the run made to be sent, which is what goes.
        weighSent(doc);
    }
    else if (doc.weight && doc.weightVersion == doc.warnWeightFor)
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
    if (doc.warnWeightFor < 0 || !doc.weight || doc.weightVersion != doc.warnWeightFor)
    {
        return;
    }
    doc.warnWeightFor = -1;
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
    if (doc.saving)
    {
        // One on its way: this one goes when it answers, with whatever is
        // unsaved by then -- an editor outside saving again while the last
        // compiles, a key pressed twice.
        doc.saveAgain = true;
        return;
    }
    // Where it cannot go -- its object out of sight, the item gone, the
    // connection lost -- said, with the notice back in sight to offer a
    // copy or a file; nothing is tried that would only fail.
    if (doc.orphan == Doc::Orphan::Away || doc.orphan == Doc::Orphan::Removed || doc.orphan == Doc::Orphan::Offline ||
        doc.orphan == Doc::Orphan::Locked || doc.orphan == Doc::Orphan::Unloaded)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
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
    }
    if (doc.detached)
    {
        // Its item in reach, and not loaded under it yet -- a try that
        // failed waits its turn: loaded now, what it holds carried over,
        // so that it is saved as what the item is once asked again.
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("SaveWaitsForLoad", args), true);
        doc.reattachTries = 0;
        reattach(doc);
        saveStopped(doc);
        return;
    }
    // Saved, a preview is held.
    holdPreview(doc);
    // Tidied as the scripter asked before anything is sent or checked: a
    // step each to undo, and nothing where the text is tidy already. The
    // safe fixes first, while the text is still the one they were made
    // for, and once a save, however many checks it waits on.
    if (!doc.notecard)
    {
        if (gSavedSettings.getBOOL("ALScriptFixOnSave") && !doc.fixedForSave && doc.analysisVersion == doc.editor->document().version())
        {
            doc.fixedForSave = true;
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
    if (!doc.file.empty())
    {
        saveFile(doc);
        return;
    }
    if (doc.notecard)
    {
        // What goes back: the text with the items it still stands
        // somewhere, numbered afresh, and those items alone, as the
        // legacy notecard prunes what an edit took out. The editor keeps
        // its own numbering and its whole list: a placeholder undone
        // back into the text still names its item, and the next save
        // numbers afresh from whatever the text then stands.
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
        std::string error;
        doc.sentAt = doc.editor->savePoint();
        if (!ALScriptWorkspace::instance().saveNotecard(doc.ref, text, items, nullptr, error))
        {
            LLStringUtil::format_map_t failed;
            failed["[NAME]"]  = doc.name;
            failed["[ERROR]"] = error;
            report(getString("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
            saveStopped(doc);
            return;
        }
        doc.saving = true;
        doc.saving_items.clear();
        for (const LLPointer<LLInventoryItem>& each : items)
        {
            doc.saving_items.push_back(each->getUUID());
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("Saving", args));
        refreshToolbar();
        return;
    }
    if (!preflight(doc))
    {
        return;
    }
    if (preprocessed(doc))
    {
        // Expanded first, with its includes fetched; the upload follows.
        preprocess(doc, true);
        return;
    }
    doc.uploaded.valid = false;
    weighForSave(doc);
    upload(doc, doc.editor->text());
}

void ALFloaterScriptStudio::upload(Doc& doc, const std::string& text, const ALSourceMap* map)
{
    // Past the checks: the next save may fix again.
    doc.fixedForSave = false;
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
    doc.sentAt = doc.editor->savePoint();
    if (!ALScriptWorkspace::instance().save(doc.ref, text, options, nullptr, error))
    {
        LLStringUtil::format_map_t failed;
        failed["[NAME]"]  = doc.name;
        failed["[ERROR]"] = error;
        report(getString("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
        saveStopped(doc);
        return;
    }
    doc.clearSaveChecks();
    doc.saving = true;
    // What the compiler's lines are read back through, kept as it went:
    // the next preprocess, for whatever reason, is of another text.
    doc.sentMap = map ? std::optional<ALSourceMap>(*map) : std::nullopt;
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
    Doc&       doc  = *mDocs[index];
    const bool ours = doc.saving;
    doc.saving      = false;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (!result.error.empty())
    {
        args["[ERROR]"] = result.error;
        // The region would not say what experience it runs under: the box
        // beside Save says so and offers what may be picked, which the
        // next save sets.
        report(getString(result.experienceUnknown ? "SaveExperienceUnknown" : "SaveFailed", args), true, &doc, { "retry", "copy", "export" });
        // What was asked for meanwhile would meet the same; Retry is offered.
        doc.saveAgain = false;
        if (ours)
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
        doc.editor->markSavedAt(doc.sentAt);
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
                doc.items->saved(doc.saving_items);
            }
            doc.saving_items.clear();
        }
        report(getString("SavedNotecard", args), false, &doc);
        refreshToolbar();
        fillTabs();
        if (sendQueuedSave(doc))
        {
            return;
        }
        if (doc.closeAfterSave)
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
        if (!doc.externalSave)
        {
            syncExternal(doc);
        }
        doc.externalSave = false;
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
        if (ours && !(doc.closeAfterSave && quittingOnUs()))
        {
            saveStopped(doc);
        }
        // What the compiler said, in sight, as the checks before a save
        // show theirs: the first error chosen.
        if (ours && index == mActive && !doc.closeAfterSave)
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
    if (doc.closeAfterSave)
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
    if (doc.sentMap)
    {
        return &*doc.sentMap;
    }
    return doc.uploaded.valid && !doc.uploaded.disabled ? &doc.uploaded.map : nullptr;
}

bool ALFloaterScriptStudio::sendQueuedSave(Doc& doc)
{
    // Asked for while the last was on its way, and something is unsaved
    // still: sent now, and a close waiting on the save waits on this one.
    if (!std::exchange(doc.saveAgain, false) || !doc.unsaved())
    {
        return false;
    }
    save(doc);
    return doc.saveUnderway();
}

void ALFloaterScriptStudio::saveStopped(Doc& doc)
{
    doc.closeAfterSave = false;
    doc.fixedForSave   = false;
    stopClosing();
}

void ALFloaterScriptStudio::saveToClose(const std::string& id)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc           = *mDocs[index];
    doc.closeAfterSave = true;
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

bool ALFloaterScriptStudio::preflight(Doc& doc)
{
    static LLCachedControl<bool> hold(gSavedSettings, "ALScriptStudioPreflight", false);
    if (doc.notecard)
    {
        return true;
    }
    const S64 version = doc.editor->document().version();
    if (!hold)
    {
        // Not held for the analyzers, who are not the region's compiler:
        // what the check of this text found said, where it is in, and the
        // save sent.
        if (doc.analysisVersion == version)
        {
            const S32 errors = static_cast<S32>(std::count_if(doc.analysis.begin(), doc.analysis.end(),
                                                              [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
            if (errors > 0)
            {
                LLStringUtil::format_map_t args;
                args["[NAME]"] = doc.name;
                report(counted("SentWithErrors", errors, args), true, &doc);
            }
        }
        return true;
    }
    if (doc.letsPast(version, Doc::CheckAll))
    {
        // Saved over whatever is found: a copy made to be kept, a save from
        // an editor outside.
        return true;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    const bool errors_wanted = !doc.letsPast(version, Doc::CheckAnalyzers);
    if (errors_wanted && doc.analysisVersion != doc.editor->document().version())
    {
        // Checked first; the save follows the answer.
        doc.saveAfterCheck = true;
        scheduleAnalysis(doc, true);
        setStatus(getString("Preflight", args));
        return false;
    }
    S32 errors = 0;
    if (errors_wanted)
    {
        for (const ALScriptProblem& problem : doc.analysis)
        {
            if (problem.severity == ALScriptProblem::Severity::Error)
            {
                ++errors;
            }
        }
        if (doc.expanded.valid && doc.expanded.version == doc.analysisVersion)
        {
            for (const ALScriptProblem& problem : doc.expanded.problems)
            {
                if (problem.severity == ALScriptProblem::Severity::Error)
                {
                    ++errors;
                }
            }
        }
    }
    // What it weighs is not asked here: a save goes up whatever its
    // estimate, and says so where it is over (weighForSave).
    if (errors == 0)
    {
        return true;
    }
    // Said as what stopped it, and only that let past by asking again.
    report(counted("PreflightErrors", errors, args), true, &doc, { "save_anyway" });
    doc.stoppedBy(version, Doc::CheckAnalyzers);
    saveStopped(doc);
    // The first of the checkers' errors, in sight.
    showBottom("problems_tab");
    selectFirstError(true);
    return false;
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
    doc.letPast(doc.editor->document().version());
    save(doc);
}

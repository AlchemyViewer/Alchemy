/**
 * @file alscriptstudiosaving.cpp
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

#include "alscriptstudiosaving.h"

#include "alincludesearch.h"
#include "alscriptnotecardtab.h"
#include "alscriptstudioservices.h"
#include "lldate.h"

#include <algorithm>

ALScriptStudioSaving::ALScriptStudioSaving(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

void ALScriptStudioSaving::preprocess(Doc& doc)
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
    mServices.setStatus(mServices.words("Preprocessing", args));
    const std::weak_ptr<bool> alive   = mAlive;
    const std::string         id      = doc.id;
    const U32                 version = doc.editor->document().version();
    mWindow.runPreprocessor(doc, [this, alive, id, version](const ALPreprocessor::Result& result) {
        if (alive.lock())
        {
            preprocessedAnswer(id, version, result);
        }
    });
}

namespace
{
    // Whether two //program_version lines are the same viewer's: by its
    // name, the first word, whatever the channel's rest and the version.
    bool sameProgram(const std::string& a, const std::string& b)
    {
        const std::string first = a.substr(0, a.find(' '));
        return !first.empty() && first == b.substr(0, b.find(' '));
    }

    // Names as a save says them: in a row, a comma between.
    std::string joined(const std::vector<std::string>& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
}

void ALScriptStudioSaving::compareCompiled(Doc& doc, U32 version, const ALPreprocessor::Result& result)
{
    if (!doc.compareCompiledAt)
    {
        return;
    }
    const bool as_loaded = *doc.compareCompiledAt == version;
    doc.compareCompiledAt.reset();
    // Only a whole run of the text as it came, which tells: where the
    // source asks for what differs from one run to the next, or another
    // viewer made the half with transforms whose names ours need not
    // share, it cannot be told (ALScriptEnvelope::comparable).
    if (!as_loaded || !doc.envelope || result.overran || result.disabled || result.hasErrors() || !result.pending.empty())
    {
        return;
    }
    const bool here        = sameProgram(doc.envelope->programVersion, mWindow.saveOptions().program);
    const bool transformed = result.usedSwitches || result.usedLazyLists || result.usedExtensions;
    if (ALScriptEnvelope::comparable(doc.editor->wholeText(), doc.language.lua, here, transformed) &&
        !ALScriptEnvelope::compiledFrom(result.text, doc.envelope->expanded, doc.language.lua))
    {
        doc.compiledDiffers = doc.envelope->expanded;
        mWindow.refreshNotice();
    }
}

void ALScriptStudioSaving::preprocessedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result)
{
    Doc* found = mServices.findDoc(id);
    if (!found)
    {
        return;
    }
    Doc& doc          = *found;
    doc.preprocessing = false;
    // What a save would upload, shown; the analyzers' own expansion is
    // made again now that every include is in, without the optimizer.
    doc.uploaded.valid    = true;
    doc.uploaded.disabled = result.disabled;
    doc.uploaded.version  = version;
    doc.uploaded.text     = std::make_shared<const std::string>(result.text);
    doc.uploaded.map      = result.map;
    doc.uploaded.problems = result.problems;
    doc.uploaded.resolved = result.resolved;
    doc.uploaded.consts   = result.consts;
    doc.uploaded.codeBefore = result.codeBefore;
    doc.uploaded.codeAfter  = result.codeAfter;
    doc.expanded.valid    = false;
    mWindow.runningKnown(doc);
    mWindow.showExpanded(doc, result.text);
    mWindow.refreshProblems(doc);
    compareCompiled(doc, version, result);
    // And what it comes to for a save waiting on it.
    ALScriptSaveFlow::Run run;
    run.asked                             = version;
    run.now                               = doc.editor->document().version();
    const ALScriptSaveFlow::Landed landed = doc.save.preprocessed(run);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (run.now != run.asked)
    {
        // The text moved on while the includes came: analysed again, and
        // saved again from the start if that was the point.
        doc.expanded.valid = false;
        mWindow.scheduleAnalysis(doc, true);
        if (landed == ALScriptSaveFlow::Landed::MovedOn)
        {
            save(doc);
        }
        return;
    }
    mWindow.scheduleAnalysis(doc, true);
    // The includes that never came, by the names the script gave them.
    args["[FILES]"] = joined(result.pending);
    switch (landed)
    {
        case ALScriptSaveFlow::Landed::NotForSave:
            if (!result.pending.empty())
            {
                mServices.report(mServices.counted("PreprocessedPending", static_cast<S32>(result.pending.size()), args), true, &doc);
            }
            else
            {
                // Said where the status goes, not added to the output: the
                // view is expanded every time it is shown, and every open
                // script is when a file one of them may include is saved.
                mServices.setStatus(mServices.words("Preprocessed", args));
                // Weighed as a save would send it; not with an include still
                // to come, which may yet.
                mWindow.weighSent(doc);
            }
            return;
        case ALScriptSaveFlow::Landed::MovedOn:
        case ALScriptSaveFlow::Landed::Send:
            break;
    }
    // Going up whatever the run found: a save is what keeps the author's
    // work, the source going up in the envelope with what it expanded to.
    // What it goes without -- an include not found, or one that never came
    // -- said by name as it goes, and the errors besides, in sight.
    const ALIncludeSearch::LeftOut left_out = ALIncludeSearch::leftOut(result.problems);
    std::vector<std::string>       without  = left_out.names;
    for (const std::string& name : result.pending)
    {
        if (std::find(without.begin(), without.end(), name) == without.end())
        {
            without.push_back(name);
        }
    }
    if (!without.empty())
    {
        args["[FILES]"]  = joined(without);
        std::string said = mServices.counted("SavingWithout", static_cast<S32>(without.size()), args);
        if (left_out.diskRoute)
        {
            said += " " + mServices.words("PreprocessMissingDiskRoute", args);
        }
        mServices.report(said, true, &doc);
    }
    const S32 errors = static_cast<S32>(std::count_if(result.problems.begin(), result.problems.end(),
                                                      [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
    if (errors > left_out.problems)
    {
        mServices.report(mServices.counted("SavingWithErrors", errors - left_out.problems, args), true, &doc);
        mWindow.showProblems();
    }
    // Weighed as it goes.
    weighForSave(doc);
    sendPreprocessed(doc, doc.uploaded);
}

void ALScriptStudioSaving::sendPreprocessed(Doc& doc, const Doc::Expanded& sent)
{
    if (sent.disabled)
    {
        // `//fspreprocessor off`: the text goes up as it is, as
        // Firestorm sends it.
        upload(doc, doc.editor->wholeText());
        return;
    }
    // In the envelope, with the source as written, so Firestorm opens
    // what we save; the lines that say who wrote it and when are ours.
    const ALScriptEnvelope envelope = doc.envelopeFor(*sent.text, mWindow.saveOptions().program);
    doc.envelope                    = envelope;
    upload(doc, envelope.wrap(), &sent.map);
}

void ALScriptStudioSaving::weighForSave(Doc& doc)
{
    if (!mWindow.weightTarget(doc))
    {
        return;
    }
    doc.save.setWarnWeightFor(doc.editor->document().version());
    if (mWindow.preprocessed(doc))
    {
        // What the run made to be sent, which is what goes.
        mWindow.weighSent(doc);
    }
    else if (doc.weighing.weight && doc.weighing.version == doc.save.warnWeightFor())
    {
        warnOverWeight(doc);
    }
    else
    {
        mWindow.weigh(doc);
    }
}

void ALScriptStudioSaving::warnOverWeight(Doc& doc)
{
    if (doc.save.warnWeightFor() < 0 || !doc.weighing.weight || doc.weighing.version != doc.save.warnWeightFor())
    {
        return;
    }
    doc.save.setWarnWeightFor(-1);
    // Over the limit as the text was sent, as nearly as the studio can
    // tell: Mono's an estimate, and said as one.
    const ALScriptWeight& weight = *doc.weighing.weight;
    if (doc.weighing.exact && weight.total > weight.limit)
    {
        reportOverWeight(doc, weight);
    }
}

void ALScriptStudioSaving::save(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    // Where it cannot go -- its object out of sight, the item gone, the
    // connection lost -- said, with the notice back in sight to offer a
    // copy or a file.
    const Doc::Orphan kind         = doc.orphan.kind;
    const bool        out_of_reach = kind == Doc::Orphan::Away || kind == Doc::Orphan::Removed || kind == Doc::Orphan::Offline ||
                              kind == Doc::Orphan::Locked || kind == Doc::Orphan::Unloaded;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    switch (doc.save.ask(out_of_reach, doc.orphan.detached))
    {
        case ALScriptSaveFlow::Start::Queued:
            return;
        case ALScriptSaveFlow::Start::OutOfReach:
            mServices.setStatus(mServices.words(doc.orphan.kind == Doc::Orphan::Away       ? "SaveBlockedAway"
                                                : doc.orphan.kind == Doc::Orphan::Removed  ? "SaveBlockedRemoved"
                                                : doc.orphan.kind == Doc::Orphan::Locked   ? "SaveBlockedLocked"
                                                : doc.orphan.kind == Doc::Orphan::Unloaded ? "SaveBlockedUnloaded"
                                                                                      : "SaveBlockedOffline",
                                                args),
                                true);
            doc.orphan.noticeDismissed = false;
            stopped(doc);
            if (&doc == mServices.frontDoc())
            {
                mWindow.refreshNotice();
            }
            return;
        case ALScriptSaveFlow::Start::Detached:
            // Its item in reach, and not loaded under it yet -- a try that
            // failed waits its turn: loaded now, what it holds carried over,
            // so that it is saved as what the item is once asked again.
            mServices.setStatus(mServices.words("SaveWaitsForLoad", args), true);
            doc.orphan.reattachTries = 0;
            mWindow.reattach(doc);
            stopped(doc);
            return;
        case ALScriptSaveFlow::Start::Go:
            break;
    }
    // Saved, a preview is held.
    mWindow.holdPreview(doc);
    // Tidied as the scripter asked before anything is sent or checked: one
    // step to undo however many of them change it, and nothing where the
    // text is tidy already. The safe fixes first, while the text is still
    // the one they were made for.
    // Not a save the external editor made: what it wrote is what the
    // author is looking at there, and nothing here is written back to it,
    // so a text tidied here would read as changed on both sides at the
    // next save it made.
    const Options options = mWindow.saveOptions();
    if (!doc.notecard && !doc.save.external())
    {
        const bool fix = options.fix && doc.check.analysisVersion == doc.editor->document().version() && doc.save.fixOnce();
        doc.editor->undoJournal().beginGroup();
        mWindow.tidy(doc, fix, options.format, options.trim);
        doc.editor->undoJournal().endGroup();
    }
    ALScriptSaveFlow::Tab tab;
    tab.version          = doc.editor->document().version();
    tab.file             = !doc.file.empty();
    tab.notecard         = doc.notecard;
    tab.preprocessed     = !tab.file && !tab.notecard && mWindow.preprocessed(doc);
    tab.preprocessorBusy = doc.preprocessing;
    tab.holdOnErrors     = options.holdOnErrors;
    tab.checked          = doc.check.analysisVersion == tab.version;
    tab.checkerErrors    = checkerErrors(doc);
    const ALScriptSaveFlow::Route route = doc.save.route(tab);
    switch (route)
    {
        case ALScriptSaveFlow::Route::File:
            mWindow.saveFile(doc);
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
            const ALTextUndo::SavePoint at      = doc.editor->savePoint();
            const U64                   request = mWindow.newRequest();
            if (!mWindow.sendNotecard(doc, text, items, error, request))
            {
                LLStringUtil::format_map_t failed;
                failed["[NAME]"]  = doc.name;
                failed["[ERROR]"] = error;
                mServices.report(mServices.words("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
                stopped(doc);
                return;
            }
            std::vector<LLUUID> sent;
            for (const LLPointer<LLInventoryItem>& each : items)
            {
                sent.push_back(each->getUUID());
            }
            doc.save.sent(at, std::nullopt, std::move(sent), request);
            mServices.setStatus(mServices.words("Saving", args));
            mWindow.refreshToolbar();
            return;
        }
        case ALScriptSaveFlow::Route::Check:
            // Checked first; the save follows the answer.
            mWindow.scheduleAnalysis(doc, true);
            mServices.setStatus(mServices.words("Preflight", args));
            return;
        case ALScriptSaveFlow::Route::StoppedByAnalyzers:
            // Said as what stopped it, and only that let past by asking
            // again; the first of the checkers' errors in sight.
            mServices.report(mServices.counted("PreflightErrors", tab.checkerErrors, args), true, &doc, { "save_anyway" });
            stopped(doc);
            mWindow.showProblems();
            mWindow.selectFirstError(true);
            return;
        case ALScriptSaveFlow::Route::Preprocess:
        case ALScriptSaveFlow::Route::JoinPreprocessor:
        case ALScriptSaveFlow::Route::Send:
            break;
    }
    // Not held for the analyzers, who are not the region's compiler: what
    // the check of this text found said, where it is in.
    if (!options.holdOnErrors && tab.checked)
    {
        const S32 errors = static_cast<S32>(std::count_if(doc.check.analysis.begin(), doc.check.analysis.end(),
                                                          [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
        if (errors > 0)
        {
            mServices.report(mServices.counted("SentWithErrors", errors, args), true, &doc);
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
    upload(doc, doc.editor->wholeText());
}

S32 ALScriptStudioSaving::checkerErrors(const Doc& doc)
{
    // The analyzers' errors, and the preprocessor's in the expansion they
    // read.
    S32 errors = 0;
    for (const ALScriptProblem& problem : doc.check.analysis)
    {
        errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
    }
    if (doc.expanded.valid && doc.expanded.version == doc.check.analysisVersion)
    {
        for (const ALScriptProblem& problem : doc.expanded.problems)
        {
            errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
        }
    }
    return errors;
}

void ALScriptStudioSaving::upload(Doc& doc, const std::string& text, const ALSourceMap* map)
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
        const char* shrink = !map                                                    ? "SaveTooLargePlain"
                             : !doc.language.lua && !mWindow.saveOptions().compress ? "SaveTooLargeCompress"
                                                                                    : "SaveTooLargeWrapped";
        mServices.report(mServices.words("SaveTooLarge", args) + " " + mServices.words(shrink), true, &doc);
        doc.weighing.assetBytes = text.size();
        stopped(doc);
        if (&doc == mServices.frontDoc())
        {
            mWindow.refreshTrailer(doc);
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
    options.sender = ALScriptWorkspace::Sender(ALScriptWorkspace::Origin::Studio, mWindow.newRequest());
    std::string error;
    // Where the journal stands as the text goes, taken before anything
    // can be typed after it.
    const ALTextUndo::SavePoint at = doc.editor->savePoint();
    if (!mWindow.send(doc, text, options, error))
    {
        LLStringUtil::format_map_t failed;
        failed["[NAME]"]  = doc.name;
        failed["[ERROR]"] = error;
        mServices.report(mServices.words("SaveFailed", failed), true, &doc, { "retry", "copy", "export" });
        stopped(doc);
        return;
    }
    // With the map the compiler's lines are read back through.
    doc.save.sent(at, map ? std::optional<ALSourceMap>(*map) : std::nullopt, {}, options.sender.request);
    doc.problems.clear();
    mWindow.refreshProblems(doc);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    mServices.setStatus(mServices.words("Saving", args));
    mWindow.refreshToolbar();
}

void ALScriptStudioSaving::saveAll()
{
    for (Doc* doc : mServices.openDocs())
    {
        if (doc->unsaved())
        {
            save(*doc);
        }
    }
}

void ALScriptStudioSaving::savedElsewhere(const ALScriptWorkspace::Saved& saved)
{
    Doc* found = mServices.findDoc(saved.ref);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    // This tab's own save lands as its answer; one of its own on its way
    // lands after this, and is what the server keeps.
    const bool notecard = saved.kind == ALScriptWorkspace::Kind::Notecard;
    if ((saved.sender.origin == ALScriptWorkspace::Origin::Studio && saved.sender.request == doc.save.request()) || !doc.loaded ||
        notecard != doc.notecard || doc.save.sending())
    {
        return;
    }
    // The author's text as it went up: a script's out of its envelope.
    std::string theirs = saved.text;
    if (!notecard)
    {
        if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(saved.text))
        {
            theirs = envelope->source;
        }
    }
    if (saved.asset.notNull())
    {
        doc.assetId = saved.asset;
    }
    const std::string& here = doc.editor->wholeText();
    if (theirs == here)
    {
        // What is here is what went up.
        doc.editor->resetDirty();
        mWindow.refreshToolbar();
        return;
    }
    if (doc.editor->isDirty() && doc.modifiable)
    {
        // A save of the text this tab last had -- a recompile, a queue --
        // changes nothing it holds: what was typed stays, to be saved.
        const std::optional<std::string> before = doc.editor->undoJournal().savedText();
        if (before && theirs == *before)
        {
            return;
        }
        // Changed here and there: one would be lost, so the author says
        // which, with the two to compare.
        doc.savedThere = theirs;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        args["[WHO]"]  = mServices.words(saved.sender.origin == ALScriptWorkspace::Origin::Bridge   ? "SavedByBridge"
                                         : saved.sender.origin == ALScriptWorkspace::Origin::Editor ? "SavedByEditor"
                                                                                                    : "SavedByQueue");
        mServices.report(mServices.words("SavedElsewhereConflict", args), true, &doc, { "take_saved", "keep_saved", "compare_saved" });
        return;
    }
    // Nothing typed here: taken as if loaded afresh.
    mWindow.takeLoaded(doc, saved.text);
}

void ALScriptStudioSaving::takeSaved(Doc& doc)
{
    if (!doc.savedThere)
    {
        return;
    }
    // What was saved elsewhere put in as one step, what was typed here a
    // step back in the undo; the server holds it, so nothing to save.
    doc.carriedText = std::move(*doc.savedThere);
    doc.savedThere.reset();
    mWindow.takeCarried(doc);
    doc.editor->resetDirty();
    mWindow.refreshToolbar();
}

void ALScriptStudioSaving::keepSaved(Doc& doc)
{
    if (!doc.savedThere)
    {
        return;
    }
    // What was typed here kept; saving it replaces what was saved there.
    doc.savedThere.reset();
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    mServices.setStatus(mServices.words("SavedElsewhereKept", args));
}

void ALScriptStudioSaving::compiled(const ALScriptWorkspace::CompileResult& result)
{
    // A copy of another tab, saved -- compiled or not, the text is up: the
    // tab it copied is safe in the inventory, and closes, once everything
    // below is done with the tabs as they stand.
    std::string copy_of;
    U32         copied_at = 0;
    if (Doc* doc = mServices.findDoc(result.ref); doc && result.error.empty())
    {
        copy_of.swap(doc->copyOf);
        copied_at = doc->copyOfVersion;
    }
    compiledHere(result);
    if (Doc* original = copy_of.empty() ? nullptr : mServices.findDoc(copy_of))
    {
        Doc&                       from = *original;
        LLStringUtil::format_map_t copied;
        copied["[NAME]"] = from.name;
        if (from.editor->document().version() != copied_at)
        {
            // Typed in since the copy was made: what was typed is not in the
            // copy, and the tab stays with it.
            mServices.report(mServices.words("CopiedToKeptOpen", copied), false, &from);
            return;
        }
        mServices.report(mServices.words("CopiedTo", copied));
        from.editor->resetDirty();
        mWindow.letGoOf(from);
    }
}

void ALScriptStudioSaving::compiledHere(const ALScriptWorkspace::CompileResult& result)
{
    Doc* found = mServices.findDoc(result.ref);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    // Whether it answers this tab's own save, rather than a recompile from
    // the explorer, and what that save comes to.
    ALScriptSaveFlow::Answer answer;
    answer.up                             = result.error.empty();
    answer.compiled                       = result.success;
    answer.quitting                       = mWindow.quittingOnUs();
    answer.request                        = result.sender.request;
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
        mServices.report(mServices.words(result.experienceUnknown ? "SaveExperienceUnknown" : "SaveFailed", args), true, &doc,
                         { "retry", "copy", "export" });
        if (landing.stopped)
        {
            stopped(doc);
        }
        mWindow.refreshToolbar();
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
        mWindow.fillTabs();
        mWindow.keepSavedWeights(doc);
    }
    if (result.newAssetId.notNull())
    {
        doc.assetId = result.newAssetId;
    }
    // Saved: nothing of it to keep against a crash any more, or only what
    // was typed while the save was on its way.
    mWindow.keepForRecovery(doc);
    if (result.kind == ALScriptWorkspace::Kind::Notecard)
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
        mServices.report(mServices.words("SavedNotecard", args), false, &doc);
        mWindow.refreshToolbar();
        mWindow.fillTabs();
        if (sendQueuedSave(doc))
        {
            return;
        }
        if (doc.save.closeAfter())
        {
            mWindow.letGoOf(doc);
            mWindow.continueClosing();
        }
        return;
    }
    doc.problems.clear();
    // The region's lines count the envelope's; the map is of the code under it.
    const ALSourceMap* read  = doc.runningMap();
    const S32          under = doc.runningCodeLine();
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
            const ALSourceMap::Loc loc = read->toSource(said.line - under, said.column);
            if (loc.found())
            {
                one.line   = loc.line;
                one.column = loc.column;
                if (loc.file > 0)
                {
                    one.file = read->files()[loc.file].path;
                }
            }
            else
            {
                // In code the preprocessor made, at the expansion's line.
                one.line = llmax(0, said.line - under);
                one.file = Doc::GENERATED;
            }
        }
        doc.problems.push_back(std::move(one));
    }
    if (result.success)
    {
        // A new script runs from here; what the old one said is past.
        doc.runtime.clear();
    }
    mWindow.refreshProblems(doc);
    if (ours && doc.external.watch)
    {
        // The editor outside sees what was saved here, and what the
        // compiler made of it; its own save is not written back to it.
        if (!doc.save.external())
        {
            mWindow.syncExternal(doc);
        }
        doc.save.endExternal();
        mWindow.logExternal(doc, result);
    }

    if (result.success)
    {
        mServices.report(mServices.words("Compiled", args), false, &doc);
    }
    else
    {
        mServices.report(mServices.counted("CompileFailed", static_cast<S32>(doc.problems.size()), args), true, &doc);
        // Saved, but not running: a close waiting on it leaves the tab
        // open with what the compiler said, rather than taking both away --
        // but for the viewer quitting, when the text is saved and that was
        // what was asked.
        if (landing.stopped)
        {
            stopped(doc);
        }
        // What the compiler said, in sight, as the checks before a save
        // show theirs: the first error chosen.
        if (ours && &doc == mServices.frontDoc() && !doc.save.closeAfter())
        {
            mWindow.showProblems();
            mWindow.selectFirstError(false);
        }
    }
    if (&doc == mServices.frontDoc())
    {
        mWindow.refreshToolbar();
    }
    mWindow.fillTabs();
    if (sendQueuedSave(doc))
    {
        return;
    }
    if (doc.save.closeAfter())
    {
        mWindow.letGoOf(doc);
        mWindow.continueClosing();
    }
}

bool ALScriptStudioSaving::sendQueuedSave(Doc& doc)
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

void ALScriptStudioSaving::stopped(Doc& doc)
{
    doc.save.stopped();
    mWindow.stopClosing();
}

void ALScriptStudioSaving::saveToClose(const std::string& id)
{
    Doc* found = mServices.findDoc(id);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    doc.save.setCloseAfter(true);
    save(doc);
    // Gone already -- a file, saved and let go of on the spot -- or on its
    // way: sent, or waiting on the preprocessor or a check.
    if (!mServices.findDoc(id) || doc.saveUnderway())
    {
        return;
    }
    // It could not begin -- still loading, say: the tab stays for the
    // author, not set to close at whatever save comes next, and a close
    // waiting on it waits no longer.
    stopped(doc);
}

void ALScriptStudioSaving::reportOverWeight(const Doc& doc, const ALScriptWeight& weight)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"]   = doc.name;
    args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
    args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
    args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
    mServices.report(mServices.words(weight.estimate ? "SaveOverWeightEstimate" : "SaveOverWeight", args), true, &doc);
}

void ALScriptStudioSaving::saveAsked(Doc& doc)
{
    // Asked for by the author: over the text the last save was stopped at,
    // past what stopped it.
    doc.save.letPast(doc.editor->document().version());
    save(doc);
}

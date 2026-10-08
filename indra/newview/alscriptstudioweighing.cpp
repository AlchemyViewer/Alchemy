/**
 * @file alscriptstudioweighing.cpp
 * @brief Script Studio's weighing: what a script comes to for its target, kept as saved, shown in the editor and the Weights tab.
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

#include "alscriptstudioweighing.h"

#include "alscriptenvelope.h"
#include "alscriptfixes.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudiochecking.h"
#include "alscriptstudiosaves.h"
#include "alscriptstudioservices.h"
#include "alscriptweightspane.h"
#include "lldate.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>

ALScriptStudioWeighing::ALScriptStudioWeighing(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, ALScriptStudioSaves& saves, Window& window) : mServices(services), mAnalysis(analysis), mSaves(saves), mWindow(window)
{
}

std::optional<ALScriptWeight::Target> ALScriptStudioWeighing::target(const Doc& doc) const
{
    if (!doc.loaded || doc.notecard || mAnalysis.lslFragment(doc))
    {
        return std::nullopt;
    }
    if (doc.language.lua)
    {
        return ALScriptWeight::Target::SLua;
    }
    const std::string& target = doc.language.compileTarget;
    return target == "lsl2"                     ? std::optional(ALScriptWeight::Target::LSO)
           : target == "lsl-luau"               ? std::optional(ALScriptWeight::Target::LSLLuau)
           : target == "mono" || target.empty() ? std::optional(ALScriptWeight::Target::Mono)
                                                : std::nullopt;
}

// static
bool ALScriptStudioWeighing::asking(const Doc& doc)
{
    // An answer for an older text is not coming: the analyzers pass over a
    // question a newer text has made pointless.
    return doc.weighing->asking && doc.weighing->askedFor == doc.editor->document().version();
}

void ALScriptStudioWeighing::weigh(Doc& doc)
{
    if (!target(doc))
    {
        doc.weighing->weight.reset();
        return;
    }
    doc.weighing->asking   = true;
    doc.weighing->askedFor = doc.editor->document().version();
    mWindow.askWeights(doc);
}

void ALScriptStudioWeighing::weighed(Doc& doc, const ALScriptAnalysis::Result& result)
{
    doc.weighing->asking = false;
    if (result.version != doc.editor->document().version() || result.weights.empty())
    {
        return;
    }
    // In the source's places, through the expansion the question was
    // asked over -- which answered() has made sure is the one there is;
    // the bundle's, where it was the bundle that was weighed.
    doc.weighing->all.clear();
    const ALSourceMap& map = doc.expanded.bundle ? doc.expanded.bundleMap : doc.expanded.map;
    for (const ALScriptWeight& weight : result.weights)
    {
        doc.weighing->all.push_back(mAnalysis.preprocessed(doc) && doc.expanded.valid ? weight.inSource(map) : weight);
    }
    doc.weighing->allVersion = result.version;
    keepSaved(doc);
    if (&doc == mServices.frontDoc())
    {
        mStale = true;
    }
    // What a preprocessor's run made to be sent, weighed, says more of the
    // same text than its check does: it stands until the text changes.
    if (!doc.weighing->sent || doc.weighing->version != result.version)
    {
        doc.weighing->weight        = doc.weighing->all.front();
        doc.weighing->version = result.version;
        doc.weighing->sent    = false;
        // What was weighed is what a save compiles where the preprocessor
        // does not run, or runs without the optimizer, which comes after
        // the check's expansion: SLua's is never optimized.
        doc.weighing->exact = !mAnalysis.preprocessed(doc) || doc.language.lua || !mWindow.optimizing();
        mAnalysis.refreshProblems(doc);
        showInEditor(doc);
    }
    mSaves.warnOverWeight(doc);
}

void ALScriptStudioWeighing::weighSent(Doc& doc)
{
    const std::optional<ALScriptWeight::Target> target = this->target(doc);
    if (!target || !doc.uploaded.valid || doc.uploaded.version != doc.editor->document().version())
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind     = ALScriptAnalysis::Kind::Weigh;
    request.weighing = ALScriptAnalysis::Request::Weighing::Sent;
    request.id       = doc.id;
    request.version  = doc.uploaded.version;
    request.lua     = doc.language.lua;
    request.text    = doc.uploaded.disabled ? doc.snapshot() : doc.uploaded.text;
    request.targets = { *target };
    // What was weighed kept with the question, its map for the places: a
    // run since -- a setting changed while it was weighed -- is of another
    // text, and weighs its own.
    const std::weak_ptr<bool> alive = mAlive;
    mAnalysis.askAnalysis(std::move(request), [this, alive, sent = doc.uploaded](const ALScriptAnalysis::Result& result) {
        if (Doc* doc = alive.lock() ? mServices.findDoc(result.id) : nullptr)
        {
            weighedSent(*doc, result, sent);
        }
    });
}

void ALScriptStudioWeighing::weighedSent(Doc& doc, const ALScriptAnalysis::Result& result, const Doc::Expanded& sent)
{
    const U32 version = doc.editor->document().version();
    if (result.version != version)
    {
        // The text moved on while it was weighed.
        return;
    }
    const ALScriptWeight* weight = result.weights.empty() ? nullptr : &result.weights.front();
    if (weight)
    {
        doc.weighing->weight        = weight->inSource(sent.map);
        doc.weighing->version = result.version;
        doc.weighing->exact   = true;
        doc.weighing->sent    = true;
        mAnalysis.refreshProblems(doc);
        showInEditor(doc);
        if (&doc == mServices.frontDoc())
        {
            mStale = true;
        }
    }
    mSaves.warnOverWeight(doc);
}

std::vector<ALScriptWeight::Target> ALScriptStudioWeighing::targets(const Doc& doc) const
{
    const std::optional<ALScriptWeight::Target> own = this->target(doc);
    if (!own)
    {
        return {};
    }
    std::vector<ALScriptWeight::Target> targets = { *own };
    // Three compiles where one would do are the analyzer's time that a
    // completion waits behind: the other two only while they are looked
    // at.
    const bool in_front = mServices.frontDoc() == &doc;
    if (!doc.language.lua && in_front && mWindow.weightsShown())
    {
        for (const ALScriptWeight::Target other :
             { ALScriptWeight::Target::LSO, ALScriptWeight::Target::Mono, ALScriptWeight::Target::LSLLuau })
        {
            if (other != *own)
            {
                targets.push_back(other);
            }
        }
    }
    return targets;
}

void ALScriptStudioWeighing::keepSaved(Doc& doc)
{
    if (doc.editor->isDirty() || doc.weighing->allVersion != doc.editor->document().version())
    {
        return;
    }
    // Each target's in place of what it weighed before, the others kept: a
    // target weighed only while the tab was looked at is counted from what
    // it was then.
    for (const ALScriptWeight& weight : doc.weighing->all)
    {
        const auto same = [&weight](const ALScriptWeight& w) { return w.target == weight.target; };
        auto       held = std::find_if(doc.weighing->saved.begin(), doc.weighing->saved.end(), same);
        if (held != doc.weighing->saved.end())
        {
            *held = weight;
        }
        else
        {
            doc.weighing->saved.push_back(weight);
        }
    }
    if (&doc == mServices.frontDoc())
    {
        mStale = true;
    }
}

void ALScriptStudioWeighing::showInEditor(Doc& doc)
{
    if (!doc.editor)
    {
        return;
    }
    const bool notes_shown = mWindow.weightNotes();
    const bool heat_shown  = mWindow.weightHeat();
    if (!notes_shown)
    {
        doc.editor->setLineNotes({});
    }
    if (!heat_shown)
    {
        doc.editor->setLineHeat({});
    }
    if ((!notes_shown && !heat_shown) || !doc.weighing->weight || doc.weighing->version != doc.editor->document().version())
    {
        return;
    }
    const ALScriptWeight&      weight = *doc.weighing->weight;
    LLStringUtil::format_map_t args;
    args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
    args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
    const auto share = [this, &weight](size_t bytes) {
        return weight.limit ? mServices.words("Percent", { { "[VALUE]", fmt::format("{:f}", (F64)bytes * 100.0 / (F64)weight.limit) } }) : std::string();
    };
    // Said as measured before the optimizer, where it was.
    const std::string before = doc.weighing->exact ? std::string() : mServices.words("WeightsHeadBefore");
    if (notes_shown)
    {
        // Each function, handler, state and global of the script's own, by
        // the line it is declared on; more than one on a line each by name.
        std::map<S32, std::vector<const ALScriptWeight::Part*>> by_line;
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            const bool declared = part.kind == ALScriptWeight::Part::Kind::Function || part.kind == ALScriptWeight::Part::Kind::Handler ||
                                  part.kind == ALScriptWeight::Part::Kind::State || part.kind == ALScriptWeight::Part::Kind::Global;
            if (declared && part.file.empty() && part.line >= 0 && part.bytes > 0)
            {
                by_line[part.line].push_back(&part);
            }
        }
        std::vector<ALCodeEditor::LineNote> notes;
        for (const auto& [line, parts] : by_line)
        {
            ALCodeEditor::LineNote note;
            note.line = line;
            for (const ALScriptWeight::Part* part : parts)
            {
                LLStringUtil::format_map_t said = args;
                said["[BYTES]"]                 = std::to_string(part->bytes);
                said["[SHARE]"]                 = share(part->bytes);
                if (part->name.empty())
                {
                    said["[NAME]"] = mServices.words("WeightsPartUnnamed");
                }
                else if (!part->within.empty())
                {
                    LLStringUtil::format_map_t handler;
                    handler["[EVENT]"] = part->name;
                    handler["[STATE]"] = part->within;
                    said["[NAME]"]     = mServices.words("WeightsPartHandler", handler);
                }
                else
                {
                    said["[NAME]"] = part->name;
                }
                std::string bytes = mServices.words(weight.estimate ? "WeightNoteEstimate" : "WeightNote", said);
                if (parts.size() > 1)
                {
                    said["[NOTE]"] = bytes;
                    bytes          = mServices.words("WeightNoteNamed", said);
                }
                note.text += (note.text.empty() ? "" : "  \xC2\xB7  ") + bytes;
                note.tip += (note.tip.empty() ? "" : "\n") +
                            mServices.words(weight.estimate ? "WeightNoteEstimateTip" : "WeightNoteTip", said);
            }
            note.tip = mServices.sentences(note.tip, before);
            notes.push_back(std::move(note));
        }
        doc.editor->setLineNotes(notes);
    }
    if (heat_shown)
    {
        // Each of the script's own lines by the most any line came to, so
        // that the warmest is the heat's own colour; by its square root,
        // so that a line of a tenth of that is still seen.
        size_t most = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            most = line.file.empty() ? std::max(most, line.bytes) : most;
        }
        std::vector<ALCodeEditor::LineHeat> heat;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            if (!line.file.empty() || line.bytes == 0 || most == 0)
            {
                continue;
            }
            LLStringUtil::format_map_t said = args;
            said["[LINE]"]                  = std::to_string(line.line + 1);
            said["[BYTES]"]                 = std::to_string(line.bytes);
            said["[SHARE]"]                 = share(line.bytes);
            heat.push_back({ line.line, std::sqrt(static_cast<F32>(line.bytes) / static_cast<F32>(most)),
                             mServices.sentences(mServices.words(weight.estimate ? "WeightHeatEstimateTip" : "WeightHeatTip", said), before) });
        }
        doc.editor->setLineHeat(heat);
    }
}

bool ALScriptStudioWeighing::editedCopy(const Doc& doc, const std::vector<std::pair<ALTextRange, std::string>>& edits,
                                        std::string& out) const
{
    ALScriptFix fix;
    for (const auto& [range, text] : edits)
    {
        const ALTextRange at = range.normalised();
        fix.edits.push_back({ at.begin.line, at.begin.column, at.end.line, at.end.column, text });
    }
    if (mAnalysis.preprocessed(doc) && !ALScriptFixes::intoExpansion(doc.expanded.map, fix))
    {
        return false;
    }
    const std::optional<std::string> made = ALScriptFixes::apply(mAnalysis.preprocessed(doc) ? *doc.expanded.text : doc.editor->wholeText(), fix);
    if (!made)
    {
        return false;
    }
    out = *made;
    return true;
}

void ALScriptStudioWeighing::weighFixes(Doc& doc, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes)
{
    const std::optional<ALScriptWeight::Target> target = this->target(doc);
    const U32                                   version = doc.editor->document().version();
    if (!target || (mAnalysis.preprocessed(doc) && (!doc.expanded.valid || doc.expanded.version != version)))
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind     = ALScriptAnalysis::Kind::Weigh;
    request.weighing = ALScriptAnalysis::Request::Weighing::Fixes;
    request.id       = doc.id;
    request.version  = version;
    request.lua      = doc.language.lua;
    request.targets  = { *target };
    // What the analyzers read as it stands first, weighed with the rest so
    // that each is measured against the same weigher at the same moment.
    request.variants.push_back(mAnalysis.preprocessed(doc) ? *doc.expanded.text : doc.editor->wholeText());
    std::vector<S32> variant_of(fixes.size(), -1);
    for (size_t i = 0; i < fixes.size(); ++i)
    {
        std::string copy;
        if (!fixes[i].suppress && !fixes[i].edits.empty() && editedCopy(doc, fixes[i].edits, copy))
        {
            variant_of[i] = static_cast<S32>(request.variants.size());
            request.variants.push_back(std::move(copy));
        }
    }
    if (request.variants.size() < 2)
    {
        return;
    }
    const std::weak_ptr<bool>    alive       = mAlive;
    const ALScriptWeight::Target weighed_for = *target;
    mAnalysis.askAnalysis(std::move(request), [this, alive, shown, variant_of, weighed_for](const ALScriptAnalysis::Result& result) {
        Doc* found = alive.lock() ? mServices.findDoc(result.id) : nullptr;
        if (!found)
        {
            return;
        }
        Doc& doc = *found;
        if (result.version != doc.editor->document().version() || result.variantTotals.empty() || result.variantTotals.front() == 0)
        {
            return;
        }
        // What each would come to less, or more; nothing where it is the
        // same, or did not come to anything.
        const S64                  base     = S64(result.variantTotals.front());
        const bool                 estimate = weighed_for == ALScriptWeight::Target::Mono;
        std::vector<std::string>   notes(variant_of.size());
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = ALScriptWeight::nameOf(weighed_for);
        for (size_t i = 0; i < variant_of.size(); ++i)
        {
            const S32 at = variant_of[i];
            if (at <= 0 || static_cast<size_t>(at) >= result.variantTotals.size() || result.variantTotals[static_cast<size_t>(at)] == 0)
            {
                continue;
            }
            const S64 change = S64(result.variantTotals[static_cast<size_t>(at)]) - base;
            if (change == 0)
            {
                continue;
            }
            args["[BYTES]"] = std::to_string(std::abs(change));
            const char* said = change < 0 ? (estimate ? "FixLighterEstimate" : "FixLighter")
                                          : (estimate ? "FixHeavierEstimate" : "FixHeavier");
            notes[i]         = mServices.words(said, args);
        }
        doc.editor->noteFixes(shown, notes);
    });
}

void ALScriptStudioWeighing::measureAsset(Doc& doc)
{
    const U32 version   = doc.editor->document().version();
    const U32 expansion = doc.expanded.valid ? doc.expanded.generation : 0;
    if (doc.weighing->assetMeasured == std::make_pair(version, expansion))
    {
        return;
    }
    doc.weighing->assetMeasured = std::make_pair(version, expansion);
    doc.weighing->assetBytes    = 0;
    // A file on disk is saved to the disk, which has no limit.
    if (!doc.loaded || !doc.file.empty())
    {
        return;
    }
    // A notecard's text against the notecard's own limit.
    if (doc.notecard)
    {
        doc.weighing->assetBytes = doc.editor->document().byteCount();
        return;
    }
    const std::string& text = doc.editor->wholeText();
    doc.weighing->assetBytes         = text.size();
    if (mAnalysis.preprocessed(doc) && doc.expanded.valid && doc.expanded.version == version && !doc.expanded.disabled)
    {
        // What envelopeFor would make, measured without making it: its
        // two texts are the tab's and the expansion's as they stand.
        // The bundle where the expansion is the script apart from its
        // modules: what a save sends.
        const std::string& sent = doc.expanded.bundle ? *doc.expanded.bundle : *doc.expanded.text;
        doc.weighing->assetBytes = ALScriptEnvelope::wrappedSize(doc.language.lua, text, sent, doc.language.compileTarget,
                                                                mWindow.programVersion(), LLDate::now().asString(),
                                                                doc.headerFor(sent, mWindow.uploadHeader()));
    }
}

void ALScriptStudioWeighing::refreshPane()
{
    ALScriptWeightsPane* pane = mWindow.weightsPane();
    if (!pane)
    {
        return;
    }
    Doc* doc = mServices.frontDoc();
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc ? doc->name : std::string();
    if (!doc)
    {
        pane->showNothing(mServices.words("WeightsNoScript"));
        return;
    }
    if (doc->loaded && !target(*doc))
    {
        pane->showNothing(mServices.words("WeightsNoTarget", args));
        return;
    }
    if (!doc->loaded || doc->weighing->all.empty())
    {
        pane->showNothing(mServices.words("WeightsNotYet", args));
        return;
    }
    ALScriptWeightsPane::Shown shown;
    shown.id      = doc->id;
    shown.name    = doc->name;
    shown.weights = doc->weighing->all;
    shown.saved   = doc->weighing->saved;
    shown.region  = mWindow.regionOf(*doc);
    for (const ALScriptOutlineEntry& entry : doc->outline)
    {
        if (entry.kind == ALScriptSymbolKind::Event)
        {
            shown.handlers.emplace(entry.span.line, entry.name);
        }
    }
    // Weighed as the check has the text, which is before the optimizer
    // where one runs; what a save sends beside it, where it has been
    // weighed of the text as it stands.
    shown.beforeOptimizer = mAnalysis.preprocessed(*doc) && !doc->language.lua && mWindow.optimizing();
    if (doc->weighing->weight && doc->weighing->sent && doc->weighing->version == doc->weighing->allVersion)
    {
        shown.sent = doc->weighing->weight->total;
    }
    for (const ALScriptWeight& weight : doc->weighing->all)
    {
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            if (!part.file.empty() && !shown.fileNames.contains(part.file))
            {
                shown.fileNames[part.file] = mAnalysis.includeName(*doc, part.file);
            }
        }
    }
    pane->show(std::move(shown));
}

void ALScriptStudioWeighing::fixesShown(const std::string& id, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes)
{
    mFixesToWeigh = FixesToWeigh{ id, shown, fixes };
}

void ALScriptStudioWeighing::pump()
{
    // The fix list last shown, weighed once nothing more is coming to it;
    // dropped once it has closed.
    if (mFixesToWeigh)
    {
        Doc* doc = mServices.findDoc(mFixesToWeigh->id);
        if (!doc || !doc->editor->fixesOpen())
        {
            mFixesToWeigh.reset();
        }
        else if (!doc->editor->actionsAwaited())
        {
            const FixesToWeigh asked = std::move(*mFixesToWeigh);
            mFixesToWeigh.reset();
            weighFixes(*doc, asked.shown, asked.fixes);
        }
    }
    // The tab in front weighed once for its text, where its check was not
    // asked with it in front: only the front tab is weighed with its check,
    // and one come to the front since is weighed now.
    if (Doc* front = mServices.frontDoc(); front && target(*front))
    {
        const U32 version = front->editor->document().version();
        if (front->check->analysisVersion == version && front->weighing->askedFor != version)
        {
            weigh(*front);
        }
    }
    // The Weights tab, filled while it is looked at: with what came since,
    // or with the script now in front; and that script weighed for the
    // targets beside its own, which the tab alone asks for.
    const bool shown = mWindow.weightsShown();
    if (shown)
    {
        Doc*                 doc  = mServices.frontDoc();
        ALScriptWeightsPane* pane = mWindow.weightsPane();
        if (mStale || !mWasShown || (pane && (doc ? doc->id : std::string()) != pane->shownId()))
        {
            mStale = false;
            refreshPane();
        }
        if (doc && !asking(*doc) && doc->check->analysisVersion == doc->editor->document().version())
        {
            const U32 version = doc->editor->document().version();
            for (const ALScriptWeight::Target each : targets(*doc))
            {
                const auto same = [each](const ALScriptWeight& w) { return w.target == each; };
                const bool held =
                    doc->weighing->allVersion == version && std::any_of(doc->weighing->all.begin(), doc->weighing->all.end(), same);
                if (!held)
                {
                    weigh(*doc);
                    break;
                }
            }
        }
    }
    mWasShown = shown;
}

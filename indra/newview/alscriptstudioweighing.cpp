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

#include "alfloaterscriptstudio.h"

#include "alscriptenvelope.h"
#include "alscriptfixes.h"
#include "alscriptweightspane.h"
#include "lldate.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <cmath>
#include <map>

std::optional<ALScriptWeight::Target> ALFloaterScriptStudio::weightTarget(const Doc& doc) const
{
    if (!doc.loaded || doc.notecard || lslFragment(doc))
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

void ALFloaterScriptStudio::weigh(Doc& doc)
{
    if (!weightTarget(doc))
    {
        doc.weight.reset();
        return;
    }
    doc.weighing = true;
    askAnalyzer(doc, ALScriptAnalysis::Kind::Weigh, ALTextPos());
}

void ALFloaterScriptStudio::weighed(Doc& doc, const ALScriptAnalysis::Result& result)
{
    doc.weighing = false;
    if (result.version != doc.editor->document().version() || result.weights.empty())
    {
        return;
    }
    // In the source's places, through the expansion the question was
    // asked over -- which answered() has made sure is the one there is.
    doc.weights.clear();
    for (const ALScriptWeight& weight : result.weights)
    {
        doc.weights.push_back(preprocessed(doc) && doc.expanded.valid ? weight.inSource(doc.expanded.map) : weight);
    }
    doc.weightsVersion = result.version;
    keepSavedWeights(doc);
    if (&doc == active())
    {
        mWeightsStale = true;
    }
    // What a preprocessor's run made to be sent, weighed, says more of the
    // same text than its check does: it stands until the text changes.
    if (!doc.weightSent || doc.weightVersion != result.version)
    {
        doc.weight        = doc.weights.front();
        doc.weightVersion = result.version;
        doc.weightSent    = false;
        // What was weighed is what a save compiles where the preprocessor
        // does not run, or runs without the optimizer, which comes after
        // the check's expansion: SLua's is never optimized.
        doc.weightExact = !preprocessed(doc) || doc.language.lua || !gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
        refreshProblems(doc);
        showWeightsInEditor(doc);
    }
    mSaving.warnOverWeight(doc);
}

void ALFloaterScriptStudio::weighSent(Doc& doc)
{
    const std::optional<ALScriptWeight::Target> target = weightTarget(doc);
    if (!target || !doc.uploaded.valid || doc.uploaded.version != doc.editor->document().version())
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::Weigh;
    request.id      = doc.id;
    request.version = doc.uploaded.version;
    request.lua     = doc.language.lua;
    request.text    = doc.uploaded.disabled ? doc.editor->text() : doc.uploaded.text;
    request.targets = { *target };
    // What was weighed kept with the question, its map for the places: a
    // run since -- a setting changed while it was weighed -- is of another
    // text, and weighs its own.
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request), [handle, sent = doc.uploaded](const ALScriptAnalysis::Result& result) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(result.id) : NONE;
        if (index != NONE)
        {
            studio->weighedSent(*studio->mDocs[index], result, sent);
        }
    });
}

void ALFloaterScriptStudio::weighedSent(Doc& doc, const ALScriptAnalysis::Result& result, const Doc::Expanded& sent)
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
        doc.weight        = weight->inSource(sent.map);
        doc.weightVersion = result.version;
        doc.weightExact   = true;
        doc.weightSent    = true;
        refreshProblems(doc);
        showWeightsInEditor(doc);
        if (&doc == active())
        {
            mWeightsStale = true;
        }
    }
    mSaving.warnOverWeight(doc);
}

std::vector<ALScriptWeight::Target> ALFloaterScriptStudio::weighedTargets(const Doc& doc) const
{
    const std::optional<ALScriptWeight::Target> own = weightTarget(doc);
    if (!own)
    {
        return {};
    }
    std::vector<ALScriptWeight::Target> targets = { *own };
    // Three compiles where one would do are the analyzer's time that a
    // completion waits behind: the other two only while they are looked
    // at.
    const bool in_front = mActive < mDocs.size() && mDocs[mActive].get() == &doc;
    if (!doc.language.lua && in_front && weightsShown())
    {
        for (const ALScriptWeight::Target other : { ALScriptWeight::Target::LSO, ALScriptWeight::Target::Mono, ALScriptWeight::Target::LSLLuau })
        {
            if (other != *own)
            {
                targets.push_back(other);
            }
        }
    }
    return targets;
}

void ALFloaterScriptStudio::keepSavedWeights(Doc& doc)
{
    if (doc.editor->isDirty() || doc.weightsVersion != doc.editor->document().version())
    {
        return;
    }
    // Each target's in place of what it weighed before, the others kept: a
    // target weighed only while the tab was looked at is counted from what
    // it was then.
    for (const ALScriptWeight& weight : doc.weights)
    {
        auto held = std::find_if(doc.weightsSaved.begin(), doc.weightsSaved.end(), [&weight](const ALScriptWeight& w) { return w.target == weight.target; });
        if (held != doc.weightsSaved.end())
        {
            *held = weight;
        }
        else
        {
            doc.weightsSaved.push_back(weight);
        }
    }
    if (&doc == active())
    {
        mWeightsStale = true;
    }
}

void ALFloaterScriptStudio::showWeightsInEditor(Doc& doc)
{
    if (!doc.editor)
    {
        return;
    }
    if (!mWeightNotes)
    {
        doc.editor->setLineNotes({});
    }
    if (!mWeightHeat)
    {
        doc.editor->setLineHeat({});
    }
    if ((!mWeightNotes && !mWeightHeat) || !doc.weight || doc.weightVersion != doc.editor->document().version())
    {
        return;
    }
    const ALScriptWeight&      weight = *doc.weight;
    LLStringUtil::format_map_t args;
    args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
    args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
    const auto share = [&weight](size_t bytes) { return weight.limit ? llformat("%.1f%%", (F64)bytes * 100.0 / (F64)weight.limit) : std::string(); };
    // Said as weighed before the optimizer, where it was.
    const std::string before = doc.weightExact ? std::string() : " " + getString("WeightsHeadBefore");
    if (mWeightNotes)
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
                    said["[NAME]"] = getString("WeightsPartUnnamed");
                }
                else if (!part->within.empty())
                {
                    LLStringUtil::format_map_t handler;
                    handler["[EVENT]"] = part->name;
                    handler["[STATE]"] = part->within;
                    said["[NAME]"]     = getString("WeightsPartHandler", handler);
                }
                else
                {
                    said["[NAME]"] = part->name;
                }
                std::string bytes = getString(weight.estimate ? "WeightNoteEstimate" : "WeightNote", said);
                if (parts.size() > 1)
                {
                    said["[NOTE]"] = bytes;
                    bytes          = getString("WeightNoteNamed", said);
                }
                note.text += (note.text.empty() ? "" : "  \xC2\xB7  ") + bytes;
                note.tip += (note.tip.empty() ? "" : "\n") + getString(weight.estimate ? "WeightNoteEstimateTip" : "WeightNoteTip", said);
            }
            note.tip += before;
            notes.push_back(std::move(note));
        }
        doc.editor->setLineNotes(notes);
    }
    if (mWeightHeat)
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
                             getString(weight.estimate ? "WeightHeatEstimateTip" : "WeightHeatTip", said) + before });
        }
        doc.editor->setLineHeat(heat);
    }
}

bool ALFloaterScriptStudio::editedCopy(const Doc& doc, const std::vector<std::pair<ALTextRange, std::string>>& edits, std::string& out) const
{
    ALScriptFix fix;
    for (const auto& [range, text] : edits)
    {
        const ALTextRange at = range.normalised();
        fix.edits.push_back({ at.begin.line, at.begin.column, at.end.line, at.end.column, text });
    }
    if (preprocessed(doc) && !ALScriptFixes::intoExpansion(doc.expanded.map, fix))
    {
        return false;
    }
    const std::optional<std::string> made = ALScriptFixes::apply(preprocessed(doc) ? doc.expanded.text : doc.editor->text(), fix);
    if (!made)
    {
        return false;
    }
    out = *made;
    return true;
}

void ALFloaterScriptStudio::weighFixes(Doc& doc, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes)
{
    const std::optional<ALScriptWeight::Target> target = weightTarget(doc);
    const U32                                   version = doc.editor->document().version();
    if (!target || (preprocessed(doc) && (!doc.expanded.valid || doc.expanded.version != version)))
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::Weigh;
    request.id      = doc.id;
    request.version = version;
    request.lua     = doc.language.lua;
    request.targets = { *target };
    // What the analyzers read as it stands first, weighed with the rest so
    // that each is measured against the same weigher at the same moment.
    request.variants.push_back(preprocessed(doc) ? doc.expanded.text : doc.editor->text());
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
    const LLHandle<LLFloater> handle = getHandle();
    const ALScriptWeight::Target weighed_for = *target;
    ALScriptAnalysis::instance().ask(std::move(request), [handle, shown, variant_of, weighed_for](const ALScriptAnalysis::Result& result) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(result.id) : NONE;
        if (index == NONE)
        {
            return;
        }
        Doc& doc = *studio->mDocs[index];
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
            notes[i]        = studio->getString(change < 0 ? (estimate ? "FixLighterEstimate" : "FixLighter") : (estimate ? "FixHeavierEstimate" : "FixHeavier"), args);
        }
        doc.editor->noteFixes(shown, notes);
    });
}

void ALFloaterScriptStudio::measureAsset(Doc& doc)
{
    const U32 version   = doc.editor->document().version();
    const U32 expansion = doc.expanded.valid ? doc.expanded.generation : 0;
    if (doc.assetMeasured == std::make_pair(version, expansion))
    {
        return;
    }
    doc.assetMeasured = std::make_pair(version, expansion);
    doc.assetBytes    = 0;
    // A file on disk is saved to the disk, which has no limit.
    if (!doc.loaded || !doc.file.empty())
    {
        return;
    }
    // A notecard's text against the notecard's own limit.
    if (doc.notecard)
    {
        doc.assetBytes = doc.editor->document().byteCount();
        return;
    }
    const std::string text = doc.editor->text();
    doc.assetBytes         = text.size();
    if (preprocessed(doc) && doc.expanded.valid && doc.expanded.version == version && !doc.expanded.disabled)
    {
        ALScriptEnvelope envelope = doc.envelope ? *doc.envelope : ALScriptEnvelope();
        envelope.lua              = doc.language.lua;
        envelope.source           = text;
        envelope.expanded         = doc.expanded.text;
        envelope.compileTarget    = doc.language.compileTarget;
        envelope.programVersion   = LLVersionInfo::instance().getChannelAndVersion();
        envelope.lastCompiled     = LLDate::now().asString();
        doc.assetBytes            = envelope.wrap().size();
    }
}

void ALFloaterScriptStudio::refreshWeights()
{
    Doc* doc = active();
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc ? doc->name : std::string();
    if (!doc)
    {
        mWeightsPane->showNothing(getString("WeightsNoScript"));
        return;
    }
    if (doc->loaded && !weightTarget(*doc))
    {
        mWeightsPane->showNothing(getString("WeightsNoTarget", args));
        return;
    }
    if (!doc->loaded || doc->weights.empty())
    {
        mWeightsPane->showNothing(getString("WeightsNotYet", args));
        return;
    }
    ALScriptWeightsPane::Shown shown;
    shown.id      = doc->id;
    shown.name    = doc->name;
    shown.weights = doc->weights;
    shown.saved   = doc->weightsSaved;
    // Weighed as the check has the text, which is before the optimizer
    // where one runs; what a save sends beside it, where it has been
    // weighed of the text as it stands.
    shown.beforeOptimizer = preprocessed(*doc) && !doc->language.lua && gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
    if (doc->weight && doc->weightSent && doc->weightVersion == doc->weightsVersion)
    {
        shown.sent = doc->weight->total;
    }
    for (const ALScriptWeight& weight : doc->weights)
    {
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            if (!part.file.empty() && !shown.fileNames.contains(part.file))
            {
                shown.fileNames[part.file] = includeName(*doc, part.file);
            }
        }
    }
    mWeightsPane->show(std::move(shown));
}

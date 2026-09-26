/**
 * @file alscriptcrumbsbar.cpp
 * @brief Script Studio's bar under the editor: the path of symbols the caret is in, and the words past it.
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

#include "aljumpbar.h"
#include "alscriptenvelope.h"
#include "alscriptstudioplaces.h"
#include "llnotecard.h"

using ALScriptPlaces::outlineEntryOf;
using ALScriptPlaces::outlineValue;
using ALScriptPlaces::rangeOf;

namespace
{
    // Whether a span holds a position, its ends included.
    bool holds(const ALScriptSpan& span, const ALTextPos& pos)
    {
        const ALTextPos begin(span.line, span.column);
        const ALTextPos end(span.endLine, span.endColumn);
        return begin <= pos && pos <= end;
    }

    // Whether a span lies within another.
    bool within(const ALScriptSpan& inner, const ALScriptSpan& outer)
    {
        return holds(outer, ALTextPos(inner.line, inner.column)) && holds(outer, ALTextPos(inner.endLine, inner.endColumn));
    }
}

void ALFloaterScriptStudio::refreshTrailer(Doc& doc)
{
    // The view in front's caret: the expansion's own line, while it is
    // the one being read.
    const ALCodeEditor&         shown = *doc.shownText();
    const ALTextPos             caret = shown.caret();
    const ALTextDocument&       text  = shown.document();
    LLStringUtil::format_map_t args;
    args["[LINE]"]  = std::to_string(caret.line + 1);
    // Where the caret is as it is seen -- a character a column, a tab to
    // its stop -- rather than its byte in the line.
    args["[COL]"]   = std::to_string(text.displayColumn(caret, shown.getTabWidth()) + 1);
    std::vector<ALJumpBar::TrailerPart> parts;
    // A script that may be read and not changed says so for as long as it
    // is in front, not only in the status line as it arrives.
    if (doc.loaded && !doc.modifiable)
    {
        parts.push_back({ getString("TrailerReadOnly"), std::string(), getString("TrailerReadOnlyTip") });
    }
    if (!mVim.banner().empty())
    {
        parts.push_back({ mVim.banner(), std::string(), std::string() });
    }
    parts.push_back({ getString("CaretPosition", args), "line", mTrailerLineTip });
    // What is selected: lines across lines, characters within one.
    const ALTextRange selection = shown.selection().normalised();
    if (!selection.empty())
    {
        if (selection.begin.line != selection.end.line)
        {
            const S32 lines = selection.end.line - selection.begin.line + (selection.end.column > 0 ? 1 : 0);
            parts.push_back({ counted("SelectedLines", lines), std::string(), std::string() });
        }
        else
        {
            // Characters as they are seen, not the bytes they are
            // written in; a tab is one.
            parts.push_back({ counted("SelectedChars", text.displayColumn(selection.end, 1) - text.displayColumn(selection.begin, 1)), std::string(),
                              std::string() });
        }
    }
    S32 errors = 0, warnings = 0;
    problemCounts(doc, errors, warnings);
    if (errors > 0)
    {
        parts.push_back({ counted("ProblemErrors", errors), "problems", mTrailerProblemsTip });
    }
    if (warnings > 0)
    {
        parts.push_back({ counted("ProblemWarnings", warnings), "problems", mTrailerProblemsTip });
    }
    // While the Preprocessed view is in front, where the optimizer ran over
    // the text as it stands: what its code weighed before the optimizer and
    // after, the whole of what that view shows it did.
    const bool optimized = doc.shownView() == Doc::View::Expanded && doc.uploaded.valid && doc.uploaded.version == doc.editor->document().version() &&
                           doc.uploaded.codeBefore > 0 && doc.uploaded.codeAfter > 0 && weightTarget(doc);
    if (optimized)
    {
        const ALScriptWeight::Target target = *weightTarget(doc);
        const size_t                 limit  = ALScriptWeight::limitOf(target);
        LLStringUtil::format_map_t   args;
        args["[TARGET]"]      = ALScriptWeight::nameOf(target);
        args["[BEFORE]"]      = llformat("%.1f", (F64)doc.uploaded.codeBefore / 1024.0);
        args["[AFTER]"]       = llformat("%.1f", (F64)doc.uploaded.codeAfter / 1024.0);
        args["[LIMIT]"]       = std::to_string(limit / 1024);
        args["[BYTESBEFORE]"] = std::to_string(doc.uploaded.codeBefore);
        args["[BYTESAFTER]"]  = std::to_string(doc.uploaded.codeAfter);
        args["[MAX]"]         = std::to_string(limit);
        const bool estimate   = target == ALScriptWeight::Target::Mono;
        ALJumpBar::TrailerPart part{ getString(estimate ? "TrailerOptimizedEstimate" : "TrailerOptimized", args), std::string(),
                                     getString(estimate ? "TrailerOptimizedEstimateTip" : "TrailerOptimizedTip", args) };
        if (doc.uploaded.codeAfter > limit)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (doc.uploaded.codeAfter * 5 > limit * 4)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // What its code weighs for its target, against what the target runs
    // it in: in the warning colour past four fifths, the error's past it.
    if (!optimized && doc.weighing.weight && doc.weighing.weight->total > 0)
    {
        const ALScriptWeight&      weight = *doc.weighing.weight;
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
        args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
        args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
        args["[BYTES]"]  = std::to_string(weight.total);
        args["[MAX]"]    = std::to_string(weight.limit);
        std::string tip  = getString(weight.estimate ? "TrailerWeightEstimateTip" : "TrailerWeightTip", args);
        if (!doc.weighing.exact)
        {
            tip += " " + getString("TrailerWeightBeforeTip");
        }
        else if (doc.weighing.sent)
        {
            tip += " " + getString("TrailerWeightSentTip");
        }
        ALJumpBar::TrailerPart part{ getString(weight.estimate ? "TrailerWeightEstimate" : "TrailerWeight", args), std::string(), tip };
        if (weight.total > weight.limit)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (weight.total * 5 > weight.limit * 4)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // What a save would send, once it is past half of what a script -- or
    // a notecard's text -- may be: in the warning colour past nine tenths,
    // the error's past the whole, where a save is refused.
    const size_t LIMIT = doc.notecard ? static_cast<size_t>(LLNotecard::MAX_SIZE) : ALScriptEnvelope::MAX_ASSET_BYTES;
    if (doc.weighing.assetBytes * 2 > LIMIT)
    {
        LLStringUtil::format_map_t size;
        size["[SIZE]"]  = std::to_string((doc.weighing.assetBytes + 1023) / 1024);
        size["[LIMIT]"] = std::to_string(LIMIT / 1024);
        size["[BYTES]"] = std::to_string(doc.weighing.assetBytes);
        size["[MAX]"]   = std::to_string(LIMIT);
        size["[OVER]"]  = std::to_string(doc.weighing.assetBytes > LIMIT ? doc.weighing.assetBytes - LIMIT : 0);
        const bool             over = doc.weighing.assetBytes > LIMIT;
        const char*            tip  = doc.notecard ? (over ? "TrailerNotecardSizeOverTip" : "TrailerNotecardSizeTip")
                                                   : (over ? "TrailerSizeOverTip" : "TrailerSizeTip");
        ALJumpBar::TrailerPart part{ getString("TrailerSize", size), std::string(), getString(tip, size) };
        if (doc.weighing.assetBytes > LIMIT)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (doc.weighing.assetBytes * 10 > LIMIT * 9)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // Which of the two it is, where the script has an expansion to show:
    // pressed, the other.
    if (doc.expandedEditor)
    {
        const bool expanded = doc.shownView() == Doc::View::Expanded;
        parts.push_back({ getString(expanded ? "TrailerExpanded" : "TrailerSource"), "expanded", expanded ? mTrailerExpandedTip : mTrailerSourceTip });
    }
    // Joined by a middle dot with air around it; in code, since a
    // string of the skin's is trimmed of its spaces.
    std::vector<ALJumpBar::TrailerPart> said;
    for (ALJumpBar::TrailerPart& part : parts)
    {
        if (!said.empty())
        {
            said.push_back({ "   \xC2\xB7   ", std::string(), std::string() });
        }
        said.push_back(std::move(part));
    }
    mBreadcrumb->setTrailer(std::move(said));
}

void ALFloaterScriptStudio::onTrailerChosen(const std::string& value)
{
    if (value == "line")
    {
        goToLine();
    }
    else if (value == "problems")
    {
        showBottom("problems_tab", true);
    }
    else if (value == "expanded")
    {
        toggleExpanded();
    }
}

void ALFloaterScriptStudio::refreshBreadcrumb(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    const ALTextPos               caret = doc.editor->caret();
    // The path the caret is in: which outline entry at each depth holds
    // it. The crumbs are built from the outline, which a caret move
    // does not touch, so the bar is told only where the path itself has
    // changed -- and it is asked on every key. The outline is the
    // source's, so while the expansion is in front, whose lines are other
    // lines, the path is the script alone.
    std::vector<size_t> path;
    if (doc.shownView() == Doc::View::Source)
    {
        size_t parent = NONE;
        for (S32 depth = 0;; ++depth)
        {
            size_t found = NONE;
            for (size_t i = 0; i < doc.outline.size(); ++i)
            {
                const ALScriptOutlineEntry& entry = doc.outline[i];
                if (entry.depth == depth && holds(entry.span, caret) && (parent == NONE || within(entry.span, doc.outline[parent].span)))
                {
                    found = i;
                }
            }
            if (found == NONE)
            {
                break;
            }
            path.push_back(found);
            parent = found;
        }
    }
    if (mCrumbsShownFor == doc.id && doc.crumbsOf == doc.analysisVersion && doc.crumbPath == path && doc.crumbName == doc.name)
    {
        // The same steps over the same outline: only the trailer, which
        // says where the caret is.
        refreshTrailer(doc);
        return;
    }
    doc.crumbsOf  = doc.analysisVersion;
    doc.crumbPath = path;
    doc.crumbName = doc.name;
    mOutlinePane->followCaret(doc);

    std::vector<ALJumpBar::Crumb> crumbs;
    LLStringUtil::format_map_t    args;

    // The script itself, offering what it declares at the top.
    ALJumpBar::Crumb root;
    root.label     = doc.name;
    root.value     = "top";
    args["[NAME]"] = doc.name;
    root.toolTip   = getString("CrumbRootTip", args);
    for (size_t i = 0; i < doc.outline.size(); ++i)
    {
        if (doc.outline[i].depth == 0)
        {
            root.alternatives.emplace_back(doc.outline[i].name, outlineValue(doc, i));
        }
    }
    crumbs.push_back(std::move(root));

    // Then each symbol on the path, the outermost first, offering the
    // others at its depth in the same holder.
    size_t parent = NONE;
    for (S32 depth = 0; depth < static_cast<S32>(path.size()); ++depth)
    {
        const size_t found = path[static_cast<size_t>(depth)];
        ALJumpBar::Crumb crumb;
        crumb.label    = doc.outline[found].name;
        crumb.value    = outlineValue(doc, found);
        args["[NAME]"] = crumb.label;
        crumb.toolTip  = getString("CrumbTip", args);
        for (size_t i = 0; i < doc.outline.size(); ++i)
        {
            const ALScriptOutlineEntry& entry = doc.outline[i];
            if (entry.depth == depth && (parent == NONE || within(entry.span, doc.outline[parent].span)))
            {
                crumb.alternatives.emplace_back(entry.name, outlineValue(doc, i));
            }
        }
        if (crumb.alternatives.size() < 2)
        {
            crumb.alternatives.clear();
        }
        crumbs.push_back(std::move(crumb));
        parent = found;
    }
    mBreadcrumb->setPath(std::move(crumbs));
    mCrumbsShownFor = doc.id;
    refreshTrailer(doc);
}

void ALFloaterScriptStudio::onCrumbChosen(size_t, const std::string& value)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    mNavigation.noteJump();
    ALCodeEditor& source = sourceInFront(*doc);
    if (value == "top")
    {
        source.goTo(ALTextPos(0, 0));
    }
    else if (const size_t index = outlineEntryOf(*doc, value); index != NONE)
    {
        source.goTo(rangeOf(doc->outline[index].nameSpan));
    }
    source.setFocus(true);
}

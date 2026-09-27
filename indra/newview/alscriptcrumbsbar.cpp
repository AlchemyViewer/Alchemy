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

#include "alscriptcrumbsbar.h"

#include "alcodeeditor.h"
#include "alscriptenvelope.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "llfloater.h"
#include "llmenugl.h"
#include "lluictrlfactory.h"
#include "llnotecard.h"

using ALScriptPlaces::NONE;
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

static LLPanelInjector<ALScriptCrumbsBar> t_script_studio_crumbs("script_studio_crumbs");

ALScriptCrumbsBar::ALScriptCrumbsBar(const LLPanel::Params& params) : LLPanel(params) {}

ALScriptCrumbsBar::~ALScriptCrumbsBar()
{
    // A menu still open calls into this bar, which is going: it goes
    // first. The menus live in the viewer's menu holder, not here.
    if (LLContextMenu* open = mIndentMenu.get())
    {
        open->hide();
        open->die();
    }
}

bool ALScriptCrumbsBar::postBuild()
{
    mBar = getChild<ALJumpBar>("breadcrumb");
    // The window this is the bar of, found through the view tree, as what
    // the bar asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The bar under the editor is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    mBar->onChose([this](size_t, const std::string& value) { choose(value); });
    mBar->onTrailerChosen([this](const std::string& value) {
        if (value == "indent")
        {
            showIndentMenu();
            return;
        }
        mWindow->trailerChosen(value);
    });
    return true;
}

void ALScriptCrumbsBar::showTrailer(Doc& doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices)
    {
        return;
    }
    std::vector<Part> parts;
    place(doc, parts);
    selection(doc, parts);
    indentation(doc, parts);
    problems(doc, parts);
    weight(doc, parts);
    sending(doc, parts);
    views(doc, parts);
    // Joined by a middle dot with air around it; in code, since a
    // string of the skin's is trimmed of its spaces.
    std::vector<Part> said;
    for (Part& part : parts)
    {
        if (!said.empty())
        {
            said.push_back({ "   \xC2\xB7   ", std::string(), std::string() });
        }
        said.push_back(std::move(part));
    }
    mBar->setTrailer(std::move(said));
}

void ALScriptCrumbsBar::place(Doc& doc, std::vector<Part>& parts) const
{
    // A script that may be read and not changed says so for as long as it
    // is in front, not only in the status line as it arrives.
    if (doc.loaded && !doc.modifiable)
    {
        parts.push_back({ mServices->words("TrailerReadOnly"), std::string(), mServices->words("TrailerReadOnlyTip") });
    }
    const std::string banner = mWindow->vimBanner();
    if (!banner.empty())
    {
        parts.push_back({ banner, std::string(), std::string() });
    }
    // The view in front's caret: the expansion's own line, while it is
    // the one being read.
    const ALCodeEditor&        shown = *doc.shownText();
    const ALTextPos            caret = shown.caret();
    LLStringUtil::format_map_t args;
    args["[LINE]"] = std::to_string(caret.line + 1 + shown.lineNumberBase());
    // Where the caret is as it is seen -- a character a column, a tab to
    // its stop -- rather than its byte in the line.
    args["[COL]"]  = std::to_string(shown.document().displayColumn(caret, shown.getTabWidth()) + 1);
    parts.push_back({ mServices->words("CaretPosition", args), "line", mTips.line });
}

void ALScriptCrumbsBar::selection(Doc& doc, std::vector<Part>& parts) const
{
    // What is selected: lines across lines, characters within one.
    const ALCodeEditor&   shown    = *doc.shownText();
    const ALTextDocument& text     = shown.document();
    const ALTextRange     selected = shown.selection().normalised();
    if (selected.empty())
    {
        return;
    }
    if (selected.begin.line != selected.end.line)
    {
        const S32 lines = selected.end.line - selected.begin.line + (selected.end.column > 0 ? 1 : 0);
        parts.push_back({ mServices->counted("SelectedLines", lines), std::string(), std::string() });
    }
    else
    {
        // Characters as they are seen, not the bytes they are
        // written in; a tab is one.
        const S32 chars = text.displayColumn(selected.end, 1) - text.displayColumn(selected.begin, 1);
        parts.push_back({ mServices->counted("SelectedChars", chars), std::string(), std::string() });
    }
}

void ALScriptCrumbsBar::problems(Doc& doc, std::vector<Part>& parts) const
{
    S32 errors = 0, warnings = 0;
    mWindow->problemCounts(doc, errors, warnings);
    if (errors > 0)
    {
        parts.push_back({ mServices->counted("ProblemErrors", errors), "problems", mTips.problems });
    }
    if (warnings > 0)
    {
        parts.push_back({ mServices->counted("ProblemWarnings", warnings), "problems", mTips.problems });
    }
}

void ALScriptCrumbsBar::weight(Doc& doc, std::vector<Part>& parts) const
{
    // While the Preprocessed view is in front, where the optimizer ran over
    // the text as it stands: what its code weighed before the optimizer and
    // after, the whole of what that view shows it did.
    const std::optional<ALScriptWeight::Target> target = mWindow->weightTarget(doc);
    const bool optimized = doc.shownView() == Doc::View::Expanded && doc.uploaded.valid &&
                           doc.uploaded.version == doc.editor->document().version() && doc.uploaded.codeBefore > 0 &&
                           doc.uploaded.codeAfter > 0 && target;
    if (optimized)
    {
        const size_t               limit = ALScriptWeight::limitOf(*target);
        LLStringUtil::format_map_t args;
        args["[TARGET]"]      = ALScriptWeight::nameOf(*target);
        args["[BEFORE]"]      = llformat("%.1f", (F64)doc.uploaded.codeBefore / 1024.0);
        args["[AFTER]"]       = llformat("%.1f", (F64)doc.uploaded.codeAfter / 1024.0);
        args["[LIMIT]"]       = std::to_string(limit / 1024);
        args["[BYTESBEFORE]"] = std::to_string(doc.uploaded.codeBefore);
        args["[BYTESAFTER]"]  = std::to_string(doc.uploaded.codeAfter);
        args["[MAX]"]         = std::to_string(limit);
        const bool estimate   = *target == ALScriptWeight::Target::Mono;
        Part       part{ mServices->words(estimate ? "TrailerOptimizedEstimate" : "TrailerOptimized", args), std::string(),
                   mServices->words(estimate ? "TrailerOptimizedEstimateTip" : "TrailerOptimizedTip", args) };
        if (doc.uploaded.codeAfter > limit)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (doc.uploaded.codeAfter * 5 > limit * 4)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
        return;
    }
    // What its code weighs for its target, against what the target runs
    // it in: in the warning colour past four fifths, the error's past it.
    if (!doc.weighing.weight || doc.weighing.weight->total <= 0)
    {
        return;
    }
    const ALScriptWeight&      weighed = *doc.weighing.weight;
    LLStringUtil::format_map_t args;
    args["[TARGET]"] = ALScriptWeight::nameOf(weighed.target);
    args["[SIZE]"]   = llformat("%.1f", (F64)weighed.total / 1024.0);
    args["[LIMIT]"]  = std::to_string(weighed.limit / 1024);
    args["[BYTES]"]  = std::to_string(weighed.total);
    args["[MAX]"]    = std::to_string(weighed.limit);
    std::string tip  = mServices->words(weighed.estimate ? "TrailerWeightEstimateTip" : "TrailerWeightTip", args);
    if (!doc.weighing.exact)
    {
        tip += " " + mServices->words("TrailerWeightBeforeTip");
    }
    else if (doc.weighing.sent)
    {
        tip += " " + mServices->words("TrailerWeightSentTip");
    }
    Part part{ mServices->words(weighed.estimate ? "TrailerWeightEstimate" : "TrailerWeight", args), std::string(), tip };
    if (weighed.total > weighed.limit)
    {
        part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
    }
    else if (weighed.total * 5 > weighed.limit * 4)
    {
        part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
    }
    parts.push_back(std::move(part));
}

void ALScriptCrumbsBar::sending(Doc& doc, std::vector<Part>& parts) const
{
    // What a save would send, once it is past half of what a script -- or
    // a notecard's text -- may be: in the warning colour past nine tenths,
    // the error's past the whole, where a save is refused.
    const size_t LIMIT = doc.notecard ? static_cast<size_t>(LLNotecard::MAX_SIZE) : ALScriptEnvelope::MAX_ASSET_BYTES;
    if (doc.weighing.assetBytes * 2 <= LIMIT)
    {
        return;
    }
    LLStringUtil::format_map_t size;
    size["[SIZE]"]  = std::to_string((doc.weighing.assetBytes + 1023) / 1024);
    size["[LIMIT]"] = std::to_string(LIMIT / 1024);
    size["[BYTES]"] = std::to_string(doc.weighing.assetBytes);
    size["[MAX]"]   = std::to_string(LIMIT);
    size["[OVER]"]  = std::to_string(doc.weighing.assetBytes > LIMIT ? doc.weighing.assetBytes - LIMIT : 0);
    const bool  over = doc.weighing.assetBytes > LIMIT;
    const char* tip  = doc.notecard ? (over ? "TrailerNotecardSizeOverTip" : "TrailerNotecardSizeTip")
                                    : (over ? "TrailerSizeOverTip" : "TrailerSizeTip");
    Part        part{ mServices->words("TrailerSize", size), std::string(), mServices->words(tip, size) };
    if (over)
    {
        part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
    }
    else if (doc.weighing.assetBytes * 10 > LIMIT * 9)
    {
        part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
    }
    parts.push_back(std::move(part));
}

void ALScriptCrumbsBar::indentation(Doc& doc, std::vector<Part>& parts) const
{
    // How the script is indented -- a tab's width, or a level's spaces --
    // and, on the tip, where that was said.
    if (!doc.loaded)
    {
        return;
    }
    const ALCodeEditor&        editor = *doc.editor;
    LLStringUtil::format_map_t args;
    args["[WIDTH]"] = std::to_string(editor.getTabWidth());
    const ALTextView::IndentFrom from = editor.indentFrom();
    const char* tip = from == ALTextView::IndentFrom::Text ? "TrailerIndentTipText" : from == ALTextView::IndentFrom::Chosen ? "TrailerIndentTipChosen" : "TrailerIndentTipDefaults";
    parts.push_back({ mServices->words(editor.getSoftTabs() ? "TrailerIndentSpaces" : "TrailerIndentTabs", args), "indent", mServices->words(tip) });
}

void ALScriptCrumbsBar::showIndentMenu()
{
    if (!LLMenuGL::sMenuContainer || !mServices || !mServices->frontDoc())
    {
        return;
    }
    if (LLContextMenu* old = mIndentMenu.get())
    {
        old->die();
        mIndentMenu.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Indent.Action", [this](LLUICtrl*, const LLSD& param) { indentAct(param.asString()); });
    enable.add("Indent.Enable", [this](LLUICtrl*, const LLSD& param) { return indentEnabled(param.asString()); });
    enable.add("Indent.Check", [this](LLUICtrl*, const LLSD& param) { return indentChecked(param.asString()); });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_indent.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    // Back to the script's own, where scripts are read for it, or to the
    // default.
    if (LLMenuItemGL* read = menu->findChild<LLMenuItemGL>("read"))
    {
        read->setLabel(mServices->words(mServices->frontDoc()->editor->readsIndentation() ? "IndentReadAgain" : "IndentToDefault"));
    }
    // Where the bar was pressed.
    S32 x = 0;
    S32 y = 0;
    LLUI::getInstance()->getMousePositionLocal(mBar, &x, &y);
    mIndentMenu = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(mBar, menu, x, y);
}

void ALScriptCrumbsBar::indentAct(const std::string& action)
{
    Doc* doc = mServices ? mServices->frontDoc() : nullptr;
    if (!doc || !doc->loaded)
    {
        return;
    }
    ALCodeEditor& editor = *doc->editor;
    if (action == "spaces" || action == "tabs")
    {
        editor.setSoftTabs(action == "spaces");
    }
    else if (action.rfind("width_", 0) == 0)
    {
        editor.setTabWidth(std::stoi(action.substr(6)));
    }
    else if (action == "read")
    {
        editor.readIndentation();
    }
    else if ((action == "convert_spaces" || action == "convert_tabs") && indentEnabled(action))
    {
        // The whole script, and what it is indented by from here on.
        const bool spaces = action == "convert_spaces";
        editor.convertIndentation(0, editor.document().lineCount() - 1, spaces);
        editor.setSoftTabs(spaces);
    }
    showTrailer(*doc);
}

bool ALScriptCrumbsBar::indentEnabled(const std::string& action) const
{
    const Doc* doc = mServices ? mServices->frontDoc() : nullptr;
    if (!doc || !doc->loaded)
    {
        return false;
    }
    if (action == "read")
    {
        return doc->editor->indentFrom() == ALTextView::IndentFrom::Chosen;
    }
    if (action == "convert_spaces" || action == "convert_tabs")
    {
        return doc->modifiable && doc->shownView() == Doc::View::Source;
    }
    return true;
}

bool ALScriptCrumbsBar::indentChecked(const std::string& action) const
{
    const Doc* doc = mServices ? mServices->frontDoc() : nullptr;
    if (!doc)
    {
        return false;
    }
    const ALCodeEditor& editor = *doc->editor;
    if (action == "spaces" || action == "tabs")
    {
        return editor.getSoftTabs() == (action == "spaces");
    }
    if (action.rfind("width_", 0) == 0)
    {
        return std::to_string(editor.getTabWidth()) == action.substr(6);
    }
    return false;
}

void ALScriptCrumbsBar::views(Doc& doc, std::vector<Part>& parts) const
{
    // Which of the two it is, where the script has an expansion to show:
    // pressed, the other.
    if (doc.expandedEditor)
    {
        const bool expanded = doc.shownView() == Doc::View::Expanded;
        parts.push_back(
            { mServices->words(expanded ? "TrailerExpanded" : "TrailerSource"), "expanded", expanded ? mTips.expanded : mTips.source });
    }
}

void ALScriptCrumbsBar::showPath(Doc& doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices || &doc != mServices->frontDoc())
    {
        return;
    }
    const ALTextPos caret = doc.editor->caret();
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
    if (mShownFor == doc.id && doc.caret.crumbsOf == doc.check.analysisVersion && doc.caret.crumbPath == path &&
        doc.caret.crumbName == doc.name)
    {
        // The same steps over the same outline: only the trailer, which
        // says where the caret is.
        showTrailer(doc);
        return;
    }
    doc.caret.crumbsOf  = doc.check.analysisVersion;
    doc.caret.crumbPath = path;
    doc.caret.crumbName = doc.name;
    mWindow->pathChanged(doc);

    std::vector<ALJumpBar::Crumb> crumbs;
    LLStringUtil::format_map_t    args;

    // The script itself, offering what it declares at the top.
    ALJumpBar::Crumb root;
    root.label     = doc.name;
    root.value     = "top";
    args["[NAME]"] = doc.name;
    root.toolTip   = mServices->words("CrumbRootTip", args);
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
        const size_t     found = path[static_cast<size_t>(depth)];
        ALJumpBar::Crumb crumb;
        crumb.label    = doc.outline[found].name;
        crumb.value    = outlineValue(doc, found);
        args["[NAME]"] = crumb.label;
        crumb.toolTip  = mServices->words("CrumbTip", args);
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
    mBar->setPath(std::move(crumbs));
    mShownFor = doc.id;
    showTrailer(doc);
}

void ALScriptCrumbsBar::choose(const std::string& value)
{
    Doc* doc = mServices ? mServices->frontDoc() : nullptr;
    if (!doc)
    {
        return;
    }
    // The script's top, or the symbol by where it was and what it is
    // called; nothing, where it is gone.
    std::optional<ALTextRange> at;
    if (value == "top")
    {
        at = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0));
    }
    else if (const size_t index = outlineEntryOf(*doc, value); index != NONE)
    {
        at = rangeOf(doc->outline[index].nameSpan);
    }
    mWindow->crumbChosen(*doc, at);
}

void ALScriptCrumbsBar::forget()
{
    mBar->setPath({});
    mBar->setTrailer(LLStringUtil::null);
    mShownFor.clear();
}

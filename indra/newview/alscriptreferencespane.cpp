/**
 * @file alscriptreferencespane.cpp
 * @brief Script Studio's References tab: the places a name was found, listed, slid with edits, and chosen.
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

#include "alscriptreferencespane.h"

#include "alpanelist.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "llfloater.h"
#include "llscrolllistcell.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include <algorithm>
#include <set>

static LLPanelInjector<ALScriptReferencesPane> t_script_studio_references("script_studio_references");

ALScriptReferencesPane::ALScriptReferencesPane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptReferencesPane::postBuild()
{
    mList = getChild<ALPaneList>("references");
    mHead = getChild<LLTextBox>("references_head");
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The References tab is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) { choose(false); });
    // Return and a double-click go to the place chosen, to type there, and
    // escape goes back to the script without going anywhere.
    mList->setGo([this]() { choose(true); });
    mList->setBack([this]() { mServices->revealed(mList, true); });
    mList->setCopyable(true);
    mList->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) {
        const size_t i = placeWith(static_cast<U32>(a->getValue().asInteger()), false);
        const size_t j = placeWith(static_cast<U32>(b->getValue().asInteger()), false);
        if (i >= mFound.places.size() || j >= mFound.places.size())
        {
            return 0;
        }
        const Doc::Place& x    = mFound.places[i];
        const Doc::Place& y    = mFound.places[j];
        const auto        cell = [](const LLScrollListItem* item, S32 at) {
            return item->getColumn(at) ? item->getColumn(at)->getValue().asString() : std::string();
        };
        S32 said = 0;
        switch (column)
        {
            case 0: said = LLStringUtil::compareDict(cell(a, 0), cell(b, 0)); break;
            case 2: said = cell(b, 2).size() < cell(a, 2).size() ? -1 : cell(b, 2).size() > cell(a, 2).size() ? 1 : 0; break;
            case 3: said = LLStringUtil::compareDict(x.text, y.text); break;
            default: break;
        }
        if (said == 0 && column != 1 && x.file != y.file)
        {
            said = LLStringUtil::compareDict(cell(a, 0), cell(b, 0));
        }
        return said != 0 ? said : x.span < y.span ? -1 : y.span < x.span ? 1 : 0;
    });
    return true;
}

void ALScriptReferencesPane::show(Found found)
{
    mFound = std::move(found);
    U32 id = 0;
    for (Doc::Place& place : mFound.places)
    {
        place.id = ++id;
    }
    fill();
}

void ALScriptReferencesPane::fill()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices)
    {
        return;
    }
    // The row chosen and the scroll kept through a refill, which an edit
    // moving the places asks for.
    mStale             = false;
    const S32 scrolled = mList->getScrollPos();
    const S32 chosen   = mList->getFirstSelected() ? mList->getFirstSelected()->getValue().asInteger() : 0;
    mList->deleteAllItems();
    mWindow->referencesCounted();
    if (mFound.places.empty())
    {
        // A list with nothing in it and nothing to say is a pane that
        // looks broken; this one is empty until it is asked a question,
        // so it says which question.
        mHead->setText(mServices->words("NoReferences"));
        return;
    }
    // What was looked up, and where it was found, over the places: the
    // status line says it once, and the next thing said takes it away.
    std::set<std::string> files;
    for (const Doc::Place& place : mFound.places)
    {
        files.insert(place.file);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = mFound.name;
    args["[FILES]"] = std::to_string(files.size());
    const S32 count = static_cast<S32>(mFound.places.size());
    mHead->setText(mServices->counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", count, args));

    LLStringUtil::format_map_t named;
    named["[NAME]"]                = mFound.name;
    const std::string declared_tip = mServices->words("ReferenceDeclarationTip", named);
    for (size_t i = 0; i < mFound.places.size(); ++i)
    {
        const Doc::Place& place = mFound.places[i];
        // The declaration, marked: in the script it was looked up from
        // where no include declares it, else in the include that does.
        const std::string& in_file     = place.file;
        const bool         declaration = mFound.hasDefinition && place.span.line == mFound.definition.line &&
                                 place.span.column == mFound.definition.column && in_file == mFound.home;
        LLSD row;
        row["value"]                = static_cast<S32>(place.id);
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = place.file.empty() ? mFound.fromName : place.fileName;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = llformat("%d:%d", place.span.line + 1, place.span.column + 1);
        row["columns"][2]["column"] = "role";
        row["columns"][2]["value"]  = declaration ? mServices->words("ReferenceDeclaration") : std::string();
        row["columns"][3]["column"] = "text";
        row["columns"][3]["value"]  = place.text;
        if (declaration)
        {
            for (S32 c = 0; c < 4; ++c)
            {
                row["columns"][c]["font"]["style"] = "BOLD";
                row["columns"][c]["tool_tip"]      = declared_tip;
            }
        }
        LLScrollListItem* item = mList->addElement(row);
        // The name, lit where it stands in the line.
        if (item && place.at >= 0)
        {
            if (LLScrollListCell* text = item->getColumn(3))
            {
                text->highlightText(place.at, static_cast<S32>(mFound.name.size()));
            }
        }
    }
    // The row chosen, where its place is still listed; else the one now
    // at its place in the list.
    if (chosen > 0 && !mList->selectByValue(LLSD(chosen)))
    {
        const size_t at = placeWith(static_cast<U32>(chosen), true);
        mList->selectByValue(LLSD(static_cast<S32>(mFound.places[llmin(at, mFound.places.size() - 1)].id)));
    }
    mList->setScrollPos(scrolled);
}

void ALScriptReferencesPane::slide(Doc& doc, const std::string& path, const ALTextDocument::Edit& edit)
{
    if (mFound.places.empty())
    {
        return;
    }
    // This script's places: its own, where it was looked up from, or the
    // ones in it as an include or another of the object's scripts.
    const size_t before  = mFound.places.size();
    const S32    first   = edit.range.normalised().begin.line;
    const S32    last    = edit.rangeAfter().normalised().end.line;
    bool         changed = false;
    const auto   mine    = [&](const std::string& file) { return file.empty() ? doc.id == mFound.from : file == path; };
    const auto   slide   = [&](ALScriptSpan& span) {
        ALTextRange       range(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
        const ALTextRange was = range;
        if (!edit.slide(range))
        {
            return false;
        }
        if (range != was)
        {
            span.line      = range.begin.line;
            span.column    = range.begin.column;
            span.endLine   = range.end.line;
            span.endColumn = range.end.column;
            changed        = true;
        }
        return true;
    };
    mFound.places.erase(std::remove_if(mFound.places.begin(), mFound.places.end(),
                                       [&](Doc::Place& place) {
                                           if (!mine(place.file))
                                           {
                                               return false;
                                           }
                                           // The name itself edited: no longer a place it stands.
                                           if (!slide(place.span))
                                           {
                                               return true;
                                           }
                                           // Its line's words, where the edit was on it.
                                           if (place.span.line >= first && place.span.line <= last)
                                           {
                                               ALScriptPlaces::placeText(place, doc.editor->document().line(place.span.line));
                                               changed = true;
                                           }
                                           return false;
                                       }),
                        mFound.places.end());
    if (mFound.hasDefinition && mine(mFound.home))
    {
        mFound.hasDefinition = slide(mFound.definition);
    }
    // Refilled once this frame is drawn, however many edits it has.
    mStale = mStale || changed || mFound.places.size() != before;
}

size_t ALScriptReferencesPane::placeWith(U32 id, bool or_after) const
{
    // The places keep the order they were numbered in, some gone.
    const auto it = std::lower_bound(mFound.places.begin(), mFound.places.end(), id, [](const Doc::Place& p, U32 v) { return p.id < v; });
    if (it != mFound.places.end() && (or_after || it->id == id))
    {
        return static_cast<size_t>(it - mFound.places.begin());
    }
    return or_after && !mFound.places.empty() ? mFound.places.size() - 1 : mFound.places.size();
}

void ALScriptReferencesPane::pump()
{
    if (mStale)
    {
        fill();
    }
}

void ALScriptReferencesPane::rekey(const std::string& was, const std::string& id)
{
    if (mFound.from == was)
    {
        mFound.from = id;
    }
}

void ALScriptReferencesPane::choose(bool to_editor)
{
    LLScrollListItem* item = mList->getFirstSelected();
    if (!item || !mWindow)
    {
        return;
    }
    // By the place's own number: an edit since the rows were made may
    // have taken places out before it, and the place itself.
    const size_t index = placeWith(static_cast<U32>(item->getValue().asInteger()), false);
    if (index >= mFound.places.size())
    {
        return;
    }
    const Doc::Place place = mFound.places[index];
    mWindow->referenceChosen(mFound, place, to_editor);
}

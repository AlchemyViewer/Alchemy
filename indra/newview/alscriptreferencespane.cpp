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

#include "alcodeeditor.h"
#include "alpanefolds.h"
#include "alpanelist.h"
#include "alscriptstudiopane.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "llbutton.h"
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
    mList      = getChild<ALPaneList>("references");
    mHead      = getChild<LLTextBox>("references_head");
    mRename    = getChild<LLButton>("references_rename");
    mCancel    = getChild<LLButton>("references_cancel");
    mHeadRight = mHead->getRect().mRight;
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    if (!ALScriptStudioPane::findWindow(*this, "The References tab", mServices, mWindow))
    {
        return true;
    }
    // A box ticked, where a rename is previewed, is read before the place
    // is shown.
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        readBoxes();
        choose(false);
    });
    mList->setSpace([this]() {
        if (LLScrollListItem* item = previewing() ? mList->getFirstSelected() : nullptr)
        {
            const U32 id = static_cast<U32>(item->getValue().asInteger());
            setKept(id - 1, mLeftOut.contains(id));
        }
    });
    mRename->setCommitCallback([this](LLUICtrl*, const LLSD&) { renamePreviewed(); });
    mCancel->setCommitCallback([this](LLUICtrl*, const LLSD&) { cancelPreview(); });
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
    take(std::move(found));
    fill();
}

void ALScriptReferencesPane::take(Found found)
{
    mFound = std::move(found);
    U32 id = 0;
    for (Doc::Place& place : mFound.places)
    {
        place.id = ++id;
    }
    mApply = nullptr;
    mNewName.clear();
    mSaid.clear();
    mLeftOut.clear();
    showButtons();
}

void ALScriptReferencesPane::preview(Found found, const std::string& new_name, const std::string& said,
                                     std::function<void(const std::vector<size_t>& kept)> apply)
{
    take(std::move(found));
    mNewName = new_name;
    mSaid    = said;
    mApply   = std::move(apply);
    showButtons();
    fill();
}

std::vector<size_t> ALScriptReferencesPane::kept() const
{
    // The places still listed -- one an edit took the name from is gone --
    // and not left out, by the order they were found in.
    std::vector<size_t> out;
    for (const Doc::Place& place : mFound.places)
    {
        if (!mLeftOut.contains(place.id))
        {
            out.push_back(static_cast<size_t>(place.id) - 1);
        }
    }
    return out;
}

void ALScriptReferencesPane::setKept(size_t index, bool kept)
{
    const U32 id = static_cast<U32>(index) + 1;
    if (kept)
    {
        mLeftOut.erase(id);
    }
    else
    {
        mLeftOut.insert(id);
    }
    fill();
}

void ALScriptReferencesPane::readBoxes()
{
    if (!previewing())
    {
        return;
    }
    for (const LLScrollListItem* item : mList->getAllData())
    {
        const LLScrollListCell* box = item->getColumn(2);
        const U32               id  = static_cast<U32>(item->getValue().asInteger());
        if (id == 0)
        {
            // The count of those not listed.
            continue;
        }
        if (box && box->getValue().asBoolean())
        {
            mLeftOut.erase(id);
        }
        else
        {
            mLeftOut.insert(id);
        }
    }
}

void ALScriptReferencesPane::renamePreviewed()
{
    if (!previewing())
    {
        return;
    }
    // Listed as found again before the change is made, which then takes
    // the renamed places out as it lands: what is left is what was left
    // out.
    readBoxes();
    const std::vector<size_t> chosen = kept();
    const auto                apply  = std::move(mApply);
    mApply                           = nullptr;
    mLeftOut.clear();
    showButtons();
    fill();
    apply(chosen);
}

void ALScriptReferencesPane::cancelPreview()
{
    mApply = nullptr;
    mLeftOut.clear();
    showButtons();
    fill();
}

void ALScriptReferencesPane::showButtons()
{
    if (!mRename || !mCancel || !mHead)
    {
        return;
    }
    const bool shown = previewing();
    mRename->setVisible(shown);
    mCancel->setVisible(shown);
    LLRect head = mHead->getRect();
    head.mRight = shown ? mRename->getRect().mLeft - 4 : mHeadRight;
    mHead->setRect(head);
}

void ALScriptReferencesPane::fill()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices)
    {
        return;
    }
    // The row chosen and the scroll are kept through a refill by the
    // places' numbers, which an edit moving the places asks for.
    mStale           = false;
    const S32 chosen = mList->getFirstSelected() ? mList->getFirstSelected()->getValue().asInteger() : 0;
    mWindow->referencesCounted();
    if (mFound.places.empty())
    {
        mList->setRows({});
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
    mHead->setText(previewing() ? mSaid : mServices->counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", count, args));

    LLStringUtil::format_map_t named;
    named["[NAME]"]                = mFound.name;
    const std::string declared_tip = mServices->words("ReferenceDeclarationTip", named);
    const std::string declared     = mServices->words("ReferenceDeclaration");
    const LLFontGL*   bold         = LLFontGL::getFontSansSerifSmallBold();
    // Each place its own row by the number it was given, made again only
    // where it says something else -- an edit moving its line -- and no
    // more than PLACES_MOST of them, the rest counted.
    const size_t                 listing = llmin(mFound.places.size(), PLACES_MOST);
    std::vector<ALPaneList::Row> rows;
    rows.reserve(listing + 1);
    for (size_t i = 0; i < listing; ++i)
    {
        const Doc::Place& place = mFound.places[i];
        // The declaration, marked: in the script it was looked up from
        // where no include declares it, else in the include that does.
        const bool declaration = mFound.hasDefinition && place.span.line == mFound.definition.line &&
                                 place.span.column == mFound.definition.column && place.file == mFound.home;
        // Previewed: a box to leave the place out by, and the line as the
        // rename would leave it.
        const bool left    = mLeftOut.contains(place.id);
        const bool renamed = previewing() && !left && place.at >= 0 && static_cast<size_t>(place.at) + mFound.name.size() <= place.text.size();
        const auto cell    = [&](const char* column, const LLSD& value, const char* type = "text") {
            LLScrollListCell::Params one;
            one.column = column;
            one.type   = type;
            one.value  = value;
            if (declaration)
            {
                one.font     = bold;
                one.tool_tip = declared_tip;
            }
            return one;
        };
        ALPaneList::Row row;
        row.key   = std::to_string(place.id);
        row.value = static_cast<S32>(place.id);
        row.cells = { cell("where", place.file.empty() ? mFound.fromName : place.fileName),
                      cell("line", llformat("%d:%d", place.span.line + 1, place.span.column + 1)),
                      previewing() ? cell("role", LLSD(!left), "checkbox") : cell("role", declaration ? declared : std::string()),
                      cell("text", renamed ? place.text.substr(0, place.at) + mNewName + place.text.substr(place.at + mFound.name.size()) : place.text) };
        rows.push_back(std::move(row));
    }
    if (listing < mFound.places.size())
    {
        ALPaneList::Row more;
        more.key     = "#unlisted";
        more.enabled = false;
        LLScrollListCell::Params words;
        words.column = "text";
        words.value  = mServices->counted("ReferencesUnlisted", static_cast<S32>(mFound.places.size() - listing));
        more.cells   = { words };
        rows.push_back(std::move(more));
    }
    mList->setRows(std::move(rows));
    // The name, lit where it stands in each line.
    for (size_t i = 0; i < listing; ++i)
    {
        const Doc::Place& place   = mFound.places[i];
        const bool        renamed = previewing() && !mLeftOut.contains(place.id) && place.at >= 0 &&
                             static_cast<size_t>(place.at) + mFound.name.size() <= place.text.size();
        LLScrollListItem* item = place.at >= 0 ? mList->rowWithKey(std::to_string(place.id)) : nullptr;
        if (LLScrollListCell* text = item ? item->getColumn(3) : nullptr)
        {
            text->highlightText(place.at, static_cast<S32>(renamed ? mNewName.size() : mFound.name.size()));
        }
    }
    // The row chosen, where its place is still listed; else the one now
    // at its place in the list.
    if (chosen > 0 && !mList->getFirstSelected())
    {
        const size_t at = llmin(placeWith(static_cast<U32>(chosen), true), listing - 1);
        mList->selectByValue(LLSD(static_cast<S32>(mFound.places[at].id)));
    }
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
    // Refilled only while it can be seen, and once it is.
    if (mStale && ALPaneFolds::inSight(this))
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

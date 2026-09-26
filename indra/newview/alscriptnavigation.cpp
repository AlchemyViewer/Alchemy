/**
 * @file alscriptnavigation.cpp
 * @brief Script Studio's navigation: the places gone from, back and forward, and previews opened as a list is walked.
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

#include "alscriptnavigation.h"

#include "alpanelist.h"
#include "alscriptstudioservices.h"
#include "llsdutil.h"
#include "llscrolllistitem.h"
#include "lltimer.h"

// --- the history ------------------------------------------------------------------------

void ALNavHistory::note(const Place& place)
{
    mForward.clear();
    // Another jump from the same line is not another place to go back to.
    if (!mBack.empty() && mBack.back().doc == place.doc && mBack.back().view == place.view && mBack.back().at.line == place.at.line)
    {
        return;
    }
    mBack.push_back(place);
    if (mBack.size() > PLACES)
    {
        mBack.erase(mBack.begin());
    }
}

std::optional<ALNavHistory::Place> ALNavHistory::take(bool forward, const std::optional<Place>& here,
                                                      const std::function<bool(const std::string& doc)>& open)
{
    std::vector<Place>& from = forward ? mForward : mBack;
    std::vector<Place>& to   = forward ? mBack : mForward;
    while (!from.empty())
    {
        const Place place = from.back();
        from.pop_back();
        // A tab closed since is passed over.
        if (!open(place.doc))
        {
            continue;
        }
        if (here)
        {
            to.push_back(*here);
        }
        return place;
    }
    return std::nullopt;
}

void ALNavHistory::rekey(const std::string& was, const std::string& id)
{
    for (std::vector<Place>* places : { &mBack, &mForward })
    {
        for (Place& place : *places)
        {
            if (place.doc == was)
            {
                place.doc = id;
            }
        }
    }
}

// --- jumps --------------------------------------------------------------------------------

ALScriptNavigation::ALScriptNavigation(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

void ALScriptNavigation::noteJump(bool walking)
{
    // A walk down a pane's list is one jump, from where the caret was
    // before it began; it ends when the editor has the keyboard again.
    if (walking && mWalking)
    {
        return;
    }
    mWalking = walking;
    Doc* doc = mServices.frontDoc();
    if (!doc || !doc->loaded)
    {
        return;
    }
    mHistory.note(Place{ doc->id, doc->shownText()->caret(), doc->shownView() });
}

void ALScriptNavigation::goBack(bool forward)
{
    std::optional<Place> here;
    if (Doc* front = mServices.frontDoc(); front && front->loaded)
    {
        here = Place{ front->id, front->shownText()->caret(), front->shownView() };
    }
    const auto                 open  = [this](const std::string& id) { return mServices.findDoc(id) != nullptr; };
    const std::optional<Place> place = mHistory.take(forward, here, open);
    if (!place)
    {
        return;
    }
    mWalking = false;
    // In the view it was in, where the tab still has it.
    mWindow.showPlace(*mServices.findDoc(place->doc), place->view, place->at);
}

// --- previews -----------------------------------------------------------------------------

bool ALScriptNavigation::deferOpen(ALPaneList* list, const std::string& path)
{
    // Once the list has stayed on it -- the arrows walking past each row
    // they go by -- it is opened, as a preview, which the next one opened
    // so takes the place of.
    if (mSettled || path.empty() || mWindow.pathOpen(path))
    {
        return false;
    }
    constexpr F64     SETTLE = 0.35;
    LLScrollListItem* item   = list->getFirstSelected();
    mSettleList              = list;
    mSettleValue             = item ? item->getValue() : LLSD();
    mSettleDue               = LLTimer::getTotalSeconds() + SETTLE;
    return true;
}

void ALScriptNavigation::pumpSettle()
{
    if (!mSettleList || LLTimer::getTotalSeconds() < mSettleDue)
    {
        return;
    }
    ALPaneList* list = mSettleList;
    mSettleList      = nullptr;
    // Still there, and still being walked: gone from, or left for
    // something else, it is not opened.
    LLScrollListItem* item = list->getFirstSelected();
    if (!item || !list->hasFocus() || !llsd_equals(item->getValue(), mSettleValue))
    {
        return;
    }
    mSettled = true;
    ++mOpenPreview;
    mWindow.choosePreview(list);
    --mOpenPreview;
    mSettled = false;
}

void ALScriptNavigation::closePreview()
{
    for (Doc* doc : mServices.openDocs())
    {
        if (!doc->preview)
        {
            continue;
        }
        // One a pane is listing -- its problems, the places a name was
        // looked up from it -- is being worked from, and stays.
        if (doc->editor->isDirty() || doc->save.sending() || mWindow.workedFrom(*doc))
        {
            holdPreview(*doc);
            return;
        }
        // Nothing held there: the next look takes its place.
        mWindow.letGoOf(*doc);
        return;
    }
}

void ALScriptNavigation::holdPreview(Doc& doc)
{
    if (doc.preview)
    {
        doc.preview = false;
        mWindow.fillTabs();
    }
}

void ALScriptNavigation::revealed(LLUICtrl* list, bool to_editor)
{
    // To the script to type there, held where it was a preview, or back
    // to the list to walk on.
    if (to_editor)
    {
        mSettleList = nullptr;
        if (Doc* doc = mServices.frontDoc())
        {
            holdPreview(*doc);
            mWindow.focusDoc(*doc);
        }
        return;
    }
    list->setFocus(true);
}

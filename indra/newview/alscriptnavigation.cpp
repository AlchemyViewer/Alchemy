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

#include "alfloaterscriptstudio.h"

#include "alpanelist.h"
#include "alscriptpreprocessor.h"
#include "llsdutil.h"
#include "llscrolllistitem.h"
#include "lltimer.h"

bool ALFloaterScriptStudio::deferOpen(ALPaneList* list, const std::string& path)
{
    // Once the list has stayed on it -- the arrows walking past each row
    // they go by -- it is opened, as a preview, which the next one opened
    // so takes the place of.
    if (mSettled || path.empty())
    {
        return false;
    }
    ALScriptRef ref;
    std::string file;
    const bool  open = ALScriptPreprocessor::refOf(path, ref) ? indexOf(ref) != NONE
                       : ALScriptPreprocessor::fileOf(path, file) ? indexOf("disk:" + file) != NONE
                                                                   : true;
    if (open)
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

void ALFloaterScriptStudio::pumpSettle()
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
    if (list == mProblemsPane->list())
    {
        mProblemsPane->choose(false);
    }
    else if (list == mReferences)
    {
        onReferenceChosen(false);
    }
    else if (list == mSearchPane->list())
    {
        mSearchPane->choose(false);
    }
    else if (list == mWeightsParts)
    {
        onWeightChosen(false);
    }
    --mOpenPreview;
    mSettled = false;
}

void ALFloaterScriptStudio::closePreview()
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        Doc& doc = *mDocs[i];
        if (!doc.preview)
        {
            continue;
        }
        // One a pane is listing -- its problems, the places a name was
        // looked up from it -- is being worked from, and stays.
        if (doc.editor->isDirty() || doc.save.sending() || doc.id == mProblemsPane->listedId() || doc.id == mFound.from)
        {
            holdPreview(doc);
            return;
        }
        // Nothing held there: the next look takes its place.
        letGoOf(i);
        return;
    }
}

void ALFloaterScriptStudio::holdPreview(Doc& doc)
{
    if (doc.preview)
    {
        doc.preview = false;
        fillTabs();
    }
}

void ALFloaterScriptStudio::revealed(LLUICtrl* list, bool to_editor)
{
    // To the script to type there, held where it was a preview, or back
    // to the list to walk on.
    if (to_editor)
    {
        mSettleList = nullptr;
        if (Doc* doc = active())
        {
            holdPreview(*doc);
            focusShown(*doc);
        }
        return;
    }
    list->setFocus(true);
}

void ALFloaterScriptStudio::noteJump(bool walking)
{
    // A walk down a pane's list is one jump, from where the caret was
    // before it began; it ends when the editor has the keyboard again.
    if (walking && mWalking)
    {
        return;
    }
    mWalking = walking;
    Doc* doc = active();
    if (!doc || !doc->loaded)
    {
        return;
    }
    rememberPlace(NavPlace{ doc->id, doc->shownText()->caret(), doc->shownView() });
}

void ALFloaterScriptStudio::rememberPlace(const NavPlace& place)
{
    mForward.clear();
    // Another jump from the same line is not another place to go back to.
    if (!mBack.empty() && mBack.back().doc == place.doc && mBack.back().view == place.view && mBack.back().at.line == place.at.line)
    {
        return;
    }
    mBack.push_back(place);
    constexpr size_t PLACES = 50;
    if (mBack.size() > PLACES)
    {
        mBack.erase(mBack.begin());
    }
}

void ALFloaterScriptStudio::goBack(bool forward)
{
    std::vector<NavPlace>& from = forward ? mForward : mBack;
    std::vector<NavPlace>& to   = forward ? mBack : mForward;
    while (!from.empty())
    {
        const NavPlace place = from.back();
        from.pop_back();
        // A tab closed since is passed over.
        const size_t index = indexOf(place.doc);
        if (index == NONE)
        {
            continue;
        }
        if (Doc* here = active(); here && here->loaded)
        {
            to.push_back(NavPlace{ here->id, here->shownText()->caret(), here->shownView() });
        }
        mWalking = false;
        if (index != mActive)
        {
            activate(index);
        }
        // In the view it was in, where the tab still has it.
        Doc& doc = *mDocs[index];
        showView(doc, place.view);
        ALCodeEditor& text = *doc.shownText();
        text.goTo(text.document().clamp(place.at));
        text.setFocus(true);
        return;
    }
}

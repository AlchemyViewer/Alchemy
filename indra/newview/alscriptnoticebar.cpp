/**
 * @file alscriptnoticebar.cpp
 * @brief Script Studio's notice over the editor: what the tab in front has to reckon with, and up to two things to do about it.
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

#include "alscriptnoticebar.h"

#include "alscriptstudiopane.h"
#include "alscriptstudioservices.h"
#include "llbutton.h"
#include "llfloater.h"
#include "llfontgl.h"
#include "lltextbox.h"

static LLPanelInjector<ALScriptNoticeBar> t_script_studio_notice("script_studio_notice");

bool ALScriptNoticeBar::postBuild()
{
    mText       = getChild<LLTextBox>("notice_text");
    mButtons[0] = getChild<LLButton>("notice_first");
    mButtons[1] = getChild<LLButton>("notice_second");
    mButtons[2] = getChild<LLButton>("notice_third");
    // The window this is over, found through the view tree, as what the
    // buttons ask of it.
    if (!ALScriptStudioPane::findWindow(*this, "The notice", mServices, mWindow))
    {
        return true;
    }
    for (size_t i = 0; i < BUTTONS; ++i)
    {
        mButtons[i]->setCommitCallback([this, i](LLUICtrl*, const LLSD&) { mWindow->noticeAction(mActions[i]); });
    }
    getChild<LLButton>("notice_close")->setCommitCallback([this](LLUICtrl*, const LLSD&) { mWindow->noticeAction("close"); });
    return true;
}

void ALScriptNoticeBar::show(const Notice& notice)
{
    // Its layout panel, which gives it room over the editor, there while
    // it has something to say.
    if (LLView* holder = getParent())
    {
        holder->setVisible(!notice.text.empty());
    }
    if (notice.text.empty() || !mServices)
    {
        for (std::string& action : mActions)
        {
            action.clear();
        }
        return;
    }
    mText->setText(notice.text);
    mText->setToolTip(notice.text);
    // The buttons as wide as their words, from the right, the way out
    // last; the words have what is left.
    const LLFontGL* font  = LLFontGL::getFontSansSerifSmall();
    S32             right = getRect().getWidth() - 4 - 22 - 6;
    for (S32 i = static_cast<S32>(BUTTONS) - 1; i >= 0; --i)
    {
        LLButton*   button      = mButtons[i];
        const auto& [id, label] = notice.buttons[i];
        mActions[i]             = id;
        button->setVisible(!id.empty());
        if (id.empty())
        {
            continue;
        }
        const std::string said  = mServices->words(label);
        const S32         width = font->getWidth(said) + 24;
        button->setLabel(said);
        button->setToolTip(mServices->words(label + "Tip"));
        const LLRect was = button->getRect();
        button->setShape(LLRect(right - width, was.mTop, right, was.mBottom));
        right -= width + 4;
    }
    const LLRect words = mText->getRect();
    mText->setShape(LLRect(words.mLeft, words.mTop, llmax(words.mLeft + 40, right - 6), words.mBottom));
}

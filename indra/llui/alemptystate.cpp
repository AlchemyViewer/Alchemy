/**
 * @file alemptystate.cpp
 * @brief What a pane says when there is nothing in it.
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

#include "linden_common.h"

#include "alemptystate.h"

#include "llbutton.h"
#include "lliconctrl.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALEmptyState> r("empty_state");

namespace
{
    constexpr S32 MARGIN = 16;      // from the edges of the room
    constexpr S32 GAP = 6;          // between the parts
    constexpr S32 ICON_SIDE = 32;
    constexpr S32 HEADLINE_HEIGHT = 20;
    constexpr S32 BUTTON_HEIGHT = 22;
    constexpr S32 BUTTON_PAD = 24;  // either side of the label
    // Wider than this and a sentence is read across the room rather than
    // down it, which is what a paragraph is for and not what this is.
    constexpr S32 MOST_WIDTH = 320;
}

ALEmptyState::Params::Params()
:   icon("icon"),
    headline("headline"),
    sentence("sentence"),
    action("action")
{
}

ALEmptyState::ALEmptyState(const Params& p)
:   LLPanel(p),
    mHeadline(p.headline),
    mSentence(p.sentence),
    mActionLabel(p.action)
{
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor quiet = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    {
        LLIconCtrl::Params ip;
        ip.name = "icon";
        ip.rect = LLRect(0, ICON_SIDE, ICON_SIDE, 0);
        mIcon = LLUICtrlFactory::create<LLIconCtrl>(ip);
        mIcon->setValue(std::string(p.icon));
        addChild(mIcon);
    }

    LLTextBox::Params hp;
    hp.name = "headline";
    hp.rect = LLRect(0, HEADLINE_HEIGHT, 10, 0);
    hp.font = LLFontGL::getFontSansSerifBold();
    hp.font_halign = LLFontGL::HCENTER;
    hp.text_color = ink;
    hp.initial_value = mHeadline;
    mHeadlineText = LLUICtrlFactory::create<LLTextBox>(hp);
    addChild(mHeadlineText);

    LLTextBox::Params sp;
    sp.name = "sentence";
    sp.rect = LLRect(0, HEADLINE_HEIGHT, 10, 0);
    sp.font = LLFontGL::getFontSansSerifSmall();
    sp.font_halign = LLFontGL::HCENTER;
    sp.text_color = quiet;
    sp.wrap = true;
    sp.initial_value = mSentence;
    mSentenceText = LLUICtrlFactory::create<LLTextBox>(sp);
    addChild(mSentenceText);

    LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
    bp.name = "action";
    bp.rect = LLRect(0, BUTTON_HEIGHT, 10, 0);
    bp.label = mActionLabel;
    mButton = LLUICtrlFactory::create<LLButton>(bp);
    mButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mAction(); });
    addChild(mButton);

    layout();
}

void ALEmptyState::say(const std::string& headline, const std::string& sentence,
                       const std::string& action)
{
    mHeadline = headline;
    mSentence = sentence;
    mActionLabel = action;
    mHeadlineText->setText(mHeadline);
    mSentenceText->setText(mSentence);
    mButton->setLabel(mActionLabel);
    layout();
}

void ALEmptyState::setIcon(const std::string& name)
{
    if (mIcon)
    {
        mIcon->setValue(name);
        mIcon->setVisible(!name.empty());
        layout();
    }
}

void ALEmptyState::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}

void ALEmptyState::layout()
{
    const S32 room = getRect().getWidth() - 2 * MARGIN;
    const S32 width = llclamp(room, 1, MOST_WIDTH);

    const bool has_icon = mIcon && !mIcon->getValue().asString().empty();
    const bool has_headline = !mHeadline.empty();
    const bool has_sentence = !mSentence.empty();
    const bool has_action = !mActionLabel.empty();

    // What each part will take, before anything is placed: the block is
    // centred down the room, so how tall it is has to be known first.
    mSentenceText->setVisible(has_sentence);
    if (has_sentence)
    {
        // Measured at the width it will be read at, since a wrapped line
        // count is a fact about the width and not about the words.
        mSentenceText->reshape(width, HEADLINE_HEIGHT);
        mSentenceText->setText(mSentence);
    }
    const S32 sentence_height = has_sentence ? llmax(mSentenceText->getTextPixelHeight(), 1) : 0;

    S32 tall = 0;
    const auto add = [&tall](bool part, S32 height)
    {
        if (part)
        {
            tall += (tall > 0 ? GAP : 0) + height;
        }
    };
    add(has_icon, ICON_SIDE);
    add(has_headline, HEADLINE_HEIGHT);
    add(has_sentence, sentence_height);
    add(has_action, BUTTON_HEIGHT);

    S32 top = (getRect().getHeight() + tall) / 2;
    const auto place = [&](LLView* view, bool part, S32 height, S32 how_wide)
    {
        if (!view)
        {
            return;
        }
        view->setVisible(part);
        if (!part)
        {
            return;
        }
        const S32 at = (getRect().getWidth() - how_wide) / 2;
        view->setShape(LLRect(at, top, at + how_wide, top - height));
        top -= height + GAP;
    };

    place(mIcon, has_icon, ICON_SIDE, ICON_SIDE);
    place(mHeadlineText, has_headline, HEADLINE_HEIGHT, width);
    place(mSentenceText, has_sentence, sentence_height, width);

    // A button is as wide as its label needs, and never wider than the room.
    S32 button_width = width;
    if (has_action)
    {
        const LLFontGL* font = mButton->getFont() ? mButton->getFont() : LLFontGL::getFontSansSerif();
        button_width = llclamp((S32)font->getWidth(mActionLabel) + 2 * BUTTON_PAD, 60, width);
    }
    place(mButton, has_action, BUTTON_HEIGHT, button_width);
}

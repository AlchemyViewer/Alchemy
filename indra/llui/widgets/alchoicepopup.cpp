/**
 * @file alchoicepopup.cpp
 * @brief A list of choices floated over a view, and the box beside it about the chosen one.
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

#include "alchoicepopup.h"

#include "alplace.h"
#include "altextview.h"
#include "llrender2dutils.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llurlaction.h"

namespace
{
    // The box beside the list: as wide as this at most, and the room
    // inside its edges.
    const S32 SIDE_WIDTH = 320;
    const S32 SIDE_PAD   = 6;
}

ALChoicePopup::Params::Params()
:   list_name("list_name"),
    side_name("side_name"),
    menu_like("menu_like", false),
    font("font")
{
    mouse_opaque  = false;
    follows.flags = FOLLOWS_ALL;
}

ALChoicePopup::ALChoicePopup(const Params& p)
:   LLView(p),
    mSideName(p.side_name)
{
    // On the text engine, as the hover card is: the choices in the view's
    // face and the colours of their kinds, their details in the reading
    // face after them.
    const LLUIColor        ground = LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black);
    ALChoiceList::Params list(LLUICtrlFactory::getDefaultParams<ALChoiceList>());
    list.name(p.list_name());
    list.rect(LLRect(0, 10, 10, 0));
    list.visible(false);
    list.follows.flags(FOLLOWS_NONE);
    list.mouse_opaque(true);
    if (p.font.isProvided())
    {
        list.font(p.font());
    }
    list.bg_visible(true);
    list.bg_color(ground);
    list.bg_readonly_color(ground);
    list.context_menu(std::string());
    list.h_pad(4);
    list.v_pad(2);
    mList = LLUICtrlFactory::create<ALChoiceList>(list);
    mList->setMenuLike(p.menu_like);
    addChild(mList);
}

bool ALChoicePopup::onList(S32 x, S32 y) const
{
    return getVisible() && mList->getVisible() && mList->getRect().pointInRect(x, y);
}

void ALChoicePopup::setLook(const LLFontGL* font, const LLColor4& ground, const LLColor4& ink, const LLColor4& chosen, const LLColor4& border)
{
    if (font && mList->getFont() != font)
    {
        mList->setFont(font);
    }
    mList->setBackgroundColor(ground);
    mList->setTextColor(ink);
    mList->setSelectionColor(chosen);
    mList->setBorderColor(border);
    mGround = ground;
    mInk    = ink;
    mBorder = border;
}

void ALChoicePopup::fill(std::vector<ALChoiceList::Choice> choices, S32 chosen, S32 width)
{
    mList->setShape(LLRect(0, 40, width, 0));
    mList->setChoices(std::move(choices), chosen);
}

void ALChoicePopup::showUnder(const LLRect& anchor, S32 rows, S32 width)
{
    // Under the row, or over it where under would run off the bottom;
    // across, within the view.
    mList->setShape(ALPlace::under(anchor, width, mList->heightFor(rows), getLocalRect()));
    mList->setVisible(true);
    setVisible(true);
}

void ALChoicePopup::hideList()
{
    mList->setVisible(false);
    mList->setChoices({});
    hideSide();
    setVisible(false);
}

ALTextView& ALChoicePopup::side()
{
    if (!mSide)
    {
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name                = mSideName;
        p.rect                = LLRect(0, 20, SIDE_WIDTH, 0);
        p.read_only           = true;
        p.word_wrap           = true;
        p.tab_stop            = false;
        p.takes_focus         = false;
        p.mouse_opaque        = true;
        p.font                = LLFontGL::getFontSansSerif();
        p.bg_visible          = true;
        p.bg_color            = mGround;
        p.bg_readonly_color   = mGround;
        p.text_readonly_color = mInk;
        p.h_pad               = SIDE_PAD;
        p.v_pad               = SIDE_PAD - 2;
        p.context_menu        = std::string();
        mSide                 = LLUICtrlFactory::create<ALTextView>(p);
        mSide->setVisible(false);
        mSide->onLinkClicked([](const ALTextView::Substitution& link) {
            if (!link.url.empty())
            {
                LLUrlAction::clickAction(link.url, false);
            }
        });
        addChild(mSide);
    }
    mSide->setBackgroundColor(mGround);
    mSide->setTextColor(mInk);
    mSide->setLineAnnotations({});
    return *mSide;
}

void ALChoicePopup::placeSide()
{
    // Beside the list where there is room, on the right or else the
    // left; else under it, or over it; as tall as it says, up to a
    // limit, the rest cut.
    if (!mSide)
    {
        return;
    }
    ALTextView&  box   = *mSide;
    const LLRect local = getLocalRect();
    const LLRect list  = mList->getRect();
    const S32    width = llmin(SIDE_WIDTH, llmax(120, local.getWidth() - 8));
    const S32    lines = box.document().lineCount();
    box.setShape(LLRect(0, 40, width, 0));
    for (S32 line = 0; line < lines; ++line)
    {
        box.layout().line(line);
    }
    const S32 height = llmin(box.layout().totalHeight() + 2 * (SIDE_PAD - 2) + 2, llmax(list.getHeight(), 200));
    box.setShape(ALPlace::beside(list, width, height, local, 2));
    box.setVisible(true);
}

void ALChoicePopup::hideSide()
{
    if (mSide)
    {
        mSide->setVisible(false);
    }
    mSideSays.clear();
}

bool ALChoicePopup::sideShown() const
{
    return mSide && mSide->getVisible();
}

bool ALChoicePopup::sideSays(const std::string& says) const
{
    return sideShown() && says == mSideSays && mList->getRect() == mSideBeside;
}

void ALChoicePopup::sideSaid(const std::string& says)
{
    mSideSays   = says;
    mSideBeside = mList->getRect();
}

void ALChoicePopup::draw()
{
    LLView::draw();
    // Over the box rather than under it, so that its own ground cannot
    // paint the frame out.
    if (sideShown())
    {
        gl_rect_2d(mSide->getRect(), mBorder % getDrawContext().mAlpha, false);
    }
}

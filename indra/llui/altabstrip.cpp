/**
 * @file altabstrip.cpp
 * @brief A row of tabs over what a window shows, one per thing it holds.
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

#include "altabstrip.h"

#include "llfontgl.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "lltooltip.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

static LLDefaultChildRegistry::Register<ALTabStrip> r("tab_strip");

namespace
{
    // Between a tab's edge and its name, and around the way out.
    constexpr S32 PAD = 8;
    // The way out is a square the height of the text, at the right end.
    constexpr S32 CLOSE = 12;
    // The mark's own room, so that names line up whether or not they have
    // one, and the mark itself: a dot drawn rather than a glyph looked up,
    // since a bullet is whatever size the face makes it and this is a mark
    // beside text, not text.
    constexpr S32 MARK = 10;
    constexpr F32 DOT = 3.f;
}

ALTabStrip::Params::Params()
:   min_tab_width("min_tab_width", 64),
    max_tab_width("max_tab_width", 220),
    gap("gap", 2)
{
}

ALTabStrip::ALTabStrip(const Params& p)
:   LLUICtrl(p),
    mMinTabWidth(p.min_tab_width),
    mMaxTabWidth(p.max_tab_width),
    mGap(p.gap)
{
}

void ALTabStrip::setTabs(std::vector<Tab> tabs, const std::string& chosen)
{
    mTabs = std::move(tabs);
    mChosen = chosen;
    mHover = -1;
    layout();
}

void ALTabStrip::choose(const std::string& value)
{
    mChosen = value;
}

std::string ALTabStrip::textOf(const Tab& tab) const
{
    return tab.detail.empty() ? tab.label : tab.label + "  " + tab.detail;
}

const LLFontGL* ALTabStrip::fontFor(const Tab& tab)
{
    return LLFontGL::getFontSansSerifSmall()->faceFor(styleOf(tab));
}

U8 ALTabStrip::styleOf(const Tab& tab)
{
    return tab.preview ? LLFontGL::ITALIC : LLFontGL::NORMAL;
}

// Each tab wants the width of its words; each gets that where they all fit,
// and an equal share where they do not, down to the least a tab may be. The
// strip is not the place to decide that a document should not be shown, so
// past that the tabs run off the right edge rather than vanish.
void ALTabStrip::layout()
{
    mWidths.assign(mTabs.size(), 0);
    if (mTabs.empty())
    {
        return;
    }
    S32 wanted = 0;
    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        const S32 words = fontFor(mTabs[i])->getWidth(textOf(mTabs[i]));
        mWidths[i] = llclamp(words + MARK + PAD * 2 + CLOSE, mMinTabWidth, mMaxTabWidth);
        wanted += mWidths[i];
    }
    const S32 room = getRect().getWidth() - mGap * ((S32)mTabs.size() - 1);
    if (wanted > room)
    {
        const S32 share = llmax(mMinTabWidth, room / (S32)mTabs.size());
        for (S32& width : mWidths)
        {
            width = llmin(width, share);
        }
    }
}

LLRect ALTabStrip::rectOf(size_t index) const
{
    S32 left = 0;
    for (size_t i = 0; i < index && i < mWidths.size(); ++i)
    {
        left += mWidths[i] + mGap;
    }
    const S32 width = index < mWidths.size() ? mWidths[index] : 0;
    return LLRect(left, getRect().getHeight(), left + width, 0);
}

LLRect ALTabStrip::closeRectOf(size_t index) const
{
    const LLRect tab = rectOf(index);
    const S32 middle = tab.getHeight() / 2;
    return LLRect(tab.mRight - PAD - CLOSE, middle + CLOSE / 2, tab.mRight - PAD, middle - CLOSE / 2);
}

S32 ALTabStrip::at(S32 x, S32 y) const
{
    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        if (rectOf(i).pointInRect(x, y))
        {
            return (S32)i;
        }
    }
    return -1;
}

void ALTabStrip::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

void ALTabStrip::draw()
{
    static const LLUIColor shown = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::grey4);
    static const LLUIColor rest = LLUIColorTable::instance().getColor("DkGray", LLColor4::grey3);
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor quiet = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const S32 height = getRect().getHeight();
    const F32 alpha = getDrawContext().mAlpha;

    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        const Tab& tab = mTabs[i];
        const bool current = tab.value == mChosen;
        const bool hovered = (S32)i == mHover;
        const LLRect r = rectOf(i);
        if (r.mLeft >= getRect().getWidth())
        {
            break;
        }
        const LLFontGL* font = fontFor(tab);
        const S32 baseline = (height - font->getLineHeight()) / 2;

        // The shown tab is the face of what is under it; the rest sit back.
        gl_rect_2d(r, (current ? shown : rest).get() % alpha, true);
        gl_rect_2d(r, edge.get() % alpha, false);

        S32 x = r.mLeft + PAD;
        if (tab.dirty)
        {
            gGL.getTextureSlot(0)->unbind();
            gGL.color4fv((ink.get() % alpha).mV);
            gl_circle_2d((F32)x + DOT, (F32)height * 0.5f, DOT, 12, true);
        }
        x += MARK;

        // The name stops short of the way out, which every held tab has
        // and a preview does not.
        const S32 room = r.mRight - PAD - CLOSE - PAD / 2 - x;
        if (room > 0)
        {
            const U8 style = styleOf(tab);
            F32 after = (F32)x;
            font->renderUTF8(tab.label, 0, (F32)x, (F32)baseline, (current ? ink : quiet).get() % alpha,
                             LLFontGL::LEFT, LLFontGL::BOTTOM, style, LLFontGL::NO_SHADOW,
                             S32_MAX, room, &after, /*use_ellipses=*/true);
            if (!tab.detail.empty())
            {
                const S32 left = (S32)after + PAD / 2;
                const S32 remaining = r.mRight - PAD - CLOSE - PAD / 2 - left;
                if (remaining > CLOSE)
                {
                    font->renderUTF8(tab.detail, 0, (F32)left, (F32)baseline, quiet.get() % alpha,
                                     LLFontGL::LEFT, LLFontGL::BOTTOM, style, LLFontGL::NO_SHADOW,
                                     S32_MAX, remaining, nullptr, /*use_ellipses=*/true);
                }
            }
        }
        if (!tab.preview)
        {
            // Brighter under the pointer, so that a press there is plainly
            // a press on it and not on the tab.
            const LLRect c = closeRectOf(i);
            const bool over = hovered && c.pointInRect(mHoverX, mHoverY);
            font->renderUTF8("\xC3\x97", 0, (F32)c.getCenterX(), (F32)baseline,
                             (over || current ? ink : quiet).get() % alpha,
                             LLFontGL::HCENTER, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        }
    }
    LLUICtrl::draw();
}

bool ALTabStrip::handleMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which < 0)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    const Tab& tab = mTabs[which];
    if (!tab.preview && closeRectOf(which).pointInRect(x, y))
    {
        mClosedSignal(tab.value);
        return true;
    }
    if (tab.value != mChosen)
    {
        mChosen = tab.value;
        mChosenSignal(tab.value);
    }
    return true;
}

bool ALTabStrip::handleMiddleMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which >= 0 && !mTabs[which].preview)
    {
        mClosedSignal(mTabs[which].value);
        return true;
    }
    return LLUICtrl::handleMiddleMouseDown(x, y, mask);
}

bool ALTabStrip::handleHover(S32 x, S32 y, MASK mask)
{
    mHover = at(x, y);
    mHoverX = x;
    mHoverY = y;
    if (LLWindow* window = getWindow())
    {
        window->setCursor(UI_CURSOR_ARROW);
    }
    return true;
}

void ALTabStrip::onMouseLeave(S32 x, S32 y, MASK mask)
{
    mHover = -1;
    LLUICtrl::onMouseLeave(x, y, mask);
}

// A tab's own words, whatever the strip was given as a whole.
bool ALTabStrip::handleToolTip(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which < 0 || mTabs[which].toolTip.empty())
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    LLToolTipMgr::instance().show(mTabs[which].toolTip);
    return true;
}

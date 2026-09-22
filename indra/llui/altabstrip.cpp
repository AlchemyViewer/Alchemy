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

#include "llfocusmgr.h"

#include "llfontgl.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "lltooltip.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

#include <algorithm>

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
    // An image before the name, a square this wide, and the room after it.
    constexpr S32 IMAGE = 16;
    constexpr S32 IMAGE_GAP = 4;
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
    mShown.clear();
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

// A name that fits is itself. One that does not keeps its end -- the
// extension and a few letters before it -- and as much of its start as
// leaves room for an ellipsis between; where even the end will not fit,
// the whole is given back for the renderer to cut at its end.
std::string ALTabStrip::shortened(const LLFontGL* font, const std::string& label, S32 room)
{
    if (font->getWidth(label) <= room)
    {
        return label;
    }
    static const std::string ELLIPSIS = "\xE2\x80\xA6";
    // The end: the extension where the name has one near its end, with
    // three letters before it; else the last four letters.
    const size_t dot  = label.rfind('.');
    size_t       tail = dot != std::string::npos && label.size() - dot <= 6 ? dot : label.size();
    for (S32 letters = 0; letters < (dot != std::string::npos && tail == dot ? 3 : 4) && tail > 0; ++letters)
    {
        --tail;
        while (tail > 0 && (static_cast<unsigned char>(label[tail]) & 0xC0) == 0x80)
        {
            --tail;
        }
    }
    const std::string end   = label.substr(tail);
    const S32         fixed = font->getWidth(ELLIPSIS + end);
    if (fixed > room || tail < 2)
    {
        return label;
    }
    // The start: as many whole characters as fit before the ellipsis,
    // found by halving over the character boundaries, since a longer
    // start is never narrower.
    std::vector<size_t> bounds;
    for (size_t at = 0; at < tail; ++at)
    {
        if ((static_cast<unsigned char>(label[at]) & 0xC0) != 0x80)
        {
            bounds.push_back(at);
        }
    }
    bounds.push_back(tail);
    // bounds[fits] is the longest start that fits; bounds[0] is nothing.
    size_t fits = 0;
    size_t lo = 0, hi = bounds.size() - 1;
    while (lo < hi)
    {
        const size_t mid = (lo + hi + 1) / 2;
        if (font->getWidth(label.substr(0, bounds[mid])) + fixed <= room)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    fits = lo;
    return fits == 0 ? label : label.substr(0, bounds[fits]) + ELLIPSIS + end;
}

// Each tab wants the width of its words. Where they all fit, each gets
// that, and a name too long for the usual most a tab may be is let run on
// into whatever room is spare, up to half the strip, so that a long name
// shows whole while there is room to show it. Where they do not fit, the
// room is shared: the narrow ones keep what they want and the wide ones
// split what is left equally, down to the least a tab may be. The strip
// is not the place to decide that a document should not be shown, so
// past that the tabs run off the right edge rather than vanish.
void ALTabStrip::layout()
{
    mWidths.assign(mTabs.size(), 0);
    if (mTabs.empty())
    {
        return;
    }
    const S32        room = getRect().getWidth() - mGap * ((S32)mTabs.size() - 1);
    std::vector<S32> full(mTabs.size());
    S32              wanted = 0;
    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        // What the tab is drawn from: the padding, the mark, the image,
        // the words, the half pad before the way out, the way out, the
        // padding -- the same sum the drawing makes, or the words come
        // up a few pixels short and are cut for no reason.
        const S32 words = fontFor(mTabs[i])->getWidth(textOf(mTabs[i]));
        const S32 image = mTabs[i].image ? IMAGE + IMAGE_GAP : 0;
        full[i]         = llmax(PAD + MARK + image + words + PAD / 2 + CLOSE + PAD, mMinTabWidth);
        mWidths[i]      = llmin(full[i], mMaxTabWidth);
        wanted += mWidths[i];
    }
    if (wanted <= room)
    {
        // The spare room to the names cut short by the usual most, the
        // shortest of them served first so that each gets what it wants
        // before the longer split the rest.
        std::vector<size_t> cut;
        for (size_t i = 0; i < mTabs.size(); ++i)
        {
            if (full[i] > mWidths[i])
            {
                cut.push_back(i);
            }
        }
        std::sort(cut.begin(), cut.end(), [&](size_t a, size_t b) { return full[a] < full[b]; });
        S32 spare = room - wanted;
        for (size_t k = 0; k < cut.size() && spare > 0; ++k)
        {
            const size_t i     = cut[k];
            const S32    most  = llmax(mMaxTabWidth, room / 2);
            const S32    share = spare / static_cast<S32>(cut.size() - k);
            const S32    grow  = llmin(llmin(full[i], most) - mWidths[i], share);
            if (grow > 0)
            {
                mWidths[i] += grow;
                spare -= grow;
            }
        }
        return;
    }
    // Not enough: from the narrowest up, each that wants no more than an
    // equal share of what is left keeps what it wants; the first that
    // wants more sets the share, and it and every wider one take that,
    // down to the least, so that the cut ones are all one width.
    std::vector<size_t> order(mTabs.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return mWidths[a] < mWidths[b]; });
    S32 left = room;
    for (size_t k = 0; k < order.size(); ++k)
    {
        const S32 share = left / static_cast<S32>(order.size() - k);
        if (mWidths[order[k]] <= share)
        {
            left -= mWidths[order[k]];
            continue;
        }
        for (size_t wide = k; wide < order.size(); ++wide)
        {
            mWidths[order[wide]] = llmax(mMinTabWidth, share);
        }
        break;
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
        const bool badged = tab.badge.mV[VALPHA] > 0.f;
        if (tab.dirty || badged)
        {
            // The dot: a problem's colour where there is one, else the
            // ink for unsaved changes; filled for unsaved, a ring for a
            // problem in a saved tab.
            gGL.getTextureSlot(0)->unbind();
            gGL.color4fv(((badged ? tab.badge : ink.get()) % alpha).mV);
            gl_circle_2d((F32)x + DOT, (F32)height * 0.5f, DOT, 12, tab.dirty);
        }
        x += MARK;
        if (tab.image)
        {
            // The image, a square in the middle of the tab's height, in
            // the ink of the name.
            const S32 top = (height + IMAGE) / 2;
            tab.image->draw(LLRect(x, top, x + IMAGE, top - IMAGE), (current ? ink : quiet).get() % alpha);
            x += IMAGE + IMAGE_GAP;
        }

        // The name stops short of the way out, which every held tab has
        // and a preview does not; one too long is cut in the middle, so
        // that its end -- the extension, the number -- still shows.
        const S32 room = r.mRight - PAD - CLOSE - PAD / 2 - x;
        if (room > 0)
        {
            const U8 style = styleOf(tab);
            F32 after = (F32)x;
            // The name as cut for this room, kept from one frame to the
            // next until the room changes.
            if (i >= mShown.size())
            {
                mShown.resize(mTabs.size());
            }
            if (mShown[i].room != room || mShown[i].font != font || mShown[i].label != tab.label)
            {
                mShown[i].room  = room;
                mShown[i].font  = font;
                mShown[i].label = tab.label;
                mShown[i].text  = shortened(font, tab.label, room);
            }
            font->renderUTF8(mShown[i].text, 0, (F32)x, (F32)baseline, (current ? ink : quiet).get() % alpha,
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
    // Held, so that a drag along the strip may reorder.
    mPressed  = which;
    mPressX   = x;
    mDragging = false;
    gFocusMgr.setMouseCapture(this);
    return true;
}

bool ALTabStrip::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (hasMouseCapture())
    {
        gFocusMgr.setMouseCapture(nullptr);
        if (mDragging)
        {
            std::vector<std::string> order;
            for (const Tab& tab : mTabs)
            {
                order.push_back(tab.value);
            }
            mReorderedSignal(order);
        }
        mPressed  = -1;
        mDragging = false;
        return true;
    }
    return LLUICtrl::handleMouseUp(x, y, mask);
}

void ALTabStrip::onMouseCaptureLost()
{
    mPressed  = -1;
    mDragging = false;
}

bool ALTabStrip::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which >= 0)
    {
        // Chosen first, so that the menu is about what is in view.
        if (mTabs[which].value != mChosen)
        {
            mChosen = mTabs[which].value;
            mChosenSignal(mChosen);
        }
        mMenuSignal(mTabs[which].value, x, y);
        return true;
    }
    return LLUICtrl::handleRightMouseDown(x, y, mask);
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
    if (mPressed >= 0 && hasMouseCapture())
    {
        // A press that has travelled is a drag: the tab moves past a
        // neighbour once the mouse is past that neighbour's middle.
        if (!mDragging && std::abs(x - mPressX) > 4)
        {
            mDragging = true;
        }
        if (mDragging)
        {
            bool moved = true;
            while (moved)
            {
                moved = false;
                if (mPressed > 0 && x < rectOf((size_t)mPressed - 1).getCenterX())
                {
                    std::swap(mTabs[(size_t)mPressed], mTabs[(size_t)mPressed - 1]);
                    --mPressed;
                    layout();
                    moved = true;
                }
                else if (mPressed + 1 < (S32)mTabs.size() && x > rectOf((size_t)mPressed + 1).getCenterX())
                {
                    std::swap(mTabs[(size_t)mPressed], mTabs[(size_t)mPressed + 1]);
                    ++mPressed;
                    layout();
                    moved = true;
                }
            }
        }
        mHover = mPressed;
        mHoverX = x;
        mHoverY = y;
        return true;
    }
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

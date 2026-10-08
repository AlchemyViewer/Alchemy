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

#include "alsurface.h"
#include "llfocusmgr.h"

#include "alsaid.h"
#include "llfontgl.h"
#include "lllocalcliprect.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llstring.h"
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
    // The button listing every tab, at the right end while they overflow,
    // and the shade over a tab cut off at either edge.
    constexpr S32 LIST_W = 20;
    constexpr S32 SHADE = 12;
    // Pixels a wheel's notch moves the tabs.
    constexpr F32 WHEEL_PIXELS = 40.f;

    // A rect shaded across, from one colour at its left to another at its
    // right.
    void shadeAcross(const LLRect& r, const LLColor4& left, const LLColor4& right)
    {
        gGL.getTextureSlot(0)->unbind();
        gGL.begin(LLRender::TRIANGLES);
        {
            const auto corner = [](const LLColor4& c, S32 x, S32 y)
            {
                gGL.color4fv(c.mV);
                gGL.vertex2i(x, y);
            };
            corner(left, r.mLeft, r.mTop);
            corner(left, r.mLeft, r.mBottom);
            corner(right, r.mRight, r.mBottom);

            corner(left, r.mLeft, r.mTop);
            corner(right, r.mRight, r.mBottom);
            corner(right, r.mRight, r.mTop);
        }
        gGL.end();
    }
}

ALTabStrip::Params::Params()
:   min_tab_width("min_tab_width", 64),
    max_tab_width("max_tab_width", 220),
    gap("gap", 2),
    attached("attached", false)
{
}

ALTabStrip::ALTabStrip(const Params& p)
:   LLUICtrl(p),
    mMinTabWidth(p.min_tab_width),
    mMaxTabWidth(p.max_tab_width),
    mGap(p.gap),
    mAttached(p.attached)
{
}

void ALTabStrip::setTabs(std::vector<Tab> tabs, const std::string& chosen)
{
    // What each tab last drew, kept where it is the same tab by value
    // with the same name; the cutting measures the words, and a host
    // may fill the strip on every keystroke.
    std::vector<Shown> kept(tabs.size());
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        for (size_t was = 0; was < mTabs.size() && was < mShown.size(); ++was)
        {
            if (mTabs[was].value == tabs[i].value && mShown[was].label == tabs[i].label)
            {
                kept[i] = mShown[was];
                break;
            }
        }
    }
    const bool moved = chosen != mChosen || tabs.size() != mTabs.size();
    // A press held follows its tab into the new tabs, by value.
    const auto follow = [&](S32 at)
    {
        if (at < 0 || at >= static_cast<S32>(mTabs.size()))
        {
            return -1;
        }
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            if (tabs[i].value == mTabs[static_cast<size_t>(at)].value)
            {
                return static_cast<S32>(i);
            }
        }
        return -1;
    };
    const bool pressing = mPressed >= 0 || mPressedClose >= 0;
    mPressed      = follow(mPressed);
    mPressedClose = follow(mPressedClose);
    mTabs   = std::move(tabs);
    mChosen = chosen;
    mHover  = -1;
    mShown.swap(kept);
    layout();
    if (moved)
    {
        showChosen();
    }
    if (pressing && mPressed < 0 && mPressedClose < 0 && hasMouseCapture())
    {
        // The tab held is gone: so is the press.
        mDrag.cancel();
        gFocusMgr.setMouseCapture(nullptr);
    }
}

bool ALTabStrip::setTab(Tab tab)
{
    const S32 at = indexOf(tab.value);
    if (at < 0)
    {
        return false;
    }
    Tab&       was      = mTabs[static_cast<size_t>(at)];
    const bool measured = textOf(was) != textOf(tab) || styleOf(was) != styleOf(tab) || (was.image != nullptr) != (tab.image != nullptr);
    was                 = std::move(tab);
    if (measured)
    {
        layout();
        showChosen();
    }
    return true;
}

S32 ALTabStrip::indexOf(const std::string& value) const
{
    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        if (mTabs[i].value == value)
        {
            return static_cast<S32>(i);
        }
    }
    return -1;
}

void ALTabStrip::sayOrder()
{
    std::vector<std::string> order;
    order.reserve(mTabs.size());
    for (const Tab& tab : mTabs)
    {
        order.push_back(tab.value);
    }
    mReorderedSignal(order);
}

void ALTabStrip::choose(const std::string& value)
{
    mChosen = value;
    showChosen();
}

// Each tab's cut name goes with it, since it is kept by place.
void ALTabStrip::trade(size_t a, size_t b)
{
    std::swap(mTabs[a], mTabs[b]);
    if (a < mShown.size() && b < mShown.size())
    {
        std::swap(mShown[a], mShown[b]);
    }
    layout();
}

// What a mouse does to the chosen tab, with the keyboard. Each signal gets
// a copy of the value, as the mouse's do: whoever hears fills the strip
// afresh.
bool ALTabStrip::handleKeyHere(KEY key, MASK mask)
{
    const S32 at = indexOf(mChosen);
    if (at < 0)
    {
        // Nothing chosen: the first tab is where a walk starts. With no
        // tab at all the walk goes nowhere, and the arrow is still the
        // strip's -- passed on, it would walk the avatar.
        if (mask == MASK_NONE && (key == KEY_RIGHT || key == KEY_LEFT || key == KEY_HOME || key == KEY_END))
        {
            if (!mTabs.empty())
            {
                chooseKeyed(key == KEY_END || key == KEY_LEFT ? mTabs.size() - 1 : 0);
            }
            return true;
        }
        return LLUICtrl::handleKeyHere(key, mask);
    }
    const size_t here = static_cast<size_t>(at);
    const size_t last = mTabs.size() - 1;
    if (mask == MASK_NONE)
    {
        switch (key)
        {
            case KEY_LEFT: chooseKeyed(here > 0 ? here - 1 : here); return true;
            case KEY_RIGHT: chooseKeyed(here < last ? here + 1 : here); return true;
            case KEY_HOME: chooseKeyed(0); return true;
            case KEY_END: chooseKeyed(last); return true;
            case KEY_DELETE:
            {
                const std::string value = mChosen;
                mClosedSignal(value);
                return true;
            }
            case KEY_RETURN:
            case ' ':
            {
                const std::string value = mChosen;
                mHeldSignal(value);
                return true;
            }
            case KEY_DOWN: mListSignal(); return true;
            default: break;
        }
    }
    if (mask == MASK_SHIFT && (key == KEY_LEFT || key == KEY_RIGHT))
    {
        const bool left = key == KEY_LEFT;
        if (left ? here > 0 : here < last)
        {
            trade(here, left ? here - 1 : here + 1);
            showChosen();
            sayOrder();
        }
        return true;
    }
    if (mask == MASK_SHIFT && key == KEY_F10)
    {
        const LLRect      r     = rectOf(here);
        const std::string value = mChosen;
        mMenuSignal(value, r.getCenterX(), r.mBottom);
        return true;
    }
    return LLUICtrl::handleKeyHere(key, mask);
}

void ALTabStrip::chooseKeyed(size_t index)
{
    const std::string value = mTabs[index].value;
    if (value != mChosen)
    {
        mChosen = value;
        showChosen();
        mChosenSignal(value);
    }
    // Kept while the tabs are being walked: a host that answers a choice
    // by putting the keyboard in what it shows would end the walk at one.
    setFocus(true);
}

S32 ALTabStrip::contentWidth() const
{
    return mContentWidth;
}

bool ALTabStrip::overflowing() const
{
    return contentWidth() > getRect().getWidth();
}

S32 ALTabStrip::shownWidth() const
{
    return getRect().getWidth() - (overflowing() ? LIST_W : 0);
}

LLRect ALTabStrip::listRect() const
{
    if (!overflowing())
    {
        return LLRect();
    }
    return LLRect(getRect().getWidth() - LIST_W, getRect().getHeight(), getRect().getWidth(), 0);
}

void ALTabStrip::clampScroll()
{
    mScroll = llclamp(mScroll, 0, llmax(0, contentWidth() - shownWidth()));
}

void ALTabStrip::showChosen()
{
    for (size_t i = 0; i < mTabs.size() && i < mWidths.size(); ++i)
    {
        if (mTabs[i].value != mChosen)
        {
            continue;
        }
        const LLRect r = rectOf(i);
        if (r.mLeft < 0)
        {
            mScroll += r.mLeft;
        }
        else if (r.mRight > shownWidth())
        {
            mScroll += r.mRight - shownWidth();
        }
        break;
    }
    clampScroll();
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
    // three letters before it; else the last four letters. A letter is one
    // as a reader sees it, with the marks on it or what joins it.
    const size_t dot  = label.rfind('.');
    size_t       tail = dot != std::string::npos && label.size() - dot <= 6 ? dot : label.size();
    for (S32 letters = 0; letters < (dot != std::string::npos && tail == dot ? 3 : 4) && tail > 0; ++letters)
    {
        tail = utf8str_step_grapheme_backward(label, tail);
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
    std::vector<size_t> bounds = utf8str_grapheme_starts(label, tail);
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
    layoutWidths();
    mLefts.resize(mWidths.size());
    S32 left = 0;
    for (size_t i = 0; i < mWidths.size(); ++i)
    {
        mLefts[i] = left;
        left += mWidths[i] + mGap;
    }
    mContentWidth = mWidths.empty() ? 0 : left - mGap;
}

void ALTabStrip::layoutWidths()
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
    const S32 left  = (index < mLefts.size() ? mLefts[index] : mWidths.empty() ? 0 : mContentWidth + mGap) - mScroll;
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
    if (x < 0 || x >= shownWidth())
    {
        return -1;
    }
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
    showChosen();
}

void ALTabStrip::draw()
{
    static const LLUIColor shown = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::grey4);
    static const LLUIColor rest = LLUIColorTable::instance().getColor("StudioTabColor", LLColor4::grey3);
    const LLUIColor& edge = ALSurface::well();
    const LLUIColor& ink = ALSurface::text();
    const LLUIColor& quiet = ALSurface::quiet();

    const S32 height = getRect().getHeight();
    const F32 alpha = getDrawContext().mAlpha;
    const S32 shown_width = shownWidth();

    {
    // The tabs within the room they are shown in; the list button, when
    // there is one, has the rest.
    LLLocalClipRect clip(LLRect(0, height, shown_width, 0));
    for (size_t i = 0; i < mTabs.size(); ++i)
    {
        const Tab& tab = mTabs[i];
        const bool current = tab.value == mChosen;
        const bool hovered = (S32)i == mHover;
        const LLRect r = rectOf(i);
        if (r.mRight <= 0)
        {
            continue;
        }
        if (r.mLeft >= shown_width)
        {
            break;
        }
        const LLFontGL* font = fontFor(tab);
        const S32 baseline = (height - font->getLineHeight()) / 2;

        // The shown tab is the face of what is under it; the rest sit back.
        // Where the strip sits on what it shows, the shown tab opens into
        // it: its bottom edge filled over, between its sides.
        gl_rect_2d(r, (current ? shown : rest).get() % alpha, true);
        gl_rect_2d(r, edge.get() % alpha, false);
        if (current && mAttached)
        {
            gl_rect_2d(r.mLeft + 1, r.mBottom + 1, r.mRight - 1, r.mBottom, shown.get() % alpha, true);
        }
        if (current && hasFocus())
        {
            // Where the keyboard is, while the strip has it.
            gl_rect_2d(r.mLeft + 1, r.mTop - 1, r.mRight - 1, r.mBottom + 1, gFocusMgr.getFocusColor() % alpha, false);
        }

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

        // The name stops short of the way out; one too long is cut in the
        // middle, so that its end -- the extension, the number -- still
        // shows.
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
        {
            // Brighter under the pointer, so that a press there is plainly
            // a press on it and not on the tab.
            const LLRect c = closeRectOf(i);
            const bool over = hovered && c.pointInRect(mHoverX, mHoverY) && (mPressedClose < 0 || mPressedClose == (S32)i);
            if (over && mPressedClose == (S32)i)
            {
                // Held: a press let go of here closes it.
                gl_rect_2d(c.mLeft - 2, c.mTop + 2, c.mRight + 2, c.mBottom - 2, rest.get() % alpha, true);
            }
            font->renderUTF8("\xC3\x97", 0, (F32)c.getCenterX(), (F32)baseline,
                             (over || current ? ink : quiet).get() % alpha,
                             LLFontGL::HCENTER, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        }
    }
    // A shade over whichever end has tabs past it, so that a cut tab reads
    // as one that goes on rather than one that ends there.
    const LLColor4 dark = rest.get() % alpha;
    const LLColor4 none = LLColor4(dark.mV[VRED], dark.mV[VGREEN], dark.mV[VBLUE], 0.f);
    if (mScroll > 0)
    {
        shadeAcross(LLRect(0, height, SHADE, 0), dark, none);
    }
    if (contentWidth() - mScroll > shown_width)
    {
        shadeAcross(LLRect(shown_width - SHADE, height, shown_width, 0), none, dark);
    }
    }

    if (overflowing())
    {
        // The button that lists every tab: a small triangle, brighter
        // under the pointer.
        const LLRect list = listRect();
        const bool   over = mHover < 0 && list.pointInRect(mHoverX, mHoverY) && mHoverX >= 0;
        gl_rect_2d(list, rest.get() % alpha, true);
        gl_rect_2d(list, edge.get() % alpha, false);
        const S32 cx = list.getCenterX();
        const S32 cy = list.getCenterY();
        gl_triangle_2d(cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 3, (over ? ink : quiet).get() % alpha, true);
    }
    LLUICtrl::draw();
}

bool ALTabStrip::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (listRect().pointInRect(x, y))
    {
        mListSignal();
        return true;
    }
    const S32 which = at(x, y);
    if (which < 0)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    if (closeRectOf(which).pointInRect(x, y))
    {
        // Closed when let go of over the way out, so a press can still be
        // taken back by sliding off it.
        mPressedClose = which;
        gFocusMgr.setMouseCapture(this);
        return true;
    }
    // The value, copied: whoever hears of the choice fills the strip
    // afresh, and the tab it was read from goes with the old tabs.
    const std::string value = mTabs[which].value;
    if (value != mChosen)
    {
        mChosen = value;
        mChosenSignal(value);
    }
    // Held, so that a drag along the strip may reorder: the tab, wherever
    // the choice left it.
    mPressed = indexOf(value);
    if (mPressed < 0)
    {
        return true;
    }
    mDrag.press(x, y);
    gFocusMgr.setMouseCapture(this);
    return true;
}

bool ALTabStrip::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (hasMouseCapture() && mPressedClose >= 0)
    {
        // Read before letting go: losing the capture forgets the press.
        const S32 which = mPressedClose;
        mPressedClose   = -1;
        gFocusMgr.setMouseCapture(nullptr);
        if (which < (S32)mTabs.size() && closeRectOf(which).pointInRect(x, y))
        {
            const std::string value = mTabs[which].value;
            mClosedSignal(value);
        }
        return true;
    }
    if (hasMouseCapture())
    {
        // Likewise: letting go forgets the drag, and a drag forgotten
        // before it was told left the host's tabs in the old order. A tab
        // let go of off the strip is torn out, which is said after the
        // order it was dragged into on the way.
        const bool        dragged = mDrag.dragging();
        const bool        torn    = mTearing && mPressed >= 0 && mPressed < (S32)mTabs.size();
        const std::string value   = torn ? mTabs[(size_t)mPressed].value : std::string();
        mPressed                  = -1;
        mTearing                  = false;
        mDrag.release();
        gFocusMgr.setMouseCapture(nullptr);
        if (dragged)
        {
            sayOrder();
        }
        if (torn)
        {
            S32 screen_x = 0, screen_y = 0;
            localPointToScreen(x, y, &screen_x, &screen_y);
            mTornSignal(value, screen_x, screen_y);
        }
        return true;
    }
    return LLUICtrl::handleMouseUp(x, y, mask);
}

// Taken away mid-drag: the tabs are where the drag left them, which the
// host is told, rather than the strip and the host going on in two
// orders until the next fill.
void ALTabStrip::onMouseCaptureLost()
{
    const bool dragged = mDrag.dragging();
    mPressed      = -1;
    mTearing      = false;
    mDrag.cancel();
    mPressedClose = -1;
    if (dragged)
    {
        sayOrder();
    }
}

bool ALTabStrip::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (!overflowing())
    {
        return LLUICtrl::handleScrollWheel(x, y, delta);
    }
    // Along the tabs, by the wheel's own fractions where it has them.
    mScrollRemainder += delta.mPrecise * WHEEL_PIXELS;
    const S32 pixels = static_cast<S32>(mScrollRemainder);
    mScrollRemainder -= static_cast<F32>(pixels);
    mScroll += pixels;
    clampScroll();
    mHover = at(x, y);
    return true;
}

bool ALTabStrip::handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta)
{
    return handleScrollWheel(x, y, delta);
}

bool ALTabStrip::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which >= 0)
    {
        // Chosen first, so that the menu is about what is in view; by a
        // copy of the value, which the choice may fill the strip under.
        const std::string value = mTabs[which].value;
        if (value != mChosen)
        {
            mChosen = value;
            mChosenSignal(value);
        }
        mMenuSignal(value, x, y);
        return true;
    }
    return LLUICtrl::handleRightMouseDown(x, y, mask);
}

bool ALTabStrip::handleMiddleMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which >= 0)
    {
        const std::string value = mTabs[which].value;
        mClosedSignal(value);
        return true;
    }
    return LLUICtrl::handleMiddleMouseDown(x, y, mask);
}

bool ALTabStrip::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    const S32 which = at(x, y);
    if (which >= 0 && !closeRectOf(which).pointInRect(x, y))
    {
        const std::string value = mTabs[which].value;
        mHeldSignal(value);
        return true;
    }
    return LLUICtrl::handleDoubleClick(x, y, mask);
}

bool ALTabStrip::handleHover(S32 x, S32 y, MASK mask)
{
    if (mPressedClose >= 0 && hasMouseCapture())
    {
        mHover  = mPressedClose;
        mHoverX = x;
        mHoverY = y;
        return true;
    }
    if (mPressed >= 0 && hasMouseCapture())
    {
        // Pulled well off the strip, any way, the tab is being torn out: it
        // stops finding its place along the strip, and the cursor says it
        // is carried. Brought back, it goes on finding it.
        const LLRect local = getLocalRect();
        mTearing           = y < local.mBottom - TEAR || y > local.mTop + TEAR || x < local.mLeft - TEAR || x > local.mRight + TEAR;
        if (mTearing)
        {
            if (LLWindow* window = getWindow())
            {
                window->setCursor(UI_CURSOR_ARROWDRAG);
            }
            mHover  = mPressed;
            mHoverX = x;
            mHoverY = y;
            return true;
        }
        // A press that has travelled along the strip is a drag: the tab
        // moves past a neighbour once the mouse is past that neighbour's
        // middle.
        if (mDrag.moved(x, y))
        {
            bool moved = true;
            while (moved && mPressed < (S32)mTabs.size())
            {
                moved = false;
                if (mPressed > 0 && x < rectOf((size_t)mPressed - 1).getCenterX())
                {
                    trade((size_t)mPressed, (size_t)mPressed - 1);
                    --mPressed;
                    moved = true;
                }
                else if (mPressed + 1 < (S32)mTabs.size() && x > rectOf((size_t)mPressed + 1).getCenterX())
                {
                    trade((size_t)mPressed, (size_t)mPressed + 1);
                    ++mPressed;
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
    if (listRect().pointInRect(x, y))
    {
        LLToolTipMgr::instance().show(alSaid("TabStripList", "All tabs"));
        return true;
    }
    const S32 which = at(x, y);
    if (which < 0 || mTabs[which].toolTip.empty())
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    LLToolTipMgr::instance().show(mTabs[which].toolTip);
    return true;
}

/**
 * @file alcanvasview.cpp
 * @brief A view tree shown on a surface that can be zoomed and scrolled.
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

#include "alcanvasview.h"

#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llkeyboard.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "lluicolortable.h"
#include "llwindow.h"

#include <array>
#include <cmath>
#include <fmt/format.h>

namespace
{
    // What a surface keeps around what is on it, so nothing is drawn hard
    // against the edge of the thing it is shown on.
    constexpr S32 SURFACE_MARGIN = 4;
    // The least a surface may be whatever it was told, since a surface of
    // no area is one a container has nothing to place.
    constexpr S32 LEAST_WIDTH = 120;
    constexpr S32 LEAST_HEIGHT = 40;
    // The steps the wheel goes through, within the zoom's bounds.
    constexpr std::array<F32, 10> ZOOM_STEPS = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f };

    constexpr std::pair<const char*, ALCanvasView::Backdrop> BACKDROPS[] = {
        { "none",    ALCanvasView::Backdrop::None },
        { "pattern", ALCanvasView::Backdrop::Pattern },
        { "checker", ALCanvasView::Backdrop::Checker },
        { "window",  ALCanvasView::Backdrop::Window },
        { "light",   ALCanvasView::Backdrop::Light },
    };

    // The container's window where there is one: the room a surface is
    // shown in is what the container leaves after its own scrollbars, and
    // the parent's rect otherwise.
    LLRect roomOf(const LLView* view)
    {
        if (LLScrollContainer* scroller = view->getParentByType<LLScrollContainer>())
        {
            return scroller->getContentWindowRect();
        }

        return view->getParent() ? view->getParent()->getLocalRect() : view->getLocalRect();
    }
}

void ALCanvasView::setRoot(LLView* root)
{
    mRoot = root;
    rememberRoot();
}

void ALCanvasView::clear()
{
    deleteAllChildren();
    mRoot = nullptr;
    mNeedWidth = 0;
    mNeedHeight = 0;
}

void ALCanvasView::rememberRoot()
{
    if (!mRoot)
    {
        mNeedWidth = 0;
        mNeedHeight = 0;
        return;
    }
    // Measured against the surface as it is, because that is the surface the
    // root is sitting on. Clearing what the surface needs before reading it
    // measures the root against a smaller one, and then puts it back that
    // much higher and larger -- which is a preview that jumps every time it
    // is moved and grows the surface under itself until it is off the far
    // end of what can be scrolled to.
    const LLView* parent = mRoot->getParent();
    const LLRect placed = mRoot->getRect();
    mAnchorLeft = placed.mLeft;
    mAnchorTop = (parent == this ? surfaceHeight() : parent->getRect().getHeight()) - placed.mTop;

    // What is actually drawn, which is not always the rect the root claims:
    // a child placed past its parent's edge is drawn past it, and a surface
    // cut to the rect leaves that drawing clipped with nowhere to scroll to
    // -- which is the one thing a canvas that scrolls exists to prevent.
    mRoot->setUseBoundingRect(true);
    mRoot->updateBoundingRect();
    LLRect drawn(placed);
    drawn.unionWith(mRoot->getBoundingRect());

    // What spills off the near sides is room the root has to be moved over
    // by: there is no growing off the top left of a surface, so a root put
    // that way stops at the corner rather than disappearing over it.
    mAnchorLeft = llmax(mAnchorLeft, placed.mLeft - drawn.mLeft);
    mAnchorTop = llmax(mAnchorTop, drawn.mTop - placed.mTop);
    // So the surface reaches from its own top left corner to the far side of
    // everything that is drawn, wherever the root has been put.
    mNeedWidth = mAnchorLeft + placed.getWidth() + (drawn.mRight - placed.mRight) + SURFACE_MARGIN;
    mNeedHeight = mAnchorTop + placed.getHeight() + (placed.mBottom - drawn.mBottom) + SURFACE_MARGIN;
    mPlaced = true;

    resurface();
    anchorRoot();
}

// Held across a clear, because a clear is what a rebuild starts with.
bool ALCanvasView::keptPlace(S32& left, S32& down) const
{
    if (!mPlaced)
    {
        return false;
    }
    left = mAnchorLeft;
    down = mAnchorTop;
    return true;
}

// A surface that is the one thing in its container fills the container,
// so what is on it sits in the middle of the room until it outgrows it; a
// surface in a row is given its share by the row. Then, where anchorRoot
// would put the root against where it is: the two differ exactly when
// something other than this surface moved it.
void ALCanvasView::refresh()
{
    if (mSizable && getParent() && getParent()->as<LLScrollContainer>())
    {
        const LLRect room = roomRect();
        setLeastSurface(room.getWidth(), room.getHeight());
    }
    if (!mRoot)
    {
        return;
    }
    const LLView* held = mRoot;
    while (held && held->getParent() != this)
    {
        held = held->getParent();
    }
    if (!held)
    {
        return;
    }
    const S32 room = held == mRoot ? surfaceHeight() : held->getRect().getHeight();
    if (mRoot->getRect().mLeft != mAnchorLeft || room - mRoot->getRect().mTop != mAnchorTop)
    {
        rememberRoot();
    }
}

void ALCanvasView::anchorRoot()
{
    if (!mRoot)
    {
        return;
    }
    LLView* held = mRoot;
    while (held && held->getParent() != this)
    {
        held = held->getParent();
    }
    if (!held)
    {
        return;
    }
    const LLRect surface = surfaceRect();
    if (held != mRoot)
    {
        // A holder covers the whole surface and the root sits in it.
        held->setShape(surface);
    }
    const S32 room = held == mRoot ? surface.getHeight() : held->getRect().getHeight();
    mRoot->setOrigin(mAnchorLeft, room - mAnchorTop - mRoot->getRect().getHeight());
}

void ALCanvasView::fitContent(S32 width, S32 height)
{
    mContentWidth = llmax(width, LEAST_WIDTH);
    mContentHeight = llmax(height, LEAST_HEIGHT);
    resurface();
}

void ALCanvasView::setLeastSurface(S32 width, S32 height)
{
    if (mLeastWidth != width || mLeastHeight != height)
    {
        mLeastWidth = width;
        mLeastHeight = height;
        resurface();
    }
}

// The room is in drawn units and the surface is in its own, so the room is
// divided by the zoom to be compared with the rest -- and rounded down,
// never to nearest: a surface drawn a pixel or two past the room it was
// told to fill brings out a scrollbar, the scrollbar takes room, the
// smaller room rounds the other way and the scrollbar goes, and so on
// every frame.
S32 ALCanvasView::surfaceWidth() const
{
    return llmax(mContentWidth, mNeedWidth, (S32)std::floor((F32)mLeastWidth / mZoom));
}

S32 ALCanvasView::surfaceHeight() const
{
    return llmax(mContentHeight, mNeedHeight, (S32)std::floor((F32)mLeastHeight / mZoom));
}

void ALCanvasView::resurface()
{
    if (mSizable)
    {
        // The surface takes up what it is drawn as, so a zoomed one scrolls
        // by what it covers rather than by what it measures.
        reshape(ll_round((F32)surfaceWidth() * mZoom), ll_round((F32)surfaceHeight() * mZoom));
    }
}

void ALCanvasView::setZoom(F32 zoom)
{
    zoom = llclamp(zoom, MIN_ZOOM, MAX_ZOOM);
    if (mZoom != zoom)
    {
        mZoom = zoom;
        resurface();
        // A surface whose size in its own coordinates did not change was not
        // reshaped, and then nothing has put the root back.
        anchorRoot();
    }
}

F32 ALCanvasView::steppedZoom(F32 from, S32 direction)
{
    if (direction > 0)
    {
        for (const F32 step : ZOOM_STEPS)
        {
            if (step > from + 0.001f)
            {
                return llclamp(step, MIN_ZOOM, MAX_ZOOM);
            }
        }
        return MAX_ZOOM;
    }
    for (auto it = ZOOM_STEPS.rbegin(); it != ZOOM_STEPS.rend(); ++it)
    {
        if (*it < from - 0.001f)
        {
            return llclamp(*it, MIN_ZOOM, MAX_ZOOM);
        }
    }
    return MIN_ZOOM;
}

F32 ALCanvasView::fittingZoom() const
{
    const LLRect room = roomRect();
    const F32 width = (F32)mContentWidth;
    const F32 height = (F32)mContentHeight;
    F32 fit = ZOOM_STEPS.front();
    for (const F32 step : ZOOM_STEPS)
    {
        if (width * step <= (F32)room.getWidth() && height * step <= (F32)room.getHeight())
        {
            fit = step;
        }
    }
    return llclamp(fit, MIN_ZOOM, MAX_ZOOM);
}

LLRect ALCanvasView::roomRect() const
{
    return roomOf(this);
}

ALCanvasView::Backdrop ALCanvasView::backdropNamed(std::string_view name)
{
    for (const auto& [known, backdrop] : BACKDROPS)
    {
        if (name == known)
        {
            return backdrop;
        }
    }
    return Backdrop::None;
}

const char* ALCanvasView::backdropName(Backdrop backdrop)
{
    for (const auto& [name, known] : BACKDROPS)
    {
        if (known == backdrop)
        {
            return name;
        }
    }
    return "none";
}

std::string ALCanvasView::legend() const
{
    return fmt::format("{}%", ll_round(mZoom * 100.f));
}

// The point under the pointer before, in the surface's own coordinates
// and in the window's; the zoom; then the container scrolled so that the
// same point of the surface is at the same place in the window. Whoever
// listens for the zoom is told between the two, since a row of surfaces
// lays itself out again on hearing it and the scroll is measured against
// what the container then holds.
void ALCanvasView::zoomAbout(F32 zoom, S32 x, S32 y)
{
    zoom = llclamp(zoom, MIN_ZOOM, MAX_ZOOM);
    if (zoom == mZoom)
    {
        return;
    }
    LLScrollContainer* scroller = getParentByType<LLScrollContainer>();
    const F32 cx = (F32)x / mZoom;
    const F32 cy = (F32)y / mZoom;
    S32 wx = 0;
    S32 wy = 0;
    if (scroller)
    {
        localPointToOtherView(x, y, &wx, &wy, scroller);
        const LLRect window = scroller->getContentWindowRect();
        wx -= window.mLeft;
        wy -= window.mBottom;
    }
    setZoom(zoom);
    mZoomChange(mZoom);
    if (!scroller || !scroller->getScrolledView())
    {
        return;
    }
    S32 dx = ll_round(cx * mZoom);
    S32 dy = ll_round(cy * mZoom);
    localPointToOtherView(dx, dy, &dx, &dy, scroller->getScrolledView());
    scroller->scrollToShowRect(LLRect(dx, dy + 1, dx + 1, dy), LLRect(wx, wy + 1, wx + 1, wy));
}

// A wheel event carries no modifiers of its own, so the keyboard is asked
// what is held.
bool ALCanvasView::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    const MASK held = gKeyboard ? gKeyboard->currentMask(false) : MASK_NONE;
    if (!(held & MASK_CONTROL) || delta.mClicks == 0)
    {
        toContent(x, y);
        return LLPanel::handleScrollWheel(x, y, delta);
    }
    zoomAbout(steppedZoom(delta.mClicks < 0 ? 1 : -1), x, y);
    return true;
}

// ---------------------------------------------------------------------------
// Panning
// ---------------------------------------------------------------------------

bool ALCanvasView::panGesture() const
{
    return mDragPans || (gKeyboard && gKeyboard->getKeyDown(' '));
}

// Measured on the screen rather than on the surface, since the surface is
// what moves: a point of it is somewhere else once it has been scrolled.
void ALCanvasView::beginPan(S32 x, S32 y)
{
    localPointToScreen(x, y, &mPanScreenX, &mPanScreenY);
    const LLScrollContainer* scroller = getParentByType<LLScrollContainer>();
    mPanDocX = scroller ? scroller->getDocPosHorizontal() : 0;
    mPanDocY = scroller ? scroller->getDocPosVertical() : 0;
    mPanning = true;
    gFocusMgr.setMouseCapture(this);
}

void ALCanvasView::panTo(S32 x, S32 y)
{
    if (!mPanning)
    {
        return;
    }
    S32 sx = 0;
    S32 sy = 0;
    localPointToScreen(x, y, &sx, &sy);
    LLScrollContainer* scroller = getParentByType<LLScrollContainer>();
    if (!scroller)
    {
        return;
    }
    // The surface follows the hand: dragged right, what is seen moves
    // right, which is the container scrolled left.
    scroller->setDocPosHorizontal(mPanDocX - (sx - mPanScreenX));
    scroller->setDocPosVertical(mPanDocY + (sy - mPanScreenY));
}

void ALCanvasView::endPan()
{
    mPanning = false;
    if (gFocusMgr.getMouseCapture() == this)
    {
        gFocusMgr.setMouseCapture(nullptr);
    }
}

void ALCanvasView::panBy(S32 dx, S32 dy)
{
    if (LLScrollContainer* scroller = getParentByType<LLScrollContainer>())
    {
        scroller->setDocPosHorizontal(scroller->getDocPosHorizontal() - dx);
        scroller->setDocPosVertical(scroller->getDocPosVertical() + dy);
    }
}

// The pointer arrives in drawn pixels and what is on the surface is laid
// out in its own, so the zoom is taken out here for everything under this
// -- a subclass with a pointer of its own takes it out itself and comes
// here with the drawn point for the rest.
bool ALCanvasView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (panGesture())
    {
        beginPan(x, y);
        return true;
    }
    toContent(x, y);
    return LLPanel::handleMouseDown(x, y, mask);
}

bool ALCanvasView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mPanning)
    {
        endPan();
        return true;
    }
    toContent(x, y);
    return LLPanel::handleMouseUp(x, y, mask);
}

bool ALCanvasView::handleMiddleMouseDown(S32 x, S32 y, MASK mask)
{
    beginPan(x, y);
    return true;
}

bool ALCanvasView::handleMiddleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mPanning)
    {
        endPan();
        return true;
    }
    toContent(x, y);
    return LLPanel::handleMiddleMouseUp(x, y, mask);
}

bool ALCanvasView::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    toContent(x, y);
    return LLPanel::handleRightMouseDown(x, y, mask);
}

bool ALCanvasView::handleRightMouseUp(S32 x, S32 y, MASK mask)
{
    toContent(x, y);
    return LLPanel::handleRightMouseUp(x, y, mask);
}

bool ALCanvasView::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    toContent(x, y);
    return LLPanel::handleDoubleClick(x, y, mask);
}

bool ALCanvasView::handleToolTip(S32 x, S32 y, MASK mask)
{
    toContent(x, y);
    return LLPanel::handleToolTip(x, y, mask);
}

bool ALCanvasView::handleHover(S32 x, S32 y, MASK mask)
{
    if (mPanning)
    {
        panTo(x, y);
        getWindow()->setCursor(UI_CURSOR_HAND);
        return true;
    }
    if (panGesture())
    {
        getWindow()->setCursor(UI_CURSOR_HAND);
        return true;
    }
    toContent(x, y);
    return LLPanel::handleHover(x, y, mask);
}

void ALCanvasView::onMouseCaptureLost()
{
    mPanning = false;
    LLPanel::onMouseCaptureLost();
}

// What the backdrop is, for the words over it: light or dark by its
// luminance, and busy where it is the pattern. The panel's own colour,
// where there is no backdrop, is taken as dark: every canvas region in
// the viewer is.
LLColor4 ALCanvasView::inkColor(bool quiet) const
{
    static const LLUIColor window = LLUIColorTable::instance().getColor("FloaterFocusBackgroundColor", LLColor4::grey4);

    F32 luminance = 0.2f;
    switch (mBackdrop)
    {
        case Backdrop::None:    luminance = 0.2f; break;
        case Backdrop::Pattern: luminance = 0.5f; break;
        case Backdrop::Checker: luminance = 0.85f; break;
        case Backdrop::Light:   luminance = 0.92f; break;
        case Backdrop::Window:
        {
            const LLColor4 c = window.get();
            luminance = 0.2126f * c.mV[0] + 0.7152f * c.mV[1] + 0.0722f * c.mV[2];
            break;
        }
    }
    const bool dark_ink = luminance > 0.6f;
    const F32 tone = dark_ink ? (quiet ? 0.25f : 0.08f) : (quiet ? 0.8f : 1.f);
    return LLColor4(tone, tone, tone, 1.f);
}

LLFontGL::ShadowType ALCanvasView::inkShadow() const
{
    return mBackdrop == Backdrop::Pattern ? LLFontGL::DROP_SHADOW : LLFontGL::NO_SHADOW;
}

void ALCanvasView::drawChrome()
{
    const LLRect seen = viewportRect();
    if (empty())
    {
        if (!mHint.empty())
        {
            LLFontGL::getFontSansSerif()->renderUTF8(mHint, 0, seen.getCenterX(), seen.getCenterY(), inkColor(true),
                                                     LLFontGL::HCENTER, LLFontGL::VCENTER, LLFontGL::NORMAL, inkShadow());
        }
        return;
    }
    const std::string words = legend();
    if (!words.empty())
    {
        LLFontGL::getFontSansSerifSmall()->renderUTF8(words, 0, seen.mRight - 6, seen.mBottom + 6, inkColor(true),
                                                      LLFontGL::RIGHT, LLFontGL::BOTTOM, LLFontGL::NORMAL, inkShadow());
    }
    drawRulers();
}

LLRect ALCanvasView::rulerOrigin() const
{
    return mRoot && mRoot != this ? localRectOf(mRoot) : LLRect(0, surfaceHeight(), surfaceWidth(), 0);
}

// The rules are drawn on the canvas and numbered on the surface: a mark
// for surface coordinate n goes at n times the zoom, from the corner of
// what the surface says they measure from.
void ALCanvasView::drawRulers() const
{
    static const LLUIColor ground = LLUIColorTable::instance().getColor("CanvasRulerGround", LLColor4(0.169f, 0.169f, 0.169f, 0.85f));
    static const LLUIColor marks_color = LLUIColorTable::instance().getColor("CanvasRulerInk", LLColor4::white);
    static const LLUIColor highlight = LLUIColorTable::instance().getColor("CanvasRulerMark", LLColor4::red);
    static constexpr S32 RULER = 14;
    static constexpr S32 LABEL_EVERY = 50;

    if (!rulersShown() || empty())
    {
        return;
    }
    const LLRect view = viewportRect();
    if (view.getWidth() <= RULER * 2 || view.getHeight() <= RULER * 2)
    {
        return;
    }
    const LLRect origin = rulerOrigin();
    const LLColor4 marks = marks_color.get();
    const S32 rule_bottom = view.mTop - RULER;
    const S32 rule_right = view.mLeft + RULER;
    const LLRect top(view.mLeft, view.mTop, view.mRight, rule_bottom);
    const LLRect left(view.mLeft, rule_bottom, rule_right, view.mBottom);
    gl_rect_2d(top, ground.get(), true);
    gl_rect_2d(left, ground.get(), true);
    gl_rect_2d(top, marks, false);
    gl_rect_2d(left, marks, false);

    // The first mark at or after a coordinate. Rounding towards zero is
    // not rounding down, and a rule that reaches left of what it measures
    // has negative numbers on it.
    const auto from = [](S32 value, S32 step)
    {
        const S32 n = value >= 0 ? (value + step - 1) / step : -((-value) / step);
        return n * step;
    };
    const S32 step = llmax(rulerStep(), 1);
    // A mark every step, unless the steps are drawn too close to tell
    // apart, and then every few of them.
    S32 every = step;
    while ((F32)every * mZoom < 4.f)
    {
        every += step;
    }
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const auto onCanvas = [this](S32 content) { return ll_round((F32)content * mZoom); };

    for (S32 fx = from(ll_round((F32)rule_right / mZoom) - origin.mLeft, every);
         onCanvas(origin.mLeft + fx) <= view.mRight; fx += every)
    {
        const S32 x = onCanvas(origin.mLeft + fx);
        const bool named = fx % LABEL_EVERY == 0;
        gl_line_2d(x, rule_bottom, x, rule_bottom + (named ? 5 : 3), marks);
        if (named)
        {
            font->renderUTF8(std::to_string(fx), 0, x + 2, rule_bottom + 3, marks, LLFontGL::LEFT, LLFontGL::BOTTOM);
        }
    }
    for (S32 fy = from(origin.mTop - ll_round((F32)rule_bottom / mZoom), every);
         onCanvas(origin.mTop - fy) >= view.mBottom; fy += every)
    {
        const S32 y = onCanvas(origin.mTop - fy);
        const bool named = fy % LABEL_EVERY == 0;
        gl_line_2d(view.mLeft, y, view.mLeft + (named ? 5 : 3), y, marks);
        if (named)
        {
            font->renderUTF8(std::to_string(fy), 0, view.mLeft + 2, y - 10, marks, LLFontGL::LEFT, LLFontGL::BOTTOM);
        }
    }

    // Where the chosen thing sits, on both rules.
    LLRect content;
    if (rulerHighlight(content))
    {
        const LLRect box(onCanvas(content.mLeft), onCanvas(content.mTop), onCanvas(content.mRight), onCanvas(content.mBottom));
        gl_rect_2d(LLRect(llmax(box.mLeft, rule_right), view.mTop, llmin(box.mRight, view.mRight), rule_bottom),
                   highlight.get(), false);
        gl_rect_2d(LLRect(view.mLeft, llmin(box.mTop, rule_bottom), rule_right, llmax(box.mBottom, view.mBottom)),
                   highlight.get(), false);
    }
}

// Bands of colour, two discs and some text: enough contrast, edges and
// detail for blur, refraction, dispersion and a rim to each show as
// themselves.
static void drawPatternBackdrop(const LLRect& area)
{
    static const LLColor4 BANDS[] = {
        { 0.86f, 0.25f, 0.24f, 1.f }, { 0.96f, 0.62f, 0.18f, 1.f }, { 0.93f, 0.86f, 0.30f, 1.f },
        { 0.30f, 0.72f, 0.42f, 1.f }, { 0.22f, 0.50f, 0.90f, 1.f }, { 0.56f, 0.36f, 0.80f, 1.f }
    };
    constexpr S32 COUNT = 6;
    for (S32 i = 0; i < COUNT; ++i)
    {
        const S32 left = area.mLeft + area.getWidth() * i / COUNT;
        const S32 right = area.mLeft + area.getWidth() * (i + 1) / COUNT;
        gl_rect_2d(LLRect(left, area.mTop, right, area.mBottom), BANDS[i], true);
    }
    const F32 radius = llmax(6.f, (F32)area.getHeight() * 0.16f);
    gGL.color4f(1.f, 1.f, 1.f, 1.f);
    gl_circle_2d((F32)area.mLeft + (F32)area.getWidth() * 0.28f, (F32)area.mBottom + (F32)area.getHeight() * 0.62f, radius, 32, true);
    gGL.color4f(0.08f, 0.08f, 0.1f, 1.f);
    gl_circle_2d((F32)area.mLeft + (F32)area.getWidth() * 0.68f, (F32)area.mBottom + (F32)area.getHeight() * 0.36f, radius, 32, true);
    LLFontGL::getFontSansSerifHuge()->renderUTF8("Aa Bb", 0, (F32)area.mLeft + (F32)area.getWidth() * 0.5f,
                                                 (F32)area.mBottom + (F32)area.getHeight() * 0.5f, LLColor4::white,
                                                 LLFontGL::HCENTER, LLFontGL::VCENTER, LLFontGL::BOLD, LLFontGL::DROP_SHADOW);
}

void ALCanvasView::drawBackdrop(const LLRect& area) const
{
    static const LLUIColor window = LLUIColorTable::instance().getColor("FloaterFocusBackgroundColor", LLColor4::grey4);

    switch (mBackdrop)
    {
        case Backdrop::None:
            break;
        case Backdrop::Pattern:
            drawPatternBackdrop(area);
            break;
        case Backdrop::Checker:
            gl_rect_2d_checkerboard(area, 1.f);
            break;
        case Backdrop::Window:
            gl_rect_2d(area, window.get(), true);
            break;
        case Backdrop::Light:
            gl_rect_2d(area, LLColor4(0.92f, 0.92f, 0.92f, 1.f), true);
            break;
    }
}

void ALCanvasView::toContent(S32& x, S32& y) const
{
    if (mZoom != 1.f)
    {
        x = ll_round((F32)x / mZoom);
        y = ll_round((F32)y / mZoom);
    }
}

LLRect ALCanvasView::getSnapRect() const
{
    // Held out past every edge rather than turned off, because a child asks
    // its parent where its edges are and there is no answer for "nowhere".
    // Far enough that none of them is within snapping distance of anything on
    // the surface, whatever that distance has been set to.
    constexpr S32 OUT_OF_REACH = 1 << 20;
    LLRect nothing_to_line_up_with(getRect());
    nothing_to_line_up_with.stretch(OUT_OF_REACH);
    return nothing_to_line_up_with;
}

// The surface changes size; what is on it does not. A view carries its
// children through a reshape by their follows flags, which would stretch a
// previewed window to the size of the thing previewing it.
void ALCanvasView::reshape(S32 width, S32 height, bool called_from_parent)
{
    if (width == getRect().getWidth() && height == getRect().getHeight())
    {
        return;
    }
    LLRect r(getRect());
    r.mRight = r.mLeft + width;
    r.mTop = r.mBottom + height;
    setRect(r);
    if (!called_from_parent && getParent())
    {
        getParent()->reshape(getParent()->getRect().getWidth(),
                             getParent()->getRect().getHeight(), false);
    }
    updateBoundingRect();
    dirtyRect();
    anchorRoot();
}

void ALCanvasView::draw()
{
    refresh();
    // The backdrop is drawn before the zoom is pushed: it is what is
    // behind the window, not part of the window.
    drawBackdrop(getLocalRect());
    const bool zoomed = mZoom != 1.f;
    if (zoomed)
    {
        LLRender2D::pushMatrix();
        LLRender2D::scale(mZoom, mZoom);
    }
    drawContent();
    if (zoomed)
    {
        LLRender2D::popMatrix();
    }
    if (mPixelGrid && mZoom >= PIXEL_GRID_ZOOM)
    {
        drawPixelGrid();
    }
    drawChrome();
}

// One batch of lines, at every multiple of the zoom across what can be
// seen: a line per pixel of the surface, in drawn pixels.
void ALCanvasView::drawPixelGrid() const
{
    const LLRect seen = viewportRect();
    const S32 first_x = (S32)std::floor((F32)seen.mLeft / mZoom);
    const S32 last_x = (S32)std::ceil((F32)seen.mRight / mZoom);
    const S32 first_y = (S32)std::floor((F32)seen.mBottom / mZoom);
    const S32 last_y = (S32)std::ceil((F32)seen.mTop / mZoom);

    gGL.getTextureSlot(0)->unbind();
    gGL.color4f(0.5f, 0.5f, 0.5f, 0.35f);
    gGL.begin(LLRender::LINES);
    for (S32 x = first_x; x <= last_x; ++x)
    {
        const S32 at = ll_round((F32)x * mZoom);
        gGL.vertex2i(at, seen.mBottom);
        gGL.vertex2i(at, seen.mTop);
    }
    for (S32 y = first_y; y <= last_y; ++y)
    {
        const S32 at = ll_round((F32)y * mZoom);
        gGL.vertex2i(seen.mLeft, at);
        gGL.vertex2i(seen.mRight, at);
    }
    gGL.end();
}

LLRect ALCanvasView::viewportRect() const
{
    if (LLScrollContainer* scroller = getParentByType<LLScrollContainer>())
    {
        LLRect screen;
        scroller->localRectToScreen(scroller->getContentWindowRect(), &screen);
        LLRect local;
        screenRectToLocal(screen, &local);
        local.intersectWith(getLocalRect());
        if (local.getWidth() > 0 && local.getHeight() > 0)
        {
            return local;
        }
    }
    return getLocalRect();
}

LLRect ALCanvasView::localRectOf(const LLView* view) const
{
    LLRect local;
    screenRectToLocal(view->calcScreenRect(), &local);
    return local;
}

// ---------------------------------------------------------------------------
// The row
// ---------------------------------------------------------------------------

void ALCanvasRow::addCanvas(ALCanvasView* canvas)
{
    mCanvases.push_back(canvas);
    addChild(canvas);
}

void ALCanvasRow::show(ALCanvasView* canvas, bool visible)
{
    canvas->setVisible(visible);
    shareRoom();
}

void ALCanvasRow::shareRoom()
{
    S32 showing = 0;
    for (const ALCanvasView* canvas : mCanvases)
    {
        showing += canvas->getVisible() ? 1 : 0;
    }
    if (showing == 0)
    {
        return;
    }
    const LLRect room = regionRect();
    const S32 share = (room.getWidth() - GUTTER * (showing - 1)) / showing;
    for (ALCanvasView* canvas : mCanvases)
    {
        canvas->setLeastSurface(llmax(share, 0), room.getHeight());
    }
}

void ALCanvasRow::wanted(S32& width, S32& height) const
{
    width = 0;
    height = 0;
    for (const ALCanvasView* canvas : mCanvases)
    {
        if (canvas->getVisible())
        {
            width += (width > 0 ? GUTTER : 0) + canvas->getRect().getWidth();
            height = llmax(height, canvas->getRect().getHeight());
        }
    }
    width = llmax(width, mRoomWidth, 1);
    height = llmax(height, mRoomHeight, 1);
}

void ALCanvasRow::layout()
{
    // How much room there is, asked here as well as in the draw: a row laid
    // out before it has ever been drawn -- which is every row, once -- would
    // otherwise be as big as what is on it and no bigger.
    const LLRect room = regionRect();
    mRoomWidth = room.getWidth();
    mRoomHeight = room.getHeight();

    S32 width = 0;
    S32 height = 0;
    wanted(width, height);
    LLPanel::reshape(width, height, false);

    S32 left = 0;
    for (ALCanvasView* canvas : mCanvases)
    {
        if (canvas->getVisible())
        {
            canvas->setOrigin(left, height - canvas->getRect().getHeight());
            left += canvas->getRect().getWidth() + GUTTER;
        }
    }
}

void ALCanvasRow::settle()
{
    // The room is shared out again whenever there is a different amount of
    // it: the window resized, a region folded away, a scrollbar arriving or
    // going. The container is asked rather than told, because it answers
    // after its own scrollbars have been worked out.
    const LLRect room = regionRect();
    if (room.getWidth() != mRoomWidth || room.getHeight() != mRoomHeight)
    {
        mRoomWidth = room.getWidth();
        mRoomHeight = room.getHeight();
        shareRoom();
        layout();
    }
    else
    {
        // A surface that changed size on its own -- what is on it rebuilt,
        // or moved towards an edge -- is a document of a different size, and
        // what the container scrolls is this. Cheaper to ask every frame
        // than to be wrong about it for one.
        S32 width = 0;
        S32 height = 0;
        wanted(width, height);
        if (width != getRect().getWidth() || height != getRect().getHeight())
        {
            layout();
        }
    }
}

void ALCanvasRow::draw()
{
    settle();
    LLPanel::draw();
}

// The gutter between two surfaces and any slack around them are still the
// canvas region, so control and the wheel reaches a surface there as well.
// What it does is the surface's, so that there is one of it.
bool ALCanvasRow::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (LLPanel::handleScrollWheel(x, y, delta))
    {
        return true;
    }
    // Only that: a wheel over the slack around a surface is not a wheel over
    // anything on it, so nothing on it is scrolled by it.
    const MASK held = gKeyboard ? gKeyboard->currentMask(false) : MASK_NONE;
    if (held & MASK_CONTROL)
    {
        for (ALCanvasView* canvas : mCanvases)
        {
            if (canvas->getVisible())
            {
                return canvas->handleScrollWheel(0, 0, delta);
            }
        }
    }
    return false;
}

LLRect ALCanvasRow::regionRect()
{
    return roomOf(this);
}

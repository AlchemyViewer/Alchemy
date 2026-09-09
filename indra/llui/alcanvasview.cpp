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

#include "llkeyboard.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"

namespace
{
    // What a surface keeps around what is on it, so nothing is drawn hard
    // against the edge of the thing it is shown on.
    constexpr S32 SURFACE_MARGIN = 4;
    // The least a surface may be whatever it was told, since a surface of
    // no area is one a container has nothing to place.
    constexpr S32 LEAST_WIDTH = 120;
    constexpr S32 LEAST_HEIGHT = 40;
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

    resurface();
    anchorRoot();
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
// divided by the zoom to be compared with the rest.
S32 ALCanvasView::surfaceWidth() const
{
    return llmax(mContentWidth, mNeedWidth, ll_round((F32)mLeastWidth / mZoom));
}

S32 ALCanvasView::surfaceHeight() const
{
    return llmax(mContentHeight, mNeedHeight, ll_round((F32)mLeastHeight / mZoom));
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
    zoom = llclamp(zoom, 0.2f, 4.f);
    if (mZoom != zoom)
    {
        mZoom = zoom;
        resurface();
        // A surface whose size in its own coordinates did not change was not
        // reshaped, and then nothing has put the root back.
        anchorRoot();
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
    drawChrome();
}

LLRect ALCanvasView::viewportRect()
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

void ALCanvasRow::draw()
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
    if (LLScrollContainer* scroller = getParentByType<LLScrollContainer>())
    {
        return scroller->getContentWindowRect();
    }
    return getLocalRect();
}

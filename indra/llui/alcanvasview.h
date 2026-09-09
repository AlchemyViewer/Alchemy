/**
 * @file alcanvasview.h
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

#pragma once

#include "llpanel.h"

#include <vector>

// A surface something else is shown on: a view tree built from a file, a
// preview, a page. The surface is drawn at a zoom, sized to whatever is on
// it, and put in a scroll container, so what does not fit is scrolled to
// rather than cut off.
//
// **A surface has two sizes**, and at any zoom but a hundred per cent they
// are different numbers.
//
//  - the **surface rect** is what is on it in its own coordinates. Whatever
//    is shown here is built, hit and measured in those, and never learns
//    that it is being drawn at a zoom.
//  - the **view's rect** is what that comes to when it is drawn, which is
//    the surface times the zoom. This is what a row places and what a
//    container scrolls, so it is what decides whether there are scrollbars.
//
// Conflating the two is the mistake this class exists to stop anybody making
// again: it put previews against a height they were not laid out in, and
// sized surfaces to rooms measured in the wrong units.
//
// **The surface reaches to the far side of what is drawn on it.** Not to the
// rect the root claims -- a child placed past its parent's edge is drawn
// past it -- and not to where the root was built, since a root may be moved
// on its surface afterwards. What it cannot do is grow off its own top left
// corner, so a root moved that way stops at the corner.
//
// **What is on it does not change size when the surface does.** A view
// carries its children through a reshape by their follows flags, which would
// stretch a previewed window to the size of the thing previewing it, so this
// reshapes without them and writes the root back where it was put.
class ALCanvasView : public LLPanel
{
public:
    AL_VIEW_TYPE(ALCanvasView, LLPanel);

    // Constructed in code rather than declared in a file: what a surface
    // shows comes from somewhere no XUI file can name, so there is nothing
    // for a tag to say.
    explicit ALCanvasView(const LLPanel::Params& p) : LLPanel(p) {}

    // What is shown, and where on the surface it was put. Null takes the
    // surface back to what it is without anything on it.
    void setRoot(LLView* root);
    LLView* root() const { return mRoot; }

    // Everything on the surface goes, and the surface forgets what it held.
    void clear();

    // Where the root sits, and how far past itself it draws, read again.
    // Call it after moving the root: where a root sits on a surface is
    // remembered here and nowhere else, so without this the next change of
    // size puts it back where it was first placed.
    void rememberRoot();

    // The root put back where it was put, against the surface as it is now.
    void anchorRoot();

    // Where the last root was put, from the surface's top left corner, for
    // whoever builds the next one. A rebuild of what is already shown is not
    // a new thing to show: it goes back where it was left rather than to the
    // corner it was first built at, so that editing a field does not also
    // move the window the field is about. False until something has been put
    // on this surface at all.
    bool keptPlace(S32& left, S32& down) const;

    // What is on a surface can move itself: a previewed window dragged by
    // its own title bar does, and nothing tells the surface. So the surface
    // asks, every time it draws -- otherwise the next change of size puts
    // the root back where the surface last put it, and a window cannot be
    // dragged by the handle it is meant to be dragged by.
    void refresh();

    // A surface in a window of its own is as big as what it shows. A surface
    // that is a region of a window is as big as the region, and what it
    // shows sits at the top of it. Only a sizable one resizes itself.
    void setSizable(bool sizable) { mSizable = sizable; }

    // How big what is on it is, in its own coordinates, told by whoever put
    // it there: a root's own rect says nothing about the margin around it.
    void fitContent(S32 width, S32 height);
    S32 contentWidth() const { return mContentWidth; }
    S32 contentHeight() const { return mContentHeight; }

    // The least a surface may be whatever is on it, as it is drawn: its
    // share of the room it is shown in. A surface that stopped at the edge
    // of its content would have nowhere to draw a rule along, nowhere to
    // drag anything to and nothing to drop onto.
    void setLeastSurface(S32 width, S32 height);

    // What is on it, in its own coordinates.
    S32 surfaceWidth() const;
    S32 surfaceHeight() const;
    LLRect surfaceRect() const { return LLRect(0, surfaceHeight(), surfaceWidth(), 0); }

    // The view's rect worked out again from all of that.
    void resurface();

    // How much bigger than life it is drawn. What is on it keeps its own
    // numbers: what changes is the transform it is drawn through, the room
    // it needs, and what a pointer at a place means.
    F32 zoom() const { return mZoom; }
    void setZoom(F32 zoom);

    // The pointer in the coordinates what is on the surface is laid out in.
    // Everything that finds, measures or moves anything works in those, so
    // this is the one door the zoom is taken out at.
    void toContent(S32& x, S32& y) const;

    // What of the surface can be seen, in the surface's own drawn
    // coordinates. A surface is as big as what is on it and the container
    // scrolls it, so its top left is often somewhere off screen.
    LLRect viewportRect();

    // Where a view inside this one is, in the surface's coordinates.
    LLRect localRectOf(const LLView* view) const;

    // Nothing on a surface has an edge of the surface to line up with. A
    // surface reaches to a margin past what is on it, so the far edges sit
    // right against whatever was last dragged towards them -- inside the
    // distance a drag snaps from -- and lining a window up with one of those
    // moves the window, which moves the edge, which lines it up again. The
    // near edges are the same fight from the other side: what is pushed off
    // the top left corner is put back at it.
    LLRect getSnapRect() const override;

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    void draw() override;

protected:
    // What is drawn in the surface's own coordinates, through the zoom:
    // what is on it, and anything drawn over it that belongs to it.
    virtual void drawContent() { LLPanel::draw(); }

    // What is drawn at its own size whatever the zoom, because it is the
    // canvas's own furniture rather than a part of what is shown: a rule
    // along the edge of what can be seen, and its like.
    virtual void drawChrome() {}

    F32     mZoom = 1.f;

private:
    LLView* mRoot = nullptr;
    bool    mSizable = false;
    S32     mContentWidth = 0;
    S32     mContentHeight = 0;
    S32     mLeastWidth = 0;
    S32     mLeastHeight = 0;
    S32     mAnchorLeft = 0;        // where the root was put, from the
    S32     mAnchorTop = 0;         // surface's top left corner
    S32     mNeedWidth = 0;         // to the far side of what it draws,
    S32     mNeedHeight = 0;        // from that same corner
    bool    mPlaced = false;        // something has been put on it
};

// The surfaces of one document side by side, sharing the room they are shown
// in. Each is as big as what is on it; the row is as wide as all of them and
// as tall as its tallest, and never smaller than the region, so the whole
// region belongs to the canvas whatever happens to be built on it.
//
// Nothing here is placed from the bottom: surfaces are read across and their
// tops are what line up.
class ALCanvasRow : public LLPanel
{
public:
    AL_VIEW_TYPE(ALCanvasRow, LLPanel);

    static constexpr S32 GUTTER = 12;

    explicit ALCanvasRow(const LLPanel::Params& p) : LLPanel(p) {}

    void addCanvas(ALCanvasView* canvas);

    // A surface comes and goes with what is on it, and the room is shared
    // out again when it does. Told before a build rather than after,
    // because what is built is placed against the surface it lands on.
    void show(ALCanvasView* canvas, bool visible);

    // The room, divided between the surfaces that are showing.
    void shareRoom();

    // The row sized and its surfaces placed. Runs after a build, since a
    // surface sizes itself to what was built on it.
    void layout();

    // What the row has to be, which is not always what it is.
    void wanted(S32& width, S32& height) const;

    // The row brought into line with the room and with what is on it. Runs
    // before every frame is drawn, because everything that changes either of
    // those -- a region folded away, a window dragged, a file built -- does
    // it between frames and none of them tells the row.
    void settle();

    void draw() override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;

private:
    // The room the row is shown in, which is the container's window rather
    // than the row's own rect: the row is the thing being sized here, so its
    // rect is the answer from last time.
    LLRect regionRect();

    std::vector<ALCanvasView*> mCanvases;
    S32 mRoomWidth = 0;
    S32 mRoomHeight = 0;
};

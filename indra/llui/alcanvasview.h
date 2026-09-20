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

#include "llfontgl.h"
#include "llpanel.h"

#include <string>
#include <string_view>
#include <vector>

#include <boost/signals2.hpp>

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
    void clear() override;

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

    // The keyboard given to a surface stays on the surface. A panel handed
    // it passes it on to the first thing in it that will take it, and here
    // that is a widget in the picture, which would then keep the arrows
    // meant to move it. What is on the surface can still be given the
    // keyboard by name, or by a plain click on it, which is the picture
    // working.
    void setFocus(bool b) override { LLUICtrl::setFocus(b); }

    // What is on a surface can move itself: a previewed window dragged by
    // its own title bar does, and nothing tells the surface. So the surface
    // asks, every time it draws -- otherwise the next change of size puts
    // the root back where the surface last put it, and a window cannot be
    // dragged by the handle it is meant to be dragged by.
    void refresh() override;

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
    static constexpr F32 MIN_ZOOM = 0.2f;
    static constexpr F32 MAX_ZOOM = 8.f;
    F32 zoom() const { return mZoom; }
    void setZoom(F32 zoom);
    // The next of the steps the wheel goes through, up or down from the
    // zoom now: a ladder that doubles and halves rather than a fixed
    // amount, so a small thing and a large one both reach a useful size
    // in a few turns.
    F32 steppedZoom(S32 direction) const { return steppedZoom(mZoom, direction); }
    static F32 steppedZoom(F32 from, S32 direction);
    // The largest step of that ladder at which what is on the surface fits
    // the room it is shown in.
    F32 fittingZoom() const;
    // The zoom changed with a point of the surface held still: what was
    // under the pointer is under it afterwards, which is what a wheel over
    // a canvas means by zooming. The point is in the surface's drawn
    // coordinates; the container is scrolled to keep it where it was.
    void zoomAbout(F32 zoom, S32 x, S32 y);
    // The zoom the wheel came to, for whatever else shows the zoom.
    typedef boost::signals2::signal<void(F32)> zoom_signal_t;
    boost::signals2::connection onZoomChange(const zoom_signal_t::slot_type& cb) { return mZoomChange.connect(cb); }

    // The surface moved under the pointer: the middle button drags it,
    // and so does the left button with the space bar held, in every
    // canvas; a surface with nothing on it to click drags by the left
    // button alone when it says so. What moves is the container's scroll,
    // by however far the pointer went in drawn pixels.
    void setDragPans(bool pans) { mDragPans = pans; }
    bool dragPans() const { return mDragPans; }
    bool panning() const { return mPanning; }
    // Whether a press now is the start of a pan: the space bar is held,
    // or the surface drags by itself.
    bool panGesture() const;
    void beginPan(S32 x, S32 y);
    void panTo(S32 x, S32 y);
    void endPan();
    // The container scrolled by this much, in drawn pixels: positive
    // moves what is seen right and up, as a drag that way would.
    void panBy(S32 dx, S32 dy);

    // The rules along the top and left of what can be seen, numbered in
    // the surface's own coordinates from the corner of what is on it,
    // with a mark at every step and a number every fifty: a caller with a
    // grid steps the rules by it. Drawn by the default chrome when asked
    // for; a subclass says whether they show and what they measure.
    void setRulers(bool shown) { mRulers = shown; }
    virtual bool rulersShown() const { return mRulers; }
    void setRulerStep(S32 step) { mRulerStep = llmax(1, step); }
    virtual S32 rulerStep() const { return mRulerStep; }

    // The room the surface is shown in, in drawn pixels: the window of the
    // container that scrolls it, or failing that the parent.
    LLRect roomRect() const;

    // What is said in the middle while there is nothing on the surface,
    // in the canvas's own words and size.
    void setHint(std::string hint) { mHint = std::move(hint); }
    // Whether there is anything on the surface to draw. A surface shows a
    // view tree unless a subclass shows something else.
    virtual bool empty() const { return !mRoot; }
    // The words in the corner of what can be seen while there is
    // something on the surface: how much bigger than life it is drawn.
    virtual std::string legend() const;

    // What is drawn under everything on the surface, over the whole of
    // it and at the canvas's own scale whatever the zoom -- it stands for
    // what is behind a window, and that does not grow when the window is
    // looked at closely: nothing, so the panel's own colour shows; a
    // pattern of colour bands, discs and text that shows what a
    // translucent window does to what is behind it; a checkerboard that
    // shows edges and
    // transparency; the colour of a window; or a light grey.
    enum class Backdrop : U8
    {
        None,
        Pattern,
        Checker,
        Window,
        Light
    };

    void setBackdrop(Backdrop backdrop) { mBackdrop = backdrop; }
    Backdrop backdrop() const { return mBackdrop; }
    // A backdrop by the name a menu or a saved state uses for it -- none,
    // pattern, checker, window, light -- and the name back; an unknown
    // name is none.
    static Backdrop backdropNamed(std::string_view name);
    static const char* backdropName(Backdrop backdrop);

    // The ink for words drawn over the backdrop, so they can be read on
    // it: dark on a light one, light on a dark one, and with a shadow
    // where the backdrop is busy. Quiet is for words that are said
    // quietly -- a legend, a caption -- and is nearer the backdrop.
    LLColor4 inkColor(bool quiet = false) const;
    LLFontGL::ShadowType inkShadow() const;

    // A faint line along every pixel of what is on the surface, drawn
    // over it once the zoom is enough for the pixels to be seen apart.
    static constexpr F32 PIXEL_GRID_ZOOM = 4.f;
    void setPixelGrid(bool shown) { mPixelGrid = shown; }
    bool pixelGrid() const { return mPixelGrid; }

    // The pointer in the coordinates what is on the surface is laid out in.
    // Everything that finds, measures or moves anything works in those, so
    // this is the one door the zoom is taken out at.
    void toContent(S32& x, S32& y) const;

    // What of the surface can be seen, in the surface's own drawn
    // coordinates. A surface is as big as what is on it and the container
    // scrolls it, so its top left is often somewhere off screen.
    LLRect viewportRect() const;

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
    // Control and the wheel steps the zoom about the pointer; the wheel
    // alone belongs to whatever is under it, and to the container when
    // nothing wants it.
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    // A pan by the middle button, or by the left one where the gesture
    // says so; a subclass with a pointer of its own answers first and
    // comes here for the rest, with the drawn point: everything else the
    // pointer does is converted here, once, for whatever is on the
    // surface.
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleMiddleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMiddleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleRightMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    void onMouseCaptureLost() override;

protected:
    // What is drawn in the surface's own coordinates, through the zoom:
    // what is on it, and anything drawn over it that belongs to it.
    virtual void drawContent() { LLPanel::draw(); }

    // What is drawn at its own size whatever the zoom, because it is the
    // canvas's own furniture rather than a part of what is shown: a rule
    // along the edge of what can be seen, and its like. Unless a subclass
    // has furniture of its own, it is the hint while the surface is empty
    // and the legend in the corner while it is not.
    virtual void drawChrome();

    // The chosen backdrop over an area, in drawn pixels.
    void drawBackdrop(const LLRect& area) const;
    void drawPixelGrid() const;
    // The rules, when they show: along the top and left of what can be
    // seen, at their own size whatever the zoom.
    void drawRulers() const;
    // What the rules measure from: the rect of what is on the surface, in
    // the surface's own coordinates. The root's, or the surface itself.
    virtual LLRect rulerOrigin() const;
    // A rect to mark on both rules, in the surface's own coordinates: the
    // thing chosen. False for none.
    virtual bool rulerHighlight(LLRect& content) const { return false; }

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
    std::string mHint;
    Backdrop mBackdrop = Backdrop::None;
    bool mPixelGrid = false;
    bool mRulers = false;
    S32 mRulerStep = 4;
    zoom_signal_t mZoomChange;
    // A pan under way: where the pointer was on the screen when it began,
    // and where the container was scrolled to.
    bool mDragPans = false;
    bool mPanning = false;
    S32 mPanScreenX = 0;
    S32 mPanScreenY = 0;
    S32 mPanDocX = 0;
    S32 mPanDocY = 0;
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

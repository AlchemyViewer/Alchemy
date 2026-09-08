/**
 * @file alfloaterxuistudio.cpp
 * @brief The XUI Studio: catalog, preview, hierarchy, inspectors and diagnostics for XUI files.
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

#include "alfloaterxuistudio.h"

#include "alxmldocument.h"
#include "alxmllayermerge.h"
#include "alcolorfield.h"
#include "alpropertygrid.h"
#include "alxuischema.h"
#include "alxuishellbuild.h"
#include "alxuitranslate.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llexternaleditor.h"
#include "llfile.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfocusmgr.h"
#include "llimagebmp.h"
#include "llimagej2c.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llfolderview.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "lllivefile.h"
#include "llmenugl.h"
#include "llnotifications.h"
#include "llnotificationtemplate.h"
#include "llsdparam.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltimer.h"
#include "lltoastalertpanel.h"
#include "lltoastnotifypanel.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerwindow.h"
#include "llwindow.h"

#include <boost/unordered_set.hpp>

#include <algorithm>
#include <cctype>
#include <set>

// ===========================================================================
// The pieces the floater builds on
// ===========================================================================

// The skin and language the directory object answers with, switched for a
// build and switched back after it. The factory's cached defaults are keyed
// by parameter block type, so they are dropped on both sides of the switch.
class ALXUISkinScope
{
public:
    ALXUISkinScope(const std::string& skin, const std::string& language)
    :   mSkin(gDirUtilp->getSkinFolder()),
        mLanguage(gDirUtilp->getLanguage()),
        mSwitched(skin != mSkin || language != mLanguage)
    {
        if (mSwitched)
        {
            gDirUtilp->setSkinFolder(skin, language);
            LLUICtrlFactory::instance().flushDefaults();
        }
    }

    ~ALXUISkinScope()
    {
        if (mSwitched)
        {
            gDirUtilp->setSkinFolder(mSkin, mLanguage);
            LLUICtrlFactory::instance().flushDefaults();
        }
    }

    ALXUISkinScope(const ALXUISkinScope&) = delete;
    ALXUISkinScope& operator=(const ALXUISkinScope&) = delete;

private:
    std::string mSkin;
    std::string mLanguage;
    bool        mSwitched;
};

// One file of the primary preview, watched for a change on disk. The
// first check counts as reading it; only a change after that reloads.
class ALXUILiveFile final : public LLLiveFile
{
public:
    ALXUILiveFile(const std::string& path, ALFloaterXUIStudio* tool)
    :   LLLiveFile(path, 1.f),
        mTool(tool)
    {
    }

protected:
    bool loadFile() override
    {
        if (!mPrimed)
        {
            mPrimed = true;
            return true;
        }
        mTool->fileChanged();
        return true;
    }

private:
    ALFloaterXUIStudio*   mTool;
    bool                mPrimed = false;
};

// A preview: a floater in the floater view that is the previewed floater,
// or hosts the previewed panel, menu or widget. It draws the tool's hover
// and selection over what it shows, answers a modifier click with a
// selection, and tells the tool when it goes.
class ALXUIPreviewHost final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALXUIPreviewHost, LLFloater);

    ALXUIPreviewHost(ALFloaterXUIStudio* tool, S32 which, const LLFloater::Params& p)
    :   LLFloater(LLSD(), p),
        mTool(tool),
        mWhich(which)
    {
    }

    ~ALXUIPreviewHost() override
    {
        if (mTool)
        {
            mTool->hostClosed(mWhich);
        }
    }

    void detach() { mTool = nullptr; }
    void setRoot(LLView* root) { mRoot = root; }

    void draw() override
    {
        LLFloater::draw();
        if (!mTool || !mRoot)
        {
            return;
        }
        // Only while a handle is held: the grid answers "where will this
        // land", which is a question nobody is asking the rest of the time,
        // and a preview under a permanent mesh is a preview of the mesh.
        if (mTool->snapToGrid() && grabbed())
        {
            drawGrid();
        }
        if (mTool->showRulers())
        {
            drawRulers();
        }
        const ALXUISelection& selection = mTool->selection();
        if (selection.hasHover() && mTool->hoverHighlight())
        {
            static const LLUIColor hover_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
            if (LLView* view = ALXUISelection::resolve(mRoot, selection.hover()))
            {
                drawBox(view, hover_color.get(), true);
            }
        }
        if (selection.hasSelection())
        {
            if (LLView* view = ALXUISelection::resolve(mRoot, selection.selection()))
            {
                drawBox(view, LLColor4::red, true);
                if (gKeyboard && (gKeyboard->currentMask(false) & MASK_ALT))
                {
                    drawGuides(view);
                }
                if (editable(view))
                {
                    const LLRect r = localRectOf(view);
                    drawGrips(r);
                    if (dragging())
                    {
                        LLRect dragged(r);
                        dragged.mLeft += mDelta[EDGE_L];
                        dragged.mBottom += mDelta[EDGE_B];
                        dragged.mRight += mDelta[EDGE_R];
                        dragged.mTop += mDelta[EDGE_T];
                        gl_rect_2d(dragged, LLColor4::white, false);
                    }
                }
            }
        }
    }

    bool handleKeyHere(KEY key, MASK mask) override
    {
        if (mTool && mWhich == ALFloaterXUIStudio::PRIMARY)
        {
            if (key == 'Z' && mask == MASK_CONTROL && mTool->undoEdit())
            {
                return true;
            }
            if (mTool->nudge(key, mask))
            {
                return true;
            }
        }
        return LLFloater::handleKeyHere(key, mask);
    }

    // A plain click belongs to the preview: its tabs turn and its lists
    // scroll, which is half of what a preview is for. Control selects the
    // element under the pointer, and held through a drag it moves what it
    // selected.
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (!mTool || !mRoot)
        {
            return LLFloater::handleMouseDown(x, y, mask);
        }

        // A handle answers before the widget under it does, with or
        // without the modifier: a button in a preview is a picture of a
        // button, and the handle on its corner is a handle.
        LLView* selected = ALXUISelection::resolve(mRoot, mTool->selection().selection());
        if (editable(selected))
        {
            const S32 grip = gripAt(x, y, localRectOf(selected), mask);
            if (grip != GRIP_NONE)
            {
                return beginDrag(grip, x, y);
            }
        }
        if (mask & MASK_CONTROL)
        {
            LLView* view = hitTest(x, y);
            mTool->canvasSelect(mWhich, view);
            if (editable(view))
            {
                return beginDrag(GRIP_MOVE, x, y);
            }
            setFocus(true);
            return true;
        }
        return LLFloater::handleMouseDown(x, y, mask);
    }

    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (grabbed())
        {
            track(x, y);
            setGripCursor(mGrip);
            return true;
        }
        if (mTool && mRoot)
        {
            mTool->canvasHover(mWhich, hitTest(x, y));
            LLView* selected = ALXUISelection::resolve(mRoot, mTool->selection().selection());
            if (editable(selected))
            {
                const S32 grip = gripAt(x, y, localRectOf(selected), mask);
                if (grip != GRIP_NONE)
                {
                    setGripCursor(grip);
                    return true;
                }
            }
        }
        return LLFloater::handleHover(x, y, mask);
    }

    bool handleMouseUp(S32 x, S32 y, MASK mask) override
    {
        if (grabbed())
        {
            track(x, y);
            const S32 grip = mGrip;
            mGrip = GRIP_NONE;
            gFocusMgr.setMouseCapture(nullptr);
            if (mTool && grip != GRIP_NONE)
            {
                // One operation for the whole drag, written when the
                // button comes up rather than on every pixel of it.
                mTool->canvasDrag(mWhich, mDelta[EDGE_L], mDelta[EDGE_B], mDelta[EDGE_R], mDelta[EDGE_T]);
            }
            return true;
        }
        return LLFloater::handleMouseUp(x, y, mask);
    }

    void onMouseCaptureLost() override
    {
        mGrip = GRIP_NONE;
        LLFloater::onMouseCaptureLost();
    }

    void onMouseLeave(S32 x, S32 y, MASK mask) override
    {
        LLFloater::onMouseLeave(x, y, mask);
        if (mTool)
        {
            mTool->canvasHover(mWhich, nullptr);
        }
    }

private:
    // The four edges, the eight grips that move them, and the ninth in the
    // middle that moves all four at once.
    enum Edge : S32 { EDGE_L, EDGE_B, EDGE_R, EDGE_T, EDGE_COUNT };
    static constexpr S32 GRIP_NONE = -1;
    static constexpr S32 GRIP_MOVE = -2;

    static constexpr S32 GRIP_SIZE = 7;
    static constexpr S32 MOVE_GRIP_SIZE = 13;
    static constexpr S32 DEAD_ZONE = 3;      // a click is not a drag

    // The root of a floater preview is the preview window: its corners
    // are the window's own, and dragging its bar is how the window is
    // moved out of the way. The second preview is another language of the
    // same file, shown beside the first and not written to.
    bool editable(const LLView* view) const
    {
        return view && view != this && mWhich == ALFloaterXUIStudio::PRIMARY;
    }

    bool dragging() const
    {
        return mDelta[EDGE_L] || mDelta[EDGE_B] || mDelta[EDGE_R] || mDelta[EDGE_T];
    }

    // A handle is held: the button went down on one and has not come up.
    // True from the grab rather than from the first pixel of movement,
    // because what the grid is for is saying where a move will land.
    bool grabbed()
    {
        return mGrip != GRIP_NONE && hasMouseCapture();
    }

    bool beginDrag(S32 grip, S32 x, S32 y)
    {
        mGrip = grip;
        mDragX = x;
        mDragY = y;
        for (S32& d : mDelta)
        {
            d = 0;
        }
        gFocusMgr.setMouseCapture(this);
        setFocus(true);
        return true;
    }

    // The eight squares, as rects in this floater's space.
    static void gripRects(const LLRect& r, LLRect (&out)[8])
    {
        const S32 h = GRIP_SIZE / 2;
        const S32 mid_x = (r.mLeft + r.mRight) / 2;
        const S32 mid_y = (r.mBottom + r.mTop) / 2;
        const S32 xs[8] = { r.mLeft, mid_x, r.mRight, r.mLeft, r.mRight, r.mLeft, mid_x, r.mRight };
        const S32 ys[8] = { r.mBottom, r.mBottom, r.mBottom, mid_y, mid_y, r.mTop, r.mTop, r.mTop };
        for (S32 i = 0; i < 8; ++i)
        {
            out[i] = LLRect(xs[i] - h, ys[i] + h, xs[i] + h, ys[i] - h);
        }
    }

    // The middle of the part of the element that is inside the window,
    // since an element that hangs over the edge is drawn there but the
    // mouse never reaches past it, and that element is the one most in
    // need of being dragged back.
    LLRect moveGripRect(const LLRect& r) const
    {
        LLRect visible(r);
        visible.intersectWith(getLocalRect());
        if (visible.isEmpty())
        {
            visible = r;
        }
        const S32 h = MOVE_GRIP_SIZE / 2;
        const S32 mid_x = (visible.mLeft + visible.mRight) / 2;
        const S32 mid_y = (visible.mBottom + visible.mTop) / 2;
        return LLRect(mid_x - h, mid_y + h, mid_x + h, mid_y - h);
    }

    void setGripCursor(S32 grip) const
    {
        static const ECursorType cursors[8] = {
            UI_CURSOR_SIZENESW, UI_CURSOR_SIZENS, UI_CURSOR_SIZENWSE,
            UI_CURSOR_SIZEWE,                     UI_CURSOR_SIZEWE,
            UI_CURSOR_SIZENWSE, UI_CURSOR_SIZENS, UI_CURSOR_SIZENESW };
        getWindow()->setCursor(grip >= 0 && grip < 8 ? cursors[grip] : UI_CURSOR_SIZEALL);
    }

    // Which edges each of the eight moves, in the order gripRects builds
    // them: the corners move two.
    static void gripEdges(S32 index, bool (&edges)[EDGE_COUNT])
    {
        static const bool table[8][EDGE_COUNT] = {
            { true,  true,  false, false },     // bottom left
            { false, true,  false, false },     // bottom
            { false, true,  true,  false },     // bottom right
            { true,  false, false, false },     // left
            { false, false, true,  false },     // right
            { true,  false, false, true  },     // top left
            { false, false, false, true  },     // top
            { false, false, true,  true  },     // top right
        };
        for (S32 i = 0; i < EDGE_COUNT; ++i)
        {
            edges[i] = table[index][i];
        }
    }

    void drawGrips(const LLRect& r) const
    {
        LLRect grips[8];
        gripRects(r, grips);
        for (const LLRect& grip : grips)
        {
            gl_rect_2d(grip, LLColor4::white, true);
            gl_rect_2d(grip, LLColor4::black, false);
        }

        // The one in the middle moves the element, and says so with the
        // four arrows a move cursor has.
        const LLRect move = moveGripRect(r);
        gl_rect_2d(move, LLColor4::white, true);
        gl_rect_2d(move, LLColor4::black, false);
        const S32 mid_x = (move.mLeft + move.mRight) / 2;
        const S32 mid_y = (move.mBottom + move.mTop) / 2;
        gl_line_2d(move.mLeft + 2, mid_y, move.mRight - 2, mid_y, LLColor4::black);
        gl_line_2d(mid_x, move.mBottom + 2, mid_x, move.mTop - 2, LLColor4::black);
    }

    S32 gripAt(S32 x, S32 y, const LLRect& r, MASK mask) const
    {
        LLRect grips[8];
        gripRects(r, grips);
        for (S32 i = 0; i < 8; ++i)
        {
            if (grips[i].pointInRect(x, y))
            {
                return i;
            }
        }
        if (moveGripRect(r).pointInRect(x, y))
        {
            return GRIP_MOVE;
        }
        return (mask & MASK_ALT) && r.pointInRect(x, y) ? GRIP_MOVE : GRIP_NONE;
    }

    void track(S32 x, S32 y)
    {
        S32 dx = x - mDragX;
        S32 dy = y - mDragY;
        if (!dragging() && llabs(dx) < DEAD_ZONE && llabs(dy) < DEAD_ZONE)
        {
            // The hand moves a little on the way down; a click that
            // selects an element is not a move of it.
            dx = 0;
            dy = 0;
        }
        bool edges[EDGE_COUNT] = { true, true, true, true };
        if (mGrip != GRIP_MOVE)
        {
            gripEdges(mGrip, edges);
        }

        // The grid is the element's own coordinates -- the ones the file
        // writes -- so an edge lands where a number in the file lands,
        // not where a pixel of this window happens to be.
        if (mTool && mTool->snapToGrid())
        {
            LLView* view = ALXUISelection::resolve(mRoot, mTool->selection().selection());
            const LLView* parent = view ? view->getParent() : nullptr;
            if (view && parent)
            {
                const S32 grid = mTool->gridSize();
                const LLRect& r = view->getRect();
                const S32 top = parent->getRect().getHeight() - r.mTop;
                if (mGrip == GRIP_MOVE)
                {
                    // A move keeps its size: the edges the file counts
                    // from decide, and the others follow.
                    dx = snapped(r.mLeft + dx, grid) - r.mLeft;
                    dy = -(snapped(top - dy, grid) - top);
                }
                else
                {
                    // A resize lands each edge it moves on the grid.
                    if (edges[EDGE_L]) { dx = snapped(r.mLeft + dx, grid) - r.mLeft; }
                    else if (edges[EDGE_R]) { dx = snapped(r.mRight + dx, grid) - r.mRight; }
                    if (edges[EDGE_T]) { dy = -(snapped(top - dy, grid) - top); }
                    else if (edges[EDGE_B]) { dy = snapped(r.mBottom + dy, grid) - r.mBottom; }
                }
            }
        }

        mDelta[EDGE_L] = edges[EDGE_L] ? dx : 0;
        mDelta[EDGE_R] = edges[EDGE_R] ? dx : 0;
        mDelta[EDGE_B] = edges[EDGE_B] ? dy : 0;
        mDelta[EDGE_T] = edges[EDGE_T] ? dy : 0;
    }

    static S32 snapped(S32 value, S32 grid)
    {
        return grid > 1 ? ((value + (value >= 0 ? grid / 2 : -grid / 2)) / grid) * grid : value;
    }

    // The topmost view drawn over a point, taking the deepest before its
    // parent. Drawing is not clipped to a parent's rect: a widget
    // positioned outside its panel is drawn outside it, and that is
    // exactly the widget someone opens this tool to drag back, so the
    // search is over what is on screen rather than over what contains
    // what.
    static LLView* pickDrawn(LLView* view, S32 screen_x, S32 screen_y)
    {
        if (!view->getVisible())
        {
            return nullptr;
        }
        for (LLView* child : *view->getChildList())
        {
            if (LLView* hit = pickDrawn(child, screen_x, screen_y))
            {
                return hit;
            }
        }
        return view->calcScreenRect().pointInRect(screen_x, screen_y) ? view : nullptr;
    }

    // What was hit, and then the nearest view above it that the file
    // describes, since a widget's own children are not what an author is
    // pointing at.
    LLView* hitTest(S32 x, S32 y)
    {
        LLRect screen;
        localRectToScreen(LLRect(x, y, x, y), &screen);
        LLView* view = mRoot ? pickDrawn(mRoot, screen.mLeft, screen.mBottom) : nullptr;
        const ALXUISourceMap& map = mTool->sourceMap(mWhich);
        while (view && view != mRoot && !map.isFromXML(view))
        {
            view = view->getParent();
        }
        return view;
    }

    // The lines a drag lands on, drawn where the file's own numbers put
    // them: from the previewed root's corner, since that is where its
    // children are measured from.
    void drawGrid() const
    {
        const S32 grid = mTool->gridSize();
        const LLRect r = mRoot == this ? getLocalRect() : localRectOf(mRoot);
        if (grid < 2 || r.getWidth() <= 0)
        {
            return;
        }
        static const LLUIColor grid_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
        LLColor4 faint(grid_color.get());
        faint.mV[VALPHA] = 0.12f;
        // A two pixel grid drawn whole is a wash, so a fine grid is drawn
        // every few of itself: the lines a drag lands on are still the grid.
        const S32 step = grid >= 4 ? grid : grid * ((8 + grid - 1) / grid);
        for (S32 x = r.mLeft; x <= r.mRight; x += step)
        {
            gl_line_2d(x, r.mBottom, x, r.mTop, faint);
        }
        for (S32 y = r.mTop; y >= r.mBottom; y -= step)
        {
            gl_line_2d(r.mLeft, y, r.mRight, y, faint);
        }
    }

    // Two strips beyond the top and the left edges, counting from the
    // previewed root's top left corner, which is where the file counts from.
    // Outside it, so a rule never sits over the thing being measured: a view
    // draws through its parent's translation and nothing clips it to its own
    // rect, so the strips are simply at negative coordinates.
    //
    // Ticks are the grid, which is whatever the grid is; the numbers are
    // every fifty, which is round whether or not fifty is a multiple of the
    // grid. The selection's edges are marked on both.
    void drawRulers() const
    {
        static constexpr S32 RULER = 14;
        static constexpr S32 LABEL_EVERY = 50;
        const LLRect r = mRoot == this ? getLocalRect() : localRectOf(mRoot);
        if (r.getWidth() <= 0)
        {
            return;
        }
        static const LLUIColor back = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::black);
        static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
        LLColor4 ground(back.get());
        ground.mV[VALPHA] = 0.85f;

        const LLRect top(r.mLeft - RULER, r.mTop + RULER, r.mRight, r.mTop);
        const LLRect left(r.mLeft - RULER, r.mTop, r.mLeft, r.mBottom);
        gl_rect_2d(top, ground, true);
        gl_rect_2d(left, ground, true);
        gl_rect_2d(top, ink.get(), false);
        gl_rect_2d(left, ink.get(), false);

        const S32 grid = llmax(mTool->gridSize(), 2);
        const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
        for (S32 x = 0; x <= r.getWidth(); x += grid)
        {
            gl_line_2d(r.mLeft + x, r.mTop, r.mLeft + x, r.mTop + 3, ink.get());
        }
        for (S32 y = 0; y <= r.getHeight(); y += grid)
        {
            gl_line_2d(r.mLeft, r.mTop - y, r.mLeft - 3, r.mTop - y, ink.get());
        }
        for (S32 x = 0; x <= r.getWidth(); x += LABEL_EVERY)
        {
            gl_line_2d(r.mLeft + x, r.mTop, r.mLeft + x, r.mTop + 5, ink.get());
            if (x > 0)
            {
                font->renderUTF8(std::to_string(x), 0, r.mLeft + x + 2, r.mTop + 5,
                                 ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
            }
        }
        for (S32 y = 0; y <= r.getHeight(); y += LABEL_EVERY)
        {
            gl_line_2d(r.mLeft, r.mTop - y, r.mLeft - 5, r.mTop - y, ink.get());
            if (y > 0)
            {
                font->renderUTF8(std::to_string(y), 0, r.mLeft - RULER + 2, r.mTop - y - 10,
                                 ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
            }
        }

        // Where the selection sits, on both rules.
        if (LLView* view = ALXUISelection::resolve(mRoot, mTool->selection().selection()))
        {
            const LLRect box = localRectOf(view);
            gl_rect_2d(LLRect(box.mLeft, r.mTop + RULER, box.mRight, r.mTop), LLColor4::red, false);
            gl_rect_2d(LLRect(r.mLeft - RULER, box.mTop, r.mLeft, box.mBottom), LLColor4::red, false);
        }
    }

    LLRect localRectOf(const LLView* view) const
    {
        LLRect local;
        screenRectToLocal(view->calcScreenRect(), &local);
        return local;
    }

    void drawBox(const LLView* view, const LLColor4& color, bool label)
    {
        const LLRect r = localRectOf(view);
        gl_rect_2d(r, color, false);
        LLRect outer(r);
        outer.stretch(1);
        LLColor4 faint(color);
        faint.mV[VALPHA] = 0.5f;
        gl_rect_2d(outer, faint, false);
        if (label)
        {
            const std::string text = std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight());
            LLFontGL::getFontSansSerifSmall()->renderUTF8(text, 0, (F32)r.mLeft, (F32)r.mTop + 2.f, color,
                                                          LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL,
                                                          LLFontGL::DROP_SHADOW);
        }
    }

    void drawDistance(S32 x1, S32 y1, S32 x2, S32 y2, S32 value, const LLColor4& color)
    {
        if (value <= 0)
        {
            return;
        }
        gl_line_2d(x1, y1, x2, y2, color);
        const std::string text = std::to_string(value);
        LLFontGL::getFontSansSerifSmall()->renderUTF8(text, 0, (F32)((x1 + x2) / 2), (F32)((y1 + y2) / 2), color,
                                                      LLFontGL::HCENTER, LLFontGL::VCENTER, LLFontGL::NORMAL,
                                                      LLFontGL::DROP_SHADOW);
    }

    // The numbers left, top, right and bottom mean, drawn from the view to
    // its parent's edges, and left_pad and top_pad from the sibling created
    // before it.
    void drawGuides(const LLView* view)
    {
        const LLView* parent = view->getParent();
        if (!parent)
        {
            return;
        }
        static const LLUIColor guide_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
        const LLColor4 color = guide_color.get();
        const LLRect r = localRectOf(view);
        const LLRect p = localRectOf(parent);
        const S32 mid_y = (r.mTop + r.mBottom) / 2;
        const S32 mid_x = (r.mLeft + r.mRight) / 2;
        drawDistance(p.mLeft, mid_y, r.mLeft, mid_y, r.mLeft - p.mLeft, color);
        drawDistance(r.mRight, mid_y, p.mRight, mid_y, p.mRight - r.mRight, color);
        drawDistance(mid_x, r.mTop, mid_x, p.mTop, p.mTop - r.mTop, color);
        drawDistance(mid_x, p.mBottom, mid_x, r.mBottom, r.mBottom - p.mBottom, color);

        const LLView::child_list_t& siblings = *parent->getChildList();
        auto it = std::find(siblings.begin(), siblings.end(), view);
        if (it != siblings.end() && std::next(it) != siblings.end())
        {
            const LLRect s = localRectOf(*std::next(it));
            LLColor4 sibling_color = LLColor4::cyan;
            drawDistance(s.mRight, mid_y, r.mLeft, mid_y, r.mLeft - s.mRight, sibling_color);
            drawDistance(mid_x, r.mTop, mid_x, s.mBottom, s.mBottom - r.mTop, sibling_color);
        }
    }

    ALFloaterXUIStudio*   mTool;
    LLView*             mRoot = nullptr;
    S32                 mWhich;

    S32                 mGrip = GRIP_NONE;      // the handle the button went down on
    S32                 mDragX = 0;
    S32                 mDragY = 0;
    S32                 mDelta[EDGE_COUNT] = { 0, 0, 0, 0 };
};

namespace
{
    constexpr S32 MAX_FIND_ROWS = 500;

    std::string firstToken(const std::string& path)
    {
        const size_t dot = path.find('.');
        return dot == std::string::npos ? path : path.substr(0, dot);
    }

    // A child widget's attributes fail against the parent's block by
    // design and are parsed again by the child; those are not findings.
    bool isNoise(const ALXUIDiagnostics::Entry& e)
    {
        return e.kind == ALXUIDiagnostics::Kind::UnknownAttribute && e.depth > 0
            && ALXUICatalog::isWidgetTag(firstToken(e.path));
    }

    std::string followsText(U32 follows)
    {
        std::string text;
        auto add = [&](U32 flag, const char* name)
        {
            if (follows & flag)
            {
                if (!text.empty())
                {
                    text += '|';
                }
                text += name;
            }
        };
        add(FOLLOWS_LEFT, "left");
        add(FOLLOWS_TOP, "top");
        add(FOLLOWS_RIGHT, "right");
        add(FOLLOWS_BOTTOM, "bottom");
        return text.empty() ? std::string("none") : text;
    }

    std::string rectText(const LLRect& r)
    {
        return "left " + std::to_string(r.mLeft) + "  top " + std::to_string(r.mTop)
             + "  right " + std::to_string(r.mRight) + "  bottom " + std::to_string(r.mBottom)
             + "  (" + std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight()) + ")";
    }

    bool isBuilt(ALXUICatalog::Kind kind)
    {
        switch (kind)
        {
        case ALXUICatalog::Kind::Floater:
        case ALXUICatalog::Kind::Panel:
        case ALXUICatalog::Kind::Menu:
        case ALXUICatalog::Kind::Widget:
        case ALXUICatalog::Kind::Template:
            return true;
        default:
            return false;
        }
    }

    S32 countViews(const LLView* view)
    {
        S32 n = 1;
        for (const LLView* child : *view->getChildList())
        {
            n += countViews(child);
        }
        return n;
    }

    S32 widestLine(const LLFontGL* font, const std::string& text)
    {
        S32 widest = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
            {
                end = text.size();
            }
            widest = llmax(widest, font->getWidth(std::string_view(text).substr(start, end - start)));
            start = end + 1;
        }
        return widest;
    }

    // From the first '<' on a line, the bytes of the element that starts
    // there: tags open and close it, and comments and declarations are
    // skipped over.
    std::string elementTextAt(const std::string& text, S32 line)
    {
        size_t pos = 0;
        for (S32 l = 1; l < line && pos != std::string::npos; ++l)
        {
            pos = text.find('\n', pos);
            if (pos != std::string::npos)
            {
                ++pos;
            }
        }
        if (pos == std::string::npos)
        {
            return std::string();
        }
        const size_t start = text.find('<', pos);
        if (start == std::string::npos)
        {
            return std::string();
        }
        S32 depth = 0;
        size_t i = start;
        while (i < text.size())
        {
            if (text.compare(i, 4, "<!--") == 0)
            {
                const size_t end = text.find("-->", i);
                i = end == std::string::npos ? text.size() : end + 3;
                continue;
            }
            if (text[i] == '<')
            {
                const bool closing = i + 1 < text.size() && text[i + 1] == '/';
                const bool declaration = i + 1 < text.size() && (text[i + 1] == '?' || text[i + 1] == '!');
                const size_t end = text.find('>', i);
                if (end == std::string::npos)
                {
                    break;
                }
                if (!declaration)
                {
                    const bool self_closing = end > 0 && text[end - 1] == '/';
                    if (closing || self_closing)
                    {
                        if (!closing)
                        {
                            ++depth;
                        }
                        --depth;
                    }
                    else
                    {
                        ++depth;
                    }
                    if (depth <= 0)
                    {
                        return text.substr(start, end + 1 - start);
                    }
                }
                i = end + 1;
                continue;
            }
            ++i;
        }
        return text.substr(start);
    }

    std::string numbered(const std::string& text, S32 first_line)
    {
        std::string out;
        S32 line = first_line;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            const bool last = end == std::string::npos;
            if (last)
            {
                end = text.size();
            }
            out += std::to_string(line++);
            out += "  ";
            out += text.substr(start, end - start);
            out += '\n';
            if (last)
            {
                break;
            }
            start = end + 1;
        }
        return out;
    }

    const char* KIND_FIELDS[] = { "any", "tag", "attribute", "value", "name", "text" };

    ALXUICatalog::Field fieldFrom(const std::string& value)
    {
        if (value == "tag") return ALXUICatalog::Field::Tag;
        if (value == "attribute") return ALXUICatalog::Field::Attribute;
        if (value == "value") return ALXUICatalog::Field::Value;
        if (value == "name") return ALXUICatalog::Field::Name;
        if (value == "text") return ALXUICatalog::Field::Text;
        return ALXUICatalog::Field::Any;
    }

    // The tags the UI library registers. A tag the viewer registers is
    // built by a viewer class, whose constructor is viewer code with the
    // expectations of viewer code: a notification to attach to, an agent,
    // a plugin. A shell build cannot meet them, so those tags are shown
    // from their files and searched, and built only by the viewer.
    bool isCoreWidgetTag(const std::string& tag)
    {
        static const std::set<std::string> core = {
            "accordion", "accordion_tab", "badge", "button", "chat_editor", "check_box", "combo_box",
            "console", "container_view", "context_menu", "filter_editor", "flat_list_view", "floater_view",
            "flyout_button", "folder_view_item", "fs_virtual_trackpad", "icon", "icons_combo_box",
            "layout_panel", "layout_stack", "line_editor", "loading_indicator", "locate", "menu",
            "menu_bar", "menu_button", "menu_item", "menu_item_call", "menu_item_check",
            "menu_item_separator", "menu_item_tear_off", "multi_slider", "multi_slider_bar", "panel",
            "placeholder", "progress_bar", "radio_group", "scroll_bar", "scroll_container", "scroll_list",
            "scrolling_panel_list", "search_editor", "simple_text_editor", "slider", "slider_bar",
            "spinner", "stat_bar", "stat_view", "sun_moon_trackball", "tab_container", "text", "time",
            "toggleable_menu", "tool_tip", "toolbar", "tooltip_view", "ui_ctrl", "view", "view_border",
            "window_shade", "xy_vector"
        };
        return core.count(tag) != 0;
    }

    // The core tags the gallery shows: the ones that stand on their own
    // with defaults. The rest need a parent of a kind, children, or
    // parameters a template does not give.
    bool galleryTag(const std::string& tag)
    {
        static const std::set<std::string> skipped = {
            "accordion", "accordion_tab", "chat_editor", "console", "container_view", "context_menu",
            "flat_list_view", "floater_view", "folder_view_item", "layout_panel", "layout_stack", "locate",
            "menu", "menu_bar", "menu_item", "menu_item_call", "menu_item_check", "menu_item_separator",
            "menu_item_tear_off", "panel", "placeholder", "scroll_container", "scrolling_panel_list",
            "stat_view", "tab_container", "toggleable_menu", "tool_tip", "toolbar", "tooltip_view",
            "ui_ctrl", "view", "window_shade"
        };
        return isCoreWidgetTag(tag) && skipped.count(tag) == 0;
    }
}

// ===========================================================================
// ALFloaterXUIStudio
// ===========================================================================
ALFloaterXUIStudio::ALFloaterXUIStudio(const LLSD& key)
:   LLFloater(key)
{
    mCommitCallbackRegistrar.add("XUIStudio.Tree", boost::bind(&ALFloaterXUIStudio::onTreeAction, this, _2));
    mEnableCallbackRegistrar.add("XUIStudio.TreeEnabled", boost::bind(&ALFloaterXUIStudio::onTreeActionEnabled, this, _2));
    mCommitCallbackRegistrar.add("XUIStudio.List", boost::bind(&ALFloaterXUIStudio::onListAction, this, _2));
    mEnableCallbackRegistrar.add("XUIStudio.ListEnabled", boost::bind(&ALFloaterXUIStudio::onListActionEnabled, this, _2));
    mCommitCallbackRegistrar.add("XUIStudio.Menu", boost::bind(&ALFloaterXUIStudio::onMenuAction, this, _2));
    mEnableCallbackRegistrar.add("XUIStudio.MenuCheck", boost::bind(&ALFloaterXUIStudio::onMenuCheck, this, _2));
    mEnableCallbackRegistrar.add("XUIStudio.MenuEnable", boost::bind(&ALFloaterXUIStudio::onMenuEnable, this, _2));
}

ALFloaterXUIStudio::~ALFloaterXUIStudio()
{
    // The channels outlive this, and each holds a slot bound to it.
    for (LLBoundListener& listener : mChannelListeners)
    {
        listener.disconnect();
    }
    closePreviews();
}

bool ALFloaterXUIStudio::postBuild()
{
    mCatalogFilter = getChild<LLFilterEditor>("catalog_filter");
    mFileList = getChild<LLScrollListCtrl>("file_list");
    mSkinCombo = getChild<LLComboBox>("skin_combo");
    mLanguageCombo = getChild<LLComboBox>("language_combo");
    mLanguageCombo2 = getChild<LLComboBox>("language_combo_2");
    mSecondaryCheck = getChild<LLCheckBoxCtrl>("secondary_check");
    mFindQuery = getChild<LLLineEditor>("find_query");
    mFindField = getChild<LLComboBox>("find_field");
    mFindResults = getChild<LLScrollListCtrl>("find_results");
    mTreeFilter = getChild<LLFilterEditor>("tree_filter");
    mTreePanel = getChild<LLPanel>("tree_host");
    mBreadcrumb = getChild<LLPanel>("breadcrumb");
    mFindings = getChild<LLScrollListCtrl>("findings");
    mInspectors = getChild<LLTabContainer>("inspector_tabs");
    mAttributeGrid = getChild<ALPropertyGrid>("attributes_grid");
    mLayout = getChild<LLScrollListCtrl>("layout");
    mSourceLayers = getChild<LLTextBox>("source_layers");
    mSourceText = getChild<LLTextEditor>("source_text");
    mBindings = getChild<LLScrollListCtrl>("bindings");
    mState = getChild<LLScrollListCtrl>("state");
    mSelectionFindings = getChild<LLScrollListCtrl>("selection_findings");
    mMenuBar = getChild<LLMenuBarGL>("studio_menu");
    mBottomTabs = getChild<LLTabContainer>("bottom_tabs");
    mNotifications = getChild<LLScrollListCtrl>("notifications");
    mNotificationFilter = getChild<LLFilterEditor>("notification_filter");
    mAttributeGrid->onFieldCommit(boost::bind(&ALFloaterXUIStudio::onFieldCommit, this, _1, _2));
    getChild<LLCheckBoxCtrl>("attributes_authored")->setCommitCallback(
        [this](LLUICtrl* ctrl, const LLSD&)
        {
            mAttributeGrid->setAuthoredOnly(ctrl->getValue().asBoolean());
        });
    mChannels = getChild<LLScrollListCtrl>("channels");
    mChannelResponse = getChild<LLComboBox>("channel_response");
    mPalette = getChild<LLScrollListCtrl>("palette");
    getChild<LLButton>("palette_insert")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onInsertFromPalette, this));
    getChild<LLButton>("gallery_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::showGallery, this));
    mTranslateLanguage = getChild<LLComboBox>("translate_language");
    mTranslateList = getChild<LLScrollListCtrl>("translate_list");
    mTranslateValue = getChild<LLLineEditor>("translate_value");
    mTranslateCounts = getChild<LLTextBox>("translate_counts");
    mEditTarget = getChild<LLTextBox>("edit_target");
    mStatus = getChild<LLTextBox>("status");

    loadState();
    scanCatalog();

    mCatalogFilter->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onCatalogFilter, this));
    mFileList->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onFileSelected, this));
    mFileList->setCommitOnSelectionChange(true);
    mSkinCombo->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onSkinOrLanguage, this));
    mLanguageCombo->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onSkinOrLanguage, this));
    mLanguageCombo2->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onSkinOrLanguage, this));
    mSecondaryCheck->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onToggleSecondary, this));
    mFindQuery->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onFind, this));
    mFindField->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onFind, this));
    mFindResults->setDoubleClickCallback(boost::bind(&ALFloaterXUIStudio::onFindResult, this));
    mTreeFilter->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onTreeFilter, this));
    mFindings->setDoubleClickCallback(boost::bind(&ALFloaterXUIStudio::onFindingSelected, this));

    // Every table in the tool copies the same way.
    for (LLScrollListCtrl* list : { mFileList, mFindResults, mFindings,
                                    mLayout, mBindings, mState, mSelectionFindings, mTranslateList })
    {
        watchList(list);
    }
    mInspectors->setCommitCallback(boost::bind(&ALFloaterXUIStudio::refreshInspectors, this));
    mBottomTabs->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onBottomTab, this));
    mNotifications->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onNotificationSelected, this));
    mNotificationFilter->setCommitCallback(boost::bind(&ALFloaterXUIStudio::fillNotifications, this));
    getChild<LLButton>("notification_post")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onPostNotification, this));
    mChannels->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onChannelSelected, this));
    getChild<LLButton>("channel_respond")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onRespondToNotification, this));
    getChild<LLButton>("channel_clear")->setClickedCallback([this](LLUICtrl*, const LLSD&)
    {
        mChannels->deleteAllItems();
        mChannelNotifications.clear();
        mChannelResponse->removeall();
    });
    watchChannels();
    mTranslateLanguage->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onTranslationLanguage, this));
    mTranslateList->setCommitOnSelectionChange(true);
    mTranslateList->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onTranslationSelected, this));
    mTranslateValue->setCommitCallback(boost::bind(&ALFloaterXUIStudio::onTranslationWrite, this));
    getChild<LLButton>("translate_write")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onTranslationWrite, this));
    getChild<LLButton>("translate_repair_file")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onRepairFile, this));
    getChild<LLButton>("translate_repair_all")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::startRepairAll, this));
    getChild<LLButton>("translate_repair_roots")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onRepairRoots, this));

    getChild<LLButton>("show_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::showPreviews, this));
    getChild<LLButton>("hide_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::closePreviews, this));
    getChild<LLButton>("reload_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::reloadAll, this));
    getChild<LLButton>("edit_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onJumpToSource, this));
    getChild<LLButton>("jump_btn")->setClickedCallback(boost::bind(&ALFloaterXUIStudio::onJumpToSource, this));

    mSecondaryCheck->setValue(mShowSecondary);
    mLanguageCombo2->setEnabled(mShowSecondary);

    mSelection.onSelectionChanged(boost::bind(&ALFloaterXUIStudio::onSelectionChanged, this));
    mSelection.onHoverChanged(boost::bind(&ALFloaterXUIStudio::onHoverChanged, this));
    mModel.setHoverHandler(boost::bind(&ALFloaterXUIStudio::onTreeHover, this, _1));
    mModel.setBadgeProvider([this](const ALXUISelection::path_t& path)
                            { return mPreviews[PRIMARY].lint.countUnder(path); });
    mModel.getFilter().setShowCodeBuilt(mShowCodeBuilt);
    mModel.getFilter().setEmptyLookupMessage(getString("NoResults"));

    if (!mFile.empty())
    {
        showPreviews();
    }
    else
    {
        setStatus(getString("NoFile"));
    }
    return true;
}

void ALFloaterXUIStudio::onClose(bool app_quitting)
{
    saveState();
    closePreviews();
}

void ALFloaterXUIStudio::draw()
{
    if (!mLintQueue.empty())
    {
        stepLintAll();
    }
    if (!mRepairQueue.empty())
    {
        stepRepairAll();
    }
    if (!mCensusQueue.empty())
    {
        stepCensus();
    }
    if (mReloadPending)
    {
        mReloadPending = false;
        if (mReloadEntryOnly)
        {
            // One file changed; the rest of the tree is as it was.
            mCatalog.reload(mFile);
        }
        else
        {
            scanCatalog();
        }
        mReloadEntryOnly = false;
        // A rebuild is not a new preview: it stays where it was put, and
        // it keeps the keyboard. Without this the arrows move an element
        // once and then nothing: the floater they were going to is gone,
        // and its replacement has never been focused.
        const LLFloater* was = mPreviews[PRIMARY].host.get();
        const bool had_keyboard = was && gFocusMgr.childHasKeyboardFocus(was);
        mKeepPlace = true;
        showPreviews();
        mKeepPlace = false;
        if (had_keyboard)
        {
            if (LLFloater* host = mPreviews[PRIMARY].host.get())
            {
                host->setFocus(true);
            }
        }
        if (mReloadFromDisk)
        {
            setStatus(getString("Reloaded"));
        }
        else if (!mPendingStatus.empty())
        {
            setStatus(mPendingStatus);
        }
        mReloadFromDisk = false;
        mPendingStatus.clear();
    }
    if (mTree)
    {
        mTree->update();
    }
    if (mInspectors->getCurrentPanel() && mInspectors->getCurrentPanel()->getName() == "state_tab"
        && mStateTimer.getElapsedTimeF32() > 0.25f)
    {
        mStateTimer.reset();
        refreshState(selectedView());
    }
    LLFloater::draw();
}

bool ALFloaterXUIStudio::handleKeyHere(KEY key, MASK mask)
{
    // The menu bar's own accelerators, which belong to this floater and
    // not to the viewer: they answer while it has the keyboard and are
    // silent everywhere else, which is what a floater-local menu is for.
    if (mMenuBar && mMenuBar->handleAcceleratorKey(key, mask))
    {
        return true;
    }
    if (key == 'F' && mask == MASK_CONTROL)
    {
        mTreeFilter->setFocus(true);
        return true;
    }
    if (key == 'C' && mask == MASK_CONTROL)
    {
        if (LLScrollListCtrl* list = focusedList())
        {
            copyList(list, list->getAllSelected());
            return true;
        }
    }
    // The panes keep their own keys: the hierarchy walks itself with the
    // arrows and a list scrolls with them, and a list that happens to
    // ignore one is not asking for a file to be written. The arrows move
    // the element when the tool itself holds the keyboard, and when the
    // preview does, which is where they are wanted.
    const LLFocusableElement* focus = gFocusMgr.getKeyboardFocus();
    if ((!focus || focus == static_cast<const LLFocusableElement*>(this)) && nudge(key, mask))
    {
        return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}

// ---------------------------------------------------------------------------
// The catalog pane
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::scanCatalog()
{
    mCatalog.scan(gDirUtilp->getSkinBaseDir());
    fillSkinsAndLanguages();
    fillCatalog();
}

void ALFloaterXUIStudio::fillSkinsAndLanguages()
{
    mSkinCombo->removeall();
    for (const std::string& skin : mCatalog.skins())
    {
        mSkinCombo->add(skin, LLSD(skin));
    }
    if (std::find(mCatalog.skins().begin(), mCatalog.skins().end(), mSkin) == mCatalog.skins().end())
    {
        mSkin = "default";
    }
    mSkinCombo->setValue(mSkin);

    for (LLComboBox* combo : { mLanguageCombo, mLanguageCombo2, mTranslateLanguage })
    {
        combo->removeall();
        for (const std::string& language : mCatalog.languages())
        {
            combo->add(language, LLSD(language));
        }
    }
    const std::vector<std::string>& languages = mCatalog.languages();
    if (std::find(languages.begin(), languages.end(), mLanguage) == languages.end())
    {
        mLanguage = "en";
    }
    if (std::find(languages.begin(), languages.end(), mLanguage2) == languages.end())
    {
        mLanguage2 = "en";
    }
    mLanguageCombo->setValue(mLanguage);
    mLanguageCombo2->setValue(mLanguage2);
    mTranslateLanguage->setValue(mLanguage2);
}

// static
LLSD ALFloaterXUIStudio::row(const LLSD& id, std::initializer_list<std::pair<const char*, std::string>> cells)
{
    LLSD r;
    r["id"] = id;
    S32 i = 0;
    for (const auto& [column, value] : cells)
    {
        r["columns"][i]["column"] = column;
        r["columns"][i]["value"] = value;
        ++i;
    }
    return r;
}

void ALFloaterXUIStudio::fillCatalog()
{
    const std::string filter = utf8str_tolower(mCatalogFilter->getText());
    mFileList->deleteAllItems();
    for (const ALXUICatalog::Entry& e : mCatalog.entries())
    {
        const char* kind = ALXUICatalog::kindName(e.kind);
        if (!filter.empty()
            && utf8str_tolower(e.name).find(filter) == std::string::npos
            && utf8str_tolower(e.title).find(filter) == std::string::npos
            && filter != kind)
        {
            continue;
        }
        const bool has_language = mLanguage != "en"
            && (e.layer(mSkin, mLanguage) || e.layer("default", mLanguage));
        const bool has_skin = mSkin != "default"
            && (e.layer(mSkin, "en") || e.layer(mSkin, mLanguage));
        mFileList->addElement(row(e.name, {
            { "kind", kind },
            { "name", e.name },
            { "lang", has_language ? "x" : "" },
            { "skin", has_skin ? "x" : "" } }));
    }
    if (!mFile.empty())
    {
        mFileList->setSelectedByValue(mFile, true);
    }
}

void ALFloaterXUIStudio::onCatalogFilter()
{
    fillCatalog();
}

void ALFloaterXUIStudio::onFileSelected()
{
    const std::string file = mFileList->getSelectedValue().asString();
    if (file.empty() || file == mFile)
    {
        return;
    }
    mFile = file;
    mSelection.clearSelection();
    saveState();
    showPreviews();
}

void ALFloaterXUIStudio::onSkinOrLanguage()
{
    mSkin = mSkinCombo->getValue().asString();
    mLanguage = mLanguageCombo->getValue().asString();
    mLanguage2 = mLanguageCombo2->getValue().asString();
    saveState();
    fillCatalog();
    if (!mFile.empty())
    {
        showPreviews();
    }
}

void ALFloaterXUIStudio::onFind()
{
    const std::string query = mFindQuery->getText();
    mFindResults->deleteAllItems();
    if (query.empty())
    {
        return;
    }
    std::vector<ALXUICatalog::Hit> hits = mCatalog.find(query, fieldFrom(mFindField->getValue().asString()));
    S32 shown = 0;
    for (const ALXUICatalog::Hit& hit : hits)
    {
        if (shown++ >= MAX_FIND_ROWS)
        {
            break;
        }
        LLSD id;
        id["file"] = hit.entry->name;
        id["skin"] = hit.layer->skin;
        id["language"] = hit.layer->language;
        id["line"] = hit.line;
        id["path"] = hit.path;
        mFindResults->addElement(row(id, {
            { "file", hit.entry->name },
            { "line", std::to_string(hit.line) },
            { "layer", hit.layer->skin + "/" + hit.layer->language },
            { "snippet", hit.snippet } }));
    }
    setStatus(std::to_string(hits.size()) + (hits.size() == 1 ? " match" : " matches")
              + (hits.size() > (size_t)MAX_FIND_ROWS ? ", the first " + std::to_string(MAX_FIND_ROWS) + " listed" : ""));
}

void ALFloaterXUIStudio::onFindResult()
{
    LLScrollListItem* item = mFindResults->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD id = item->getValue();
    const std::string file = id["file"].asString();
    const std::string language = id["language"].asString();
    if (language != "en" && language != mLanguage)
    {
        mLanguage = language;
        mLanguageCombo->setValue(mLanguage);
    }
    if (file != mFile)
    {
        mFile = file;
        mFileList->setSelectedByValue(mFile, true);
        showPreviews();
    }
    mSelection.select(ALXUISelection::fromString(id["path"].asString()));
    if (!selectedView())
    {
        // Not a built element: open the file at the line instead.
        openInEditor(id["path"].asString().empty() ? std::string() : std::string(), 0);
        const ALXUICatalog::Entry* entry = mCatalog.find(file);
        if (const ALXUICatalog::Layer* layer = entry ? entry->layer(id["skin"].asString(), language) : nullptr)
        {
            mSourcePath = layer->path;
            mSourceLine = id["line"].asInteger();
        }
    }
}

// ---------------------------------------------------------------------------
// Previews
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::closePreview(S32 which)
{
    Preview& pv = mPreviews[which];
    if (LLFloater* host = pv.host.get())
    {
        if (which == PRIMARY)
        {
            const LLRect r = host->calcScreenRect();
            mLastX = r.mLeft;
            mLastY = r.mBottom;
        }
        detachHost(host);
        host->closeFloater();
    }
    pv.host.markDead();
    pv.root = nullptr;
    pv.node = nullptr;
    pv.sourceMap.clear();
    pv.overlay.clear();
    pv.liveFiles.clear();
    pv.diagnostics.clear();
    pv.lint.clear();
    if (which == PRIMARY)
    {
        clearTree();
    }
}

void ALFloaterXUIStudio::closePreviews()
{
    for (S32 i = 0; i < PREVIEWS; ++i)
    {
        closePreview(i);
    }
}

void ALFloaterXUIStudio::hostClosed(S32 which)
{
    Preview& pv = mPreviews[which];
    if (which == PRIMARY && pv.root)
    {
        const LLRect r = pv.root->calcScreenRect();
        mLastX = r.mLeft;
        mLastY = r.mBottom;
    }
    pv.host.markDead();
    pv.root = nullptr;
    pv.node = nullptr;
    pv.sourceMap.clear();
    pv.overlay.clear();
    pv.liveFiles.clear();
    if (which == PRIMARY)
    {
        clearTree();
        refreshBreadcrumb();
        refreshInspectors();
    }
}

void ALFloaterXUIStudio::showPreviews()
{
    showPreview(PRIMARY);
    if (mShowSecondary)
    {
        showPreview(SECONDARY);
    }
    else
    {
        closePreview(SECONDARY);
    }
}

// A preview opens beside the tool, since the two are read together. A
// rebuild is not an opening: a preview someone has moved stays where they
// moved it.
void ALFloaterXUIStudio::placeHost(S32 which, LLFloater* host)
{
    const LLRect tool = calcScreenRect();
    if (which == PRIMARY)
    {
        if (mKeepPlace && mLastX >= 0)
        {
            host->setOrigin(mLastX, mLastY);
        }
        else
        {
            host->setOrigin(tool.mRight + 8, tool.mTop - host->getRect().getHeight());
        }
    }
    else if (LLFloater* primary = mPreviews[PRIMARY].host.get())
    {
        const LLRect p = primary->getRect();
        host->setOrigin(p.mRight + 8, p.mTop - host->getRect().getHeight());
    }
    else
    {
        host->setOrigin(tool.mRight + 8, tool.mTop - host->getRect().getHeight());
    }
    gFloaterView->adjustToFitScreen(host, false);
}

LLView* ALFloaterXUIStudio::buildRoot(S32 which, const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr& node)
{
    LLUICtrlFactory& factory = LLUICtrlFactory::instance();
    const std::string& file = entry.name;

    if (entry.kind == ALXUICatalog::Kind::Template)
    {
        // The widget the template is for, with nothing but its defaults.
        std::string tag = file.substr(file.rfind('/') + 1);
        tag = tag.substr(0, tag.size() - 4);
        const std::string xml = "<" + tag + " name=\"" + tag + "\" label=\"" + tag
                              + "\" layout=\"topleft\" left=\"8\" top=\"8\" width=\"200\" height=\"24\"/>";
        if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
        {
            return nullptr;
        }
    }
    else
    {
        // The viewer's own layers in the viewer's own order, merged by
        // its own call, with the tool's observer recording what each
        // layer wrote and what it dropped -- and the layer under edit
        // taken from the document rather than from the disk, so the
        // preview is of what has been done to it and not of what was
        // last written.
        if (!ALXmlLayerMerge::loadSources(sourcesFor(file), node, &mPreviews[which].overlay))
        {
            return nullptr;
        }
    }
    return buildFromNode(entry, host, node);
}

// The node as a view, by the kind of file it is. A floater is the host
// itself; everything else is hosted by it.
LLView* ALFloaterXUIStudio::buildFromNode(const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr node)
{
    LLUICtrlFactory& factory = LLUICtrlFactory::instance();
    const std::string& file = entry.name;

    LLView* root = nullptr;
    factory.pushFileName(file);
    switch (entry.kind)
    {
    case ALXUICatalog::Kind::Floater:
        if (host->initFloaterXML(node, gFloaterView, file))
        {
            root = host;
            host->setCanResize(host->isResizable());
        }
        break;

    case ALXUICatalog::Kind::Panel:
    {
        LLPanel::Params pp;
        LLPanel* panel = LLUICtrlFactory::create<LLPanel>(pp);
        if (panel->initPanelXML(node, host, LLUICtrlFactory::getDefaultParams<LLPanel>()))
        {
            panel->setOrigin(2, 2);
            panel->setUseBoundingRect(true);
            panel->updateBoundingRect();
            LLRect fit = panel->getRect();
            fit.unionWith(panel->getBoundingRect());
            // A child positioned past its parent's edge is drawn past it,
            // so the window starts where the drawing does and not where
            // the panel says it does.
            panel->setOrigin(2 + llmax(0, panel->getRect().mLeft - fit.mLeft),
                             2 + llmax(0, panel->getRect().mBottom - fit.mBottom));
            panel->reshape(fit.getWidth(), fit.getHeight());
            host->reshape(fit.getWidth() + 4, fit.getHeight() + 4 + LLFloater::getDefaultParams().header_height);
            host->setCanResize(true);
            root = panel;
        }
        else
        {
            delete panel;
        }
        break;
    }

    case ALXUICatalog::Kind::Menu:
    {
        LLMenuHolderGL::Params hp;
        hp.name = "menu_holder";
        hp.rect = host->getLocalRect();
        hp.follows.flags = FOLLOWS_ALL;
        LLMenuHolderGL* holder = LLUICtrlFactory::create<LLMenuHolderGL>(hp);
        holder->setCanHide(false);
        host->addChild(holder);
        LLView* view = factory.createFromXML(node, holder, file, LLMenuHolderGL::child_registry_t::instance());
        if (LLMenuGL* menu = view ? view->as<LLMenuGL>() : nullptr)
        {
            menu->setVisible(true);
            if (!menu->as<LLMenuBarGL>())
            {
                menu->needsArrange();
                menu->arrangeAndClear();
            }
            const S32 header = LLFloater::getDefaultParams().header_height;
            const LLRect r = menu->getRect();
            host->reshape(llmax(r.getWidth() + 8, 120), r.getHeight() + 8 + header);
            holder->reshape(host->getRect().getWidth(), host->getRect().getHeight() - header);
            menu->setOrigin(4, holder->getRect().getHeight() - r.getHeight() - 4);
            root = menu;
        }
        else if (view)
        {
            root = view;
        }
        break;
    }

    case ALXUICatalog::Kind::Notifications:
        root = buildNotification(host);
        break;

    case ALXUICatalog::Kind::Widget:
    case ALXUICatalog::Kind::Template:
    {
        LLView* view = factory.createFromXML(node, host, file, LLDefaultChildRegistry::instance());
        if (view)
        {
            const S32 header = LLFloater::getDefaultParams().header_height;
            LLRect r = view->getRect();
            host->reshape(llmax(r.getWidth() + 16, 120), llmax(r.getHeight() + 16, 40) + header);
            view->setOrigin(8, host->getRect().getHeight() - header - r.getHeight() - 8);
            host->setCanResize(true);
            root = view;
        }
        break;
    }

    default:
        break;
    }
    factory.popFileName();
    return root;
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------
// A template as the panel it would produce. A notification can be built
// without being posted -- its constructor is public and touches no channel
// -- so nothing here reaches the queues, the history, or what the viewer
// remembers about "do not show me this again".
//
// No substitutions are supplied. A template's [PLACEHOLDER] left standing is
// what a XUI author wants to see: it says where the text will grow, which is
// the question a preview of a notification is being asked.
LLView* ALFloaterXUIStudio::buildNotification(ALXUIPreviewHost* host)
{
    if (mNotification.empty() || !LLNotifications::instance().templateExists(mNotification))
    {
        return nullptr;
    }
    const LLNotificationTemplatePtr tmpl = LLNotifications::instance().getTemplate(mNotification);
    if (!tmpl)
    {
        return nullptr;
    }

    LLSDParamAdapter<LLNotification::Params> params;
    params.name = mNotification;
    const LLNotificationPtr note(new LLNotification(params));

    // The panel the viewer would route this type to.
    LLPanel* panel = (tmpl->mType == "alertmodal" || tmpl->mType == "alert")
                   ? (LLPanel*)new LLToastAlertPanel(note, false)
                   : (LLPanel*)new LLToastNotifyPanel(note);

    const S32 header = LLFloater::getDefaultParams().header_height;
    const LLRect r = panel->getRect();
    host->addChild(panel);
    host->reshape(llmax(r.getWidth() + 16, 160), llmax(r.getHeight() + 16, 40) + header);
    panel->setOrigin(8, host->getRect().getHeight() - header - r.getHeight() - 8);
    host->setCanResize(true);
    return panel;
}

// Every template the notification system knows, which is notifications.xml
// as the viewer read it rather than as the file says it: the layers are
// merged and the language applied by the time it is here.
void ALFloaterXUIStudio::fillNotifications()
{
    if (!mNotifications)
    {
        return;
    }
    const std::string selected = mNotification;
    mNotifications->deleteAllItems();

    std::string filter = mNotificationFilter ? mNotificationFilter->getText() : std::string();
    LLStringUtil::toLower(filter);

    for (auto it = LLNotifications::instance().templatesBegin();
         it != LLNotifications::instance().templatesEnd(); ++it)
    {
        const LLNotificationTemplatePtr& tmpl = it->second;
        if (!filter.empty())
        {
            std::string name = tmpl->mName;
            LLStringUtil::toLower(name);
            if (name.find(filter) == std::string::npos)
            {
                continue;
            }
        }
        std::string message = tmpl->mMessage;
        LLStringUtil::replaceChar(message, '\n', ' ');
        mNotifications->addElement(row(tmpl->mName, {
            { "name", tmpl->mName },
            { "type", tmpl->mType },
            { "buttons", std::to_string(tmpl->mForm ? tmpl->mForm->getNumElements() : 0) },
            { "message", message } }));
    }
    mNotifications->sortByColumn("name", true);
    if (!selected.empty())
    {
        mNotifications->selectByValue(selected);
    }
}

// Sending one for real, which is the other half of the question: the
// preview says what it looks like, and this says where it goes. It is a
// button rather than the selection, so nobody posts a notification to the
// whole viewer by arrowing down a list.
void ALFloaterXUIStudio::onPostNotification()
{
    const LLSD value = mNotifications->getSelectedValue();
    if (!value.isDefined())
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = value.asString();
    setStatus(getString("NotificationPosted", args));
    LLNotifications::instance().add(value.asString(), LLSD(), LLSD());
}

// ---------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------
// Every notification through every channel, in the order the channels
// process them. The first four decide whether a notification is seen at
// all; the rest hang off Visible and are the kinds it can be seen as. A
// notification that appears in one and not the next was stopped between
// them, which is the question this pane exists to answer.
void ALFloaterXUIStudio::watchChannels()
{
    static const char* CHANNELS[] = {
        "Unexpired", "Ignore", "VisibilityRules", "Visible",
        "Persistent", "Alerts", "AlertModal",
        "Group Notifications", "Notifications", "NotificationTips" };

    for (const char* name : CHANNELS)
    {
        LLNotificationChannelPtr channel = LLNotifications::instance().getChannel(name);
        if (!channel)
        {
            continue;
        }
        const std::string label(name);
        mChannelListeners.push_back(channel->connectChanged(
            [this, label](const LLSD& payload) { return onChannelChanged(label, payload); }));
    }
}

bool ALFloaterXUIStudio::onChannelChanged(const std::string& channel, const LLSD& payload)
{
    const LLNotificationPtr note = LLNotifications::instance().find(payload["id"].asUUID());
    if (note && mChannels)
    {
        // Held by pointer rather than copied: the notification may be
        // answered and dropped while its row is still on screen, and a row
        // that outlives what it is about is what the console it replaces
        // used a raw new and a destructor to avoid.
        const std::string id = note->getID().asString();
        mChannelNotifications[id] = note;

        std::string message = note->getMessage();
        LLStringUtil::replaceChar(message, '\n', ' ');
        mChannels->addElement(row(id, {
            { "channel", channel },
            { "name", note->getName() },
            { "time", LLDate(LLTimer::getTotalSeconds()).toHTTPDateString("%H:%M:%S") },
            { "message", message } }));
    }
    return false;
}

void ALFloaterXUIStudio::onChannelSelected()
{
    mChannelResponse->removeall();
    const LLNotificationPtr note = selectedChannelNotification();
    if (!note)
    {
        return;
    }
    const LLNotificationFormPtr form = note->getForm();
    if (!form)
    {
        return;
    }
    const LLSD elements = form->asLLSD();
    for (LLSD::array_const_iterator it = elements.beginArray(); it != elements.endArray(); ++it)
    {
        if ((*it)["type"].asString() == "button")
        {
            mChannelResponse->add((*it)["text"].asString());
        }
    }
}

void ALFloaterXUIStudio::onRespondToNotification()
{
    const LLNotificationPtr note = selectedChannelNotification();
    const std::string button = mChannelResponse->getSelectedValue().asString();
    if (!note || button.empty())
    {
        return;
    }
    LLSD response = note->getResponseTemplate();
    response[button] = true;
    note->respond(response);

    LLStringUtil::format_map_t args;
    args["[NAME]"] = note->getName();
    args["[BUTTON]"] = button;
    setStatus(getString("NotificationAnswered", args));
}

LLNotificationPtr ALFloaterXUIStudio::selectedChannelNotification() const
{
    const LLSD value = mChannels->getSelectedValue();
    if (!value.isDefined())
    {
        return LLNotificationPtr();
    }
    const auto found = mChannelNotifications.find(value.asString());
    return found == mChannelNotifications.end() ? LLNotificationPtr() : found->second;
}

// ---------------------------------------------------------------------------
// The palette
// ---------------------------------------------------------------------------
// What may go under the selected element: the registry that element names
// as its children, which is what the factory will accept there and nothing
// wider. A tag that takes text or holds other widgets says so, since that
// is what decides which of them is wanted.
void ALFloaterXUIStudio::fillPalette()
{
    if (!mPalette)
    {
        return;
    }
    const std::string chosen = mPalette->getSelectedValue().asString();
    mPalette->deleteAllItems();

    LLView* view = selectedView();
    const std::string* tag = view ? LLUICtrlFactory::widgetTag(view->viewType()) : nullptr;
    const ALXUISchema::Tag* declared = tag ? ALXUISchema::get().tag(*tag) : nullptr;
    if (!declared)
    {
        return;
    }
    for (const std::string& child : declared->children)
    {
        const ALXUISchema::Tag* what = ALXUISchema::get().tag(child);
        std::string takes;
        if (what)
        {
            if (what->text)
            {
                takes = "text";
            }
            if (!what->children.empty())
            {
                takes += takes.empty() ? "" : ", ";
                takes += std::to_string(what->children.size()) + " kinds of child";
            }
        }
        mPalette->addElement(row(child, { { "tag", child }, { "takes", takes } }));
    }
    if (!chosen.empty())
    {
        mPalette->selectByValue(chosen);
    }
}

// A new element carrying its name, where it goes, and nothing else: the
// widget's own template supplies the rest, and writing what the template
// already says is what makes a file hard to read.
void ALFloaterXUIStudio::onInsertFromPalette()
{
    const LLSD chosen = mPalette->getSelectedValue();
    if (!chosen.isDefined() || !mSelection.hasSelection())
    {
        setStatus(getString("EditNoSelection"));
        return;
    }
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        return;
    }
    const std::vector<const ALXUICatalog::Layer*> layers =
        mCatalog.layersFor(*entry, mPreviews[PRIMARY].skin, mLanguage);
    if (layers.empty())
    {
        setStatus(getString("EditNoTarget"));
        return;
    }
    ALXUIEdit* held = document(*layers.front());
    if (!held)
    {
        return;
    }

    // A name of its own, since a name is identity to the merge, to
    // getChild and to every overlay, and two of one name is a defect the
    // lint already reports.
    const std::string tag = chosen.asString();
    std::string name = tag;
    for (S32 n = 2; held->resolve({ name }) && n < 100; ++n)
    {
        name = tag + "_" + std::to_string(n);
    }

    const std::string xml = "<" + tag + " name=\"" + name + "\" layout=\"topleft\""
                            " left=\"8\" top=\"8\" width=\"100\" height=\"20\"/>";
    if (!held->insertElement(mSelection.selection(), xml))
    {
        setStatus(held->error());
        return;
    }

    LLStringUtil::format_map_t args;
    args["[ATTRS]"] = tag;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layers.front()->skin + "/" + layers.front()->language;
    documentChanged(getString("EditWrote", args));
}

// The two tabs that fill themselves from something other than the preview.
void ALFloaterXUIStudio::onBottomTab()
{
    fillTranslation();
    fillPalette();
    if (mNotifications->getItemCount() == 0)
    {
        fillNotifications();
    }
}

void ALFloaterXUIStudio::onNotificationSelected()
{
    const LLSD value = mNotifications->getSelectedValue();
    if (!value.isDefined())
    {
        return;
    }
    mNotification = value.asString();
    showPreviews();
}

// ---------------------------------------------------------------------------
// The document under edit
// ---------------------------------------------------------------------------
// The layers a file is built from, with the one being edited taken from
// memory. Every preview goes through here, so an edit shows on the screen
// without the disk hearing about it.
std::vector<ALXmlLayerMerge::Source> ALFloaterXUIStudio::sourcesFor(const std::string& file) const
{
    std::vector<std::string> paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, file);
    if (paths.empty())
    {
        paths.push_back(file);
    }

    std::vector<ALXmlLayerMerge::Source> sources;
    sources.reserve(paths.size());
    for (const std::string& path : paths)
    {
        const bool edited = !mDocumentPath.empty() && path == mDocumentPath;
        sources.push_back({ path, edited ? &mDocument.text() : nullptr });
    }
    return sources;
}

// The document for a layer, loaded from it the first time. Moving to
// another layer with work in hand would lose it, so it does not: the
// caller is told to save or revert first.
ALXUIEdit* ALFloaterXUIStudio::document(const ALXUICatalog::Layer& layer)
{
    if (mDocumentPath == layer.path)
    {
        return &mDocument;
    }
    if (mDocument.dirty())
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
        setStatus(getString("EditUnsaved", args));
        return nullptr;
    }
    if (!mDocument.loadFile(layer.path))
    {
        setStatus(mDocument.error());
        return nullptr;
    }
    mDocumentPath = layer.path;
    return &mDocument;
}

// What an operation does once it has changed the document: the preview is
// rebuilt from what is now in memory, next frame.
void ALFloaterXUIStudio::documentChanged(const std::string& status)
{
    mPendingStatus = status;
    setStatus(status);
    mReloadEntryOnly = true;
    mReloadPending = true;
}

// What the unsaved edits would do to the translations, before they are
// written. A translation applies because the base has an element of that
// name at that place; rename it, move it, or take it away, and the
// language's value stops arriving without a word being said to anyone.
// This is the word: the base as the disk has it and the base as it now
// stands, each scanned against every language, and the difference is what
// the edit costs.
//
// Only when the document is the file's base layer. Editing a language's
// own overlay changes that translation and no other.
S32 ALFloaterXUIStudio::translationImpact(std::vector<Impact>& out) const
{
    out.clear();
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || mDocumentPath.empty() || !mDocument.dirty())
    {
        return 0;
    }
    const std::vector<const ALXUICatalog::Layer*> layers =
        mCatalog.layersFor(*entry, mPreviews[PRIMARY].skin, mLanguage);
    if (layers.empty() || layers.front()->path != mDocumentPath)
    {
        return 0;
    }

    ALXUIEdit before;
    if (!before.loadBuffer(mDocument.saved()) || !before.root() || !mDocument.root())
    {
        return 0;
    }

    S32 total = 0;
    for (const std::string& language : mCatalog.languages())
    {
        if (language == mLanguage)
        {
            continue;
        }
        const ALXUICatalog::Layer* overlay = overlayLayer(*entry, language);
        if (!overlay || !overlay->root())
        {
            continue;
        }

        ALXUITranslate was;
        ALXUITranslate now;
        was.scan(before.root(), overlay->root());
        now.scan(mDocument.root(), overlay->root());

        // A value the language wrote, keyed by where it sits in the
        // language's own file: the base paths are what the edit moves, so
        // they are the one thing that cannot be compared across the two
        // scans, and the overlay's line does not move at all.
        boost::unordered_set<std::string> applied;
        for (const ALXUITranslate::Unit& unit : was.units())
        {
            if (unit.applies())
            {
                applied.insert(std::to_string(unit.overlayLine) + "\n" + unit.field);
            }
        }
        for (const ALXUITranslate::Unit& unit : now.units())
        {
            if (unit.applies())
            {
                applied.erase(std::to_string(unit.overlayLine) + "\n" + unit.field);
            }
        }
        if (applied.empty())
        {
            continue;
        }

        Impact impact;
        impact.language = language;
        impact.stranded = (S32)applied.size();
        for (const ALXUITranslate::Unit& unit : was.units())
        {
            if (unit.applies() && applied.count(std::to_string(unit.overlayLine) + "\n" + unit.field)
                && impact.what.size() < 8)
            {
                impact.what.push_back(ALXUISelection::toString(unit.path)
                                      + (unit.field.empty() ? "" : "/" + unit.field));
            }
        }
        total += impact.stranded;
        out.push_back(std::move(impact));
    }
    return total;
}

void ALFloaterXUIStudio::reportTranslationImpact()
{
    std::vector<Impact> impacts;
    const S32 total = translationImpact(impacts);
    if (!total)
    {
        setStatus(getString("TranslateImpactNone"));
        return;
    }

    std::string languages;
    for (const Impact& impact : impacts)
    {
        languages += (languages.empty() ? "" : ", ") + impact.language
                   + " " + std::to_string(impact.stranded);
        for (const std::string& what : impact.what)
        {
            LL_INFOS("XUIStudio") << impact.language << "/" << mFile << ": " << what
                                  << " stops applying" << LL_ENDL;
        }
    }
    LLStringUtil::format_map_t args;
    args["[COUNT]"] = std::to_string(total);
    args["[LANGS]"] = languages;
    setStatus(getString("TranslateImpact", args));
}

// Save, then put the translations back where the saved base wants them.
// The repair moves what a language wrote to the path the base now gives
// it, so it has to run against the base as written and not as held.
void ALFloaterXUIStudio::saveAndRepair()
{
    std::vector<Impact> impacts;
    if (!translationImpact(impacts))
    {
        saveDocument();
        return;
    }

    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || !mDocument.save())
    {
        setStatus(mDocument.error());
        return;
    }
    mCatalog.reload(mFile);

    S32 moved = 0;
    std::string error;
    for (const Impact& impact : impacts)
    {
        const S32 count = repairFile(*entry, impact.language, error);
        if (count > 0)
        {
            moved += count;
        }
    }
    mCatalog.reload(mFile);

    LLStringUtil::format_map_t args;
    args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
    args["[MOVES]"] = std::to_string(moved);
    documentChanged(getString("EditSavedAndRepaired", args));
}

void ALFloaterXUIStudio::saveDocument()
{
    if (mDocumentPath.empty() || !mDocument.dirty())
    {
        setStatus(getString("EditNothingToSave"));
        return;
    }
    if (!mDocument.save())
    {
        setStatus(mDocument.error());
        return;
    }
    // The watchers prime themselves on what they find when the rebuild
    // makes them, so a write of the tool's own is not an outside change.
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
    documentChanged(getString("EditSaved", args));
}

void ALFloaterXUIStudio::revertDocument()
{
    if (mDocumentPath.empty() || !mDocument.dirty())
    {
        setStatus(getString("EditNothingToSave"));
        return;
    }
    if (!mDocument.loadFile(mDocumentPath))
    {
        setStatus(mDocument.error());
        return;
    }
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
    documentChanged(getString("EditReverted", args));
}

// A preview the tool did not make has no handles to let go of.
void ALFloaterXUIStudio::detachHost(LLFloater* host)
{
    if (ALXUIPreviewHost* preview = host ? host->as<ALXUIPreviewHost>() : nullptr)
    {
        preview->detach();
    }
}

// The name a file is registered under, when the real floater is what the
// author asked to see and this one may be built.
//
// A shell build shows what the file describes. It is the right answer for
// reading a layout and the wrong one for anything the class does: the
// callbacks a floater registers in its own constructor are not there, so
// every one of them reads as unregistered, and a panel the file names by
// class is a plain panel. The real floater answers all of that, and pays
// for it -- a constructor that wants an agent, a region or an inventory
// gets none of them at the login screen. So it is asked for, per file, and
// a name the deny list carries is never built.
std::string ALFloaterXUIStudio::realFloaterName(const ALXUICatalog::Entry& entry) const
{
    if (!mRealFloater || entry.kind != ALXUICatalog::Kind::Floater)
    {
        return LLStringUtil::null;
    }
    const std::string name = LLFloaterReg::findNameForFile(entry.name);
    if (name.empty() || !LLFloaterReg::getBuildData(name))
    {
        return LLStringUtil::null;
    }

    // The viewer's own gate first: a floater it would refuse to show now is
    // one this has no business building either.
    if (!LLFloaterReg::canShowInstance(name))
    {
        return LLStringUtil::null;
    }

    const std::string deny = gSavedSettings.getString("ALXUIStudioRealFloaterDenyList");
    for (size_t start = 0; start < deny.size();)
    {
        const size_t end = deny.find_first_of(" ,", start);
        const std::string one = deny.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (one == name)
        {
            return LLStringUtil::null;
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return name;
}

// The floater the registrar builds, made fresh rather than fetched: the
// instance the rest of the viewer shares is one the author may have open,
// and a preview must not move it, retitle it or close it.
LLFloater* ALFloaterXUIStudio::buildRealFloater(S32 which, const ALXUICatalog::Entry& entry,
                                                const std::string& name, LLXMLNodePtr& node)
{
    // The layers as the tool reads them, for the source map and the record
    // of which layer wrote what. The floater merges them again for itself.
    if (!ALXmlLayerMerge::loadSources(sourcesFor(entry.name), node, &mPreviews[which].overlay))
    {
        return nullptr;
    }

    const LLFloaterReg::BuildData* data = LLFloaterReg::getBuildData(name);
    LLFloater* floater = data->mFunc ? data->mFunc(LLSD()) : nullptr;
    if (!floater)
    {
        return nullptr;
    }
    if (!floater->buildFromFile(data->mFile))
    {
        floater->closeFloater();
        return nullptr;
    }
    return floater;
}

void ALFloaterXUIStudio::showPreview(S32 which)
{
    closePreview(which);
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        setStatus(getString("NoFile"));
        return;
    }
    Preview& pv = mPreviews[which];
    pv.skin = mSkin;
    pv.language = which == PRIMARY ? mLanguage : mLanguage2;

    // A widget file or template names its tag in its root or its name;
    // a viewer widget's constructor is not run by a shell build.
    std::string widget_tag;
    if (entry->kind == ALXUICatalog::Kind::Widget)
    {
        widget_tag = entry->rootTag;
    }
    else if (entry->kind == ALXUICatalog::Kind::Template)
    {
        widget_tag = mFile.substr(mFile.rfind('/') + 1);
        widget_tag = widget_tag.substr(0, widget_tag.size() - 4);
    }
    const bool viewer_widget = !widget_tag.empty() && !isCoreWidgetTag(widget_tag);

    // notifications.xml is a file of templates rather than a view tree; it
    // is built once one of them has been chosen on the Notifications tab.
    const bool notification = entry->kind == ALXUICatalog::Kind::Notifications && !mNotification.empty();
    if ((!isBuilt(entry->kind) && !notification) || viewer_widget)
    {
        if (which == PRIMARY)
        {
            LLStringUtil::format_map_t args;
            args["[TAG]"] = widget_tag;
            setStatus(viewer_widget ? getString("ViewerWidget", args) : getString("NotBuilt"));
            runLint();
            fillFindings();
            refreshBreadcrumb();
            refreshInspectors();
        }
        return;
    }

    LLFloater* host = nullptr;
    LLView* root = nullptr;
    LLXMLNodePtr node;
    LLTimer timer;
    if (const std::string registered = realFloaterName(*entry); !registered.empty())
    {
        ALXUISkinScope scope(pv.skin, pv.language);
        ALXUIDiagnostics sink;
        host = buildRealFloater(which, *entry, registered, node);
        root = host;
        pv.diagnostics = sink.entries();
    }
    else
    {
        ALXUISkinScope scope(pv.skin, pv.language);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        LLFloater::Params p(LLFloater::getDefaultParams());
        p.min_height = p.header_height;
        p.min_width = 10;
        ALXUIPreviewHost* preview = new ALXUIPreviewHost(this, which, p);
        host = preview;
        root = buildRoot(which, *entry, preview, node);
        pv.diagnostics = sink.entries();
    }
    pv.seconds = timer.getElapsedTimeF32();

    if (!root)
    {
        detachHost(host);
        host->closeFloater();
        if (which == PRIMARY)
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = mFile;
            setStatus(getString("BuildFailed", args));
            runLint();
            fillFindings();
        }
        return;
    }

    if (ALXUIPreviewHost* preview = host->as<ALXUIPreviewHost>())
    {
        preview->setRoot(root);
    }
    std::string title = root == host ? host->getTitle() : mFile;
    title += " [" + pv.skin + "/" + pv.language + (which == PRIMARY ? "" : ", second") + "]";
    host->setTitle(title);
    pv.host = host->getHandle();
    pv.root = root;
    pv.node = node;
    pv.views = countViews(root);
    // A notification's panel comes from its own XUI, not from the file the
    // template is in: there is no element in notifications.xml that any of
    // those widgets was built from, so nothing is paired with one.
    pv.sourceMap.build(root, entry->kind == ALXUICatalog::Kind::Notifications ? LLXMLNodePtr() : node);
    placeHost(which, host);
    host->openFloater();

    if (which == PRIMARY)
    {
        watchFiles(*entry);
        // Before the tree: a row shows the findings under it, and the
        // rows are made once.
        runLint();
        rebuildTree();
        fillFindings();
        const S32 findings = (S32)pv.lint.findings().size();
        LLStringUtil::format_map_t args;
        args["[VIEWS]"] = std::to_string(pv.views);
        args["[MS]"] = std::to_string((S32)(pv.seconds * 1000.f));
        args["[DIAG]"] = std::to_string(findings);
        args["[SKIN]"] = pv.skin;
        args["[LANG]"] = pv.language;
        // Which build this was, since the answer to almost every other
        // question the tool gives depends on it.
        setStatus(getString(host->as<ALXUIPreviewHost>() ? "Built" : "BuiltReal", args));
        fillTranslation();
        // The selection is a path; it may name something in the new tree.
        onSelectionChanged();
    }
}

void ALFloaterXUIStudio::watchFiles(const ALXUICatalog::Entry& entry)
{
    Preview& pv = mPreviews[PRIMARY];
    pv.liveFiles.clear();
    for (const ALXUICatalog::Layer* layer : mCatalog.layersFor(entry, pv.skin, pv.language))
    {
        auto live = std::make_unique<ALXUILiveFile>(layer->path, this);
        live->checkAndReload();
        live->addToEventTimer();
        pv.liveFiles.push_back(std::move(live));
    }
}

// Only the layers of the previewed file are watched, so a change on disk
// is a change to what is on screen: that one entry is read again rather
// than the whole tree. The watchers are made afresh by the rebuild and
// prime themselves on what they find, which is why the tool's own writes
// need no special case here.
void ALFloaterXUIStudio::fileChanged()
{
    // Not while there is work in hand. A rebuild reads the layers again,
    // and the layer under edit would come back as the disk has it: the
    // author is told instead, and chooses which of the two to keep.
    if (mDocument.dirty())
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
        setStatus(getString("EditChangedOnDisk", args));
        return;
    }

    // The check runs from a timer; the rebuild waits for the next frame.
    mReloadEntryOnly = true;
    mReloadFromDisk = true;
    mReloadPending = true;
}

void ALFloaterXUIStudio::reloadAll()
{
    mReloadEntryOnly = false;
    mReloadFromDisk = true;
    mReloadPending = true;
}

void ALFloaterXUIStudio::showGallery()
{
    ALXUIPreviewHost* host = nullptr;
    LLScrollContainer* scroller = nullptr;
    LLPanel* content = nullptr;
    S32 built = 0;
    {
        ALXUISkinScope scope(mSkin, mLanguage);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        LLFloater::Params p(LLFloater::getDefaultParams());
        p.min_height = 100;
        p.min_width = 200;
        host = new ALXUIPreviewHost(this, SECONDARY, p);
        const S32 header = p.header_height;
        host->reshape(900, 640 + header);
        host->setCanResize(true);
        host->setTitle("Widget gallery [" + mSkin + "/" + mLanguage + "]");

        // A child registry keeps its static registrations in a scope of
        // their own; the widget type registry lists every tag, and the
        // default child registry says which of them it builds.
        std::vector<std::string> tags;
        const auto& registrar = LLWidgetTypeRegistry::instance().defaultRegistrar();
        for (auto it = registrar.beginItems(); it != registrar.endItems(); ++it)
        {
            if (galleryTag(it->first) && LLDefaultChildRegistry::instance().getValue(it->first))
            {
                tags.push_back(it->first);
            }
        }
        std::sort(tags.begin(), tags.end());

        constexpr S32 COLUMNS = 4;
        constexpr S32 CELL_W = 220;
        constexpr S32 CELL_H = 64;
        const S32 rows = ((S32)tags.size() + COLUMNS - 1) / COLUMNS;
        const S32 content_h = rows * CELL_H + 8;

        LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        sp.name = "gallery_scroller";
        sp.rect = LLRect(0, host->getRect().getHeight() - header, host->getRect().getWidth(), 0);
        sp.follows.flags = FOLLOWS_ALL;
        scroller = LLUICtrlFactory::create<LLScrollContainer>(sp);
        host->addChild(scroller);

        LLPanel::Params cp;
        cp.name = "gallery";
        cp.rect = LLRect(0, content_h, COLUMNS * CELL_W + 8, 0);
        content = LLUICtrlFactory::create<LLPanel>(cp);
        scroller->addChild(content);

        LLUICtrlFactory& factory = LLUICtrlFactory::instance();
        S32 i = 0;
        for (const std::string& tag : tags)
        {
            const S32 col = i % COLUMNS;
            const S32 r = i / COLUMNS;
            const S32 left = 8 + col * CELL_W;
            const S32 top = 4 + r * CELL_H;
            ++i;

            const std::string label_xml = "<text name=\"label_" + tag + "\" layout=\"topleft\" left=\"" + std::to_string(left)
                + "\" top=\"" + std::to_string(top) + "\" width=\"" + std::to_string(CELL_W - 16) + "\" height=\"14\" font=\"SansSerifSmall\">"
                + tag + "</text>";
            LLXMLNodePtr label_node;
            if (LLXMLNode::parseBuffer(label_xml.data(), label_xml.size(), label_node))
            {
                factory.createFromXML(label_node, content, "gallery", LLDefaultChildRegistry::instance());
            }

            const std::string xml = "<" + tag + " name=\"" + tag + "\" label=\"" + tag + "\" layout=\"topleft\" left=\""
                + std::to_string(left) + "\" top=\"" + std::to_string(top + 16) + "\" width=\"" + std::to_string(CELL_W - 16)
                + "\" height=\"24\"/>";
            LLXMLNodePtr node;
            if (LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
            {
                factory.pushFileName("gallery");
                built += factory.createFromXML(node, content, "gallery", LLDefaultChildRegistry::instance()) != nullptr;
                factory.popFileName();
            }
        }
    }
    host->setRoot(content);
    host->detach();
    host->center();
    gFloaterView->adjustToFitScreen(host, false);
    host->openFloater();
    setStatus("Gallery: " + std::to_string(built) + " widgets in " + mSkin + "/" + mLanguage);
}

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::canvasHover(S32 which, const LLView* view)
{
    ALXUISelection::path_t path;
    if (view && ALXUISelection::pathOf(view, mPreviews[which].root, path))
    {
        mSelection.setHover(path);
    }
    else
    {
        mSelection.clearHover();
    }
}

void ALFloaterXUIStudio::canvasSelect(S32 which, const LLView* view)
{
    ALXUISelection::path_t path;
    if (view && ALXUISelection::pathOf(view, mPreviews[which].root, path))
    {
        mSelection.select(path);
    }
}

// ---------------------------------------------------------------------------
// The tree pane
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::clearTree()
{
    mRows.clear();
    mModel.setCanvasHover(nullptr);
    mModel.clear();
    if (mTree)
    {
        mTreePanel->deleteAllChildren();
        mTree = nullptr;
    }
}

void ALFloaterXUIStudio::rebuildTree()
{
    clearTree();
    Preview& pv = mPreviews[PRIMARY];
    ALXUITreeItem* root_item = mModel.build(pv.root, pv.sourceMap);
    if (!root_item)
    {
        return;
    }

    LLFolderView::Params p(LLUICtrlFactory::getDefaultParams<LLFolderView>());
    p.name = "xui_tree";
    p.title = root_item->getName();
    p.rect = LLRect(0, 0, mTreePanel->getRect().getWidth(), 0);
    p.parent_panel = mTreePanel;
    p.listener = root_item;
    p.view_model = &mModel;
    p.root = nullptr;
    p.use_ellipses = true;
    p.options_menu = "menu_xui_studio_tree.xml";
    mTree = LLUICtrlFactory::create<LLFolderView>(p);
    mTree->setCallbackRegistrar(&mCommitCallbackRegistrar);
    mTree->setEnableRegistrar(&mEnableCallbackRegistrar);

    LLRect scroller_rect = mTreePanel->getLocalRect();
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLFolderViewScrollContainer>());
    sp.rect(scroller_rect);
    LLScrollContainer* scroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(sp);
    scroller->setFollowsAll();
    mTreePanel->addChild(scroller);
    scroller->addChild(mTree);
    mTree->setScrollContainer(scroller);
    mTree->setFollowsAll();
    mTree->addChild(mTree->mStatusTextBox);
    mTree->setSelectCallback(boost::bind(&ALFloaterXUIStudio::onTreeSelection, this, _1, _2));
    mModel.setFolderView(mTree);

    createRows(root_item, mTree);
    mTree->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
    mTree->arrangeAll();
    mModel.getFilter().setModified();
}

void ALFloaterXUIStudio::createRows(ALXUITreeItem* item, LLFolderViewFolder* parent_widget)
{
    static const LLUIColor from_xml_color = LLUIColorTable::instance().getColor("MenuItemEnabledColor", LLColor4::white);
    static const LLUIColor code_built_color = LLUIColorTable::instance().getColor("MenuItemDisabledColor", LLColor4::grey);
    static const LLUIColor highlight_color = LLUIColorTable::instance().getColor("MenuItemHighlightColor", LLColor4::white);

    for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
    {
        ALXUITreeItem* child = static_cast<ALXUITreeItem*>(it->get());
        LLFolderViewItem::Params params(LLUICtrlFactory::getDefaultParams<LLFolderViewItem>());
        params.name = child->getName();
        params.root = mTree;
        params.listener = child;
        params.tool_tip = ALXUISelection::toString(child->getPath());
        params.text_pad_right = ALXUITreeEye::WIDTH + 4;
        params.font_color = child->isFromXML() ? from_xml_color : code_built_color;
        params.font_highlight_color = highlight_color;

        LLFolderViewItem* widget;
        if (child->hasChildren())
        {
            ALXUITreeFolder* folder = LLUICtrlFactory::create<ALXUITreeFolder>(params);
            folder->setChildrenInited(true);
            widget = folder;
        }
        else
        {
            widget = LLUICtrlFactory::create<ALXUITreeRow>(params);
        }
        widget->addToFolder(parent_widget);
        mRows[ALXUISelection::toString(child->getPath())] = widget;
        if (child->hasChildren())
        {
            createRows(child, static_cast<LLFolderViewFolder*>(widget));
        }
    }
}

void ALFloaterXUIStudio::onTreeFilter()
{
    mModel.getFilter().setFilterSubString(mTreeFilter->getText());
}

void ALFloaterXUIStudio::onTreeSelection(const std::deque<LLFolderViewItem*>& items, bool user_action)
{
    if (mSyncingTree || items.empty() || !items.front())
    {
        return;
    }
    ALXUITreeItem* item = static_cast<ALXUITreeItem*>(items.front()->getViewModelItem());
    if (!item)
    {
        return;
    }
    mSyncingTree = true;
    mSelection.select(item->getPath());
    mSyncingTree = false;
}

void ALFloaterXUIStudio::onTreeHover(const ALXUITreeItem* item)
{
    if (item)
    {
        mSelection.setHover(item->getPath());
    }
    else
    {
        mSelection.clearHover();
    }
}

ALXUITreeItem* ALFloaterXUIStudio::selectedItem() const
{
    return mSelection.hasSelection() ? mModel.itemFor(mSelection.selection()) : nullptr;
}

bool ALFloaterXUIStudio::onTreeActionEnabled(const LLSD& param)
{
    const std::string action = param.asString();
    if (action == "reveal")
    {
        ALXUITreeItem* item = selectedItem();
        return item && item->isFromXML();
    }
    if (action == "paste")
    {
        return !mCutPath.empty();
    }
    return true;
}

void ALFloaterXUIStudio::onTreeAction(const LLSD& param)
{
    const std::string action = param.asString();
    ALXUITreeItem* item = selectedItem();
    if (action == "expand_all" || action == "collapse_all")
    {
        if (mTree)
        {
            mTree->setOpenArrangeRecursively(action == "expand_all", LLFolderViewFolder::RECURSE_DOWN);
            mTree->arrangeAll();
        }
        return;
    }
    if (!item)
    {
        return;
    }
    if (action == "reveal")
    {
        onJumpToSource();
    }
    else if (action == "copy_path")
    {
        const std::string text = ALXUISelection::toString(item->getPath());
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
    else if (action == "copy_getchild")
    {
        std::string type = item->getView()->viewType()->mName;
        const std::string text = "getChild<" + type + ">(\"" + item->getName() + "\")";
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
    else if (action == "toggle_visible")
    {
        item->toggleShown();
    }
    else if (action == "move_up" || action == "move_down" || action == "cut"
          || action == "paste" || action == "delete")
    {
        restructure(action, item->getPath());
    }
}

// The four things an editor does to the shape of a file: order among
// siblings, take an element somewhere else, and take it away.
//
// Reparenting is two steps rather than a drag, because the tree is where
// the hierarchy is legible and a drag in it would have to mean three
// things at once -- before, after, or into. Cut names the element; the
// next selection is where it goes.
void ALFloaterXUIStudio::restructure(const std::string& action, const ALXUISelection::path_t& path)
{
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || path.empty())
    {
        setStatus(getString("EditNoSelection"));
        return;
    }

    if (action == "cut")
    {
        mCutPath = path;
        LLStringUtil::format_map_t args;
        args["[WHAT]"] = ALXUISelection::toString(path);
        setStatus(getString("EditCut", args));
        return;
    }

    const std::vector<const ALXUICatalog::Layer*> layers =
        mCatalog.layersFor(*entry, mPreviews[PRIMARY].skin, mLanguage);
    if (layers.empty())
    {
        setStatus(getString("EditNoTarget"));
        return;
    }
    ALXUIEdit* held = document(*layers.front());
    if (!held)
    {
        return;
    }

    bool ok = false;
    std::string what;
    if (action == "delete")
    {
        ok = held->removeElement(path);
        what = "removed";
    }
    else if (action == "paste")
    {
        if (mCutPath.empty())
        {
            setStatus(getString("EditNothingCut"));
            return;
        }
        // Into the element the tree has now, which is where the author is
        // pointing; a cut of the very thing pointed at goes nowhere.
        if (mCutPath == path)
        {
            setStatus(getString("EditNothingCut"));
            return;
        }
        ok = held->moveElement(mCutPath, path);
        mCutPath.clear();
        what = "moved";
    }
    else
    {
        // Among the siblings, which is what a menu, a tab container and a
        // layout stack are: the element before or after this one in the
        // file, which the document knows and the built tree does not.
        ALXUISelection::path_t sibling;
        if (!siblingOf(*held, path, action == "move_up", sibling))
        {
            setStatus(getString("EditNoSibling"));
            return;
        }
        ok = action == "move_up" ? held->moveBefore(path, sibling)
                                 : held->moveAfter(path, sibling);
        what = "moved";
    }

    if (!ok)
    {
        setStatus(held->error());
        return;
    }

    LLStringUtil::format_map_t args;
    args["[ATTRS]"] = what;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layers.front()->skin + "/" + layers.front()->language;
    documentChanged(getString("EditWrote", args));
}

// The element before or after this one among its parent's children, in
// the document rather than in the built tree: the file's order is what a
// move changes, and a widget may build children of its own that no
// element describes.
bool ALFloaterXUIStudio::siblingOf(const ALXUIEdit& document, const ALXUISelection::path_t& path,
                                   bool before, ALXUISelection::path_t& out) const
{
    pugi::xml_node node = document.resolve(path);
    pugi::xml_node found = before ? node.previous_sibling() : node.next_sibling();
    while (found && found.type() != pugi::node_element)
    {
        found = before ? found.previous_sibling() : found.next_sibling();
    }
    if (!found)
    {
        return false;
    }
    out = ALXUICatalog::namePath(found, /*any_tag=*/true);
    return !out.empty();
}

// The preview as it stands on screen, cropped out of a snapshot of the
// window with the UI drawn. The preview is brought to the front first,
// since what is over it is what would be captured.
void ALFloaterXUIStudio::capturePreview()
{
    LLFloater* host = mPreviews[PRIMARY].host.get();
    if (!host)
    {
        setStatus(getString("NoFile"));
        return;
    }
    host->setFrontmost(false);

    const S32 window_width = gViewerWindow->getWindowWidthRaw();
    const S32 window_height = gViewerWindow->getWindowHeightRaw();
    LLPointer<LLImageRaw> shot = new LLImageRaw;
    if (!gViewerWindow->rawSnapshot(shot, window_width, window_height, /*keep_window_aspect=*/true,
                                    /*is_texture=*/false, /*show_ui=*/true, /*show_hud=*/false))
    {
        setStatus("The window would not give a snapshot.");
        return;
    }

    // The floater's rect in the window, in the snapshot's own scale: a
    // snapshot may come back at a different size than the window.
    const LLRect screen = host->calcScreenRect();
    const F32 scale_x = (F32)shot->getWidth() / (F32)llmax(1, gViewerWindow->getWindowWidthScaled());
    const F32 scale_y = (F32)shot->getHeight() / (F32)llmax(1, gViewerWindow->getWindowHeightScaled());
    const S32 left = llclamp((S32)(screen.mLeft * scale_x), 0, shot->getWidth());
    const S32 right = llclamp((S32)(screen.mRight * scale_x), left, shot->getWidth());
    const S32 bottom = llclamp((S32)(screen.mBottom * scale_y), 0, shot->getHeight());
    const S32 top = llclamp((S32)(screen.mTop * scale_y), bottom, shot->getHeight());
    const S32 width = right - left;
    const S32 height = top - bottom;
    if (width <= 0 || height <= 0)
    {
        setStatus("The preview is off screen.");
        return;
    }

    // Row zero of the snapshot is the bottom of the window, which is
    // where the rect's bottom is too.
    const U8 components = shot->getComponents();
    LLPointer<LLImageRaw> cropped = new LLImageRaw(width, height, components);
    for (S32 row = 0; row < height; ++row)
    {
        memcpy(cropped->getData() + (size_t)row * width * components,
               shot->getData() + ((size_t)(bottom + row) * shot->getWidth() + left) * components,
               (size_t)width * components);
    }

    mCapture = cropped;

    // The file and the format are one question: the extension the author
    // types is what the image is written as.
    std::string name = mFile;
    for (char& c : name)
    {
        if (c == '/' || c == '\\' || c == '.')
        {
            c = '_';
        }
    }
    name += "_" + mPreviews[PRIMARY].skin + "_" + mPreviews[PRIMARY].language + ".png";
    LLFilePickerReplyThread::startPicker(boost::bind(&ALFloaterXUIStudio::writeCapture, this, _1),
                                         LLFilePicker::FFSAVE_ALL, name);
}

void ALFloaterXUIStudio::writeCapture(const std::vector<std::string>& filenames)
{
    if (filenames.empty() || mCapture.isNull())
    {
        return;
    }
    const std::string path = filenames.front();

    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    LLPointer<LLImageFormatted> image;
    if (extension == "jpg" || extension == "jpeg")
    {
        image = new LLImageJPEG(gSavedSettings.getS32("SnapshotQuality"));
    }
    else if (extension == "bmp")
    {
        image = new LLImageBMP;
    }
    else if (extension == "tga")
    {
        image = new LLImageTGA;
    }
    else if (extension == "j2c" || extension == "jp2")
    {
        image = new LLImageJ2C;
    }
    else
    {
        // Including no extension at all: a picture of a floater is a PNG
        // unless the author says otherwise.
        image = new LLImagePNG;
    }

    if (!image->encode(mCapture, 0.f) || !image->save(path))
    {
        setStatus("Could not write " + path);
        return;
    }
    setStatus("Captured " + std::to_string(mCapture->getWidth()) + " by "
              + std::to_string(mCapture->getHeight()) + " to " + path);
    mCapture = nullptr;
}

// Every file in the catalog, checked a few per frame. The status line
// counts down and the report lands beside the log, which is the form an
// author can read a whole tree's worth of findings in.
void ALFloaterXUIStudio::startLintAll()
{
    if (!mLintQueue.empty())
    {
        mLintQueue.clear();
        setStatus("Lint all: stopped.");
        return;
    }
    mLintReport.clear();
    mLintByRule.clear();
    mLintFiles = 0;
    mLintFindings = 0;
    for (const ALXUICatalog::Entry& entry : mCatalog.entries())
    {
        mLintQueue.push_back(entry.name);
    }
    mLintTotal = (S32)mLintQueue.size();

    // The rule that needs no build, once, before the files are walked.
    for (const ALXUILint::Finding& f : ALXUILint::checkCatalog(mCatalog))
    {
        ++mLintByRule[ALXUILint::ruleName(f.rule)];
        ++mLintFindings;
        mLintReport.push_back(std::string(ALXUILint::severityName(f.severity)) + " " + ALXUILint::ruleName(f.rule)
                              + " " + f.file + ":" + std::to_string(f.line) + " " + f.message);
    }
    setStatus("Lint all: " + std::to_string(mLintTotal) + " files...");
}

void ALFloaterXUIStudio::stepLintAll()
{
    // A budget per frame rather than a count of files: the largest file
    // takes as long as twenty small ones.
    constexpr F32 BUDGET = 0.015f;
    LLTimer timer;
    while (!mLintQueue.empty() && timer.getElapsedTimeF32() < BUDGET)
    {
        const std::string name = mLintQueue.front();
        mLintQueue.pop_front();
        if (const ALXUICatalog::Entry* entry = mCatalog.find(name))
        {
            ++mLintFiles;
            mLintFindings += lintOneFile(*entry, mLintReport);
        }
    }

    if (mLintQueue.empty())
    {
        finishLintAll();
    }
    else
    {
        setStatus("Lint all: " + std::to_string(mLintTotal - (S32)mLintQueue.size()) + " of "
                  + std::to_string(mLintTotal) + " files, " + std::to_string(mLintFindings) + " findings...");
    }
}

S32 ALFloaterXUIStudio::lintOneFile(const ALXUICatalog::Entry& entry, std::vector<std::string>& lines)
{
    std::string widget_tag;
    if (entry.kind == ALXUICatalog::Kind::Widget)
    {
        widget_tag = entry.rootTag;
    }
    else if (entry.kind == ALXUICatalog::Kind::Template)
    {
        widget_tag = entry.name.substr(entry.name.rfind('/') + 1);
        widget_tag = widget_tag.substr(0, widget_tag.size() - 4);
    }
    if (!isBuilt(entry.kind) || (!widget_tag.empty() && !isCoreWidgetTag(widget_tag)))
    {
        return 0;
    }

    ALXUIPreviewHost* host = nullptr;
    LLView* root = nullptr;
    LLXMLNodePtr node;
    ALXUIOverlay overlay;
    std::vector<ALXUIDiagnostics::Entry> entries;
    {
        ALXUISkinScope scope(mSkin, mLanguage);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        std::vector<std::string> paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, entry.name);
        if (paths.empty())
        {
            paths.push_back(entry.name);
        }
        if (entry.kind == ALXUICatalog::Kind::Template)
        {
            const std::string xml = "<" + widget_tag + " name=\"" + widget_tag + "\" layout=\"topleft\""
                                  + " left=\"8\" top=\"8\" width=\"200\" height=\"24\"/>";
            LLXMLNode::parseBuffer(xml.data(), xml.size(), node);
        }
        else
        {
            ALXmlLayerMerge::load(paths, node, &overlay);
        }

        if (node.notNull())
        {
            LLFloater::Params p(LLFloater::getDefaultParams());
            p.min_height = p.header_height;
            p.min_width = 10;
            host = new ALXUIPreviewHost(this, SECONDARY, p);
            host->detach();
            root = buildFromNode(entry, host, node);
        }
        entries = sink.entries();
    }

    S32 found = 0;
    if (root)
    {
        ALXUISourceMap map;
        map.build(root, node);

        ALXUILint lint;
        ALXUILint::Input input;
        input.root = root;
        input.sourceMap = &map;
        input.diagnostics = &entries;
        input.overlay = &overlay;
        input.catalog = &mCatalog;
        input.file = entry.name;
        input.callbacksAreDecisive = entry.kind == ALXUICatalog::Kind::Menu;
        std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(entry, mSkin, mLanguage);
        if (!layers.empty())
        {
            input.authored = layers.front()->root();
        }
        lint.run(input);

        for (const ALXUILint::Finding& f : lint.findings())
        {
            ++mLintByRule[ALXUILint::ruleName(f.rule)];
            ++found;
            lines.push_back(std::string(ALXUILint::severityName(f.severity)) + " " + ALXUILint::ruleName(f.rule)
                            + " " + entry.name + ":" + std::to_string(f.line) + " "
                            + ALXUISelection::toString(f.path) + " " + f.what + ": " + f.message);
        }
    }
    if (host)
    {
        host->closeFloater();
    }
    return found;
}

void ALFloaterXUIStudio::finishLintAll()
{
    const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "xui_lint.txt");
    llofstream out(path, std::ios::binary);
    out << mLintFiles << " files built in " << mSkin << "/" << mLanguage << ", "
        << mLintFindings << " findings\n\n";
    for (const auto& [rule, count] : mLintByRule)
    {
        out << "  " << count << "\t" << rule << "\n";
    }
    out << "\n";
    for (const std::string& line : mLintReport)
    {
        out << line << "\n";
    }

    setStatus("Lint all: " + std::to_string(mLintFindings) + " findings over " + std::to_string(mLintFiles)
              + " files; the report is " + path);
    LL_INFOS("XUIStudio") << "lint all: " << mLintFindings << " findings over " << mLintFiles
                        << " files, report at " << path << LL_ENDL;
}

// A list's rows are a table, and where a copied table is going is a bug
// report or a message: it carries a line saying which file and which
// element it is about, a heading naming its columns, and its columns lined
// up. An empty column is left out, since a heading over nothing tells no
// one anything.
namespace
{
    // Widths count characters and not bytes, since these tables carry
    // translated text.
    S32 xui_display_width(const std::string& text)
    {
        S32 count = 0;
        for (const char c : text)
        {
            count += ((U8)c & 0xC0) != 0x80;
        }
        return count;
    }

    // A cell with a newline or a tab in it would break the table it is
    // being written into.
    std::string xui_one_line(std::string text)
    {
        for (char& c : text)
        {
            if (c == '\n' || c == '\r' || c == '\t')
            {
                c = ' ';
            }
        }
        return text;
    }
}

void ALFloaterXUIStudio::watchList(LLScrollListCtrl* list)
{
    list->setRightMouseDownCallback(boost::bind(&ALFloaterXUIStudio::onListRightClick, this, _1, _2, _3, _4));
    mLists.push_back(list);
}

// Control+C over a list copies what the menu's Copy would, rather than the
// comma-separated rows the edit menu would reach.
LLScrollListCtrl* ALFloaterXUIStudio::focusedList() const
{
    for (LLScrollListCtrl* list : mLists)
    {
        if (list->hasFocus())
        {
            return list;
        }
    }
    return nullptr;
}

std::string ALFloaterXUIStudio::listCaption(const LLScrollListCtrl* list) const
{
    if (list == mFileList)
    {
        const std::string filter = mCatalogFilter->getText();
        return filter.empty() ? "XUI files" : "XUI files matching \"" + filter + "\"";
    }
    if (list == mFindResults)
    {
        return "Search for \"" + mFindQuery->getText() + "\" in "
             + utf8str_tolower(mFindField->getSelectedItemLabel()) + ", " + mSkin + "/" + mLanguage;
    }

    const Preview& pv = mPreviews[PRIMARY];
    std::string where = mFile.empty() ? std::string("no file") : mFile;
    if (!pv.skin.empty())
    {
        where += " (" + pv.skin + "/" + pv.language + ")";
    }
    if (list == mFindings)
    {
        return "Findings in " + where;
    }

    std::string what = "Rows";
    if (list == mLayout)                  { what = "Layout"; }
    else if (list == mBindings)           { what = "Bindings"; }
    else if (list == mState)              { what = "State"; }
    else if (list == mSelectionFindings)  { what = "Findings"; }
    if (mSelection.hasSelection())
    {
        what += " of " + ALXUISelection::toString(mSelection.selection());
    }
    return what + " in " + where;
}

std::string ALFloaterXUIStudio::listAsText(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const
{
    const S32 columns = list->getNumColumns();
    if (columns <= 0 || rows.empty())
    {
        return std::string();
    }

    // A column draws no heading when it needs none, which leaves its name
    // to stand for it here.
    std::vector<std::string> heading((size_t)columns);
    for (S32 i = 0; i < columns; ++i)
    {
        const LLScrollListColumn* column = list->getColumn(i);
        if (!column)
        {
            continue;
        }
        heading[i] = column->mLabel.getString();
        if (heading[i].empty())
        {
            heading[i] = column->mName;
            if (!heading[i].empty())
            {
                heading[i][0] = (char)toupper((U8)heading[i][0]);
            }
        }
    }

    std::vector<std::vector<std::string>> cells;
    std::vector<bool> used((size_t)columns, false);
    cells.reserve(rows.size());
    for (const LLScrollListItem* item : rows)
    {
        std::vector<std::string> line((size_t)columns);
        for (S32 i = 0; i < columns; ++i)
        {
            const LLScrollListCell* cell = item->getColumn(i);
            if (!cell)
            {
                continue;
            }
            line[i] = xui_one_line(cell->getValue().asString());
            used[i] = used[i] || !line[i].empty();
        }
        cells.push_back(std::move(line));
    }

    S32 last = -1;
    std::vector<S32> width((size_t)columns, 0);
    for (S32 i = 0; i < columns; ++i)
    {
        if (!used[i])
        {
            continue;
        }
        last = i;
        width[i] = xui_display_width(heading[i]);
        for (const std::vector<std::string>& line : cells)
        {
            width[i] = llmax(width[i], xui_display_width(line[i]));
        }
        // One long value -- a tool tip, a translated label -- would push
        // every other row's remaining columns out past reading distance,
        // so it is the one that steps out of line instead.
        width[i] = llmin(width[i], 48);
    }
    if (last < 0)
    {
        return std::string();
    }

    std::string text = listCaption(list);
    if (!text.empty())
    {
        text += "\n\n";
    }
    auto append = [&](const std::vector<std::string>& line)
    {
        for (S32 i = 0; i <= last; ++i)
        {
            if (!used[i])
            {
                continue;
            }
            text += line[i];
            if (i != last)
            {
                text.append((size_t)llmax(0, width[i] - xui_display_width(line[i])) + 2, ' ');
            }
        }
        text += '\n';
    };
    append(heading);
    for (const std::vector<std::string>& line : cells)
    {
        append(line);
    }
    return text;
}

void ALFloaterXUIStudio::copyList(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const
{
    const std::string text = listAsText(list, rows);
    if (!text.empty())
    {
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
}

void ALFloaterXUIStudio::onListRightClick(LLUICtrl* ctrl, S32 x, S32 y, MASK mask)
{
    mMenuList = ctrl ? ctrl->as<LLScrollListCtrl>() : nullptr;
    if (!mMenuList)
    {
        return;
    }

    // The right button does not select, so Copy would have the wrong rows,
    // or none at all on the first click. Take the row under it, unless the
    // click landed inside a selection someone has already made.
    LLScrollListItem* hit = mMenuList->hitItem(x, y);
    if (hit && !hit->getSelected())
    {
        mMenuList->selectItemAt(x, y, MASK_NONE);
    }

    // The one cell under the pointer, for the copy that is meant to be
    // pasted into a line of code and not read.
    mMenuCell.clear();
    if (hit)
    {
        if (const LLScrollListCell* cell = hit->getColumn(mMenuList->getColumnIndexFromOffset(x)))
        {
            mMenuCell = cell->getValue().asString();
        }
    }

    LLContextMenu* menu = static_cast<LLContextMenu*>(mListMenu.get());
    if (!menu)
    {
        // The floater's registrars are its own scope, active while it
        // builds itself and not a moment longer; a menu built later finds
        // the names in them only if that scope is pushed for the build,
        // which is what the hierarchy's own menu does.
        mCommitCallbackRegistrar.pushScope();
        mEnableCallbackRegistrar.pushScope();
        menu = LLUICtrlFactory::getInstance()->createFromFile<LLContextMenu>(
            "menu_xui_studio_list.xml", LLMenuGL::sMenuContainer, LLMenuHolderGL::child_registry_t::instance());
        mEnableCallbackRegistrar.popScope();
        mCommitCallbackRegistrar.popScope();
        if (!menu)
        {
            return;
        }
        mListMenu = menu->getHandle();
    }

    // A context menu places itself; the popup puts it in front and takes
    // the mouse. Both, in that order, as every other list here does.
    menu->show(x, y);
    LLMenuGL::showPopup(mMenuList, menu, x, y);
}

bool ALFloaterXUIStudio::onListActionEnabled(const LLSD& param)
{
    if (!mMenuList)
    {
        return false;
    }
    const std::string action = param.asString();
    if (action == "copy_cell")
    {
        return !mMenuCell.empty();
    }
    if (action == "copy_all" || action == "select_all")
    {
        return mMenuList->getFirstData() != nullptr;
    }
    return mMenuList->getFirstSelected() != nullptr;
}

void ALFloaterXUIStudio::onListAction(const LLSD& param)
{
    if (!mMenuList)
    {
        return;
    }
    const std::string action = param.asString();
    if (action == "copy_cell")
    {
        LLClipboard::instance().copyToClipboard(mMenuCell, 0, (S32)mMenuCell.size());
    }
    else if (action == "copy")
    {
        copyList(mMenuList, mMenuList->getAllSelected());
    }
    else if (action == "copy_all")
    {
        copyList(mMenuList, mMenuList->getAllData());
    }
    else if (action == "select_all")
    {
        mMenuList->selectAll();
    }
}

// ---------------------------------------------------------------------------
// The translation table
// ---------------------------------------------------------------------------
const ALXUICatalog::Layer* ALFloaterXUIStudio::overlayLayer(const ALXUICatalog::Entry& entry,
                                                          const std::string& language) const
{
    // The language's own file in the chosen skin, or in the default skin,
    // which is the order the merge reads them in.
    if (const ALXUICatalog::Layer* layer = entry.layer(mSkin, language))
    {
        return layer;
    }
    return entry.layer("default", language);
}

// Where a translation for this language goes when the language has no
// file for it yet: beside the base file, under the language's directory,
// with a root the merge will match.
bool ALFloaterXUIStudio::overlayPath(const ALXUICatalog::Entry& entry, const std::string& language,
                                   std::string& path, bool& created, std::string& error) const
{
    created = false;
    if (const ALXUICatalog::Layer* layer = overlayLayer(entry, language))
    {
        path = layer->path;
        return true;
    }
    created = true;

    const ALXUICatalog::Layer* base = entry.layer(mSkin, "en");
    if (!base)
    {
        base = entry.layer("default", "en");
    }
    if (!base)
    {
        error = "there is no base file to translate";
        return false;
    }

    // <skins>/<skin>/xui/<lang>/<name>, which is the base's path with the
    // language directory changed.
    const std::string delim = gDirUtilp->getDirDelimiter();
    const size_t file_at = base->path.find_last_of("/\\");
    if (file_at == std::string::npos)
    {
        error = "the base file is in no directory";
        return false;
    }
    std::string dir = base->path.substr(0, file_at);
    const std::string tail = base->path.substr(file_at + 1);
    std::string prefix;
    if (const size_t widgets_at = dir.find_last_of("/\\"); widgets_at != std::string::npos
        && dir.substr(widgets_at + 1) == "widgets")
    {
        prefix = "widgets";
        dir = dir.substr(0, widgets_at);
    }
    const size_t lang_at = dir.find_last_of("/\\");
    if (lang_at == std::string::npos)
    {
        error = "the base file is in no language directory";
        return false;
    }
    dir = dir.substr(0, lang_at) + delim + language;
    if (!prefix.empty())
    {
        dir += delim + prefix;
    }
    if (LLFile::mkdir(dir) != 0 && !gDirUtilp->fileExists(dir))
    {
        error = "could not make " + dir;
        return false;
    }

    // A file with nothing in it but the root the base names, which is
    // what the merge matches the whole file on.
    const pugi::xml_node root = base->root();
    const std::string text = "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\" ?>\n<"
                           + std::string(root.name()) + " name=\"" + root.attribute("name").as_string()
                           + "\">\n</" + std::string(root.name()) + ">\n";
    path = dir + delim + tail;
    return ALXUIEdit::writeFile(path, text, error);
}

// One row per unit: where it is, which field it is, the English, the
// language's own, what the merge does with it, and whether it fits in the
// second preview, which is the language this table is about.
void ALFloaterXUIStudio::fillTranslation()
{
    mTranslateList->deleteAllItems();
    mTranslateValue->setText(LLStringUtil::null);
    mTranslate.clear();

    const std::string language = mTranslateLanguage->getValue().asString();
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || language.empty())
    {
        mTranslateCounts->setText(getString("TranslateNoFile"));
        return;
    }
    if (language == mLanguage)
    {
        mTranslateCounts->setText(getString("TranslateSameLanguage"));
        return;
    }

    std::vector<const ALXUICatalog::Layer*> base_layers = mCatalog.layersFor(*entry, mSkin, mLanguage);
    if (base_layers.empty())
    {
        mTranslateCounts->setText(getString("TranslateNoFile"));
        return;
    }
    const ALXUICatalog::Layer* overlay = overlayLayer(*entry, language);
    mTranslate.scan(base_layers.front()->root(), overlay ? overlay->root() : pugi::xml_node());

    // What does not fit is known from the second preview, when it is the
    // language this table is about.
    const Preview& second = mPreviews[SECONDARY];
    const bool measured = mShowSecondary && second.root && second.language == language;

    S32 index = 0;
    for (const ALXUITranslate::Unit& unit : mTranslate.units())
    {
        std::string where = ALXUISelection::toString(unit.path);
        if (where.empty())
        {
            where = entry->rootTag;
        }
        std::string state;
        switch (unit.state)
        {
        case ALXUITranslate::State::Translated:   state = "translated"; break;
        case ALXUITranslate::State::Missing:      state = "missing"; break;
        case ALXUITranslate::State::Placeholders: state = "placeholders differ"; break;
        case ALXUITranslate::State::Forbidden:    state = "translate=\"false\""; break;
        case ALXUITranslate::State::NotApplied:
            switch (unit.miss)
            {
            case ALXUITranslate::Miss::Moved:           state = "applies to nothing: moved"; break;
            case ALXUITranslate::Miss::Absent:          state = "applies to nothing: absent"; break;
            case ALXUITranslate::Miss::Unnamed:         state = "applies to nothing: unnamed"; break;
            case ALXUITranslate::Miss::Ambiguous:       state = "applies to nothing: ambiguous"; break;
            case ALXUITranslate::Miss::AttributeAbsent: state = "applies to nothing: no such field"; break;
            default:                                    state = "applies to nothing"; break;
            }
            break;
        }
        std::string fits;
        if (measured && unit.applies())
        {
            fits = "yes";
            for (const ALXUILint::Finding& f : second.lint.findings())
            {
                if (f.rule == ALXUILint::Rule::Truncation && f.path == unit.path)
                {
                    fits = "no";
                    break;
                }
            }
        }
        mTranslateList->addElement(row(index++, {
            { "path", where },
            { "field", unit.field.empty() ? std::string("text") : unit.field },
            { "english", unit.english },
            { "translation", unit.translation },
            { "state", state },
            { "fits", fits } }));
    }

    LLStringUtil::format_map_t args;
    args["[TRANSLATED]"] = std::to_string(mTranslate.count(ALXUITranslate::State::Translated));
    args["[MISSING]"] = std::to_string(mTranslate.count(ALXUITranslate::State::Missing));
    args["[NOTAPPLIED]"] = std::to_string(mTranslate.count(ALXUITranslate::State::NotApplied));
    args["[PLACEHOLDERS]"] = std::to_string(mTranslate.count(ALXUITranslate::State::Placeholders));
    args["[FORBIDDEN]"] = std::to_string(mTranslate.count(ALXUITranslate::State::Forbidden));
    mTranslateCounts->setText(getString("TranslateCounts", args));
}

void ALFloaterXUIStudio::onTranslationSelected()
{
    LLScrollListItem* item = mTranslateList->getFirstSelected();
    if (!item)
    {
        return;
    }
    const S32 index = item->getValue().asInteger();
    if (index < 0 || index >= (S32)mTranslate.units().size())
    {
        return;
    }
    const ALXUITranslate::Unit& unit = mTranslate.units()[index];
    mTranslateValue->setText(unit.translation.empty() ? unit.english : unit.translation);
    if (!unit.path.empty())
    {
        mSelection.select(unit.path);
    }
}

void ALFloaterXUIStudio::onTranslationWrite()
{
    LLScrollListItem* item = mTranslateList->getFirstSelected();
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!item || !entry)
    {
        return;
    }
    const S32 index = item->getValue().asInteger();
    if (index < 0 || index >= (S32)mTranslate.units().size())
    {
        return;
    }
    const ALXUITranslate::Unit unit = mTranslate.units()[index];
    const std::string language = mTranslateLanguage->getValue().asString();

    std::vector<const ALXUICatalog::Layer*> base_layers = mCatalog.layersFor(*entry, mSkin, mLanguage);
    if (base_layers.empty())
    {
        return;
    }

    std::string path;
    std::string error;
    bool created = false;
    if (!overlayPath(*entry, language, path, created, error))
    {
        setStatus(error);
        return;
    }

    // The table is written through: a cell answered is a cell written,
    // and what reads it back is the catalog rather than a document held
    // open. So this one keeps the file it replaced, and undo puts that
    // back, where an edit to the document under preview is undone in
    // memory and never reached the disk at all.
    ALXUIEdit overlay;
    if (!overlay.loadFile(path))
    {
        setStatus(overlay.error());
        return;
    }
    const std::string before = overlay.text();
    if (!ALXUITranslate::write(overlay, base_layers.front()->root(), unit, mTranslateValue->getText(), error))
    {
        setStatus(error);
        return;
    }
    if (!overlay.save())
    {
        setStatus(overlay.error());
        return;
    }

    mWroteThroughPath = path;
    mWroteThroughText = before;
    LLStringUtil::format_map_t args;
    args["[FIELD]"] = unit.field.empty() ? std::string("the text") : unit.field;
    args["[FILE]"] = language + "/" + mFile;
    mPendingStatus = getString("TranslateWrote", args);
    setStatus(mPendingStatus);
    if (created)
    {
        // A file that did not exist a moment ago is not in the catalog,
        // and reloading an entry re-reads the layers it already knows:
        // the second write into a new language would create the file
        // again over the first.
        scanCatalog();
    }
    else
    {
        mCatalog.reload(mFile);
    }
    fillTranslation();
    // The second preview is this language, so it shows what was written.
    if (mShowSecondary && mLanguage2 == language)
    {
        mReloadEntryOnly = true;
        mReloadPending = true;
    }
}

// The language this table is about is the one the second preview shows,
// so choosing it here turns that preview on.
void ALFloaterXUIStudio::onTranslationLanguage()
{
    mLanguage2 = mTranslateLanguage->getValue().asString();
    mLanguageCombo2->setValue(mLanguage2);
    if (!mShowSecondary && mLanguage2 != mLanguage)
    {
        mShowSecondary = true;
        mSecondaryCheck->setValue(true);
        mLanguageCombo2->setEnabled(true);
    }
    saveState();
    showPreviews();
    fillTranslation();
}

// Every value this file writes at a path the base has moved on from,
// moved to where the base has it. Nothing else is touched: the value is
// the language's own, written back where it will be read.
S32 ALFloaterXUIStudio::repairFile(const ALXUICatalog::Entry& entry, const std::string& language, std::string& error)
{
    std::vector<const ALXUICatalog::Layer*> base_layers = mCatalog.layersFor(entry, mSkin, mLanguage);
    const ALXUICatalog::Layer* overlay_layer = overlayLayer(entry, language);
    if (base_layers.empty() || !overlay_layer || !overlay_layer->root())
    {
        return 0;
    }

    ALXUIEdit overlay;
    if (!overlay.loadFile(overlay_layer->path))
    {
        error = overlay.error();
        return 0;
    }
    const S32 done = ALXUITranslate::repair(overlay, base_layers.front()->root(), error);
    if (done && !overlay.save())
    {
        error = overlay.error();
        return 0;
    }
    return done;
}

void ALFloaterXUIStudio::onRepairFile()
{
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    const std::string language = mTranslateLanguage->getValue().asString();
    if (!entry || language.empty() || language == mLanguage)
    {
        return;
    }
    std::string error;
    const S32 moves = repairFile(*entry, language, error);
    LLStringUtil::format_map_t args;
    args["[MOVES]"] = std::to_string(moves);
    args["[FILE]"] = language + "/" + mFile;
    setStatus(moves ? getString("TranslateRepaired", args)
                    : (error.empty() ? getString("TranslateNothingToRepair", args) : error));
    if (moves)
    {
        mCatalog.reload(mFile);
        fillTranslation();
    }
}

// A file whose root carries another name, or none, is repaired by giving
// it the base's -- when the file is this file under that name, which is
// what its own values say.
void ALFloaterXUIStudio::onRepairRoots()
{
    const std::string language = mTranslateLanguage->getValue().asString();
    if (language.empty() || language == mLanguage)
    {
        return;
    }

    S32 named = 0;
    S32 left = 0;
    for (const ALXUICatalog::Entry& entry : mCatalog.entries())
    {
        const ALXUICatalog::Layer* overlay = overlayLayer(entry, language);
        if (!overlay || !overlay->root())
        {
            continue;
        }
        std::vector<const ALXUICatalog::Layer*> base_layers = mCatalog.layersFor(entry, mSkin, mLanguage);
        if (base_layers.empty() || !base_layers.front()->root())
        {
            continue;
        }
        const pugi::xml_node base = base_layers.front()->root();
        const std::string over_root = overlay->root().attribute("name").as_string();
        const std::string base_root = base.attribute("name").as_string();
        if (over_root == base_root)
        {
            continue;
        }

        ALXUITranslate units;
        units.scan(base, overlay->root());
        if (!units.sameFileRenamed(over_root))
        {
            LL_INFOS("XUIStudio") << language << "/" << entry.name << ": left the root \"" << over_root
                                << "\" alone; almost nothing in it names what the base has" << LL_ENDL;
            ++left;
            continue;
        }

        ALXUIEdit edit;
        std::string error;
        if (!edit.loadFile(overlay->path) || !edit.setAttribute({}, "name", base_root) || !edit.save())
        {
            setStatus(edit.error());
            return;
        }
        LL_INFOS("XUIStudio") << language << "/" << entry.name << ": root \"" << over_root
                            << "\" -> \"" << base_root << "\"" << LL_ENDL;
        ++named;
    }

    LLStringUtil::format_map_t args;
    args["[FILES]"] = std::to_string(named);
    args["[LEFT]"] = std::to_string(left);
    args["[LANG]"] = language;
    setStatus(getString("TranslateRoots", args));
    if (named)
    {
        scanCatalog();
        fillTranslation();
    }
}

// ---------------------------------------------------------------------------
// The census
// ---------------------------------------------------------------------------
// What the merge does with every overlay of every language, counted: the
// same instrument the console check gates on, run from here over the
// catalog the tool already holds.
void ALFloaterXUIStudio::startCensus()
{
    mCensusQueue.clear();
    mCensus.clear();
    mCensusFiles = 0;
    for (const ALXUICatalog::Entry& entry : mCatalog.entries())
    {
        mCensusQueue.push_back(entry.name);
    }
    setStatus(getString("CensusStarted"));
}

void ALFloaterXUIStudio::stepCensus()
{
    LLTimer timer;
    while (!mCensusQueue.empty() && timer.getElapsedTimeF32() < 0.015f)
    {
        const std::string name = mCensusQueue.front();
        mCensusQueue.pop_front();
        const ALXUICatalog::Entry* entry = mCatalog.find(name);
        if (!entry)
        {
            continue;
        }
        for (const std::string& language : mCatalog.languages())
        {
            if (language == mLanguage)
            {
                continue;
            }
            const ALXUICatalog::Layer* overlay = overlayLayer(*entry, language);
            if (!overlay || !overlay->root())
            {
                continue;
            }
            std::vector<const ALXUICatalog::Layer*> base_layers = mCatalog.layersFor(*entry, mSkin, mLanguage);
            if (base_layers.empty() || !base_layers.front()->root())
            {
                ++mCensus[language]["orphan_file"];
                continue;
            }
            const pugi::xml_node base = base_layers.front()->root();

            std::map<std::string, S32>& counts = mCensus[language];
            ++counts["files"];
            if (std::string_view(overlay->root().attribute("name").as_string())
                != std::string_view(base.attribute("name").as_string()))
            {
                ++counts["root_name_differs"];
            }

            ALXUITranslate units;
            units.scan(base, overlay->root());
            for (const ALXUITranslate::Unit& unit : units.units())
            {
                switch (unit.state)
                {
                case ALXUITranslate::State::Translated:   ++counts["covered"]; break;
                case ALXUITranslate::State::Missing:      ++counts["missing"]; break;
                case ALXUITranslate::State::Placeholders: ++counts["placeholders_differ"]; break;
                case ALXUITranslate::State::Forbidden:    ++counts["translated_despite_false"]; break;
                case ALXUITranslate::State::NotApplied:
                    ++counts["applies_to_nothing"];
                    switch (unit.miss)
                    {
                    case ALXUITranslate::Miss::Moved:           ++counts["moved"]; break;
                    case ALXUITranslate::Miss::Absent:          ++counts["absent"]; break;
                    case ALXUITranslate::Miss::Unnamed:         ++counts["unnamed"]; break;
                    case ALXUITranslate::Miss::Ambiguous:       ++counts["ambiguous"]; break;
                    case ALXUITranslate::Miss::AttributeAbsent: ++counts["field_absent"]; break;
                    default: break;
                    }
                    break;
                }
            }
        }
        ++mCensusFiles;
    }

    LLStringUtil::format_map_t args;
    args["[DONE]"] = std::to_string(mCensusFiles);
    args["[TOTAL]"] = std::to_string(mCensusFiles + (S32)mCensusQueue.size());
    setStatus(getString("CensusProgress", args));
    if (mCensusQueue.empty())
    {
        finishCensus();
    }
}

void ALFloaterXUIStudio::finishCensus()
{
    // The columns every language has a number for, in the order they read
    // best: what arrived, what did not, and why not.
    static const char* COLUMNS[] = { "files", "covered", "missing", "applies_to_nothing",
                                     "moved", "absent", "unnamed", "ambiguous", "field_absent",
                                     "placeholders_differ", "translated_despite_false",
                                     "root_name_differs", "orphan_file" };
    std::vector<std::string> lines;
    std::string header = "language   ";
    for (const char* column : COLUMNS)
    {
        header += " " + std::string(column);
    }
    lines.push_back(header);

    std::map<std::string, S32> totals;
    for (const std::string& language : mCatalog.languages())
    {
        const auto it = mCensus.find(language);
        if (it == mCensus.end())
        {
            continue;
        }
        std::string line = language;
        line.resize(11, ' ');
        for (const char* column : COLUMNS)
        {
            const auto found = it->second.find(column);
            const S32 value = found == it->second.end() ? 0 : found->second;
            totals[column] += value;
            line += " " + std::to_string(value);
        }
        lines.push_back(line);
    }
    std::string all = "all        ";
    for (const char* column : COLUMNS)
    {
        all += " " + std::to_string(totals[column]);
    }
    lines.push_back(all);

    const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "xui_census.txt");
    llofstream out(path);
    for (const std::string& line : lines)
    {
        out << line << "\n";
        LL_INFOS("XUIStudio") << line << LL_ENDL;
    }
    out.close();

    LLStringUtil::format_map_t args;
    args["[COVERED]"] = std::to_string(totals["covered"]);
    args["[NOTHING]"] = std::to_string(totals["applies_to_nothing"]);
    args["[FILE]"] = path;
    setStatus(getString("CensusDone", args));
}

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------
// Every widget the viewer registers, and what a file may write under it.
// The viewer is the only place the whole vocabulary exists: llui's console
// utility can only see the widgets llui itself registers, and the rest are
// registered by static registrars in the viewer's own translation units.
void ALFloaterXUIStudio::onExportSchema()
{
    const ALXUISchema& schema = ALXUISchema::get();
    const std::string path = gDirUtilp->getSkinBaseDir() + gDirUtilp->getDirDelimiter() + "xui.xsd";

    llofstream out(path);
    LLStringUtil::format_map_t args;
    args["[FILE]"] = path;
    if (!out.is_open())
    {
        setStatus(getString("SchemaFailed", args));
        return;
    }
    out << schema.asXSD();
    out.close();

    args["[SUMMARY]"] = schema.summary();
    setStatus(getString("SchemaWritten", args));
}

// The same over every file the language has, a few per frame so the
// viewer keeps drawing.
void ALFloaterXUIStudio::startRepairAll()
{
    const std::string language = mTranslateLanguage->getValue().asString();
    if (language.empty() || language == mLanguage)
    {
        return;
    }
    mRepairQueue.clear();
    mRepairFiles = 0;
    mRepairMoves = 0;
    for (const ALXUICatalog::Entry& entry : mCatalog.entries())
    {
        if (overlayLayer(entry, language))
        {
            mRepairQueue.push_back(entry.name);
        }
    }
}

void ALFloaterXUIStudio::stepRepairAll()
{
    const std::string language = mTranslateLanguage->getValue().asString();
    LLTimer timer;
    while (!mRepairQueue.empty() && timer.getElapsedTimeF32() < 0.015f)
    {
        const std::string name = mRepairQueue.front();
        mRepairQueue.pop_front();
        if (const ALXUICatalog::Entry* entry = mCatalog.find(name))
        {
            std::string error;
            const S32 moves = repairFile(*entry, language, error);
            if (moves)
            {
                ++mRepairFiles;
                mRepairMoves += moves;
                mCatalog.reload(name);
            }
        }
    }

    LLStringUtil::format_map_t args;
    args["[MOVES]"] = std::to_string(mRepairMoves);
    args["[FILES]"] = std::to_string(mRepairFiles);
    args["[LANG]"] = language;
    setStatus(getString("TranslateRepairedAll", args));
    if (mRepairQueue.empty())
    {
        LL_INFOS("XUIStudio") << "repair " << language << ": " << mRepairMoves << " values moved into place across "
                            << mRepairFiles << " files" << LL_ENDL;
        fillTranslation();
    }
}

void ALFloaterXUIStudio::runLint()
{
    Preview& pv = mPreviews[PRIMARY];
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    ALXUILint::Input input;
    input.root = pv.root;
    input.sourceMap = &pv.sourceMap;
    input.diagnostics = &pv.diagnostics;
    input.overlay = &pv.overlay;
    input.catalog = &mCatalog;
    input.file = mFile;
    // A menu file's functions are registered at startup, so a name no
    // registry knows is decisive there and nowhere else in a shell build.
    input.callbacksAreDecisive = entry && entry->kind == ALXUICatalog::Kind::Menu;
    if (entry)
    {
        // The file as written, which is where the parameter elements the
        // parser consumed still are.
        std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(*entry, pv.skin, pv.language);
        if (!layers.empty())
        {
            input.authored = layers.front()->root();
        }
    }
    pv.lint.run(input);
}

// One row per finding: the severity and rule, where it is, and what it
// says. The path is what a double-click selects by, since a finding from
// a layer carries that layer's line and not the base's.
void ALFloaterXUIStudio::fillFindings()
{
    mFindings->deleteAllItems();
    const Preview& pv = mPreviews[PRIMARY];
    for (const ALXUILint::Finding& f : pv.lint.findings())
    {
        std::string where = ALXUISelection::toString(f.path);
        std::string file = f.file;
        const size_t slash = file.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            file = file.substr(slash + 1);
        }
        if (where.empty())
        {
            where = f.what;
        }
        if (!file.empty() && file != mFile)
        {
            where = file + ": " + where;
        }
        LLSD id;
        id["path"] = ALXUISelection::toString(f.path);
        id["line"] = f.line;
        mFindings->addElement(row(id, {
            { "severity", ALXUILint::severityName(f.severity) },
            { "rule", ALXUILint::ruleName(f.rule) },
            { "line", f.line > 0 ? std::to_string(f.line) : std::string() },
            { "where", where },
            { "message", f.what.empty() ? f.message : f.what + ": " + f.message } }));
    }
}

void ALFloaterXUIStudio::onFindingSelected()
{
    LLScrollListItem* item = mFindings->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD id = item->getValue();
    const Preview& pv = mPreviews[PRIMARY];
    const std::string path = id["path"].asString();
    if (!path.empty())
    {
        mSelection.select(ALXUISelection::fromString(path));
        return;
    }
    const LLView* view = pv.sourceMap.viewAtLine(id["line"].asInteger());
    ALXUISelection::path_t found;
    if (view && ALXUISelection::pathOf(view, pv.root, found))
    {
        mSelection.select(found);
    }
}

// The findings on the selected element and everything below it.
void ALFloaterXUIStudio::refreshSelectionFindings()
{
    mSelectionFindings->deleteAllItems();
    if (!mSelection.hasSelection())
    {
        return;
    }
    const ALXUISelection::path_t& selected = mSelection.selection();
    for (const ALXUILint::Finding& f : mPreviews[PRIMARY].lint.findings())
    {
        if (f.path.size() < selected.size()
            || !std::equal(selected.begin(), selected.end(), f.path.begin()))
        {
            continue;
        }
        const bool here = f.path.size() == selected.size();
        mSelectionFindings->addElement(row(ALXUISelection::toString(f.path), {
            { "severity", ALXUILint::severityName(f.severity) },
            { "rule", ALXUILint::ruleName(f.rule) },
            { "where", here ? std::string("here")
                            : ALXUISelection::toString(ALXUISelection::path_t(f.path.begin() + selected.size(), f.path.end())) },
            { "message", f.what.empty() ? f.message : f.what + ": " + f.message } }));
    }
}

void ALFloaterXUIStudio::refreshBreadcrumb()
{
    mBreadcrumb->deleteAllChildren();
    const Preview& pv = mPreviews[PRIMARY];
    if (!pv.root || !mSelection.hasSelection())
    {
        return;
    }

    // One crumb per ancestor from the root down, then the layer the
    // element came from. The selection's end matters more than its
    // start, so when the chain is wider than the panel the first crumbs
    // fold into one that selects the last of them.
    const ALXUISelection::path_t& path = mSelection.selection();
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const ALXUICatalog::Layer* layer = nullptr;
    authoredElement(layer);
    const std::string layer_text = layer ? layer->skin + "/" + layer->language : std::string("code-built");
    const S32 layer_width = font->getWidth(layer_text) + 12;
    const S32 available = mBreadcrumb->getRect().getWidth() - layer_width;

    std::vector<std::string> labels;
    std::vector<S32> widths;
    S32 total = 0;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        labels.push_back(i == 0 ? pv.root->getName() : path[i - 1]);
        widths.push_back(font->getWidth(labels.back()) + 12);
        total += widths.back() + 2;
    }
    size_t first = 0;
    const S32 fold_width = font->getWidth("...") + 12 + 2;
    while (first + 1 < labels.size() && total + (first ? fold_width : 0) > available)
    {
        total -= widths[first] + 2;
        ++first;
    }

    S32 x = 0;
    const S32 height = mBreadcrumb->getRect().getHeight();
    auto crumb = [&](const std::string& label, S32 width, size_t depth)
    {
        ALXUISelection::path_t prefix(path.begin(), path.begin() + depth);
        LLButton::Params bp;
        bp.name = "crumb_" + std::to_string(depth);
        bp.label = label;
        bp.rect = LLRect(x, height, x + width, 0);
        bp.font = font;
        bp.tab_stop = false;
        LLButton* button = LLUICtrlFactory::create<LLButton>(bp);
        button->setClickedCallback([this, prefix](LLUICtrl*, const LLSD&) { mSelection.select(prefix); });
        mBreadcrumb->addChild(button);
        x += width + 2;
    };
    if (first > 0)
    {
        crumb("...", fold_width - 2, first - 1);
    }
    for (size_t i = first; i < labels.size(); ++i)
    {
        crumb(labels[i], widths[i], i);
    }

    LLTextBox::Params tp;
    tp.name = "crumb_layer";
    tp.rect = LLRect(x + 6, height - 3, x + 6 + layer_width, 0);
    tp.font = font;
    tp.initial_value = layer_text;
    mBreadcrumb->addChild(LLUICtrlFactory::create<LLTextBox>(tp));
}

// ---------------------------------------------------------------------------
// The selection
// ---------------------------------------------------------------------------
LLView* ALFloaterXUIStudio::selectedView() const
{
    const Preview& pv = mPreviews[PRIMARY];
    if (!pv.root || !mSelection.hasSelection())
    {
        return nullptr;
    }
    return ALXUISelection::resolve(pv.root, mSelection.selection());
}

// The element the selection names, in the most specific layer that has
// it, which is the one whose values the built view shows.
// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

// The layer a move is written into: the most specific one that positions
// the element, since that is the one whose numbers are on screen, and the
// first that has the element at all when none of them positions it, since
// that is where a position has to be written.
const ALXUICatalog::Layer* ALFloaterXUIStudio::editTarget() const
{
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || !mSelection.hasSelection())
    {
        return nullptr;
    }
    const Preview& pv = mPreviews[PRIMARY];
    const ALXUICatalog::Layer* base = nullptr;
    const ALXUICatalog::Layer* target = nullptr;
    for (const ALXUICatalog::Layer* layer : mCatalog.layersFor(*entry, pv.skin, pv.language))
    {
        const pugi::xml_node node = ALXUICatalog::resolve(layer->root(), mSelection.selection());
        if (!node)
        {
            continue;
        }
        if (!base)
        {
            base = layer;
        }
        for (pugi::xml_attribute attribute : node.attributes())
        {
            if (ALXUIEdit::isGeometryAttribute(attribute.name()))
            {
                target = layer;
                break;
            }
        }
    }
    return target ? target : base;
}

void ALFloaterXUIStudio::refreshEditTarget()
{
    if (!mEditTarget)
    {
        return;
    }
    const ALXUICatalog::Layer* layer = editTarget();
    if (!layer)
    {
        mEditTarget->setText(getString("EditNoTarget"));
        return;
    }
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layer->skin + "/" + layer->language;
    mEditTarget->setText(getString("EditTarget", args));
}

// A move or a resize as the movement of the four edges. The near edges
// are the move, since they are what the file positions from, and what is
// left over is the size.
bool ALFloaterXUIStudio::applyEdges(S32 dl, S32 db, S32 dr, S32 dt)
{
    if (!dl && !db && !dr && !dt)
    {
        return false;
    }
    LLView* view = selectedView();
    if (!view || !view->getParent())
    {
        setStatus(getString("EditNoSelection"));
        return false;
    }
    const ALXUICatalog::Layer* layer = editTarget();
    if (!layer)
    {
        setStatus(getString("EditNoTarget"));
        return false;
    }

    const LLRect& rect = view->getRect();
    ALXUIEdit::Anchor now;
    now.left = rect.mLeft;
    now.top = view->getParent()->getRect().getHeight() - rect.mTop;
    now.bottom = rect.mBottom;
    now.width = rect.getWidth();
    now.height = rect.getHeight();
    now.topLeft = view->isLayoutTopLeft();

    ALXUIEdit* held = document(*layer);
    if (!held)
    {
        return false;
    }
    ALXUIEdit& edit = *held;

    const ALXUISelection::path_t& path = mSelection.selection();
    S32 dx = dl;
    S32 dy = now.topLeft ? dt : db;
    const S32 dw = dr - dl;
    const S32 dh = dt - db;

    // The root sits where the tool put it: a floater preview is placed
    // beside this window and a panel preview is placed in its host, so
    // the numbers a move would write are the tool's and not the file's.
    // Its size is the file's, and that is still editable.
    bool root_move = false;
    if (view == mPreviews[PRIMARY].root && (dx || dy))
    {
        dx = 0;
        dy = 0;
        root_move = true;
        if (!dw && !dh)
        {
            setStatus(getString("EditRootMove"));
            return false;
        }
    }
    std::vector<std::string> written;
    if ((dx || dy) && !edit.translate(path, dx, dy, now))
    {
        setStatus(edit.error());
        return false;
    }
    written = edit.lastWritten();
    if ((dw || dh) && !edit.resize(path, dw, dh, now))
    {
        setStatus(edit.error());
        return false;
    }
    written.insert(written.end(), edit.lastWritten().begin(), edit.lastWritten().end());
    if (written.empty())
    {
        setStatus(edit.error());
        return false;
    }

    std::string names;
    for (const std::string& name : written)
    {
        names += names.empty() ? name : ", " + name;
    }
    LLStringUtil::format_map_t args;
    args["[ATTRS]"] = names;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layer->skin + "/" + layer->language;
    // The rebuild says what it built; this has to come after it. It waits
    // for the next frame: this can be the tail of a mouse-up in the very
    // floater it would take down.
    documentChanged(getString(root_move ? "EditWroteNotMoved" : "EditWrote", args));
    return true;
}

// One step of the document's own stack, which is one operation as it was
// asked for however many splices it took, and nothing on disk to put back
// because nothing went there. The translation table is written through
// rather than held, so its last write is undone the other way: the file it
// replaced, put back.
bool ALFloaterXUIStudio::undoEdit()
{
    if (!mDocumentPath.empty() && mDocument.undo())
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
        documentChanged(getString("EditUndone", args));
        return true;
    }

    if (mWroteThroughPath.empty())
    {
        return false;
    }
    std::string error;
    if (!ALXUIEdit::writeFile(mWroteThroughPath, mWroteThroughText, error))
    {
        setStatus(error);
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mWroteThroughPath.substr(mWroteThroughPath.find_last_of("/\\") + 1);
    mWroteThroughPath.clear();
    mWroteThroughText.clear();
    documentChanged(getString("EditUndone", args));
    return true;
}

bool ALFloaterXUIStudio::redoEdit()
{
    if (mDocumentPath.empty() || !mDocument.redo())
    {
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mDocumentPath.substr(mDocumentPath.find_last_of("/\\") + 1);
    documentChanged(getString("EditRedone", args));
    return true;
}

void ALFloaterXUIStudio::canvasDrag(S32 which, S32 dl, S32 db, S32 dr, S32 dt)
{
    if (which == PRIMARY)
    {
        applyEdges(dl, db, dr, dt);
    }
}

bool ALFloaterXUIStudio::nudge(KEY key, MASK mask)
{
    if (mask & (MASK_CONTROL | MASK_ALT))
    {
        return false;
    }
    const S32 step = (mask & MASK_SHIFT) ? 10 : 1;
    S32 dx = 0;
    S32 dy = 0;
    switch (key)
    {
    case KEY_LEFT:  dx = -step; break;
    case KEY_RIGHT: dx = step;  break;
    case KEY_DOWN:  dy = -step; break;
    case KEY_UP:    dy = step;  break;
    default:        return false;
    }
    return applyEdges(dx, dy, dx, dy);
}

pugi::xml_node ALFloaterXUIStudio::authoredElement(const ALXUICatalog::Layer*& layer) const
{
    layer = nullptr;
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || !mSelection.hasSelection())
    {
        return pugi::xml_node();
    }
    const Preview& pv = mPreviews[PRIMARY];
    std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(*entry, pv.skin, pv.language);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
    {
        if (pugi::xml_node node = ALXUICatalog::resolve((*it)->root(), mSelection.selection()))
        {
            layer = *it;
            return node;
        }
    }
    return pugi::xml_node();
}

void ALFloaterXUIStudio::onSelectionChanged()
{
    if (mTree && !mSyncingTree)
    {
        mSyncingTree = true;
        mTree->clearSelection();
        if (mSelection.hasSelection())
        {
            auto it = mRows.find(ALXUISelection::toString(mSelection.selection()));
            if (it != mRows.end())
            {
                mTree->setSelection(it->second, false, false);
                mTree->scrollToShowSelection();
            }
        }
        mSyncingTree = false;
    }
    refreshBreadcrumb();
    refreshEditTarget();
    refreshInspectors();
}

void ALFloaterXUIStudio::onHoverChanged()
{
    mModel.setCanvasHover(mSelection.hasHover() ? mModel.itemFor(mSelection.hover()) : nullptr);
}

// ---------------------------------------------------------------------------
// The inspectors
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::refreshInspectors()
{
    LLView* view = selectedView();
    LLPanel* current = mInspectors->getCurrentPanel();
    const std::string tab = current ? current->getName() : std::string();
    if (tab == "attributes_tab")
    {
        refreshAttributes(view);
    }
    else if (tab == "layout_tab")
    {
        refreshLayout(view);
    }
    else if (tab == "source_tab")
    {
        refreshSource(view);
    }
    else if (tab == "bindings_tab")
    {
        refreshBindings(view);
    }
    else if (tab == "state_tab")
    {
        refreshState(view);
    }
    else if (tab == "findings_tab")
    {
        refreshSelectionFindings();
    }
}

// Every field the selected widget answers to, with what is in force and
// which layer put it there. The schema says what the fields are and what
// each one is, so the grid gets a check box for a flag and a list of names
// for an enumeration without either of them knowing what a widget is.
void ALFloaterXUIStudio::refreshAttributes(LLView* view)
{
    mAttributeGrid->clearFields();
    if (!view)
    {
        return;
    }
    const Preview& pv = mPreviews[PRIMARY];
    const ALXUISourceMap::Origin* origin = pv.sourceMap.find(view);
    if (!origin)
    {
        return;
    }

    // What the widget answers to, which is the class that was built and not
    // the tag the file wrote: <panel class="foo"> is foo's parameters.
    const std::string* tag = LLUICtrlFactory::widgetTag(view->viewType());
    const ALXUISchema& schema = ALXUISchema::get();
    const ALXUISchema::Tag* declared = tag ? schema.tag(*tag) : nullptr;

    std::vector<ALPropertyGrid::Field> fields;
    boost::unordered_set<std::string> written;

    const auto describe = [&](ALPropertyGrid::Field& field, const std::string& name)
    {
        if (const ALXUISchema::Attribute* attribute = tag ? schema.attribute(*tag, name) : nullptr)
        {
            field.kind = attribute->value;
            field.values = attribute->values;
            field.type = attribute->type;
        }
        else if (declared)
        {
            field.type = getString("AttributeUnknown");
        }
    };

    // Which layer last wrote each attribute, as the merge's observer
    // recorded it; the base wrote the rest.
    for (const auto& [name_entry, attribute] : origin->node->mAttributes)
    {
        ALPropertyGrid::Field field;
        field.name = name_entry->mString;
        field.value = attribute->getValue();
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(attribute.get());
        field.source = layerLabel(PRIMARY, from ? from->layer : 0);
        field.authored = true;
        describe(field, field.name);
        written.insert(field.name);
        fields.push_back(std::move(field));
    }

    if (origin->node->hasTextContents())
    {
        ALPropertyGrid::Field field;
        field.name = "value";
        field.value = origin->node->getTextContents();
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(origin->node.get());
        field.source = layerLabel(PRIMARY, from ? from->layer : 0);
        field.authored = true;
        field.kind = ALParamType::STRING;
        written.insert(field.name);
        fields.push_back(std::move(field));
    }

    // And everything else the tag takes, for an author looking for the
    // name of a thing rather than changing one they can already see.
    if (declared)
    {
        for (const ALXUISchema::Attribute& attribute : declared->attributes)
        {
            if (written.count(attribute.name))
            {
                continue;
            }
            ALPropertyGrid::Field field;
            field.name = attribute.name;
            field.kind = attribute.value;
            field.values = attribute.values;
            field.type = attribute.type;
            fields.push_back(std::move(field));
        }
    }

    mAttributeGrid->setFields(std::move(fields));
}

// A field committed in the grid is one operation on the document, at the
// element the selection names.
void ALFloaterXUIStudio::onFieldCommit(const std::string& name, const std::string& value)
{
    if (!mSelection.hasSelection())
    {
        setStatus(getString("EditNoSelection"));
        return;
    }
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        return;
    }

    // Where a field is written: the layer that already writes the geometry
    // when there is one, else the file's own base layer, which is where an
    // author working in English means it to go.
    const ALXUICatalog::Layer* layer = editTarget();
    if (!layer)
    {
        const std::vector<const ALXUICatalog::Layer*> layers =
            mCatalog.layersFor(*entry, mPreviews[PRIMARY].skin, mLanguage);
        layer = layers.empty() ? nullptr : layers.front();
    }
    if (!layer)
    {
        setStatus(getString("EditNoTarget"));
        return;
    }

    ALXUIEdit* held = document(*layer);
    if (!held)
    {
        return;
    }
    const bool ok = name == "value"
        ? held->setText(mSelection.selection(), value)
        : held->setAttribute(mSelection.selection(), name, value);
    if (!ok)
    {
        setStatus(held->error());
        return;
    }

    LLStringUtil::format_map_t args;
    args["[ATTRS]"] = name;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layer->skin + "/" + layer->language;
    documentChanged(getString("EditWrote", args));
}

// A layer's skin and language, read off its path: the segments around
// the xui directory.
std::string ALFloaterXUIStudio::layerLabel(S32 which, S32 layer) const
{
    const std::string& path = mPreviews[which].overlay.layerPath(layer);
    if (path.empty())
    {
        return std::string();
    }
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find_first_of("/\\", start);
        segments.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    for (size_t i = 1; i + 1 < segments.size(); ++i)
    {
        if (segments[i] == "xui")
        {
            return segments[i - 1] + "/" + segments[i + 1];
        }
    }
    return path;
}

void ALFloaterXUIStudio::refreshLayout(LLView* view)
{
    mLayout->deleteAllItems();
    if (!view)
    {
        return;
    }
    auto add = [&](const std::string& property, const std::string& value)
    {
        mLayout->addElement(row(property, { { "property", property }, { "value", value } }));
    };

    const LLRect& r = view->getRect();
    const LLView* parent = view->getParent();
    add("size", std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight()));
    if (parent)
    {
        const S32 ph = parent->getRect().getHeight();
        const S32 pw = parent->getRect().getWidth();
        add("left / top (from parent's top-left)", std::to_string(r.mLeft) + " / " + std::to_string(ph - r.mTop));
        add("right / bottom (from parent's top-left)", std::to_string(r.mRight) + " / " + std::to_string(ph - r.mBottom));
        add("gap to parent's right / bottom", std::to_string(pw - r.mRight) + " / " + std::to_string(r.mBottom));
        add("parent", parent->getName() + "  " + std::to_string(pw) + " x " + std::to_string(ph));
    }
    add("rect (parent space, bottom-left origin)", rectText(r));
    add("rect (screen)", rectText(view->calcScreenRect()));
    if (view->getUseBoundingRect() && view->getBoundingRect() != r)
    {
        add("bounding rect", rectText(view->getBoundingRect()));
    }
    add("follows", followsText(view->getFollows()));

    const ALXUICatalog::Layer* layer = nullptr;
    pugi::xml_node element = authoredElement(layer);
    if (element)
    {
        std::string form;
        for (const char* attr : { "left", "left_pad", "left_delta", "right", "top", "top_pad", "top_delta", "bottom", "width", "height" })
        {
            if (pugi::xml_attribute a = element.attribute(attr))
            {
                if (!form.empty())
                {
                    form += "  ";
                }
                form += std::string(attr) + "=\"" + a.value() + "\"";
            }
        }
        add("authored", form.empty() ? std::string("(none: the widget's defaults)") : form);
        add("layout", element.attribute("layout").as_string("(default)"));
        add("follows (authored)", element.attribute("follows").as_string("(none)"));
        if (element.attribute("left_pad") || element.attribute("left_delta")
            || element.attribute("top_pad") || element.attribute("top_delta"))
        {
            // The sibling the pads and deltas are measured from is the
            // widget element before this one.
            pugi::xml_node sibling = element.previous_sibling();
            while (sibling && (sibling.type() != pugi::node_element || !ALXUICatalog::isWidgetTag(sibling.name())))
            {
                sibling = sibling.previous_sibling();
            }
            add("relative to", sibling ? std::string(sibling.attribute("name").as_string("unnamed")) + " <" + sibling.name() + ">"
                                       : std::string("(no widget before it: the parent)"));
        }
    }

    if (const LLFloater* floater = view->as<LLFloater>())
    {
        add("resizable", floater->isResizable() ? "yes" : "no");
        add("min size", std::to_string(floater->getMinWidth()) + " x " + std::to_string(floater->getMinHeight()));
    }
}

void ALFloaterXUIStudio::refreshSource(LLView* view)
{
    mSourceLayers->setText(std::string());
    mSourceText->setText(std::string());
    mSourcePath.clear();
    mSourceLine = 0;
    if (!view)
    {
        return;
    }
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        return;
    }
    const Preview& pv = mPreviews[PRIMARY];

    // Every layer of the file, with the line the element is on in each.
    std::string layers_text;
    std::string text;
    for (const ALXUICatalog::Layer* layer : mCatalog.layersFor(*entry, pv.skin, pv.language))
    {
        pugi::xml_node node = ALXUICatalog::resolve(layer->root(), mSelection.selection());
        const S32 line = ALXUICatalog::lineOf(*layer, node);
        if (!layers_text.empty())
        {
            layers_text += "   ";
        }
        layers_text += layer->skin + "/" + layer->language + ": " + (node ? std::to_string(line) : getString("LayerMissing"));
        if (!node)
        {
            continue;
        }
        const std::string file_text = LLFile::getContents(layer->path);
        text += "--- " + layer->path + ":" + std::to_string(line) + "\n";
        text += numbered(elementTextAt(file_text, line), line);
        text += "\n";
        mSourcePath = layer->path;
        mSourceLine = line;
    }
    mSourceLayers->setText(layers_text);
    mSourceText->setText(text);
}

void ALFloaterXUIStudio::refreshBindings(LLView* view)
{
    mBindings->deleteAllItems();
    if (!view)
    {
        return;
    }
    const ALXUICatalog::Layer* layer = nullptr;
    pugi::xml_node element = authoredElement(layer);
    if (!element)
    {
        return;
    }
    S32 n = 0;
    auto add = [&](const std::string& kind, const std::string& name, const std::string& status)
    {
        mBindings->addElement(row(n++, { { "kind", kind }, { "name", name }, { "status", status } }));
    };

    // A floater's own registrar is a scope pushed around its build and
    // popped after it, so nothing global can be asked about a name it
    // holds. The registrar object outlives the scope, though, and the real
    // floater is the one that filled it: where the preview is that floater,
    // "not global" becomes an answer instead of a shrug.
    LLFloater* real = mPreviews[PRIMARY].host.get();
    if (real && real->as<ALXUIPreviewHost>())
    {
        real = nullptr;
    }

    // Callbacks are child elements with a function attribute.
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling())
    {
        if (child.type() != pugi::node_element)
        {
            continue;
        }
        pugi::xml_attribute function = child.attribute("function");
        if (!function)
        {
            continue;
        }
        std::string kind = child.name();
        const size_t dot = kind.rfind('.');
        if (dot != std::string::npos)
        {
            kind = kind.substr(dot + 1);
        }
        const bool commit = LLUICtrl::CommitCallbackRegistry::instance().getValue(function.value()) != nullptr;
        const bool enable = LLUICtrl::EnableCallbackRegistry::instance().getValue(function.value()) != nullptr;
        const bool own = real
                      && (real->getCommitCallbackRegistrar().getValueFromScope(function.value()) != nullptr
                       || real->getEnableCallbackRegistrar().getValueFromScope(function.value()) != nullptr);
        std::string status = commit  ? "commit registry"
                           : enable  ? "enable registry"
                           : own     ? "the floater's own"
                           : real    ? "registered nowhere"
                                     : "not global";
        std::string name = function.value();
        if (pugi::xml_attribute parameter = child.attribute("parameter"))
        {
            name += "  (" + std::string(parameter.value()) + ")";
        }
        add(kind, name, status);
    }

    auto control = [&](const char* attr)
    {
        if (pugi::xml_attribute a = element.attribute(attr))
        {
            const bool global = gSavedSettings.controlExists(a.value());
            const bool account = gSavedPerAccountSettings.controlExists(a.value());
            add(attr, a.value(), global ? "config" : account ? "account" : "no such control");
        }
    };
    control("control_name");
    control("control");
    control("enabled_control");
    control("disabled_control");
    control("visibility_control");
    control("invisibility_control");

    auto file = [&](const char* attr)
    {
        if (pugi::xml_attribute a = element.attribute(attr))
        {
            add(attr, a.value(), mCatalog.find(a.value()) ? "in the catalog" : "no such file");
        }
    };
    file("menu_filename");
    file("filename");
    if (pugi::xml_attribute a = element.attribute("help_topic"))
    {
        add("help_topic", a.value(), "");
    }
}

void ALFloaterXUIStudio::refreshState(LLView* view)
{
    // Rebuilt on a timer while the tab shows; the scroll position is kept.
    const S32 scroll = mState->getScrollPos();
    mState->deleteAllItems();
    if (!view)
    {
        return;
    }
    auto add = [&](const std::string& property, const std::string& value)
    {
        mState->addElement(row(property, { { "property", property }, { "value", value } }));
    };
    auto yes = [](bool b) { return std::string(b ? "yes" : "no"); };

    add("visible", yes(view->getVisible()));
    add("in visible chain", yes(view->isInVisibleChain()));
    add("enabled", yes(view->getEnabled()));
    S32 mx, my;
    LLUI::getInstance()->getMousePositionLocal(view, &mx, &my);
    add("mouse over", yes(view->pointInView(mx, my)));
    if (LLUICtrl* ctrl = view->as<LLUICtrl>())
    {
        add("focus", yes(ctrl->hasFocus()));
        add("value", ctrl->getValue().asString());
    }
    std::string text;
    bool truncated = false;
    if (const LLTextBox* box = view->as<LLTextBox>())
    {
        text = box->getText();
        const LLFontGL* font = box->getFont();
        truncated = font && !box->getWordWrap() && !text.empty()
            && widestLine(font, text) > box->getRect().getWidth() - 2 * box->getHPad();
    }
    else if (const LLButton* button = view->as<LLButton>())
    {
        text = button->getLabelUnselected();
        const LLFontGL* font = button->getFont();
        truncated = font && !text.empty() && font->getWidth(text) > button->getRect().getWidth() - 8;
    }
    if (!text.empty())
    {
        add("text", text);
        add("truncated", yes(truncated));
    }
    add("tooltip", view->getToolTip());
    add("name", view->getName());
    add("class", view->viewType()->mName);
    mState->setScrollPos(scroll);
}

void ALFloaterXUIStudio::onJumpToSource()
{
    // Worked out again every time. The Source tab's path is where it last
    // looked, and a file chosen since then is the one the button means:
    // keeping the old one opened whatever had been looked at first.
    refreshSource(selectedView());
    if (mSourcePath.empty())
    {
        const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
        if (entry && !entry->layers.empty())
        {
            const ALXUICatalog::Layer* layer = entry->layer("default", "en");
            mSourcePath = (layer ? layer : &entry->layers.front())->path;
            mSourceLine = 1;
        }
    }
    if (!mSourcePath.empty())
    {
        openInEditor(mSourcePath, mSourceLine);
    }
}

void ALFloaterXUIStudio::openInEditor(const std::string& path, S32 line)
{
    if (path.empty())
    {
        return;
    }
    LLExternalEditor editor;
    LLExternalEditor::EErrorCode status = editor.setCommand("LL_XUI_EDITOR");
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        setStatus(status == LLExternalEditor::EC_NOT_SPECIFIED ? getString("ExternalEditorNotSet")
                                                               : LLExternalEditor::getErrorMessage(status));
        return;
    }
    status = editor.run(path, line);
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        setStatus(LLExternalEditor::getErrorMessage(status));
        return;
    }
    setStatus("Opened " + path + ":" + std::to_string(line));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void ALFloaterXUIStudio::setStatus(const std::string& text)
{
    mStatus->setText(text);
}

// Everything the menu bar does, by the name the item carries. The actions
// are the methods the toolbar's own buttons call; the switches are the
// members the drawing and the tree already read, so a menu item is the whole
// of the control rather than a second copy of the state.
void ALFloaterXUIStudio::onMenuAction(const LLSD& param)
{
    const std::string action = param.asString();

    if (action == "show")               { showPreviews(); }
    else if (action == "hide")          { closePreviews(); }
    else if (action == "reload")        { reloadAll(); }
    else if (action == "edit")          { onJumpToSource(); }
    else if (action == "capture")       { capturePreview(); }
    else if (action == "save")          { saveDocument(); }
    else if (action == "save_repair")   { saveAndRepair(); }
    else if (action == "impact")        { reportTranslationImpact(); }
    else if (action == "revert")        { revertDocument(); }
    else if (action == "undo")          { undoEdit(); }
    else if (action == "redo")          { redoEdit(); }
    else if (action == "gallery")       { showGallery(); }
    else if (action == "lint_all")      { startLintAll(); }
    else if (action == "census")        { startCensus(); }
    else if (action == "schema")        { onExportSchema(); }
    else if (action == "repair_roots")  { onRepairRoots(); }
    else if (action == "repair_all")    { startRepairAll(); }
    else if (action == "hover")         { mHoverHighlight = !mHoverHighlight; saveState(); }
    else if (action == "rulers")        { mRulers = !mRulers; saveState(); }
    else if (action == "snap")          { mSnap = !mSnap; saveState(); }
    else if (action == "real_floater")
    {
        mRealFloater = !mRealFloater;
        saveState();
        if (!mFile.empty())
        {
            showPreviews();
        }
    }
    else if (action == "code_built")
    {
        mShowCodeBuilt = !mShowCodeBuilt;
        mModel.getFilter().setShowCodeBuilt(mShowCodeBuilt);
        saveState();
    }
    else if (action.compare(0, 5, "grid:") == 0)
    {
        mGrid = llmax(1, std::atoi(action.c_str() + 5));
        saveState();
    }
}

// Whether an item can be chosen at all: what the document has to say
// about itself, which is the only state the menu asks about.
bool ALFloaterXUIStudio::onMenuEnable(const LLSD& param)
{
    const std::string what = param.asString();

    if (what == "dirty")    { return mDocument.dirty(); }
    if (what == "undo")     { return mDocument.canUndo() || !mWroteThroughPath.empty(); }
    if (what == "redo")     { return mDocument.canRedo(); }
    return true;
}

bool ALFloaterXUIStudio::onMenuCheck(const LLSD& param)
{
    const std::string flag = param.asString();

    if (flag == "hover")        { return mHoverHighlight; }
    if (flag == "rulers")       { return mRulers; }
    if (flag == "snap")         { return mSnap; }
    if (flag == "code_built")   { return mShowCodeBuilt; }
    if (flag == "real_floater") { return mRealFloater; }
    if (flag.compare(0, 5, "grid:") == 0)
    {
        return mGrid == std::atoi(flag.c_str() + 5);
    }
    return false;
}

void ALFloaterXUIStudio::onToggleSecondary()
{
    mShowSecondary = mSecondaryCheck->getValue().asBoolean();
    mLanguageCombo2->setEnabled(mShowSecondary);
    saveState();
    if (!mFile.empty())
    {
        if (mShowSecondary)
        {
            showPreview(SECONDARY);
        }
        else
        {
            closePreview(SECONDARY);
        }
    }
}

void ALFloaterXUIStudio::saveState()
{
    LLSD state;
    state["file"] = mFile;
    state["skin"] = mSkin;
    state["language"] = mLanguage;
    state["language2"] = mLanguage2;
    state["secondary"] = mShowSecondary;
    state["hover"] = mHoverHighlight;
    state["code_built"] = mShowCodeBuilt;
    state["snap"] = mSnap;
    state["rulers"] = mRulers;
    state["real_floater"] = mRealFloater;
    state["grid"] = mGrid;
    if (LLPanel* current = mInspectors ? mInspectors->getCurrentPanel() : nullptr)
    {
        state["tab"] = current->getName();
    }
    gSavedSettings.setLLSD("ALXUIStudioState", state);
}

void ALFloaterXUIStudio::loadState()
{
    const LLSD state = gSavedSettings.getLLSD("ALXUIStudioState");
    if (!state.isMap())
    {
        return;
    }
    mFile = state["file"].asString();
    if (state.has("skin"))
    {
        mSkin = state["skin"].asString();
    }
    if (state.has("language"))
    {
        mLanguage = state["language"].asString();
    }
    if (state.has("language2"))
    {
        mLanguage2 = state["language2"].asString();
    }
    mShowSecondary = state["secondary"].asBoolean();
    mSnap = state["snap"].asBoolean();
    mRulers = state["rulers"].asBoolean();
    mRealFloater = state["real_floater"].asBoolean();
    if (state.has("grid"))
    {
        mGrid = llmax(1, state["grid"].asInteger());
    }
    if (state.has("hover"))
    {
        mHoverHighlight = state["hover"].asBoolean();
    }
    if (state.has("code_built"))
    {
        mShowCodeBuilt = state["code_built"].asBoolean();
    }
    if (state.has("tab") && mInspectors)
    {
        mInspectors->selectTabByName(state["tab"].asString());
    }
}

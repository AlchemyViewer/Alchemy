/**
 * @file alfloaterxuitool.cpp
 * @brief The XUI tool: catalog, preview, hierarchy, inspectors and diagnostics for XUI files.
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

#include "alfloaterxuitool.h"

#include "alxmldocument.h"
#include "alxmllayermerge.h"
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
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltimer.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerwindow.h"
#include "llwindow.h"

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
    ALXUILiveFile(const std::string& path, ALFloaterXUITool* tool)
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
    ALFloaterXUITool*   mTool;
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

    ALXUIPreviewHost(ALFloaterXUITool* tool, S32 which, const LLFloater::Params& p)
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
        if (mTool->snapToGrid())
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
        if (mTool && mWhich == ALFloaterXUITool::PRIMARY)
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
        if (mGrip != GRIP_NONE && hasMouseCapture())
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
        if (mGrip != GRIP_NONE && hasMouseCapture())
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
        return view && view != this && mWhich == ALFloaterXUITool::PRIMARY;
    }

    bool dragging() const
    {
        return mDelta[EDGE_L] || mDelta[EDGE_B] || mDelta[EDGE_R] || mDelta[EDGE_T];
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
        const S32 step = grid < 4 ? grid * 4 : grid;   // a two pixel grid drawn whole is a wash
        for (S32 x = r.mLeft; x <= r.mRight; x += step)
        {
            gl_line_2d(x, r.mBottom, x, r.mTop, faint);
        }
        for (S32 y = r.mTop; y >= r.mBottom; y -= step)
        {
            gl_line_2d(r.mLeft, y, r.mRight, y, faint);
        }
    }

    // Two strips along the top and the left, counting from the previewed
    // root's top left corner, which is where the file counts from. The
    // selection's edges are marked on both.
    void drawRulers() const
    {
        static constexpr S32 RULER = 14;
        const LLRect r = mRoot == this ? getLocalRect() : localRectOf(mRoot);
        if (r.getWidth() <= 0)
        {
            return;
        }
        static const LLUIColor back = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::black);
        static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
        LLColor4 ground(back.get());
        ground.mV[VALPHA] = 0.85f;

        const LLRect top(r.mLeft, r.mTop, r.mRight, r.mTop - RULER);
        const LLRect left(r.mLeft, r.mTop, r.mLeft + RULER, r.mBottom);
        gl_rect_2d(top, ground, true);
        gl_rect_2d(left, ground, true);

        const S32 grid = llmax(mTool->gridSize(), 2);
        const S32 label_every = grid * 10 < 40 ? 50 : grid * 10;
        const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
        for (S32 x = 0; x <= r.getWidth(); x += grid)
        {
            const bool labelled = (x % label_every) == 0;
            gl_line_2d(r.mLeft + x, r.mTop - RULER, r.mLeft + x, r.mTop - (labelled ? RULER + 4 : RULER + 2), ink.get());
            if (labelled && x > 0)
            {
                font->renderUTF8(std::to_string(x), 0, r.mLeft + x + 2, r.mTop - RULER + 2,
                                 ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
            }
        }
        for (S32 y = 0; y <= r.getHeight(); y += grid)
        {
            const bool labelled = (y % label_every) == 0;
            gl_line_2d(r.mLeft + RULER, r.mTop - y, r.mLeft + (labelled ? RULER + 4 : RULER + 2), r.mTop - y, ink.get());
            if (labelled && y > 0)
            {
                font->renderUTF8(std::to_string(y), 0, r.mLeft + 2, r.mTop - y - 10,
                                 ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
            }
        }

        // Where the selection sits, on both rules.
        if (LLView* view = ALXUISelection::resolve(mRoot, mTool->selection().selection()))
        {
            const LLRect box = localRectOf(view);
            gl_rect_2d(LLRect(box.mLeft, r.mTop, box.mRight, r.mTop - RULER), LLColor4::red, false);
            gl_rect_2d(LLRect(r.mLeft, box.mTop, r.mLeft + RULER, box.mBottom), LLColor4::red, false);
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

    ALFloaterXUITool*   mTool;
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
// ALFloaterXUITool
// ===========================================================================
ALFloaterXUITool::ALFloaterXUITool(const LLSD& key)
:   LLFloater(key)
{
    mCommitCallbackRegistrar.add("XUITool.Tree", boost::bind(&ALFloaterXUITool::onTreeAction, this, _2));
    mEnableCallbackRegistrar.add("XUITool.TreeEnabled", boost::bind(&ALFloaterXUITool::onTreeActionEnabled, this, _2));
    mCommitCallbackRegistrar.add("XUITool.List", boost::bind(&ALFloaterXUITool::onListAction, this, _2));
    mEnableCallbackRegistrar.add("XUITool.ListEnabled", boost::bind(&ALFloaterXUITool::onListActionEnabled, this, _2));
}

ALFloaterXUITool::~ALFloaterXUITool()
{
    closePreviews();
}

bool ALFloaterXUITool::postBuild()
{
    mCatalogFilter = getChild<LLFilterEditor>("catalog_filter");
    mFileList = getChild<LLScrollListCtrl>("file_list");
    mSkinCombo = getChild<LLComboBox>("skin_combo");
    mLanguageCombo = getChild<LLComboBox>("language_combo");
    mLanguageCombo2 = getChild<LLComboBox>("language_combo_2");
    mSecondaryCheck = getChild<LLCheckBoxCtrl>("secondary_check");
    mSnapCheck = getChild<LLCheckBoxCtrl>("snap_check");
    mRulersCheck = getChild<LLCheckBoxCtrl>("rulers_check");
    mGridCombo = getChild<LLComboBox>("grid_combo");
    mFindQuery = getChild<LLLineEditor>("find_query");
    mFindField = getChild<LLComboBox>("find_field");
    mFindResults = getChild<LLScrollListCtrl>("find_results");
    mTreeFilter = getChild<LLFilterEditor>("tree_filter");
    mTreePanel = getChild<LLPanel>("tree_host");
    mBreadcrumb = getChild<LLPanel>("breadcrumb");
    mFindings = getChild<LLScrollListCtrl>("findings");
    mInspectors = getChild<LLTabContainer>("inspector_tabs");
    mAttributes = getChild<LLScrollListCtrl>("attributes");
    mLayout = getChild<LLScrollListCtrl>("layout");
    mSourceLayers = getChild<LLTextBox>("source_layers");
    mSourceText = getChild<LLTextEditor>("source_text");
    mBindings = getChild<LLScrollListCtrl>("bindings");
    mState = getChild<LLScrollListCtrl>("state");
    mSelectionFindings = getChild<LLScrollListCtrl>("selection_findings");
    mBottomTabs = getChild<LLTabContainer>("bottom_tabs");
    mTranslateLanguage = getChild<LLComboBox>("translate_language");
    mTranslateList = getChild<LLScrollListCtrl>("translate_list");
    mTranslateValue = getChild<LLLineEditor>("translate_value");
    mTranslateCounts = getChild<LLTextBox>("translate_counts");
    mEditTarget = getChild<LLTextBox>("edit_target");
    mStatus = getChild<LLTextBox>("status");

    loadState();
    scanCatalog();

    mCatalogFilter->setCommitCallback(boost::bind(&ALFloaterXUITool::onCatalogFilter, this));
    mFileList->setCommitCallback(boost::bind(&ALFloaterXUITool::onFileSelected, this));
    mFileList->setCommitOnSelectionChange(true);
    mSkinCombo->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mLanguageCombo->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mLanguageCombo2->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mSecondaryCheck->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleSecondary, this));
    mFindQuery->setCommitCallback(boost::bind(&ALFloaterXUITool::onFind, this));
    mFindField->setCommitCallback(boost::bind(&ALFloaterXUITool::onFind, this));
    mFindResults->setDoubleClickCallback(boost::bind(&ALFloaterXUITool::onFindResult, this));
    mTreeFilter->setCommitCallback(boost::bind(&ALFloaterXUITool::onTreeFilter, this));
    mFindings->setDoubleClickCallback(boost::bind(&ALFloaterXUITool::onFindingSelected, this));

    // Every table in the tool copies the same way.
    for (LLScrollListCtrl* list : { mFileList, mFindResults, mFindings, mAttributes,
                                    mLayout, mBindings, mState, mSelectionFindings, mTranslateList })
    {
        watchList(list);
    }
    mInspectors->setCommitCallback(boost::bind(&ALFloaterXUITool::refreshInspectors, this));
    mBottomTabs->setCommitCallback(boost::bind(&ALFloaterXUITool::fillTranslation, this));
    mTranslateLanguage->setCommitCallback(boost::bind(&ALFloaterXUITool::onTranslationLanguage, this));
    mTranslateList->setCommitOnSelectionChange(true);
    mTranslateList->setCommitCallback(boost::bind(&ALFloaterXUITool::onTranslationSelected, this));
    mTranslateValue->setCommitCallback(boost::bind(&ALFloaterXUITool::onTranslationWrite, this));
    getChild<LLButton>("translate_write")->setClickedCallback(boost::bind(&ALFloaterXUITool::onTranslationWrite, this));
    getChild<LLButton>("translate_repair_file")->setClickedCallback(boost::bind(&ALFloaterXUITool::onRepairFile, this));
    getChild<LLButton>("translate_repair_all")->setClickedCallback(boost::bind(&ALFloaterXUITool::startRepairAll, this));
    getChild<LLButton>("translate_repair_roots")->setClickedCallback(boost::bind(&ALFloaterXUITool::onRepairRoots, this));
    getChild<LLButton>("census_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::startCensus, this));
    getChild<LLButton>("schema_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::onExportSchema, this));

    getChild<LLButton>("show_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::showPreviews, this));
    getChild<LLButton>("hide_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::closePreviews, this));
    getChild<LLButton>("reload_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::reloadAll, this));
    getChild<LLButton>("edit_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::onJumpToSource, this));
    getChild<LLButton>("jump_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::onJumpToSource, this));
    getChild<LLButton>("gallery_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::showGallery, this));
    getChild<LLButton>("lint_all_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::startLintAll, this));
    getChild<LLButton>("capture_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::capturePreview, this));

    LLCheckBoxCtrl* hover = getChild<LLCheckBoxCtrl>("hover_check");
    hover->setValue(mHoverHighlight);
    hover->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleHover, this));
    mSnapCheck->setValue(mSnap);
    mSnapCheck->setCommitCallback(boost::bind(&ALFloaterXUITool::onGridChanged, this));
    mRulersCheck->setValue(mRulers);
    mRulersCheck->setCommitCallback(boost::bind(&ALFloaterXUITool::onGridChanged, this));
    mGridCombo->setValue(mGrid);
    mGridCombo->setCommitCallback(boost::bind(&ALFloaterXUITool::onGridChanged, this));

    LLCheckBoxCtrl* code_built = getChild<LLCheckBoxCtrl>("code_built_check");
    code_built->setValue(mShowCodeBuilt);
    code_built->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleCodeBuilt, this));
    mSecondaryCheck->setValue(mShowSecondary);
    mLanguageCombo2->setEnabled(mShowSecondary);

    mSelection.onSelectionChanged(boost::bind(&ALFloaterXUITool::onSelectionChanged, this));
    mSelection.onHoverChanged(boost::bind(&ALFloaterXUITool::onHoverChanged, this));
    mModel.setHoverHandler(boost::bind(&ALFloaterXUITool::onTreeHover, this, _1));
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

void ALFloaterXUITool::onClose(bool app_quitting)
{
    saveState();
    closePreviews();
}

void ALFloaterXUITool::draw()
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

bool ALFloaterXUITool::handleKeyHere(KEY key, MASK mask)
{
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
    if (key == 'Z' && mask == MASK_CONTROL && undoEdit())
    {
        return true;
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
void ALFloaterXUITool::scanCatalog()
{
    mCatalog.scan(gDirUtilp->getSkinBaseDir());
    fillSkinsAndLanguages();
    fillCatalog();
}

void ALFloaterXUITool::fillSkinsAndLanguages()
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
LLSD ALFloaterXUITool::row(const LLSD& id, std::initializer_list<std::pair<const char*, std::string>> cells)
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

void ALFloaterXUITool::fillCatalog()
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

void ALFloaterXUITool::onCatalogFilter()
{
    fillCatalog();
}

void ALFloaterXUITool::onFileSelected()
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

void ALFloaterXUITool::onSkinOrLanguage()
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

void ALFloaterXUITool::onFind()
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

void ALFloaterXUITool::onFindResult()
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
void ALFloaterXUITool::closePreview(S32 which)
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
        static_cast<ALXUIPreviewHost*>(host)->detach();
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

void ALFloaterXUITool::closePreviews()
{
    for (S32 i = 0; i < PREVIEWS; ++i)
    {
        closePreview(i);
    }
}

void ALFloaterXUITool::hostClosed(S32 which)
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

void ALFloaterXUITool::showPreviews()
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
void ALFloaterXUITool::placeHost(S32 which, LLFloater* host)
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

LLView* ALFloaterXUITool::buildRoot(S32 which, const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr& node)
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
        // layer wrote and what it dropped.
        std::vector<std::string> paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, file);
        if (paths.empty())
        {
            paths.push_back(file);
        }
        if (!ALXmlLayerMerge::load(paths, node, &mPreviews[which].overlay))
        {
            return nullptr;
        }
    }
    return buildFromNode(entry, host, node);
}

// The node as a view, by the kind of file it is. A floater is the host
// itself; everything else is hosted by it.
LLView* ALFloaterXUITool::buildFromNode(const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr node)
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

void ALFloaterXUITool::showPreview(S32 which)
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

    if (!isBuilt(entry->kind) || viewer_widget)
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

    ALXUIPreviewHost* host = nullptr;
    LLView* root = nullptr;
    LLXMLNodePtr node;
    LLTimer timer;
    {
        ALXUISkinScope scope(pv.skin, pv.language);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        LLFloater::Params p(LLFloater::getDefaultParams());
        p.min_height = p.header_height;
        p.min_width = 10;
        host = new ALXUIPreviewHost(this, which, p);
        root = buildRoot(which, *entry, host, node);
        pv.diagnostics = sink.entries();
    }
    pv.seconds = timer.getElapsedTimeF32();

    if (!root)
    {
        host->detach();
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

    host->setRoot(root);
    std::string title = root == host ? host->getTitle() : mFile;
    title += " [" + pv.skin + "/" + pv.language + (which == PRIMARY ? "" : ", second") + "]";
    host->setTitle(title);
    pv.host = host->getHandle();
    pv.root = root;
    pv.node = node;
    pv.views = countViews(root);
    pv.sourceMap.build(root, node);
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
        setStatus(getString("Built", args));
        fillTranslation();
        // The selection is a path; it may name something in the new tree.
        onSelectionChanged();
    }
}

void ALFloaterXUITool::watchFiles(const ALXUICatalog::Entry& entry)
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
void ALFloaterXUITool::fileChanged()
{
    // The check runs from a timer; the rebuild waits for the next frame.
    mReloadEntryOnly = true;
    mReloadFromDisk = true;
    mReloadPending = true;
}

void ALFloaterXUITool::reloadAll()
{
    mReloadEntryOnly = false;
    mReloadFromDisk = true;
    mReloadPending = true;
}

void ALFloaterXUITool::showGallery()
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
void ALFloaterXUITool::canvasHover(S32 which, const LLView* view)
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

void ALFloaterXUITool::canvasSelect(S32 which, const LLView* view)
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
void ALFloaterXUITool::clearTree()
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

void ALFloaterXUITool::rebuildTree()
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
    p.options_menu = "menu_xui_tool_tree.xml";
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
    mTree->setSelectCallback(boost::bind(&ALFloaterXUITool::onTreeSelection, this, _1, _2));
    mModel.setFolderView(mTree);

    createRows(root_item, mTree);
    mTree->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
    mTree->arrangeAll();
    mModel.getFilter().setModified();
}

void ALFloaterXUITool::createRows(ALXUITreeItem* item, LLFolderViewFolder* parent_widget)
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

void ALFloaterXUITool::onTreeFilter()
{
    mModel.getFilter().setFilterSubString(mTreeFilter->getText());
}

void ALFloaterXUITool::onTreeSelection(const std::deque<LLFolderViewItem*>& items, bool user_action)
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

void ALFloaterXUITool::onTreeHover(const ALXUITreeItem* item)
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

ALXUITreeItem* ALFloaterXUITool::selectedItem() const
{
    return mSelection.hasSelection() ? mModel.itemFor(mSelection.selection()) : nullptr;
}

bool ALFloaterXUITool::onTreeActionEnabled(const LLSD& param)
{
    const std::string action = param.asString();
    if (action == "reveal")
    {
        ALXUITreeItem* item = selectedItem();
        return item && item->isFromXML();
    }
    return true;
}

void ALFloaterXUITool::onTreeAction(const LLSD& param)
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
}

// The preview as it stands on screen, cropped out of a snapshot of the
// window with the UI drawn. The preview is brought to the front first,
// since what is over it is what would be captured.
void ALFloaterXUITool::capturePreview()
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
    LLFilePickerReplyThread::startPicker(boost::bind(&ALFloaterXUITool::writeCapture, this, _1),
                                         LLFilePicker::FFSAVE_ALL, name);
}

void ALFloaterXUITool::writeCapture(const std::vector<std::string>& filenames)
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
void ALFloaterXUITool::startLintAll()
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

void ALFloaterXUITool::stepLintAll()
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

S32 ALFloaterXUITool::lintOneFile(const ALXUICatalog::Entry& entry, std::vector<std::string>& lines)
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

void ALFloaterXUITool::finishLintAll()
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
    LL_INFOS("XUITool") << "lint all: " << mLintFindings << " findings over " << mLintFiles
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

void ALFloaterXUITool::watchList(LLScrollListCtrl* list)
{
    list->setRightMouseDownCallback(boost::bind(&ALFloaterXUITool::onListRightClick, this, _1, _2, _3, _4));
    mLists.push_back(list);
}

// Control+C over a list copies what the menu's Copy would, rather than the
// comma-separated rows the edit menu would reach.
LLScrollListCtrl* ALFloaterXUITool::focusedList() const
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

std::string ALFloaterXUITool::listCaption(const LLScrollListCtrl* list) const
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
    if (list == mAttributes)              { what = "Attributes"; }
    else if (list == mLayout)             { what = "Layout"; }
    else if (list == mBindings)           { what = "Bindings"; }
    else if (list == mState)              { what = "State"; }
    else if (list == mSelectionFindings)  { what = "Findings"; }
    if (mSelection.hasSelection())
    {
        what += " of " + ALXUISelection::toString(mSelection.selection());
    }
    return what + " in " + where;
}

std::string ALFloaterXUITool::listAsText(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const
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

void ALFloaterXUITool::copyList(LLScrollListCtrl* list, const std::vector<LLScrollListItem*>& rows) const
{
    const std::string text = listAsText(list, rows);
    if (!text.empty())
    {
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
}

void ALFloaterXUITool::onListRightClick(LLUICtrl* ctrl, S32 x, S32 y, MASK mask)
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
            "menu_xui_tool_list.xml", LLMenuGL::sMenuContainer, LLMenuHolderGL::child_registry_t::instance());
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

bool ALFloaterXUITool::onListActionEnabled(const LLSD& param)
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

void ALFloaterXUITool::onListAction(const LLSD& param)
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
const ALXUICatalog::Layer* ALFloaterXUITool::overlayLayer(const ALXUICatalog::Entry& entry,
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
bool ALFloaterXUITool::overlayPath(const ALXUICatalog::Entry& entry, const std::string& language,
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
void ALFloaterXUITool::fillTranslation()
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

void ALFloaterXUITool::onTranslationSelected()
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

void ALFloaterXUITool::onTranslationWrite()
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

    mUndoPath = path;
    mUndoText = before;
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
void ALFloaterXUITool::onTranslationLanguage()
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
S32 ALFloaterXUITool::repairFile(const ALXUICatalog::Entry& entry, const std::string& language, std::string& error)
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

void ALFloaterXUITool::onRepairFile()
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
void ALFloaterXUITool::onRepairRoots()
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
            LL_INFOS("XUITool") << language << "/" << entry.name << ": left the root \"" << over_root
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
        LL_INFOS("XUITool") << language << "/" << entry.name << ": root \"" << over_root
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
void ALFloaterXUITool::startCensus()
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

void ALFloaterXUITool::stepCensus()
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

void ALFloaterXUITool::finishCensus()
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
        LL_INFOS("XUITool") << line << LL_ENDL;
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
void ALFloaterXUITool::onExportSchema()
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
void ALFloaterXUITool::startRepairAll()
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

void ALFloaterXUITool::stepRepairAll()
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
        LL_INFOS("XUITool") << "repair " << language << ": " << mRepairMoves << " values moved into place across "
                            << mRepairFiles << " files" << LL_ENDL;
        fillTranslation();
    }
}

void ALFloaterXUITool::runLint()
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
void ALFloaterXUITool::fillFindings()
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

void ALFloaterXUITool::onFindingSelected()
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
void ALFloaterXUITool::refreshSelectionFindings()
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

void ALFloaterXUITool::refreshBreadcrumb()
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
LLView* ALFloaterXUITool::selectedView() const
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
const ALXUICatalog::Layer* ALFloaterXUITool::editTarget() const
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

void ALFloaterXUITool::refreshEditTarget()
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
bool ALFloaterXUITool::applyEdges(S32 dl, S32 db, S32 dr, S32 dt)
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

    ALXUIEdit edit;
    if (!edit.loadFile(layer->path))
    {
        setStatus(edit.error());
        return false;
    }
    const std::string before = edit.text();

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
    if (written.empty() || !edit.save())
    {
        setStatus(edit.error());
        return false;
    }

    // One step back, which is the whole file as it was: an edit is one
    // write and a stray drag is one write to undo. The stack arrives with
    // the rest of the operations.
    mUndoPath = layer->path;
    mUndoText = before;

    // The rebuild waits for the next frame: this can be the tail of a
    // mouse-up in the very floater it would take down.
    mReloadEntryOnly = true;
    mReloadPending = true;

    std::string names;
    for (const std::string& name : written)
    {
        names += names.empty() ? name : ", " + name;
    }
    LLStringUtil::format_map_t args;
    args["[ATTRS]"] = names;
    args["[FILE]"] = mFile;
    args["[LAYER]"] = layer->skin + "/" + layer->language;
    // The rebuild says what it built; this has to come after it.
    mPendingStatus = getString(root_move ? "EditWroteNotMoved" : "EditWrote", args);
    setStatus(mPendingStatus);
    return true;
}

// The file as it was before the last write. One step, because one drag or
// one key is one write, and a stray one should cost nothing to take back.
bool ALFloaterXUITool::undoEdit()
{
    if (mUndoPath.empty())
    {
        return false;
    }
    std::string error;
    if (!ALXUIEdit::writeFile(mUndoPath, mUndoText, error))
    {
        setStatus(error);
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[FILE]"] = mUndoPath.substr(mUndoPath.find_last_of("/\\") + 1);
    mPendingStatus = getString("EditUndone", args);
    setStatus(mPendingStatus);
    mUndoPath.clear();
    mUndoText.clear();
    mReloadEntryOnly = true;
    mReloadPending = true;
    return true;
}

void ALFloaterXUITool::canvasDrag(S32 which, S32 dl, S32 db, S32 dr, S32 dt)
{
    if (which == PRIMARY)
    {
        applyEdges(dl, db, dr, dt);
    }
}

bool ALFloaterXUITool::nudge(KEY key, MASK mask)
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

pugi::xml_node ALFloaterXUITool::authoredElement(const ALXUICatalog::Layer*& layer) const
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

void ALFloaterXUITool::onSelectionChanged()
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

void ALFloaterXUITool::onHoverChanged()
{
    mModel.setCanvasHover(mSelection.hasHover() ? mModel.itemFor(mSelection.hover()) : nullptr);
}

// ---------------------------------------------------------------------------
// The inspectors
// ---------------------------------------------------------------------------
void ALFloaterXUITool::refreshInspectors()
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

void ALFloaterXUITool::refreshAttributes(LLView* view)
{
    mAttributes->deleteAllItems();
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

    // Which layer last wrote each attribute, and the line in that layer's
    // file, as the merge's observer recorded it; the base wrote the rest.
    for (const auto& [name_entry, attribute] : origin->node->mAttributes)
    {
        const char* name = name_entry->mString;
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(attribute.get());
        const S32 line = from ? from->line : attribute->getLineNumber();
        const ALXUISchema::Attribute* declared = tag ? schema.attribute(*tag, name) : nullptr;
        mAttributes->addElement(row(name, {
            { "attribute", name },
            { "value", attribute->getValue() },
            { "type", declared ? declared->type
                               : (tag && schema.tag(*tag) ? getString("AttributeUnknown") : std::string()) },
            { "layer", layerLabel(PRIMARY, from ? from->layer : 0) },
            { "line", line > 0 ? std::to_string(line) : std::string() } }));
    }
    if (origin->node->hasTextContents())
    {
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(origin->node.get());
        const S32 line = from ? from->line : origin->line;
        const ALXUISchema::Tag* declared = tag ? schema.tag(*tag) : nullptr;
        mAttributes->addElement(row("text()", {
            { "attribute", "(text)" },
            { "value", origin->node->getTextContents() },
            { "type", declared && !declared->text ? getString("AttributeUnknown") : std::string() },
            { "layer", layerLabel(PRIMARY, from ? from->layer : 0) },
            { "line", line > 0 ? std::to_string(line) : std::string() } }));
    }
}

// A layer's skin and language, read off its path: the segments around
// the xui directory.
std::string ALFloaterXUITool::layerLabel(S32 which, S32 layer) const
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

void ALFloaterXUITool::refreshLayout(LLView* view)
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

void ALFloaterXUITool::refreshSource(LLView* view)
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

void ALFloaterXUITool::refreshBindings(LLView* view)
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

    // Callbacks are child elements with a function attribute; in shell
    // mode only the global registries answer, so a floater's own
    // registrar is reported as such.
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
        std::string status = commit ? "commit registry" : enable ? "enable registry" : "not global";
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

void ALFloaterXUITool::refreshState(LLView* view)
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

void ALFloaterXUITool::onJumpToSource()
{
    if (mSourcePath.empty())
    {
        // The Source tab has not been shown for this selection.
        refreshSource(selectedView());
    }
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

void ALFloaterXUITool::openInEditor(const std::string& path, S32 line)
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
void ALFloaterXUITool::setStatus(const std::string& text)
{
    mStatus->setText(text);
}

void ALFloaterXUITool::onGridChanged()
{
    mSnap = mSnapCheck->getValue().asBoolean();
    mRulers = mRulersCheck->getValue().asBoolean();
    mGrid = llmax(1, mGridCombo->getValue().asInteger());
    saveState();
}

void ALFloaterXUITool::onToggleHover()
{
    mHoverHighlight = getChild<LLCheckBoxCtrl>("hover_check")->getValue().asBoolean();
    saveState();
}

void ALFloaterXUITool::onToggleCodeBuilt()
{
    mShowCodeBuilt = getChild<LLCheckBoxCtrl>("code_built_check")->getValue().asBoolean();
    mModel.getFilter().setShowCodeBuilt(mShowCodeBuilt);
    saveState();
}

void ALFloaterXUITool::onToggleSecondary()
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

void ALFloaterXUITool::saveState()
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
    state["grid"] = mGrid;
    if (LLPanel* current = mInspectors ? mInspectors->getCurrentPanel() : nullptr)
    {
        state["tab"] = current->getName();
    }
    gSavedSettings.setLLSD("ALXUIToolState", state);
}

void ALFloaterXUITool::loadState()
{
    const LLSD state = gSavedSettings.getLLSD("ALXUIToolState");
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

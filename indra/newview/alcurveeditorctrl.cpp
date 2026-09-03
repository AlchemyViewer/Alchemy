/**
 * @file alcurveeditorctrl.cpp
 * @brief XUI widget that plots a curve and lets the user drag its control points
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "alcurveeditorctrl.h"

#include "llfocusmgr.h"
#include "lllocalcliprect.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llstring.h"
#include "lluicolortable.h"
#include "llwindow.h"

static LLDefaultChildRegistry::Register<ALCurveEditorCtrl> r("curve_editor");

namespace
{
// How far from a handle's centre a click still counts, on top of its drawn
// radius. A 4px handle is a small target on a high-DPI display, and missing it
// silently starts no drag at all, so the grab area is deliberately generous.
constexpr S32 GRAB_SLOP = 4;
}

ALCurveEditorCtrl::Params::Params()
:   background_color("background_color", LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor")),
    border_color("border_color", LLUIColorTable::instance().getColor("DefaultShadowLight")),
    grid_color("grid_color", LLUIColorTable::instance().getColor("DefaultShadowDark")),
    curve_color("curve_color", LLUIColorTable::instance().getColor("White")),
    handle_color("handle_color", LLUIColorTable::instance().getColor("White")),
    grid_divisions("grid_divisions", 4),
    curve_samples("curve_samples", 96),
    handle_radius("handle_radius", 4),
    curve_width("curve_width", 1.6f),
    draw_diagonal("draw_diagonal", true),
    points_editable("points_editable", false),
    edited_settings("edited_settings")
{
    changeDefault(mouse_opaque, true);
}

ALCurveEditorCtrl::ALCurveEditorCtrl(const ALCurveEditorCtrl::Params& p)
:   LLUICtrl(p),
    mBackgroundColor(p.background_color),
    mBorderColor(p.border_color),
    mGridColor(p.grid_color),
    mCurveColor(p.curve_color),
    mHandleColor(p.handle_color),
    mGridDivisions(p.grid_divisions),
    mCurveSamples(llmax(2, p.curve_samples())),
    mHandleRadius(llmax(2, p.handle_radius())),
    mCurveWidth(llmax(0.5f, p.curve_width())),
    mDrawDiagonal(p.draw_diagonal),
    mPointsEditable(p.points_editable)
{
    // "A,B, C" -> {A, B, C}: runs of separators collapse, so a space after a
    // comma in the XUI is harmless.
    LLStringUtil::getTokens(p.edited_settings(), mSettingNames, ", ");
}

void ALCurveEditorCtrl::addGhostCurve(curve_fn_t fn, const LLColor4& color)
{
    mGhosts.emplace_back(std::move(fn), color);
}

void ALCurveEditorCtrl::addFillCurve(curve_fn_t fn, const LLColor4& color)
{
    mFills.emplace_back(std::move(fn), color);
}

std::string ALCurveEditorCtrl::getActiveHandleName() const
{
    if (mDragIndex < 0 || mDragIndex >= (S32)mHandles.size())
    {
        return std::string();
    }
    // By value on purpose: a consumer typically reacts to the commit by
    // writing a setting and calling setHandles(), which would leave a
    // reference into the old vector dangling.
    return mHandles[mDragIndex].mName;
}

LLRect ALCurveEditorCtrl::plotRect() const
{
    LLRect rect = getLocalRect();
    rect.stretch(-mHandleRadius);
    return rect;
}

LLVector2 ALCurveEditorCtrl::graphToPixelF(F32 gx, F32 gy) const
{
    const LLRect plot = plotRect();
    return LLVector2(plot.mLeft + llclamp(gx, 0.f, 1.f) * (F32)llmax(1, plot.getWidth()),
                     plot.mBottom + llclamp(gy, 0.f, 1.f) * (F32)llmax(1, plot.getHeight()));
}

void ALCurveEditorCtrl::graphToPixel(F32 gx, F32 gy, S32& px, S32& py) const
{
    const LLVector2 p = graphToPixelF(gx, gy);
    px = ll_round(p.mV[VX]);
    py = ll_round(p.mV[VY]);
}

void ALCurveEditorCtrl::pixelToGraph(S32 px, S32 py, F32& gx, F32& gy) const
{
    const LLRect plot = plotRect();
    gx = llclamp((F32)(px - plot.mLeft) / (F32)llmax(1, plot.getWidth()), 0.f, 1.f);
    gy = llclamp((F32)(py - plot.mBottom) / (F32)llmax(1, plot.getHeight()), 0.f, 1.f);
}

S32 ALCurveEditorCtrl::hitTest(S32 px, S32 py) const
{
    const S32 grab = mHandleRadius + GRAB_SLOP;
    S32 best = -1;
    S32 best_dist_sq = grab * grab + 1;

    for (S32 i = 0; i < (S32)mHandles.size(); ++i)
    {
        S32 hx, hy;
        graphToPixel(mHandles[i].mX, mHandles[i].mY, hx, hy);
        const S32 dx = px - hx;
        const S32 dy = py - hy;
        const S32 dist_sq = dx * dx + dy * dy;
        // Strictly nearer, so when handles overlap the earlier one wins and a
        // drag does not flicker between them as the pointer moves.
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best = i;
        }
    }
    return best;
}

bool ALCurveEditorCtrl::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // Belt and braces: parent dispatch already skips disabled children
    // (visibleEnabledAndContains, llview.cpp), so this cannot be reached
    // disabled today. It is here so a drag can never commit through a greyed
    // widget if that dispatch ever changes.
    if (!getEnabled())
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    const S32 index = hitTest(x, y);
    if (index < 0)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    mDragIndex = index;

    // Grab by the offset from the handle's centre rather than snapping the
    // handle to the pointer, so a click a few pixels off does not jog the
    // value before the drag has even started.
    S32 hx, hy;
    graphToPixel(mHandles[index].mX, mHandles[index].mY, hx, hy);
    mGrabOffsetX = x - hx;
    mGrabOffsetY = y - hy;
    mLastPointerX = x;
    mLastPointerY = y;

    gFocusMgr.setMouseCapture(this);
    setFocus(true);
    return true;
}

bool ALCurveEditorCtrl::handleHover(S32 x, S32 y, MASK mask)
{
    // Bounds-checked every time rather than trusted: the commit below hands
    // control to the consumer, which is free to replace the handle list.
    if (gFocusMgr.getMouseCapture() != this || mDragIndex < 0 || mDragIndex >= (S32)mHandles.size())
    {
        // Not dragging: remember the handle under the pointer, so the target
        // of a double-click is visible before it is clicked.
        mHoverIndex = getEnabled() ? hitTest(x, y) : -1;
        getWindow()->setCursor(mHoverIndex >= 0 ? UI_CURSOR_HAND : UI_CURSOR_ARROW);
        return true;
    }

    // The viewer calls the captor's handleHover every frame (updateUI in
    // llviewerwindow.cpp), not only on motion. A press that never moves must
    // not commit: it would rewrite the value at pixel resolution, dirty the
    // Look and put a step on the undo stack for a click -- and the first
    // half of a double-click is exactly such a press.
    if (x == mLastPointerX && y == mLastPointerY)
    {
        return true;
    }
    mLastPointerX = x;
    mLastPointerY = y;

    F32 gx, gy;
    pixelToGraph(x - mGrabOffsetX, y - mGrabOffsetY, gx, gy);

    if (!mHandles[mDragIndex].mLockX)
    {
        mHandles[mDragIndex].mX = gx;
    }
    if (!mHandles[mDragIndex].mLockY)
    {
        mHandles[mDragIndex].mY = gy;
    }

    // Commit per move: the consumer writes its setting, the renderer picks it
    // up next frame, and the drag previews live. The handle list is the
    // widget's own state, so a consumer that clamps differently is free to
    // write back a corrected position via setHandles().
    mAction = ACTION_DRAG;
    onCommit();
    mAction = ACTION_NONE;

    getWindow()->setCursor(UI_CURSOR_ARROW);
    return true;
}

bool ALCurveEditorCtrl::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (gFocusMgr.getMouseCapture() != this)
    {
        return LLUICtrl::handleMouseUp(x, y, mask);
    }

    gFocusMgr.setMouseCapture(nullptr);
    mDragIndex = -1;
    return true;
}

bool ALCurveEditorCtrl::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    // Without point editing the second press stays inert, as it is today: the
    // widget is mouse_opaque, so the parent chain reports the event handled
    // and the single-click fallback in LLViewerWindow never runs.
    if (!mPointsEditable || !getEnabled())
    {
        return LLUICtrl::handleDoubleClick(x, y, mask);
    }

    // The first press and its release were delivered before this (DOWN, UP,
    // DBLCLK, UP on Win32 and SDL alike), so no drag is live. Drop capture
    // anyway: a REMOVE must not leave a drag pointing at a handle that is
    // about to go.
    if (gFocusMgr.getMouseCapture() == this)
    {
        gFocusMgr.setMouseCapture(nullptr);
    }
    mDragIndex = -1;

    const S32 index = hitTest(x, y);
    if (index >= 0)
    {
        // Name the target through the same API a drag uses, for the duration
        // of the callback only.
        mDragIndex = index;
        mAction = ACTION_REMOVE;
        onCommit();
        mAction = ACTION_NONE;
        mDragIndex = -1;
        mHoverIndex = -1;   // the list may be one shorter; the next hover re-finds it
    }
    else
    {
        pixelToGraph(x, y, mActionX, mActionY);
        mAction = ACTION_ADD;
        onCommit();
        mAction = ACTION_NONE;
    }

    setFocus(true);
    return true;
}

void ALCurveEditorCtrl::onMouseLeave(S32 x, S32 y, MASK mask)
{
    mHoverIndex = -1;
    LLUICtrl::onMouseLeave(x, y, mask);
}

std::vector<LLVector2> ALCurveEditorCtrl::sampleCurve(const curve_fn_t& fn) const
{
    std::vector<LLVector2> pts;

    const LLRect plot = plotRect();
    if (!fn || plot.getWidth() <= 0 || plot.getHeight() <= 0)
    {
        return pts;
    }

    // Sampled in float. Rounding to whole pixels here would quantise the curve
    // before it was drawn, so the anti-aliasing would only soften stair steps
    // the widget had introduced itself.
    pts.reserve(mCurveSamples);
    for (S32 i = 0; i < mCurveSamples; ++i)
    {
        const F32 gx = (F32)i / (F32)(mCurveSamples - 1);
        pts.push_back(graphToPixelF(gx, fn(gx)));
    }
    return pts;
}

void ALCurveEditorCtrl::drawCurve(const curve_fn_t& fn, const LLColor4& color) const
{
    const std::vector<LLVector2> pts = sampleCurve(fn);
    if (pts.empty())
    {
        return;
    }
    gl_polyline_2d(pts, color, mCurveWidth);
}

void ALCurveEditorCtrl::drawFill(const curve_fn_t& fn, const LLColor4& color) const
{
    const std::vector<LLVector2> pts = sampleCurve(fn);
    if (pts.empty())
    {
        return;
    }
    // Down to graph y = 0, not to the widget's bottom edge: the plot is inset
    // by the handle radius, so a fill to the frame would sit below its own
    // axis and the bands would look like they never reach zero.
    gl_polyfill_2d(pts, graphToPixelF(0.f, 0.f).mV[VY], color);
}

void ALCurveEditorCtrl::draw()
{
    const LLRect bounds = getLocalRect();
    const LLRect plot = plotRect();

    gl_rect_2d(bounds, mBackgroundColor.get(), true);

    LLLocalClipRect clip(bounds);

    if (mGridDivisions > 0 && plot.getWidth() > 0 && plot.getHeight() > 0)
    {
        const LLColor4 grid = mGridColor.get();
        for (S32 i = 1; i < mGridDivisions; ++i)
        {
            const F32 f = (F32)i / (F32)mGridDivisions;
            S32 px, py;
            graphToPixel(f, f, px, py);
            gl_line_2d(px, plot.mBottom, px, plot.mTop, grid);
            gl_line_2d(plot.mLeft, py, plot.mRight, py, grid);
        }
    }

    if (mDrawDiagonal)
    {
        // The identity. Without it there is no way to see at a glance whether
        // the curve is lifting or crushing a given input. Drawn as a ribbon
        // like the curves: the grid lines above are axis-aligned and land on
        // pixel boundaries, but a 45-degree line is the worst case for
        // stair-stepping.
        const std::vector<LLVector2> diagonal = { graphToPixelF(0.f, 0.f), graphToPixelF(1.f, 1.f) };
        gl_polyline_2d(diagonal, mGridColor.get(), 1.f);
    }

    // Fills first, so the grid reads through them and the curves sit on top.
    for (const auto& fill : mFills)
    {
        drawFill(fill.first, fill.second);
    }

    for (const auto& ghost : mGhosts)
    {
        drawCurve(ghost.first, ghost.second);
    }
    drawCurve(mCurve, mCurveColor.get());

    for (S32 i = 0; i < (S32)mHandles.size(); ++i)
    {
        const Handle& handle = mHandles[i];
        const LLVector2 c = graphToPixelF(handle.mX, handle.mY);

        // Filled disc first, then an anti-aliased ring over its edge. The
        // fan's own outline is aliased and there is no cheap way to feather a
        // fan, but the ring sits exactly on that edge and covers it.
        const bool active  = (i == mDragIndex);
        const bool hovered = !active && (i == mHoverIndex);
        gGL.color4fv(handle.mColor.mV);
        gl_circle_2d(c.mV[VX], c.mV[VY], (F32)mHandleRadius, 16, true);

        constexpr S32 RING_STEPS = 20;
        std::vector<LLVector2> ring;
        ring.reserve(RING_STEPS);
        for (S32 step = 0; step < RING_STEPS; ++step)
        {
            const F32 a = F_TWO_PI * (F32)step / (F32)RING_STEPS;
            ring.emplace_back(c.mV[VX] + cosf(a) * (F32)mHandleRadius,
                              c.mV[VY] + sinf(a) * (F32)mHandleRadius);
        }
        // The ring, not the disc, carries the highlight: the disc is the
        // handle's own colour, which a channel or a band may be using to say
        // what it is.
        gl_polyline_2d(ring,
                       active ? LLColor4::white
                              : hovered ? LLColor4(1.f, 1.f, 1.f, 0.8f) : mHandleColor.get(),
                       hovered ? 2.5f : 1.5f, true);
    }

    gl_rect_2d(bounds, mBorderColor.get(), false);

    LLUICtrl::draw();
}

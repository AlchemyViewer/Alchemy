/**
 * @file alcurveeditorctrl.h
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

#ifndef AL_CURVEEDITORCTRL_H
#define AL_CURVEEDITORCTRL_H

#include "lluictrl.h"
#include "lluicolor.h"

#include <functional>
#include <string>
#include <vector>

/**
 * A graph with draggable handles. Registered as @c curve_editor.
 *
 * The widget owns no curve of its own: a consumer hands it a sampling function
 * and a list of handles, and it draws the one and drags the others. That split
 * is what makes it reusable -- a tone curve, a falloff, a day-cycle envelope
 * and a gain ramp all differ in what the curve *means*, not in what the graph
 * has to do. @ref ALCurveModel supplies a ready function for the two shapes
 * that already have consumers.
 *
 * Both axes run 0..1 with y up, i.e. graph coordinates, not screen ones.
 *
 * Handles carry axis locks because most real curves have some. A filmic toe
 * moves only horizontally; a locked spline endpoint moves only vertically.
 * Rather than have every consumer re-derive that from its own model, the lock
 * rides on the handle and the drag honours it.
 *
 * A drag fires the commit callback on every mouse move (so the preview tracks
 * the pointer, which is the whole point of a graph) with @ref getActiveHandle
 * naming which one moved.
 */
class ALCurveEditorCtrl final : public LLUICtrl
{
public:
    /// A draggable point on the graph.
    struct Handle
    {
        F32 mX = 0.f;               ///< 0..1, left to right
        F32 mY = 0.f;               ///< 0..1, bottom to top
        bool mLockX = false;        ///< drag cannot change x
        bool mLockY = false;        ///< drag cannot change y
        std::string mName;          ///< reported to the consumer on commit
        LLColor4 mColor = LLColor4::white;
    };

    /// A curve to plot. The primary one is drawn solid; ghosts are drawn thin
    /// and dim, which is how a channel editor shows the two channels you are
    /// not currently editing.
    typedef std::function<F32(F32)> curve_fn_t;

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLUIColor> background_color;
        Optional<LLUIColor> border_color;
        Optional<LLUIColor> grid_color;
        Optional<LLUIColor> curve_color;
        Optional<LLUIColor> handle_color;
        Optional<S32>       grid_divisions;
        Optional<S32>       curve_samples;
        Optional<S32>       handle_radius;
        Optional<F32>       curve_width;
        Optional<bool>      draw_diagonal;

        Params();
    };

    ALCurveEditorCtrl(const Params& p);
    ~ALCurveEditorCtrl() override = default;

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;

    /// The curve to plot. Passing an empty function draws handles only.
    void setCurve(curve_fn_t fn) { mCurve = std::move(fn); }

    /// Dimmed reference curves, drawn behind the primary one.
    void clearGhostCurves() { mGhosts.clear(); }
    void addGhostCurve(curve_fn_t fn, const LLColor4& color);

    /// Curves drawn as a filled area down to y = 0, behind everything else.
    ///
    /// For a graph whose subject is coverage rather than shape -- the split
    /// tone bands, where the question is which tones a tint reaches and how
    /// strongly -- three outlines read as three crossing lines and a filled
    /// band reads immediately. Pass a translucent colour; they overlap.
    void clearFillCurves() { mFills.clear(); }
    void addFillCurve(curve_fn_t fn, const LLColor4& color);

    void setHandles(std::vector<Handle> handles) { mHandles = std::move(handles); }
    const std::vector<Handle>& getHandles() const { return mHandles; }

    /// Which handle the pointer is dragging, or -1 between drags. Valid inside
    /// the commit callback; that is how a consumer tells one handle's move
    /// from another's without diffing the whole list.
    S32 getActiveHandle() const { return mDragIndex; }
    std::string getActiveHandleName() const;

private:
    /// Graph coordinates (0..1, y up) to local widget pixels and back. The
    /// plot is inset by the handle radius so a handle sitting on 0 or 1 is
    /// fully drawn instead of half-clipped by the border.
    LLRect    plotRect() const;
    LLVector2 graphToPixelF(F32 gx, F32 gy) const;
    void      graphToPixel(F32 gx, F32 gy, S32& px, S32& py) const;
    void      pixelToGraph(S32 px, S32 py, F32& gx, F32& gy) const;

    /// Nearest handle to a pixel within grab range, or -1.
    S32 hitTest(S32 px, S32 py) const;

    /// Sample a curve across the plot, in pixels. Shared so a fill and its
    /// outline cannot disagree about where the curve runs.
    std::vector<LLVector2> sampleCurve(const curve_fn_t& fn) const;

    void drawCurve(const curve_fn_t& fn, const LLColor4& color) const;
    void drawFill(const curve_fn_t& fn, const LLColor4& color) const;

    curve_fn_t mCurve;
    std::vector<std::pair<curve_fn_t, LLColor4>> mGhosts;
    std::vector<std::pair<curve_fn_t, LLColor4>> mFills;
    std::vector<Handle> mHandles;

    LLUIColor mBackgroundColor;
    LLUIColor mBorderColor;
    LLUIColor mGridColor;
    LLUIColor mCurveColor;
    LLUIColor mHandleColor;
    S32  mGridDivisions;
    S32  mCurveSamples;
    S32  mHandleRadius;
    F32  mCurveWidth;
    bool mDrawDiagonal;

    S32 mDragIndex = -1;
    S32 mGrabOffsetX = 0;
    S32 mGrabOffsetY = 0;
};

#endif // AL_CURVEEDITORCTRL_H

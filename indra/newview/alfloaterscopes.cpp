/**
 * @file alfloaterscopes.cpp
 * @brief Histogram of the frame the viewer is presenting
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

#include "alfloaterscopes.h"

#include "alcolorwheelmodel.h"
#include "llcombobox.h"
#include "llfontgl.h"
#include "lllocalcliprect.h"
#include "llmenugl.h"
#include "llpanel.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenu.h" // gMenuHolder
#include "llviewerwindow.h"
#include "pipeline.h"

#include <cmath>

namespace
{
const LLColor4 CHANNEL_COLOR[ALScopeData::CH_COUNT] = {
    LLColor4(0.95f, 0.30f, 0.30f, 0.75f),   // red
    LLColor4(0.30f, 0.92f, 0.42f, 0.75f),   // green
    LLColor4(0.40f, 0.58f, 0.98f, 0.75f),   // blue
    LLColor4(0.88f, 0.88f, 0.88f, 0.85f),   // luma
};

/// Curve steepness for the log scale. Chosen so a bin holding a thousandth of
/// the sample still draws about a fifth of the plot's height -- tall enough to
/// see, short enough not to be mistaken for a peak.
constexpr F32 LOG_SCALE_K = 400.f;

/// Space between panes. Wide enough to read as a division rather than a drawing
/// artefact, narrow enough not to eat a small window: at the floater's minimum
/// size a quad layout has about sixty pixels of plot height per pane, and this
/// takes two of them.
constexpr S32 PANE_GAP = 4;
}

ALFloaterScopes::ALFloaterScopes(const LLSD& key)
:   LLFloater(key)
{
    // Registered here rather than in postBuild because the context menu is
    // built from XML during postBuild and resolves these names as it parses.
    mCommitCallbackRegistrar.add("Scopes.SetPaneMode",
                                 boost::bind(&ALFloaterScopes::onPaneModePicked, this, _2));
    mEnableCallbackRegistrar.add("Scopes.IsPaneMode",
                                 boost::bind(&ALFloaterScopes::isPaneModeChecked, this, _2));
}

ALFloaterScopes::~ALFloaterScopes()
{
    // Belt and braces: onClose already cleared it, but a floater destroyed
    // without closing would otherwise leave the renderer sampling forever.
    LLPipeline::sScopeCapture = false;

    // The menu lives in gMenuHolder, not in this floater's view tree, so
    // nothing else is going to take it down with us.
    if (auto* menu = static_cast<LLMenuGL*>(mPopupMenuHandle.get()))
    {
        menu->die();
        mPopupMenuHandle.markDead();
    }
}

bool ALFloaterScopes::postBuild()
{
    mPlotPanel = getChild<LLPanel>("scope_plot");
    mLayoutCombo = getChild<LLComboBox>("scope_layout");
    mClipReadout = getChild<LLTextBox>("scope_clipping");

    if (auto* menu = LLUICtrlFactory::getInstance()->createFromFile<LLContextMenu>(
            "menu_scopes_pane.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance()))
    {
        mPopupMenuHandle = menu->getHandle();
    }

    return LLFloater::postBuild();
}

void ALFloaterScopes::onOpen(const LLSD& key)
{
    LLPipeline::sScopeCapture = true;
    LLFloater::onOpen(key);
}

void ALFloaterScopes::onClose(bool app_quitting)
{
    LLPipeline::sScopeCapture = false;
    LLFloater::onClose(app_quitting);
}

ALFloaterScopes::ELayout ALFloaterScopes::getLayout() const
{
    static LLCachedControl<S32> layout(gSavedSettings, "AlchemyScopeLayout", LAYOUT_SINGLE);
    // Clamped rather than trusted: the setting is user-editable and persisted,
    // and a bad value here would index the pane table out of range.
    return (ELayout)llclamp(layout(), (S32)LAYOUT_SINGLE, (S32)LAYOUT_COUNT - 1);
}

S32 ALFloaterScopes::getPaneCount() const
{
    switch (getLayout())
    {
    case LAYOUT_COLUMNS:
    case LAYOUT_ROWS:
        return 2;
    case LAYOUT_QUAD:
        return MAX_PANES;
    case LAYOUT_SINGLE:
    default:
        return 1;
    }
}

ALFloaterScopes::EMode ALFloaterScopes::getPaneMode(S32 pane) const
{
    if (pane < 0 || pane >= MAX_PANES)
    {
        return MODE_RGB;
    }
    // One control per pane rather than one packed value, so each is separately
    // readable and editable in Debug Settings like every other scope control.
    const std::string name = llformat("AlchemyScopePane%d", pane);
    return (EMode)llclamp(gSavedSettings.getS32(name), (S32)MODE_RGB, (S32)MODE_COUNT - 1);
}

void ALFloaterScopes::setPaneMode(S32 pane, EMode mode)
{
    if (pane < 0 || pane >= MAX_PANES || mode < MODE_RGB || mode >= MODE_COUNT)
    {
        return;
    }
    gSavedSettings.setS32(llformat("AlchemyScopePane%d", pane), (S32)mode);
}

// static
const char* ALFloaterScopes::modeStringName(EMode mode)
{
    switch (mode)
    {
    case MODE_LUMA:      return "mode_luma";
    case MODE_RED:       return "mode_red";
    case MODE_GREEN:     return "mode_green";
    case MODE_BLUE:      return "mode_blue";
    case MODE_VECTOR:    return "mode_vector";
    case MODE_WAVE_LUMA: return "mode_wave_luma";
    case MODE_WAVE_RGB:  return "mode_wave_rgb";
    case MODE_PARADE:    return "mode_parade";
    case MODE_RGB:
    default:             return "mode_rgb";
    }
}

S32 ALFloaterScopes::computePaneRects(LLRect (&out)[MAX_PANES]) const
{
    if (!mPlotPanel)
    {
        return 0;
    }

    const LLRect area = mPlotPanel->getRect();
    const S32    count = getPaneCount();

    // Halves are taken from the far edge rather than by adding the first half's
    // width, so an odd number of pixels lands in one pane instead of leaving a
    // one-pixel strip of background between them.
    const S32 mid_x = area.mLeft + (area.getWidth() - PANE_GAP) / 2;
    const S32 mid_y = area.mBottom + (area.getHeight() - PANE_GAP) / 2;

    switch (getLayout())
    {
    case LAYOUT_COLUMNS:
        out[0] = LLRect(area.mLeft, area.mTop, mid_x, area.mBottom);
        out[1] = LLRect(mid_x + PANE_GAP, area.mTop, area.mRight, area.mBottom);
        break;

    case LAYOUT_ROWS:
        // Pane 0 on top: panes read in the order you assign them, and the eye
        // starts at the top of a window rather than the bottom.
        out[0] = LLRect(area.mLeft, area.mTop, area.mRight, mid_y + PANE_GAP);
        out[1] = LLRect(area.mLeft, mid_y, area.mRight, area.mBottom);
        break;

    case LAYOUT_QUAD:
        out[0] = LLRect(area.mLeft, area.mTop, mid_x, mid_y + PANE_GAP);
        out[1] = LLRect(mid_x + PANE_GAP, area.mTop, area.mRight, mid_y + PANE_GAP);
        out[2] = LLRect(area.mLeft, mid_y, mid_x, area.mBottom);
        out[3] = LLRect(mid_x + PANE_GAP, mid_y, area.mRight, area.mBottom);
        break;

    case LAYOUT_SINGLE:
    default:
        out[0] = area;
        break;
    }

    return count;
}

S32 ALFloaterScopes::paneAt(S32 x, S32 y) const
{
    LLRect    rects[MAX_PANES];
    const S32 count = computePaneRects(rects);
    for (S32 i = 0; i < count; ++i)
    {
        if (rects[i].pointInRect(x, y))
        {
            return i;
        }
    }
    return -1;
}

// static
bool ALFloaterScopes::isWaveMode(EMode mode)
{
    return mode == MODE_WAVE_LUMA || mode == MODE_WAVE_RGB || mode == MODE_PARADE;
}

bool ALFloaterScopes::useLogScale() const
{
    static LLCachedControl<bool> log_scale(gSavedSettings, "AlchemyScopeLogScale", true);
    return log_scale();
}

F32 ALFloaterScopes::barHeight(F32 share, F32 peak) const
{
    if (peak <= 0.f || share <= 0.f)
    {
        return 0.f;
    }
    if (!useLogScale())
    {
        return llclamp(share / peak, 0.f, 1.f);
    }
    // Normalised log so the tallest bin still reaches the top of the plot,
    // whatever the peak happens to be.
    const F32 num = logf(1.f + share * LOG_SCALE_K);
    const F32 den = logf(1.f + peak * LOG_SCALE_K);
    return (den > 0.f) ? llclamp(num / den, 0.f, 1.f) : 0.f;
}

void ALFloaterScopes::drawChannel(const ALScopeData& data, ALScopeData::EChannel channel,
                                  const LLColor4& color, const LLRect& plot, bool filled) const
{
    const F32 peak = data.getPeak(channel);
    if (peak <= 0.f)
    {
        return;
    }

    const F32 width = (F32)plot.getWidth();
    const F32 height = (F32)plot.getHeight();
    const F32 base = (F32)plot.mBottom;

    // One point per bin, at the bin's centre.
    std::vector<LLVector2> outline;
    outline.reserve(ALScopeData::BIN_COUNT);
    for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
    {
        const F32 h = barHeight(data.getBin(channel, i), peak) * height;
        const F32 x = plot.mLeft + width * ((F32)i + 0.5f) / (F32)ALScopeData::BIN_COUNT;
        outline.emplace_back(x, base + h);
    }

    if (filled)
    {
        // Filled as trapezoids between neighbouring bin centres rather than as
        // 256 separate bars. At a typical plot width a bin is barely more than
        // a pixel across, so bars land on fractional boundaries and cover one
        // pixel centre or two depending on phase -- which beats into a moire
        // pattern that moves as the window is resized. Trapezoids share their
        // edges exactly, so every pixel column is covered once.
        gGL.getTextureSlot(0)->unbind();
        gGL.color4fv(color.mV);
        gGL.begin(LLRender::TRIANGLES);
        for (size_t i = 0; i + 1 < outline.size(); ++i)
        {
            const F32 x0 = outline[i].mV[VX];
            const F32 x1 = outline[i + 1].mV[VX];
            const F32 y0 = outline[i].mV[VY];
            const F32 y1 = outline[i + 1].mV[VY];

            gGL.vertex2f(x0, base);
            gGL.vertex2f(x1, base);
            gGL.vertex2f(x1, y1);

            gGL.vertex2f(x0, base);
            gGL.vertex2f(x1, y1);
            gGL.vertex2f(x0, y0);
        }
        gGL.end();
        gGL.flush();
    }

    // The fill's upper edge is the one part of it that is not axis-aligned, so
    // it gets the anti-aliased ribbon over the top. In outline mode this is
    // the whole plot.
    LLColor4 edge(color);
    edge.mV[VALPHA] = filled ? llmin(1.f, color.mV[VALPHA] * 1.6f) : color.mV[VALPHA];
    gl_polyline_2d(outline, edge, filled ? 1.2f : 1.6f);
}

void ALFloaterScopes::drawHistogram(const ALScopeData& data, EMode mode, const LLRect& plot) const
{
    gl_rect_2d(plot, LLUIColorTable::instance().getColor("MenuDefaultBgColor").get(), true);

    // Quarter-tone guides. Photographers reach for these constantly -- "is
    // anything in the top quarter?" is most of what a histogram is asked.
    const LLColor4 grid = LLUIColorTable::instance().getColor("DefaultShadowDark").get();
    for (S32 i = 1; i < 4; ++i)
    {
        const S32 x = plot.mLeft + plot.getWidth() * i / 4;
        gl_line_2d(x, plot.mBottom, x, plot.mTop, grid);
    }

    {
        LLLocalClipRect clip(plot);
        switch (mode)
        {
        case MODE_RGB:
            // Additive so overlapping channels brighten towards white where
            // they agree, which is exactly the neutral-grey reading you want.
            for (S32 c = ALScopeData::CH_RED; c <= ALScopeData::CH_BLUE; ++c)
            {
                drawChannel(data, (ALScopeData::EChannel)c, CHANNEL_COLOR[c], plot, true);
            }
            drawChannel(data, ALScopeData::CH_LUMA, LLColor4(1.f, 1.f, 1.f, 0.55f), plot, false);
            break;
        case MODE_LUMA:
            drawChannel(data, ALScopeData::CH_LUMA, CHANNEL_COLOR[ALScopeData::CH_LUMA], plot, true);
            break;
        case MODE_RED:
        case MODE_GREEN:
        case MODE_BLUE:
        {
            const auto ch = (ALScopeData::EChannel)(mode - MODE_RED);
            drawChannel(data, ch, CHANNEL_COLOR[ch], plot, true);
            break;
        }
        default:
            break;
        }
    }

    gl_rect_2d(plot, LLUIColorTable::instance().getColor("DefaultShadowLight").get(), false);
}

void ALFloaterScopes::drawWaveChannel(const ALScopeData& data, ALScopeData::EChannel channel,
                                      const LLColor4& color, const LLRect& plot) const
{
    const F32 peak = data.getWavePeak(channel);
    if (peak <= 0.f || plot.getWidth() <= 0 || plot.getHeight() <= 0)
    {
        return;
    }

    const F32 cell_w = (F32)plot.getWidth() / (F32)ALScopeData::WAVE_COLUMNS;
    const F32 cell_h = (F32)plot.getHeight() / (F32)ALScopeData::WAVE_LEVELS;

    // Below this a cell cannot change an 8-bit pixel, so drawing it costs six
    // vertices to composite nothing. Most of a waveform's grid is this: a
    // typical frame lights well under half of any column.
    constexpr F32 MIN_VISIBLE = 1.f / 255.f;

    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLES);
    for (S32 column = 0; column < ALScopeData::WAVE_COLUMNS; ++column)
    {
        const F32 x0 = (F32)plot.mLeft + (F32)column * cell_w;
        const F32 x1 = x0 + cell_w;

        // Levels are contiguous within a column, which is the order this walks
        // them in -- see ALScopeData::waveIndex.
        for (S32 level = 0; level < ALScopeData::WAVE_LEVELS; ++level)
        {
            const F32 share = data.getWaveCell(channel, column, level);
            if (share <= 0.f)
            {
                continue;
            }

            // Same curve the histogram's bars use, so the log toggle means the
            // same thing in both and a trace does not change character when
            // you switch modes.
            const F32 intensity = barHeight(share, peak) * color.mV[VALPHA];
            if (intensity < MIN_VISIBLE)
            {
                continue;
            }

            const F32 y0 = (F32)plot.mBottom + (F32)level * cell_h;
            const F32 y1 = y0 + cell_h;

            gGL.color4f(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], intensity);
            gGL.vertex2f(x0, y0);
            gGL.vertex2f(x1, y0);
            gGL.vertex2f(x1, y1);

            gGL.vertex2f(x0, y0);
            gGL.vertex2f(x1, y1);
            gGL.vertex2f(x0, y1);
        }
    }
    gGL.end();
    gGL.flush();
}

void ALFloaterScopes::drawWaveform(const ALScopeData& data, EMode mode, const LLRect& panel) const
{
    gl_rect_2d(panel, LLUIColorTable::instance().getColor("MenuDefaultBgColor").get(), true);

    // Guides run horizontally here, not vertically as on the histogram: on a
    // waveform the level is the vertical axis, so "is anything in the top
    // quarter" is a question about height.
    const LLColor4 grid = LLUIColorTable::instance().getColor("DefaultShadowDark").get();
    for (S32 i = 1; i < 4; ++i)
    {
        const S32 y = panel.mBottom + panel.getHeight() * i / 4;
        gl_line_2d(panel.mLeft, y, panel.mRight, y, grid);
    }

    {
        LLLocalClipRect clip(panel);
        switch (mode)
        {
        case MODE_PARADE:
        {
            // Three plots side by side. A parade is read by comparing the
            // heights of the three traces, so they need separate horizontal
            // space -- overlaid, a cast just looks like a thicker trace.
            constexpr S32 GAP = 3;
            const S32     width = (panel.getWidth() - 2 * GAP) / 3;
            for (S32 c = 0; c < 3; ++c)
            {
                const S32    left = panel.mLeft + c * (width + GAP);
                const LLRect sub(left, panel.mTop, left + width, panel.mBottom);

                // Opaque: side by side there is nothing to see through, and the
                // shared alpha would only make all three dimmer.
                LLColor4 solid(CHANNEL_COLOR[c]);
                solid.mV[VALPHA] = 1.f;
                drawWaveChannel(data, (ALScopeData::EChannel)c, solid, sub);

                gl_rect_2d(sub, grid, false);
            }
            break;
        }
        case MODE_WAVE_RGB:
            for (S32 c = ALScopeData::CH_RED; c <= ALScopeData::CH_BLUE; ++c)
            {
                drawWaveChannel(data, (ALScopeData::EChannel)c, CHANNEL_COLOR[c], panel);
            }
            break;
        case MODE_WAVE_LUMA:
        default:
            drawWaveChannel(data, ALScopeData::CH_LUMA, CHANNEL_COLOR[ALScopeData::CH_LUMA], panel);
            break;
        }
    }

    gl_rect_2d(panel, LLUIColorTable::instance().getColor("DefaultShadowLight").get(), false);
}

void ALFloaterScopes::updateReadouts(const ALScopeData& data)
{
    if (!mClipReadout)
    {
        return;
    }

    if (data.isEmpty())
    {
        mClipReadout->setText(getString("waiting"));
        return;
    }

    // Report the worst channel: one blown channel is a clipped pixel even when
    // the other two have headroom, and averaging would hide it.
    F32 low = 0.f;
    F32 high = 0.f;
    for (S32 c = ALScopeData::CH_RED; c <= ALScopeData::CH_BLUE; ++c)
    {
        const auto ch = (ALScopeData::EChannel)c;
        low = llmax(low, data.getClippedLow(ch));
        high = llmax(high, data.getClippedHigh(ch));
    }

    LLStringUtil::format_map_t args;
    args["[LOW]"] = llformat("%.2f", low * 100.f);
    args["[HIGH]"] = llformat("%.2f", high * 100.f);

    // Reading the value under the cursor is most of what a scope gets asked
    // once the shape of the plot is familiar -- "is that 235 or 255?" -- and it
    // comes free: the sample captureScopeSample already took is still in
    // memory, so this is a lookup rather than a readback.
    //
    // It replaces the sample count rather than joining it. The count is
    // reassurance you read once; the pixel is the answer you came for, and the
    // line is not wide enough for both.
    LLColor4U pixel;
    if (gPipeline.getScopePixel(gViewerWindow->getCurrentMouseX(), gViewerWindow->getCurrentMouseY(), pixel))
    {
        args["[R]"] = llformat("%3d", pixel.mV[VRED]);
        args["[G]"] = llformat("%3d", pixel.mV[VGREEN]);
        args["[B]"] = llformat("%3d", pixel.mV[VBLUE]);
        mClipReadout->setText(getString("clipping_pixel", args));
        return;
    }

    args["[SAMPLES]"] = llformat("%d", data.getSampleCount());
    mClipReadout->setText(getString("clipping", args));
}

namespace
{
/// Colour for a vectorscope cell: the hue of its own direction, saturated by
/// its distance from neutral.
///
/// Both are fixed by the cell's index, so this is built once. Deriving it per
/// frame cost three trig calls for each of up to 4096 cells, every frame the
/// window was open, to arrive at the same answer each time.
///
/// Saturating by radius is not just decoration. Drawn at full chroma
/// regardless of distance, the middle of the scope -- where a well balanced
/// frame puts nearly all of its pixels -- came out as a faint rainbow rather
/// than the neutral grey it actually is.
const LLColor4& cellTint(S32 u, S32 v)
{
    static std::vector<LLColor4> table;
    if (table.empty())
    {
        table.reserve(ALScopeData::CHROMA_SIZE * ALScopeData::CHROMA_SIZE);
        for (S32 i = 0; i < ALScopeData::CHROMA_SIZE; ++i)
        {
            for (S32 j = 0; j < ALScopeData::CHROMA_SIZE; ++j)
            {
                const F32 su = ALScopeData::chromaCellCentre(i);
                const F32 sv = ALScopeData::chromaCellCentre(j);
                const F32 radius = llmin(1.f, sqrtf(su * su + sv * sv));

                // Through the wheels' own basis, so the trace and a wheel puck
                // agree about where a hue lives.
                const LLVector3 dir = ALColorWheelModel::chromaDirection(atan2f(sv, su));
                table.emplace_back(0.5f + dir.mV[VX] * 0.6f * radius,
                                   0.5f + dir.mV[VY] * 0.6f * radius,
                                   0.5f + dir.mV[VZ] * 0.6f * radius,
                                   1.f);
            }
        }
    }
    return table[u * ALScopeData::CHROMA_SIZE + v];
}
} // namespace

void ALFloaterScopes::drawVectorscope(const ALScopeData& data, const LLRect& panel) const
{
    gl_rect_2d(panel, LLUIColorTable::instance().getColor("MenuDefaultBgColor").get(), true);

    // Square and centred. The chroma plane is isotropic -- a hue is a
    // direction -- so stretching it to a wide panel would make some hues look
    // more saturated than others at the same distance from neutral.
    const S32 side = llmin(panel.getWidth(), panel.getHeight());
    const S32 left = panel.mLeft + (panel.getWidth() - side) / 2;
    const S32 bottom = panel.mBottom + (panel.getHeight() - side) / 2;
    const LLRect plot(left, bottom + side, left + side, bottom);
    const F32 cx = (F32)plot.mLeft + (F32)side * 0.5f;
    const F32 cy = (F32)plot.mBottom + (F32)side * 0.5f;
    const F32 radius = (F32)side * 0.5f;

    const LLColor4 grid = LLUIColorTable::instance().getColor("DefaultShadowDark").get();

    // Graticule: rings at a third and two thirds of full chroma, and spokes on
    // the six primaries and secondaries -- the targets a colourist actually
    // reads a vectorscope against.
    for (S32 ring = 1; ring <= 3; ++ring)
    {
        std::vector<LLVector2> circle;
        constexpr S32 STEPS = 48;
        circle.reserve(STEPS);
        const F32 r = radius * (F32)ring / 3.f;
        for (S32 i = 0; i < STEPS; ++i)
        {
            const F32 a = F_TWO_PI * (F32)i / (F32)STEPS;
            circle.emplace_back(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        gl_polyline_2d(circle, grid, 1.f, true);
    }
    for (S32 spoke = 0; spoke < 6; ++spoke)
    {
        // Derived from the same basis the wheels use, so a spoke labelled red
        // points where a red cast actually lands.
        const F32 a = F_TWO_PI * (F32)spoke / 6.f;
        const std::vector<LLVector2> line = {
            LLVector2(cx, cy),
            LLVector2(cx + cosf(a) * radius, cy + sinf(a) * radius) };
        gl_polyline_2d(line, grid, 1.f);
    }

    {
        LLLocalClipRect clip(plot);

        const F32 peak = data.getChromaPeak();
        if (peak > 0.f)
        {
            // Each cell is drawn in the colour it represents, at full chroma
            // for the hue and a brightness that follows how much of the frame
            // sits there. A trace is then legible as itself: a warm sky reads
            // as an orange smear at orange, not as a grey blob you have to
            // measure against the graticule.
            const F32 cell = (F32)side / (F32)ALScopeData::CHROMA_SIZE;
            gGL.getTextureSlot(0)->unbind();
            gGL.begin(LLRender::TRIANGLES);
            for (S32 u = 0; u < ALScopeData::CHROMA_SIZE; ++u)
            {
                for (S32 v = 0; v < ALScopeData::CHROMA_SIZE; ++v)
                {
                    const F32 share = data.getChromaCell(u, v);
                    if (share <= 0.f)
                    {
                        continue;
                    }

                    // The same curve the histogram offers, and for the same
                    // reason: a frame is mostly near-neutral, so on a linear
                    // scale the centre saturates and everything a colourist is
                    // looking for disappears into the background.
                    const F32 level = barHeight(share, peak);

                    const F32 su = ALScopeData::chromaCellCentre(u);
                    const F32 sv = ALScopeData::chromaCellCentre(v);
                    const F32 x0 = cx + su * radius - cell * 0.5f;
                    const F32 y0 = cy + sv * radius - cell * 0.5f;
                    const F32 x1 = x0 + cell;
                    const F32 y1 = y0 + cell;

                    LLColor4 tint = cellTint(u, v);
                    tint.mV[VALPHA] = llclamp(level, 0.f, 1.f);

                    gGL.color4fv(tint.mV);
                    gGL.vertex2f(x0, y0);
                    gGL.vertex2f(x1, y0);
                    gGL.vertex2f(x1, y1);

                    gGL.vertex2f(x0, y0);
                    gGL.vertex2f(x1, y1);
                    gGL.vertex2f(x0, y1);
                }
            }
            gGL.end();
            gGL.flush();
        }
    }

    gl_rect_2d(panel, LLUIColorTable::instance().getColor("DefaultShadowLight").get(), false);
}

void ALFloaterScopes::drawScope(const ALScopeData& data, EMode mode, const LLRect& rect) const
{
    if (rect.getWidth() <= 0 || rect.getHeight() <= 0)
    {
        return;
    }

    if (mode == MODE_VECTOR)
    {
        drawVectorscope(data, rect);
    }
    else if (isWaveMode(mode))
    {
        drawWaveform(data, mode, rect);
    }
    else
    {
        drawHistogram(data, mode, rect);
    }
}

void ALFloaterScopes::drawPaneLabel(EMode mode, const LLRect& rect) const
{
    // Only when there is more than one pane. With a single scope filling the
    // window the layout combo already says what it is, and the label would be
    // ink on the plot for nothing.
    if (getPaneCount() < 2)
    {
        return;
    }

    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    if (!font)
    {
        return;
    }

    // Dim rather than full strength: this identifies the pane, it is not part
    // of the measurement, and a bright label competes with the trace.
    LLColor4 color = LLUIColorTable::instance().getColor("TextFgReadOnlyColor").get();
    color.mV[VALPHA] = 0.65f;

    // Ellipsised to the pane rather than clipped to it. These names are
    // translated, and a longer language would otherwise run a label out of its
    // own pane and across the trace in the one beside it.
    font->renderUTF8(getString(modeStringName(mode)), 0,
                     (F32)(rect.mLeft + 4), (F32)(rect.mTop - 2),
                     color, LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::NORMAL,
                     LLFontGL::DROP_SHADOW_SOFT,
                     S32_MAX, rect.getWidth() - 8, nullptr, true);
}

bool ALFloaterScopes::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 pane = paneAt(x, y);
    if (pane < 0)
    {
        return LLFloater::handleRightMouseDown(x, y, mask);
    }

    auto* menu = static_cast<LLContextMenu*>(mPopupMenuHandle.get());
    if (!menu)
    {
        return LLFloater::handleRightMouseDown(x, y, mask);
    }

    mMenuPane = pane;
    menu->buildDrawLabels();
    menu->updateParent(LLMenuGL::sMenuContainer);

    // LLContextMenu::show, not LLMenuGL::showPopup. LLContextMenu overrides
    // setVisible to ignore anything but false ("can't set visibility directly,
    // must call show or hide"), and showPopup's only attempt to reveal a menu
    // is setVisible(true) -- so it silently does nothing here. Everything else
    // showPopup would have done, show() does for itself.
    //
    // show() takes screen coordinates; a mouse handler is given coordinates
    // local to the view it landed on.
    S32 screen_x, screen_y;
    localPointToScreen(x, y, &screen_x, &screen_y);
    menu->show(screen_x, screen_y, this);
    return true;
}

void ALFloaterScopes::onPaneModePicked(const LLSD& userdata)
{
    setPaneMode(mMenuPane, (EMode)userdata.asInteger());
}

bool ALFloaterScopes::isPaneModeChecked(const LLSD& userdata) const
{
    return mMenuPane >= 0 && getPaneMode(mMenuPane) == (EMode)userdata.asInteger();
}

void ALFloaterScopes::draw()
{
    if (mPlotPanel)
    {
        updateReadouts(gPipeline.getScopeData());
    }

    // Base first. Unlike a panel, a floater paints its own background in
    // draw(), so a plot drawn before this would simply be painted over.
    LLFloater::draw();

    if (mPlotPanel)
    {
        const ALScopeData& data = gPipeline.getScopeData();

        LLRect    rects[MAX_PANES];
        const S32 count = computePaneRects(rects);
        for (S32 i = 0; i < count; ++i)
        {
            const EMode mode = getPaneMode(i);
            drawScope(data, mode, rects[i]);
            drawPaneLabel(mode, rects[i]);
        }
    }
}

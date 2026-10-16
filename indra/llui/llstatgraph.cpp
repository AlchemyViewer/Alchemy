/**
 * @file llstatgraph.cpp
 * @brief Simpler compact stat graph with tooltip
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

//#include "llviewerprecompiledheaders.h"
#include "linden_common.h"

#include "llstatgraph.h"
#include "llrender.h"

#include "llmath.h"
#include "llui.h"
#include "llgl.h"
#include "llglheaders.h"
#include "lltracerecording.h"
#include "lltracethreadrecorder.h"

#include <fmt/format.h>

#include <algorithm>

///////////////////////////////////////////////////////////////////////////////////

LLStatGraph::LLStatGraph(const Params& p)
:   LLView(p),
    mNewStatFloatp(p.stat.count_stat_float),
    mPerSec(p.per_sec),
    mValue(p.value),
    mMin(p.min),
    mMax(p.max),
    mLabel(p.label),
    mUnits(p.units),
    mPrecision(p.precision)
{
    setToolTip(p.name());

    for(LLInitParam::ParamIterator<ThresholdParams>::const_iterator it = p.thresholds.threshold.begin(), end_it = p.thresholds.threshold.end();
        it != end_it;
        ++it)
    {
        mThresholds.push_back(Threshold(it->value(), it->color));
    }

    // draw() picks a color with lower_bound, which needs these ordered; the
    // defaults arrive sorted but a set supplied from XUI need not be.
    std::sort(mThresholds.begin(), mThresholds.end());
}

void LLStatGraph::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    const F32 range = mMax - mMin;
    if (mNewStatFloatp)
    {
        LLTrace::Recording& recording = LLTrace::get_frame_recording().getLastRecording();

        if (mPerSec)
        {
            mValue = (F32)recording.getPerSec(*mNewStatFloatp);
        }
        else
        {
            mValue = (F32)recording.getSum(*mNewStatFloatp);
        }
    }

    // An empty range has no position to report, so the bar reads as full rather
    // than dividing through zero and carrying a NaN into the fill height.
    F32 frac = is_approx_equal(range, 0.f) ? 1.f : (mValue - mMin) / range;
    frac = llclamp(frac, 0.f, 1.f);

    if (mUpdateTimer.getElapsedTimeF32() > 0.5f)
    {
        setToolTip(fmt::format("{}{:.{}f}{}", mLabel, mValue, (int)mPrecision, mUnits));

        mUpdateTimer.reset();
    }

    // Thresholds are positions within the same range the fill height uses, so
    // both read the range the same way and a non-zero mMin cannot desync the
    // color from the bar it colors.
    LLColor4 fill_color = LLColor4::white;
    if (!mThresholds.empty())
    {
        threshold_vec_t::const_iterator it = std::lower_bound(mThresholds.begin(), mThresholds.end(), Threshold(frac, LLUIColor()));

        if (it != mThresholds.begin())
        {
            it--;
        }
        fill_color = it->mColor();
    }

    static LLUIColor default_color = LLUIColorTable::instance().getColor( "MenuDefaultBgColor" );
    gGL.color4fv(default_color.get().mV);
    gl_rect_2d(0, getRect().getHeight(), getRect().getWidth(), 0, true);

    gGL.color4fv(LLColor4::black.mV);
    gl_rect_2d(0, getRect().getHeight(), getRect().getWidth(), 0, false);

    gGL.color4fv(fill_color.mV);
    gl_rect_2d(1, ll_round(frac*getRect().getHeight()), getRect().getWidth() - 1, 0, true);
}

void LLStatGraph::setMin(const F32 min)
{
    mMin = min;
}

void LLStatGraph::setMax(const F32 max)
{
    mMax = max;
}


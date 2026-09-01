/**
 * @file llstatbar.cpp
 * @brief A little map of the world with network information
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llstatbar.h"

#include "llmath.h"
#include "llui.h"
#include "llgl.h"
#include "llfontgl.h"

#include "lluictrlfactory.h"
#include "lltracerecording.h"
#include "llcriticaldamp.h"
#include "lltooltip.h"
#include "lltrans.h"

#include <fmt/format.h>

#include <array>
#include <string_view>

// rate at which to update display of value that is rapidly changing
const F32 MEAN_VALUE_UPDATE_TIME = 1.f / 4.f;
// time between value changes that qualifies as a "rapid change"
const F32Seconds    RAPID_CHANGE_THRESHOLD(0.2f);
// maximum number of rapid changes in RAPID_CHANGE_WINDOW before switching over to displaying the mean
// instead of latest value
const S32 MAX_RAPID_CHANGES_PER_SEC = 10;
// period of time over which to measure rapid changes
const F32Seconds RAPID_CHANGE_WINDOW(1.f);

// Stat bar vertical layout, in UI pixels. The label/value line at the top and
// (for VERTICAL bars) the tick-label row along the bottom are each one monospace
// line tall. Deriving the reserves from the live font line height keeps taller
// faces (e.g. the current default) from clipping descenders or tick labels the
// way the old fixed 14/15/20px constants did.
const S32 STAT_BAR_TEXT_VPAD   = 2;  // breathing room beneath a text line
const S32 STAT_BAR_TICK_LENGTH = 4;  // tick-mark length drawn past the bar
const S32 STAT_BAR_MIN_BAR     = 5;  // smallest drawn bar thickness

F32 calc_tick_value(F32 min, F32 max)
{
    F32 range = max - min;
    const S32 DIVISORS[] = {6, 8, 10, 4, 5};
    // try storing
    S32 best_decimal_digit_count = S32_MAX;
    S32 best_divisor = 10;
    for (U32 divisor_idx = 0; divisor_idx < LL_ARRAY_SIZE(DIVISORS); divisor_idx++)
    {
        S32 divisor = DIVISORS[divisor_idx];
        F32 possible_tick_value = range / divisor;
        // logf(0) is -inf, and llceil of that is not representable as S32; a
        // magnitude that small has one whole digit ("0") by definition.
        const F32 first_tick_magnitude = llabs(min + possible_tick_value);
        S32 num_whole_digits = is_approx_equal(first_tick_magnitude, 0.f)
                             ? 1
                             : llceil(logf(first_tick_magnitude) * OO_LN10);
        for (S32 digit_count = -(num_whole_digits - 1); digit_count < 6; digit_count++)
        {
            F32 test_tick_value = min + (possible_tick_value * (F32)pow(10.0, digit_count));

            if (is_approx_equal((F32)(S32)test_tick_value, test_tick_value))
            {
                if (digit_count < best_decimal_digit_count)
                {
                    best_decimal_digit_count = digit_count;
                    best_divisor = divisor;
                }
                break;
            }
        }
    }

    return is_approx_equal(range, 0.f) ? 0.f : range / best_divisor;
}

void calc_auto_scale_range(F32& min, F32& max, F32& tick)
{
    // The displayed range always contains zero, so bar lengths read as magnitudes.
    // Both bounds come from the incoming pair; deriving max from the already
    // clamped min would fold the two steps together.
    const F32 in_min = min;
    const F32 in_max = max;
    min = llmin(0.f, in_min, in_max);
    max = llmax(0.f, in_min, in_max);

    const F32 RANGES[] = {0.f, 1.f,   1.5f, 2.f, 3.f, 5.f, 10.f};
    const F32 TICKS[]  = {0.f, 0.25f, 0.5f, 1.f, 1.f, 1.f, 2.f };

    const S32 num_digits_max = is_approx_equal(llabs(max), 0.f)
                            ? S32_MIN + 1
                            : llceil(logf(llabs(max)) * OO_LN10);
    const S32 num_digits_min = is_approx_equal(llabs(min), 0.f)
                            ? S32_MIN + 1
                            : llceil(logf(llabs(min)) * OO_LN10);

    const S32 num_digits = llmax(num_digits_max, num_digits_min);
    const F32 power_of_10 = (F32)pow(10.0, num_digits - 1);
    const F32 starting_max = power_of_10 * ((max < 0.f) ? -1 : 1);
    const F32 starting_min = power_of_10 * ((min < 0.f) ? -1 : 1);

    F32 out_max = max;
    F32 out_min = min;

    F32 cur_tick_min = 0.f;
    F32 cur_tick_max = 0.f;

    // min is at or below zero and max at or above it, so only a bound that
    // widens away from zero can match. Walk from the widest range to the
    // narrowest and keep the last one that still contains the data.
    for (S32 range_idx = (S32)LL_ARRAY_SIZE(RANGES) - 1; range_idx >= 0; range_idx--)
    {
        const F32 cur_max = starting_max * RANGES[range_idx];
        const F32 cur_min = starting_min * RANGES[range_idx];

        if (min < 0.f && cur_min <= min)
        {
            out_min = cur_min;
            cur_tick_min = TICKS[range_idx];
        }
        if (max > 0.f && cur_max >= max)
        {
            out_max = cur_max;
            cur_tick_max = TICKS[range_idx];
        }
    }

    tick = power_of_10 * llmax(cur_tick_min, cur_tick_max);
    min = out_min;
    max = out_max;
}

// The buffer belongs to the caller and has to outlive the view.
std::string_view LLStatBar::bufView(const text_buf_t& buf, size_t written)
{
    // format_to_n reports what the format *would* have taken, snprintf-style.
    return std::string_view(buf.data(), llmin(written, buf.size()));
}

// A tick that lands on a whole number reads better without a fraction.
std::string_view LLStatBar::formatTickLabel(text_buf_t& buf, F32 tick_value, S32 decimal_digits)
{
    const int digits = is_approx_equal((F32)(S32)tick_value, tick_value) ? 0 : (int)decimal_digits;
    const auto result = fmt::format_to_n(buf.data(), buf.size(), "{:.{}f}", tick_value, digits);
    return bufView(buf, result.size);
}

// Emits a filled rect into the batch the caller has open, wound the way
// gl_rect_2d winds one. gl_rect_2d itself cannot be used inside a batch: it
// unbinds the texture slot first, and unbind() flushes, so every rect drawn
// through it becomes its own draw call.
//
// The colour rides on the vertices. That holds because the nested-UI pass runs
// under gUIProgram, whose vertex shader takes diffuse_color as an attribute; a
// program without that attribute routes gGL.color4f to a uniform instead, and
// the whole batch would come out the last colour set.
//
// Coordinates are clamped to the widget rather than scissored to it. Several of
// them are derived from stat values and are unbounded until something bounds
// them; a scissor is what used to, at the price of a glScissor and a flush on
// the way in and another pair on the way out. Each axis clamps on its own, so a
// rect handed its edges the other way round stays that way round.
void LLStatBar::batchRect( S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color ) const
{
    const S32 max_x = getRect().getWidth();
    const S32 max_y = getRect().getHeight();

    left   = llclamp(left,   0, max_x);
    right  = llclamp(right,  0, max_x);
    top    = llclamp(top,    0, max_y);
    bottom = llclamp(bottom, 0, max_y);

    if (left == right || top == bottom)
    {
        return;
    }

    gGL.color4fv(color.mV);

    gGL.vertex2i(left,  top);
    gGL.vertex2i(left,  bottom);
    gGL.vertex2i(right, bottom);

    gGL.vertex2i(left,  top);
    gGL.vertex2i(right, bottom);
    gGL.vertex2i(right, top);
}

LLStatBar::Params::Params()
:   label("label"),
    unit_label("unit_label"),
    bar_min("bar_min", 0.f),
    bar_max("bar_max", 0.f),
    tick_spacing("tick_spacing", 0.f),
    decimal_digits("decimal_digits", 3),
    show_bar("show_bar", false),
    show_median("show_median", false),
    show_history("show_history", false),
    scale_range("scale_range", true),
    num_frames("num_frames", 200),
    num_frames_short("num_frames_short", 20),
    max_height("max_height", 100),
    stat("stat"),
    orientation("orientation", VERTICAL)
{
    changeDefault(follows.flags, FOLLOWS_TOP | FOLLOWS_LEFT);
}

///////////////////////////////////////////////////////////////////////////////////

LLStatBar::LLStatBar(const Params& p)
:   LLView(p),
    mLabel(p.label),
    mUnitLabel(p.unit_label),
    mTargetMinBar(llmin(p.bar_min, p.bar_max)),
    mTargetMaxBar(llmax(p.bar_max, p.bar_min)),
    mCurMaxBar(p.bar_max),
    mCurMinBar(0),
    mDecimalDigits(p.decimal_digits),
    mNumHistoryFrames(p.num_frames),
    mNumShortHistoryFrames(p.num_frames_short),
    mMaxHeight(p.max_height),
    mDisplayBar(p.show_bar),
    mShowMedian(p.show_median),
    mDisplayHistory(p.show_history),
    mOrientation(p.orientation),
    mAutoScaleMax(!p.bar_max.isProvided()),
    mAutoScaleMin(!p.bar_min.isProvided()),
    mTickSpacing(p.tick_spacing),
    mLastDisplayValue(0.f),
    mStatType(STAT_NONE)
{
    mFloatingTargetMinBar = mTargetMinBar;
    mFloatingTargetMaxBar = mTargetMaxBar;

    mStat.valid = NULL;
    // tick value will be automatically calculated later
    if (!p.tick_spacing.isProvided() && p.bar_min.isProvided() && p.bar_max.isProvided())
    {
        mTickSpacing = calc_tick_value(mTargetMinBar, mTargetMaxBar);
    }

    setStat(p.stat);
}

bool LLStatBar::handleHover(S32 x, S32 y, MASK mask)
{
    switch(mStatType)
    {
    case STAT_COUNT:
        LLToolTipMgr::instance().show(LLToolTip::Params().message(mStat.countStatp->getDescription()).sticky_rect(calcScreenRect()));
        break;
    case STAT_EVENT:
        LLToolTipMgr::instance().show(LLToolTip::Params().message(mStat.eventStatp->getDescription()).sticky_rect(calcScreenRect()));
        break;
    case STAT_SAMPLE:
        LLToolTipMgr::instance().show(LLToolTip::Params().message(mStat.sampleStatp->getDescription()).sticky_rect(calcScreenRect()));
        break;
    default:
        break;
    }
    return true;
}

bool LLStatBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    bool handled = LLView::handleMouseDown(x, y, mask);
    if (!handled)
    {
        if (mDisplayBar)
        {
            if (mDisplayHistory || mOrientation == HORIZONTAL)
            {
                mDisplayBar = false;
                mDisplayHistory = false;
            }
            else
            {
                mDisplayHistory = true;
            }
        }
        else
        {
            mDisplayBar = true;
            if (mOrientation == HORIZONTAL)
            {
                mDisplayHistory = true;
            }
        }
        LLView* parent = getParent();
        parent->reshape(parent->getRect().getWidth(), parent->getRect().getHeight(), false);
    }
    return true;
}

template<typename T>
S32 calc_num_rapid_changes(LLTrace::PeriodicRecording& periodic_recording, const T& stat, const F32Seconds time_period)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    F32Seconds          elapsed_time,
                        time_since_value_changed;
    S32                 num_rapid_changes           = 0;
    F64                 last_value                  = periodic_recording.getPrevRecording(1).getLastValue(stat);

    const size_t        num_periods                 = periodic_recording.getNumRecordedPeriods();
    for (size_t i = 2; i < num_periods; i++)
    {
        LLTrace::Recording& recording = periodic_recording.getPrevRecording(i);
        F64 cur_value = recording.getLastValue(stat);
        const F32Seconds period_duration = recording.getDuration();

        if (last_value != cur_value)
        {
            // A change only counts as rapid if it lands close behind the one
            // before it, so the gap has to accumulate over the periods between
            // the two -- starting with the one the earlier change landed in.
            if (time_since_value_changed < RAPID_CHANGE_THRESHOLD) num_rapid_changes++;
            time_since_value_changed = (F32Seconds)0;
        }
        time_since_value_changed += period_duration;
        last_value = cur_value;

        elapsed_time += period_duration;
        if (elapsed_time > time_period) break;
    }

    return num_rapid_changes;
}

void LLStatBar::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLTrace::PeriodicRecording& frame_recording = LLTrace::get_frame_recording();
    LLTrace::Recording& last_frame_recording = frame_recording.getLastRecording();

    std::string unit_label;
    F32         current         = 0,
                min             = 0,
                max             = 0,
                mean            = 0,
                display_value   = 0;
    S32         num_frames      = mDisplayHistory
                                ? mNumHistoryFrames
                                : mNumShortHistoryFrames;
    S32         num_rapid_changes = 0;
    S32         decimal_digits = mDecimalDigits;

    switch(mStatType)
    {
    case STAT_COUNT:
        {
            const LLTrace::StatType<LLTrace::CountAccumulator>& count_stat = *mStat.countStatp;

            unit_label    = std::string(count_stat.getUnitLabel()) + "/s";
            current       = (F32)last_frame_recording.getPerSec(count_stat);
            min           = (F32)frame_recording.getPeriodMinPerSec(count_stat, num_frames);
            max           = (F32)frame_recording.getPeriodMaxPerSec(count_stat, num_frames);
            mean          = (F32)frame_recording.getPeriodMeanPerSec(count_stat, num_frames);
            if (mShowMedian)
            {
                display_value = (F32)frame_recording.getPeriodMedianPerSec(count_stat, num_frames);
            }
            else
            {
                display_value = mean;
            }
        }
        break;
    case STAT_EVENT:
        {
            const LLTrace::StatType<LLTrace::EventAccumulator>& event_stat = *mStat.eventStatp;

            unit_label        = mUnitLabel.empty() ? event_stat.getUnitLabel() : mUnitLabel;
            current           = (F32)last_frame_recording.getLastValue(event_stat);
            min               = (F32)frame_recording.getPeriodMin(event_stat, num_frames);
            max               = (F32)frame_recording.getPeriodMax(event_stat, num_frames);
            mean              = (F32)frame_recording.getPeriodMean(event_stat, num_frames);
            display_value     = mean;
        }
        break;
    case STAT_SAMPLE:
        {
            const LLTrace::StatType<LLTrace::SampleAccumulator>& sample_stat = *mStat.sampleStatp;

            unit_label        = mUnitLabel.empty() ? sample_stat.getUnitLabel() : mUnitLabel;
            current           = (F32)last_frame_recording.getLastValue(sample_stat);
            min               = (F32)frame_recording.getPeriodMin(sample_stat, num_frames);
            max               = (F32)frame_recording.getPeriodMax(sample_stat, num_frames);
            mean              = (F32)frame_recording.getPeriodMean(sample_stat, num_frames);
            num_rapid_changes = calc_num_rapid_changes(frame_recording, sample_stat, RAPID_CHANGE_WINDOW);

            if (mShowMedian)
            {
                display_value = (F32)frame_recording.getPeriodMedian(sample_stat, num_frames);
            }
            else if (num_rapid_changes / RAPID_CHANGE_WINDOW.value() > MAX_RAPID_CHANGES_PER_SEC)
            {
                display_value = mean;
            }
            else
            {
                display_value = current;
                // always display current value, don't rate limit
                mLastDisplayValue = current;
                if (is_approx_equal((F32)(S32)display_value, display_value))
                {
                    decimal_digits = 0;
                }
            }
        }
        break;
    default:
        break;
    }

    // Reserve a full text line at the top for the label/value so descenders
    // ('g', 'y', 'p', 'q') clear the bar below instead of spilling into it, and
    // (for VERTICAL bars) a tick mark plus a text line at the bottom so the
    // tick labels aren't cut off. Both reserves track the live font metrics;
    // the old fixed 15/20px were tuned for a shorter monospace face.
    LLFontGL* font = LLFontGL::getFontMonospace();
    const S32 text_height       = font->getLineHeight() + STAT_BAR_TEXT_VPAD;
    const S32 tick_label_height = STAT_BAR_TICK_LENGTH + text_height;

    LLRect bar_rect;
    if (mOrientation == HORIZONTAL)
    {
        bar_rect.mTop    = llmax(STAT_BAR_MIN_BAR, getRect().getHeight() - text_height);
        bar_rect.mLeft   = 0;
        bar_rect.mRight  = getRect().getWidth() - 40;
        bar_rect.mBottom = llmin(bar_rect.mTop - STAT_BAR_MIN_BAR, 0);
    }
    else // VERTICAL
    {
        bar_rect.mTop    = llmax(STAT_BAR_MIN_BAR, getRect().getHeight() - text_height);
        bar_rect.mLeft   = 0;
        bar_rect.mRight  = getRect().getWidth();
        bar_rect.mBottom = llmin(bar_rect.mTop - STAT_BAR_MIN_BAR, tick_label_height);
    }

    mCurMaxBar = LLSmoothInterpolation::lerp(mCurMaxBar, mTargetMaxBar, 0.05f);
    mCurMinBar = LLSmoothInterpolation::lerp(mCurMinBar, mTargetMinBar, 0.05f);

    // rate limited updates
    if (mLastDisplayValueTimer.getElapsedTimeF32() < MEAN_VALUE_UPDATE_TIME)
    {
        display_value = mLastDisplayValue;
    }
    else
    {
        mLastDisplayValueTimer.reset();
    }
    mLastDisplayValue = display_value;

    // All of the bar's geometry goes into one batch and every string is drawn
    // after it closes. Untextured geometry and glyphs cannot share a draw --
    // each rect has to unbind the font atlas and each string has to bind it
    // back -- so interleaving them costs a draw call per switch. Kept in the
    // painter order the bar has always had: ticks under the background, then
    // the min/max span, the samples, and the mean line on top.
    tick_label_list_t tick_labels;
    U32               num_tick_labels = 0;

    if (mDisplayBar && mStat.valid)
    {
        LLGLSUIDefault gls_ui;
        gGL.getTextureSlot(0)->unbind();

        F32 value_scale;
        if (mCurMaxBar == mCurMinBar)
        {
            value_scale = 0.f;
        }
        else
        {
            value_scale = (mOrientation == HORIZONTAL)
                ? (bar_rect.getHeight())/(mCurMaxBar - mCurMinBar)
                : (bar_rect.getWidth())/(mCurMaxBar - mCurMinBar);
        }

        updateBarRange(min, max);

        gGL.begin(LLRender::TRIANGLES);

        num_tick_labels = drawTickMarks(value_scale, bar_rect, tick_labels);

        // draw background bar.
        batchRect(bar_rect.mLeft, bar_rect.mTop, bar_rect.mRight, bar_rect.mBottom, LLColor4(0.f, 0.f, 0.f, 0.25f));

        // draw values
        if (!llisnan(display_value) && frame_recording.getNumRecordedPeriods() != 0)
        {
            // draw min and max
            S32 begin = (S32) ((min - mCurMinBar) * value_scale);

            if (begin < 0)
            {
                begin = 0;
            }

            S32 end = (S32) ((max - mCurMinBar) * value_scale);
            if (mOrientation == HORIZONTAL)
            {
                batchRect(bar_rect.mLeft, end, bar_rect.mRight, begin, LLColor4(1.f, 0.f, 0.f, 0.25f));
            }
            else // VERTICAL
            {
                batchRect(begin, bar_rect.mTop, end, bar_rect.mBottom, LLColor4(1.f, 0.f, 0.f, 0.25f));
            }

            F32 span = (mOrientation == HORIZONTAL)
                    ? (F32)(bar_rect.getWidth())
                    : (F32)(bar_rect.getHeight());

            if (mDisplayHistory && mStat.valid)
            {
                const S32 num_values = static_cast<S32>(frame_recording.getNumRecordedPeriods()) - 1;
                const F32 max_x = (F32)getRect().getWidth();
                const F32 max_y = (F32)getRect().getHeight();
                F32 min_value = 0.f,
                    max_value = 0.f;

                gGL.color4f(1.f, 0.f, 0.f, 1.f);
                const S32 max_frame = llmin(num_frames, num_values);
                U32 num_samples = 0;
                for (S32 i = 1; i <= max_frame; i++)
                {
                    F32 offset = ((F32)i / (F32)num_frames) * span;
                    LLTrace::Recording& recording = frame_recording.getPrevRecording(i);

                    switch(mStatType)
                    {
                        case STAT_COUNT:
                            min_value       = (F32)recording.getPerSec(*mStat.countStatp);
                            max_value       = min_value;
                            num_samples     = recording.getSampleCount(*mStat.countStatp);
                            break;
                        case STAT_EVENT:
                            min_value       = (F32)recording.getMin(*mStat.eventStatp);
                            max_value       = (F32)recording.getMax(*mStat.eventStatp);
                            num_samples     = recording.getSampleCount(*mStat.eventStatp);
                            break;
                        case STAT_SAMPLE:
                            min_value       = (F32)recording.getMin(*mStat.sampleStatp);
                            max_value       = (F32)recording.getMax(*mStat.sampleStatp);
                            num_samples     = recording.getSampleCount(*mStat.sampleStatp);
                            break;
                        default:
                            break;
                    }

                    if (!num_samples) continue;

                    // These go in as vertices rather than through batchRect, so
                    // they take the same clamp to the widget it applies. min and
                    // max come straight off the stat and are what needs it.
                    F32 min = (min_value  - mCurMinBar) * value_scale;
                    F32 max = llmax(min + 1, (max_value - mCurMinBar) * value_scale);
                    if (mOrientation == HORIZONTAL)
                    {
                        const F32 lo = llclamp(min, 0.f, max_y);
                        const F32 hi = llclamp(max, 0.f, max_y);
                        const F32 x0 = llclamp((F32)bar_rect.mRight - offset,       0.f, max_x);
                        const F32 x1 = llclamp((F32)bar_rect.mRight - offset - 1.f, 0.f, max_x);

                        gGL.vertex2f(x0, hi);
                        gGL.vertex2f(x0, lo);
                        gGL.vertex2f(x1, lo);

                        gGL.vertex2f(x0, hi);
                        gGL.vertex2f(x1, lo);
                        gGL.vertex2f(x1, hi);
                    }
                    else
                    {
                        const F32 lo = llclamp(min, 0.f, max_x);
                        const F32 hi = llclamp(max, 0.f, max_x);
                        const F32 y0 = llclamp((F32)bar_rect.mBottom + offset,       0.f, max_y);
                        const F32 y1 = llclamp((F32)bar_rect.mBottom + offset + 1.f, 0.f, max_y);

                        gGL.vertex2f(lo, y1);
                        gGL.vertex2f(lo, y0);
                        gGL.vertex2f(hi, y0);

                        gGL.vertex2f(lo, y1);
                        gGL.vertex2f(hi, y0);
                        gGL.vertex2f(hi, y1);
                    }
                }
            }
            else
            {
                S32 begin = (S32) ((current - mCurMinBar) * value_scale) - 1;
                S32 end = (S32) ((current - mCurMinBar) * value_scale) + 1;
                // draw current
                if (mOrientation == HORIZONTAL)
                {
                    batchRect(bar_rect.mLeft, end, bar_rect.mRight, begin, LLColor4(1.f, 0.f, 0.f, 1.f));
                }
                else
                {
                    batchRect(begin, bar_rect.mTop, end, bar_rect.mBottom, LLColor4(1.f, 0.f, 0.f, 1.f));
                }
            }

            // draw mean bar
            {
                const S32 begin = (S32) ((mean - mCurMinBar) * value_scale) - 1;
                const S32 end = (S32) ((mean - mCurMinBar) * value_scale) + 1;
                if (mOrientation == HORIZONTAL)
                {
                    batchRect(bar_rect.mLeft - 2, begin, bar_rect.mRight + 2, end, LLColor4(0.f, 1.f, 0.f, 1.f));
                }
                else
                {
                    batchRect(begin, bar_rect.mTop + 2, end, bar_rect.mBottom - 2, LLColor4(0.f, 1.f, 0.f, 1.f));
                }
            }
        }

        gGL.end();
    }

    drawLabelAndValue(display_value, unit_label, bar_rect, decimal_digits);
    drawTickLabels(tick_labels, num_tick_labels);

    LLView::draw();
}

void LLStatBar::setStat(const std::string& stat_name)
{
    using namespace LLTrace;

    if (auto count_stat = StatType<CountAccumulator>::getInstance(stat_name))
    {
        mStat.countStatp = count_stat.get();
        mStatType = STAT_COUNT;
    }
    else if (auto event_stat = StatType<EventAccumulator>::getInstance(stat_name))
    {
        mStat.eventStatp = event_stat.get();
        mStatType = STAT_EVENT;
    }
    else if (auto sample_stat = StatType<SampleAccumulator>::getInstance(stat_name))
    {
        mStat.sampleStatp = sample_stat.get();
        mStatType = STAT_SAMPLE;
    }
    else
    {
        // A name that resolves to nothing leaves the bar showing no stat rather
        // than continuing to report whichever one it was bound to before.
        mStat.valid = nullptr;
        mStatType = STAT_NONE;
    }
}

void LLStatBar::setRange(F32 bar_min, F32 bar_max)
{
    mTargetMinBar       = llmin(bar_min, bar_max);
    mTargetMaxBar       = llmax(bar_min, bar_max);
    mFloatingTargetMinBar = mTargetMinBar;
    mFloatingTargetMaxBar = mTargetMaxBar;
    mTickSpacing    = calc_tick_value(mTargetMinBar, mTargetMaxBar);
}

LLRect LLStatBar::getRequiredRect()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLRect rect;

    // Heights are derived from the live monospace line height so rows stay tall
    // enough for the current font (see the layout reserves used in draw()).
    const S32 text_height = LLFontGL::getFontMonospace()->getLineHeight() + STAT_BAR_TEXT_VPAD;

    if (mDisplayBar)
    {
        if (mDisplayHistory)
        {
            rect.mTop = mMaxHeight;
        }
        else if (mOrientation == VERTICAL)
        {
            // Partial expand: value/label line on top, a short bar, and a row of
            // tick labels (a tick mark + a text line) along the bottom.
            rect.mTop = text_height + STAT_BAR_MIN_BAR + (STAT_BAR_TICK_LENGTH + text_height);
        }
        else
        {
            // Horizontal: value/label line plus the bar; tick labels sit to the
            // right of the bar, so no bottom row is needed.
            rect.mTop = llmax(40, text_height + STAT_BAR_MIN_BAR);
        }
    }
    else
    {
        // Collapsed: just the label/value line, tall enough for its descenders.
        rect.mTop = text_height;
    }
    return rect;
}

void LLStatBar::drawLabelAndValue( F32 value, std::string &label, LLRect &bar_rect, S32 decimal_digits )
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLFontGL* font = LLFontGL::getFontMonospace();

    static std::string na_string = LLTrans::getString("na");

    text_buf_t       value_buf;
    std::string_view value_str(na_string);
    if (!llisnan(value))
    {
        const auto result = fmt::format_to_n(value_buf.data(), value_buf.size(),
                                             "{:10.{}f} {}", value, (int)decimal_digits, label);
        value_str = bufView(value_buf, result.size);
    }

    // The label is left-aligned from the left edge and the value is right-aligned
    // at the bar's right edge, both on the same line. Reserve the value's width so
    // a long label is ellipsized instead of overprinting the value.
    static const S32 LABEL_VALUE_GAP = 4;
    const S32 value_width = font->getWidth(value_str);
    const S32 label_max_pixels = llmax(0, bar_rect.mRight - value_width - LABEL_VALUE_GAP);

    font->renderBytes(mLabel.getString(), 0, 0.f, (F32)getRect().getHeight(), LLColor4(1.f, 1.f, 1.f, 1.f),
        LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
        S32_MAX, label_max_pixels, NULL, /*use_ellipses=*/true);

    // Draw the current value (right-aligned at the bar's right edge for both
    // orientations). Capped at the width it has to run back through, so a value
    // too wide for the widget is ellipsized rather than drawn off the left edge.
    font->renderBytes(value_str, 0, (F32)bar_rect.mRight, (F32)getRect().getHeight(),
        LLColor4(1.f, 1.f, 1.f, 1.f),
        LLFontGL::RIGHT, LLFontGL::TOP, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
        S32_MAX, bar_rect.mRight, NULL, /*use_ellipses=*/true);
}

void LLStatBar::updateBarRange( F32 min, F32 max )
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if (!llisnan(min) && (mAutoScaleMax || mAutoScaleMin))
    {
        F32 u = LLSmoothInterpolation::getInterpolant(10.f);
        mFloatingTargetMinBar = llmin(min, lerp(mFloatingTargetMinBar, min, u));
        mFloatingTargetMaxBar = llmax(max, lerp(mFloatingTargetMaxBar, max, u));
        F32 range_min = mAutoScaleMin ? mFloatingTargetMinBar : mTargetMinBar;
        F32 range_max = mAutoScaleMax ? mFloatingTargetMaxBar : mTargetMaxBar;
        F32 tick_value = 0.f;
        calc_auto_scale_range(range_min, range_max, tick_value);
        if (mAutoScaleMin) { mTargetMinBar = range_min; }
        if (mAutoScaleMax) { mTargetMaxBar = range_max; }
        if (mAutoScaleMin && mAutoScaleMax)
        {
            mTickSpacing = tick_value;
        }
        else
        {
            mTickSpacing = calc_tick_value(mTargetMinBar, mTargetMaxBar);
        }
    }
}

U32 LLStatBar::drawTickMarks( F32 value_scale, LLRect &bar_rect, tick_label_list_t& labels )
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    U32 num_labels = 0;

    if (mTickSpacing <= 0.f || value_scale <= 0.f)
    {
        return num_labels;
    }

    // start counting from actual min, not current, animating min, so that ticks don't float between numbers
    // ensure ticks always hit 0
    S32 last_tick = S32_MIN;
    S32 last_label = S32_MIN;

    const S32 MIN_TICK_SPACING  = mOrientation == HORIZONTAL ? 20 : 30;
    const S32 MIN_LABEL_SPACING = mOrientation == HORIZONTAL ? 30 : 60;
    const S32 TICK_LENGTH = STAT_BAR_TICK_LENGTH;
    const S32 TICK_WIDTH = 1;

    F32 start = mCurMinBar < 0.f
        ? llceil(-mCurMinBar / mTickSpacing) * -mTickSpacing
        : 0.f;

    // A tick spacing negligible beside the values it steps through stops
    // advancing them in F32, so the walk needs a bound of its own to fall
    // back on when passing mCurMaxBar can never happen.
    const S32 MAX_TICKS = 1024;

    // One tick is drawn past mCurMaxBar so part of its label stays visible.
    // The test is latched here, before the spacing test below can skip the
    // rest of the body, so a run of too-close ticks cannot outlive it.
    bool past_max = false;
    F32 tick_value = start;
    for (S32 tick_idx = 0; !past_max && tick_idx < MAX_TICKS; tick_idx++, tick_value += mTickSpacing)
    {
        past_max = tick_value > mCurMaxBar;

        // clamp to S32_MAX / 2 to avoid floating point to integer overflow resulting in S32_MIN
        const S32 tick_begin = llfloor(llmin((F32)(S32_MAX / 2), (tick_value - mCurMinBar)*value_scale));
        const S32 tick_end = tick_begin + TICK_WIDTH;
        if (tick_begin < last_tick + MIN_TICK_SPACING)
        {
            continue;
        }
        last_tick = tick_begin;

        // Past the collection capacity a tick keeps its mark and loses its
        // label, which is the same thing the spacing rule does to it.
        const bool draw_label = tick_begin > last_label + MIN_LABEL_SPACING
                             && num_labels < labels.size();
        if (mOrientation == HORIZONTAL)
        {
            if (draw_label)
            {
                batchRect(bar_rect.mLeft, tick_end, bar_rect.mRight - TICK_LENGTH, tick_begin, LLColor4(1.f, 1.f, 1.f, 0.25f));

                TickLabel& label = labels[num_labels++];
                label.mLength = (U32)formatTickLabel(label.mText, tick_value, mDecimalDigits).size();
                label.mX      = (F32)bar_rect.mRight;
                label.mY      = (F32)tick_begin;
                last_label    = tick_begin;
            }
            else
            {
                batchRect(bar_rect.mLeft, tick_end, bar_rect.mRight - TICK_LENGTH/2, tick_begin, LLColor4(1.f, 1.f, 1.f, 0.1f));
            }
        }
        else
        {
            if (draw_label)
            {
                batchRect(tick_begin, bar_rect.mTop, tick_end, bar_rect.mBottom - TICK_LENGTH, LLColor4(1.f, 1.f, 1.f, 0.25f));

                TickLabel& label = labels[num_labels++];
                const std::string_view text = formatTickLabel(label.mText, tick_value, mDecimalDigits);
                label.mLength = (U32)text.size();

                // Slide the label left as it nears the far edge, so the last one
                // ends at the bar's end instead of running past it.
                const S32 tick_label_width = LLFontGL::getFontMonospace()->getWidthBytes(text, 0, S32_MAX);
                const S32 label_pos = tick_begin - ll_round((F32)tick_label_width * ((F32)tick_begin / (F32)bar_rect.getWidth()));
                label.mX   = (F32)label_pos;
                label.mY   = (F32)(bar_rect.mBottom - TICK_LENGTH);
                last_label = label_pos;
            }
            else
            {
                batchRect(tick_begin, bar_rect.mTop, tick_end, bar_rect.mBottom - TICK_LENGTH/2, LLColor4(1.f, 1.f, 1.f, 0.1f));
            }
        }
    }

    return num_labels;
}

void LLStatBar::drawTickLabels( const tick_label_list_t& labels, U32 num_labels )
{
    if (!num_labels)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // Horizontal ticks run up the left edge and label to the right of the bar;
    // vertical ticks run along the bottom and label beneath it.
    const LLFontGL::VAlign valign = (mOrientation == HORIZONTAL) ? LLFontGL::VCENTER : LLFontGL::TOP;
    LLFontGL* font  = LLFontGL::getFontMonospace();
    const S32 right = getRect().getWidth();

    for (U32 i = 0; i < num_labels; i++)
    {
        const TickLabel& label = labels[i];
        // Each label is capped at the room left between it and the widget's
        // edge, which is what the scissor used to take care of.
        const S32 max_pixels = llmax(0, right - (S32)label.mX);
        font->renderBytes(std::string_view(label.mText.data(), label.mLength), 0, label.mX, label.mY,
            LLColor4(1.f, 1.f, 1.f, 0.5f),
            LLFontGL::LEFT, valign, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
            S32_MAX, max_pixels, NULL, /*use_ellipses=*/true);
    }
}

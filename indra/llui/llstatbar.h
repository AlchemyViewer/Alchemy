/**
 * @file llstatbar.h
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

#ifndef LL_LLSTATBAR_H
#define LL_LLSTATBAR_H

#include "llview.h"
#include "llframetimer.h"
#include "lltracerecording.h"

#include <array>
#include <string_view>

class LLStatBar : public LLView
{
public:

    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Optional<std::string>   label,
                                unit_label;

        Optional<F32>           bar_min,
                                bar_max,
                                tick_spacing;

        Optional<bool>          show_bar,
                                show_history,
                                scale_range,
                                show_median; // default is mean

        Optional<S32>           decimal_digits,
                                num_frames,
                                num_frames_short,
                                max_height;
        Optional<std::string>   stat;
        Optional<EOrientation>  orientation;

        Params();
    };
    LLStatBar(const Params&);

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool handleHover(S32 x, S32 y, MASK mask);

    void setStat(const std::string& stat_name);

    void setRange(F32 bar_min, F32 bar_max);
    void getRange(F32& bar_min, F32& bar_max) const { bar_min = mTargetMinBar; bar_max = mTargetMaxBar; }

    /*virtual*/ LLRect getRequiredRect();   // Return the height of this object, given the set options.

    // The spacing that lands the most tick marks on round numbers across
    // [min, max]. Zero when the range is empty.
    static F32  calcTickValue(F32 min, F32 max);

    // Widens [min, max] to a range that contains zero and ends on a round
    // number, and reports the tick spacing that suits the result.
    static void calcAutoScaleRange(F32& min, F32& max, F32& tick);

private:
    // One formatted number. The value line and the tick labels are rebuilt
    // every frame, so they are formatted in place and handed to the font as a
    // view rather than through the allocator.
    typedef std::array<char, 64> text_buf_t;

    static std::string_view bufView(const text_buf_t& buf, size_t written);
    static std::string_view formatTickLabel(text_buf_t& buf, F32 tick_value, S32 decimal_digits);

    // A tick's label, collected while the bar geometry is being batched and
    // drawn once that batch closes. Text bound between two rects splits the
    // batch on both sides of itself, and a labelled tick would do that several
    // times per bar. The capacity is far past what the label spacing allows
    // across a bar of any width the UI can give one.
    struct TickLabel
    {
        text_buf_t mText;
        U32        mLength;
        F32        mX;
        F32        mY;
    };
    typedef std::array<TickLabel, 24> tick_label_list_t;

    void batchRect( S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color ) const;
    void drawLabelAndValue( F32 mean, const std::string &unit_label, LLRect &bar_rect, S32 decimal_digits );
    void updateBarRange( F32 min, F32 max );
    U32  drawTickMarks( F32 value_scale, LLRect &bar_rect, tick_label_list_t& labels );
    void drawTickLabels( const tick_label_list_t& labels, U32 num_labels );

    F32          mTargetMinBar,
                 mTargetMaxBar,
                 mFloatingTargetMinBar,
                 mFloatingTargetMaxBar,
                 mCurMaxBar,
                 mCurMinBar,
                 mTickSpacing;
    S32          mDecimalDigits,
                 mNumHistoryFrames,
                 mNumShortHistoryFrames;
    S32          mMaxHeight;
    EOrientation mOrientation;
    F32          mLastDisplayValue;
    LLFrameTimer mLastDisplayValueTimer;

    enum
    {
        STAT_NONE,
        STAT_COUNT,
        STAT_EVENT,
        STAT_SAMPLE
    } mStatType;

    union
    {
        void*                                                   valid;
        const LLTrace::StatType<LLTrace::CountAccumulator>*     countStatp;
        const LLTrace::StatType<LLTrace::EventAccumulator>*     eventStatp;
        const LLTrace::StatType<LLTrace::SampleAccumulator>*    sampleStatp;
    } mStat;

    LLUIString   mLabel;
    std::string  mUnitLabel;

    // The unit the value line prints beside the number. It follows from the
    // stat and this widget's own override, and neither moves between frames.
    std::string  mDisplayUnitLabel;

    // What the last walk over the history window produced. Each of these costs
    // a pass over every recorded period, and the line they feed only changes on
    // MEAN_VALUE_UPDATE_TIME.
    F32          mCachedMin,
                 mCachedMax,
                 mCachedMean;
    S32          mCachedDecimalDigits;
    bool         mCachedShowsCurrent,   // value line tracks `current`, frame to frame
                 mHaveAggregates;

    // The value line as last formatted, kept so its width is measured only when
    // the bytes change. Measuring is a shaping pass, and the width is needed
    // every frame to reserve room for the label beside it.
    text_buf_t   mValueText;
    U32          mValueTextLength;
    S32          mValueWidth;

    // Stands in for a value that is not a number. Resolved once: the row asks
    // for it on every draw, and it is the same string every time.
    std::string  mNotApplicable;

    bool         mDisplayBar,           // Display the bar graph.
                 mDisplayHistory,
                 mShowMedian,
                 mAutoScaleMax,
                 mAutoScaleMin;
};

#endif

/**
 * @file windgen.h
 * @brief Templated wind noise generation
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
#ifndef WINDGEN_H
#define WINDGEN_H

#include "llcommon.h"
#include "llrand.h"

template <class MIXBUFFERFORMAT_T>
class LLWindGen
{
public:
    // Resonant low-pass filter bandwidth (Hz). Sets the Q of the
    // resonator that sculpts the pinked noise into the audible wind
    // sweep — narrower = more tonal "whistle"; wider = more
    // broadband rush. 50 Hz is the historical tuning and feels
    // right; left as a class constant so it's both findable and
    // tweakable in one place.
    static constexpr F32 kFilterBandwidthHz = 50.f;

    // Per-sample exponential smoothing coefficient applied to the
    // target/current parameters (gain, freq, pan). At 44.1 kHz the
    // time constant is ~22 ms; the same numeric coefficient at
    // higher sample rates means proportionally faster (in samples)
    // smoothing but no audible difference at typical device rates
    // (44.1 → 48 kHz). If the device rate ever diverges enough that
    // this matters, recompute from a time-constant in seconds.
    static constexpr F32 kCoefficientSmoothing = 0.999f;

    // Random-walk gust LFO design. Off when mGustinessDepth == 0
    // (the inner loop short-circuits the modulation entirely so the
    // synthesised sound is bit-exact to the pre-feature path). When
    // enabled, a new random target in [-1, 1] is picked every
    // kGustTargetIntervalSec seconds and mGustValue interpolates
    // toward it — a slow, audibly-coherent gust feel. Depth scales
    // the modulation amplitude (depth=1 means ±100 % gain wobble).
    static constexpr F32 kGustTargetIntervalSec = 1.5f;

    LLWindGen(const U32 sample_rate = 44100) :
        mTargetGain(0.f),
        mTargetFreq(100.f),
        mTargetPanGainR(0.5f),
        mInputSamplingRate(sample_rate),
        mSubSamples(2),
        mFilterBandWidth(kFilterBandwidthHz),
        mBuf0(0.0f),
        mBuf1(0.0f),
        mBuf2(0.0f),
        mY0(0.0f),
        mY1(0.0f),
        mCurrentGain(0.f),
        mCurrentFreq(100.f),
        mCurrentPanGainR(0.5f),
        mLastSample(0.f),
        mGustinessDepth(0.f),
        mGustValue(0.f),
        mGustTarget(0.f),
        mGustSamplesUntilNewTarget(0)
    {
        mSamplePeriod = (F32)mSubSamples / (F32)mInputSamplingRate;
        mB2 = expf(-F_TWO_PI * mFilterBandWidth * mSamplePeriod);
    }

    const U32 getInputSamplingRate() { return mInputSamplingRate; }
    const F32 getNextSample();
    const F32 getClampedSample(bool clamp, F32 sample);

    // Optional slow random-walk modulator on the smoothed gain.
    // 0 disables it entirely (default); 1 means ±100 % gain wobble
    // — the inner loop branches on depth==0 so the no-feature path
    // is bit-exact to the historical synthesis.
    void setGustinessDepth(F32 depth)
    {
        if (depth < 0.f) depth = 0.f;
        if (depth > 1.f) depth = 1.f;
        mGustinessDepth = depth;
    }

    // newbuffer = the buffer passed from the previous DSP unit.
    // numsamples = length in samples-per-channel at this mix time.
    // NOTE: generates L/R interleaved stereo
    MIXBUFFERFORMAT_T* windGenerate(MIXBUFFERFORMAT_T *newbuffer, int numsamples)
    {
        MIXBUFFERFORMAT_T *cursamplep = newbuffer;

        // Filter coefficients
        F32 a0 = 0.0f, b1 = 0.0f;

        // No need to clip at normal volumes
        bool clip = mCurrentGain > 2.0f;

        bool interp_freq = false;

        //if the frequency isn't changing much, we don't need to interpolate in the inner loop
        if (llabs(mTargetFreq - mCurrentFreq) < (mCurrentFreq * 0.112f))
        {
            // calculate resonant filter coefficients
            mCurrentFreq = mTargetFreq;
            b1 = (-4.0f * mB2) / (1.0f + mB2) * cosf(F_TWO_PI * (mCurrentFreq * mSamplePeriod));
            a0 = (1.0f - mB2) * sqrtf(1.0f - (b1 * b1) / (4.0f * mB2));
        }
        else
        {
            interp_freq = true;
        }

        // Pre-compute the gust LFO step. Each output sample we drift
        // mGustValue toward mGustTarget by this fraction; when the
        // sample-countdown elapses we pick a fresh target. The total
        // interval (samples_until_new_target * step) is calibrated to
        // kGustTargetIntervalSec at the current sample rate.
        const S32 kGustTargetSamples =
            llmax(1, (S32)(kGustTargetIntervalSec * (F32)mInputSamplingRate / (F32)mSubSamples));
        const F32 kGustInterpStep = 1.f / (F32)kGustTargetSamples;

        while (numsamples)
        {
            F32 next_sample;

            // Start with white noise.
            //
            // The expression in getNextSample() is "fragile" because
            // it is a single-statement composition of (rand-based)
            // value scaling, mid-range offset, and float→integer
            // promotion in a deliberate order chosen so the result
            // sits symmetrically around zero with the intended
            // amplitude. Reordering the operands or splitting the
            // expression across statements can introduce subtle
            // floating-point cancellation differences that bias the
            // DC component of the noise, which then propagates
            // through the pinking + resonant filters as a slow DC
            // creep audible as a "click" at the start of wind onset.
            // If you need to refactor it, verify with a long-running
            // Σx accumulator that the bias stays at zero.
            next_sample = getNextSample();

            // Apply a pinking filter
            // Magic numbers taken from PKE method at http://www.firstpr.com.au/dsp/pink-noise/
            mBuf0 = mBuf0 * 0.99765f + next_sample * 0.0990460f;
            mBuf1 = mBuf1 * 0.96300f + next_sample * 0.2965164f;
            mBuf2 = mBuf2 * 0.57000f + next_sample * 1.0526913f;

            next_sample = mBuf0 + mBuf1 + mBuf2 + next_sample * 0.1848f;

            if (interp_freq)
            {
                // calculate and interpolate resonant filter coefficients
                mCurrentFreq = (kCoefficientSmoothing * mCurrentFreq) + ((1.f - kCoefficientSmoothing) * mTargetFreq);
                b1 = (-4.0f * mB2) / (1.0f + mB2) * cosf(F_TWO_PI * (mCurrentFreq * mSamplePeriod));
                a0 = (1.0f - mB2) * sqrtf(1.0f - (b1 * b1) / (4.0f * mB2));
            }

            // Apply a resonant low-pass filter on the pink noise
            next_sample = a0 * next_sample - b1 * mY0 - mB2 * mY1;
            mY1 = mY0;
            mY0 = next_sample;

            mCurrentGain = (kCoefficientSmoothing * mCurrentGain) + ((1.f - kCoefficientSmoothing) * mTargetGain);
            mCurrentPanGainR = (kCoefficientSmoothing * mCurrentPanGainR) + ((1.f - kCoefficientSmoothing) * mTargetPanGainR);

            // For a 3dB pan law use:
            // next_sample *= mCurrentGain * ((mCurrentPanGainR*(mCurrentPanGainR-1)*1.652+1.413);
            next_sample *= mCurrentGain;

            // Slow gust modulator. When mGustinessDepth==0 we never
            // touch mGustValue / mGustTarget so the synthesis stays
            // bit-identical to the historical pre-feature path. When
            // enabled, mGustValue ramps toward a new random target
            // every kGustTargetIntervalSec seconds, modulating the
            // amplitude — feels like natural gust variation rather
            // than the steady drone we've had until now.
            if (mGustinessDepth > 0.f)
            {
                if (--mGustSamplesUntilNewTarget <= 0)
                {
                    mGustTarget = ll_frand() * 2.f - 1.f;  // [-1, 1]
                    mGustSamplesUntilNewTarget = kGustTargetSamples;
                }
                mGustValue += (mGustTarget - mGustValue) * kGustInterpStep;
                next_sample *= 1.f + mGustValue * mGustinessDepth;
            }

            // delta is used to interpolate between synthesized samples
            F32 delta = (next_sample - mLastSample) / (F32)mSubSamples;

            // Fill the audio buffer, clipping if necessary
            for (U8 i=mSubSamples; i && numsamples; --i, --numsamples)
            {
                mLastSample = mLastSample + delta;
                MIXBUFFERFORMAT_T sample_right = (MIXBUFFERFORMAT_T)getClampedSample(clip, mLastSample * mCurrentPanGainR);
                MIXBUFFERFORMAT_T sample_left  = (MIXBUFFERFORMAT_T)getClampedSample(clip, mLastSample - (F32)sample_right);

                *cursamplep = sample_left;
                ++cursamplep;
                *cursamplep = sample_right;
                ++cursamplep;
            }
        }

        return newbuffer;
    }

public:
    F32 mTargetGain;
    F32 mTargetFreq;
    F32 mTargetPanGainR;

private:
    U32 mInputSamplingRate;
    U8  mSubSamples;
    F32 mSamplePeriod;
    F32 mFilterBandWidth;
    F32 mB2;

    F32 mBuf0;
    F32 mBuf1;
    F32 mBuf2;
    F32 mY0;
    F32 mY1;

    F32 mCurrentGain;
    F32 mCurrentFreq;
    F32 mCurrentPanGainR;
    F32 mLastSample;

    // Gust LFO state. mGustinessDepth gates the whole feature
    // (0 = off, bit-exact to historical synth). mGustValue is the
    // smoothed modulator; mGustTarget is what it's interpolating
    // toward; the sample counter triggers a fresh random target
    // every kGustTargetIntervalSec.
    F32 mGustinessDepth;
    F32 mGustValue;
    F32 mGustTarget;
    S32 mGustSamplesUntilNewTarget;
};

template<class T> inline const F32 LLWindGen<T>::getNextSample() { return (F32)rand() * (1.0f / (F32)(RAND_MAX / (U16_MAX / 8))) + (F32)(S16_MIN / 8); }
template<> inline const F32 LLWindGen<F32>::getNextSample() { return ll_frand()-.5f; }
template<class T> inline const F32 LLWindGen<T>::getClampedSample(bool clamp, F32 sample) { return clamp ? (F32)llclamp((S32)sample,(S32)S16_MIN,(S32)S16_MAX) : sample; }
template<> inline const F32 LLWindGen<F32>::getClampedSample(bool clamp, F32 sample) { return sample; }

#endif

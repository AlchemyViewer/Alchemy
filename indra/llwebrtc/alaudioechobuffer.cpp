/**
 * @file alaudioechobuffer.cpp
 * @brief Capture audio held for playback during device preview
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
 *
 * $/LicenseInfo$
 */

#include "alaudioechobuffer.h"

#include <algorithm>

namespace llwebrtc
{

namespace
{
// The processing module works in floats over the int16 range, so a sample
// only has to be clamped, not rescaled.
int16_t toSample(float value)
{
    if (value >= 32767.0f)
    {
        return 32767;
    }
    if (value <= -32768.0f)
    {
        return -32768;
    }
    return static_cast<int16_t>(value);
}
}  // namespace

void ALAudioEchoBuffer::setEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mEnabled.store(enabled, std::memory_order_relaxed);
    mSamples.clear();
    mRead       = 0;
    mFill       = 0;
    mChannels   = 0;
    mSampleRate = 0;
}

void ALAudioEchoBuffer::write(const float* const* channels, size_t channel_count, size_t frames, uint32_t samples_per_sec)
{
    if (!channels || !channel_count || !frames || !samples_per_sec)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);

    if (!mEnabled.load(std::memory_order_relaxed))
    {
        return;
    }

    if (channel_count != mChannels || samples_per_sec != mSampleRate)
    {
        // First block of the preview, or the capture format changed.
        mChannels   = channel_count;
        mSampleRate = samples_per_sec;
        mSamples.assign((samples_per_sec * channel_count * BUFFER_MS) / 1000, 0);
        mRead = 0;
        mFill = 0;
    }

    const size_t capacity = mSamples.size();
    if (!capacity)
    {
        return;
    }

    // Keep the newest audio when more arrives than fits: an echo that lags is
    // worse than one that drops.
    const size_t available = frames * channel_count;
    const size_t count     = std::min(available, capacity);
    const size_t skip      = (available - count) / channel_count;

    size_t write = (mRead + mFill) % capacity;
    for (size_t frame = skip; frame < frames; ++frame)
    {
        for (size_t channel = 0; channel < channel_count; ++channel)
        {
            mSamples[write] = toSample(channels[channel][frame]);
            write           = (write + 1) % capacity;
        }
    }

    if (mFill + count > capacity)
    {
        mRead = (mRead + (mFill + count - capacity)) % capacity;
        mFill = capacity;
    }
    else
    {
        mFill += count;
    }
}

bool ALAudioEchoBuffer::read(int16_t* out, size_t frames, size_t channel_count, uint32_t samples_per_sec)
{
    if (!out || !frames || !channel_count)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mMutex);

    // Nothing is resampled here.  Rendering audio captured at another rate
    // would be heard as noise rather than as the microphone.
    if (!mEnabled.load(std::memory_order_relaxed) || !mChannels || samples_per_sec != mSampleRate)
    {
        return false;
    }

    const size_t capacity = mSamples.size();
    const size_t needed   = frames * mChannels;
    if (!capacity || mFill < needed)
    {
        return false;
    }

    if (channel_count == mChannels)
    {
        for (size_t i = 0; i < needed; ++i)
        {
            out[i] = mSamples[(mRead + i) % capacity];
        }
    }
    else if (mChannels == 1 && channel_count == 2)
    {
        for (size_t frame = 0; frame < frames; ++frame)
        {
            const int16_t sample = mSamples[(mRead + frame) % capacity];
            out[frame * 2]       = sample;
            out[frame * 2 + 1]   = sample;
        }
    }
    else if (mChannels == 2 && channel_count == 1)
    {
        for (size_t frame = 0; frame < frames; ++frame)
        {
            const int32_t left  = mSamples[(mRead + frame * 2) % capacity];
            const int32_t right = mSamples[(mRead + frame * 2 + 1) % capacity];
            out[frame]          = static_cast<int16_t>((left + right) / 2);
        }
    }
    else
    {
        return false;
    }

    mRead = (mRead + needed) % capacity;
    mFill -= needed;
    return true;
}

}  // namespace llwebrtc

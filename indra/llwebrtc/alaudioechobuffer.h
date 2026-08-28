/**
 * @file alaudioechobuffer.h
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

#ifndef AL_AUDIOECHOBUFFER_H
#define AL_AUDIOECHOBUFFER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace llwebrtc
{

// Carries capture audio from the tail of the audio processing chain, where it
// has been echo cancelled, noise suppressed and levelled exactly as it will be
// heard by others, over to playout so it can be rendered back to the user
// while they preview their devices.  Written from the capture thread and read
// from the render thread.
class ALAudioEchoBuffer
{
  public:
    // Discards whatever is held, so a preview never opens on stale audio.
    void setEnabled(bool enabled);

    bool enabled() const { return mEnabled.load(std::memory_order_relaxed); }

    // Capture thread.  Takes deinterleaved samples in the int16 range the
    // audio processing module works in.  Keeps the most recent audio, dropping
    // the oldest once BUFFER_MS is exceeded, so the echo tracks the microphone
    // rather than falling steadily further behind it.
    void write(const float* const* channels, size_t channel_count, size_t frames, uint32_t samples_per_sec);

    // Render thread.  False when nothing usable is held -- too little audio
    // yet, or a rate the echo cannot be mapped from -- and the caller should
    // render silence instead.
    bool read(int16_t* out, size_t frames, size_t channel_count, uint32_t samples_per_sec);

  private:
    static const size_t BUFFER_MS = 120;

    std::atomic<bool>    mEnabled{ false };
    std::mutex           mMutex;
    std::vector<int16_t> mSamples; // interleaved, mChannels wide
    size_t               mRead{ 0 };
    size_t               mFill{ 0 };
    size_t               mChannels{ 0 };
    uint32_t             mSampleRate{ 0 };
};

}  // namespace llwebrtc

#endif  // AL_AUDIOECHOBUFFER_H

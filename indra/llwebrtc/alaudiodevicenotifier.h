/**
 * @file alaudiodevicenotifier.h
 * @brief System audio endpoint change notification
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

#ifndef AL_AUDIODEVICENOTIFIER_H
#define AL_AUDIODEVICENOTIFIER_H

#include <memory>

namespace llwebrtc
{

// Watches the system for audio endpoint changes -- devices arriving or
// leaving, and the default endpoint moving elsewhere.  The audio device
// module reports none of this, so the platform is asked directly.
class ALAudioDeviceNotifier
{
  public:
    class Observer
    {
      public:
        virtual ~Observer() = default;

        // Called from a platform callback thread, which on Windows and macOS
        // is one the audio device module itself may be holding.  Hand the work
        // to another thread rather than touching the device module here.
        virtual void OnDevicesUpdated() = 0;
    };

    // Returns nullptr where the platform offers no notification source, or
    // where it could not be reached.  Callers keep whatever device refresh
    // they drive themselves; they just won't be told when to run it.
    static std::unique_ptr<ALAudioDeviceNotifier> create(Observer* observer);

    virtual ~ALAudioDeviceNotifier() = default;

    ALAudioDeviceNotifier(const ALAudioDeviceNotifier&)            = delete;
    ALAudioDeviceNotifier& operator=(const ALAudioDeviceNotifier&) = delete;

  protected:
    ALAudioDeviceNotifier() = default;
};

}  // namespace llwebrtc

#endif  // AL_AUDIODEVICENOTIFIER_H

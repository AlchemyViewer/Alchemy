/**
 * @file llwavfile.h
 * @brief Tiny RIFF/WAVE loader shared by audio backends.
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

#ifndef LL_WAVFILE_H
#define LL_WAVFILE_H

#include "stdtypes.h"

#include <string>
#include <vector>

namespace LLAudio
{
    struct WavInfo
    {
        std::vector<U8> pcm;
        U16             channels        = 0;
        U32             sample_rate     = 0;
        U16             bits_per_sample = 0;
        U16             format_tag      = 0;  // 1 = PCM, 3 = IEEE float
    };

    // Parses a RIFF/WAVE file from disk. Returns false on any failure
    // (missing file, malformed header, unsupported sub-format). The
    // viewer's decode pipeline (LLAudioDecodeMgr) always emits PCM WAV,
    // so consumers should accept format_tag == 1 only.
    bool parseWav(const std::string& filename, WavInfo& out);
}

#endif

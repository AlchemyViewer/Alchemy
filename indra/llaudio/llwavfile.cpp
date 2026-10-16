/**
 * @file llwavfile.cpp
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

#include "linden_common.h"

#include "llwavfile.h"

#include "llfile.h"

#include <cstring>

namespace
{
    inline U16 read_u16_le(const U8* p) { return (U16)p[0] | ((U16)p[1] << 8); }
    inline U32 read_u32_le(const U8* p)
    {
        return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
    }
}

bool LLAudio::parseWav(const std::string& filename, WavInfo& out)
{
    out = WavInfo{};

    if (filename.empty())
    {
        return false;
    }

    llifstream infile(filename, std::ios::in | std::ios::binary);
    if (!infile)
    {
        if (LLFile::isfile(filename))
        {
            LL_WARNS() << "LLAudio::parseWav() Unable to open "
                << filename << LL_ENDL;
        }
        else
        {
            LL_DEBUGS() << "LLAudio::parseWav() File missing "
                << filename << LL_ENDL;
        }
        return false;
    }

    U8 riff[12];
    infile.read(reinterpret_cast<char*>(riff), sizeof(riff));
    if (!infile || static_cast<size_t>(infile.gcount()) != sizeof(riff)
        || memcmp(riff, "RIFF", 4) != 0
        || memcmp(riff + 8, "WAVE", 4) != 0)
    {
        LL_WARNS() << "LLAudio::parseWav() Not a RIFF/WAVE file: "
            << filename << LL_ENDL;
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;

    while (infile)
    {
        U8 chunk_hdr[8];
        infile.read(reinterpret_cast<char*>(chunk_hdr), sizeof(chunk_hdr));
        if (static_cast<size_t>(infile.gcount()) != sizeof(chunk_hdr))
        {
            break;
        }
        U32 chunk_size = read_u32_le(chunk_hdr + 4);

        if (memcmp(chunk_hdr, "fmt ", 4) == 0)
        {
            if (chunk_size < 16)
            {
                LL_WARNS() << "LLAudio::parseWav() Malformed fmt chunk in "
                    << filename << LL_ENDL;
                return false;
            }
            std::vector<U8> fmt(chunk_size);
            infile.read(reinterpret_cast<char*>(fmt.data()), chunk_size);
            if (static_cast<U32>(infile.gcount()) != chunk_size)
            {
                LL_WARNS() << "LLAudio::parseWav() Truncated fmt chunk in "
                    << filename << LL_ENDL;
                return false;
            }
            out.format_tag      = read_u16_le(fmt.data() + 0);
            out.channels        = read_u16_le(fmt.data() + 2);
            out.sample_rate     = read_u32_le(fmt.data() + 4);
            out.bits_per_sample = read_u16_le(fmt.data() + 14);
            have_fmt = true;
        }
        else if (memcmp(chunk_hdr, "data", 4) == 0)
        {
            out.pcm.resize(chunk_size);
            if (chunk_size > 0)
            {
                infile.read(reinterpret_cast<char*>(out.pcm.data()), chunk_size);
                if (static_cast<U32>(infile.gcount()) != chunk_size)
                {
                    LL_WARNS() << "LLAudio::parseWav() Truncated data chunk in "
                        << filename << LL_ENDL;
                    return false;
                }
            }
            have_data = true;
            break;
        }
        else
        {
            U32 padded = chunk_size + (chunk_size & 1);
            infile.seekg(padded, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data)
    {
        LL_WARNS() << "LLAudio::parseWav() Missing fmt or data chunk in "
            << filename << LL_ENDL;
        return false;
    }

    if (out.channels == 0 || out.sample_rate == 0)
    {
        LL_WARNS() << "LLAudio::parseWav() Invalid fmt chunk ("
            << out.channels << "ch @ " << out.sample_rate << "Hz) in "
            << filename << LL_ENDL;
        return false;
    }

    return true;
}

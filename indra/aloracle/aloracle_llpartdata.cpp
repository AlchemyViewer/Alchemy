/**
 * @file aloracle_llpartdata.cpp
 * @brief aloracle's function over llmessage's particle systems.
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "aloracleboundary.h"

#include "lldatapacker.h"
#include "llpartdata.h"
#include "llsd.h"
#include "llsdutil_math.h"
#include "message.h"

#include <limits>
#include <string>

namespace
{
    // PS_LEGACY_DATA_BLOCK_SIZE: the system block and the particle block.
    constexpr size_t LEGACY_SIZE = 68 + 18;

    // PS_SYS_DATA_BLOCK_SIZE.
    constexpr S32 SYSTEM_SIZE = 68;

    // An extended system whose system block is the size it knows: the two
    // sizes, the system block and the particle block's fixed part.
    constexpr size_t EXTENDED_SIZE = 4 + 68 + 4 + 18;

    LLSD describe(const LLPartSysData& system, bool ok)
    {
        const LLPartData& part = system.mPartData;

        LLSD value;
        value["ok"] = ok;
        value["crc"] = (S32)system.mCRC;
        value["flags"] = (S32)system.mFlags;
        value["pattern"] = (S32)system.mPattern;
        value["max_age"] = system.mMaxAge;
        value["start_age"] = system.mStartAge;
        value["inner_angle"] = system.mInnerAngle;
        value["outer_angle"] = system.mOuterAngle;
        value["burst_rate"] = system.mBurstRate;
        value["burst_radius"] = system.mBurstRadius;
        value["burst_speed_min"] = system.mBurstSpeedMin;
        value["burst_speed_max"] = system.mBurstSpeedMax;
        value["burst_part_count"] = (S32)system.mBurstPartCount;
        value["angular_velocity"] = system.mAngularVelocity.getValue();
        value["accel"] = system.mPartAccel.getValue();
        value["image"] = system.mPartImageID;
        value["target"] = system.mTargetUUID;
        value["part"]["flags"] = (S32)part.mFlags;
        value["part"]["max_age"] = part.mMaxAge;
        value["part"]["start_color"] = ll_sd_from_color4(part.mStartColor);
        value["part"]["end_color"] = ll_sd_from_color4(part.mEndColor);
        value["part"]["start_scale"] = part.mStartScale.getValue();
        value["part"]["end_scale"] = part.mEndScale.getValue();
        value["part"]["start_glow"] = part.mStartGlow;
        value["part"]["end_glow"] = part.mEndGlow;
        value["part"]["blend_source"] = (S32)part.mBlendFuncSource;
        value["part"]["blend_dest"] = (S32)part.mBlendFuncDest;
        return value;
    }
}

int32_t aloracle_particles_unpack(int32_t form, const uint8_t* data, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || size > (size_t)std::numeric_limits<S32>::max()
            || !aloracle::take(data, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        if (form == ALORACLE_PARTICLES_LEGACY)
        {
            if (bytes.size() < LEGACY_SIZE)
            {
                return ALORACLE_BAD_ARGUMENT;
            }
        }
        else if (form == ALORACLE_PARTICLES_EXTENDED)
        {
            S32 system_size = 0;
            if (bytes.size() >= 4)
            {
                htolememcpy(&system_size, bytes.data(), MVT_S32, 4);
            }
            if (system_size == SYSTEM_SIZE && bytes.size() < EXTENDED_SIZE)
            {
                return ALORACLE_BAD_ARGUMENT;
            }
        }
        else
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLDataPackerBinaryBuffer dp(reinterpret_cast<U8*>(bytes.data()), (S32)bytes.size());
        LLPartSysData system;
        const bool ok = form == ALORACLE_PARTICLES_LEGACY ? system.unpackLegacy(dp) : system.unpack(dp);
        return aloracle::giveValue(describe(system, ok), value);
    });
}

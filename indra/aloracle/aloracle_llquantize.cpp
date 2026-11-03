/**
 * @file aloracle_llquantize.cpp
 * @brief aloracle's functions over llquantize.h's quantized floats.
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

#include "aloracle.h"

#include "llmath.h"
#include "llquantize.h"

uint8_t aloracle_f32_to_u8(float value, float lower, float upper)
{
    return F32_to_U8(value, lower, upper);
}

uint8_t aloracle_f32_to_u8_round(float value, float lower, float upper)
{
    return F32_to_U8_ROUND(value, lower, upper);
}

float aloracle_u8_to_f32(uint8_t value, float lower, float upper)
{
    return U8_to_F32(value, lower, upper);
}

uint16_t aloracle_f32_to_u16(float value, float lower, float upper)
{
    return F32_to_U16(value, lower, upper);
}

uint16_t aloracle_f32_to_u16_round(float value, float lower, float upper)
{
    return F32_to_U16_ROUND(value, lower, upper);
}

float aloracle_u16_to_f32(uint16_t value, float lower, float upper)
{
    return U16_to_F32(value, lower, upper);
}

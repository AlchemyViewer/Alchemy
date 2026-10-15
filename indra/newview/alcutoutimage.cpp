/**
 * @file alcutoutimage.cpp
 * @brief A snapshot cutout's read-back, resolved to the image that is saved
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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
#include "alcutoutimage.h"

#include "llimage.h"

#include <algorithm>

namespace
{
    U8 to_byte(F32 v)
    {
        return (U8)llclamp((S32)(v * 255.f + 0.5f), 0, 255);
    }
}

bool ALCutoutImage::resolve(LLImageRaw& raw, S32 width, S32 height, bool* drawn)
{
    LLImageDataLock lock(&raw);

    if (raw.getComponents() != 4 || raw.isBufferInvalid() || width <= 0 || height <= 0)
    {
        return false;
    }

    // A premultiplied texel is never brighter than it is opaque. And at full
    // size, before anything is averaged away, any texel covered at all is the
    // subject drawn.
    {
        U8* data = raw.getData();
        const S32 texels = raw.getWidth() * raw.getHeight();
        U8 covered = 0;
        for (S32 i = 0; i < texels; ++i, data += 4)
        {
            const U8 alpha = data[3];
            data[0] = std::min(data[0], alpha);
            data[1] = std::min(data[1], alpha);
            data[2] = std::min(data[2], alpha);
            covered |= alpha;
        }
        if (drawn)
        {
            *drawn = covered != 0;
        }
    }

    // Averaged while premultiplied, so an edge keeps its colour and loses only
    // its coverage.
    return raw.scale(width, height);
}

bool ALCutoutImage::layOverFill(LLImageRaw& raw, const LLColor4& fill, bool keep_alpha)
{
    LLImageDataLock lock(&raw);

    if (raw.getComponents() != 4 || raw.isBufferInvalid())
    {
        return false;
    }

    const F32 fill_alpha = llclamp(fill.mV[VALPHA], 0.f, 1.f);
    const S32 width = raw.getWidth();
    const S32 height = raw.getHeight();
    const S32 texels = width * height;

    // Composited in place: each texel is read whole before it is written, and
    // three components written never pass the four being read.
    U8* data = raw.getData();
    const U8* in = data;
    U8* dst = data;
    if (keep_alpha)
    {
        for (S32 i = 0; i < texels; ++i, in += 4, dst += 4)
        {
            const F32 coverage = in[3] / 255.f;
            const F32 uncovered = 1.f - coverage;
            // Premultiplied "over": the fill contributes what the subject
            // leaves uncovered, in proportion to how much fill there is.
            const F32 fill_weight = fill_alpha * uncovered;
            const F32 alpha = coverage + fill_weight;
            const F32 premultiplied[3] = { in[0] / 255.f + fill.mV[0] * fill_weight,
                                           in[1] / 255.f + fill.mV[1] * fill_weight,
                                           in[2] / 255.f + fill.mV[2] * fill_weight };
            for (S32 c = 0; c < 3; ++c)
            {
                dst[c] = to_byte(alpha > 0.f ? premultiplied[c] / alpha : 0.f);
            }
            dst[3] = to_byte(alpha);
        }
        return true;
    }

    // Nowhere to put alpha: the fill is the background, whole.
    for (S32 i = 0; i < texels; ++i, in += 4, dst += 3)
    {
        const F32 uncovered = 1.f - in[3] / 255.f;
        const F32 colour[3] = { in[0] / 255.f + fill.mV[0] * uncovered,
                                in[1] / 255.f + fill.mV[1] * uncovered,
                                in[2] / 255.f + fill.mV[2] * uncovered };
        for (S32 c = 0; c < 3; ++c)
        {
            dst[c] = to_byte(colour[c]);
        }
    }
    // Shrunk to the three components now packed at its start.
    if (!raw.reallocateData(texels * 3))
    {
        return false;
    }
    raw.setSize(width, height, 3);
    return true;
}

bool ALCutoutImage::resolveOverFill(LLImageRaw& raw, S32 width, S32 height, const LLColor4& fill, bool keep_alpha, bool* drawn)
{
    return resolve(raw, width, height, drawn) && layOverFill(raw, fill, keep_alpha);
}

bool ALCutoutImage::scaleStraightAlpha(LLImageRaw& raw, S32 width, S32 height)
{
    LLImageDataLock lock(&raw);

    if (raw.getComponents() != 4 || raw.isBufferInvalid())
    {
        return raw.scale(width, height);
    }

    U8* data = raw.getData();
    const S32 texels = raw.getWidth() * raw.getHeight();
    for (S32 i = 0; i < texels; ++i, data += 4)
    {
        const U32 alpha = data[3];
        for (S32 c = 0; c < 3; ++c)
        {
            data[c] = (U8)((data[c] * alpha + 127) / 255);
        }
    }
    return resolveOverFill(raw, width, height, LLColor4::transparent, true);
}

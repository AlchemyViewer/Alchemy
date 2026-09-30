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
#include <vector>

namespace
{
    U8 to_byte(F32 v)
    {
        return (U8)llclamp((S32)(v * 255.f + 0.5f), 0, 255);
    }
}

bool ALCutoutImage::resolve(LLImageRaw& raw, S32 width, S32 height, const LLColor4& fill, bool keep_alpha)
{
    LLImageDataLock lock(&raw);

    if (raw.getComponents() != 4 || raw.isBufferInvalid() || width <= 0 || height <= 0)
    {
        return false;
    }

    // A premultiplied texel is never brighter than it is opaque.
    {
        U8* data = raw.getData();
        const S32 texels = raw.getWidth() * raw.getHeight();
        for (S32 i = 0; i < texels; ++i, data += 4)
        {
            const U8 alpha = data[3];
            data[0] = std::min(data[0], alpha);
            data[1] = std::min(data[1], alpha);
            data[2] = std::min(data[2], alpha);
        }
    }

    // Averaged while premultiplied, so an edge keeps its colour and loses only
    // its coverage.
    if (!raw.scale(width, height))
    {
        return false;
    }

    const F32 fill_alpha = llclamp(fill.mV[VALPHA], 0.f, 1.f);
    const S32 texels = width * height;
    const U8* in = raw.getData();

    std::vector<U8> out((size_t)texels * (keep_alpha ? 4 : 3));
    U8* dst = out.data();
    for (S32 i = 0; i < texels; ++i, in += 4)
    {
        const F32 coverage = in[3] / 255.f;
        const F32 uncovered = 1.f - coverage;

        if (keep_alpha)
        {
            // Premultiplied "over": the fill contributes what the subject
            // leaves uncovered, in proportion to how much fill there is.
            const F32 alpha = coverage + fill_alpha * uncovered;
            const F32 fill_weight = fill_alpha * uncovered;
            for (S32 c = 0; c < 3; ++c)
            {
                const F32 premultiplied = in[c] / 255.f + fill.mV[c] * fill_weight;
                dst[c] = to_byte(alpha > 0.f ? premultiplied / alpha : 0.f);
            }
            dst[3] = to_byte(alpha);
            dst += 4;
        }
        else
        {
            // Nowhere to put alpha: the fill is the background, whole.
            for (S32 c = 0; c < 3; ++c)
            {
                dst[c] = to_byte(in[c] / 255.f + fill.mV[c] * uncovered);
            }
            dst += 3;
        }
    }

    if (!raw.resize((U16)width, (U16)height, (S8)(keep_alpha ? 4 : 3)) || raw.isBufferInvalid())
    {
        return false;
    }
    memcpy(raw.getData(), out.data(), out.size());
    return true;
}

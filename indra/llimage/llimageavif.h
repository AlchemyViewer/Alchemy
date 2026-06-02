/*
 * @file llimageavif.h
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) Rye Mutt <rye@alchemyviewer.org>
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

#ifndef AL_ALIMAGEAVIF_H
#define AL_ALIMAGEAVIF_H

#include "stdtypes.h"
#include "llimage.h"

class LLImageAVIF final : public LLImageFormatted
{
protected:
    ~LLImageAVIF() = default;

public:
    LLImageAVIF(S32 quality = 100);

    std::string getExtension() override { return std::string("avif"); }
    bool updateData() override;
    bool decode(LLImageRaw* raw_image, F32 decode_time) override;
    bool encode(const LLImageRaw* raw_image, F32 encode_time) override;

    void setEncodeQuality(S32 q) { mEncodeQuality = q; }    // 0 (worst) - 100 (lossless)
    S32  getEncodeQuality() const { return mEncodeQuality; }

protected:
    S32 mEncodeQuality;     // AVIF quality scale, 0 (worst) - 100 (lossless)
};

#endif

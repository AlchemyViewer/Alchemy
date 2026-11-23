/**
 * @file alimagej2crs.h
 * @brief JPEG 2000 with pnj2k, the Rust codec, over its C API
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

#pragma once

#include "llimagej2c.h"

// Decodes and encodes with pnj2k.
//
// A decode reads the bytes the image holds, or its first getMaxBytes(), at
// the image's discard level, capped at the most the codestream can discard,
// straight into the raw image's buffer, bottom row first, with the channels
// asked for. A prefix decodes to what its whole packets give.
//
// An encode writes Second Life's upload profile, which every viewer
// streams: one tile, LRCP, 64x64 code-blocks, five levels at every size,
// quality layers of 600 and 2000 bytes and four times more up to an eighth
// of the raw image (and an unbounded one when lossless), with the image's
// encode content choosing its colour transform and weighting. An opaque
// alpha is dropped. A resize for an upload averages colour in linear light
// and renormalises normal maps.
//
// Each thread keeps one pnj2k decoder and one encoder, whose working memory
// their calls reuse.
class ALImageJ2CRS : public LLImageJ2CImpl
{
public:
    ALImageJ2CRS() = default;
    ~ALImageJ2CRS() override = default;

protected:
    bool getMetadata(LLImageJ2C& base) override;
    bool initDecode(LLImageJ2C& base, LLImageRaw& raw_image, int discard_level = -1, int* region = nullptr) override;
    bool initEncode(LLImageJ2C& base, LLImageRaw& raw_image, int blocks_size = -1, int precincts_size = -1,
                    int levels = 0) override;
    bool decodeImpl(LLImageJ2C& base, LLImageRaw& raw_image, F32 decode_time, S32 first_channel,
                    S32 max_channel_count) override;
    bool encodeImpl(LLImageJ2C& base, const LLImageRaw& raw_image, const char* comment_text, F32 encode_time = 0.0,
                    bool reversible = false) override;
    bool resizeImpl(LLImageJ2C& base, LLImageRaw& raw_image, S32 new_width, S32 new_height) override;
    std::string getEngineInfo() const override;
};

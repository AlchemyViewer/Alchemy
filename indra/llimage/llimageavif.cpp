/*
 * @file llimageavif.cpp
 * @brief LLImageFormatted glue to encode / decode AVIF files.
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

#include "linden_common.h"
#include "stdtypes.h"
#include "llerror.h"

#include "llimage.h"
#include "llimageavif.h"

#include <avif/avif.h>

#include <memory>
#include <vector>

// libavif has no built-in vertical flip, but Second Life raw images are
// stored bottom-up (OpenGL convention), so we flip rows by hand.
namespace
{
    void flip_rows_in_place(U8* data, U32 height, size_t stride)
    {
        if (height < 2)
        {
            return;
        }
        std::vector<U8> row_tmp(stride);
        for (U32 i = 0; i < height / 2; ++i)
        {
            U8* top = data + (size_t)i * stride;
            U8* bottom = data + (size_t)(height - 1 - i) * stride;
            memcpy(row_tmp.data(), top, stride);
            memcpy(top, bottom, stride);
            memcpy(bottom, row_tmp.data(), stride);
        }
    }
}

// ---------------------------------------------------------------------------
// LLImageAVIF
// ---------------------------------------------------------------------------
LLImageAVIF::LLImageAVIF(S32 quality)
    : LLImageFormatted(IMG_CODEC_AVIF)
    , mEncodeQuality(quality)
{
}

// Virtual
// Parse AVIF image information and set the appropriate
// width, height and component (channel) information.
bool LLImageAVIF::updateData()
{
    resetLastError();

    LLImageDataLock lock(this);

    // Check to make sure that this instance has been initialized with data
    if (isBufferInvalid() || (0 == getDataSize()))
    {
        setLastError("Uninitialized instance of LLImageAVIF");
        return false;
    }

    avifROData raw = { getData(), (size_t)getDataSize() };
    if (!avifPeekCompatibleFileType(&raw))
    {
        setLastError("LLImageAVIF data does not have a valid AVIF header!");
        return false;
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder)
    {
        setLastError("LLImageAVIF could not create decoder");
        return false;
    }

    bool success = false;
    if (avifDecoderSetIOMemory(decoder, getData(), getDataSize()) == AVIF_RESULT_OK
        && avifDecoderParse(decoder) == AVIF_RESULT_OK)
    {
        setSize(decoder->image->width, decoder->image->height, decoder->alphaPresent ? 4 : 3);
        success = true;
    }
    else
    {
        setLastError("LLImageAVIF failed to parse AVIF header");
    }

    avifDecoderDestroy(decoder);
    return success;
}

// Virtual
// Decode an in-memory AVIF image into the raw RGB or RGBA format
// used within SecondLife.
bool LLImageAVIF::decode(LLImageRaw* raw_image, F32 decode_time)
{
    llassert_always(raw_image);

    LLImageDataSharedLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    resetLastError();

    // Check to make sure that this instance has been initialized with data
    if (isBufferInvalid() || (0 == getDataSize()))
    {
        setLastError("LLImageAVIF trying to decode an image with no data!");
        return false;
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder)
    {
        setLastError("LLImageAVIF could not create decoder");
        return false;
    }

    // RAII-ish cleanup for the decoder along every early-out below.
    struct DecoderGuard
    {
        avifDecoder* d;
        ~DecoderGuard() { if (d) avifDecoderDestroy(d); }
    } guard{ decoder };

    if (avifDecoderSetIOMemory(decoder, getData(), getDataSize()) != AVIF_RESULT_OK)
    {
        setLastError("LLImageAVIF failed to set decoder input");
        return false;
    }

    if (avifDecoderParse(decoder) != AVIF_RESULT_OK)
    {
        setLastError("LLImageAVIF data does not have a valid AVIF header!");
        return false;
    }

    if (avifDecoderNextImage(decoder) != AVIF_RESULT_OK)
    {
        setLastError("LLImageAVIF failed to decode AVIF image");
        return false;
    }

    const bool has_alpha = decoder->alphaPresent;

    setSize(decoder->image->width, decoder->image->height, has_alpha ? 4 : 3);

    if (!raw_image->resize(getWidth(), getHeight(), getComponents()))
    {
        setLastError("LLImageAVIF failed to resize raw image output buffer");
        return false;
    }

    // Convert the decoded YUV image straight into the raw image buffer as
    // 8-bit RGB/RGBA. Higher bit depths / HDR are down-converted to 8-bit sRGB.
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = has_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    rgb.pixels = (uint8_t*)raw_image->getData();
    rgb.rowBytes = (uint32_t)(raw_image->getWidth() * raw_image->getComponents());

    if (avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK)
    {
        setLastError("LLImageAVIF failed to convert AVIF image to RGB");
        return false;
    }

    // Flip the image because SL is opengl
    flip_rows_in_place(raw_image->getData(), getHeight(), rgb.rowBytes);

    return true;
}

// Virtual
// Encode the in memory RGB image into AVIF format.
bool LLImageAVIF::encode(const LLImageRaw* raw_image, F32 encode_time)
{
    llassert_always(raw_image);

    resetLastError();

    LLImageDataSharedLock lockIn(raw_image);
    LLImageDataLock lockOut(this);

    if (raw_image->isBufferInvalid() || (0 == raw_image->getDataSize()))
    {
        setLastError("LLImageAVIF trying to encode an image with no data!");
        return false;
    }

    const U8* datap = raw_image->getData();
    const U32 height = raw_image->getHeight();
    const U32 width = raw_image->getWidth();
    const S8 components = raw_image->getComponents();
    const size_t stride = (size_t)width * components;

    setSize(width, height, components);

    // Flip image vertically into a temporary buffer for encode (SL is opengl).
    std::unique_ptr<U8[]> tmp_buff;
    try
    {
        tmp_buff = std::make_unique<U8[]>(height * stride);
    }
    catch (const std::bad_alloc&)
    {
        setLastError("LLImageAVIF::out of memory");
        return false;
    }

    for (U32 i = 0; i < height; ++i)
    {
        const U8* row = &datap[(size_t)(height - 1 - i) * stride];
        memcpy(tmp_buff.get() + (size_t)i * stride, row, stride);
    }

    avifImage* image = avifImageCreate(width, height, 8, AVIF_PIXEL_FORMAT_YUV420);
    if (!image)
    {
        setLastError("LLImageAVIF::Failed to allocate image");
        return false;
    }

    // Tag the bitstream as sRGB so decoders reproduce our 8-bit sRGB content.
    image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = (components == 4) ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    rgb.pixels = tmp_buff.get();
    rgb.rowBytes = (uint32_t)stride;

    if (avifImageRGBToYUV(image, &rgb) != AVIF_RESULT_OK)
    {
        setLastError("LLImageAVIF::Failed to convert image to YUV");
        avifImageDestroy(image);
        return false;
    }

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder)
    {
        setLastError("LLImageAVIF::Failed to create encoder");
        avifImageDestroy(image);
        return false;
    }

    encoder->quality = mEncodeQuality;
    encoder->qualityAlpha = mEncodeQuality;
    encoder->speed = 6;     // balance encode time against quality for snapshots

    avifRWData output = AVIF_DATA_EMPTY;
    const avifResult result = avifEncoderWrite(encoder, image, &output);

    avifEncoderDestroy(encoder);
    avifImageDestroy(image);

    if (result != AVIF_RESULT_OK || output.data == nullptr || output.size == 0)
    {
        setLastError("LLImageAVIF::Failed to encode image");
        avifRWDataFree(&output);
        return false;
    }

    if (!allocateData(narrow(output.size)))
    {
        setLastError("LLImageAVIF::Failed to allocate final buffer for image");
        avifRWDataFree(&output);
        return false;
    }

    memcpy(getData(), output.data, output.size);
    avifRWDataFree(&output);

    return true;
}

/**
 * @file alimagej2crs.cpp
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

#include "linden_common.h"

#include "alimagej2crs.h"

#include "pnj2k.h"

#include <memory>

// Factory function: see declaration in llimagej2c.cpp
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
    return new ALImageJ2CRS();
}

namespace
{
    // A pnj2k decoder for the thread: one is used by one thread at a time,
    // and keeps its working memory between decodes.
    class ThreadDecoder
    {
    public:
        ThreadDecoder()
        {
            if (pnj2k_abi_version() == PNJ2K_ABI_VERSION)
            {
                mDecoder = pnj2k_decoder_new(nullptr);
            }
        }

        ~ThreadDecoder() { pnj2k_decoder_free(mDecoder); }

        ThreadDecoder(const ThreadDecoder&) = delete;
        ThreadDecoder& operator=(const ThreadDecoder&) = delete;

        pnj2k_decoder* get() const { return mDecoder; }

    private:
        pnj2k_decoder* mDecoder = nullptr;
    };

    pnj2k_decoder* threadDecoder()
    {
        thread_local ThreadDecoder decoder;
        return decoder.get();
    }

    // A pnj2k encoder for the thread, as the decoder.
    class ThreadEncoder
    {
    public:
        ThreadEncoder()
        {
            if (pnj2k_abi_version() == PNJ2K_ABI_VERSION)
            {
                mEncoder = pnj2k_encoder_new(nullptr);
            }
        }

        ~ThreadEncoder() { pnj2k_encoder_free(mEncoder); }

        ThreadEncoder(const ThreadEncoder&) = delete;
        ThreadEncoder& operator=(const ThreadEncoder&) = delete;

        pnj2k_encoder* get() const { return mEncoder; }

    private:
        pnj2k_encoder* mEncoder = nullptr;
    };

    pnj2k_encoder* threadEncoder()
    {
        thread_local ThreadEncoder encoder;
        return encoder.get();
    }

    U32 contentOf(EJ2CContent content)
    {
        switch (content)
        {
            case EJ2CContent::Data:
                return PNJ2K_CONTENT_DATA;
            case EJ2CContent::Normal:
                return PNJ2K_CONTENT_NORMAL;
            case EJ2CContent::Colour:
            default:
                return PNJ2K_CONTENT_COLOUR;
        }
    }

    struct CodestreamFree
    {
        void operator()(pnj2k_codestream* cs) const { pnj2k_codestream_free(cs); }
    };
    using CodestreamPtr = std::unique_ptr<pnj2k_codestream, CodestreamFree>;

    // A pnj2k struct, zeroed but for its size.
    template <typename T>
    T sized()
    {
        T value{};
        value.struct_size = sizeof(T);
        return value;
    }

    // The 8-bit format of one to four channels.
    constexpr U32 FORMATS[] = { PNJ2K_FORMAT_GRAY8, PNJ2K_FORMAT_GRAY_ALPHA8, PNJ2K_FORMAT_RGB8,
                                PNJ2K_FORMAT_RGBA8 };

    // Whether the image is larger than the viewer holds, a side or in its
    // components. A raw image is sized by what the header declares, all of
    // its components whatever a decode takes of them, and a header alone
    // decodes, to mid-grey of that size, so a larger one is refused, not
    // held.
    bool tooLarge(const pnj2k_info& info)
    {
        return info.width > U32(MAX_IMAGE_SIZE) || info.height > U32(MAX_IMAGE_SIZE)
            || info.components > U32(MAX_IMAGE_COMPONENTS);
    }

    std::string tooLargeMessage(const pnj2k_info& info)
    {
        return "pnj2k: an image of " + std::to_string(info.width) + "x" + std::to_string(info.height) + " and "
            + std::to_string(info.components) + " components is larger than " + std::to_string(MAX_IMAGE_SIZE)
            + " a side or " + std::to_string(MAX_IMAGE_COMPONENTS) + " components";
    }

    std::string message(const pnj2k_decoder* dec, pnj2k_status status)
    {
        const char* detail = pnj2k_decoder_last_error(dec);
        return std::string("pnj2k: ")
            + (detail && *detail ? detail : pnj2k_status_message(status));
    }
} // namespace

std::string ALImageJ2CRS::getEngineInfo() const
{
    // The texture cache keeps this in its header, in 31 bytes and a
    // terminator, and empties itself when it changes.
    return (std::string("pnj2k ") + pnj2k_version()).substr(0, 31);
}

bool ALImageJ2CRS::initDecode(LLImageJ2C& base, LLImageRaw& raw_image, int discard_level, int* region)
{
    // A decode reads the bytes it is given in one call, at the discard level
    // LLImageJ2C has set; nothing to set up.
    return false;
}

bool ALImageJ2CRS::initEncode(LLImageJ2C& base, LLImageRaw& raw_image, int blocks_size, int precincts_size,
                              int levels)
{
    // Every encode writes Second Life's upload profile.
    return false;
}

bool ALImageJ2CRS::encodeImpl(LLImageJ2C& base, const LLImageRaw& raw_image, const char* comment_text, F32 encode_time,
                              bool reversible)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageDataCopyLock lock(&raw_image, &base);

    if (raw_image.isBufferInvalid() || !raw_image.getData())
    {
        base.setLastError("Invalid input, no buffer");
        return false;
    }
    pnj2k_encoder* enc = threadEncoder();
    if (!enc)
    {
        base.setLastError("pnj2k: no encoder");
        return false;
    }

    pnj2k_source source = sized<pnj2k_source>();
    source.pixels = raw_image.getData();
    source.size = size_t(raw_image.getDataSize());
    source.width = U32(raw_image.getWidth());
    source.height = U32(raw_image.getHeight());
    source.channels = U32(raw_image.getComponents());

    // Second Life's upload profile, which every viewer streams. The raw
    // image's rows run bottom to top, as OpenGL's do.
    pnj2k_encode_params params = sized<pnj2k_encode_params>();
    params.preset = PNJ2K_PRESET_SECOND_LIFE;
    params.flags = PNJ2K_ENCODE_FLIP_ROWS | (reversible ? PNJ2K_ENCODE_REVERSIBLE : 0);
    params.content = contentOf(base.getEncodeContent());
    params.comment = comment_text;

    pnj2k_buffer out = sized<pnj2k_buffer>();
    const pnj2k_status status = pnj2k_encode(enc, &source, &params, &out);
    if (status != PNJ2K_OK)
    {
        const char* detail = pnj2k_encoder_last_error(enc);
        const std::string why = std::string("pnj2k: ")
            + (detail && *detail ? detail : pnj2k_status_message(status));
        base.setLastError(why);
        LL_WARNS() << "pnj2k encoding failed: " << why << LL_ENDL;
        return false;
    }

    const bool copied = out.len <= size_t(S32_MAX) && base.allocateData(S32(out.len));
    if (copied)
    {
        memcpy(base.getData(), out.data, out.len);
    }
    pnj2k_buffer_free(&out);
    if (!copied)
    {
        base.setLastError("pnj2k: no memory for the codestream");
        return false;
    }
    base.updateData(); // the width, height and components from the header
    return true;
}

bool ALImageJ2CRS::resizeImpl(LLImageJ2C& base, LLImageRaw& raw_image, S32 new_width, S32 new_height)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageDataLock lock(&raw_image);

    pnj2k_encoder* enc = threadEncoder();
    const S32 components = raw_image.getComponents();
    if (!enc || raw_image.isBufferInvalid() || !raw_image.getData() || new_width <= 0 || new_height <= 0 ||
        new_width > MAX_IMAGE_SIZE || new_height > MAX_IMAGE_SIZE)
    {
        return false;
    }

    pnj2k_source source = sized<pnj2k_source>();
    source.pixels = raw_image.getData();
    source.size = size_t(raw_image.getDataSize());
    source.width = U32(raw_image.getWidth());
    source.height = U32(raw_image.getHeight());
    source.channels = U32(components);

    // The scaled image is made in a buffer of its own and handed to the raw
    // image only once it is whole, as LLImageRaw::scale does it: a resize
    // that fails, for want of the buffer or in pnj2k, leaves the image as
    // it was, for LLImageRaw to scale.
    const size_t size = size_t(new_width) * size_t(new_height) * size_t(components);
    U8* pixels = (U8*)ll_aligned_malloc_16(size);
    if (!pixels)
    {
        return false;
    }
    pnj2k_image image = sized<pnj2k_image>();
    image.pixels = pixels;
    image.size = size;
    const pnj2k_status status =
        pnj2k_resize(enc, &source, U32(new_width), U32(new_height), contentOf(base.getEncodeContent()), &image);
    if (status != PNJ2K_OK)
    {
        LL_WARNS() << "pnj2k resize failed: " << pnj2k_encoder_last_error(enc) << LL_ENDL;
        ll_aligned_free_16(pixels);
        return false;
    }
    raw_image.setDataAndSize(pixels, new_width, new_height, S8(components));
    return true;
}

bool ALImageJ2CRS::getMetadata(LLImageJ2C& base)
{
    LLImageDataLock lock(&base);

    pnj2k_info info = sized<pnj2k_info>();
    const pnj2k_status status = pnj2k_probe(base.getData(), size_t(llmax(base.getDataSize(), 0)), &info);
    if (status != PNJ2K_OK)
    {
        base.setLastError(std::string("pnj2k: ") + pnj2k_status_message(status));
        return false;
    }
    if (tooLarge(info))
    {
        base.setLastError(tooLargeMessage(info));
        return false;
    }

    base.setSize(S32(info.width), S32(info.height), S32(info.components));
    return true;
}

bool ALImageJ2CRS::decodeImpl(LLImageJ2C& base, LLImageRaw& raw_image, F32 decode_time, S32 first_channel,
                              S32 max_channel_count)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageDataLockPair lock(&base, &raw_image);

    const U8* data = base.getData();
    S32 size = llmax(base.getDataSize(), 0);
    if (base.getMaxBytes() > 0)
    {
        size = llmin(size, base.getMaxBytes());
    }

    pnj2k_decoder* dec = threadDecoder();
    auto failed = [&](const std::string& why)
    {
        base.setLastError(why);
        base.decodeFailed();
        LL_DEBUGS("Texture") << "ERROR -> decodeImpl: " << why << LL_ENDL;
        return true; // done
    };
    if (!dec)
    {
        return failed("pnj2k: no decoder");
    }

    // A tile's layout of code-blocks, precincts and tag trees is held to
    // 32 MiB, where real textures take under 1 MiB, rather than the
    // library's 256 MiB, which a hostile header could make each decode
    // thread hold. Its samples are held to what an image may hold, rather
    // than the library's 1 GiB: a decode takes little more than its
    // output, but one made to decode a tile whole, by a region-of-interest
    // shift or by deep components, takes several times that.
    pnj2k_parse_params parse = sized<pnj2k_parse_params>();
    parse.max_layout_bytes = U64(32) << 20;
    parse.max_decoded_bytes = U64(MAX_IMAGE_DATA_SIZE);
    pnj2k_codestream* parsed = nullptr;
    pnj2k_status status = pnj2k_codestream_parse(dec, data, size_t(size), &parse, &parsed);
    if (status != PNJ2K_OK)
    {
        return failed(message(dec, status));
    }
    const CodestreamPtr cs(parsed);

    // The first comment, which holds the uploader and the upload's time.
    if (pnj2k_codestream_comment_count(cs.get()) > 0)
    {
        pnj2k_comment comment = sized<pnj2k_comment>();
        if (pnj2k_codestream_comment(cs.get(), 0, &comment) == PNJ2K_OK)
        {
            raw_image.mComment.assign(reinterpret_cast<const char*>(comment.data), comment.len);
        }
    }

    pnj2k_info info = sized<pnj2k_info>();
    status = pnj2k_codestream_info(cs.get(), &info);
    if (status != PNJ2K_OK)
    {
        return failed(message(dec, status));
    }
    if (tooLarge(info))
    {
        return failed(tooLargeMessage(info));
    }

    const S32 channels = llclamp(S32(info.components) - first_channel, 0, llmin(max_channel_count, 4));
    if (channels <= 0)
    {
        return failed("Requested first_channel beyond decoded components");
    }

    // The discard level taken as a byte, as OpenJPEG takes it, so that -1
    // asks for the most, and capped at the most the codestream can discard.
    const U32 reduce = llmin(U32(U8(base.getDiscardLevel())), info.max_reduce);

    pnj2k_decode_params params = sized<pnj2k_decode_params>();
    params.reduce = reduce;
    params.first_component = U32(first_channel);
    params.component_count = U32(channels);
    params.format = FORMATS[channels - 1];
    // Components subsampled on the image's grid, as the colour of a 4:2:0
    // image is, are brought to the image's size, the one getMetadata gives:
    // pixels hold components of one size, and pnj2k refuses to make them of
    // components that differ. A pixel between a component's samples is
    // interpolated from those about it, which takes no longer than repeating
    // the one before it and draws the component as no blocks. An image
    // sampled at every point, which every texture a viewer uploads is,
    // decodes the same with the two flags and without.
    params.flags = PNJ2K_FLIP_ROWS | PNJ2K_UPSAMPLE | PNJ2K_INTERPOLATE;

    pnj2k_image image = sized<pnj2k_image>();
    status = pnj2k_codestream_image_info(dec, cs.get(), &params, &image);
    if (status != PNJ2K_OK)
    {
        return failed(message(dec, status));
    }
    if (image.width > U16_MAX || image.height > U16_MAX
        || !raw_image.resize(U16(image.width), U16(image.height), S8(channels)) || !raw_image.getData())
    {
        return failed("pnj2k: no memory for the decoded image");
    }

    image.pixels = raw_image.getData();
    image.size = size_t(raw_image.getDataSize());
    status = pnj2k_codestream_decode(dec, cs.get(), data, size_t(size), &params, &image);
    if (status != PNJ2K_OK)
    {
        return failed(message(dec, status));
    }

    base.setDiscardLevel(S8(reduce));
    return true; // done
}

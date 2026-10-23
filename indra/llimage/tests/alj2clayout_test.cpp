/**
 * @file alj2clayout_test.cpp
 * @brief Tests for the J2C codestream layout and the byte targets it gives
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

#include "alj2clayout.h"
#include "llimagej2c.h"

#include "openjpeg.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
    constexpr F64 QUALITY_BAR_DB = 45.0;
    constexpr F64 IDENTICAL_DB = 99.0;

    U32 hash(U32 x, U32 y, U32 seed)
    {
        U32 h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return h;
    }

    // Smoothly interpolated lattice noise in [0, 1], one lattice cell every `period` pixels.
    F64 valueNoise(S32 x, S32 y, S32 period, U32 seed)
    {
        const S32 cx = x / period;
        const S32 cy = y / period;
        const F64 fx = F64(x % period) / period;
        const F64 fy = F64(y % period) / period;
        const F64 sx = fx * fx * (3.0 - 2.0 * fx);
        const F64 sy = fy * fy * (3.0 - 2.0 * fy);
        auto corner = [&](S32 dx, S32 dy) { return F64(hash(cx + dx, cy + dy, seed) & 0xffff) / 65535.0; };
        const F64 top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * sx;
        const F64 bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * sx;
        return top + (bottom - top) * sy;
    }

    // A texture with detail at every scale, as photographs and painted textures have it: noise
    // octaves whose amplitude falls with frequency, a different mix in each channel, and a few
    // hard-edged shapes.
    LLPointer<LLImageRaw> makeTexture(S32 size, S32 components, U32 seed)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(U16(size), U16(size), S8(components));
        U8* data = raw->getData();
        for (S32 y = 0; y < size; ++y)
        {
            for (S32 x = 0; x < size; ++x)
            {
                for (S32 c = 0; c < components; ++c)
                {
                    F64 value = 0.0;
                    F64 amplitude = 0.5;
                    for (S32 period = size / 2; period >= 1; period /= 2)
                    {
                        value += amplitude * (valueNoise(x, y, period, seed + c * 977) - 0.5);
                        amplitude *= 0.75;
                    }
                    const bool stripe = ((x + 2 * y) / (size / 8)) % 3 == 0;
                    const bool disc = (x - size / 3) * (x - size / 3) + (y - size / 2) * (y - size / 2) < size * size / 25;
                    value += (stripe ? 0.12 : 0.0) - (disc ? 0.18 : 0.0);
                    data[(y * size + x) * components + c] = U8(std::clamp(128.0 + 255.0 * value, 0.0, 255.0));
                }
            }
        }
        return raw;
    }

    // The viewer's own encoder: LRCP with quality layers of 600 and 2000 bytes, then four
    // times the last, up to an eighth of the raw size.
    LLPointer<LLImageJ2C> encodeLayered(const LLImageRaw* raw)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C;
        REQUIRE(j2c->encode(raw, 0.f));
        return j2c;
    }

    struct MemoryOut
    {
        std::vector<U8> mBytes;
        size_t mPos = 0;
        size_t mEnd = 0;
    };

    OPJ_SIZE_T writeOut(void* buffer, OPJ_SIZE_T bytes, void* user)
    {
        MemoryOut* out = static_cast<MemoryOut*>(user);
        if (out->mPos + bytes > out->mBytes.size())
        {
            out->mBytes.resize(out->mPos + bytes);
        }
        memcpy(out->mBytes.data() + out->mPos, buffer, bytes);
        out->mPos += bytes;
        out->mEnd = std::max(out->mEnd, out->mPos);
        return bytes;
    }

    OPJ_OFF_T skipOut(OPJ_OFF_T bytes, void* user)
    {
        MemoryOut* out = static_cast<MemoryOut*>(user);
        out->mPos += size_t(bytes);
        if (out->mPos > out->mBytes.size())
        {
            out->mBytes.resize(out->mPos);
        }
        out->mEnd = std::max(out->mEnd, out->mPos);
        return bytes;
    }

    OPJ_BOOL seekOut(OPJ_OFF_T pos, void* user)
    {
        static_cast<MemoryOut*>(user)->mPos = size_t(pos);
        return OPJ_TRUE;
    }

    // An LRCP codestream at half the raw size in one quality layer, which orders its packets
    // by resolution alone, as Second Life's single-layer textures are; or the same cut into
    // four tiles.
    std::vector<U8> encodeSingleLayer(const LLImageRaw* raw, bool four_tiles = false)
    {
        const S32 size = raw->getWidth();
        const S32 components = raw->getComponents();

        opj_cparameters_t parameters;
        opj_set_default_encoder_parameters(&parameters);
        parameters.cod_format = OPJ_CODEC_J2K;
        parameters.prog_order = OPJ_LRCP;
        parameters.irreversible = 1;
        parameters.cp_disto_alloc = 1;
        parameters.tcp_numlayers = 1;
        parameters.tcp_rates[0] = 2.f;
        parameters.tcp_mct = components >= 3 ? 1 : 0;
        if (four_tiles)
        {
            parameters.tile_size_on = OPJ_TRUE;
            parameters.cp_tdx = size / 2;
            parameters.cp_tdy = size / 2;
        }

        std::vector<opj_image_cmptparm_t> component_parameters(components);
        for (opj_image_cmptparm_t& component : component_parameters)
        {
            component = {};
            component.dx = 1;
            component.dy = 1;
            component.w = size;
            component.h = size;
            component.prec = 8;
        }
        opj_image_t* image = opj_image_create(components, component_parameters.data(), components >= 3 ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY);
        REQUIRE(image);
        image->x1 = size;
        image->y1 = size;
        const U8* data = raw->getData();
        for (S32 i = 0; i < size * size; ++i)
        {
            for (S32 c = 0; c < components; ++c)
            {
                image->comps[c].data[i] = data[i * components + c];
            }
        }

        MemoryOut out;
        opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
        opj_stream_t* stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE);
        opj_stream_set_user_data(stream, &out, nullptr);
        opj_stream_set_write_function(stream, writeOut);
        opj_stream_set_skip_function(stream, skipOut);
        opj_stream_set_seek_function(stream, seekOut);
        const bool encoded = opj_setup_encoder(codec, &parameters, image)
            && opj_start_compress(codec, image, stream)
            && opj_encode(codec, stream)
            && opj_end_compress(codec, stream);
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        opj_image_destroy(image);
        REQUIRE(encoded);

        out.mBytes.resize(out.mEnd);
        return out.mBytes;
    }

    LLPointer<LLImageJ2C> imageFrom(const U8* data, S32 size)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C;
        U8* copy = (U8*)ll_aligned_malloc_16(size);
        memcpy(copy, data, size);
        j2c->setData(copy, size);
        return j2c;
    }

    // The decode at `discard` of the first `bytes` of a codestream, as the decode thread runs it.
    LLPointer<LLImageRaw> decodePrefix(const U8* data, S32 bytes, S32 discard)
    {
        LLPointer<LLImageJ2C> j2c = imageFrom(data, bytes);
        REQUIRE(j2c->updateData());
        j2c->setDiscardLevel(S8(discard));
        LLPointer<LLImageRaw> raw = new LLImageRaw(j2c->getWidth(), j2c->getHeight(), j2c->getComponents());
        REQUIRE(j2c->decode(raw, 0.f));
        return raw;
    }

    F64 psnr(const LLImageRaw* decoded, const LLImageRaw* reference)
    {
        REQUIRE(decoded->getWidth() == reference->getWidth());
        REQUIRE(decoded->getHeight() == reference->getHeight());
        REQUIRE(decoded->getComponents() == reference->getComponents());
        const S32 count = decoded->getDataSize();
        F64 squared_error = 0.0;
        for (S32 i = 0; i < count; ++i)
        {
            const F64 difference = F64(decoded->getData()[i]) - F64(reference->getData()[i]);
            squared_error += difference * difference;
        }
        if (squared_error == 0.0)
        {
            return IDENTICAL_DB;
        }
        return 10.0 * std::log10(255.0 * 255.0 * count / squared_error);
    }

    // PSNR of a decode at `discard` from the first `bytes` against the decode at `discard`
    // from all of them.
    F64 prefixQuality(const std::vector<U8>& codestream, S32 bytes, S32 discard)
    {
        const S32 size = S32(codestream.size());
        LLPointer<LLImageRaw> reference = decodePrefix(codestream.data(), size, discard);
        LLPointer<LLImageRaw> prefix = decodePrefix(codestream.data(), std::min(bytes, size), discard);
        return psnr(prefix, reference);
    }

    std::vector<U8> bytesOf(const LLImageJ2C* j2c)
    {
        return std::vector<U8>(j2c->getData(), j2c->getData() + j2c->getDataSize());
    }

    // Offset of the first SOT marker, found by walking the main header's segments.
    S32 firstTilePartOffset(const std::vector<U8>& codestream)
    {
        S32 pos = 2;
        while (codestream[pos + 1] != 0x90)
        {
            pos += 2 + ((codestream[pos + 2] << 8) | codestream[pos + 3]);
        }
        return pos;
    }

    TEST_CASE("alj2clayout: the viewer's encoder writes a layout the parser reads whole", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(256, 3, 1);
        LLPointer<LLImageJ2C> j2c = encodeLayered(raw);
        const std::vector<U8> codestream = bytesOf(j2c);

        const ALJ2CLayout layout = ALJ2CLayout::parse(codestream.data(), S32(codestream.size()));
        CHECK(layout.isValid());
        CHECK(layout.mWidth == 256);
        CHECK(layout.mHeight == 256);
        CHECK(layout.mComponents == 3);
        CHECK(layout.mTiles == 1);
        CHECK(layout.mProgression == ALJ2CLayout::LRCP);
        CHECK(layout.mLayers > 1);
        CHECK(layout.mLevels == 5);
        CHECK(layout.mHeaderLength == firstTilePartOffset(codestream));
        CHECK(layout.mLength == S32(codestream.size()));
    }

    TEST_CASE("alj2clayout: a single layer codestream's length is its first tile-part's", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(256, 4, 2);
        const std::vector<U8> codestream = encodeSingleLayer(raw);

        const ALJ2CLayout layout = ALJ2CLayout::parse(codestream.data(), S32(codestream.size()));
        CHECK(layout.isValid());
        CHECK(layout.mComponents == 4);
        CHECK(layout.mLayers == 1);
        CHECK(layout.mLength == S32(codestream.size()));
    }

    TEST_CASE("alj2clayout: the first packet's worth of bytes holds the whole layout", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(512, 3, 3);
        const std::vector<U8> codestream = bytesOf(encodeLayered(raw));
        REQUIRE(S32(codestream.size()) > FIRST_PACKET_SIZE);

        const ALJ2CLayout whole = ALJ2CLayout::parse(codestream.data(), S32(codestream.size()));
        const ALJ2CLayout first = ALJ2CLayout::parse(codestream.data(), FIRST_PACKET_SIZE);
        CHECK(first.isValid());
        CHECK(first.mLength == whole.mLength);
        CHECK(first.mHeaderLength == whole.mHeaderLength);
        CHECK(first.mLayers == whole.mLayers);
    }

    TEST_CASE("alj2clayout: a header cut anywhere before the first SOT's fields end is no layout", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(128, 3, 4);
        const std::vector<U8> codestream = bytesOf(encodeLayered(raw));
        const S32 sot_fields_end = firstTilePartOffset(codestream) + 12;

        for (S32 size = 0; size < sot_fields_end; ++size)
        {
            CAPTURE(size);
            CHECK_FALSE(ALJ2CLayout::parse(codestream.data(), size).isValid());
        }
        CHECK(ALJ2CLayout::parse(codestream.data(), sot_fields_end).isValid());
    }

    TEST_CASE("alj2clayout: a main header with no SOT after it is no layout", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(128, 3, 5);
        std::vector<U8> codestream = bytesOf(encodeLayered(raw));
        codestream.resize(firstTilePartOffset(codestream));
        codestream.push_back(0xFF);
        codestream.push_back(0xD9);

        CHECK_FALSE(ALJ2CLayout::parse(codestream.data(), S32(codestream.size())).isValid());
    }

    TEST_CASE("alj2clayout: bytes that are not a codestream are no layout", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(128, 3, 6);
        std::vector<U8> codestream = bytesOf(encodeLayered(raw));

        CHECK_FALSE(ALJ2CLayout::parse(nullptr, 600).isValid());
        std::vector<U8> shifted(codestream.begin() + 1, codestream.end());
        CHECK_FALSE(ALJ2CLayout::parse(shifted.data(), S32(shifted.size())).isValid());

        // A segment length that runs past the data.
        codestream[4] = 0xFF;
        codestream[5] = 0xFF;
        CHECK_FALSE(ALJ2CLayout::parse(codestream.data(), S32(codestream.size())).isValid());
    }

    TEST_CASE("alj2clayout: a codestream of several tiles leaves its length unknown", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(256, 3, 7);
        const std::vector<U8> codestream = encodeSingleLayer(raw, true);

        const ALJ2CLayout layout = ALJ2CLayout::parse(codestream.data(), S32(codestream.size()));
        CHECK(layout.mTiles == 4);
        CHECK(layout.mWidth == 256);
        CHECK_FALSE(layout.isValid());
        CHECK(layout.calcDataSize(1) == 0);
    }

    TEST_CASE("alj2clayout: an image learns its layout as the data comes in", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(256, 3, 8);
        const std::vector<U8> codestream = bytesOf(encodeLayered(raw));
        const S32 sot_fields_end = firstTilePartOffset(codestream) + 12;

        LLPointer<LLImageJ2C> j2c = new LLImageJ2C;
        S32 appended = 0;
        while (appended < S32(codestream.size()))
        {
            const S32 chunk = std::min(100, S32(codestream.size()) - appended);
            U8* data = (U8*)ll_aligned_malloc_16(chunk);
            memcpy(data, codestream.data() + appended, chunk);
            j2c->appendData(data, chunk);
            appended += chunk;
            CAPTURE(appended);
            CHECK(j2c->getLayout().isValid() == (appended >= sot_fields_end));
        }
        CHECK(j2c->getLayout().mLength == S32(codestream.size()));

        const S32 whole = j2c->getLayout().mLength;
        CHECK(j2c->calcDataSize(0) == whole);
        CHECK(j2c->calcDiscardLevelBytes(whole) == 0);
    }

    TEST_CASE("alj2clayout: targets grow with resolution and each reaches its own discard level", "[alj2clayout]")
    {
        LLPointer<LLImageRaw> raw = makeTexture(1024, 3, 9);
        const std::vector<U8> codestream = bytesOf(encodeLayered(raw));
        const ALJ2CLayout layout = ALJ2CLayout::parse(codestream.data(), S32(codestream.size()));
        REQUIRE(layout.isValid());

        CHECK(layout.calcDataSize(0) == layout.mLength);
        for (S32 discard = 1; discard <= MAX_DISCARD_LEVEL; ++discard)
        {
            CAPTURE(discard);
            const S32 target = layout.calcDataSize(discard);
            CHECK(target <= layout.calcDataSize(discard - 1));
            CHECK(target >= FIRST_PACKET_SIZE);
            CHECK(layout.calcDiscardLevelBytes(target) <= discard);
            if (discard < MAX_DISCARD_LEVEL)
            {
                CHECK(layout.calcDiscardLevelBytes(target - 1) > discard);
            }
        }
        CHECK(layout.calcDiscardLevelBytes(0) == MAX_DISCARD_LEVEL);
    }

    // Each codestream is decoded at every discard level from the bytes its image asks for,
    // and at discard 1 from the bytes the size-only estimate gives a codestream whose
    // headers are not in hand, which cuts both kinds short.
    void checkTargets(const std::vector<U8>& codestream, S32 size, S32 components)
    {
        LLPointer<LLImageJ2C> j2c = imageFrom(codestream.data(), S32(codestream.size()));
        REQUIRE(j2c->getLayout().isValid());

        for (S32 discard = 1; discard <= MAX_DISCARD_LEVEL; ++discard)
        {
            CAPTURE(discard, codestream.size());
            const S32 target = j2c->calcDataSize(discard);
            CAPTURE(target);
            CHECK(prefixQuality(codestream, target, discard) >= QUALITY_BAR_DB);
        }

        const S32 estimate = LLImageJ2C::calcDataSizeJ2C(size, size, components, 1);
        CAPTURE(estimate, codestream.size());
        CHECK(prefixQuality(codestream, estimate, 1) < QUALITY_BAR_DB);
    }

    TEST_CASE("alj2clayout: a layered codestream decodes within the quality bar at every target", "[alj2clayout]")
    {
        for (S32 size : { 512, 1024 })
        {
            CAPTURE(size);
            LLPointer<LLImageRaw> raw = makeTexture(size, 3, 10 + size);
            checkTargets(bytesOf(encodeLayered(raw)), size, 3);
        }
    }

    TEST_CASE("alj2clayout: a single layer codestream decodes within the quality bar at every target", "[alj2clayout]")
    {
        for (S32 size : { 512, 1024 })
        {
            CAPTURE(size);
            LLPointer<LLImageRaw> raw = makeTexture(size, 3, 20 + size);
            checkTargets(encodeSingleLayer(raw), size, 3);
        }
    }
}

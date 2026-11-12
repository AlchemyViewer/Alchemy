/**
 * @file alimagej2crs_test.cpp
 * @brief Encodes and decodes through LLImageJ2C with pnj2k, held against the
 *        images encoded and against pnj2k's own C API
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
#include "llimagej2c.h"

#include "pnj2k.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // A lossy encode of the generated textures, decoded whole, is at least
    // this close to the image encoded. They are mostly noise, which the
    // upload profile's eighth of the raw size codes to 25 dB or better; the
    // same image with its rows the other way up is 13 dB at best.
    constexpr F64 LOSSY_BAR_DB = 20.0;

    // An LLImageJ2C that decodes and encodes with pnj2k.
    class TestJ2C : public LLImageJ2C
    {
    public:
        TestJ2C() { mImpl.reset(new ALImageJ2CRS); }
    };

    LLPointer<TestJ2C> imageFrom(const U8* data, S32 size)
    {
        LLPointer<TestJ2C> j2c = new TestJ2C;
        U8* copy = (U8*)ll_aligned_malloc_16(size);
        memcpy(copy, data, size);
        j2c->setData(copy, size);
        return j2c;
    }

    // What the decode thread makes of the first `size` bytes at `discard`,
    // for the channels from `first`.
    struct Decode
    {
        bool metadata = false; // the header read
        bool decoded = false;
        S32 width = 0;
        S32 height = 0;
        S32 components = 0;
        S32 full_width = 0;
        S32 full_height = 0;
        S32 full_components = 0;
        S32 discard = 0;
        std::string comment;
        std::vector<U8> pixels; // bottom row first
    };

    Decode decode(const U8* data, S32 size, S32 discard, S32 first = 0, S32 count = 4)
    {
        Decode out;
        LLPointer<TestJ2C> j2c = imageFrom(data, size);
        out.metadata = j2c->updateData();
        if (!out.metadata)
        {
            return out;
        }
        out.full_width = j2c->getWidth();
        out.full_height = j2c->getHeight();
        out.full_components = j2c->getComponents();
        j2c->setDiscardLevel(S8(discard));
        LLPointer<LLImageRaw> raw =
            new LLImageRaw(j2c->getWidth(), j2c->getHeight(), S8(first == 0 ? j2c->getComponents() : 1));
        out.decoded = j2c->decodeChannels(raw, 0.f, first, count) && raw->getData();
        if (out.decoded)
        {
            out.width = raw->getWidth();
            out.height = raw->getHeight();
            out.components = raw->getComponents();
            out.discard = j2c->getDiscardLevel();
            out.comment = raw->getComment();
            out.pixels.assign(raw->getData(), raw->getData() + raw->getDataSize());
        }
        return out;
    }

    // The 8-bit format of one to four channels.
    constexpr U32 FORMATS[] = { PNJ2K_FORMAT_GRAY8, PNJ2K_FORMAT_GRAY_ALPHA8, PNJ2K_FORMAT_RGB8,
                                PNJ2K_FORMAT_RGBA8 };

    // What pnj2k's own API makes of the same bytes at the same discard, for
    // the same channels, subsampled ones interpolated up to the image's size:
    // the codestream's rows as it holds them, the top one first.
    struct Native
    {
        bool header = false;
        bool decoded = false;
        S32 width = 0;
        S32 height = 0;
        S32 components = 0;
        S32 full_width = 0;
        S32 full_height = 0;
        S32 full_components = 0;
        S32 reduce = 0;
        std::vector<U8> pixels; // top row first
    };

    Native decodeNative(const U8* data, S32 size, S32 discard, S32 first = 0, S32 count = 4)
    {
        Native out;
        pnj2k_info info{};
        info.struct_size = sizeof(info);
        if (pnj2k_probe(data, size_t(size), &info) != PNJ2K_OK)
        {
            return out;
        }
        out.header = true;
        out.full_width = S32(info.width);
        out.full_height = S32(info.height);
        out.full_components = S32(info.components);
        const S32 channels = llclamp(S32(info.components) - first, 0, llmin(count, 4));
        if (channels <= 0)
        {
            return out;
        }

        pnj2k_decode_params params{};
        params.struct_size = sizeof(params);
        params.reduce = llmin(U32(discard), info.max_reduce);
        params.first_component = U32(first);
        params.component_count = U32(channels);
        params.format = FORMATS[channels - 1];
        params.flags = PNJ2K_UPSAMPLE | PNJ2K_INTERPOLATE;

        pnj2k_decoder* decoder = pnj2k_decoder_new(nullptr);
        REQUIRE(decoder);
        pnj2k_image image{};
        image.struct_size = sizeof(image);
        if (pnj2k_decode(decoder, data, size_t(size), &params, &image) == PNJ2K_OK)
        {
            out.decoded = true;
            out.width = S32(image.width);
            out.height = S32(image.height);
            out.components = channels;
            out.reduce = S32(params.reduce);
            const size_t row = size_t(image.width) * channels;
            const size_t stride = image.stride ? image.stride : row;
            for (U32 y = 0; y < image.height; ++y)
            {
                out.pixels.insert(out.pixels.end(), image.pixels + y * stride, image.pixels + y * stride + row);
            }
            pnj2k_image_free(&image);
        }
        pnj2k_decoder_free(decoder);
        return out;
    }

    // `pixels` with its rows in the other order.
    std::vector<U8> flipRows(const std::vector<U8>& pixels, S32 width, S32 height, S32 components)
    {
        const size_t row = size_t(width) * components;
        std::vector<U8> out(pixels.size());
        for (S32 y = 0; y < height; ++y)
        {
            memcpy(out.data() + size_t(y) * row, pixels.data() + size_t(height - 1 - y) * row, row);
        }
        return out;
    }

    // Whether LLImageJ2C's decode is pnj2k's own with its rows the other way
    // up, or neither read the bytes.
    bool same(const Decode& ours, const Native& theirs, std::string& why)
    {
        if (ours.metadata != theirs.header)
        {
            why = ours.metadata ? "only LLImageJ2C read the header" : "only pnj2k's API read the header";
            return false;
        }
        if (!ours.metadata)
        {
            return true;
        }
        if (ours.full_width != theirs.full_width || ours.full_height != theirs.full_height
            || ours.full_components != theirs.full_components)
        {
            why = "the headers' sizes differ";
            return false;
        }
        if (ours.decoded != theirs.decoded)
        {
            why = ours.decoded ? "only LLImageJ2C decoded" : "only pnj2k's API decoded";
            return false;
        }
        if (!ours.decoded)
        {
            return true;
        }
        if (ours.width != theirs.width || ours.height != theirs.height || ours.components != theirs.components
            || ours.discard != theirs.reduce)
        {
            std::ostringstream s;
            s << "LLImageJ2C " << ours.width << "x" << ours.height << "x" << ours.components << " at "
              << ours.discard << ", pnj2k's API " << theirs.width << "x" << theirs.height << "x"
              << theirs.components << " at " << theirs.reduce;
            why = s.str();
            return false;
        }
        if (ours.pixels != flipRows(theirs.pixels, theirs.width, theirs.height, theirs.components))
        {
            why = "the pixels differ";
            return false;
        }
        return true;
    }

    // A texture with detail at every scale and every channel its own.
    LLPointer<LLImageRaw> makeTexture(S32 size, S32 components)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(U16(size), U16(size), S8(components));
        U8* data = raw->getData();
        U32 h = 0x9e3779b9u;
        for (S32 y = 0; y < size; ++y)
        {
            for (S32 x = 0; x < size; ++x)
            {
                h ^= h << 13;
                h ^= h >> 17;
                h ^= h << 5;
                for (S32 c = 0; c < components; ++c)
                {
                    const S32 smooth = (x * (c + 1) * 255 / size + y * 255 / size) / 2;
                    const S32 noise = S32((h >> (8 * c)) & 0x3f) - 32;
                    data[(y * size + x) * components + c] = U8(llclamp(smooth + noise, 0, 255));
                }
            }
        }
        return raw;
    }

    std::vector<U8> bytesOf(const LLImageRaw* raw)
    {
        return std::vector<U8>(raw->getData(), raw->getData() + raw->getDataSize());
    }

    F64 psnr(const std::vector<U8>& decoded, const std::vector<U8>& reference)
    {
        REQUIRE(decoded.size() == reference.size());
        F64 squared_error = 0.0;
        for (size_t i = 0; i < decoded.size(); ++i)
        {
            const F64 difference = F64(decoded[i]) - F64(reference[i]);
            squared_error += difference * difference;
        }
        if (squared_error == 0.0)
        {
            return 99.0;
        }
        return 10.0 * std::log10(255.0 * 255.0 * F64(decoded.size()) / squared_error);
    }

    // The layers of Second Life's upload profile for an image of `raw`
    // bytes: 600 and 2000 bytes, then four times more up to an eighth of the
    // image, which is the last lossy layer, and one of every pass more when
    // lossless.
    U32 secondLifeLayers(size_t raw, bool lossless)
    {
        const size_t share = raw / 8;
        U32 layers = 1;
        size_t last = 600;
        for (size_t bytes = 2000; bytes < share; bytes *= 4)
        {
            ++layers;
            last = bytes;
        }
        if (last < share)
        {
            ++layers;
        }
        return layers + (lossless ? 1 : 0);
    }

    std::vector<U8> encode(const LLImageRaw* raw, bool lossless, const char* comment,
                           EJ2CContent content = EJ2CContent::Colour)
    {
        LLPointer<TestJ2C> j2c = new TestJ2C;
        j2c->setReversible(lossless);
        j2c->setEncodeContent(content);
        REQUIRE(j2c->encode(raw, comment, 0.f));
        return std::vector<U8>(j2c->getData(), j2c->getData() + j2c->getDataSize());
    }

    // One component of a planar image: `width` by `height` samples, one for
    // every `step` points of the image's grid each way, the top row first.
    struct Component
    {
        S32 width = 0;
        S32 height = 0;
        U32 step = 1;
        std::vector<U8> samples;
    };

    // A lossless codestream of `components` over a grid `width` by `height`,
    // from pnj2k's Part 1 encoder, which codes each component at its own
    // size; LLImageJ2C's encode takes pixels, whose channels are of one.
    std::vector<U8> encodeComponents(S32 width, S32 height, const std::vector<Component>& components)
    {
        std::vector<pnj2k_component_source> sources;
        for (const Component& component : components)
        {
            pnj2k_component_source source{};
            source.struct_size = sizeof(source);
            source.samples = component.samples.data();
            source.size = component.samples.size();
            source.sample_type = PNJ2K_SOURCE_U8;
            source.width = U32(component.width);
            source.height = U32(component.height);
            source.precision = 8;
            source.dx = component.step;
            source.dy = component.step;
            sources.push_back(source);
        }
        pnj2k_components image{};
        image.struct_size = sizeof(image);
        image.width = U32(width);
        image.height = U32(height);
        image.components = sources.data();
        image.count = sources.size();

        pnj2k_encode_params params{};
        params.struct_size = sizeof(params);
        params.flags = PNJ2K_ENCODE_REVERSIBLE;

        pnj2k_encoder* encoder = pnj2k_encoder_new(nullptr);
        REQUIRE(encoder);
        pnj2k_buffer out{};
        out.struct_size = sizeof(out);
        const pnj2k_status status = pnj2k_encode_components(encoder, &image, &params, &out);
        const char* error = pnj2k_encoder_last_error(encoder);
        const std::string why = error ? error : "";
        std::vector<U8> bytes;
        if (status == PNJ2K_OK)
        {
            bytes.assign(out.data, out.data + out.len);
        }
        pnj2k_buffer_free(&out);
        pnj2k_encoder_free(encoder);
        INFO(why);
        REQUIRE(status == PNJ2K_OK);
        return bytes;
    }

    // The main header's marker segment `code`'s body, or nothing.
    std::vector<U8> segment(const std::vector<U8>& data, U8 code)
    {
        size_t pos = 2;
        while (pos + 4 <= data.size() && data[pos] == 0xFF && data[pos + 1] != 0x90)
        {
            const size_t length = (size_t(data[pos + 2]) << 8) | data[pos + 3];
            if (data[pos + 1] == code && pos + 2 + length <= data.size())
            {
                return std::vector<U8>(data.begin() + pos + 4, data.begin() + pos + 2 + length);
            }
            pos += 2 + length;
        }
        return {};
    }

    // The prefixes of `data` a decode at `discard` is checked on: the whole,
    // the bytes the fetcher asks for at that discard, and parts of those.
    std::vector<S32> prefixes(const std::vector<U8>& data, S32 discard)
    {
        LLPointer<TestJ2C> j2c = imageFrom(data.data(), S32(data.size()));
        std::vector<S32> sizes{ S32(data.size()) };
        if (!j2c->updateData())
        {
            return sizes;
        }
        const S32 header = j2c->getLayout().mHeaderLength;
        const S32 target = llmin(j2c->calcDataSize(discard), S32(data.size()));
        for (S32 size : { target, target * 3 / 4, target / 2, header + (target - header) / 8 })
        {
            if (size >= 16 && size >= header)
            {
                sizes.push_back(size);
            }
        }
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        return sizes;
    }

    struct Tally
    {
        size_t decodes = 0;   // both decoded, to the same image
        size_t no_header = 0; // neither read a header
        size_t no_decode = 0; // both read the header, neither decoded
        std::vector<std::string> differences;
    };

    // Every discard of every prefix of `data`, and the last channel alone of
    // an image of more than three, the alpha or a bake's aux channel,
    // decoded through LLImageJ2C and through pnj2k's own API.
    void sweep(const std::string& name, const std::vector<U8>& data, Tally& tally)
    {
        for (S32 discard = 0; discard <= MAX_DISCARD_LEVEL; ++discard)
        {
            for (S32 size : prefixes(data, discard))
            {
                const Decode ours = decode(data.data(), size, discard);
                std::vector<std::pair<Decode, Native>> pairs{ { ours, decodeNative(data.data(), size, discard) } };
                if (ours.decoded && ours.full_components > 3)
                {
                    const S32 last = ours.full_components - 1;
                    pairs.emplace_back(decode(data.data(), size, discard, last, 4),
                                       decodeNative(data.data(), size, discard, last, 4));
                }
                for (const auto& [a, b] : pairs)
                {
                    std::string why;
                    if (!same(a, b, why))
                    {
                        tally.differences.push_back(name + " at " + std::to_string(size) + " of "
                                                    + std::to_string(data.size()) + " bytes, discard "
                                                    + std::to_string(discard) + ": " + why);
                    }
                    else if (a.decoded)
                    {
                        ++tally.decodes;
                    }
                    else if (a.metadata)
                    {
                        ++tally.no_decode;
                    }
                    else
                    {
                        ++tally.no_header;
                    }
                }
            }
        }
    }

    // The codestreams in `dir`, by name.
    std::vector<std::filesystem::path> texturesIn(const std::filesystem::path& dir)
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            const std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".j2c" || ext == ".j2k"))
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    // The textures AL_J2C_CORPUS_DIR names, or none where it names none. A
    // name that is not a directory fails the case.
    std::optional<std::vector<std::filesystem::path>> corpus()
    {
        const char* dir = std::getenv("AL_J2C_CORPUS_DIR");
        if (!dir || !*dir)
        {
            return std::nullopt;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
        {
            throw std::filesystem::filesystem_error("AL_J2C_CORPUS_DIR", dir,
                                                    ec ? ec : std::make_error_code(std::errc::not_a_directory));
        }
        return texturesIn(dir);
    }

    std::vector<U8> readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<U8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void report(const Tally& tally)
    {
        for (size_t i = 0; i < std::min<size_t>(tally.differences.size(), 50); ++i)
        {
            UNSCOPED_INFO(tally.differences[i]);
        }
        CHECK(tally.differences.empty());
    }
} // namespace

// The texture cache's header holds the engine info in 32 bytes, its
// terminator included, and fails at startup on a longer one.
TEST_CASE("alimagej2crs: names pnj2k in engine info the texture cache can hold", "[alimagej2crs]")
{
    const std::string info = LLImageJ2C::getEngineInfo();
    CHECK(info.find("pnj2k") != std::string::npos);
    CHECK(info.size() + 1 <= 32);
}

// What it encodes is the profile every viewer streams: five levels, LRCP,
// 64x64 code-blocks, one tile, the layers' count. It decodes to the image
// encoded, exactly when lossless, at every discard to that discard's size,
// and from every prefix as pnj2k's own API decodes the same bytes.
TEST_CASE("alimagej2crs: decodes what it encodes at every discard and prefix", "[alimagej2crs]")
{
    for (S32 size : { 4, 37, 128 })
    {
        for (S32 components : { 1, 3, 4 })
        {
            for (bool lossless : { true, false })
            {
                CAPTURE(size, components, lossless);
                LLPointer<LLImageRaw> raw = makeTexture(size, components);
                const std::vector<U8> original = bytesOf(raw);
                const std::vector<U8> data = encode(raw, lossless, "a=uploader&z=20261031");

                const std::vector<U8> cod = segment(data, 0x52);
                REQUIRE(cod.size() >= 10);
                CHECK(cod[1] == 0);             // LRCP
                CHECK(((U32(cod[2]) << 8) | cod[3]) == secondLifeLayers(size_t(raw->getDataSize()), lossless));
                CHECK(cod[5] == 5);             // five levels at every size
                CHECK((cod[6] == 4 && cod[7] == 4)); // 64x64 code-blocks
                CHECK(cod[8] == 0);             // no code-block style
                CHECK(cod[9] == (lossless ? 1 : 0));

                Tally tally;
                sweep("encoded", data, tally);
                report(tally);
                CHECK(tally.decodes >= 6);
                CHECK(tally.no_header == 0);

                // The whole texture at full resolution, as the viewer asks for it.
                const Decode whole = decode(data.data(), S32(data.size()), 0);
                REQUIRE(whole.decoded);
                CHECK(whole.width == size);
                CHECK(whole.height == size);
                CHECK(whole.components == components);
                CHECK(whole.comment == "a=uploader&z=20261031");
                if (lossless)
                {
                    CHECK(whole.pixels == original);
                }
                else
                {
                    CHECK(psnr(whole.pixels, original) >= LOSSY_BAR_DB);
                }

                for (S32 discard = 1; discard <= MAX_DISCARD_LEVEL; ++discard)
                {
                    CAPTURE(discard);
                    const Decode reduced = decode(data.data(), S32(data.size()), discard);
                    const S32 reduced_size = (size + (1 << discard) - 1) >> discard;
                    REQUIRE(reduced.decoded);
                    CHECK(reduced.discard == discard);
                    CHECK(reduced.width == reduced_size);
                    CHECK(reduced.height == reduced_size);
                }

                // The alpha alone, as a decode of one channel gives it.
                if (components == 4 && lossless)
                {
                    const Decode alpha = decode(data.data(), S32(data.size()), 0, 3, 4);
                    REQUIRE(alpha.decoded);
                    REQUIRE(alpha.components == 1);
                    REQUIRE(alpha.pixels.size() == size_t(size) * size);
                    bool equal = true;
                    for (size_t i = 0; i < alpha.pixels.size(); ++i)
                    {
                        equal = equal && alpha.pixels[i] == original[i * 4 + 3];
                    }
                    CHECK(equal);
                }
            }
        }
    }
}

// The raw image's rows run bottom to top, as OpenGL's do, and a codestream's
// top to bottom: pnj2k's own API, which flips nothing, reads the encode's
// rows in the other order.
TEST_CASE("alimagej2crs: writes the image's top row first", "[alimagej2crs]")
{
    LLPointer<LLImageRaw> raw = makeTexture(32, 3);
    const std::vector<U8> data = encode(raw, true, nullptr);
    const Native native = decodeNative(data.data(), S32(data.size()), 0);
    REQUIRE(native.decoded);
    CHECK(native.pixels == flipRows(bytesOf(raw), 32, 32, 3));
    CHECK(native.pixels != bytesOf(raw));
}

TEST_CASE("alimagej2crs: the encode content reaches the encoder", "[alimagej2crs]")
{
    // Packed data has no colour transform; colour has it.
    LLPointer<LLImageRaw> raw = makeTexture(64, 3);
    const std::vector<U8> data_cod = segment(encode(raw, false, nullptr, EJ2CContent::Data), 0x52);
    const std::vector<U8> colour_cod = segment(encode(raw, false, nullptr, EJ2CContent::Colour), 0x52);
    REQUIRE(data_cod.size() >= 5);
    REQUIRE(colour_cod.size() >= 5);
    CHECK(data_cod[4] == 0);
    CHECK(colour_cod[4] == 1);

    // An opaque alpha is coded as three components.
    LLPointer<LLImageRaw> rgba = makeTexture(64, 4);
    for (S32 i = 0; i < 64 * 64; ++i)
    {
        rgba->getData()[i * 4 + 3] = 255;
    }
    const std::vector<U8> opaque = encode(rgba, false, nullptr);
    const Decode ours = decode(opaque.data(), S32(opaque.size()), 0);
    CHECK(ours.full_components == 3);

    // A lossless normal map is no larger than with the colour transform.
    LLPointer<LLImageRaw> normals = makeTexture(64, 3);
    const std::vector<U8> normal = encode(normals, true, nullptr, EJ2CContent::Normal);
    const std::vector<U8> colour = encode(normals, true, nullptr, EJ2CContent::Colour);
    CHECK(normal.size() <= colour.size());
    const Decode back = decode(normal.data(), S32(normal.size()), 0);
    CHECK(back.pixels == bytesOf(normals));
}

TEST_CASE("alimagej2crs: an upload's resize averages colour in linear light", "[alimagej2crs]")
{
    // Black and white columns.
    LLPointer<LLImageRaw> raw = new LLImageRaw(4, 2, 3);
    for (S32 i = 0; i < 4 * 2; ++i)
    {
        memset(raw->getData() + i * 3, i % 2 ? 255 : 0, 3);
    }
    LLPointer<TestJ2C> j2c = new TestJ2C;
    REQUIRE(j2c->scaleForEncode(*raw, 2, 1));
    CHECK(raw->getWidth() == 2);
    CHECK(raw->getHeight() == 1);
    // Half the light is sRGB 188, not the 128 of averaged code values.
    for (S32 i = 0; i < 6; ++i)
    {
        CHECK(std::abs(S32(raw->getData()[i]) - 188) <= 2);
    }

    // Data averages as it is.
    LLPointer<LLImageRaw> data = new LLImageRaw(4, 2, 3);
    for (S32 i = 0; i < 4 * 2; ++i)
    {
        memset(data->getData() + i * 3, i % 2 ? 255 : 0, 3);
    }
    j2c->setEncodeContent(EJ2CContent::Data);
    REQUIRE(j2c->scaleForEncode(*data, 2, 1));
    CHECK(std::abs(S32(data->getData()[0]) - 128) <= 1);
}

TEST_CASE("alimagej2crs: refuses what is not a codestream", "[alimagej2crs]")
{
    std::vector<U8> junk(64, 0x5a);
    const Decode ours = decode(junk.data(), S32(junk.size()), 0);
    CHECK_FALSE(ours.metadata);

    // The main header alone, cut before the tile-part: no packet has
    // arrived, so it decodes to mid-grey.
    const std::vector<U8> data = encode(makeTexture(64, 3), false, nullptr);
    LLPointer<TestJ2C> j2c = imageFrom(data.data(), S32(data.size()));
    REQUIRE(j2c->updateData());
    const S32 header = j2c->getLayout().mHeaderLength;
    const Decode cut = decode(data.data(), header, 0);
    CHECK(cut.metadata);
    REQUIRE(cut.decoded);
    CHECK(std::all_of(cut.pixels.begin(), cut.pixels.end(), [](U8 v) { return v == 128; }));
}

// A header alone decodes, to mid-grey of the size it declares, so a size
// past what the viewer holds is refused where the header is read, before a
// raw image is sized by it, and again by a decode that read no header.
TEST_CASE("alimagej2crs: refuses an image larger than the viewer holds", "[alimagej2crs]")
{
    const std::vector<U8> data = encode(makeTexture(64, 3), false, nullptr);
    REQUIRE(data.size() > 32);
    REQUIRE(data[2] == 0xFF);
    REQUIRE(data[3] == 0x51);
    // SIZ follows SOC: the image's size at bytes 8 and 12, its one tile's
    // at 24 and 28.
    auto sized = [&](U32 width, U32 height)
    {
        std::vector<U8> out = data;
        auto put = [&](size_t at, U32 value)
        {
            out[at] = U8(value >> 24);
            out[at + 1] = U8(value >> 16);
            out[at + 2] = U8(value >> 8);
            out[at + 3] = U8(value);
        };
        put(8, width);
        put(12, height);
        put(24, width);
        put(28, height);
        return out;
    };

    const std::vector<U8> largest = sized(MAX_IMAGE_SIZE, 4);
    CHECK(imageFrom(largest.data(), S32(largest.size()))->updateData());

    for (bool wide : { true, false })
    {
        CAPTURE(wide);
        const std::vector<U8> past = wide ? sized(MAX_IMAGE_SIZE + 1, 4) : sized(4, MAX_IMAGE_SIZE + 1);
        LLPointer<TestJ2C> j2c = imageFrom(past.data(), S32(past.size()));
        CHECK_FALSE(j2c->updateData());
        CHECK(j2c->getWidth() == 0);
        CHECK(j2c->getHeight() == 0);

        LLPointer<TestJ2C> unread = imageFrom(past.data(), S32(past.size()));
        unread->setDiscardLevel(0);
        LLPointer<LLImageRaw> raw = new LLImageRaw(4, 4, 3);
        CHECK_FALSE(unread->decode(raw, 0.f));
    }
}

// The raw image is sized by the header's components too, whatever a decode
// then takes of them, so more than an image may have are refused as well.
TEST_CASE("alimagej2crs: refuses an image of more components than the viewer holds", "[alimagej2crs]")
{
    const std::vector<U8> data = encode(makeTexture(64, 3), false, nullptr);
    REQUIRE(data.size() > 51);
    REQUIRE(data[2] == 0xFF);
    REQUIRE(data[3] == 0x51);
    // SIZ follows SOC: its length at byte 4, the component count at 40 and
    // three bytes a component from 42. Each component is made as the first.
    auto with_components = [&](U32 count)
    {
        const size_t old_count = (size_t(data[40]) << 8) | data[41];
        std::vector<U8> out(data.begin(), data.begin() + 42);
        const U32 length = 38 + 3 * count;
        out[4] = U8(length >> 8);
        out[5] = U8(length);
        out[40] = U8(count >> 8);
        out[41] = U8(count);
        for (U32 i = 0; i < count; ++i)
        {
            out.insert(out.end(), data.begin() + 42, data.begin() + 45);
        }
        out.insert(out.end(), data.begin() + 42 + 3 * old_count, data.end());
        return out;
    };

    const std::vector<U8> most = with_components(MAX_IMAGE_COMPONENTS);
    LLPointer<TestJ2C> held = imageFrom(most.data(), S32(most.size()));
    CHECK(held->updateData());
    CHECK(held->getComponents() == MAX_IMAGE_COMPONENTS);

    for (U32 count : { U32(MAX_IMAGE_COMPONENTS + 1), 16u, 256u })
    {
        CAPTURE(count);
        const std::vector<U8> past = with_components(count);
        LLPointer<TestJ2C> j2c = imageFrom(past.data(), S32(past.size()));
        CHECK_FALSE(j2c->updateData());
        CHECK(j2c->getComponents() == 0);

        LLPointer<TestJ2C> unread = imageFrom(past.data(), S32(past.size()));
        unread->setDiscardLevel(0);
        LLPointer<LLImageRaw> raw = new LLImageRaw(4, 4, 3);
        CHECK_FALSE(unread->decode(raw, 0.f));
    }
}

// A decode's samples are held to what an image may hold. The largest image
// the viewer holds decodes within that, since a decode takes little more
// than its output. A tile pnj2k decodes whole takes about twenty times its
// output, and one of a size the viewer holds is refused: here a reversible
// tile of one component under a region-of-interest shift of 25, more
// bit-planes than the decoder's line-by-line paths hold.
TEST_CASE("alimagej2crs: holds a decode's samples to what an image may hold", "[alimagej2crs]")
{
    {
        LLPointer<LLImageRaw> raw = makeTexture(MAX_IMAGE_SIZE, 4);
        for (bool lossless : { false, true })
        {
            CAPTURE(lossless);
            const std::vector<U8> data = encode(raw, lossless, nullptr);
            const Decode whole = decode(data.data(), S32(data.size()), 0);
            REQUIRE(whole.decoded);
            CHECK(whole.width == MAX_IMAGE_SIZE);
            CHECK(whole.height == MAX_IMAGE_SIZE);
            CHECK(whole.components == 4);
        }
    }

    std::vector<U8> deep = encode(makeTexture(64, 1), true, nullptr);
    REQUIRE(deep.size() > 51);
    REQUIRE(deep[2] == 0xFF);
    REQUIRE(deep[3] == 0x51);
    // SIZ follows SOC: the image's size at bytes 8 and 12, its one tile's
    // at 24 and 28, and its length at byte 4. RGN goes after it: a length
    // of 5, component 0, the implicit style, the shift.
    auto put = [&](size_t at, U32 value)
    {
        deep[at] = U8(value >> 24);
        deep[at + 1] = U8(value >> 16);
        deep[at + 2] = U8(value >> 8);
        deep[at + 3] = U8(value);
    };
    put(8, MAX_IMAGE_SIZE);
    put(12, MAX_IMAGE_SIZE / 2);
    put(24, MAX_IMAGE_SIZE);
    put(28, MAX_IMAGE_SIZE / 2);
    const size_t siz_end = 4 + ((size_t(deep[4]) << 8) | deep[5]);
    const U8 rgn[] = { 0xFF, 0x5E, 0x00, 0x05, 0x00, 0x00, 25 };
    deep.insert(deep.begin() + siz_end, std::begin(rgn), std::end(rgn));

    LLPointer<TestJ2C> j2c = imageFrom(deep.data(), S32(deep.size()));
    REQUIRE(j2c->updateData());
    CHECK(j2c->getWidth() == MAX_IMAGE_SIZE);
    j2c->setDiscardLevel(0);
    LLPointer<LLImageRaw> raw = new LLImageRaw(j2c->getWidth(), j2c->getHeight(), j2c->getComponents());
    CHECK_FALSE(j2c->decode(raw, 0.f));
    INFO(LLImage::getLastThreadError());
    CHECK(LLImage::getLastThreadError().find("decoded bytes") != std::string::npos);
}

// Components subsampled on the image's grid decode to the size the header
// gives, a pixel on a sample taking it and a pixel between samples the mean
// of those about it: an image whose colour is sampled every other point each
// way, as 4:2:0 has it, whole, one of its colour channels alone, and at every
// discard; and an image whose only component is subsampled.
TEST_CASE("alimagej2crs: brings subsampled components to the image's size", "[alimagej2crs]")
{
    constexpr S32 SIZE = 16;
    constexpr S32 HALF = SIZE / 2;
    Component luma{ SIZE, SIZE, 1, std::vector<U8>(SIZE * SIZE) };
    Component blue{ HALF, HALF, 2, std::vector<U8>(HALF * HALF) };
    Component red{ HALF, HALF, 2, std::vector<U8>(HALF * HALF) };
    for (S32 i = 0; i < SIZE * SIZE; ++i)
    {
        luma.samples[i] = U8(i);
    }
    for (S32 i = 0; i < HALF * HALF; ++i)
    {
        blue.samples[i] = U8(64 + i);
        red.samples[i] = U8(255 - i);
    }

    const std::vector<U8> data = encodeComponents(SIZE, SIZE, { luma, blue, red });
    const Decode whole = decode(data.data(), S32(data.size()), 0);
    REQUIRE(whole.metadata);
    CHECK(whole.full_width == SIZE);
    CHECK(whole.full_height == SIZE);
    CHECK(whole.full_components == 3);
    INFO(LLImage::getLastThreadError());
    REQUIRE(whole.decoded);
    CHECK(whole.width == SIZE);
    CHECK(whole.height == SIZE);
    CHECK(whole.components == 3);

    // A colour component at the pixel `x`, `y`: the mean of the samples
    // about it each way, which are one sample where the pixel lies on it,
    // a half rounded upwards; past the last sample, that sample stands.
    auto colourAt = [&](const Component& component, S32 x, S32 y)
    {
        const S32 x0 = x / 2;
        const S32 x1 = llmin((x + 1) / 2, HALF - 1);
        const S32 y0 = y / 2;
        const S32 y1 = llmin((y + 1) / 2, HALF - 1);
        const std::vector<U8>& samples = component.samples;
        const S32 sum = samples[y0 * HALF + x0] + samples[y0 * HALF + x1] + samples[y1 * HALF + x0]
            + samples[y1 * HALF + x1];
        return U8((sum + 2) >> 2);
    };

    // The raw image's rows run bottom to top.
    std::vector<U8> pixels(SIZE * SIZE * 3);
    std::vector<U8> blues(SIZE * SIZE);
    for (S32 y = 0; y < SIZE; ++y)
    {
        for (S32 x = 0; x < SIZE; ++x)
        {
            const S32 at = (SIZE - 1 - y) * SIZE + x;
            pixels[at * 3] = luma.samples[y * SIZE + x];
            pixels[at * 3 + 1] = colourAt(blue, x, y);
            pixels[at * 3 + 2] = colourAt(red, x, y);
            blues[at] = colourAt(blue, x, y);
        }
    }
    CHECK(whole.pixels == pixels);
    // Not its samples repeated: the second pixel lies between two of them.
    CHECK(whole.pixels[(SIZE - 1) * SIZE * 3 + 3 + 1] == 65);

    const Decode alone = decode(data.data(), S32(data.size()), 0, 1, 1);
    REQUIRE(alone.decoded);
    CHECK(alone.width == SIZE);
    CHECK(alone.height == SIZE);
    CHECK(alone.components == 1);
    CHECK(alone.pixels == blues);

    for (S32 discard = 1; discard <= MAX_DISCARD_LEVEL; ++discard)
    {
        CAPTURE(discard);
        const Decode ours = decode(data.data(), S32(data.size()), discard);
        REQUIRE(ours.decoded);
        CHECK(ours.width == llmax(SIZE >> discard, 1));
        CHECK(ours.height == llmax(SIZE >> discard, 1));
        std::string why;
        const bool matches = same(ours, decodeNative(data.data(), S32(data.size()), discard), why);
        INFO(why);
        CHECK(matches);
    }

    const std::vector<U8> sparse = encodeComponents(SIZE, SIZE, { blue });
    const Decode spread = decode(sparse.data(), S32(sparse.size()), 0);
    REQUIRE(spread.metadata);
    CHECK(spread.full_width == SIZE);
    CHECK(spread.full_height == SIZE);
    REQUIRE(spread.decoded);
    CHECK(spread.width == SIZE);
    CHECK(spread.height == SIZE);
    CHECK(spread.components == 1);
    CHECK(spread.pixels == blues);
}

// Other encoders' codestreams: the textures the viewer ships in its default
// skin, decoded whole and from every prefix at every discard as pnj2k's own
// API decodes them.
TEST_CASE("alimagej2crs: decodes the skin's textures as pnj2k's own API does", "[alimagej2crs]")
{
    const std::vector<std::filesystem::path> files = texturesIn(AL_TEST_SKIN_TEXTURES_DIR);
    REQUIRE(!files.empty());
    Tally tally;
    for (const std::filesystem::path& path : files)
    {
        const std::vector<U8> data = readFile(path);
        CAPTURE(path.filename().string());
        const Decode whole = decode(data.data(), S32(data.size()), 0);
        CHECK(whole.decoded);
        CHECK(whole.width == whole.full_width);
        CHECK(whole.height == whole.full_height);
        sweep(path.filename().string(), data, tally);
    }
    report(tally);
    CHECK(tally.decodes >= files.size() * (MAX_DISCARD_LEVEL + 1));
}

// The real textures AL_J2C_CORPUS_DIR names, decoded through LLImageJ2C at
// every discard, from the whole file, the bytes the fetcher asks for at each
// discard and parts of those, and the last channel alone of four and of
// five-component bakes: each as pnj2k's own API decodes the same bytes. Every
// texture decodes whole, so a corpus of files neither reads does not pass.
TEST_CASE("alimagej2crs: the real texture corpus decodes as pnj2k's own API decodes it", "[alimagej2crs][corpus][.]")
{
    const std::optional<std::vector<std::filesystem::path>> files = corpus();
    if (!files)
    {
        SKIP("AL_J2C_CORPUS_DIR names no textures");
    }
    REQUIRE(!files->empty());
    Tally tally;
    for (const std::filesystem::path& path : *files)
    {
        const std::string name = path.filename().string();
        const std::vector<U8> data = readFile(path);
        if (data.empty() || !decode(data.data(), S32(data.size()), 0).decoded)
        {
            FAIL_CHECK(name << " does not decode");
            continue;
        }
        sweep(name, data, tally);
    }
    std::ostringstream summary;
    summary << files->size() << " textures, " << tally.decodes << " decodes; neither read a header of "
            << tally.no_header << " prefixes, nor decoded " << tally.no_decode;
    WARN(summary.str());
    report(tally);
}

/**
 * @file llimagej2coj.cpp
 * @brief This is an implementation of JPEG2000 encode/decode using OpenJPEG.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llimagej2coj.h"

#include <algorithm>
#include <vector>

// this is defined so that we get static linking.
#include "openjpeg.h"

// Factory function: see declaration in llimagej2c.cpp
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
    return new LLImageJ2COJ();
}

std::string LLImageJ2COJ::getEngineInfo() const
{
#ifdef OPENJPEG_VERSION
    return std::string("OpenJPEG: " OPENJPEG_VERSION ", Runtime: ")
        + opj_version();
#else
    return std::string("OpenJPEG runtime: ") + opj_version();
#endif
}

#if WANT_VERBOSE_OPJ_SPAM
// Return string from message, eliminating final \n if present
static std::string chomp(const char* msg)
{
    // stomp trailing \n
    std::string message = msg ? msg : "";
    if (!message.empty())
    {
        size_t last = message.size() - 1;
        if (message[last] == '\n')
        {
            message.resize(last);
        }
    }
    return message;
}
#endif

class JPEG2KBase
{
public:
    JPEG2KBase() = default;

    U8*         buffer = nullptr;
    OPJ_SIZE_T  size = 0;
    OPJ_OFF_T   offset = 0;
    // Recent OpenJPEG error/warning messages, captured by error_callback and
    // warning_callback so they can be propagated to LLImageJ2C::setLastError.
    // We keep the last few because an error is almost always preceded by a
    // warning that identifies the marker / state — that's what tells us
    // *which* of the 9 "Stream too short" sites in j2k.c actually fired.
    std::vector<std::string> message_log;

    void appendMessage(const char* msg)
    {
        std::string m = msg ? msg : "";
        if (!m.empty() && m.back() == '\n')
            m.pop_back();
        if (m.empty())
            return;
        message_log.push_back(std::move(m));
        constexpr size_t MAX_KEPT = 4;
        if (message_log.size() > MAX_KEPT)
            message_log.erase(message_log.begin(),
                              message_log.begin() + (message_log.size() - MAX_KEPT));
    }

    std::string joinedMessages() const
    {
        std::string out;
        for (size_t i = 0; i < message_log.size(); ++i)
        {
            if (i) out += " | ";
            out += message_log[i];
        }
        return out;
    }
};

/**
 * OpenJPEG callbacks. client_data is the owning JPEG2KBase* so messages get
 * threaded into its ring buffer for later propagation. Always on (no
 * WANT_VERBOSE_OPJ_SPAM guard) — release builds still want to know why a
 * decode failed.
 */
void error_callback(const char* msg, void* client_data)
{
    if (client_data)
    {
        static_cast<JPEG2KBase*>(client_data)->appendMessage(msg);
    }
#if WANT_VERBOSE_OPJ_SPAM
    LL_WARNS() << "LLImageJ2COJ: " << chomp(msg) << LL_ENDL;
#endif
}
void warning_callback(const char* msg, void* client_data)
{
    if (client_data)
    {
        static_cast<JPEG2KBase*>(client_data)->appendMessage(msg);
    }
#if WANT_VERBOSE_OPJ_SPAM
    LL_WARNS() << "LLImageJ2COJ: " << chomp(msg) << LL_ENDL;
#endif
}
void info_callback(const char* msg, void*)
{
#if WANT_VERBOSE_OPJ_SPAM
    LL_INFOS() << "LLImageJ2COJ: " << chomp(msg) << LL_ENDL;
#endif
}

// Divide a by 2 to the power of b and round upwards
int ceildivpow2(int a, int b)
{
    return (a + (1 << b) - 1) >> b;
}

static OPJ_SIZE_T opj_read(void * buffer, OPJ_SIZE_T bytes, void* user_data)
{
    llassert(user_data && buffer);

    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);

    if (jpeg_codec->offset < 0 || static_cast<OPJ_SIZE_T>(jpeg_codec->offset) >= jpeg_codec->size)
    {
        jpeg_codec->offset = jpeg_codec->size;
        return static_cast<OPJ_SIZE_T>(-1); // Indicate EOF
    }

    OPJ_SIZE_T remainder = jpeg_codec->size - static_cast<OPJ_SIZE_T>(jpeg_codec->offset);
    OPJ_SIZE_T to_read = (bytes < remainder) ? bytes : remainder;

    memcpy(buffer, jpeg_codec->buffer + jpeg_codec->offset, to_read);
    jpeg_codec->offset += to_read;

    return to_read;
}

static OPJ_SIZE_T opj_write(void * buffer, OPJ_SIZE_T bytes, void* user_data)
{
    llassert(user_data && buffer);

    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);
    OPJ_OFF_T required_offset = jpeg_codec->offset + static_cast<OPJ_OFF_T>(bytes);

    // Overflow check
    if (required_offset < jpeg_codec->offset)
        return 0; // Overflow detected

    // Resize if needed (exponential growth)
    if (required_offset > static_cast<OPJ_OFF_T>(jpeg_codec->size))
    {
        OPJ_SIZE_T new_size = jpeg_codec->size ? jpeg_codec->size : 1024;
        while (required_offset > static_cast<OPJ_OFF_T>(new_size))
            new_size *= 2;

        const OPJ_SIZE_T MAX_BUFFER_SIZE = 512 * 1024 * 1024; // 512 MB, increase if needed
        if (new_size > MAX_BUFFER_SIZE) return 0;

        U8* new_buffer = (U8*)ll_aligned_malloc_16(new_size);
        if (!new_buffer) return 0; // Allocation failed

        if (jpeg_codec->offset > 0)
            memcpy(new_buffer, jpeg_codec->buffer, static_cast<size_t>(jpeg_codec->offset));

        ll_aligned_free_16(jpeg_codec->buffer);
        jpeg_codec->buffer = new_buffer;
        jpeg_codec->size = new_size;
    }

    memcpy(jpeg_codec->buffer + jpeg_codec->offset, buffer, static_cast<size_t>(bytes));
    jpeg_codec->offset = required_offset;
    return bytes;
}

static OPJ_OFF_T opj_skip(OPJ_OFF_T bytes, void* user_data)
{
    llassert(user_data);
    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);

    OPJ_OFF_T new_offset = jpeg_codec->offset + bytes;

    if (new_offset < 0 || new_offset > static_cast<OPJ_OFF_T>(jpeg_codec->size))
    {
        // Clamp and indicate EOF or error
        jpeg_codec->offset = llclamp<OPJ_OFF_T>(new_offset, 0, static_cast<OPJ_OFF_T>(jpeg_codec->size));
        return (OPJ_OFF_T)-1;
    }

    jpeg_codec->offset = new_offset;
    return bytes;
}

static OPJ_BOOL opj_seek(OPJ_OFF_T offset, void * user_data)
{
    llassert(user_data);
    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);

    if (offset < 0 || offset > static_cast<OPJ_OFF_T>(jpeg_codec->size))
        return OPJ_FALSE;

    jpeg_codec->offset = offset;
    return OPJ_TRUE;
}

static void opj_free_user_data(void * user_data)
{
    llassert(user_data);

    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);
    // Don't free, data is managed externally
    jpeg_codec->buffer = nullptr;
    jpeg_codec->size = 0;
    jpeg_codec->offset = 0;
}

static void opj_free_user_data_write(void * user_data)
{
    llassert(user_data);

    JPEG2KBase* jpeg_codec = static_cast<JPEG2KBase*>(user_data);
    // Free, data was allocated here
    if (jpeg_codec->buffer)
    {
        ll_aligned_free_16(jpeg_codec->buffer);
        jpeg_codec->buffer = nullptr;
    }
    jpeg_codec->size = 0;
    jpeg_codec->offset = 0;
}


class JPEG2KDecode : public JPEG2KBase
{
public:

    JPEG2KDecode(S8 discardLevel)
    {
        memset(&parameters, 0, sizeof(opj_dparameters_t));
        opj_set_default_decoder_parameters(&parameters);
        parameters.cp_reduce = discardLevel;
    }

    ~JPEG2KDecode()
    {
        if (decoder)
        {
            opj_destroy_codec(decoder);
        }
        decoder = nullptr;

        if (image)
        {
            opj_image_destroy(image);
        }
        image = nullptr;

        if (stream)
        {
            opj_stream_destroy(stream);
        }
        stream = nullptr;

        if (codestream_info)
        {
            opj_destroy_cstr_info(&codestream_info);
        }
        codestream_info = nullptr;
    }

    bool readHeader(
        U8* data,
        U32 dataSize,
        S32& widthOut,
        S32& heightOut,
        S32& components,
        S32& discard_level)
    {
        parameters.flags |= OPJ_DPARAMETERS_DUMP_FLAG;

        decoder = opj_create_decompress(OPJ_CODEC_J2K);

        /* catch events using our callbacks and give a local context */
        opj_set_error_handler(decoder, error_callback, this);
        opj_set_warning_handler(decoder, warning_callback, this);
        opj_set_info_handler(decoder, info_callback, this);

        if (!opj_setup_decoder(decoder, &parameters))
        {
            return false;
        }

        if (stream)
        {
            opj_stream_destroy(stream);
        }

        stream = opj_stream_create(dataSize, OPJ_TRUE);
        if (!stream)
        {
            return false;
        }

        opj_stream_set_user_data(stream, this, opj_free_user_data);
        opj_stream_set_user_data_length(stream, dataSize);
        opj_stream_set_read_function(stream, opj_read);
        opj_stream_set_write_function(stream, opj_write);
        opj_stream_set_skip_function(stream, opj_skip);
        opj_stream_set_seek_function(stream, opj_seek);

        buffer = data;
        size = dataSize;
        offset = 0;

        // enable decoding partially loaded images
        opj_decoder_set_strict_mode(decoder, OPJ_FALSE);

        /* Read the main header of the codestream and if necessary the JP2 boxes*/
        if (!opj_read_header(stream, decoder, &image))
        {
            // On failure, OpenJPEG may have already destroyed *p_image.
            // Null it out so our destructor doesn't double-free.
            image = nullptr;
            return false;
        }

        codestream_info = opj_get_cstr_info(decoder);

        if (!codestream_info)
        {
            return false;
        }

        U32 tileDimX = codestream_info->tdx;
        U32 tileDimY = codestream_info->tdy;
        U32 tilesW = codestream_info->tw;
        U32 tilesH = codestream_info->th;

        widthOut = S32(tilesW * tileDimX);
        heightOut = S32(tilesH * tileDimY);
        components = codestream_info->nbcomps;

        // The maximum discard level is bounded by the codestream's resolution
        // count (you can't ask OpenJPEG to reduce below the lowest encoded
        // resolution). Prior versions of this code derived it from log2(tile
        // count), which returns 0 for single-tile codestreams — the common
        // case for SL textures — and effectively disabled progressive decode.
        discard_level = 0;
        if (codestream_info->m_default_tile_info.tccp_info)
        {
            U32 numres = codestream_info->m_default_tile_info.tccp_info[0].numresolutions;
            if (numres > 0)
            {
                discard_level = llclamp<S32>(S32(numres) - 1, 0, MAX_DISCARD_LEVEL);
            }
        }

        return true;
    }

    // KDU-parity single-shot decode. Each call creates a fresh codec,
    // parses the main header, sets the discard factor, runs opj_decode
    // against the currently-available bytes, and returns. Truncated
    // codestreams are tolerated by the patched OpenJPEG (set_strict_mode
    // OFF demotes "Stream too short" to warnings; the t2.c fix preserves
    // codeblock chunks accumulated from earlier complete packets when a
    // later packet truncates).
    bool decode(U8* data, U32 dataSize, U32* channels, U8 discard_level,
                S32 first_channel = 0, S32 max_channel_count = 4)
    {
        parameters.flags &= ~OPJ_DPARAMETERS_DUMP_FLAG;

        decoder = opj_create_decompress(OPJ_CODEC_J2K);
        if (!decoder)
        {
            return false;
        }
        opj_setup_decoder(decoder, &parameters);

        opj_set_info_handler(decoder, info_callback, this);
        opj_set_warning_handler(decoder, warning_callback, this);
        opj_set_error_handler(decoder, error_callback, this);

        opj_decoder_set_strict_mode(decoder, OPJ_FALSE);

        stream = opj_stream_create(dataSize, OPJ_TRUE);
        if (!stream)
        {
            return false;
        }
        opj_stream_set_user_data(stream, this, opj_free_user_data);
        opj_stream_set_user_data_length(stream, dataSize);
        opj_stream_set_read_function(stream, opj_read);
        opj_stream_set_write_function(stream, opj_write);
        opj_stream_set_skip_function(stream, opj_skip);
        opj_stream_set_seek_function(stream, opj_seek);

        buffer = data;
        size = dataSize;
        offset = 0;

        if (!opj_read_header(stream, decoder, &image))
        {
            // OJ may have already destroyed *p_image on failure; null
            // it so the destructor doesn't double-free.
            image = nullptr;
            return false;
        }

        // Cap the requested resolution factor to what the codestream
        // actually contains.
        U32 effective_discard = discard_level;
        if (opj_codestream_info_v2_t* info = opj_get_cstr_info(decoder))
        {
            if (info->m_default_tile_info.tccp_info &&
                    info->m_default_tile_info.tccp_info[0].numresolutions > 0)
            {
                U32 numres = info->m_default_tile_info.tccp_info[0].numresolutions;
                if (effective_discard >= numres)
                {
                    effective_discard = numres - 1;
                }
            }
            opj_destroy_cstr_info(&info);
        }
        opj_set_decoded_resolution_factor(decoder, effective_discard);

        // Channel restriction (KDU-parity: apply_input_restrictions). OJ's
        // opj_set_decoded_components requires apply_color_transforms ==
        // OPJ_FALSE -- it cannot do the YCC->RGB inverse during a
        // restricted decode. That's fine when the requested range lies
        // entirely past the first three components (which is where MCT is
        // applied), e.g. the aux-channel decode in llimageworker.cpp:209
        // (first_channel=4, max_channel_count=4). For ranges that overlap
        // the MCT components we DON'T restrict, decode everything, and
        // the consumer extracts the slice at first_channel..first+N-1.
        //
        // After a restricted decode, opj_decode COMPACTS image->comps so
        // that comps[0] holds the data for codestream component
        // indices[0], comps[1] for indices[1], etc. -- and image->numcomps
        // becomes the restricted count. mSrcChannelBase records where the
        // consumer should start reading from in image->comps[].
        if (image && image->numcomps > 0 && first_channel >= 3
            && max_channel_count > 0)
        {
            const OPJ_UINT32 total = image->numcomps;
            const OPJ_UINT32 first = (OPJ_UINT32)first_channel;
            if (first < total)
            {
                const OPJ_UINT32 want = (OPJ_UINT32)max_channel_count;
                const OPJ_UINT32 avail = total - first;
                const OPJ_UINT32 n = (want < avail) ? want : avail;
                std::vector<OPJ_UINT32> indices(n);
                for (OPJ_UINT32 i = 0; i < n; ++i)
                {
                    indices[i] = first + i;
                }
                if (opj_set_decoded_components(decoder, n, indices.data(), OPJ_FALSE))
                {
                    // Output is compacted: comps[0..n-1] now hold
                    // codestream comps first..first+n-1.
                    mSrcChannelBase = 0;
                    mRestrictedCount = (S32)n;
                }
            }
        }

        if (channels)
        {
            // Report the codestream's full component count regardless of
            // restriction. The consumer uses (image_channels - first_channel)
            // to know how many components are available; combined with
            // mSrcChannelBase it can locate them in image->comps[].
            *channels = image->numcomps;
        }

        if (!opj_decode(decoder, stream, image))
        {
            opj_end_decompress(decoder, stream);
            return false;
        }

        opj_end_decompress(decoder, stream);
        return image && image->numcomps;
    }

    opj_image_t* getImage() { return image; }
    std::string getLastError() const { return joinedMessages(); }

    // Where the consumer should start reading from in image->comps[].
    // Defaults to first_channel (uncompacted layout); becomes 0 when
    // opj_set_decoded_components compacted the output.
    S32 getSrcChannelBase(S32 first_channel) const
    {
        return (mSrcChannelBase >= 0) ? mSrcChannelBase : first_channel;
    }

private:
    opj_dparameters_t         parameters;
    opj_image_t*              image = nullptr;
    opj_codec_t*              decoder = nullptr;
    opj_stream_t*             stream = nullptr;
    opj_codestream_info_v2_t* codestream_info = nullptr;
    // -1 means "no channel restriction applied; consumer should use
    // first_channel as the base index into image->comps[]". When a
    // restriction is applied this is set to 0 and image->comps[] is
    // compacted to hold just the requested slice.
    S32                       mSrcChannelBase = -1;
    S32                       mRestrictedCount = 0;
};

class JPEG2KEncode : public JPEG2KBase
{
public:
    const OPJ_UINT32 TILE_SIZE = 64 * 64 * 3;

    JPEG2KEncode(const char* comment_text_in, bool reversible)
    {
        memset(&parameters, 0, sizeof(opj_cparameters_t));

        opj_set_default_encoder_parameters(&parameters);
        parameters.cod_format = OPJ_CODEC_J2K;
        // Match KDU's default progression order (LRCP). With byte-range
        // streaming (the SL fetch model), LRCP gives the decoder a rough
        // quality across all resolutions from the first packets, which is
        // what KDU-encoded server textures produce and what the texture
        // system expects. RLCP (resolution-major) would deliver full
        // quality of the lowest resolution first, then nothing for the
        // higher resolutions until enough bytes arrive -- a structurally
        // different partial-decode experience.
        parameters.prog_order = OPJ_LRCP;
        parameters.cp_disto_alloc = 1;    // enable rate allocation by distortion
        parameters.max_cs_size = 0;       // do not cap max size because we're using tcp_rates and also irrelevant with lossless.

        // 5-3 wavelet for reversible (lossless) mode, 9-7 for irreversible.
        // Layer rates and layer count are computed in encode() once the image
        // dimensions are known (KDU-parity byte-budget tiers).
        parameters.irreversible = reversible ? 0 : 1;

        comment_text = comment_text_in ? strdup(comment_text_in) : nullptr;

        parameters.cp_comment = comment_text ? comment_text : (char*)"no comment";
        llassert(parameters.cp_comment);
    }

    ~JPEG2KEncode()
    {
        if (encoder)
        {
            opj_destroy_codec(encoder);
        }
        encoder = nullptr;

        if (image)
        {
            opj_image_destroy(image);
        }
        image = nullptr;

        if (stream)
        {
            // opj_stream_destroy invokes opj_free_user_data_write, which frees
            // buffer and nulls it. The buffer-free guard below catches the
            // path where we allocated buffer but stream creation failed before
            // it was attached.
            opj_stream_destroy(stream);
        }
        stream = nullptr;

        if (buffer)
        {
            ll_aligned_free_16(buffer);
            buffer = nullptr;
        }

        if (comment_text)
        {
            free(comment_text);
        }
        comment_text = nullptr;
    }

    bool encode(const LLImageRaw& rawImageIn, LLImageJ2C &compressedImageOut)
    {
        LLImageDataSharedLock lockIn(&rawImageIn);
        LLImageDataLock lockOut(&compressedImageOut);

        if (!setImage(rawImageIn))
        {
            return false;
        }

        encoder = opj_create_compress(OPJ_CODEC_J2K);

        /* catch events using our callbacks and give a local context */
        opj_set_error_handler(encoder, error_callback, this);
        opj_set_warning_handler(encoder, warning_callback, this);
        opj_set_info_handler(encoder, info_callback, this);

        // Enable the irreversible color transform (YCbCr) for RGB/RGBA. OpenJPEG
        // applies it to the first three components only, leaving alpha alone.
        // Grayscale (1) and gray+alpha (2) have no color transform.
        parameters.tcp_mct = (image->numcomps >= 3) ? 1 : 0;

        // KDU-parity perceptual band weighting (approximation). KDU's
        // Cband_weights:Ck=... assigns a per-subband importance scalar that
        // biases rate-distortion allocation toward bands the eye is most
        // sensitive to. The Alchemy OJ patch exposes a coarser
        // per-component weighting via opj_set_component_weights() -- the
        // dominant perceptual effect is "luma matters more than chroma at
        // the same byte budget," which a per-component scalar captures.
        // Numbers below are the average of KDU's per-band weights for the
        // EPFL-15cm/300dpi profile, normalised so luma=1.0. Lossless
        // (irreversible=0) skips weighting -- it'd just slow down the
        // encoder without changing the bit-exact output.
        if (parameters.irreversible && image->numcomps >= 3)
        {
            const OPJ_UINT32 nw = (image->numcomps < 4U) ? image->numcomps : 4U;
            OPJ_FLOAT32 weights[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            weights[0] = 1.00f; // Y / luma
            weights[1] = 0.30f; // Cb / blue-chroma
            weights[2] = 0.30f; // Cr / red-chroma
            if (nw >= 4)
            {
                weights[3] = 1.00f; // alpha (not part of MCT/YCC)
            }
            opj_set_component_weights(encoder, nw, weights);
        }


        // KDU-parity byte-budget layer scheme. Each entry in layer_bytes is
        // the cumulative number of compressed bytes the decoder needs to
        // reconstruct that quality layer.
        //
        //   layer 0  = FIRST_PACKET_SIZE (600 bytes) -- enough for a coarse
        //              preview right after the main header is parsed.
        //   layer i+ = MIN_LAYER_SIZE (2000), then *4 each step, until the
        //              total target byte budget is reached.
        //
        // For reversible mode KDU appends a final 0 (lossless) layer for
        // images large enough to make the final lossless layer worth its
        // bookkeeping; smaller images stop at the truncated layer.
        //
        // OJ's tcp_rates[] uses compression RATIO per layer where the target
        // bytes for layer i = raw_size / tcp_rates[i]. Converting:
        //   tcp_rates[i] = raw_bytes / layer_bytes[i]
        //   tcp_rates[lossless_layer] = 0.0f  (OJ convention: 0 = no limit)
        //
        // This replaces the prior ratio-curve approach (estimate_num_layers
        // + set_tcp_rates) which made the first quality layer be ~100KB
        // rather than 600 bytes -- so partial-decode previews of OJ-encoded
        // textures needed orders of magnitude more bytes downloaded before
        // showing anything. After this change OJ-encoded and KDU-encoded
        // textures stream up in roughly the same byte-budget tiers.
        {
            const U32 raw_bytes = (U32)rawImageIn.getWidth() *
                                  (U32)rawImageIn.getHeight() *
                                  (U32)rawImageIn.getComponents();
            const F32 target_rate = DEFAULT_COMPRESSION_RATE; // 1/8 by default
            const U32 max_bytes = (U32)((F32)raw_bytes * target_rate);

            S32 nb_layers = 0;
            U32 layer_bytes[MAX_NB_LAYERS] = { 0 };

            layer_bytes[nb_layers++] = (U32)FIRST_PACKET_SIZE; // 600
            U32 b = (U32)MIN_LAYER_SIZE;                       // 2000
            while (b < max_bytes && nb_layers < (MAX_NB_LAYERS - 1))
            {
                layer_bytes[nb_layers++] = b;
                b *= 4;
            }
            if ((U32)layer_bytes[nb_layers - 1] < max_bytes &&
                nb_layers < MAX_NB_LAYERS)
            {
                layer_bytes[nb_layers++] = max_bytes;
            }
            if (!parameters.irreversible &&
                (rawImageIn.getWidth() >= 32 || rawImageIn.getHeight() >= 32) &&
                nb_layers < MAX_NB_LAYERS)
            {
                // KDU adds a final 0 (lossless) layer for reversible mode on
                // anything bigger than 32px on the long side.
                layer_bytes[nb_layers++] = 0;
            }

            parameters.tcp_numlayers = nb_layers;
            for (S32 i = 0; i < nb_layers; ++i)
            {
                if (layer_bytes[i] == 0 || layer_bytes[i] >= raw_bytes)
                {
                    // OJ treats rate 0 (or 1) as "no truncation" / lossless.
                    parameters.tcp_rates[i] = 0.0f;
                }
                else
                {
                    parameters.tcp_rates[i] = (F32)raw_bytes /
                                              (F32)layer_bytes[i];
                }
            }
        }

        U32 width_tiles = (rawImageIn.getWidth() >> 6);
        U32 height_tiles = (rawImageIn.getHeight() >> 6);

        // Allow images with a width or height that are MIN_IMAGE_SIZE <= x < 64
        if (width_tiles == 0 && (rawImageIn.getWidth() >= MIN_IMAGE_SIZE))
        {
            width_tiles = 1;
        }
        if (height_tiles == 0 && (rawImageIn.getHeight() >= MIN_IMAGE_SIZE))
        {
            height_tiles = 1;
        }

        if (width_tiles == 1 || height_tiles == 1)
        {
            // Images with either dimension less than 32 need fewer resolutions
            // or OpenJPEG will refuse to encode them. Cap the default rather
            // than overwrite it so larger images don't get *more* resolutions
            // than intended.
            int min_dim = rawImageIn.getWidth() < rawImageIn.getHeight() ? rawImageIn.getWidth() : rawImageIn.getHeight();
            int max_res = 1 + (int)floor(log2(min_dim));
            if (max_res < (int)parameters.numresolution)
            {
                parameters.numresolution = max_res;
            }
        }

        if (!opj_setup_encoder(encoder, &parameters, image))
        {
            return false;
        }

        U32 tile_count = width_tiles * height_tiles;
        if (tile_count == 0)
        {
            // Image is smaller than MIN_IMAGE_SIZE on at least one axis; refuse
            // to encode rather than walk a zero-sized buffer.
            return false;
        }
        U32 data_size_guess = tile_count * TILE_SIZE;

        // will be freed in opj_free_user_data_write (or by the dtor if stream setup fails)
        buffer = (U8*)ll_aligned_malloc_16(data_size_guess);
        if (!buffer)
        {
            return false;
        }
        size = data_size_guess;
        offset = 0;

        memset(buffer, 0, data_size_guess);

        if (stream)
        {
            opj_stream_destroy(stream);
        }

        stream = opj_stream_create(data_size_guess, OPJ_FALSE);
        if (!stream)
        {
            return false;
        }

        opj_stream_set_user_data(stream, this, opj_free_user_data_write);
        opj_stream_set_user_data_length(stream, data_size_guess);
        opj_stream_set_read_function(stream, opj_read);
        opj_stream_set_write_function(stream, opj_write);
        opj_stream_set_skip_function(stream, opj_skip);
        opj_stream_set_seek_function(stream, opj_seek);

        OPJ_BOOL started = opj_start_compress(encoder, image, stream);

        if (!started)
        {
            return false;
        }

        if (!opj_encode(encoder, stream))
        {
            return false;
        }

        OPJ_BOOL encoded = opj_end_compress(encoder, stream);

        // if we successfully encoded, then stream out the compressed data...
        if (encoded)
        {
            // "append" (set) the data we "streamed" (memcopied) for writing to the formatted image
            // with side-effect of setting the actually encoded size  to same
            compressedImageOut.allocateData((S32)offset);
            memcpy(compressedImageOut.getData(), buffer, offset);
            compressedImageOut.updateData(); // update width, height etc from header
        }
        return encoded;
    }

    bool setImage(const LLImageRaw& raw)
    {
        S32 numcomps = raw.getComponents();
        S32 width    = raw.getWidth();
        S32 height   = raw.getHeight();

        if (numcomps <= 0 || width <= 0 || height <= 0)
        {
            return false;
        }

        std::vector<opj_image_cmptparm_t> cmptparm(numcomps);

        for (S32 c = 0; c < numcomps; c++)
        {
            cmptparm[c].prec = 8; // replaces .bpp
            cmptparm[c].sgnd = 0;
            cmptparm[c].dx = parameters.subsampling_dx;
            cmptparm[c].dy = parameters.subsampling_dy;
            cmptparm[c].w = width;
            cmptparm[c].h = height;
        }

        // sRGB only applies to 3/4-component images; grayscale (and gray+alpha)
        // must use GRAY so downstream decoders interpret the channels correctly.
        OPJ_COLOR_SPACE colorspace = (numcomps >= 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;

        image = opj_image_create(numcomps, cmptparm.data(), colorspace);
        if (!image)
        {
            return false;
        }

        image->x1 = width;
        image->y1 = height;

        const U8 *src_datap = raw.getData();

        S32 i = 0;
        for (S32 y = height - 1; y >= 0; y--)
        {
            const U8* row = src_datap + (size_t)y * width * numcomps;
            for (S32 x = 0; x < width; x++)
            {
                const U8 *pixel = row + (size_t)x * numcomps;
                for (S32 c = 0; c < numcomps; c++)
                {
                    image->comps[c].data[i] = *pixel;
                    pixel++;
                }
                i++;
            }
        }

        return true;
    }

    opj_image_t* getImage() { return image; }
    std::string getLastError() const { return joinedMessages(); }

private:
    opj_cparameters_t   parameters;
    opj_image_t*        image = nullptr;
    opj_codec_t*        encoder = nullptr;
    opj_stream_t*       stream = nullptr;
    char*               comment_text = nullptr;
};


LLImageJ2COJ::LLImageJ2COJ()
    : LLImageJ2CImpl()
{
}


LLImageJ2COJ::~LLImageJ2COJ()
{
}

bool LLImageJ2COJ::initDecode(LLImageJ2C &base, LLImageRaw &raw_image, int discard_level, int* region)
{
    base.mDiscardLevel = discard_level;
    return false;
}

bool LLImageJ2COJ::initEncode(LLImageJ2C &base, LLImageRaw &raw_image, int blocks_size, int precincts_size, int levels)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // No specific implementation for this method in the OpenJpeg case
    return false;
}

bool LLImageJ2COJ::decodeImpl(LLImageJ2C &base, LLImageRaw &raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count)
{
    LLImageDataLock lockIn(&base);
    LLImageDataLock lockOut(&raw_image);

    /* Extract metadata */
    /* ---------------- */
     U8* c_data = base.getData();
     size_t c_size =  base.getDataSize();
     size_t position = 0;

     // NOTE: this is a byte-by-byte heuristic scan rather than a proper JP2K
     // segment walk, so it can in principle match `FF 64` inside another
     // segment's payload. The Lcom sanity cap plus the "next byte is FF" check
     // make false positives unlikely in practice; revisit if this ever
     // misfires.
     while (c_data && c_size >= 7 && position < 1024 && position + 7 < c_size) // the comment field should be in the first 1024 bytes.
    {
         if (c_data[position] == 0xff && c_data[position + 1] == 0x64)
         {
             U8 high_byte = c_data[position + 2];
             U8 low_byte = c_data[position + 3];
             S32 c_length = (high_byte * 256) + low_byte; // This size also counts the markers, 00 01 and itself
             if (c_length < 4 || c_length > 200) // sanity check
             {
                 // Lcom must cover at least itself (2) + Rcom (2). Comments can
                 // technically be very long, but anything beyond 200 is suspect.
                 break;
             }

            if (position + 2 + (size_t)c_length > c_size)
            {
                // comment extends past end of data, corruption, or all data not retrived yet.
                break;
            }

            // if the comment block does not end at the end of data, check to see if the next
            // block starts with 0xFF
            if (position + 2 + (size_t)c_length < c_size && c_data[position + 2 + c_length] != 0xff)
            {
                // invalied comment block
                break;
            }

            // extract the comment minus the markers, 00 01
            raw_image.mComment.assign((char*)(c_data + position + 6), c_length - 4);
            break;
        }
        ++position;
    }

    // Single-shot decode. Matches KDU's per-call decode pattern: build a
    // fresh codec, parse header, decode whatever bytes are present, copy
    // out, destroy. The patched OpenJPEG handles truncation tolerantly
    // (set_strict_mode OFF + t2.c chunk-preservation fix); each call's
    // output reflects only what the current bytestream supports.
    JPEG2KDecode decoder(0);

    U32 image_channels = 0;
    S32 data_size = base.getDataSize();
    S32 max_bytes = base.getMaxBytes() ? base.getMaxBytes() : data_size;
    if (max_bytes > data_size)
    {
        max_bytes = data_size;
    }
    bool decoded = decoder.decode(base.getData(), max_bytes, &image_channels,
                                  base.mDiscardLevel,
                                  first_channel, max_channel_count);

    S32 channels = (S32)image_channels - first_channel;
    channels = llmin(channels, max_channel_count);
    channels = llmax(channels, 0);

    if (!decoded)
    {
        std::string opj_err = decoder.getLastError();
        base.setLastError(opj_err.empty() ? std::string("OpenJPEG decode failed") : opj_err);
        base.decodeFailed();

        if (channels > 0 && raw_image.getComponents() != channels)
        {
            raw_image.resize(raw_image.getWidth(), raw_image.getHeight(), S8(channels));
        }

        LL_DEBUGS("Texture") << "ERROR -> decodeImpl: failed to decode image! "
                             << (opj_err.empty() ? "(no OpenJPEG message)" : opj_err) << LL_ENDL;
        return true; // done
    }

    opj_image_t *image = decoder.getImage();
    if (!image || !image->numcomps)
    {
        base.setLastError("OpenJPEG returned no image components");
        base.decodeFailed();
        return true; // done
    }
    if (channels <= 0)
    {
        base.setLastError("Requested first_channel beyond decoded components");
        base.decodeFailed();
        return true; // done
    }

    // Where to start reading from in image->comps[]. When OJ's channel
    // restriction was applied, the output is compacted (comps[0..N-1]
    // hold the requested slice) and src_base == 0. Otherwise the
    // codestream's full component set is present and src_base ==
    // first_channel.
    const S32 src_base = decoder.getSrcChannelBase(first_channel);

    // Dimensions come from the first component of the slice -- it's
    // always populated. comps[0] could be data-NULL when the restriction
    // path didn't run but the consumer asked for first_channel > 0.
    if (src_base < 0 || (U32)src_base >= image->numcomps)
    {
        base.setLastError("Source component base out of range");
        base.decodeFailed();
        return true; // done
    }
    U32 comp_width = image->comps[src_base].w;
    U32 f = image->comps[src_base].factor;
    U32 width = image->comps[src_base].w;
    U32 height = image->comps[src_base].h;

    raw_image.resize(U16(width), U16(height), S8(channels));
    U8 *rawp = raw_image.getData();

    // Comp->raw copy with Y-flip (OJ comps are top-down; raw_image is
    // bottom-up for OpenGL).
    for (S32 dest = 0; dest < channels; dest++)
    {
        const S32 src_comp = src_base + dest;
        if ((U32)src_comp >= image->numcomps || !image->comps[src_comp].data)
        {
            LL_DEBUGS("Texture") << "ERROR -> decodeImpl: src_comp " << src_comp
                                 << " missing data (numcomps=" << image->numcomps
                                 << ")" << LL_ENDL;
            continue;
        }

        S32 offset = dest;
        for (S32 y = (S32)(height - 1); y >= 0; y--)
        {
            const S32 src_row = y * (S32)comp_width;
            for (U32 x = 0; x < width; x++)
            {
                rawp[offset] = (U8) image->comps[src_comp].data[src_row + (S32)x];
                offset += channels;
            }
        }
    }

    base.setDiscardLevel(f);

    return true; // done
}


bool LLImageJ2COJ::encodeImpl(LLImageJ2C &base, const LLImageRaw &raw_image, const char* comment_text, F32 encode_time, bool reversible)
{
    if (raw_image.isBufferInvalid())
    {
        base.setLastError("Invalid input, no buffer");
        return false;
    }

    JPEG2KEncode encode(comment_text, reversible);
    bool encoded = encode.encode(raw_image, base);
    if (!encoded)
    {
        std::string opj_err = encode.getLastError();
        base.setLastError(opj_err.empty() ? std::string("OpenJPEG encode failed") : opj_err);
        LL_WARNS() << "OpenJPEG encoding failed: "
                   << (opj_err.empty() ? "(no message)" : opj_err) << LL_ENDL;
    }
    return encoded;
}

bool LLImageJ2COJ::getMetadata(LLImageJ2C &base)
{
    LLImageDataLock lock(&base);

    JPEG2KDecode decode(0);

    S32 width = 0;
    S32 height = 0;
    S32 components = 0;
    S32 discard_level = 0;

    U32 dataSize = base.getDataSize();
    U8* data = base.getData();
    bool header_read = decode.readHeader(data, dataSize, width, height, components, discard_level);
    if (!header_read)
    {
        std::string opj_err = decode.getLastError();
        base.setLastError(opj_err.empty() ? std::string("OpenJPEG header parse failed") : opj_err);
        return false;
    }

    base.mDiscardLevel = discard_level;
    base.setSize(width, height, components);

    // Override the synthetic byte-budget estimate with actual codestream
    // tile-part boundaries when we can parse them. Our patched OpenJPEG
    // tolerates truncated tile-parts (see indra/vcpkg/ports/openjpeg/), so
    // seeding from a *partial* walk is now safe: the fetcher triggers
    // decode earlier, OJ produces whatever pixels it can from the available
    // bytes, and the texture refines as more data arrives.
    //
    // Mapping: discard 0 (highest quality) gets the end of the last walked
    // tile-part; discard MAX_DISCARD_LEVEL gets the end of the first. Single
    // tile-part files (typical OJ-encoded uploads) collapse to one value
    // across every discard.
    // Up-front cap on tile-part-ends. SL textures rarely exceed ~6 tile-parts
    // (one per resolution); a 64-entry buffer is comfortably oversized and
    // matches MAX_NB_LAYERS. The OJ walker fills out_num_ends with the
    // actual count.
    constexpr OPJ_UINT32 MAX_TILE_PART_ENDS = 64;
    OPJ_SIZE_T tile_part_ends[MAX_TILE_PART_ENDS];
    OPJ_UINT32 num_tile_part_ends = 0;
    OPJ_BOOL complete = OPJ_FALSE;
    bool walked = opj_j2k_walk_tile_part_ends(data, (OPJ_SIZE_T)dataSize,
                                              tile_part_ends,
                                              &num_tile_part_ends,
                                              MAX_TILE_PART_ENDS,
                                              &complete) == OPJ_TRUE;
    if (walked && num_tile_part_ends > 0)
    {
        S32 n = (S32)num_tile_part_ends;
        for (S32 d = 0; d <= MAX_DISCARD_LEVEL; d++)
        {
            S32 idx = llclamp(n - 1 - d, 0, n - 1);
            base.mDataSizes[d] = (S32)tile_part_ends[idx];
        }
        // Mark the cache valid so calcDataSize() returns our values rather
        // than recomputing via calcDataSizeJ2C.
        base.mAreaUsedForDataSizeCalcs = base.getHeight() * base.getWidth();

        LL_DEBUGS("Texture") << "LLImageJ2COJ: SOT walker seeded mDataSizes from "
                             << n << " tile-part(s) ("
                             << (complete ? "complete" : "partial")
                             << "); buf=" << dataSize
                             << " last_tp_end=" << tile_part_ends[n - 1]
                             << " d0=" << base.mDataSizes[0]
                             << " d5=" << base.mDataSizes[MAX_DISCARD_LEVEL]
                             << LL_ENDL;
    }
    else
    {
        LL_DEBUGS("Texture") << "LLImageJ2COJ: SOT walker failed (buf=" << dataSize
                             << "), falling back to calcDataSizeJ2C estimate"
                             << LL_ENDL;
    }

    return true;
}

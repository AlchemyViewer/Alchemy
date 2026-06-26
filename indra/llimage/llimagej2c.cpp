/**
 * @file llimagej2c.cpp
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "lldir.h"
#include "llimagej2c.h"
#include "lltimer.h"
#include "llmath.h"
#include "llmemory.h"
#include "llsd.h"

// Declare the prototype for this factory function here. It is implemented in
// other files which define a LLImageJ2CImpl subclass, but only ONE static
// library which has the implementation for this function should ever be
// linked.
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl();

//static
std::string LLImageJ2C::getEngineInfo()
{
    // All known LLImageJ2CImpl implementation subclasses are cheap to
    // construct.
    std::unique_ptr<LLImageJ2CImpl> impl(fallbackCreateLLImageJ2CImpl());
    return impl->getEngineInfo();
}

LLImageJ2C::LLImageJ2C() :  LLImageFormatted(IMG_CODEC_J2C),
                            mMaxBytes(0),
                            mRawDiscardLevel(-1),
                            mRate(DEFAULT_COMPRESSION_RATE),
                            mReversible(false),
                            mAreaUsedForDataSizeCalcs(0)
{
    mImpl.reset(fallbackCreateLLImageJ2CImpl());

    // Clear data size table
    for( S32 i = 0; i <= MAX_DISCARD_LEVEL; i++)
    {   // Array size is MAX_DISCARD_LEVEL+1
        mDataSizes[i] = 0;
    }
}

// virtual
LLImageJ2C::~LLImageJ2C() {}

// virtual
void LLImageJ2C::resetLastError()
{
    mLastError.clear();
}

//virtual
void LLImageJ2C::setLastError(const std::string& message, const std::string& filename)
{
    mLastError = message;
    if (!filename.empty())
        mLastError += std::string(" FILE: ") + filename;
}

// virtual
S8  LLImageJ2C::getRawDiscardLevel()
{
    return mRawDiscardLevel;
}

bool LLImageJ2C::updateData()
{
    bool res = true;
    resetLastError();

    LLImageDataLock lock(this);

    // Check to make sure that this instance has been initialized with data
    if (!getData() || (getDataSize() < 16))
    {
        setLastError("LLImageJ2C uninitialized");
        res = false;
    }
    else
    {
        res = mImpl->getMetadata(*this);
    }

    if (res)
    {
        // SJB: override discard based on mMaxBytes elsewhere
        S32 max_bytes = getDataSize(); // mMaxBytes ? mMaxBytes : getDataSize();
        S32 discard = calcDiscardLevelBytes(max_bytes);
        setDiscardLevel(discard);
    }

    if (!mLastError.empty())
    {
        LLImage::setLastError(mLastError);
    }
    return res;
}

bool LLImageJ2C::initDecode(LLImageRaw &raw_image, int discard_level, int* region)
{
    setDiscardLevel(discard_level != -1 ? discard_level : 0);
    return mImpl->initDecode(*this,raw_image,discard_level,region);
}

bool LLImageJ2C::initEncode(LLImageRaw &raw_image, int blocks_size, int precincts_size, int levels)
{
    return mImpl->initEncode(*this,raw_image,blocks_size,precincts_size,levels);
}

bool LLImageJ2C::decode(LLImageRaw *raw_imagep, F32 decode_time)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    return decodeChannels(raw_imagep, decode_time, 0, 4);
}


// Returns true to mean done, whether successful or not.
bool LLImageJ2C::decodeChannels(LLImageRaw *raw_imagep, F32 decode_time, S32 first_channel, S32 max_channel_count )
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLTimer elapsed;

    resetLastError();

    bool res;
    {
        LLImageDataLock lock(this);

        mDecoding = true;
        // Check to make sure that this instance has been initialized with data
        if (!getData() || (getDataSize() < 16))
        {
            setLastError("LLImageJ2C uninitialized");
            res = true; // done
        }
        else
        {
            // Update the raw discard level
            updateRawDiscardLevel();
            res = mImpl->decodeImpl(*this, *raw_imagep, decode_time, first_channel, max_channel_count);
        }
    }

    if (res)
    {
        if (!mDecoding)
        {
            // Failed
            raw_imagep->deleteData();
            res = false;
        }
        else
        {
            mDecoding = false;
        }
    }
    else
    {
        if (mDecoding)
        {
            LL_WARNS() << "decodeImpl failed but mDecoding is true" << LL_ENDL;
            mDecoding = false;
        }
    }

    if (!mLastError.empty())
    {
        LLImage::setLastError(mLastError);
    }

    return res;
}


bool LLImageJ2C::encode(const LLImageRaw *raw_imagep, F32 encode_time)
{
    return encode(raw_imagep, NULL, encode_time);
}


bool LLImageJ2C::encode(const LLImageRaw *raw_imagep, const char* comment_text, F32 encode_time)
{
    LLTimer elapsed;
    resetLastError();
    bool res = mImpl->encodeImpl(*this, *raw_imagep, comment_text, encode_time, mReversible);
    if (!mLastError.empty())
    {
        LLImage::setLastError(mLastError);
    }

    return res;
}

//static
S32 LLImageJ2C::calcHeaderSizeJ2C()
{
    return FIRST_PACKET_SIZE; // Hack. just needs to be >= actual header size...
}

//static
S32 LLImageJ2C::calcDataSizeJ2C(S32 w, S32 h, S32 comp, S32 discard_level, F32 rate)
{
    // Estimates the byte budget needed to decode at `discard_level`. Smaller
    // discards (higher quality) take more bytes.
    //
    // This is the historical sqrt-based "Byte Range Study" formula from the
    // THX1138 KDU work (see
    // https://wiki.lindenlab.com/wiki/THX1138_KDU_Improvements#Byte_Range_Study)
    // restored with modern hygiene. PRs #2032/#2406/#2525/#4018 progressively
    // inflated this estimate as a safety net against OpenJPEG aborting on
    // mid-tile-part truncation; with the patched openjpeg in
    // indra/vcpkg/ports/openjpeg/, OJ tolerates truncation natively and the
    // inflated curve only delays progressive previews unnecessarily.
    //
    // Two estimators are computed and the smaller picked when
    // `LLImage::useNewByteRange()` is on (default true):
    //   * `new_bytes` = sqrt(w*h) * comp * rate * 1000 / layer_factor
    //     — KDU-derived approximation tracking the rate-distortion curve.
    //   * `old_bytes` = w * h * comp * rate
    //     — uncompressed-times-rate; a conservative ceiling.
    // The final result is clamped to at least the codestream header size.

    // Estimate the number of quality layers used during encoding so the
    // byte estimate tracks the codestreams our encoder actually produces.
    // Matches JPEG2KEncode::estimate_num_layers in
    // indra/llimagej2coj/llimagej2coj.cpp; keep the two tables aligned or
    // the layer_factor diverges from real layer counts and the budget
    // shifts out from under partial decodes.
    S32 nb_layers;
    S32 surface = w * h;
    if      (surface <=    1024) nb_layers = 2;  // <= 32x32
    else if (surface <=   16384) nb_layers = 3;  // <= 128x128
    else if (surface <=  262144) nb_layers = 4;  // <= 512x512
    else if (surface <= 1048576) nb_layers = 5;  // <= 1024x1024 (~1 MP)
    else                         nb_layers = 6;  // larger
    F32 layer_factor = 3.0f * (7 - llclamp(nb_layers, 1, 6));

    // Reduce dimensions by 2^discard, with a floor of 1 so the sqrt doesn't
    // collapse to zero.
    w >>= discard_level;
    h >>= discard_level;
    w = llmax(w, 1);
    h = llmax(h, 1);

    // Honour the actual component count: RGB textures shouldn't budget as
    // if they had an alpha channel. Fall back to 4 if the caller hasn't
    // populated `comp` yet (pre-getMetadata path).
    S32 components = (comp > 0) ? comp : 4;

    S32 new_bytes = (S32)(sqrt((F32)(w * h)) * (F32)components * rate * 1000.f / layer_factor);
    S32 old_bytes = (S32)((F32)(w * h * components) * rate);
    S32 bytes = (LLImage::useNewByteRange() && (new_bytes < old_bytes)) ? new_bytes : old_bytes;
    // Floor at MIN_LAYER_SIZE rather than calcHeaderSizeJ2C() (== FIRST_PACKET_SIZE,
    // 600). At a 50% reverse-byte-range threshold, the old floor triggered
    // decode at ~300 bytes — past the main header but before any packet
    // body data was present. With OpenJPEG patched to tolerate truncated
    // tile-parts, the empty decode no longer errors out but still produces
    // mid-grey via the DC level shift on zero coefficients (until the
    // openjpeg tcd.c overlay patch lands too). MIN_LAYER_SIZE (2000) gives
    // a 1000-byte threshold, which is enough for the main header plus at
    // least one packet body of low-resolution coefficient data.
    bytes = llmax(bytes, MIN_LAYER_SIZE);
    return bytes;
}

S32 LLImageJ2C::calcHeaderSize()
{
    return calcHeaderSizeJ2C();
}

// calcDataSize() returns how many bytes to read to load discard_level (including header)
S32 LLImageJ2C::calcDataSize(S32 discard_level)
{
    discard_level = llclamp(discard_level, 0, MAX_DISCARD_LEVEL);
    if ( mAreaUsedForDataSizeCalcs != (getHeight() * getWidth())
        || (mDataSizes[0] == 0))
    {
        mAreaUsedForDataSizeCalcs = getHeight() * getWidth();

        S32 level = MAX_DISCARD_LEVEL;  // Start at the highest discard
        while ( level >= 0 )
        {
            mDataSizes[level] = calcDataSizeJ2C(getWidth(), getHeight(), getComponents(), level, mRate);
            level--;
        }
    }
    return mDataSizes[discard_level];
}

S32 LLImageJ2C::calcDiscardLevelBytes(S32 bytes)
{
    llassert(bytes >= 0);
    S32 discard_level = 0;
    if (bytes == 0)
    {
        return MAX_DISCARD_LEVEL;
    }
    while (1)
    {
        S32 bytes_needed = calcDataSize(discard_level);
        // Use TextureReverseByteRange percent (see settings.xml) of the optimal size to qualify as correct rendering for the given discard level
        if (bytes >= (bytes_needed*LLImage::getReverseByteRangePercent()/100))
        {
            break;
        }
        discard_level++;
        if (discard_level >= MAX_DISCARD_LEVEL)
        {
            break;
        }
    }
    return discard_level;
}

void LLImageJ2C::setMaxBytes(S32 max_bytes)
{
    mMaxBytes = max_bytes;
}

void LLImageJ2C::setReversible(const bool reversible)
{
    mReversible = reversible;
}


bool LLImageJ2C::loadAndValidate(const std::string &filename)
{
    bool res = true;

    resetLastError();

    std::error_code ec;
    LLFile infile;
    infile.open(filename, LLFile::in | LLFile::binary, ec);
    if (!infile || ec)
    {
        setLastError("Unable to open file for reading", filename);
        res = false;
    }
    else
    {
        S64 file_size = infile.size(ec);
        if (file_size == 0 || ec)
        {
            setLastError("File is empty", filename);
            res = false;
        }
        else
        {
            U8* data = (U8*)ll_aligned_malloc_16(file_size);
            if (!data)
            {
                infile.close();
                setLastError("Out of memory", filename);
                res = false;
            }
            else
            {
                S64 bytes_read = infile.read((char*)data, file_size, ec);
                infile.close();

                if (ec || bytes_read != file_size)
                {
                    ll_aligned_free_16(data);
                    setLastError("Unable to read entire file");
                    res = false;
                }
                else
                {
                    res = validate(data, narrow(file_size));
                }
            }
        }
    }

    if (!mLastError.empty())
    {
        LLImage::setLastError(mLastError);
    }

    return res;
}


bool LLImageJ2C::validate(U8 *data, U32 file_size)
{
    resetLastError();

    LLImageDataLock lock(this);

    setData(data, file_size);

    bool res = updateData();
    if ( res )
    {
        // Check to make sure that this instance has been initialized with data
        if (!getData() || (0 == getDataSize()))
        {
            setLastError("LLImageJ2C uninitialized");
            res = false;
        }
        else
        {
            res = mImpl->getMetadata(*this);
        }
    }

    if (!mLastError.empty())
    {
        LLImage::setLastError(mLastError);
    }
    return res;
}

void LLImageJ2C::decodeFailed()
{
    mDecoding = false;
}

void LLImageJ2C::updateRawDiscardLevel()
{
    mRawDiscardLevel = mMaxBytes ? calcDiscardLevelBytes(mMaxBytes) : mDiscardLevel;
}

LLImageJ2CImpl::~LLImageJ2CImpl()
{
}

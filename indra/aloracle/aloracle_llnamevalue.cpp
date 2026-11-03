/**
 * @file aloracle_llnamevalue.cpp
 * @brief aloracle's function over llmessage's name-value pairs.
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

#include "aloracleboundary.h"

#include "llnamevalue.h"
#include "llsd.h"
#include "u64.h"

#include <string>

namespace
{
    // NV_BUFFER_LEN: the size of each static buffer the constructor reads
    // into, the value's among them, which it fills with no bound.
    constexpr size_t BUFFER_SIZE = 2048;

    LLSD textOf(const char* text)
    {
        return text ? LLSD(std::string(text)) : LLSD();
    }
}

int32_t aloracle_name_value_parse(const uint8_t* line, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!aloracle::clear(value) || size >= BUFFER_SIZE || !aloracle::take(line, size, text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        const LLNameValue pair(text.c_str());

        LLSD out;
        out["name"] = textOf(pair.mName);
        out["type"] = textOf(pair.mStringType);
        out["class"] = textOf(pair.mStringClass);
        out["sendto"] = textOf(pair.mStringSendto);
        out["type_id"] = (S32)pair.mType;
        out["class_id"] = (S32)pair.mClass;
        out["sendto_id"] = (S32)pair.mSendto;
        switch (pair.mType)
        {
        case NVT_STRING:
        case NVT_ASSET:
            out["value"] = textOf(pair.mNameValueReference.string);
            break;
        case NVT_F32:
            out["value"] = *pair.mNameValueReference.f32;
            break;
        case NVT_S32:
            out["value"] = *pair.mNameValueReference.s32;
            break;
        case NVT_U32:
            out["value"] = (S32)*pair.mNameValueReference.u32;
            break;
        case NVT_VEC3:
            out["value"] = pair.mNameValueReference.vec3->getValue();
            break;
        case NVT_U64:
            out["value"] = U64_to_str(*pair.mNameValueReference.u64);
            break;
        default:
            break;
        }
        if (pair.mType != NVT_NULL)
        {
            out["printed"] = pair.printNameValue();
        }
        return aloracle::giveValue(out, value);
    });
}

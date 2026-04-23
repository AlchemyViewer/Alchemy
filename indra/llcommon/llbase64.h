/**
 * @file llbase64.h
 * @brief Wrapper for apr base64 encoding that returns a std::string
 * @author James Cook
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#ifndef LLBASE64_H
#define LLBASE64_H

#include "llpreprocessor.h"

#include <simdutf.h>

#include <string>

class LL_COMMON_API LLBase64
{
public:
    inline static std::string encode(const U8* input, size_t input_size)
    {
        std::string output;
        if (input && input_size > 0)
        {
            output.resize(simdutf::base64_length_from_binary(input_size));
            simdutf::binary_to_base64((const char*)input, input_size, output.data());
        }
        return output;
    }

    inline static std::string decodeAsString(const std::string& input)
    {
        // allocate enough memory for the maximal binary length
        std::string buffer;
        buffer.resize(simdutf::binary_length_from_base64(input.data(), input.size()));
        // convert to binary and check for errors
        simdutf::result r = simdutf::base64_to_binary(input.data(), input.size(), (char*)buffer.data());
        if (r.error != simdutf::error_code::SUCCESS)
        {
            return {};
        }
        else
        {
            return buffer;
        }
    }
};

#endif

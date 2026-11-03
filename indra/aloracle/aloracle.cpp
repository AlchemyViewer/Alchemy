/**
 * @file aloracle.cpp
 * @brief The C boundary every aloracle function goes through.
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

#include "llerrorcontrol.h"
#include "llpointer.h"
#include "llsd.h"
#include "llsdserialize.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace
{
    void throwFatalError(const std::string&)
    {
        throw aloracle::FatalError();
    }
}

namespace aloracle
{
    void prepare()
    {
        // The library's own LLError, which no one else configures: with no
        // recorder it writes nothing, and with this an LL_ERRS returns to
        // the call rather than ending the caller's process.
        static const bool prepared = []
        {
            LLError::setFatalFunction(throwFatalError);
            return true;
        }();
        (void)prepared;
    }

    LLMutex& callLock()
    {
        static LLMutex lock;
        return lock;
    }

    bool clear(aloracle_bytes* out)
    {
        if (!out)
        {
            return false;
        }
        *out = aloracle_bytes{ nullptr, 0 };
        return true;
    }

    bool take(const uint8_t* data, size_t size, std::string& text)
    {
        if (!size)
        {
            text.clear();
            return true;
        }
        if (!data)
        {
            return false;
        }
        text.assign(reinterpret_cast<const char*>(data), size);
        return true;
    }

    bool takeValue(const uint8_t* data, size_t size, LLSD& value)
    {
        std::string bytes;
        if (!take(data, size, bytes))
        {
            return false;
        }
        std::istringstream stream(bytes);
        LLPointer<LLSDBinaryParser> parser = new LLSDBinaryParser;
        return parser->parse(stream, value, (llssize)bytes.size()) > 0
            && stream.peek() == std::istringstream::traits_type::eof();
    }

    int32_t give(std::string_view text, aloracle_bytes* out)
    {
        if (text.empty())
        {
            return ALORACLE_OK;
        }
        void* data = std::malloc(text.size());
        if (!data)
        {
            return ALORACLE_EXCEPTION;
        }
        std::memcpy(data, text.data(), text.size());
        out->data = static_cast<uint8_t*>(data);
        out->size = text.size();
        return ALORACLE_OK;
    }

    int32_t giveValue(const LLSD& value, aloracle_bytes* out)
    {
        std::ostringstream stream;
        LLSDSerialize::toBinary(value, stream);
        return give(stream.view(), out);
    }
}

void aloracle_bytes_free(aloracle_bytes* bytes)
{
    if (bytes)
    {
        std::free(bytes->data);
        *bytes = aloracle_bytes{ nullptr, 0 };
    }
}

int32_t aloracle_abi_version()
{
    return ALORACLE_ABI_VERSION;
}

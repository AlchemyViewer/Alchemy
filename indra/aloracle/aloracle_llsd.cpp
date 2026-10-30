/**
 * @file aloracle_llsd.cpp
 * @brief aloracle's functions over LLSD's parsers and formatters.
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

#include "llpointer.h"
#include "llsd.h"
#include "llsdserialize.h"

#include <sstream>

static_assert((int)ALORACLE_LLSD_BINARY == (int)LLSDSerialize::LLSD_BINARY);
static_assert((int)ALORACLE_LLSD_XML == (int)LLSDSerialize::LLSD_XML);
static_assert((int)ALORACLE_LLSD_NOTATION == (int)LLSDSerialize::LLSD_NOTATION);
static_assert((int)ALORACLE_LLSD_OPTIONS_NONE == (int)LLSDFormatter::OPTIONS_NONE);
static_assert((int)ALORACLE_LLSD_OPTIONS_PRETTY == (int)LLSDFormatter::OPTIONS_PRETTY);
static_assert((int)ALORACLE_LLSD_OPTIONS_PRETTY_BINARY == (int)LLSDFormatter::OPTIONS_PRETTY_BINARY);

namespace
{
    LLPointer<LLSDParser> parserFor(int32_t format)
    {
        switch (format)
        {
        case ALORACLE_LLSD_BINARY:   return new LLSDBinaryParser;
        case ALORACLE_LLSD_XML:      return new LLSDXMLParser;
        case ALORACLE_LLSD_NOTATION: return new LLSDNotationParser;
        default:                     return {};
        }
    }

    LLPointer<LLSDFormatter> formatterFor(int32_t format)
    {
        switch (format)
        {
        case ALORACLE_LLSD_BINARY:   return new LLSDBinaryFormatter;
        case ALORACLE_LLSD_XML:      return new LLSDXMLFormatter;
        case ALORACLE_LLSD_NOTATION: return new LLSDNotationFormatter;
        default:                     return {};
        }
    }

    bool knownOptions(int32_t options)
    {
        return !(options & ~(ALORACLE_LLSD_OPTIONS_PRETTY | ALORACLE_LLSD_OPTIONS_PRETTY_BINARY));
    }
}

int32_t aloracle_llsd_parse(int32_t format,
                            const uint8_t* data,
                            size_t size,
                            int64_t max_bytes,
                            int32_t max_depth,
                            aloracle_bytes* value,
                            int32_t* count)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        LLPointer<LLSDParser> parser = parserFor(format);
        if (!aloracle::clear(value) || !aloracle::take(data, size, text) || parser.isNull())
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::istringstream stream(text);
        LLSD parsed;
        const S32 parsed_count = parser->parse(stream, parsed, (llssize)max_bytes, max_depth);
        if (count)
        {
            *count = parsed_count;
        }
        if (parsed_count == LLSDParser::PARSE_FAILURE)
        {
            return ALORACLE_FAILED;
        }
        return aloracle::giveValue(parsed, value);
    });
}

int32_t aloracle_llsd_deserialize(const uint8_t* data, size_t size, int64_t max_bytes, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!aloracle::clear(value) || !aloracle::take(data, size, text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::istringstream stream(text);
        LLSD parsed;
        if (!LLSDSerialize::deserialize(parsed, stream, (llssize)max_bytes))
        {
            return ALORACLE_FAILED;
        }
        return aloracle::giveValue(parsed, value);
    });
}

int32_t aloracle_llsd_format(int32_t format,
                             int32_t options,
                             const uint8_t* value,
                             size_t size,
                             aloracle_bytes* text)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD formatted;
        LLPointer<LLSDFormatter> formatter = formatterFor(format);
        if (!aloracle::clear(text) || formatter.isNull() || !knownOptions(options)
            || !aloracle::takeValue(value, size, formatted))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::ostringstream stream;
        formatter->format(formatted, stream, (LLSDFormatter::EFormatterOptions)options);
        return aloracle::give(stream.view(), text);
    });
}

int32_t aloracle_llsd_serialize(int32_t format,
                                int32_t options,
                                const uint8_t* value,
                                size_t size,
                                aloracle_bytes* text)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD serialized;
        if (!aloracle::clear(text) || formatterFor(format).isNull() || !knownOptions(options)
            || !aloracle::takeValue(value, size, serialized))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::ostringstream stream;
        LLSDSerialize::serialize(serialized,
                                 stream,
                                 (LLSDSerialize::ELLSD_Serialize)format,
                                 (LLSDFormatter::EFormatterOptions)options);
        return aloracle::give(stream.view(), text);
    });
}

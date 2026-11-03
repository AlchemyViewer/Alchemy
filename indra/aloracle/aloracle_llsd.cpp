/**
 * @file aloracle_llsd.cpp
 * @brief aloracle's functions over LLSD's parsers, formatters, conversions, zipped blocks and XML-RPC values.
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

#include <limits>
#include <sstream>
#include <string>

static_assert((int)ALORACLE_LLSD_BINARY == (int)LLSDSerialize::LLSD_BINARY);
static_assert((int)ALORACLE_LLSD_XML == (int)LLSDSerialize::LLSD_XML);
static_assert((int)ALORACLE_LLSD_NOTATION == (int)LLSDSerialize::LLSD_NOTATION);
static_assert((int)ALORACLE_LLSD_OPTIONS_NONE == (int)LLSDFormatter::OPTIONS_NONE);
static_assert((int)ALORACLE_LLSD_OPTIONS_PRETTY == (int)LLSDFormatter::OPTIONS_PRETTY);
static_assert((int)ALORACLE_LLSD_OPTIONS_PRETTY_BINARY == (int)LLSDFormatter::OPTIONS_PRETTY_BINARY);
static_assert((int)ALORACLE_LLSD_TYPE_UNDEFINED == (int)LLSD::TypeUndefined);
static_assert((int)ALORACLE_LLSD_TYPE_BOOLEAN == (int)LLSD::TypeBoolean);
static_assert((int)ALORACLE_LLSD_TYPE_INTEGER == (int)LLSD::TypeInteger);
static_assert((int)ALORACLE_LLSD_TYPE_REAL == (int)LLSD::TypeReal);
static_assert((int)ALORACLE_LLSD_TYPE_STRING == (int)LLSD::TypeString);
static_assert((int)ALORACLE_LLSD_TYPE_UUID == (int)LLSD::TypeUUID);
static_assert((int)ALORACLE_LLSD_TYPE_DATE == (int)LLSD::TypeDate);
static_assert((int)ALORACLE_LLSD_TYPE_URI == (int)LLSD::TypeURI);
static_assert((int)ALORACLE_LLSD_TYPE_BINARY == (int)LLSD::TypeBinary);
static_assert((int)ALORACLE_LLSD_TYPE_MAP == (int)LLSD::TypeMap);
static_assert((int)ALORACLE_LLSD_TYPE_ARRAY == (int)LLSD::TypeArray);

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

int32_t aloracle_llsd_convert(int32_t type, const uint8_t* value, size_t size, aloracle_bytes* converted)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD from;
        if (!aloracle::clear(converted) || !aloracle::takeValue(value, size, from))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLSD to;
        switch (type)
        {
        case ALORACLE_LLSD_TYPE_BOOLEAN: to = LLSD(from.asBoolean()); break;
        case ALORACLE_LLSD_TYPE_INTEGER: to = LLSD(from.asInteger()); break;
        case ALORACLE_LLSD_TYPE_REAL:    to = LLSD(from.asReal()); break;
        case ALORACLE_LLSD_TYPE_STRING:  to = LLSD(from.asString()); break;
        case ALORACLE_LLSD_TYPE_UUID:    to = LLSD(from.asUUID()); break;
        case ALORACLE_LLSD_TYPE_DATE:    to = LLSD(from.asDate()); break;
        case ALORACLE_LLSD_TYPE_URI:     to = LLSD(from.asURI()); break;
        case ALORACLE_LLSD_TYPE_BINARY:  to = LLSD(from.asBinary()); break;
        default:                         return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::giveValue(to, converted);
    });
}

int32_t aloracle_llsd_zip(const uint8_t* value, size_t size, aloracle_bytes* zipped)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD data;
        if (!aloracle::clear(zipped) || !aloracle::takeValue(value, size, data))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        const std::string result = zip_llsd(data);
        if (result.empty())
        {
            return ALORACLE_FAILED;
        }
        return aloracle::give(result, zipped);
    });
}

int32_t aloracle_llsd_unzip(const uint8_t* data, size_t size, aloracle_bytes* value, int32_t* result)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || !aloracle::take(data, size, bytes)
            || size > (size_t)std::numeric_limits<S32>::max())
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLSD unzipped;
        const LLUZipHelper::EZipRresult unzip_result =
            LLUZipHelper::unzip_llsd(unzipped, reinterpret_cast<const U8*>(bytes.data()), (S32)bytes.size());
        if (result)
        {
            *result = unzip_result;
        }
        if (unzip_result != LLUZipHelper::ZR_OK)
        {
            return ALORACLE_FAILED;
        }
        return aloracle::giveValue(unzipped, value);
    });
}

int32_t aloracle_llsd_as_xmlrpc(const uint8_t* value, size_t size, aloracle_bytes* text)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD data;
        if (!aloracle::clear(text) || !aloracle::takeValue(value, size, data))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::give(data.asXMLRPCValue(), text);
    });
}

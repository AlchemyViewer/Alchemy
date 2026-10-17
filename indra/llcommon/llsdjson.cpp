/**
 * @file llsdjson.cpp
 * @brief LLSD flexible data system
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2015, Linden Research, Inc.
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

// Must turn on conditional declarations in header file so definitions end up
// with proper linkage.
#include "linden_common.h"

#include "llsdjson.h"

#include "llsdserialize.h"
#include "llsdutil.h"
#include "llerror.h"

#include <cmath>
#include <cstdlib>
#include <utility>

#include <simdutf.h>

namespace
{
    // LLSD's Integer is 32 bits. A JSON integer past it keeps its magnitude
    // as a Real rather than its low bits, which is all a narrowing gives.
    template <typename T>
    LLSD json_integer(T value)
    {
        if (std::in_range<LLSD::Integer>(value))
        {
            return LLSD(static_cast<LLSD::Integer>(value));
        }
        return LLSD(static_cast<LLSD::Real>(value));
    }
}

//=========================================================================
LLSD LlsdFromJson(const simdjson::dom::element& val)
{
    LLSD result;

    switch (val.type())
    {
    default:
    case simdjson::dom::element_type::NULL_VALUE:
        break;
    case simdjson::dom::element_type::INT64:
        result = json_integer(val.get_int64().value_unsafe());
        break;
    case simdjson::dom::element_type::UINT64:
        result = json_integer(val.get_uint64().value_unsafe());
        break;
    case simdjson::dom::element_type::DOUBLE:
        result = LLSD(val.get_double().value_unsafe());
        break;
    case simdjson::dom::element_type::BIGINT:
    {
        // integer too large for int64/uint64: degrade to Real, matching how
        // such literals previously parsed as doubles. Only reachable if the
        // parser enables bigint storage; the default configuration fails the
        // parse with BIGINT_ERROR instead.
        double bigval;
        if (val.get_double().get(bigval) != simdjson::SUCCESS)
        {
            std::string_view digits;
            if (val.get_bigint().get(digits) != simdjson::SUCCESS)
            {
                break;
            }
            // saturates to +/-HUGE_VAL rather than failing
            bigval = strtod(std::string(digits).c_str(), nullptr);
        }
        result = LLSD(bigval);
        break;
    }
    case simdjson::dom::element_type::STRING:
        result = LLSD(std::string(val.get_string().value_unsafe()));
        break;
    case simdjson::dom::element_type::BOOL:
        result = LLSD(val.get_bool().value_unsafe());
        break;
    case simdjson::dom::element_type::ARRAY:
    {
        simdjson::dom::array array = val.get_array().value_unsafe();
        // reserved to avoid incremental allocation
        result = LLSD::emptyReservedArray(array.size());
        for (const simdjson::dom::element& element : array)
        {
            result.append(LlsdFromJson(element));
        }
        break;
    }
    case simdjson::dom::element_type::OBJECT:
    {
        result = LLSD::emptyMap();
        // copy the handle out first: iterating the simdjson_result temporary
        // dangles, as range-for does not extend the inner temporary's lifetime
        simdjson::dom::object object = val.get_object().value_unsafe();
        for (const simdjson::dom::key_value_pair& member : object)
        {
            result[member.key] = LlsdFromJson(member.value);
        }
        break;
    }
    }
    return result;
}

//=========================================================================
// parser reuse avoids reallocating its internal buffers on every call; the
// converted LLSD owns all its data, so nothing references the parser once
// the conversion returns
static simdjson::dom::parser& json_parser()
{
    thread_local simdjson::dom::parser parser;
    constexpr size_t MAX_DEPTH = LLSDParser::DEFAULT_MAX_DEPTH;
    if (parser.max_depth() != MAX_DEPTH)
    {
        // LlsdFromJson() recurses once per level, and the tree it builds is
        // destroyed and formatted recursively, so documents nest no deeper
        // than the LLSD parsers allow. The buffers keep the depth they were
        // allocated with as they grow, but a parser that was never allocated,
        // or failed to be, grows them to simdjson's own.
        constexpr size_t INITIAL_CAPACITY = 4 * 1024;
        if (parser.allocate(INITIAL_CAPACITY, MAX_DEPTH) != simdjson::SUCCESS)
        {
            LL_WARNS("LlsdJson") << "Could not allocate the JSON parser" << LL_ENDL;
        }
    }
    return parser;
}

// The parser's capacity is the largest document it has seen, its buffers about
// fourteen times that, and both are kept between calls. Once the capacity is
// past this the buffers are let go when the document is converted, rather than
// held for the life of the thread.
static constexpr size_t JSON_PARSER_KEEP_CAPACITY = 1024 * 1024;

static bool llsd_from_parsed(simdjson::simdjson_result<simdjson::dom::element> parsed,
                             LLSD& out, std::string* errmsg)
{
    simdjson::dom::element root;
    simdjson::error_code err = parsed.get(root);
    if (err != simdjson::SUCCESS)
    {
        if (errmsg)
        {
            *errmsg = simdjson::error_message(err);
        }
        out = LLSD();
    }
    else
    {
        out = LlsdFromJson(root);
    }

    simdjson::dom::parser& parser = json_parser();
    if (parser.capacity() > JSON_PARSER_KEEP_CAPACITY)
    {
        parser = simdjson::dom::parser();
    }
    return err == simdjson::SUCCESS;
}

bool LlsdFromJsonString(std::string_view json, LLSD& out, std::string* errmsg)
{
    // simdjson copies the input with memcpy(), which takes no null pointer
    // even for no bytes, and an empty view may have none
    const char* data = json.data() ? json.data() : "";
    return llsd_from_parsed(json_parser().parse(data, json.size()), out, errmsg);
}

bool LlsdFromJsonString(const simdjson::padded_string& json, LLSD& out, std::string* errmsg)
{
    return llsd_from_parsed(json_parser().parse(json), out, errmsg);
}

//=========================================================================
static void llsd_to_json(simdjson::builder::string_builder& dst, const LLSD& val)
{
    switch (val.type())
    {
    case LLSD::TypeUndefined:
        dst.append_null();
        break;
    case LLSD::TypeBoolean:
        dst.append(val.asBoolean());
        break;
    case LLSD::TypeInteger:
        dst.append(val.asInteger());
        break;
    case LLSD::TypeReal:
    {
        F64 real = val.asReal();
        if (std::isfinite(real))
        {
            dst.append(real);
        }
        else
        {
            // JSON has no representation for NaN or infinities
            dst.append_null();
        }
        break;
    }
    case LLSD::TypeURI:
    case LLSD::TypeDate:
    case LLSD::TypeUUID:
    case LLSD::TypeString:
        dst.escape_and_append_with_quotes(val.asString());
        break;
    case LLSD::TypeMap:
    {
        dst.start_object();
        bool first = true;
        for (const auto& llsd_dat : llsd::inMap(val))
        {
            if (!first)
            {
                dst.append_comma();
            }
            first = false;
            dst.escape_and_append_with_quotes(llsd_dat.first);
            dst.append_colon();
            llsd_to_json(dst, llsd_dat.second);
        }
        dst.end_object();
        break;
    }
    case LLSD::TypeArray:
    {
        dst.start_array();
        bool first = true;
        for (const auto& llsd_dat : llsd::inArray(val))
        {
            if (!first)
            {
                dst.append_comma();
            }
            first = false;
            llsd_to_json(dst, llsd_dat);
        }
        dst.end_array();
        break;
    }
    case LLSD::TypeBinary:
    {
        // JSON has no bytes; base64 in a string, as LLSD's XML carries them.
        // A value is no reason to bring the viewer down.
        const LLSD::Binary& bytes = val.asBinary();
        std::string encoded(simdutf::base64_length_from_binary(bytes.size()), '\0');
        simdutf::binary_to_base64((const char*)bytes.data(), bytes.size(), encoded.data());
        dst.escape_and_append_with_quotes(encoded);
        break;
    }
    default:
        LL_WARNS("LlsdToJson") << "Unsupported conversion to JSON from LLSD type ("
                               << val.type() << ")." << LL_ENDL;
        dst.append_null();
        break;
    }
}

std::string LlsdToJson(const LLSD& val)
{
    simdjson::builder::string_builder builder;
    llsd_to_json(builder, val);

    std::string_view view;
    if (builder.view().get(view) != simdjson::SUCCESS)
    {
        LL_WARNS("LlsdToJson") << "Allocation failure serializing LLSD to JSON" << LL_ENDL;
        return std::string();
    }
    return std::string(view);
}

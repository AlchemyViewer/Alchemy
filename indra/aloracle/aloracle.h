/**
 * @file aloracle.h
 * @brief C functions over the viewer's C++.
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

#pragma once

#include <stddef.h>
#include <stdint.h>

// aloracle, built with AL_BUILD_ORACLE: one shared library of C functions,
// each over one entry point of the viewer's own C++, which does what that
// code does, its faults included.
//
// - Every function returns a status, an ALORACLE_* below, but for the
//   quantizers, which cannot fail. Nothing is thrown across the boundary: an
//   exception, or an LL_ERRS, which would have ended the viewer, comes back
//   as a status.
// - Bytes go in as a pointer and a size, and are not NUL-terminated. The
//   pointer may be null where the size is 0.
// - Bytes come out in an aloracle_bytes, which the library allocates and the
//   caller frees with aloracle_bytes_free. A function overwrites the one it
//   is given, and leaves it empty on any status but ALORACLE_OK.
// - An LLSD value crosses in LLSD binary with no header line, as
//   LLSDSerialize::toBinary writes it. A value passed in is read by
//   LLSDBinaryParser, to its default depth of 256, and must be the whole of
//   the bytes.
// - Any function may be called from any thread, and from several at once.
// - The library is built with the viewer's compiler flags, so an optimised
//   build has its fast floating point too.

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(ALORACLE_BUILDING)
#define ALORACLE_API __declspec(dllexport)
#else
#define ALORACLE_API __declspec(dllimport)
#endif
#else
#define ALORACLE_API __attribute__((visibility("default")))
#endif

/// What a function returns.
enum
{
    ALORACLE_OK = 0,           ///< done, with its output filled
    ALORACLE_BAD_ARGUMENT = 1, ///< refused before the C++ ran: a null pointer, an unknown
                               ///< format, option or set, or a value that is not LLSD binary
    ALORACLE_FAILED = 2,       ///< the C++ ran and reported a failure, as a parse does
    ALORACLE_EXCEPTION = 3,    ///< the C++ threw, std::bad_alloc included
    ALORACLE_FATAL = 4,        ///< the C++ called LL_ERRS
};

/// Bytes the library allocated.
typedef struct aloracle_bytes
{
    uint8_t* data; ///< null where size is 0
    size_t size;
} aloracle_bytes;

/// Frees what bytes holds and leaves it empty. An empty one, or a null
/// pointer, is left as it is.
ALORACLE_API void aloracle_bytes_free(aloracle_bytes* bytes);

//
// LLSD: llsd.cpp, llsdserialize.cpp and llsdserialize_xml.cpp
//

/// The formats, numbered as LLSDSerialize::ELLSD_Serialize numbers them.
enum
{
    ALORACLE_LLSD_BINARY = 0,
    ALORACLE_LLSD_XML = 1,
    ALORACLE_LLSD_NOTATION = 2,
};

/// A formatter's options, which combine, as LLSDFormatter::EFormatterOptions.
enum
{
    ALORACLE_LLSD_OPTIONS_NONE = 0,
    ALORACLE_LLSD_OPTIONS_PRETTY = 1,
    ALORACLE_LLSD_OPTIONS_PRETTY_BINARY = 2,
};

/// Reads one value from data with the format's parser (LLSDParser::parse),
/// to a budget of max_bytes, -1 for none, and a depth of max_depth, a
/// negative one for the parser's default of 256. value gets what it read;
/// count, which may be null, gets what parse returned: the number of values
/// read, or -1, which is ALORACLE_FAILED.
ALORACLE_API int32_t aloracle_llsd_parse(int32_t format,
                                         const uint8_t* data,
                                         size_t size,
                                         int64_t max_bytes,
                                         int32_t max_depth,
                                         aloracle_bytes* value,
                                         int32_t* count);

/// Reads a value as LLSDSerialize::deserialize does, choosing the format by
/// what data holds, to a budget of max_bytes, -1 for none. Where it returns
/// false, ALORACLE_FAILED.
ALORACLE_API int32_t aloracle_llsd_deserialize(const uint8_t* data,
                                               size_t size,
                                               int64_t max_bytes,
                                               aloracle_bytes* value);

/// Writes a value with the format's formatter and the options
/// (LLSDFormatter::format), with no header line.
ALORACLE_API int32_t aloracle_llsd_format(int32_t format,
                                          int32_t options,
                                          const uint8_t* value,
                                          size_t size,
                                          aloracle_bytes* text);

/// Writes a value as LLSDSerialize::serialize does: the format's header line,
/// then what its formatter writes with the options.
ALORACLE_API int32_t aloracle_llsd_serialize(int32_t format,
                                             int32_t options,
                                             const uint8_t* value,
                                             size_t size,
                                             aloracle_bytes* text);

/// The types, numbered as LLSD::Type numbers them.
enum
{
    ALORACLE_LLSD_TYPE_UNDEFINED = 0,
    ALORACLE_LLSD_TYPE_BOOLEAN = 1,
    ALORACLE_LLSD_TYPE_INTEGER = 2,
    ALORACLE_LLSD_TYPE_REAL = 3,
    ALORACLE_LLSD_TYPE_STRING = 4,
    ALORACLE_LLSD_TYPE_UUID = 5,
    ALORACLE_LLSD_TYPE_DATE = 6,
    ALORACLE_LLSD_TYPE_URI = 7,
    ALORACLE_LLSD_TYPE_BINARY = 8,
    ALORACLE_LLSD_TYPE_MAP = 9,
    ALORACLE_LLSD_TYPE_ARRAY = 10,
};

/// Reads a value as another type with LLSD's own conversion to it (asBoolean,
/// asInteger, asReal, asString, asUUID, asDate, asURI or asBinary), and gives
/// converted what that returns. Undefined, a map and an array have none, and
/// are ALORACLE_BAD_ARGUMENT.
ALORACLE_API int32_t aloracle_llsd_convert(int32_t type,
                                           const uint8_t* value,
                                           size_t size,
                                           aloracle_bytes* converted);

/// Zips a value as zip_llsd does: zlib at its best compression, over the
/// value in LLSD binary with no header line. Where zip_llsd gives nothing,
/// ALORACLE_FAILED.
ALORACLE_API int32_t aloracle_llsd_zip(const uint8_t* value, size_t size, aloracle_bytes* zipped);

/// Unzips data as LLUZipHelper::unzip_llsd does from bytes in memory, and
/// gives value the LLSD it read. result, which may be null, gets what
/// unzip_llsd returned, an LLUZipHelper::EZipRresult; any but ZR_OK, 0, is
/// ALORACLE_FAILED. More than 2^31 - 1 bytes, which unzip_llsd cannot be
/// given, is ALORACLE_BAD_ARGUMENT.
ALORACLE_API int32_t aloracle_llsd_unzip(const uint8_t* data,
                                         size_t size,
                                         aloracle_bytes* value,
                                         int32_t* result);

//
// LLURI: lluri.cpp
//

/// The sets of characters LLURI's escapers leave as they are, as
/// LLURI::EscapeSet names and numbers them.
enum
{
    ALORACLE_LLURI_UNRESERVED = 0, ///< LLURI::escape(str)'s own
    ALORACLE_LLURI_HOST_AND_PORT = 1,
    ALORACLE_LLURI_PATH_COMPONENT = 2,
    ALORACLE_LLURI_QUERY_VARIABLE = 3,
    ALORACLE_LLURI_QUERY_VALUE = 4,
    ALORACLE_LLURI_URI_QUERY = 5,
    ALORACLE_LLURI_URI_DATA = 6,
    ALORACLE_LLURI_URI_PATH = 7,
};

/// Escapes data as LLURI::escape does, leaving the set as it is.
ALORACLE_API int32_t aloracle_lluri_escape(int32_t set,
                                           const uint8_t* data,
                                           size_t size,
                                           aloracle_bytes* escaped);

/// Unescapes data as LLURI::unescape does.
ALORACLE_API int32_t aloracle_lluri_unescape(const uint8_t* data,
                                             size_t size,
                                             aloracle_bytes* unescaped);

/// Reads an escaped query, with no '?', as LLURI::queryMap does, and gives
/// value the LLSD it returns.
ALORACLE_API int32_t aloracle_lluri_query_map(const uint8_t* data,
                                              size_t size,
                                              aloracle_bytes* value);

/// Writes a value as a query, '?' and all, as LLURI::mapToQueryString does.
ALORACLE_API int32_t aloracle_lluri_map_to_query_string(const uint8_t* value,
                                                        size_t size,
                                                        aloracle_bytes* query);

//
// LLDate: lldate.cpp
//

/// Writes a date, in seconds since 1970 UTC, as LLDate::asString does.
ALORACLE_API int32_t aloracle_lldate_format(double seconds, aloracle_bytes* text);

/// Reads a date as LLDate::fromString does, giving seconds the seconds since
/// 1970 UTC, or 0, the epoch, where it fails, ALORACLE_FAILED.
ALORACLE_API int32_t aloracle_lldate_parse(const uint8_t* data, size_t size, double* seconds);

//
// Quantized floats: llquantize.h. Each returns what its C++ returns.
//

ALORACLE_API uint8_t aloracle_f32_to_u8(float value, float lower, float upper);       ///< F32_to_U8
ALORACLE_API uint8_t aloracle_f32_to_u8_round(float value, float lower, float upper); ///< F32_to_U8_ROUND
ALORACLE_API float aloracle_u8_to_f32(uint8_t value, float lower, float upper);       ///< U8_to_F32
ALORACLE_API uint16_t aloracle_f32_to_u16(float value, float lower, float upper);     ///< F32_to_U16
ALORACLE_API uint16_t aloracle_f32_to_u16_round(float value, float lower, float upper); ///< F32_to_U16_ROUND
ALORACLE_API float aloracle_u16_to_f32(uint16_t value, float lower, float upper);     ///< U16_to_F32

#ifdef __cplusplus
}
#endif

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
// - Any function may be called from any thread, and from several at once,
//   though the library runs one call at a time.
// - The library, and every library it links, is built with /fp:precise, or
//   -ffp-contract=off on GCC and Clang, in place of the viewer's fast
//   floating point: it computes as the source says, with no contraction or
//   reassociation.
// - aloracle_abi_version gives the ALORACLE_ABI_VERSION the library was built
//   with, raised whenever a function is removed or its arguments, results or
//   meaning change, so a caller built against another can refuse the library
//   rather than call it wrongly.

#define ALORACLE_ABI_VERSION 1

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

/// The ALORACLE_ABI_VERSION the library was built with.
ALORACLE_API int32_t aloracle_abi_version(void);

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

//
// Messages: llzerocode.h, llmessagetemplateparser.cpp,
// lltemplatemessagereader.cpp and lltemplatemessagebuilder.cpp
//

/// Zerocodes packet as LLZeroCode::encode does, leaving its first header_size
/// bytes as they are, into a buffer twice its size. Where encode returns -1,
/// as it does where the packet would not shrink or is shorter than
/// header_size, ALORACLE_FAILED. More than 2^31 - 1 bytes is
/// ALORACLE_BAD_ARGUMENT.
ALORACLE_API int32_t aloracle_zerocode_encode(const uint8_t* packet,
                                              size_t size,
                                              uint32_t header_size,
                                              aloracle_bytes* encoded);

/// Expands packet as LLZeroCode::decode does into a buffer of capacity bytes,
/// and gives expanded the bytes decode wrote: none where packet is shorter
/// than header_size or its first byte has no 0x80. Where decode sets
/// overflow, ALORACLE_FAILED. A capacity under header_size, where decode would
/// write the header past the buffer, and more than 2^31 - 1 bytes, are
/// ALORACLE_BAD_ARGUMENT.
ALORACLE_API int32_t aloracle_zerocode_decode(const uint8_t* packet,
                                              size_t size,
                                              uint32_t capacity,
                                              uint32_t header_size,
                                              aloracle_bytes* expanded);

/// How often a message is sent, as EMsgFrequency numbers it: the bytes its
/// number takes.
enum
{
    ALORACLE_MESSAGE_FREQUENCY_HIGH = 1,
    ALORACLE_MESSAGE_FREQUENCY_MEDIUM = 2,
    ALORACLE_MESSAGE_FREQUENCY_LOW = 4, ///< Low, and Fixed
};

/// Whether a message is trusted, as EMsgTrust numbers it.
enum
{
    ALORACLE_MESSAGE_TRUSTED = 0,
    ALORACLE_MESSAGE_NOT_TRUSTED = 1,
};

/// How a message is encoded, as EMsgEncoding numbers it.
enum
{
    ALORACLE_MESSAGE_UNENCODED = 0,
    ALORACLE_MESSAGE_ZEROCODED = 1,
};

/// How far a message is deprecated, as EMsgDeprecation numbers it.
enum
{
    ALORACLE_MESSAGE_NOT_DEPRECATED = 0,
    ALORACLE_MESSAGE_UDP_DEPRECATED = 1,
    ALORACLE_MESSAGE_UDP_BLACKLISTED = 2,
    ALORACLE_MESSAGE_DEPRECATED = 3, ///< which the parser drops, so no message has it
};

/// A block's type, as EMsgBlockType numbers it.
enum
{
    ALORACLE_MESSAGE_BLOCK_SINGLE = 1,
    ALORACLE_MESSAGE_BLOCK_MULTIPLE = 2,
    ALORACLE_MESSAGE_BLOCK_VARIABLE = 3,
};

/// The types the parser gives a block's variables, as EMsgVariableType
/// numbers them.
enum
{
    ALORACLE_MESSAGE_TYPE_FIXED = 1,
    ALORACLE_MESSAGE_TYPE_VARIABLE = 2,
    ALORACLE_MESSAGE_TYPE_U8 = 3,
    ALORACLE_MESSAGE_TYPE_U16 = 4,
    ALORACLE_MESSAGE_TYPE_U32 = 5,
    ALORACLE_MESSAGE_TYPE_U64 = 6,
    ALORACLE_MESSAGE_TYPE_S8 = 7,
    ALORACLE_MESSAGE_TYPE_S16 = 8,
    ALORACLE_MESSAGE_TYPE_S32 = 9,
    ALORACLE_MESSAGE_TYPE_S64 = 10,
    ALORACLE_MESSAGE_TYPE_F32 = 11,
    ALORACLE_MESSAGE_TYPE_F64 = 12,
    ALORACLE_MESSAGE_TYPE_LLVECTOR3 = 13,
    ALORACLE_MESSAGE_TYPE_LLVECTOR3D = 14,
    ALORACLE_MESSAGE_TYPE_LLVECTOR4 = 15,
    ALORACLE_MESSAGE_TYPE_LLQUATERNION = 16,
    ALORACLE_MESSAGE_TYPE_LLUUID = 17,
    ALORACLE_MESSAGE_TYPE_BOOL = 18,
    ALORACLE_MESSAGE_TYPE_IP_ADDR = 19,
    ALORACLE_MESSAGE_TYPE_IP_PORT = 20,
};

/// A template's messages, kept as LLMessageSystem keeps them, by name and by
/// number, and in the order the parser made them.
typedef struct aloracle_message_template aloracle_message_template;

/// Reads a template from its text as LLMessageSystem::loadTemplateFile does
/// once it has read the file: LLTemplateTokenizer, LLTemplateParser, then
/// addTemplate for each message. message_template gets it, for the caller to
/// free with aloracle_message_template_free. The parser's LL_ERRS on a
/// malformed template, and addTemplate's on a name used twice, are
/// ALORACLE_FATAL; what the parser had made before its LL_ERRS is not freed.
/// Names are interned for the life of the process, as LLMessageStringTable
/// interns them, in a table of 8192 with no way out of it, where a lookup of
/// a new name in a full table never returns. So a template with so many
/// distinct tokens that, with the names the table holds already, it could
/// bring the table to 8191, where getString starts to warn of it, is
/// ALORACLE_BAD_ARGUMENT, refused before anything is interned.
ALORACLE_API int32_t aloracle_message_template_new(const uint8_t* text,
                                                   size_t size,
                                                   aloracle_message_template** message_template);

/// Frees a template, deleting each of its messages. Null is left as it is.
ALORACLE_API void aloracle_message_template_free(aloracle_message_template* message_template);

/// Gives value what the parser made of a template, in LLSD: a map of
/// - version: LLTemplateParser::getVersion, a real
/// - messages: an array, in the template's order, of maps of
///   - name, as the string table keeps it, cut to 63 bytes
///   - number: mMessageNumber, a U32, as the integer with its bits
///   - frequency, trust, encoding and deprecation: an ALORACLE_MESSAGE_*
///     above each
///   - blocks: an array, in the template's order, of maps of name, type (an
///     ALORACLE_MESSAGE_BLOCK_*), number (mNumber) and variables: an array,
///     in the template's order, of maps of name, type (an
///     ALORACLE_MESSAGE_TYPE_*) and size (getSize)
ALORACLE_API int32_t aloracle_message_template_describe(const aloracle_message_template* message_template,
                                                        aloracle_bytes* value);

/// What a message read did.
enum
{
    ALORACLE_MESSAGE_READ = 0,    ///< read, and written back
    ALORACLE_MESSAGE_REFUSED = 1, ///< LLTemplateMessageReader::validateMessage refused it: too short for
                                  ///< its number, a number not in the template, or a message the
                                  ///< template marks UDPBlackListed
    ALORACLE_MESSAGE_DROPPED = 2, ///< decodeData refused it: a message whose blocks are all Variable,
                                  ///< none of which came
};

/// Reads a packet as the viewer's message log does, with no message system:
/// clearMessage, validateMessage as trusted, then decodeData as custom.
/// packet is what LLMessageSystem::zeroCodeExpand leaves: acks off, the body
/// expanded, the six header bytes on. Where it reads, writes it back as
/// LLTemplateMessageBuilder does: newMessage with the template's name,
/// copyToBuilder, then buildMessage with no offset. written gets what
/// buildMessage wrote from the message number on, and is left empty unless
/// result gets ALORACLE_MESSAGE_READ. result, which may be null, gets an
/// ALORACLE_MESSAGE_* above. A packet of fewer than
/// LL_MINIMUM_VALID_PACKET_SIZE bytes, 7, which the message log does not
/// read, or of more than 2^31 - 1, is ALORACLE_BAD_ARGUMENT.
ALORACLE_API int32_t aloracle_message_read(const aloracle_message_template* message_template,
                                           const uint8_t* packet,
                                           size_t size,
                                           aloracle_bytes* written,
                                           int32_t* result);

#ifdef __cplusplus
}
#endif

/**
 * @file llsdjson.h
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

#ifndef LL_LLSDJSON_H
#define LL_LLSDJSON_H

#include <string>
#include <string_view>

#include "stdtypes.h"

#include "llsd.h"
#include <simdjson.h>

/// Convert a parsed JSON document element into LLSD maintaining member names
/// and array indexes.
/// JSON/JavaScript types are converted as follows:
///
/// JSON Type     | LLSD Type
/// --------------+--------------
///  null         |  undefined
///  integer      |  LLSD::Integer
///  unsigned     |  LLSD::Integer
///  real/numeric |  LLSD::Real
///  string       |  LLSD::String
///  boolean      |  LLSD::Boolean
///  array        |  LLSD::Array
///  object       |  LLSD::Map
///
/// For maps and arrays child entries will be converted and added to the structure.
/// Order is preserved for an array but not for objects.
LLSD LlsdFromJson(const simdjson::dom::element& val);

/// Parse a JSON document from text and convert it into LLSD as above.
/// Returns false (leaving out undefined) on a parse failure; if errmsg is
/// non-null it receives a description of the failure.
bool LlsdFromJsonString(std::string_view json, LLSD& out, std::string* errmsg = nullptr);

/// As above, but parses the buffer in place with no internal copy. Callers
/// that materialize a document from raw bytes should read directly into a
/// simdjson::padded_string and use this overload.
bool LlsdFromJsonString(const simdjson::padded_string& json, LLSD& out, std::string* errmsg = nullptr);

/// disambiguate std::string and C strings against the string_view and
/// padded_string overloads (padded_string converts implicitly from
/// std::string)
inline bool LlsdFromJsonString(const std::string& json, LLSD& out, std::string* errmsg = nullptr)
{
    return LlsdFromJsonString(std::string_view(json), out, errmsg);
}

inline bool LlsdFromJsonString(const char* json, LLSD& out, std::string* errmsg = nullptr)
{
    return LlsdFromJsonString(std::string_view(json), out, errmsg);
}

/// Serialize an LLSD object into compact JSON text maintaining member names
/// and array indexes.
///
/// Types are converted as follows:
/// LLSD Type     |  JSON Type
/// --------------+----------------
/// TypeUndefined | null
/// TypeBoolean   | boolean
/// TypeInteger   | integer
/// TypeReal      | real/numeric (non-finite values become null)
/// TypeString    | string
/// TypeURI       | string
/// TypeDate      | string
/// TypeUUID      | string
/// TypeMap       | object
/// TypeArray     | array
/// TypeBinary    | unsupported
std::string LlsdToJson(const LLSD& val);

#endif // LL_LLSDJSON_H

/**
 * @file alsettingcycle.cpp
 * @brief Flip or cycle a debug setting through values typed in chat.
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

#include "llviewerprecompiledheaders.h"

#include "alsettingcycle.h"

#include "llcontrol.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
// Every setting that is not a boolean, a string or LLSD is a list of numbers.
enum class Kind
{
    Int,
    UInt,
    Single,
    Double
};

struct Shape
{
    Kind kind;
    std::size_t count;
};

using Numbers = std::array<F64, 4>;

bool shapeOf(eControlType type, Shape& shape)
{
    switch (type)
    {
    case TYPE_S32:   shape = { Kind::Int, 1 };    return true;
    case TYPE_U32:   shape = { Kind::UInt, 1 };   return true;
    case TYPE_F32:   shape = { Kind::Single, 1 }; return true;
    case TYPE_VEC3:
    case TYPE_COL3:  shape = { Kind::Single, 3 }; return true;
    case TYPE_VEC3D: shape = { Kind::Double, 3 }; return true;
    case TYPE_QUAT:
    case TYPE_COL4:  shape = { Kind::Single, 4 }; return true;
    case TYPE_RECT:  shape = { Kind::Int, 4 };    return true;
    default:         return false;
    }
}

std::vector<std::string_view> splitList(std::string_view text, char delimiter)
{
    std::vector<std::string_view> parts;
    while (true)
    {
        const std::string_view::size_type at = text.find(delimiter);
        parts.push_back(text.substr(0, at));
        if (at == std::string_view::npos)
        {
            return parts;
        }
        text.remove_prefix(at + 1);
    }
}

// The number as the setting would hold it, or false when it does not fit.
bool fits(Kind kind, F64& value)
{
    switch (kind)
    {
    case Kind::Int:
        return value == std::floor(value) && value >= -2147483648.0 && value <= 2147483647.0;
    case Kind::UInt:
        return value == std::floor(value) && value >= 0.0 && value <= 4294967295.0;
    case Kind::Single:
        if (std::fabs(value) > std::numeric_limits<F32>::max())
        {
            return false;
        }
        value = static_cast<F64>(static_cast<F32>(value));
        return true;
    case Kind::Double:
        return true;
    }
    return false;
}

// A stored element, as the same number a typed token would give.
F64 stored(Kind kind, const LLSD& element)
{
    switch (kind)
    {
    case Kind::Int:    return static_cast<F64>(element.asInteger());
    case Kind::UInt:   return static_cast<F64>(static_cast<U32>(element.asInteger()));
    case Kind::Single: return static_cast<F64>(static_cast<F32>(element.asReal()));
    case Kind::Double: return element.asReal();
    }
    return 0.0;
}

LLSD element(Kind kind, F64 value)
{
    switch (kind)
    {
    case Kind::Int:    return LLSD(static_cast<LLSD::Integer>(value));
    case Kind::UInt:   return LLSD(static_cast<LLSD::Integer>(static_cast<U32>(value)));
    case Kind::Single:
    case Kind::Double: return LLSD(value);
    }
    return LLSD();
}

bool parseValue(const Shape& shape, const std::string& token, Numbers& out)
{
    const std::vector<std::string_view> parts = splitList(token, ',');
    if (parts.size() != shape.count)
    {
        return false;
    }
    for (std::size_t i = 0; i < shape.count; ++i)
    {
        F64 value = 0.0;
        if (!ALSettingCycle::parseNumber(parts[i], value) || !fits(shape.kind, value))
        {
            return false;
        }
        out[i] = value;
    }
    return true;
}

LLSD toLLSD(const Shape& shape, const Numbers& numbers)
{
    if (shape.count == 1)
    {
        return element(shape.kind, numbers[0]);
    }
    LLSD array = LLSD::emptyArray();
    for (std::size_t i = 0; i < shape.count; ++i)
    {
        array.append(element(shape.kind, numbers[i]));
    }
    return array;
}

bool readCurrent(const Shape& shape, const LLSD& current, Numbers& out)
{
    if (shape.count == 1)
    {
        out[0] = stored(shape.kind, current);
        return true;
    }
    if (!current.isArray() || static_cast<std::size_t>(current.size()) != shape.count)
    {
        return false;
    }
    for (std::size_t i = 0; i < shape.count; ++i)
    {
        out[i] = stored(shape.kind, current[static_cast<LLSD::Integer>(i)]);
    }
    return true;
}

// A match in the last place, or none at all, goes back to the first.
std::size_t nextIndex(std::size_t match, std::size_t count)
{
    return (match + 1 < count) ? match + 1 : 0;
}
}

bool ALSettingCycle::parseNumber(std::string_view token, F64& out)
{
    if (token.empty() || token.find_first_not_of("0123456789+-.eE") != std::string_view::npos)
    {
        return false;
    }
    const std::string text(token);
    char* end = nullptr;
    const F64 value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value))
    {
        return false;
    }
    out = value;
    return true;
}

ALSettingCycle::Result ALSettingCycle::apply(LLControlVariable& control, const std::vector<std::string>& values)
{
    const eControlType type = control.type();
    if (type == TYPE_BOOLEAN)
    {
        control.set(!control.get().asBoolean());
        return Result::Written;
    }
    if (type == TYPE_LLSD)
    {
        return Result::Unsupported;
    }
    if (values.size() < 2)
    {
        return Result::TooFewValues;
    }

    const LLSD current = control.get();
    std::size_t match = values.size();

    if (type == TYPE_STRING)
    {
        const std::string held = current.asString();
        for (std::size_t i = 0; i < values.size() && match == values.size(); ++i)
        {
            if (values[i] == held)
            {
                match = i;
            }
        }
        control.set(LLSD(values[nextIndex(match, values.size())]));
        return Result::Written;
    }

    Shape shape{};
    if (!shapeOf(type, shape))
    {
        return Result::Unsupported;
    }

    std::vector<Numbers> parsed(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (!parseValue(shape, values[i], parsed[i]))
        {
            return Result::BadValue;
        }
    }

    Numbers held{};
    if (readCurrent(shape, current, held))
    {
        for (std::size_t i = 0; i < parsed.size() && match == parsed.size(); ++i)
        {
            bool same = true;
            for (std::size_t part = 0; part < shape.count; ++part)
            {
                same = same && parsed[i][part] == held[part];
            }
            if (same)
            {
                match = i;
            }
        }
    }
    control.set(toLLSD(shape, parsed[nextIndex(match, parsed.size())]));
    return Result::Written;
}

bool ALSettingCycle::parseMapToCoordinates(const std::string& tail, std::string& region, F32& x, F32& y, bool& has_z, F32& z)
{
    constexpr char whitespace[] = " \t\r\n\v\f";
    const std::string::size_type last = tail.find_last_not_of(whitespace);
    if (last == std::string::npos)
    {
        return false;
    }
    const std::string::size_type boundary = tail.find_last_of(whitespace, last);
    if (boundary == std::string::npos)
    {
        return false; // only the coordinates, no region
    }
    region = tail.substr(0, boundary);
    LLStringUtil::trim(region);
    if (region.empty())
    {
        return false;
    }

    const std::string coords = tail.substr(boundary + 1, last - boundary);
    const std::vector<std::string_view> parts = splitList(coords, '|');
    if (parts.size() != 2 && parts.size() != 3)
    {
        return false;
    }
    F32 out[3] = {};
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        F64 value = 0.0;
        if (!parseNumber(parts[i], value) || !fits(Kind::Single, value))
        {
            return false;
        }
        out[i] = static_cast<F32>(value);
    }
    x = out[0];
    y = out[1];
    has_z = parts.size() == 3;
    if (has_z)
    {
        z = out[2];
    }
    return true;
}

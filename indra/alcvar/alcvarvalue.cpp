/**
 * @file alcvarvalue.cpp
 * @brief A setting's value of any type: compared, described, and in its LLSD
 *        and JSON forms.
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

#include "alcvarvalue.h"

#include "llsdjson.h"
#include "llsdutil.h"

#include <fmt/format.h>

#include <cmath>
#include <limits>

namespace
{
    template <typename T>
    bool sameBytes(const T& a, const T& b)
    {
        return std::memcmp(&a, &b, sizeof(T)) == 0;
    }

    template <typename T>
    bool sameList(const std::vector<T>& a, const std::vector<T>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i)
        {
            if constexpr (std::is_same_v<T, std::string>)
            {
                if (a[i] != b[i])
                {
                    return false;
                }
            }
            else if (!sameBytes(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    }

    LLSD floats(const F32* values, size_t count)
    {
        LLSD out = LLSD::emptyArray();
        for (size_t i = 0; i < count; ++i)
        {
            out.append(LLSD::Real(values[i]));
        }
        return out;
    }

    // An array of exactly `count` numbers.
    bool numbers(const LLSD& sd, size_t count, F64* out)
    {
        if (!sd.isArray() || sd.size() != count)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            const LLSD& item = sd[i];
            if (!item.isReal() && !item.isInteger())
            {
                return false;
            }
            out[i] = item.asReal();
        }
        return true;
    }

    std::optional<LLUUID> uuidOf(const LLSD& sd)
    {
        if (sd.isUUID())
        {
            return sd.asUUID();
        }
        if (sd.isString())
        {
            LLUUID id;
            if (id.set(sd.asString(), false))
            {
                return id;
            }
        }
        return std::nullopt;
    }
}

ALCVarValue ALCVarValue::zeroOf(ALCVarType type)
{
    switch (type)
    {
        case ALCVarType::Bool:       return ALCVarValue(false);
        case ALCVarType::S32:        return ALCVarValue(S32(0));
        case ALCVarType::U32:        return ALCVarValue(U32(0));
        case ALCVarType::F32:        return ALCVarValue(F32(0));
        case ALCVarType::S64:        return ALCVarValue(S64(0));
        case ALCVarType::F64:        return ALCVarValue(F64(0));
        case ALCVarType::String:     return ALCVarValue(std::string());
        case ALCVarType::UUID:       return ALCVarValue(LLUUID());
        case ALCVarType::Vec2:       return ALCVarValue(LLVector2());
        case ALCVarType::Vec3:       return ALCVarValue(LLVector3());
        case ALCVarType::Vec3d:      return ALCVarValue(LLVector3d());
        case ALCVarType::Quat:       return ALCVarValue(LLQuaternion());
        case ALCVarType::Rect:       return ALCVarValue(LLRect());
        case ALCVarType::Color3:     return ALCVarValue(LLColor3());
        case ALCVarType::Color4:     return ALCVarValue(LLColor4());
        case ALCVarType::UUIDList:   return ALCVarValue(std::vector<LLUUID>());
        case ALCVarType::StringList: return ALCVarValue(std::vector<std::string>());
        case ALCVarType::Vec2List:   return ALCVarValue(std::vector<LLVector2>());
        case ALCVarType::LLSD:       return ALCVarValue(LLSD());
    }
    return {};
}

bool operator==(const ALCVarValue& a, const ALCVarValue& b)
{
    if (a.mValue.index() != b.mValue.index())
    {
        return false;
    }
    return std::visit(
        [&b](const auto& value) -> bool {
            using T             = std::decay_t<decltype(value)>;
            const T& other      = std::get<T>(b.mValue);
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return true;
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return value == other;
            }
            else if constexpr (std::is_same_v<T, LLSD>)
            {
                return llsd_equals(value, other);
            }
            else if constexpr (std::is_same_v<T, std::vector<LLUUID>> || std::is_same_v<T, std::vector<std::string>> ||
                               std::is_same_v<T, std::vector<LLVector2>>)
            {
                return sameList(value, other);
            }
            else
            {
                return sameBytes(value, other);
            }
        },
        a.mValue);
}

std::string ALCVarValue::describe() const
{
    if (empty())
    {
        return "(none)";
    }
    const LLSD sd = ALCVarValues::toLLSD(*this);
    if (is<std::string>())
    {
        return fmt::format("\"{}\"", get<std::string>());
    }
    if (sd.isArray() || sd.isMap())
    {
        return LlsdToJson(sd);
    }
    return sd.asString();
}

namespace ALCVarValues
{
    LLSD toLLSD(const ALCVarValue& value)
    {
        return std::visit(
            [](const auto& v) -> LLSD {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return LLSD();
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    return LLSD::Boolean(v);
                }
                else if constexpr (std::is_same_v<T, S32>)
                {
                    return LLSD::Integer(v);
                }
                else if constexpr (std::is_same_v<T, U32>)
                {
                    // LLSD's integers are 32 bits and signed.
                    return v <= U32(INT32_MAX) ? LLSD(LLSD::Integer(v)) : LLSD(LLSD::Real(v));
                }
                else if constexpr (std::is_same_v<T, S64>)
                {
                    return v >= INT32_MIN && v <= INT32_MAX ? LLSD(LLSD::Integer(v)) : LLSD(LLSD::Real(F64(v)));
                }
                else if constexpr (std::is_same_v<T, F32> || std::is_same_v<T, F64>)
                {
                    return LLSD::Real(v);
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    return LLSD::String(v);
                }
                else if constexpr (std::is_same_v<T, LLUUID>)
                {
                    return LLSD::UUID(v);
                }
                else if constexpr (std::is_same_v<T, LLVector2>)
                {
                    return floats(v.mV, 2);
                }
                else if constexpr (std::is_same_v<T, LLVector3> || std::is_same_v<T, LLColor3>)
                {
                    return floats(v.mV, 3);
                }
                else if constexpr (std::is_same_v<T, LLColor4>)
                {
                    return floats(v.mV, 4);
                }
                else if constexpr (std::is_same_v<T, LLQuaternion>)
                {
                    return floats(v.mQ, 4);
                }
                else if constexpr (std::is_same_v<T, LLVector3d>)
                {
                    LLSD out = LLSD::emptyArray();
                    for (const F64 c : v.mdV)
                    {
                        out.append(c);
                    }
                    return out;
                }
                else if constexpr (std::is_same_v<T, LLRect>)
                {
                    LLSD out = LLSD::emptyArray();
                    out.append(v.mLeft);
                    out.append(v.mTop);
                    out.append(v.mRight);
                    out.append(v.mBottom);
                    return out;
                }
                else if constexpr (std::is_same_v<T, std::vector<LLUUID>> || std::is_same_v<T, std::vector<std::string>>)
                {
                    LLSD out = LLSD::emptyArray();
                    for (const auto& item : v)
                    {
                        out.append(item);
                    }
                    return out;
                }
                else if constexpr (std::is_same_v<T, std::vector<LLVector2>>)
                {
                    LLSD out = LLSD::emptyArray();
                    for (const auto& item : v)
                    {
                        out.append(floats(item.mV, 2));
                    }
                    return out;
                }
                else
                {
                    return v;
                }
            },
            value.storage());
    }

    std::optional<ALCVarValue> fromLLSD(ALCVarType type, const LLSD& sd)
    {
        F64 c[4];
        switch (type)
        {
            case ALCVarType::Bool:
                if (sd.isBoolean() || sd.isInteger())
                {
                    return ALCVarValue(sd.asBoolean());
                }
                return std::nullopt;
            case ALCVarType::S32:
            case ALCVarType::U32:
            case ALCVarType::S64:
                if (sd.isInteger())
                {
                    const S64 v = sd.asInteger();
                    if (type == ALCVarType::S32)
                    {
                        return ALCVarValue(S32(v));
                    }
                    if (type == ALCVarType::U32 && v >= 0)
                    {
                        return ALCVarValue(U32(v));
                    }
                    if (type == ALCVarType::S64)
                    {
                        return ALCVarValue(v);
                    }
                    return std::nullopt;
                }
                if (sd.isReal())
                {
                    const F64 v = sd.asReal();
                    if (type == ALCVarType::U32 && v >= 0.0 && v <= F64(UINT32_MAX) && v == F64(U32(v)))
                    {
                        return ALCVarValue(U32(v));
                    }
                    if (type == ALCVarType::S64 && v >= -9.2e18 && v <= 9.2e18 && v == F64(S64(v)))
                    {
                        return ALCVarValue(S64(v));
                    }
                }
                return std::nullopt;
            case ALCVarType::F32:
                if (sd.isReal() || sd.isInteger())
                {
                    return ALCVarValue(F32(sd.asReal()));
                }
                return std::nullopt;
            case ALCVarType::F64:
                if (sd.isReal() || sd.isInteger())
                {
                    return ALCVarValue(F64(sd.asReal()));
                }
                return std::nullopt;
            case ALCVarType::String:
                if (sd.isString())
                {
                    return ALCVarValue(sd.asString());
                }
                return std::nullopt;
            case ALCVarType::UUID:
                if (auto id = uuidOf(sd))
                {
                    return ALCVarValue(*id);
                }
                return std::nullopt;
            case ALCVarType::Vec2:
                if (numbers(sd, 2, c))
                {
                    return ALCVarValue(LLVector2(F32(c[0]), F32(c[1])));
                }
                return std::nullopt;
            case ALCVarType::Vec3:
                if (numbers(sd, 3, c))
                {
                    return ALCVarValue(LLVector3(F32(c[0]), F32(c[1]), F32(c[2])));
                }
                return std::nullopt;
            case ALCVarType::Color3:
                if (numbers(sd, 3, c))
                {
                    return ALCVarValue(LLColor3(F32(c[0]), F32(c[1]), F32(c[2])));
                }
                return std::nullopt;
            case ALCVarType::Color4:
                if (numbers(sd, 4, c))
                {
                    return ALCVarValue(LLColor4(F32(c[0]), F32(c[1]), F32(c[2]), F32(c[3])));
                }
                return std::nullopt;
            case ALCVarType::Quat:
                if (numbers(sd, 4, c))
                {
                    LLQuaternion q;
                    for (int i = 0; i < 4; ++i)
                    {
                        q.mQ[i] = F32(c[i]);
                    }
                    return ALCVarValue(q);
                }
                return std::nullopt;
            case ALCVarType::Vec3d:
                if (numbers(sd, 3, c))
                {
                    return ALCVarValue(LLVector3d(c[0], c[1], c[2]));
                }
                return std::nullopt;
            case ALCVarType::Rect:
                if (sd.isArray() && sd.size() == 4)
                {
                    LLRect r;
                    S32*   sides[4] = { &r.mLeft, &r.mTop, &r.mRight, &r.mBottom };
                    for (int i = 0; i < 4; ++i)
                    {
                        if (!sd[i].isInteger())
                        {
                            return std::nullopt;
                        }
                        *sides[i] = sd[i].asInteger();
                    }
                    return ALCVarValue(r);
                }
                return std::nullopt;
            case ALCVarType::UUIDList:
            {
                if (!sd.isArray())
                {
                    return std::nullopt;
                }
                std::vector<LLUUID> out;
                for (const auto& item : llsd::inArray(sd))
                {
                    auto id = uuidOf(item);
                    if (!id)
                    {
                        return std::nullopt;
                    }
                    out.push_back(*id);
                }
                return ALCVarValue(std::move(out));
            }
            case ALCVarType::StringList:
            {
                if (!sd.isArray())
                {
                    return std::nullopt;
                }
                std::vector<std::string> out;
                for (const auto& item : llsd::inArray(sd))
                {
                    if (!item.isString())
                    {
                        return std::nullopt;
                    }
                    out.push_back(item.asString());
                }
                return ALCVarValue(std::move(out));
            }
            case ALCVarType::Vec2List:
            {
                if (!sd.isArray())
                {
                    return std::nullopt;
                }
                std::vector<LLVector2> out;
                for (const auto& item : llsd::inArray(sd))
                {
                    if (!numbers(item, 2, c))
                    {
                        return std::nullopt;
                    }
                    out.emplace_back(F32(c[0]), F32(c[1]));
                }
                return ALCVarValue(std::move(out));
            }
            case ALCVarType::LLSD:
                return ALCVarValue(sd);
        }
        return std::nullopt;
    }

    std::string toJson(const ALCVarValue& value)
    {
        return LlsdToJson(toLLSD(value));
    }

    std::optional<ALCVarValue> fromJson(ALCVarType type, std::string_view json)
    {
        LLSD sd;
        if (!LlsdFromJsonString(json, sd))
        {
            return std::nullopt;
        }
        return fromLLSD(type, sd);
    }

    bool finite(F64 value)
    {
        const U64 bits = __builtin_bit_cast(U64, value);
        return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
    }

    namespace
    {
        // Into [lowest, highest]: where the two leave nothing between them,
        // lowest. An integer's bounds are rounded inward, and a value already
        // inside keeps its bits.
        template <typename T>
        T clampScalar(T value, F64 lowest, F64 highest)
        {
            // Both bounds within what the type holds, so that one past it is
            // made the type's most rather than converted out of range. An
            // S64's most as a double is 2^63, one past it: the double below.
            F64 least = F64(std::numeric_limits<T>::lowest());
            F64 most  = F64(std::numeric_limits<T>::max());
            if constexpr (std::is_integral_v<T> && std::numeric_limits<T>::digits > std::numeric_limits<F64>::digits)
            {
                most = std::nextafter(most, 0.0);
            }
            F64 low  = lowest;
            F64 high = highest;
            if constexpr (std::is_integral_v<T>)
            {
                low  = std::ceil(low);
                high = std::floor(high);
            }
            low  = std::min(std::max(low, least), most);
            high = std::min(std::max(high, least), most);
            const F64 v = F64(value);
            F64       x = v > high ? high : v;
            x           = x < low ? low : x;
            return x == v ? value : T(x);
        }

        template <typename T, size_t N>
        void clampEach(T (&components)[N], F64 lowest, F64 highest)
        {
            for (T& component : components)
            {
                component = clampScalar(component, lowest, highest);
            }
        }

        template <typename T, size_t N>
        bool finiteEach(const T (&components)[N])
        {
            for (const T component : components)
            {
                if (!finite(F64(component)))
                {
                    return false;
                }
            }
            return true;
        }
    }

    ALCVarValue clampNumber(const ALCVarValue& value, F64 lowest, F64 highest)
    {
        ALCVarValue out = value;
        switch (value.type())
        {
            case ALCVarType::S32: out = ALCVarValue(clampScalar(value.get<S32>(), lowest, highest)); break;
            case ALCVarType::U32: out = ALCVarValue(clampScalar(value.get<U32>(), lowest, highest)); break;
            case ALCVarType::S64: out = ALCVarValue(clampScalar(value.get<S64>(), lowest, highest)); break;
            case ALCVarType::F32: out = ALCVarValue(clampScalar(value.get<F32>(), lowest, highest)); break;
            case ALCVarType::F64: out = ALCVarValue(clampScalar(value.get<F64>(), lowest, highest)); break;
            case ALCVarType::Vec2: clampEach(out.get<LLVector2>().mV, lowest, highest); break;
            case ALCVarType::Vec3: clampEach(out.get<LLVector3>().mV, lowest, highest); break;
            case ALCVarType::Vec3d: clampEach(out.get<LLVector3d>().mdV, lowest, highest); break;
            case ALCVarType::Quat: clampEach(out.get<LLQuaternion>().mQ, lowest, highest); break;
            case ALCVarType::Color3: clampEach(out.get<LLColor3>().mV, lowest, highest); break;
            case ALCVarType::Color4: clampEach(out.get<LLColor4>().mV, lowest, highest); break;
            case ALCVarType::Rect:
            {
                LLRect& r = out.get<LLRect>();
                r.mLeft   = clampScalar(r.mLeft, lowest, highest);
                r.mTop    = clampScalar(r.mTop, lowest, highest);
                r.mRight  = clampScalar(r.mRight, lowest, highest);
                r.mBottom = clampScalar(r.mBottom, lowest, highest);
                break;
            }
            default:
                break;
        }
        return out;
    }

    bool finiteValue(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case ALCVarType::F32:    return finite(value.get<F32>());
            case ALCVarType::F64:    return finite(value.get<F64>());
            case ALCVarType::Vec2:   return finiteEach(value.get<LLVector2>().mV);
            case ALCVarType::Vec3:   return finiteEach(value.get<LLVector3>().mV);
            case ALCVarType::Vec3d:  return finiteEach(value.get<LLVector3d>().mdV);
            case ALCVarType::Quat:   return finiteEach(value.get<LLQuaternion>().mQ);
            case ALCVarType::Color3: return finiteEach(value.get<LLColor3>().mV);
            case ALCVarType::Color4: return finiteEach(value.get<LLColor4>().mV);
            case ALCVarType::Vec2List:
                for (const LLVector2& point : value.get<std::vector<LLVector2>>())
                {
                    if (!finiteEach(point.mV))
                    {
                        return false;
                    }
                }
                return true;
            default:
                return true;
        }
    }

    S64 asInteger(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case ALCVarType::S32: return value.get<S32>();
            case ALCVarType::U32: return value.get<U32>();
            case ALCVarType::S64: return value.get<S64>();
            default:              return 0;
        }
    }
}

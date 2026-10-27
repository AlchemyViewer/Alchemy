/**
 * @file alcvarvalue.h
 * @brief A setting's value of any type, and each type's C++ form: how it is
 *        held in a slot's words and read back.
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

#include "alcvartype.h"

#include "llrect.h"
#include "llsd.h"
#include "lluuid.h"
#include "llquaternion.h"
#include "v2math.h"
#include "v3color.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4color.h"

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// A type's C++ form, its schema type and its storage, and for the types a
// slot holds in words, how its value is laid into them.
template <typename T>
struct ALCVarTraits;

namespace ALCVarDetail
{
    template <typename T, ALCVarType TYPE>
    struct Traits
    {
        using value_type                        = T;
        static constexpr ALCVarType    type     = TYPE;
        static constexpr ALCVarStorage storage  = ALCVarTypes::storageOf(TYPE);
    };

    // A value of 16 or 24 bytes, laid into words as its bytes are, the
    // padding of a 12-byte value zero.
    template <typename T, ALCVarType TYPE, size_t WORDS>
    struct WordsTraits : Traits<T, TYPE>
    {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= WORDS * sizeof(U64));
        static constexpr size_t words = WORDS;

        static std::array<U64, WORDS> encode(const T& value)
        {
            std::array<U64, WORDS> out{};
            std::memcpy(out.data(), &value, sizeof(T));
            return out;
        }

        static T decode(const U64* words)
        {
            // Through void*: the types have constructors, but their bytes
            // are their value, as the assertion above holds.
            T value;
            std::memcpy(static_cast<void*>(&value), words, sizeof(T));
            return value;
        }
    };
}

template <>
struct ALCVarTraits<bool> : ALCVarDetail::Traits<bool, ALCVarType::Bool>
{
    static constexpr U8   encode(bool value) { return value ? 1 : 0; }
    static constexpr bool decode(U8 word) { return word != 0; }
};

template <>
struct ALCVarTraits<S32> : ALCVarDetail::Traits<S32, ALCVarType::S32>
{
    static constexpr U32 encode(S32 value) { return static_cast<U32>(value); }
    static constexpr S32 decode(U32 word) { return static_cast<S32>(word); }
};

template <>
struct ALCVarTraits<U32> : ALCVarDetail::Traits<U32, ALCVarType::U32>
{
    static constexpr U32 encode(U32 value) { return value; }
    static constexpr U32 decode(U32 word) { return word; }
};

template <>
struct ALCVarTraits<F32> : ALCVarDetail::Traits<F32, ALCVarType::F32>
{
    static constexpr U32 encode(F32 value) { return __builtin_bit_cast(U32, value); }
    static constexpr F32 decode(U32 word) { return __builtin_bit_cast(F32, word); }
};

template <>
struct ALCVarTraits<S64> : ALCVarDetail::Traits<S64, ALCVarType::S64>
{
    static constexpr U64 encode(S64 value) { return static_cast<U64>(value); }
    static constexpr S64 decode(U64 word) { return static_cast<S64>(word); }
};

template <>
struct ALCVarTraits<F64> : ALCVarDetail::Traits<F64, ALCVarType::F64>
{
    static constexpr U64 encode(F64 value) { return __builtin_bit_cast(U64, value); }
    static constexpr F64 decode(U64 word) { return __builtin_bit_cast(F64, word); }
};

// Two floats, x in the low half.
template <>
struct ALCVarTraits<LLVector2> : ALCVarDetail::Traits<LLVector2, ALCVarType::Vec2>
{
    static constexpr U64 encode(const LLVector2& value)
    {
        return U64(__builtin_bit_cast(U32, value.mV[0])) | (U64(__builtin_bit_cast(U32, value.mV[1])) << 32);
    }
    static LLVector2 decode(U64 word)
    {
        return LLVector2(__builtin_bit_cast(F32, U32(word)), __builtin_bit_cast(F32, U32(word >> 32)));
    }
};

template <>
struct ALCVarTraits<LLUUID> : ALCVarDetail::WordsTraits<LLUUID, ALCVarType::UUID, 2> {};
template <>
struct ALCVarTraits<LLVector3> : ALCVarDetail::WordsTraits<LLVector3, ALCVarType::Vec3, 2> {};
template <>
struct ALCVarTraits<LLQuaternion> : ALCVarDetail::WordsTraits<LLQuaternion, ALCVarType::Quat, 2> {};
template <>
struct ALCVarTraits<LLRect> : ALCVarDetail::WordsTraits<LLRect, ALCVarType::Rect, 2> {};
template <>
struct ALCVarTraits<LLColor3> : ALCVarDetail::WordsTraits<LLColor3, ALCVarType::Color3, 2> {};
template <>
struct ALCVarTraits<LLColor4> : ALCVarDetail::WordsTraits<LLColor4, ALCVarType::Color4, 2> {};
template <>
struct ALCVarTraits<LLVector3d> : ALCVarDetail::WordsTraits<LLVector3d, ALCVarType::Vec3d, 3> {};

template <>
struct ALCVarTraits<std::string> : ALCVarDetail::Traits<std::string, ALCVarType::String> {};
template <>
struct ALCVarTraits<std::vector<LLUUID>> : ALCVarDetail::Traits<std::vector<LLUUID>, ALCVarType::UUIDList>
{
    using element_type = LLUUID;
};
template <>
struct ALCVarTraits<std::vector<std::string>> : ALCVarDetail::Traits<std::vector<std::string>, ALCVarType::StringList>
{
    using element_type = std::string;
};
template <>
struct ALCVarTraits<std::vector<LLVector2>> : ALCVarDetail::Traits<std::vector<LLVector2>, ALCVarType::Vec2List>
{
    using element_type = LLVector2;
};
template <>
struct ALCVarTraits<LLSD> : ALCVarDetail::Traits<LLSD, ALCVarType::LLSD> {};

// A setting's value of whichever type it is, or nothing: what a layer holds,
// what a listener is given, and what a name-based read or write carries.
class ALCVarValue
{
public:
    // In the order of ALCVarType, after the empty value.
    using Storage = std::variant<std::monostate, bool, S32, U32, F32, S64, F64, std::string, LLUUID, LLVector2,
                                 LLVector3, LLVector3d, LLQuaternion, LLRect, LLColor3, LLColor4, std::vector<LLUUID>,
                                 std::vector<std::string>, std::vector<LLVector2>, LLSD>;

    template <typename T>
    static constexpr bool holds = requires { ALCVarTraits<std::remove_cvref_t<T>>::type; };

    ALCVarValue() = default;

    template <typename T>
        requires holds<T>
    ALCVarValue(T&& value) : mValue(std::in_place_type<std::remove_cvref_t<T>>, std::forward<T>(value))
    {
    }

    ALCVarValue(std::string_view text) : mValue(std::in_place_type<std::string>, text) {}
    ALCVarValue(const char* text) : mValue(std::in_place_type<std::string>, text) {}

    bool empty() const { return mValue.index() == 0; }
    ALCVarType type() const { return ALCVarType(mValue.index() - 1); }

    template <typename T>
    bool is() const
    {
        return std::holds_alternative<T>(mValue);
    }
    template <typename T>
    const T& get() const
    {
        return std::get<T>(mValue);
    }
    template <typename T>
    T& get()
    {
        return std::get<T>(mValue);
    }
    template <typename T>
    const T* getIf() const
    {
        return std::get_if<T>(&mValue);
    }

    const Storage& storage() const { return mValue; }

    // A type's value of nothing: false, 0, "", the null UUID, zero vectors,
    // the identity rotation, an empty list, undefined LLSD.
    static ALCVarValue zeroOf(ALCVarType type);

    // The same type, and the same value. Floats are the same when their bits
    // are, so that writing a value over itself is never a change, a NaN
    // included, and 0 and -0 are two values.
    friend bool operator==(const ALCVarValue& a, const ALCVarValue& b);

    // For logs, the debugger and the tests' messages.
    std::string describe() const;

private:
    Storage mValue;
};

namespace ALCVarValues
{
    // The value's LLSD form: a boolean, a number, a string, a UUID, or an
    // array of components or of elements.
    LLSD toLLSD(const ALCVarValue& value);
    // A value of the type from its LLSD form, exactly: nothing where the
    // shape does not fit.
    std::optional<ALCVarValue> fromLLSD(ALCVarType type, const LLSD& sd);
    // The LLSD form as JSON text: how a vector, a list or LLSD is stored, and
    // how the schema writes a list's or LLSD's default.
    std::string                toJson(const ALCVarValue& value);
    std::optional<ALCVarValue> fromJson(ALCVarType type, std::string_view json);
    // Neither infinite nor NaN, by its bits: what a build with fast floating
    // point cannot fold away.
    bool finite(F64 value);
}

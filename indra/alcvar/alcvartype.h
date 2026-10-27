/**
 * @file alcvartype.h
 * @brief What a setting holds, where its value lives, and the units it is
 *        measured in.
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

#include "stdtypes.h"

#include <optional>
#include <string_view>

// What a setting holds. The numbers are written into every row a settings
// file keeps, beside the value, so they never change: a new type takes the
// next number.
enum class ALCVarType : U8
{
    Bool       = 0,
    S32        = 1,
    U32        = 2,
    F32        = 3,
    S64        = 4,
    F64        = 5,
    String     = 6,
    UUID       = 7,
    Vec2       = 8,
    Vec3       = 9,
    Vec3d      = 10,
    Quat       = 11,
    Rect       = 12,
    Color3     = 13,
    Color4     = 14,
    UUIDList   = 15,
    StringList = 16,
    Vec2List   = 17,
    LLSD       = 18,
};

inline constexpr U32 ALCVAR_TYPE_COUNT = 19;

// Where a value lives, and so how it is read.
enum class ALCVarStorage : U8
{
    // bool: a byte, read with one relaxed load.
    Byte,
    // S32, U32 and F32: four bytes, one relaxed load.
    Word,
    // S64, F64 and vec2: eight bytes, one relaxed load.
    DWord,
    // The 16-byte values -- uuid, vec3, quat, rect, color3, color4 -- in two
    // words behind a sequence word of their own; a 12-byte value is padded.
    Pair,
    // vec3d's 24 bytes, in three words behind a sequence word.
    Triple,
    // string and the lists: an immutable value behind an atomic shared
    // pointer, replaced whole by a write.
    Shared,
    // LLSD, whose reference counts are not atomic: the main thread's alone.
    Boxed,
};

inline constexpr U32 ALCVAR_STORAGE_COUNT = 7;

// The units a setting's value is measured in. Like the types, the names are
// written into stored rows; units of one quantity scale into one another
// when a declaration changes its unit.
enum class ALCVarUnit : U8
{
    None,
    Metres,
    Kilometres,
    Centimetres,
    Seconds,
    Milliseconds,
    Microseconds,
    Minutes,
    Hours,
    Days,
    Degrees,
    Radians,
    Percent,
    Ratio,
    Bytes,
    Kilobytes,
    Megabytes,
    Gigabytes,
    Hertz,
    Pixels,
    Points,
    EV,
    Kelvin,
    FPS,
    Decibels,
    Count
};

namespace ALCVarTypes
{
    constexpr ALCVarStorage storageOf(ALCVarType type)
    {
        switch (type)
        {
            case ALCVarType::Bool:
                return ALCVarStorage::Byte;
            case ALCVarType::S32:
            case ALCVarType::U32:
            case ALCVarType::F32:
                return ALCVarStorage::Word;
            case ALCVarType::S64:
            case ALCVarType::F64:
            case ALCVarType::Vec2:
                return ALCVarStorage::DWord;
            case ALCVarType::UUID:
            case ALCVarType::Vec3:
            case ALCVarType::Quat:
            case ALCVarType::Rect:
            case ALCVarType::Color3:
            case ALCVarType::Color4:
                return ALCVarStorage::Pair;
            case ALCVarType::Vec3d:
                return ALCVarStorage::Triple;
            case ALCVarType::String:
            case ALCVarType::UUIDList:
            case ALCVarType::StringList:
            case ALCVarType::Vec2List:
                return ALCVarStorage::Shared;
            case ALCVarType::LLSD:
                return ALCVarStorage::Boxed;
        }
        return ALCVarStorage::Boxed;
    }

    // The type's name as a schema spells it: "bool", "F32", "uuid[]".
    constexpr std::string_view nameOf(ALCVarType type)
    {
        switch (type)
        {
            case ALCVarType::Bool:       return "bool";
            case ALCVarType::S32:        return "S32";
            case ALCVarType::U32:        return "U32";
            case ALCVarType::F32:        return "F32";
            case ALCVarType::S64:        return "S64";
            case ALCVarType::F64:        return "F64";
            case ALCVarType::String:     return "string";
            case ALCVarType::UUID:       return "uuid";
            case ALCVarType::Vec2:       return "vec2";
            case ALCVarType::Vec3:       return "vec3";
            case ALCVarType::Vec3d:      return "vec3d";
            case ALCVarType::Quat:       return "quat";
            case ALCVarType::Rect:       return "rect";
            case ALCVarType::Color3:     return "color3";
            case ALCVarType::Color4:     return "color4";
            case ALCVarType::UUIDList:   return "uuid[]";
            case ALCVarType::StringList: return "string[]";
            case ALCVarType::Vec2List:   return "vec2[]";
            case ALCVarType::LLSD:       return "llsd";
        }
        return "";
    }

    constexpr std::optional<ALCVarType> fromName(std::string_view name)
    {
        for (U32 i = 0; i < ALCVAR_TYPE_COUNT; ++i)
        {
            if (nameOf(ALCVarType(i)) == name)
            {
                return ALCVarType(i);
            }
        }
        return std::nullopt;
    }

    constexpr bool isInteger(ALCVarType type)
    {
        return type == ALCVarType::S32 || type == ALCVarType::U32 || type == ALCVarType::S64;
    }

    constexpr bool isFloat(ALCVarType type) { return type == ALCVarType::F32 || type == ALCVarType::F64; }

    // A single number: what a range, a clamp and a unit apply to.
    constexpr bool isNumber(ALCVarType type) { return isInteger(type) || isFloat(type); }

    constexpr bool isList(ALCVarType type)
    {
        return type == ALCVarType::UUIDList || type == ALCVarType::StringList || type == ALCVarType::Vec2List;
    }

    // A fixed number of floats, one a component: what a range applies to
    // component by component.
    constexpr U32 componentsOf(ALCVarType type)
    {
        switch (type)
        {
            case ALCVarType::Vec2:   return 2;
            case ALCVarType::Vec3:
            case ALCVarType::Vec3d:
            case ALCVarType::Color3: return 3;
            case ALCVarType::Quat:
            case ALCVarType::Rect:
            case ALCVarType::Color4: return 4;
            default:                 return 0;
        }
    }
}

namespace ALCVarUnits
{
    // What a unit measures. Units of one quantity scale into one another;
    // across quantities, or into or out of a unit of no quantity, they do
    // not.
    enum class Quantity : U8
    {
        None,
        Length,
        Time,
        Angle,
        Fraction,
        Data,
    };

    struct Info
    {
        std::string_view name;
        Quantity         quantity;
        // The size of one of this unit in the quantity's base unit.
        F64              factor;
    };

    constexpr Info infoOf(ALCVarUnit unit)
    {
        constexpr F64 PI = 3.14159265358979323846;
        switch (unit)
        {
            case ALCVarUnit::None:         return { "", Quantity::None, 1.0 };
            case ALCVarUnit::Metres:       return { "metres", Quantity::Length, 1.0 };
            case ALCVarUnit::Kilometres:   return { "kilometres", Quantity::Length, 1000.0 };
            case ALCVarUnit::Centimetres:  return { "centimetres", Quantity::Length, 0.01 };
            case ALCVarUnit::Seconds:      return { "seconds", Quantity::Time, 1.0 };
            case ALCVarUnit::Milliseconds: return { "milliseconds", Quantity::Time, 0.001 };
            case ALCVarUnit::Microseconds: return { "microseconds", Quantity::Time, 0.000001 };
            case ALCVarUnit::Minutes:      return { "minutes", Quantity::Time, 60.0 };
            case ALCVarUnit::Hours:        return { "hours", Quantity::Time, 3600.0 };
            case ALCVarUnit::Days:         return { "days", Quantity::Time, 86400.0 };
            case ALCVarUnit::Degrees:      return { "degrees", Quantity::Angle, PI / 180.0 };
            case ALCVarUnit::Radians:      return { "radians", Quantity::Angle, 1.0 };
            case ALCVarUnit::Percent:      return { "percent", Quantity::Fraction, 0.01 };
            case ALCVarUnit::Ratio:        return { "ratio", Quantity::Fraction, 1.0 };
            case ALCVarUnit::Bytes:        return { "bytes", Quantity::Data, 1.0 };
            case ALCVarUnit::Kilobytes:    return { "kilobytes", Quantity::Data, 1024.0 };
            case ALCVarUnit::Megabytes:    return { "megabytes", Quantity::Data, 1024.0 * 1024.0 };
            case ALCVarUnit::Gigabytes:    return { "gigabytes", Quantity::Data, 1024.0 * 1024.0 * 1024.0 };
            case ALCVarUnit::Hertz:        return { "hertz", Quantity::None, 1.0 };
            case ALCVarUnit::Pixels:       return { "pixels", Quantity::None, 1.0 };
            case ALCVarUnit::Points:       return { "points", Quantity::None, 1.0 };
            case ALCVarUnit::EV:           return { "ev", Quantity::None, 1.0 };
            case ALCVarUnit::Kelvin:       return { "kelvin", Quantity::None, 1.0 };
            case ALCVarUnit::FPS:          return { "fps", Quantity::None, 1.0 };
            case ALCVarUnit::Decibels:     return { "decibels", Quantity::None, 1.0 };
            case ALCVarUnit::Count:        break;
        }
        return { "", Quantity::None, 1.0 };
    }

    constexpr std::string_view nameOf(ALCVarUnit unit) { return infoOf(unit).name; }

    // The unit a schema or a stored row names; "" is no unit.
    constexpr std::optional<ALCVarUnit> fromName(std::string_view name)
    {
        for (U32 i = 0; i < U32(ALCVarUnit::Count); ++i)
        {
            if (infoOf(ALCVarUnit(i)).name == name)
            {
                return ALCVarUnit(i);
            }
        }
        return std::nullopt;
    }

    // What a value in `from` is multiplied by to be in `to`: 1 for one unit,
    // nothing where they measure different things.
    constexpr std::optional<F64> scale(ALCVarUnit from, ALCVarUnit to)
    {
        if (from == to)
        {
            return 1.0;
        }
        const Info a = infoOf(from);
        const Info b = infoOf(to);
        if (a.quantity == Quantity::None || a.quantity != b.quantity)
        {
            return std::nullopt;
        }
        return a.factor / b.factor;
    }
}

// Which settings file a setting's user value is kept in.
enum class ALCVarScope : U8
{
    Global,
    // The logged-in account's own file.
    Account,
    // Never kept: lost when the viewer exits.
    Runtime,
};

enum ALCVarFlag : U16
{
    // Not shown in the settings editor.
    ALCVAR_HIDDEN           = 1 << 0,
    // Takes effect at the next start.
    ALCVAR_RESTART          = 1 << 1,
    ALCVAR_DEVELOPER        = 1 << 2,
    // Never exported, logged or kept in the history.
    ALCVAR_SENSITIVE        = 1 << 3,
    // Runs code or redirects the network: never in a preset, confirmed one
    // by one when restored from a file of someone else's.
    ALCVAR_DANGEROUS        = 1 << 4,
    // A global setting an account may override with a value of its own.
    ALCVAR_ACCOUNT_OVERRIDE = 1 << 5,
};

enum class ALCVarBackup : U8
{
    Yes,
    // True of this computer, not of its user.
    Machine,
    // Never in an automatic backup; offered, unticked, in a manual one.
    Ask,
    Never,
};

enum class ALCVarPreset : U8
{
    None,
    Look,
    Scene,
    Camera,
};

// What a UUID setting names.
enum class ALCVarRefers : U8
{
    None,
    Sound,
    Texture,
    Material,
    InventoryFolder,
    InventoryItem,
    Object,
    Experience,
};

enum class ALCVarOrigin : U8
{
    Alchemy,
    Linden,
    Firestorm,
    RLVa,
};

// How the meaning of stored values changed, for a declaration's migrate.
enum class ALCVarMigrateKind : U8
{
    None,
    Map,
    Scale,
    Reset,
    Hook,
    // The step a rename begins: the old name's value through its alias's
    // codec.
    Rename,
    // The step that ends a fork: each of the other branch's steps answered
    // by its fingerprint.
    Join,
};

// What a map makes of a value it does not list.
enum class ALCVarMapElse : U8
{
    // The number as it was, as the conversions take it.
    Keep,
    // The setting's default.
    Default,
    // A value of its own.
    Value,
};

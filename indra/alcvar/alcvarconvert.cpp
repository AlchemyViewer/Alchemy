/**
 * @file alcvarconvert.cpp
 * @brief The one conversion every stored value takes into the setting it is
 *        now.
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

#include "alcvarconvert.h"

#include "llfile.h"
#include "llsdjson.h"
#include "llstring.h"

#include <fmt/format.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
    using T = ALCVarType;

    std::optional<F64> numberOf(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case T::Bool: return value.get<bool>() ? 1.0 : 0.0;
            case T::S32:  return F64(value.get<S32>());
            case T::U32:  return F64(value.get<U32>());
            case T::S64:  return F64(value.get<S64>());
            case T::F32:  return F64(value.get<F32>());
            case T::F64:  return value.get<F64>();
            default:      return std::nullopt;
        }
    }

    template <typename I>
    I clampInteger(S64 value)
    {
        return I(std::clamp<S64>(value, S64(std::numeric_limits<I>::lowest()), S64(std::numeric_limits<I>::max())));
    }

    // A float as the nearest integer, clamped: a stored 2.9999 is 3.
    std::optional<S64> rounded(F64 value)
    {
        if (!ALCVarValues::finite(value))
        {
            return std::nullopt;
        }
        if (value >= 9.2e18)
        {
            return std::numeric_limits<S64>::max();
        }
        if (value <= -9.2e18)
        {
            return std::numeric_limits<S64>::lowest();
        }
        return S64(std::llround(value));
    }

    // A number as another type of number; integers between integer types
    // exactly and clamped, floats rounded.
    std::optional<ALCVarValue> numberAs(const ALCVarValue& value, T to)
    {
        const bool integer = ALCVarTypes::isInteger(value.type()) || value.type() == T::Bool;
        const auto number  = numberOf(value);
        if (!number)
        {
            return std::nullopt;
        }
        S64 whole = 0;
        if (to == T::S32 || to == T::U32 || to == T::S64)
        {
            if (integer)
            {
                whole = value.type() == T::S64 ? value.get<S64>()
                        : value.type() == T::U32 ? S64(value.get<U32>())
                        : value.type() == T::S32 ? S64(value.get<S32>())
                                                 : S64(value.get<bool>());
            }
            else if (auto r = rounded(*number))
            {
                whole = *r;
            }
            else
            {
                return std::nullopt;
            }
        }
        switch (to)
        {
            case T::Bool:
                if (!integer && !ALCVarValues::finite(*number))
                {
                    return std::nullopt;
                }
                return ALCVarValue(*number != 0.0);
            case T::S32: return ALCVarValue(clampInteger<S32>(whole));
            case T::U32: return ALCVarValue(clampInteger<U32>(whole));
            case T::S64: return ALCVarValue(whole);
            case T::F32: return ALCVarValue(F32(*number));
            case T::F64:
                return value.type() == T::S64 ? ALCVarValue(F64(value.get<S64>())) : ALCVarValue(*number);
            default: return std::nullopt;
        }
    }

    std::string formatted(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case T::Bool:   return value.get<bool>() ? "true" : "false";
            case T::S32:    return std::to_string(value.get<S32>());
            case T::U32:    return std::to_string(value.get<U32>());
            case T::S64:    return std::to_string(value.get<S64>());
            case T::F32:    return fmt::format("{}", value.get<F32>());
            case T::F64:    return fmt::format("{}", value.get<F64>());
            case T::String: return value.get<std::string>();
            case T::UUID:   return value.get<LLUUID>().asString();
            default:        return ALCVarValues::toJson(value);
        }
    }

    std::optional<F64> parseNumber(std::string_view text)
    {
        if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())))
        {
            return std::nullopt;
        }
        const std::string copy(text);
        char*             end = nullptr;
        errno                 = 0;
        const F64 value       = std::strtod(copy.c_str(), &end);
        if (end != copy.c_str() + copy.size() || errno == ERANGE)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<S64> parseInteger(std::string_view text)
    {
        if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())))
        {
            return std::nullopt;
        }
        const std::string copy(text);
        char*             end = nullptr;
        errno                 = 0;
        const long long value = std::strtoll(copy.c_str(), &end, 10);
        if (end != copy.c_str() + copy.size() || errno == ERANGE)
        {
            return std::nullopt;
        }
        return S64(value);
    }

    std::optional<ALCVarValue> parsed(const std::string& text, T to)
    {
        switch (to)
        {
            case T::Bool:
            {
                std::string lower = text;
                LLStringUtil::toLower(lower);
                if (lower == "true")
                {
                    return ALCVarValue(true);
                }
                if (lower == "false")
                {
                    return ALCVarValue(false);
                }
                if (auto number = parseNumber(text))
                {
                    return numberAs(ALCVarValue(*number), to);
                }
                return std::nullopt;
            }
            case T::S32:
            case T::U32:
            case T::S64:
                if (auto whole = parseInteger(text))
                {
                    return numberAs(ALCVarValue(*whole), to);
                }
                if (auto number = parseNumber(text))
                {
                    return numberAs(ALCVarValue(*number), to);
                }
                return std::nullopt;
            case T::F32:
            case T::F64:
                if (auto number = parseNumber(text))
                {
                    return numberAs(ALCVarValue(*number), to);
                }
                return std::nullopt;
            case T::UUID:
            {
                LLUUID id;
                if (LLUUID::validate(text) && id.set(text, false))
                {
                    return ALCVarValue(id);
                }
                return std::nullopt;
            }
            case T::UUIDList:
            case T::StringList:
                if (auto list = ALCVarValues::fromJson(to, text))
                {
                    return list;
                }
                // A scalar is a one-element list.
                if (to == T::StringList)
                {
                    return ALCVarValue(std::vector<std::string>{ text });
                }
                if (auto id = parsed(text, T::UUID))
                {
                    return ALCVarValue(std::vector<LLUUID>{ id->get<LLUUID>() });
                }
                return std::nullopt;
            case T::LLSD:
            {
                LLSD sd;
                if (LlsdFromJsonString(text, sd))
                {
                    return ALCVarValue(sd);
                }
                return ALCVarValue(LLSD(text));
            }
            default:
                return ALCVarValues::fromJson(to, text);
        }
    }

    // An LLSD scalar as the value it is.
    std::optional<ALCVarValue> natural(const LLSD& sd)
    {
        switch (sd.type())
        {
            case LLSD::TypeBoolean: return ALCVarValue(sd.asBoolean());
            case LLSD::TypeInteger: return ALCVarValue(S32(sd.asInteger()));
            case LLSD::TypeReal:    return ALCVarValue(F64(sd.asReal()));
            case LLSD::TypeString:  return ALCVarValue(sd.asString());
            case LLSD::TypeUUID:    return ALCVarValue(sd.asUUID());
            default:                return std::nullopt;
        }
    }

    template <typename V, size_t N>
    V filled(F32 x)
    {
        V v;
        for (size_t i = 0; i < N; ++i)
        {
            v.mV[i] = x;
        }
        return v;
    }

    std::vector<std::string> split(const std::string& text, std::string_view separator)
    {
        std::vector<std::string> out;
        size_t                   start = 0;
        while (start <= text.size())
        {
            const size_t at    = text.find(separator, start);
            std::string  piece = text.substr(start, at == std::string::npos ? std::string::npos : at - start);
            if (!piece.empty())
            {
                out.push_back(std::move(piece));
            }
            if (at == std::string::npos)
            {
                break;
            }
            start = at + separator.size();
        }
        return out;
    }

    // The type a setting's value is scaled in by an alias's unit: a real for
    // an integer setting, whose value in another unit need not be whole
    // until it is the setting's.
    T scaledType(const ALCVarDesc& desc)
    {
        return ALCVarTypes::isInteger(desc.type) ? T::F64 : desc.type;
    }

    // An alias's encoding taken off: the value in `type` -- the setting's,
    // or the one it is scaled in -- a list's elements those of `shape`, its
    // unit still the alias's.
    std::optional<ALCVarValue> decodeAlias(const ALCVarValue& value, const ALCVarAlias& alias, T shape, T type)
    {
        std::optional<ALCVarValue> out;
        if (!alias.joined.empty() && value.is<std::string>() && ALCVarTypes::isList(shape))
        {
            const auto pieces = split(value.get<std::string>(), alias.joined);
            if (shape == T::StringList)
            {
                out = ALCVarValue(pieces);
            }
            else
            {
                LLSD list = LLSD::emptyArray();
                for (const auto& piece : pieces)
                {
                    list.append(piece);
                }
                out = ALCVarValues::fromLLSD(shape, list);
            }
        }
        else
        {
            out = ALCVarConvert::toType(value, type);
        }
        if (out && alias.inverted && out->is<bool>())
        {
            out = ALCVarValue(!out->get<bool>());
        }
        return out;
    }

    // What a step holds a value in while a row is brought forward: a real
    // for an integer, whose value need not be whole until the last step's.
    T heldType(T type)
    {
        return ALCVarTypes::isInteger(type) ? T::F64 : type;
    }

    // A value of one step as the next holds it, by the table: its unit
    // scaled, a unit named or dropped keeping the number, and its type
    // converted -- rounded to the step before's integer first where it
    // leaves numbers. Nothing where units of different quantities meet.
    std::optional<ALCVarValue> forward(const ALCVarValue& held, const ALCVarStep& from, const ALCVarStep& to)
    {
        std::optional<ALCVarValue> value = ALCVarConvert::toUnit(held, from.unit, to.unit);
        if (value && !ALCVarTypes::isNumber(to.type))
        {
            value = ALCVarConvert::toType(*value, from.type);
            return value ? ALCVarConvert::toType(*value, to.type) : std::nullopt;
        }
        return value ? ALCVarConvert::toType(*value, heldType(to.type)) : std::nullopt;
    }

    S32 stepNumber(std::span<const ALCVarStep> steps, size_t index)
    {
        return steps[index].number ? steps[index].number : S32(index + 1);
    }

    // A hook's value for a row, or nothing: one that throws or gives
    // nothing costs that row's value for the session, said once, and never
    // the open.
    std::optional<ALCVarValue> callHook(const ALCVarMigration& migration, std::string_view hook, const ALCVarDesc& desc,
                                        S32 step, const ALCVarStored* stored, const ALCVarStoredLookup& lookup)
    {
        std::string failure;
        try
        {
            if (auto value = migration(desc, stored, lookup))
            {
                return value;
            }
            failure = "gave nothing";
        }
        catch (const std::exception& error)
        {
            failure = std::string("threw: ") + error.what();
        }
        catch (...)
        {
            failure = "threw";
        }
        LL_WARNS("ALCVars") << desc.name << ": the migration " << hook << " of step " << step << " " << failure
                            << "; the setting keeps its default this session and its row is left as it was" << LL_ENDL;
        return std::nullopt;
    }

    // A value of one step through the migrate the next begins with: taken
    // as the step before held it, and given in the next's type and unit.
    std::optional<ALCVarValue> migrated(const ALCVarValue& held, const ALCVarStep& from, const ALCVarStep& to, S32 step,
                                        const ALCVarMigrate& migrate, const ALCVarDesc& desc,
                                        const ALCVarMigrationLookup& migrations, const ALCVarStoredLookup& lookup)
    {
        switch (migrate.kind)
        {
            case ALCVarMigrate::Kind::Reset:
                return std::nullopt;
            case ALCVarMigrate::Kind::Hook:
            {
                const ALCVarMigration* migration = migrations ? migrations(migrate.hook) : nullptr;
                const auto             own       = ALCVarConvert::toType(held, from.type);
                if (!migration)
                {
                    LL_WARNS("ALCVars") << desc.name << ": the migration " << migrate.hook << " is not registered"
                                        << LL_ENDL;
                    return std::nullopt;
                }
                if (!own)
                {
                    return std::nullopt;
                }
                const ALCVarStored stored{ *own, from.unit, from.decl };
                const auto         value = callHook(*migration, migrate.hook, desc, step, &stored, lookup);
                return value ? ALCVarConvert::toType(*value, heldType(to.type)) : std::nullopt;
            }
            case ALCVarMigrate::Kind::Map:
            {
                // Matched in the type the step before held it in, as a key
                // of that type is: an F32 holds 0.1 as its nearest float, not
                // as 0.1; a string by its text. What it becomes is a number,
                // or a string's text.
                const auto value = ALCVarConvert::toType(held, from.type);
                if (!value)
                {
                    return std::nullopt;
                }
                const bool textIn  = from.type == T::String;
                const bool textOut = to.type == T::String;
                auto       given   = [&](F64 number, std::string_view text) {
                    return ALCVarConvert::toType(textOut ? ALCVarValue(std::string(text)) : ALCVarValue(number),
                                                 heldType(to.type));
                };
                for (const ALCVarMapEntry& entry : migrate.map)
                {
                    const auto key = textIn ? std::nullopt : numberAs(ALCVarValue(entry.from), from.type);
                    if (textIn ? value->get<std::string>() == entry.fromText : key && *key == *value)
                    {
                        return given(entry.to, entry.toText);
                    }
                }
                switch (migrate.otherwise)
                {
                    case ALCVarMapElse::Default: return std::nullopt;
                    case ALCVarMapElse::Value:   return given(migrate.elseValue, migrate.elseText);
                    case ALCVarMapElse::Keep:    break;
                }
                return forward(*value, from, to);
            }
            case ALCVarMigrate::Kind::Scale:
            {
                const auto x = numberOf(held);
                return x ? ALCVarConvert::toType(ALCVarValue(*x * migrate.scale + migrate.offset), heldType(to.type))
                         : std::nullopt;
            }
            case ALCVarMigrate::Kind::Rename:
            {
                // The old name's value through its alias's codec, its unit
                // the old name's last step's, or the alias's.
                if (migrate.alias < 0 || size_t(migrate.alias) >= desc.aliases.size())
                {
                    return forward(held, from, to);
                }
                const ALCVarAlias& alias = desc.aliases[size_t(migrate.alias)];
                const auto         old   = ALCVarConvert::toType(held, from.type);
                auto value = old ? decodeAlias(*old, alias, to.type, heldType(to.type)) : std::nullopt;
                return value ? ALCVarConvert::toUnit(*value, from.unit != ALCVarUnit::None ? from.unit : alias.unit, to.unit)
                             : std::nullopt;
            }
            default:
                return forward(held, from, to);
        }
    }

    const ALCVarMigrate* migrateOf(const ALCVarDesc& desc, S32 step)
    {
        for (const ALCVarMigrate& migrate : desc.migrates)
        {
            if (migrate.step == step)
            {
                return &migrate;
            }
        }
        return nullptr;
    }

    // Where the setting's own name begins in its history: after the steps
    // of the names it had before.
    size_t ownFirst(std::span<const ALCVarStep> steps)
    {
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (steps[i].alias < 0)
            {
                return i;
            }
        }
        return 0;
    }

    // A value as a step holds it, from what was stored at it.
    std::optional<ALCVarValue> heldAt(const ALCVarValue& value, ALCVarUnit unit, const ALCVarStep& step)
    {
        auto held = ALCVarConvert::toType(value, heldType(step.type));
        return held ? ALCVarConvert::toUnit(*held, unit, step.unit) : std::nullopt;
    }

    // A source's row brought to its history's last step: through its own
    // declaration where it is declared, migrates and all, and by the
    // conversions alone where it was retired, its migrates gone with its
    // entry.
    std::optional<ALCVarStored> bringSource(const ALCVarStored& source, const ALCVarFrom& from,
                                            const ALCVarMigrationLookup& migrations, const ALCVarStoredLookup& lookup)
    {
        if (from.desc)
        {
            const auto value = ALCVarConvert::fromStored(&source, *from.desc, nullptr, migrations, lookup);
            return value ? std::optional<ALCVarStored>(ALCVarStored{ *value, from.desc->unit, from.desc->decl() }) : std::nullopt;
        }
        const std::span<const ALCVarStep> steps = from.steps;
        if (steps.empty())
        {
            return source;
        }
        size_t at = steps.size() - 1;
        for (size_t i = 0; i < steps.size(); ++i)
        {
            at = steps[i].decl == source.decl ? i : at;
        }
        at        = source.decl == 0 ? 0 : at;
        auto held = heldAt(source.value, source.unit, steps[at]);
        for (size_t i = at + 1; held && i < steps.size(); ++i)
        {
            held = forward(*held, steps[i - 1], steps[i]);
        }
        const auto value = held ? ALCVarConvert::toType(*held, steps.back().type) : std::nullopt;
        return value ? std::optional<ALCVarStored>(ALCVarStored{ *value, steps.back().unit, steps.back().decl }) : std::nullopt;
    }
}

namespace ALCVarConvert
{
    std::optional<ALCVarValue> toType(const ALCVarValue& value, ALCVarType to)
    {
        if (value.empty())
        {
            return std::nullopt;
        }
        const T from = value.type();
        if (from == to)
        {
            return value;
        }
        if (convertible(from, to) == ALCVarConvertible::No)
        {
            return std::nullopt;
        }
        if (from == T::LLSD)
        {
            const LLSD& sd = value.get<LLSD>();
            if (auto exact = ALCVarValues::fromLLSD(to, sd))
            {
                return exact;
            }
            if (auto scalar = natural(sd))
            {
                return toType(*scalar, to);
            }
            return std::nullopt;
        }
        if (to == T::LLSD)
        {
            return ALCVarValue(ALCVarValues::toLLSD(value));
        }
        if (from == T::String)
        {
            return parsed(value.get<std::string>(), to);
        }
        if (to == T::String)
        {
            return ALCVarValue(formatted(value));
        }
        if ((ALCVarTypes::isNumber(from) || from == T::Bool) && (ALCVarTypes::isNumber(to) || to == T::Bool))
        {
            return numberAs(value, to);
        }
        if (ALCVarTypes::isNumber(from))
        {
            const F64 x = *numberOf(value);
            switch (to)
            {
                case T::Vec2:   return ALCVarValue(filled<LLVector2, 2>(F32(x)));
                case T::Vec3:   return ALCVarValue(filled<LLVector3, 3>(F32(x)));
                case T::Vec3d:  return ALCVarValue(LLVector3d(x, x, x));
                case T::Color3: return ALCVarValue(filled<LLColor3, 3>(F32(x)));
                case T::Color4: return ALCVarValue(LLColor4(F32(x), F32(x), F32(x), 1.f));
                default:        return std::nullopt;
            }
        }
        switch (from)
        {
            case T::Color3:
                if (to == T::Color4)
                {
                    const LLColor3& c = value.get<LLColor3>();
                    return ALCVarValue(LLColor4(c.mV[0], c.mV[1], c.mV[2], 1.f));
                }
                break;
            case T::Color4:
                if (to == T::Color3)
                {
                    const LLColor4& c = value.get<LLColor4>();
                    return ALCVarValue(LLColor3(c.mV[0], c.mV[1], c.mV[2]));
                }
                break;
            case T::Vec3:
                if (to == T::Vec3d)
                {
                    const LLVector3& v = value.get<LLVector3>();
                    return ALCVarValue(LLVector3d(v.mV[0], v.mV[1], v.mV[2]));
                }
                break;
            case T::Vec3d:
                if (to == T::Vec3)
                {
                    const LLVector3d& v = value.get<LLVector3d>();
                    return ALCVarValue(LLVector3(F32(v.mdV[0]), F32(v.mdV[1]), F32(v.mdV[2])));
                }
                break;
            case T::UUID:
                if (to == T::UUIDList)
                {
                    return ALCVarValue(std::vector<LLUUID>{ value.get<LLUUID>() });
                }
                break;
            case T::Vec2:
                if (to == T::Vec2List)
                {
                    return ALCVarValue(std::vector<LLVector2>{ value.get<LLVector2>() });
                }
                break;
            case T::UUIDList:
                if (to == T::StringList)
                {
                    std::vector<std::string> out;
                    for (const LLUUID& id : value.get<std::vector<LLUUID>>())
                    {
                        out.push_back(id.asString());
                    }
                    return ALCVarValue(std::move(out));
                }
                break;
            case T::StringList:
                if (to == T::UUIDList)
                {
                    std::vector<LLUUID> out;
                    for (const std::string& text : value.get<std::vector<std::string>>())
                    {
                        auto id = parsed(text, T::UUID);
                        if (!id)
                        {
                            return std::nullopt;
                        }
                        out.push_back(id->get<LLUUID>());
                    }
                    return ALCVarValue(std::move(out));
                }
                break;
            default:
                break;
        }
        return std::nullopt;
    }

    std::optional<ALCVarValue> toUnit(const ALCVarValue& value, ALCVarUnit from, ALCVarUnit to)
    {
        if (from == to || from == ALCVarUnit::None || to == ALCVarUnit::None)
        {
            return value;
        }
        const auto scale = ALCVarUnits::scale(from, to);
        if (!scale)
        {
            return std::nullopt;
        }
        const F64 k = *scale;
        switch (value.type())
        {
            case T::S32:
            case T::U32:
            case T::S64:
                return numberAs(ALCVarValue(*numberOf(value) * k), value.type());
            case T::F32: return ALCVarValue(F32(F64(value.get<F32>()) * k));
            case T::F64: return ALCVarValue(value.get<F64>() * k);
            case T::Vec2:
            case T::Vec3:
            case T::Color3:
            case T::Color4:
            case T::Quat:
            case T::Vec3d:
            {
                LLSD components = ALCVarValues::toLLSD(value);
                for (size_t i = 0; i < components.size(); ++i)
                {
                    components[i] = components[i].asReal() * k;
                }
                return ALCVarValues::fromLLSD(value.type(), components);
            }
            default:
                return value;
        }
    }

    ALCVarType aliasType(const ALCVarAlias& alias, const ALCVarDesc& desc)
    {
        if (alias.stringEncoded || !alias.joined.empty())
        {
            return T::String;
        }
        return alias.unit != ALCVarUnit::None ? scaledType(desc) : desc.type;
    }

    std::optional<ALCVarValue> toAlias(const ALCVarValue& value, const ALCVarAlias& alias, const ALCVarDesc& desc)
    {
        std::optional<ALCVarValue> out = value;
        if (alias.inverted && out->is<bool>())
        {
            out = ALCVarValue(!out->get<bool>());
        }
        if (alias.unit != ALCVarUnit::None)
        {
            out = toType(*out, scaledType(desc));
            if (out)
            {
                out = toUnit(*out, desc.unit, alias.unit);
            }
        }
        if (!out)
        {
            return std::nullopt;
        }
        if (!alias.joined.empty() && ALCVarTypes::isList(out->type()))
        {
            std::string joined;
            const LLSD  list = ALCVarValues::toLLSD(*out);
            for (size_t i = 0; i < list.size(); ++i)
            {
                joined += (i ? std::string(alias.joined) : std::string()) + list[i].asString();
            }
            return ALCVarValue(std::move(joined));
        }
        if (alias.stringEncoded)
        {
            return ALCVarValue(formatted(*out));
        }
        return out;
    }

    std::optional<ALCVarValue> fromAlias(const ALCVarValue& value, const ALCVarAlias& alias, const ALCVarDesc& desc)
    {
        if (alias.unit == ALCVarUnit::None)
        {
            return decodeAlias(value, alias, desc.type, desc.type);
        }
        auto out = decodeAlias(value, alias, desc.type, scaledType(desc));
        if (out)
        {
            out = toUnit(*out, alias.unit, desc.unit);
        }
        return out ? toType(*out, desc.type) : std::nullopt;
    }

    std::optional<ALCVarValue> fit(const ALCVarValue& input, const ALCVarDesc& desc)
    {
        if (input.empty() || input.type() != desc.type || !ALCVarValues::finiteValue(input))
        {
            return std::nullopt;
        }
        ALCVarValue value = input;
        if (desc.ranged)
        {
            value = ALCVarValues::clampNumber(value, desc.rangeMin, desc.rangeMax);
        }
        if (!desc.choices.empty())
        {
            const bool text = desc.type == T::String;
            auto       is   = [&](const ALCVarChoice& choice) {
                return text ? value.get<std::string>() == choice.text : ALCVarValues::asInteger(value) == choice.number;
            };
            bool chosen = false;
            for (const ALCVarChoice& choice : desc.choices)
            {
                chosen = chosen || is(choice);
            }
            // A value no longer among the choices: the one it became, or the
            // default.
            for (size_t c = 0; c < desc.choices.size() && !chosen; ++c)
            {
                const ALCVarChoice& choice = desc.choices[c];
                if (text)
                {
                    for (const std::string_view was : choice.wasTexts)
                    {
                        chosen = chosen || was == value.get<std::string>();
                    }
                }
                else
                {
                    for (const S64 was : choice.wasNumbers)
                    {
                        chosen = chosen || was == ALCVarValues::asInteger(value);
                    }
                }
                if (chosen)
                {
                    value = text ? ALCVarValue(choice.text) : *numberAs(ALCVarValue(choice.number), desc.type);
                }
            }
            if (!chosen)
            {
                return std::nullopt;
            }
        }
        if (!desc.asset.empty())
        {
            const std::string& name = value.get<std::string>();
            if (name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos ||
                (!name.empty() && (name[0] == '.' || !LLFile::isSafeFileName(name))))
            {
                return std::nullopt;
            }
        }
        return value;
    }

    std::optional<size_t> stepOf(const ALCVarDesc& desc, U64 decl)
    {
        if (decl == 0 && !desc.steps.empty())
        {
            return ownFirst(desc.steps);
        }
        for (size_t i = 0; i < desc.steps.size(); ++i)
        {
            if (desc.steps[i].decl == decl && desc.steps[i].alias < 0)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<ALCVarValue> fromStored(const ALCVarStored* stored, const ALCVarDesc& desc, const ALCVarAlias* via,
                                          const ALCVarMigrationLookup& migrations, const ALCVarStoredLookup& lookup,
                                          Path* path)
    {
        // A declaration with no history is a step of its own.
        const ALCVarStep                  self{ desc.decl(), desc.type, desc.unit };
        const std::span<const ALCVarStep> steps = desc.steps.empty() ? std::span<const ALCVarStep>(&self, 1) : desc.steps;
        const size_t                      own   = ownFirst(steps);

        // The step the value is held at, and the value as it holds it.
        Path                       p;
        size_t                     at = steps.size() - 1;
        std::optional<ALCVarValue> held;
        if (!stored)
        {
            // Nothing of its own: what its sources give, each brought to its
            // own history's last step, at the setting's first.
            for (const ALCVarFrom& from : desc.from)
            {
                const ALCVarStored* source = lookup ? lookup(from.name) : nullptr;
                const auto          row    = source && !source->value.empty() ? bringSource(*source, from, migrations, lookup) : std::nullopt;
                const ALCVarMigration* migration = row && migrations ? migrations(from.hook) : nullptr;
                if (row && !migration)
                {
                    LL_WARNS("ALCVars") << desc.name << ": the migration " << from.hook << " is not registered" << LL_ENDL;
                }
                const auto value = migration ? callHook(*migration, from.hook, desc, stepNumber(steps, own), &*row, lookup)
                                             : std::nullopt;
                if (value)
                {
                    at     = own;
                    held   = toType(*value, heldType(steps[at].type));
                    p.known = p.moved = held.has_value();
                    break;
                }
            }
            if (!held)
            {
                return std::nullopt;
            }
        }
        else if (stored->value.empty())
        {
            return std::nullopt;
        }
        else if (via)
        {
            // An old name's row: at its step of the old name's history, the
            // first where it carries none; through the alias's codec at the
            // setting's own first step where the old name had no history.
            const S16 index  = S16(via - desc.aliases.data());
            bool      chain  = false;
            bool      placed = false;
            for (size_t i = 0; i < steps.size(); ++i)
            {
                if (steps[i].alias == index)
                {
                    const bool first = !chain;
                    chain            = true;
                    if (!placed && (steps[i].decl == stored->decl || (stored->decl == 0 && first)))
                    {
                        at     = i;
                        placed = true;
                    }
                }
            }
            if (placed)
            {
                held    = heldAt(stored->value, stored->unit, steps[at]);
                p.known = p.moved = true;
            }
            else
            {
                at = chain ? steps.size() - 1 : own;
                held = decodeAlias(stored->value, *via, steps[at].type, heldType(steps[at].type));
                if (held)
                {
                    held = toUnit(*held, stored->unit != ALCVarUnit::None ? stored->unit : via->unit, steps[at].unit);
                }
                // A step of the old name's history this build does not know
                // is read, never written over.
                p.known = p.moved = !chain;
            }
        }
        else
        {
            // The setting a reused name was before: never this one's.
            for (const U64 superseded : desc.superseded)
            {
                if (stored->decl == superseded)
                {
                    if (path)
                    {
                        *path = p;
                    }
                    return std::nullopt;
                }
            }
            if (const auto known = stepOf(desc, stored->decl))
            {
                at      = *known;
                held    = heldAt(stored->value, stored->unit, steps[at]);
                p.known = true;
                p.moved = at + 1 < steps.size();
            }
            else
            {
                // A step of a fork's other branch: the join's answer, at the
                // join's step.
                for (const ALCVarMigrate& migrate : desc.migrates)
                {
                    for (const ALCVarJoin& join : migrate.join)
                    {
                        if (join.decl != stored->decl)
                        {
                            continue;
                        }
                        for (size_t i = 0; i < steps.size(); ++i)
                        {
                            if (stepNumber(steps, i) == migrate.step && steps[i].alias < 0)
                            {
                                const ALCVarStep    from{ join.decl, join.type, join.unit };
                                const ALCVarMigrate answer{ .step      = migrate.step,
                                                            .kind      = join.kind,
                                                            .map       = join.map,
                                                            .otherwise = join.otherwise,
                                                            .elseValue = join.elseValue,
                                                            .elseText  = join.elseText,
                                                            .scale     = join.scale,
                                                            .offset    = join.offset,
                                                            .hook      = join.hook };
                                const auto          value = heldAt(stored->value, stored->unit, from);
                                at   = i;
                                held = value ? migrated(*value, from, steps[i], migrate.step, answer, desc, migrations, lookup)
                                             : std::nullopt;
                                p.known = p.moved = p.migrated = true;
                            }
                        }
                    }
                }
                if (!p.known)
                {
                    // Of a step this build does not know -- a newer viewer's,
                    // another branch's: by its type and unit alone.
                    at   = steps.size() - 1;
                    held = toType(stored->value, heldType(desc.type));
                    if (held)
                    {
                        held = toUnit(*held, stored->unit, desc.unit);
                    }
                }
            }
        }

        for (size_t i = at + 1; held && i < steps.size(); ++i)
        {
            const S32            step    = stepNumber(steps, i);
            const ALCVarMigrate* migrate = migrateOf(desc, step);
            if (migrate && migrate->kind != ALCVarMigrate::Kind::Join)
            {
                held       = migrated(*held, steps[i - 1], steps[i], step, *migrate, desc, migrations, lookup);
                p.migrated = p.migrated || migrate->kind != ALCVarMigrate::Kind::Rename;
            }
            else
            {
                held = forward(*held, steps[i - 1], steps[i]);
            }
        }
        if (path)
        {
            *path = p;
        }
        const auto value = held ? toType(*held, desc.type) : std::nullopt;
        return value ? fit(*value, desc) : std::nullopt;
    }
}

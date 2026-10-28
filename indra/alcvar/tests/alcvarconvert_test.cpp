/**
 * @file alcvarconvert_test.cpp
 * @brief The one conversion every stored value takes: each row of the table,
 *        at its edges; units; a row brought forward through its setting's
 *        history; each migrate form; choices that were; the alias codecs;
 *        and finding a setting by its name or an old one.
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

#include "alcvartestschema.h"

#include "tests/wrapllerrs.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;
    namespace world   = cvartest::world;

    using T = ALCVarType;

    const LLUUID ID("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d");

    // A value of each type that every conversion out of it has a fit for.
    ALCVarValue sample(T type)
    {
        switch (type)
        {
            case T::Bool:       return ALCVarValue(true);
            case T::S32:        return ALCVarValue(S32(2));
            case T::U32:        return ALCVarValue(U32(2));
            case T::F32:        return ALCVarValue(2.f);
            case T::S64:        return ALCVarValue(S64(2));
            case T::F64:        return ALCVarValue(2.0);
            case T::String:     return ALCVarValue(std::string("text"));
            case T::UUID:       return ALCVarValue(ID);
            case T::Vec2:       return ALCVarValue(LLVector2(1.f, 2.f));
            case T::Vec3:       return ALCVarValue(LLVector3(1.f, 2.f, 3.f));
            case T::Vec3d:      return ALCVarValue(LLVector3d(1.0, 2.0, 3.0));
            case T::Quat:       return ALCVarValue(LLQuaternion());
            case T::Rect:       return ALCVarValue(LLRect(1, 4, 3, 2));
            case T::Color3:     return ALCVarValue(LLColor3(0.5f, 0.25f, 1.f));
            case T::Color4:     return ALCVarValue(LLColor4(0.5f, 0.25f, 1.f, 0.5f));
            case T::UUIDList:   return ALCVarValue(std::vector<LLUUID>{ ID });
            case T::StringList: return ALCVarValue(std::vector<std::string>{ ID.asString() });
            case T::Vec2List:   return ALCVarValue(std::vector<LLVector2>{ LLVector2(1.f, 2.f) });
            case T::LLSD:       return ALCVarValue(LLSD(2));
        }
        return {};
    }

    // A stored value at the step its fingerprint names; 0 is no step, as the
    // XML import keeps a name the schema did not declare then.
    ALCVarStored stored(ALCVarValue value, ALCVarUnit unit = ALCVarUnit::None, U64 decl = 0)
    {
        return { std::move(value), unit, decl };
    }

    // A conversion's value, or an empty one where it gives none.
    ALCVarValue got(const std::optional<ALCVarValue>& value)
    {
        return value ? *value : ALCVarValue();
    }

    std::optional<ALCVarValue> convert(const ALCVarStored& value, const ALCVarDesc& desc, const ALCVarAlias* via = nullptr)
    {
        return ALCVarConvert::fromStored(&value, desc, via,
                                         [](std::string_view name) { return ALCVars::instance().migration(name); },
                                         [](std::string_view) -> const ALCVarStored* { return nullptr; });
    }

    TEST_CASE("alcvarconvert: every conversion the table has converts and none it lacks", "[alcvarconvert]")
    {
        for (U32 from = 0; from < ALCVAR_TYPE_COUNT; ++from)
        {
            for (U32 to = 0; to < ALCVAR_TYPE_COUNT; ++to)
            {
                INFO(ALCVarTypes::nameOf(T(from)) << " -> " << ALCVarTypes::nameOf(T(to)));
                // A string or LLSD source in the form the target writes, so
                // that there is something to parse.
                ALCVarValue value = sample(T(from));
                if (T(from) == T::String)
                {
                    value = ALCVarValue(*ALCVarConvert::toType(sample(T(to)), T::String));
                }
                else if (T(from) == T::LLSD)
                {
                    value = ALCVarValue(ALCVarValues::toLLSD(sample(T(to))));
                }
                const auto converted = ALCVarConvert::toType(value, T(to));
                const bool listed    = ALCVarConvert::convertible(T(from), T(to)) != ALCVarConvertible::No;
                CHECK(converted.has_value() == listed);
                if (converted)
                {
                    CHECK(converted->type() == T(to));
                }
            }
        }
    }

    TEST_CASE("alcvarconvert: what is exact comes back as it went", "[alcvarconvert]")
    {
        for (U32 from = 0; from < ALCVAR_TYPE_COUNT; ++from)
        {
            for (U32 to = 0; to < ALCVAR_TYPE_COUNT; ++to)
            {
                if (from == to || ALCVarConvert::convertible(T(from), T(to)) != ALCVarConvertible::Exact ||
                    ALCVarConvert::convertible(T(to), T(from)) == ALCVarConvertible::No)
                {
                    continue;
                }
                INFO(ALCVarTypes::nameOf(T(from)) << " -> " << ALCVarTypes::nameOf(T(to)) << " and back");
                const ALCVarValue value = sample(T(from));
                const auto        there = ALCVarConvert::toType(value, T(to));
                REQUIRE(there);
                const auto back = ALCVarConvert::toType(*there, T(from));
                REQUIRE(back);
                // A scalar spread over a vector's components comes back as
                // that component; the rest exactly.
                CHECK(*back == value);
            }
        }
    }

    TEST_CASE("alcvarconvert: the table at its edges", "[alcvarconvert]")
    {
        using ALCVarConvert::toType;
        // Integers clamped to the narrower type.
        CHECK(*toType(ALCVarValue(U32(3000000000u)), T::S32) == ALCVarValue(S32(INT32_MAX)));
        CHECK(*toType(ALCVarValue(S32(-5)), T::U32) == ALCVarValue(U32(0)));
        CHECK(*toType(ALCVarValue(S64(1) << 40), T::S32) == ALCVarValue(S32(INT32_MAX)));
        CHECK(*toType(ALCVarValue(S64(-1) << 40), T::U32) == ALCVarValue(U32(0)));
        // Integers to floats exactly, to 2^24 for an F32.
        CHECK(*toType(ALCVarValue(S32(16777216)), T::F32) == ALCVarValue(16777216.f));
        CHECK(*toType(ALCVarValue(S64(1) << 52), T::F64) == ALCVarValue(F64(S64(1) << 52)));
        // Floats rounded to the nearest, clamped.
        CHECK(*toType(ALCVarValue(0.9999f), T::S32) == ALCVarValue(S32(1)));
        CHECK(*toType(ALCVarValue(2.9999), T::S32) == ALCVarValue(S32(3)));
        CHECK(*toType(ALCVarValue(2.5f), T::S32) == ALCVarValue(S32(3)));
        CHECK(*toType(ALCVarValue(-2.5f), T::S32) == ALCVarValue(S32(-3)));
        CHECK(*toType(ALCVarValue(3.0e7f), T::S32) == ALCVarValue(S32(30000000)));
        CHECK(*toType(ALCVarValue(1.0e20), T::S32) == ALCVarValue(S32(INT32_MAX)));
        CHECK(*toType(ALCVarValue(-1.0), T::U32) == ALCVarValue(U32(0)));
        CHECK_FALSE(toType(ALCVarValue(std::numeric_limits<F32>::quiet_NaN()), T::S32));
        CHECK_FALSE(toType(ALCVarValue(std::numeric_limits<F64>::infinity()), T::S64));
        // Booleans and numbers.
        CHECK(*toType(ALCVarValue(true), T::F32) == ALCVarValue(1.f));
        CHECK(*toType(ALCVarValue(S32(-3)), T::Bool) == ALCVarValue(true));
        CHECK(*toType(ALCVarValue(0.0), T::Bool) == ALCVarValue(false));
        CHECK_FALSE(toType(ALCVarValue(std::numeric_limits<F32>::quiet_NaN()), T::Bool));
        // Strings formatted and parsed; one that does not parse is nothing.
        CHECK(*toType(ALCVarValue(0.1f), T::String) == ALCVarValue("0.1"));
        CHECK(*toType(ALCVarValue("42"), T::U32) == ALCVarValue(U32(42)));
        CHECK(*toType(ALCVarValue("2.7"), T::S32) == ALCVarValue(S32(3)));
        CHECK(*toType(ALCVarValue("TRUE"), T::Bool) == ALCVarValue(true));
        CHECK_FALSE(toType(ALCVarValue("12abc"), T::S32));
        CHECK_FALSE(toType(ALCVarValue(" 12"), T::S32));
        CHECK_FALSE(toType(ALCVarValue(""), T::F32));
        CHECK_FALSE(toType(ALCVarValue("1e999"), T::F64));
        CHECK_FALSE(toType(ALCVarValue("not-a-uuid"), T::UUID));
        CHECK(*toType(ALCVarValue(ID.asString()), T::UUID) == ALCVarValue(ID));
        CHECK(*toType(ALCVarValue("[1, 2, 3]"), T::Vec3) == ALCVarValue(LLVector3(1.f, 2.f, 3.f)));
        CHECK(*toType(ALCVarValue("one"), T::StringList) == ALCVarValue(std::vector<std::string>{ "one" }));
        // A scalar in every component, alpha 1; colours gain and lose alpha.
        CHECK(*toType(ALCVarValue(0.5f), T::Color4) == ALCVarValue(LLColor4(0.5f, 0.5f, 0.5f, 1.f)));
        CHECK(*toType(ALCVarValue(S32(2)), T::Vec3d) == ALCVarValue(LLVector3d(2.0, 2.0, 2.0)));
        CHECK(*toType(ALCVarValue(LLColor3(0.1f, 0.2f, 0.3f)), T::Color4) ==
              ALCVarValue(LLColor4(0.1f, 0.2f, 0.3f, 1.f)));
        CHECK(*toType(ALCVarValue(LLColor4(0.1f, 0.2f, 0.3f, 0.4f)), T::Color3) ==
              ALCVarValue(LLColor3(0.1f, 0.2f, 0.3f)));
        CHECK(*toType(ALCVarValue(LLVector3d(1.0e9 + 0.25, 0.0, 0.0)), T::Vec3) ==
              ALCVarValue(LLVector3(1.0e9f, 0.f, 0.f)));
        // LLSD through its form where the shape fits.
        LLSD four = LLSD::emptyArray();
        for (const F64 c : { 0.1, 0.2, 0.3, 0.4 })
        {
            four.append(c);
        }
        CHECK(*toType(ALCVarValue(four), T::Color4) == ALCVarValue(LLColor4(0.1f, 0.2f, 0.3f, 0.4f)));
        CHECK_FALSE(toType(ALCVarValue(four), T::Vec3));
        CHECK(*toType(ALCVarValue(LLSD(2.5)), T::S32) == ALCVarValue(S32(3)));
        // No conversion at all.
        CHECK_FALSE(toType(ALCVarValue(LLVector3(1.f, 2.f, 3.f)), T::F32));
        CHECK_FALSE(toType(ALCVarValue(LLRect(0, 1, 1, 0)), T::Vec2));
        CHECK_FALSE(toType(ALCVarValue(std::vector<LLUUID>{ ID }), T::UUID));
    }

    TEST_CASE("alcvarconvert: units of one quantity scale and others do not", "[alcvarconvert]")
    {
        using ALCVarConvert::toUnit;
        using U = ALCVarUnit;
        CHECK(*toUnit(ALCVarValue(2.f), U::Seconds, U::Milliseconds) == ALCVarValue(2000.f));
        CHECK(*toUnit(ALCVarValue(S32(1500)), U::Milliseconds, U::Seconds) == ALCVarValue(S32(2)));
        CHECK(*toUnit(ALCVarValue(1.0), U::Minutes, U::Seconds) == ALCVarValue(60.0));
        CHECK(std::abs(toUnit(ALCVarValue(180.0), U::Degrees, U::Radians)->get<F64>() - 3.14159265358979) < 1e-9);
        CHECK(*toUnit(ALCVarValue(50.f), U::Percent, U::Ratio) == ALCVarValue(0.5f));
        CHECK(*toUnit(ALCVarValue(1.5), U::Kilometres, U::Metres) == ALCVarValue(1500.0));
        CHECK(*toUnit(ALCVarValue(U32(2)), U::Megabytes, U::Kilobytes) == ALCVarValue(U32(2048)));
        CHECK(*toUnit(ALCVarValue(LLVector3(1.f, 2.f, 3.f)), U::Kilometres, U::Metres) ==
              ALCVarValue(LLVector3(1000.f, 2000.f, 3000.f)));
        CHECK_FALSE(toUnit(ALCVarValue(1.f), U::Seconds, U::Metres));
        CHECK_FALSE(toUnit(ALCVarValue(1.f), U::Pixels, U::Points));
        // A side that says nothing of its unit is the other's.
        CHECK(*toUnit(ALCVarValue(7.f), U::None, U::Seconds) == ALCVarValue(7.f));
        CHECK(*toUnit(ALCVarValue(7.f), U::Seconds, U::None) == ALCVarValue(7.f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a stored value is converted typed scaled and fitted",
                     "[alcvarconvert]")
    {
        const ALCVarDesc& distance = render::draw_distance.desc();
        // As written now.
        CHECK(*convert(stored(ALCVarValue(256.f), ALCVarUnit::Metres, distance.decl()), distance) ==
              ALCVarValue(256.f));
        // An integer then, a float now; past the range, clamped.
        CHECK(*convert(stored(ALCVarValue(S32(300))), distance) == ALCVarValue(300.f));
        CHECK(*convert(stored(ALCVarValue(5000.0)), distance) == ALCVarValue(1024.f));
        // In kilometres then.
        CHECK(*convert(stored(ALCVarValue(0.5f), ALCVarUnit::Kilometres), distance) == ALCVarValue(500.f));
        // Nothing converts it: the default.
        CHECK_FALSE(convert(stored(ALCVarValue(LLVector3(1.f, 2.f, 3.f))), distance));
        CHECK_FALSE(convert(stored(ALCVarValue(1.f), ALCVarUnit::Seconds), distance));
        CHECK_FALSE(convert(stored(ALCVarValue("far")), distance));
        // An empty row.
        CHECK_FALSE(convert(stored(ALCVarValue()), distance));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a value no longer among the choices takes the one it became",
                     "[alcvarconvert]")
    {
        const ALCVarDesc& quality = ui::toast::quality.desc();
        CHECK(*convert(stored(ALCVarValue("best")), quality) == ALCVarValue("high"));
        CHECK(*convert(stored(ALCVarValue("low")), quality) == ALCVarValue("low"));
        CHECK_FALSE(convert(stored(ALCVarValue("ultra")), quality));

        const ALCVarDesc& anisotropy = ui::toast::anisotropy.desc();
        CHECK(*convert(stored(ALCVarValue(S32(16))), anisotropy) == ALCVarValue(U32(8)));
        CHECK(*convert(stored(ALCVarValue(U32(2))), anisotropy) == ALCVarValue(U32(2)));
        CHECK_FALSE(convert(stored(ALCVarValue(U32(3))), anisotropy));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a row is brought forward through each step of its history",
                     "[alcvarconvert]")
    {
        // Seconds in a float at step 1, milliseconds in an integer at step 2.
        const ALCVarDesc& delay = ui::toast::delay.desc();
        REQUIRE(delay.steps.size() == 2);
        const U64 first = delay.steps[0].decl;
        CHECK(*convert(stored(ALCVarValue(1.5f), ALCVarUnit::Seconds, first), delay) == ALCVarValue(S32(1500)));
        // A row that says nothing of its unit is in its step's.
        CHECK(*convert(stored(ALCVarValue(1.5f), ALCVarUnit::None, first), delay) == ALCVarValue(S32(1500)));
        // The range is the declaration's as it is, applied at the end.
        CHECK(*convert(stored(ALCVarValue(20.f), ALCVarUnit::Seconds, first), delay) == ALCVarValue(S32(10000)));
        CHECK(*convert(stored(ALCVarValue(S32(2500)), ALCVarUnit::Milliseconds, delay.decl()), delay) ==
              ALCVarValue(S32(2500)));

        // Milliseconds, seconds and milliseconds again: a real throughout,
        // rounded once at the end.
        ALCVarDesc       there    = delay;
        const ALCVarStep steps[] = { { 0x1111, T::S32, ALCVarUnit::Milliseconds },
                                     { 0x2222, T::S32, ALCVarUnit::Seconds },
                                     { 0x3333, T::S32, ALCVarUnit::Milliseconds } };
        there.steps               = steps;
        CHECK(*convert(stored(ALCVarValue(S32(1500)), ALCVarUnit::Milliseconds, 0x1111), there) == ALCVarValue(S32(1500)));
        CHECK(*convert(stored(ALCVarValue(S32(2)), ALCVarUnit::Seconds, 0x2222), there) == ALCVarValue(S32(2000)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a row of a step the history does not hold is read by its type and unit",
                     "[alcvarconvert]")
    {
        // A newer viewer's, or another branch's: the table alone, no migrate.
        const ALCVarDesc& wind = world::wind_scale.desc();
        CHECK(*convert(stored(ALCVarValue(1.5f), ALCVarUnit::None, 0x1234), wind) == ALCVarValue(1.5f));
        const ALCVarDesc& delay = ui::toast::delay.desc();
        CHECK(*convert(stored(ALCVarValue(2.5f), ALCVarUnit::Seconds, 0x1234), delay) == ALCVarValue(S32(2500)));
        CHECK(*convert(stored(ALCVarValue(S32(700)), ALCVarUnit::None, 0x1234), delay) == ALCVarValue(S32(700)));
        CHECK_FALSE(convert(stored(ALCVarValue(1.f), ALCVarUnit::Metres, 0x1234), delay));
        CHECK(ALCVarConvert::stepOf(delay, delay.steps[0].decl) == size_t(0));
        CHECK_FALSE(ALCVarConvert::stepOf(delay, 0x1234));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a row of no step is its setting's first", "[alcvarconvert]")
    {
        // A value the XML import kept under a name the schema declared only
        // later: brought forward from the first step, through every migrate
        // since, as every imported value is.
        const ALCVarDesc& wind = world::wind_scale.desc();
        CHECK(*convert(stored(ALCVarValue(150.f)), wind) == ALCVarValue(1.5f));
        const ALCVarDesc& delay = ui::toast::delay.desc();
        CHECK(*convert(stored(ALCVarValue(1.5f)), delay) == ALCVarValue(S32(1500)));
        CHECK(ALCVarConvert::stepOf(delay, 0) == size_t(0));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: each migrate form reaches the rows of the steps before its own",
                     "[alcvarconvert]")
    {
        // map: levels doubled at step 2.
        const ALCVarDesc& detail = world::detail_level.desc();
        const U64         first  = detail.steps[0].decl;
        CHECK(*convert(stored(ALCVarValue(S32(3)), ALCVarUnit::None, first), detail) == ALCVarValue(S32(6)));
        // Not listed: as it was.
        CHECK(*convert(stored(ALCVarValue(S32(7)), ALCVarUnit::None, first), detail) == ALCVarValue(S32(7)));
        // Written at the migrate's step: already in the new meaning.
        CHECK(*convert(stored(ALCVarValue(S32(3)), ALCVarUnit::None, detail.decl()), detail) == ALCVarValue(S32(3)));

        // scale: a percentage at step 1.
        const ALCVarDesc& wind = world::wind_scale.desc();
        CHECK(*convert(stored(ALCVarValue(150.f), ALCVarUnit::None, wind.steps[0].decl), wind) == ALCVarValue(1.5f));
        CHECK(*convert(stored(ALCVarValue(1.5f), ALCVarUnit::None, wind.decl()), wind) == ALCVarValue(1.5f));

        // reset: the old values mean nothing now.
        const ALCVarDesc& clouds = world::cloud_style.desc();
        CHECK_FALSE(convert(stored(ALCVarValue(S32(2)), ALCVarUnit::None, clouds.steps[0].decl), clouds));
        CHECK(*convert(stored(ALCVarValue(S32(2)), ALCVarUnit::None, clouds.decl()), clouds) == ALCVarValue(S32(2)));

        // fn: the hook given the row of the step before; one of its own
        // step taken as it is.
        ALCVars& cvars = store();
        cvars.registerMigration("fog_from_old", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return own ? std::optional<ALCVarValue>(ALCVarValue(own->value.get<F32>() * 2.f)) : std::nullopt;
        });
        const ALCVarDesc& fog = world::fog_density.desc();
        CHECK(got(convert(stored(ALCVarValue(0.25f), ALCVarUnit::None, fog.steps[0].decl), fog)) == ALCVarValue(0.5f));
        CHECK(got(convert(stored(ALCVarValue(0.25f), ALCVarUnit::None, fog.decl()), fog)) == ALCVarValue(0.25f));

        // from: two old settings merged into one, read by their names.
        cvars.registerMigration("sky_tint_from_two",
                                [](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup& lookup)
                                    -> std::optional<ALCVarValue> {
                                    const ALCVarStored* red  = lookup("world.sky_tint_red");
                                    const ALCVarStored* blue = lookup("world.sky_tint_blue");
                                    if (!red || !blue)
                                    {
                                        return std::nullopt;
                                    }
                                    return ALCVarValue(LLColor3(red->value.get<F32>(), 0.f, blue->value.get<F32>()));
                                });
        const ALCVarDesc&  tint = world::sky_tint.desc();
        const ALCVarStored red  = stored(ALCVarValue(0.25f));
        const ALCVarStored blue = stored(ALCVarValue(0.75f));
        const auto         merged =
            ALCVarConvert::fromStored(nullptr, tint, nullptr, [&](std::string_view name) { return cvars.migration(name); },
                                      [&](std::string_view name) -> const ALCVarStored* {
                                          return name == "world.sky_tint_red" ? &red : name == "world.sky_tint_blue" ? &blue : nullptr;
                                      });
        CHECK(got(merged) == ALCVarValue(LLColor3(0.25f, 0.f, 0.75f)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: an old name's row comes through the old name's later steps once",
                     "[alcvarconvert]")
    {
        // ui.toast.duration's seconds doubled at its step 2, then renamed
        // ui.toast.lifetime at step 3.
        const ALCVarDesc& lifetime = ui::toast::lifetime.desc();
        REQUIRE(lifetime.steps.size() == 3);
        CHECK(lifetime.steps[0].alias == 0);
        CHECK(lifetime.steps[2].alias == -1);
        const ALCVarAlias* old = &lifetime.aliases[0];
        CHECK(got(convert(stored(ALCVarValue(1.5f), ALCVarUnit::Seconds, lifetime.steps[0].decl), lifetime, old)) ==
              ALCVarValue(3.f));
        // Through the doubling already: not again.
        CHECK(got(convert(stored(ALCVarValue(3.f), ALCVarUnit::Seconds, lifetime.steps[1].decl), lifetime, old)) ==
              ALCVarValue(3.f));
        // Imported under the old name: its first step.
        CHECK(got(convert(stored(ALCVarValue(1.5f)), lifetime, old)) == ALCVarValue(3.f));
        CHECK(got(convert(stored(ALCVarValue(5.f), ALCVarUnit::Seconds, lifetime.decl()), lifetime)) == ALCVarValue(5.f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a reused name's earlier rows take the default", "[alcvarconvert]")
    {
        const ALCVarDesc& style = ui::toast::style.desc();
        REQUIRE(style.superseded.size() == 1);
        CHECK_FALSE(convert(stored(ALCVarValue(S32(2)), ALCVarUnit::None, style.superseded[0]), style));
        CHECK(got(convert(stored(ALCVarValue("rounded")), style)) == ALCVarValue("rounded"));
        CHECK(got(convert(stored(ALCVarValue("rounded"), ALCVarUnit::None, style.decl()), style)) == ALCVarValue("rounded"));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a row of a fork's other branch takes the join's answer",
                     "[alcvarconvert]")
    {
        // Two branches each made ui.toast.corner a step 2: this one swapped
        // 0 and 1, the other scaled by ten, which the join scales back.
        const ALCVarDesc& corner = ui::toast::corner.desc();
        REQUIRE(corner.steps.size() == 3);
        REQUIRE(corner.migrates.size() == 2);
        REQUIRE(corner.migrates[1].join.size() == 1);
        const U64 other = corner.migrates[1].join[0].decl;
        CHECK(other != corner.steps[1].decl);
        CHECK(got(convert(stored(ALCVarValue(S32(30)), ALCVarUnit::None, other), corner)) == ALCVarValue(S32(3)));
        // From before the fork: this branch's steps.
        CHECK(got(convert(stored(ALCVarValue(S32(0)), ALCVarUnit::None, corner.steps[0].decl), corner)) == ALCVarValue(S32(1)));
        CHECK(got(convert(stored(ALCVarValue(S32(5)), ALCVarUnit::None, corner.steps[1].decl), corner)) == ALCVarValue(S32(5)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a value a map does not list takes its else", "[alcvarconvert]")
    {
        static const ALCVarMapEntry MAP[]   = { { .from = 1.0, .to = 2.0 } };
        const ALCVarStep            steps[] = { { 0x1111, T::S32, ALCVarUnit::None }, { 0x2222, T::S32, ALCVarUnit::None } };
        ALCVarMigrate               migrate{ .step = 2, .kind = ALCVarMigrate::Kind::Map, .map = MAP };
        ALCVarDesc                  level = world::detail_level.desc();
        level.ranged                      = false;
        level.steps                       = steps;
        level.migrates                    = std::span<const ALCVarMigrate>(&migrate, 1);
        CHECK(got(convert(stored(ALCVarValue(S32(1)), ALCVarUnit::None, 0x1111), level)) == ALCVarValue(S32(2)));
        CHECK(got(convert(stored(ALCVarValue(S32(7)), ALCVarUnit::None, 0x1111), level)) == ALCVarValue(S32(7)));
        migrate.otherwise = ALCVarMapElse::Default;
        CHECK_FALSE(convert(stored(ALCVarValue(S32(7)), ALCVarUnit::None, 0x1111), level));
        migrate.otherwise = ALCVarMapElse::Value;
        migrate.elseValue = 3.0;
        CHECK(got(convert(stored(ALCVarValue(S32(7)), ALCVarUnit::None, 0x1111), level)) == ALCVarValue(S32(3)));
        CHECK(got(convert(stored(ALCVarValue(S32(1)), ALCVarUnit::None, 0x1111), level)) == ALCVarValue(S32(2)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a map takes strings as the step before held them", "[alcvarconvert]")
    {
        // loud became bright at step 2, and quiet plain, which is still a
        // choice of its own.
        const ALCVarDesc& tone  = ui::toast::tone.desc();
        const U64         first = tone.steps[0].decl;
        CHECK(got(convert(stored(ALCVarValue("loud"), ALCVarUnit::None, first), tone)) == ALCVarValue("bright"));
        CHECK(got(convert(stored(ALCVarValue("quiet"), ALCVarUnit::None, first), tone)) == ALCVarValue("plain"));
        CHECK(got(convert(stored(ALCVarValue("plain"), ALCVarUnit::None, first), tone)) == ALCVarValue("plain"));
        // Of the migrate's own step: as it is.
        CHECK(got(convert(stored(ALCVarValue("quiet"), ALCVarUnit::None, tone.decl()), tone)) == ALCVarValue("quiet"));
        // A string's else of its own.
        ALCVarDesc    other   = tone;
        ALCVarMigrate migrate = tone.migrates[0];
        migrate.otherwise     = ALCVarMapElse::Value;
        migrate.elseText      = "quiet";
        other.migrates        = std::span<const ALCVarMigrate>(&migrate, 1);
        CHECK(got(convert(stored(ALCVarValue("plain"), ALCVarUnit::None, first), other)) == ALCVarValue("quiet"));

        // Strings that became numbers, and one the map does not list.
        const ALCVarDesc& level = ui::toast::level.desc();
        CHECK(got(convert(stored(ALCVarValue("high"), ALCVarUnit::None, level.steps[0].decl), level)) == ALCVarValue(S32(1)));
        CHECK(got(convert(stored(ALCVarValue("low"), ALCVarUnit::None, level.steps[0].decl), level)) == ALCVarValue(S32(0)));
        CHECK_FALSE(convert(stored(ALCVarValue("odd"), ALCVarUnit::None, level.steps[0].decl), level));
        CHECK(got(convert(stored(ALCVarValue(S32(1)), ALCVarUnit::None, level.decl()), level)) == ALCVarValue(S32(1)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a declared source's row comes through its own migrates",
                     "[alcvarconvert]")
    {
        ALCVars& cvars = store();
        cvars.registerMigration("gust_from_wind", [](const ALCVarDesc&, const ALCVarStored* source, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(source->value.get<F32>() * 2.f));
        });
        // world.wind_scale was a percentage at step 1, and a scale from step
        // 2: the hook is given the scale, whatever step the row is of.
        const ALCVarDesc& wind = world::wind_scale.desc();
        const ALCVarDesc& gust = world::gust_scale.desc();
        REQUIRE(gust.from.size() == 1);
        CHECK(gust.from[0].desc == &wind);
        const ALCVarMigrationLookup hooks = [&](std::string_view name) { return cvars.migration(name); };
        auto from = [&](const ALCVarStored& source) {
            return got(ALCVarConvert::fromStored(nullptr, gust, nullptr, hooks, [&](std::string_view name) -> const ALCVarStored* {
                return name == "world.wind_scale" ? &source : nullptr;
            }));
        };
        CHECK(from(stored(ALCVarValue(150.f), ALCVarUnit::None, wind.steps[0].decl)) == ALCVarValue(3.f));
        CHECK(from(stored(ALCVarValue(1.5f), ALCVarUnit::None, wind.decl())) == ALCVarValue(3.f));
        // As the XML import gives it: at its first step.
        CHECK(from(stored(ALCVarValue(150.f))) == ALCVarValue(3.f));
        // A retired source has only the lock's steps, which the table takes.
        REQUIRE(world::sun_azimuth.desc().from.size() == 1);
        CHECK(world::sun_azimuth.desc().from[0].desc == nullptr);
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a was of an earlier step is reached through a later one",
                     "[alcvarconvert]")
    {
        // loud became busy at step 2, and busy hectic at step 3.
        const ALCVarDesc& mood = ui::toast::mood.desc();
        CHECK(got(convert(stored(ALCVarValue("loud"), ALCVarUnit::None, mood.steps[0].decl), mood)) == ALCVarValue("hectic"));
        CHECK(got(convert(stored(ALCVarValue("busy"), ALCVarUnit::None, mood.steps[1].decl), mood)) == ALCVarValue("hectic"));
        CHECK(got(convert(stored(ALCVarValue("calm"), ALCVarUnit::None, mood.steps[0].decl), mood)) == ALCVarValue("calm"));
        CHECK_FALSE(convert(stored(ALCVarValue("dull"), ALCVarUnit::None, mood.steps[0].decl), mood));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a hook that throws or gives nothing costs only its value",
                     "[alcvarconvert]")
    {
        ALCVars&          cvars = store();
        const ALCVarDesc& fog   = world::fog_density.desc();
        const ALCVarStored row  = stored(ALCVarValue(0.25f), ALCVarUnit::None, fog.steps[0].decl);
        cvars.registerMigration("fog_from_old", [](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup&)
                                                    -> std::optional<ALCVarValue> { throw std::runtime_error("bad row"); });
        {
            CaptureLog log(LLError::LEVEL_WARN);
            CHECK_FALSE(convert(row, fog));
            const std::string said = log.messageWith("fog_from_old", false);
            CHECK(said.find("world.fog_density") != std::string::npos);
            CHECK(said.find("step 2") != std::string::npos);
        }
        cvars.registerMigration("fog_from_old", [](const ALCVarDesc&, const ALCVarStored*, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>();
        });
        CHECK_FALSE(convert(row, fog));
        cvars.registerMigration("fog_from_old", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<F32>() * 2.f));
        });
        CHECK(got(convert(row, fog)) == ALCVarValue(0.5f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: two settings each take their value from one source", "[alcvarconvert]")
    {
        ALCVars& cvars = store();
        cvars.registerMigration("azimuth_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[0]));
        });
        cvars.registerMigration("elevation_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[1]));
        });
        // As the XML import keeps world.sun_angles: no step, LLSD's array.
        LLSD angles = LLSD::emptyArray();
        angles.append(90.0);
        angles.append(30.0);
        const ALCVarStored              source = stored(ALCVarValue(angles));
        const ALCVarMigrationLookup     hooks  = [&](std::string_view name) { return cvars.migration(name); };
        const ALCVarStoredLookup        lookup = [&](std::string_view name) -> const ALCVarStored* {
            return name == "world.sun_angles" ? &source : nullptr;
        };
        CHECK(got(ALCVarConvert::fromStored(nullptr, world::sun_azimuth.desc(), nullptr, hooks, lookup)) == ALCVarValue(90.f));
        CHECK(got(ALCVarConvert::fromStored(nullptr, world::sun_elevation.desc(), nullptr, hooks, lookup)) == ALCVarValue(30.f));
        // With no source: the default.
        CHECK_FALSE(ALCVarConvert::fromStored(nullptr, world::sun_azimuth.desc(), nullptr, hooks,
                                              [](std::string_view) -> const ALCVarStored* { return nullptr; }));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a migrate reaches no row of its own step or a later one",
                     "[alcvarconvert]")
    {
        // world.detail_level's two steps, and a third that made it a float.
        ALCVarDesc       detail  = world::detail_level.desc();
        const ALCVarStep steps[] = { detail.steps[0], detail.steps[1], { 0x3333, T::F32, ALCVarUnit::None } };
        detail.steps             = steps;
        detail.type              = T::F32;
        detail.ranged            = false;
        // Before the migrate: through it, then a float.
        CHECK(*convert(stored(ALCVarValue(S32(3)), ALCVarUnit::None, steps[0].decl), detail) == ALCVarValue(6.f));
        // Through it already, at its step: a float alone, not mapped again.
        CHECK(*convert(stored(ALCVarValue(S32(3)), ALCVarUnit::None, steps[1].decl), detail) == ALCVarValue(3.f));
        CHECK(*convert(stored(ALCVarValue(3.f), ALCVarUnit::None, steps[2].decl), detail) == ALCVarValue(3.f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a unit that does not scale is the migrate's to answer",
                     "[alcvarconvert]")
    {
        // Pixels in an integer at step 1, a ratio of the window in a float at
        // step 2: no scale between them, and the migrate says how the number
        // changed.
        const ALCVarDesc& width = ui::toast::width.desc();
        REQUIRE(width.steps.size() == 2);
        CHECK(*convert(stored(ALCVarValue(S32(500)), ALCVarUnit::Pixels, width.steps[0].decl), width) ==
              ALCVarValue(0.5f));
        // With no migrate to say: the default.
        ALCVarDesc bare = width;
        bare.migrates   = {};
        CHECK_FALSE(convert(stored(ALCVarValue(S32(500)), ALCVarUnit::Pixels, width.steps[0].decl), bare));
        // Nor can the table alone say, for a row of no step it knows.
        CHECK_FALSE(convert(stored(ALCVarValue(S32(500)), ALCVarUnit::Pixels, 0x1234), width));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: an integer setting's old unit is scaled as a real", "[alcvarconvert]")
    {
        // Milliseconds in an S32 now; seconds under the old name.
        ALCVarDesc  delay = ui::toast::fade_time.desc();
        ALCVarAlias old;
        old.name      = "ToastDelay";
        old.unit      = ALCVarUnit::Seconds;
        delay.type    = ALCVarType::S32;
        delay.unit    = ALCVarUnit::Milliseconds;
        delay.ranged  = false;
        delay.aliases = std::span<const ALCVarAlias>(&old, 1);
        delay.steps   = {};

        // Read under the old name, written under it, and imported from it.
        CHECK(ALCVarConvert::aliasType(old, delay) == ALCVarType::F64);
        CHECK(*ALCVarConvert::toAlias(ALCVarValue(S32(1500)), old, delay) == ALCVarValue(1.5));
        CHECK(*ALCVarConvert::fromAlias(ALCVarValue(1.5), old, delay) == ALCVarValue(S32(1500)));
        CHECK(*ALCVarConvert::fromAlias(ALCVarValue(1.5f), old, delay) == ALCVarValue(S32(1500)));
        CHECK(*convert(stored(ALCVarValue(1.5f)), delay, &old) == ALCVarValue(S32(1500)));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a float's map key matches the value as its type holds it",
                     "[alcvarconvert]")
    {
        static const ALCVarMapEntry MAP[]      = { { .from = 0.1, .to = 0.25 }, { .from = 0.3, .to = 0.5 } };
        const ALCVarStep            steps[]    = { { 0x1111, T::F32, ALCVarUnit::None }, { 0x2222, T::F32, ALCVarUnit::None } };
        const ALCVarMigrate         migrates[] = { { .step = 2, .kind = ALCVarMigrate::Kind::Map, .map = MAP } };
        ALCVarDesc                  detail     = world::detail_level.desc();
        detail.type                            = ALCVarType::F32;
        detail.ranged                          = false;
        detail.steps                           = steps;
        detail.migrates                        = migrates;
        CHECK(*convert(stored(ALCVarValue(0.1f), ALCVarUnit::None, 0x1111), detail) == ALCVarValue(0.25f));
        CHECK(*convert(stored(ALCVarValue(0.3f), ALCVarUnit::None, 0x1111), detail) == ALCVarValue(0.5f));
        CHECK(*convert(stored(ALCVarValue(0.2f), ALCVarUnit::None, 0x1111), detail) == ALCVarValue(0.2f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a stored file name Windows opens as a device takes the default",
                     "[alcvarconvert]")
    {
        const ALCVarDesc& sound = ui::toast::sound.desc();
        CHECK_FALSE(convert(stored(ALCVarValue("NUL.wav")), sound));
        CHECK_FALSE(convert(stored(ALCVarValue("com1")), sound));
        CHECK(*convert(stored(ALCVarValue("chime.wav")), sound) == ALCVarValue("chime.wav"));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a value under an old name goes through its codec",
                     "[alcvarconvert]")
    {
        const ALCVarDesc& vintage = render::vintage::enabled.desc();
        CHECK(*convert(stored(ALCVarValue(true)), vintage, &vintage.aliases[0]) == ALCVarValue(false));
        CHECK(*convert(stored(ALCVarValue(S32(0))), vintage, &vintage.aliases[0]) == ALCVarValue(true));

        const ALCVarDesc& click = ui::sounds::click.desc();
        CHECK(*convert(stored(ALCVarValue(ID.asString())), click, &click.aliases[0]) == ALCVarValue(ID));
        // Malformed: the default, and a warning where it is imported.
        CHECK_FALSE(convert(stored(ALCVarValue("5e191c7b")), click, &click.aliases[0]));

        const ALCVarDesc& blocked = ui::sounds::blocked.desc();
        const LLUUID      other("00000000-1111-2222-3333-444444444444");
        CHECK(*convert(stored(ALCVarValue(ID.asString() + ";" + other.asString() + ";")), blocked, &blocked.aliases[0]) ==
              ALCVarValue(std::vector<LLUUID>{ ID, other }));

        const ALCVarDesc& fade = ui::toast::fade_time.desc();
        CHECK(*convert(stored(ALCVarValue(1500.f)), fade, &fade.aliases[0]) == ALCVarValue(1.5f));
        // A row that says its unit is taken at its word.
        CHECK(*convert(stored(ALCVarValue(2.f), ALCVarUnit::Seconds), fade, &fade.aliases[0]) == ALCVarValue(2.f));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: a setting is found by its name or an old one", "[alcvarconvert]")
    {
        ALCVars& cvars = store();
        const ALCVarRef byName = cvars.find("render.shadows.resolution_scale");
        REQUIRE(byName);
        CHECK(byName.index() == shadows::resolution_scale.index());
        CHECK(byName.alias() == nullptr);
        const ALCVarRef byAlias = cvars.find("RenderShadowResolutionScale");
        REQUIRE(byAlias);
        CHECK(byAlias.index() == byName.index());
        CHECK(byAlias.name() == "RenderShadowResolutionScale");
        CHECK_FALSE(cvars.find("render.shadows"));
        CHECK_FALSE(cvars.find("RenderShadowResolutionscale"));
        CHECK_FALSE(cvars.find(""));
    }

    TEST_CASE_METHOD(TestStore, "alcvarconvert: reads writes and listeners through an old name are in its form",
                     "[alcvarconvert]")
    {
        ALCVars& cvars = store();

        // An inverted boolean, as getBOOL would read it.
        const ALCVarRef vintage = cvars.find("RenderDisableVintageMode");
        CHECK(vintage.type() == T::Bool);
        CHECK(vintage.get() == ALCVarValue(true));
        std::vector<std::pair<bool, bool>> told;
        auto connection = vintage.onChange([&](const ALCVarValue& value, const ALCVarValue& old) {
            told.emplace_back(value.get<bool>(), old.get<bool>());
        });
        CHECK(vintage.set(ALCVarValue(false)));
        CHECK(render::vintage::enabled());
        REQUIRE(told.size() == 1);
        CHECK(told[0] == std::pair(false, true));
        CHECK(vintage.getLLSD().asBoolean() == false);
        CHECK(vintage.setLLSD(LLSD(true)));
        CHECK_FALSE(render::vintage::enabled());

        // A UUID that was a string, as getString would read it.
        const ALCVarRef click = cvars.find("UISndClick");
        CHECK(click.type() == T::String);
        CHECK(click.get() == ALCVarValue(std::string("4c8c3c77-de8d-bde2-b9b8-32635e0fd4a6")));
        CHECK(click.set(ALCVarValue(ID.asString())));
        CHECK(ui::sounds::click() == ID);
        CHECK_FALSE(click.set(ALCVarValue("not a uuid")));
        CHECK(ui::sounds::click() == ID);

        // A list joined into a string, as getLLSD would read it.
        const ALCVarRef blocked = cvars.find("RLVaBlockedExperiences");
        const LLUUID    other("00000000-1111-2222-3333-444444444444");
        CHECK(ui::sounds::blocked.set(std::vector<LLUUID>{ ID, other }));
        CHECK(blocked.getLLSD().asString() == ID.asString() + ";" + other.asString());
        CHECK(blocked.set(ALCVarValue(other.asString())));
        CHECK(ui::sounds::blocked() == std::vector<LLUUID>{ other });

        // An old unit.
        const ALCVarRef fade = cvars.find("ToastFadeTime");
        CHECK(fade.get() == ALCVarValue(500.f));
        CHECK(fade.set(ALCVarValue(S32(2500))));
        CHECK(ui::toast::fade_time() == 2.5f);

        // Another type asked for, as getF32 of an integer would.
        const ALCVarRef font = cvars.find("ChatFontSize");
        CHECK(*font.getAs(T::F32) == ALCVarValue(1.f));
        CHECK(font.set(ALCVarValue(2.6f)));
        CHECK(cvartest::chat::font_size() == 3);
        CHECK(font.setSession(ALCVarValue(S32(0))));
        CHECK(cvartest::chat::font_size() == 0);
        CHECK(font.isUserSet());
        font.reset();
        CHECK(cvartest::chat::font_size() == 0);
    }
}

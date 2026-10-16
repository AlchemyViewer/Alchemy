/**
 * @file llsettingssky_test.cpp
 * @brief The sky's physical atmosphere fields: their defaults, validation, setters and blending.
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

#include "../llsettingssky.h"

#include "llsdutil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    class TestSky final : public LLSettingsSky
    {
    public:
        TestSky(const LLSD& data) : LLSettingsSky(data) {}

        ptr_t buildClone() override { return std::make_shared<TestSky>(getSettings()); }
    };

    LLSD default_settings()
    {
        return llsd_clone(LLSettingsSky::defaults());
    }

    LLSettingsSky::ptr_t make_sky(const LLSD& settings)
    {
        return std::make_shared<TestSky>(settings);
    }

    F32 term(const LLSD& layer, const std::string& key)
    {
        return (F32)layer[key].asReal();
    }

    void require_close(const std::string& what, F32 actual, F32 expected)
    {
        INFO(what);
        REQUIRE_THAT(actual, Catch::Matchers::WithinAbs(expected, fabsf(expected) * 1.0e-5f + 1.0e-9f));
    }

    LLSD layer(F32 width, F32 exp_term, F32 exp_scale, F32 linear, F32 constant, F32 aniso = 0.0f)
    {
        return LLSettingsSky::createDensityProfileLayer(width, exp_term, exp_scale, linear, constant, aniso);
    }
}

namespace
{
    // setMieAnisotropy changes the sky's g and the LLSD it saves.
    TEST_CASE("llsettingssky: setMieAnisotropy changes the sky's g and the LLSD it saves", "[llsettingssky]")
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        require_close("default g", sky->getMieAnisotropy(), 0.8f);

        sky->setMieAnisotropy(0.6f);
        require_close("g after the setter", sky->getMieAnisotropy(), 0.6f);
        require_close("g in the saved LLSD",
                      term(sky->getSettings()[LLSettingsSky::SETTING_MIE_CONFIG][0], LLSettingsSky::SETTING_MIE_ANISOTROPY_FACTOR), 0.6f);
    }

    // The default ozone profile is Bruneton's tent, 0 at 10 km, 1 at 25 km, 0 at 40 km, and it
    // survives validation, negative terms included.
    TEST_CASE("llsettingssky: the default ozone profile is Bruneton's tent and validates", "[llsettingssky]")
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        CHECK(sky->validate());

        LLSD ozone = sky->getAbsorptionConfigs();
        REQUIRE(ozone.size() == size_t(2));

        require_close("lower width", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        require_close("lower linear", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_LINEAR_TERM), 1.0f / 15000.0f);
        require_close("lower constant", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), -2.0f / 3.0f);
        require_close("upper linear", term(ozone[1], LLSettingsSky::SETTING_DENSITY_PROFILE_LINEAR_TERM), -1.0f / 15000.0f);
        require_close("upper constant", term(ozone[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 8.0f / 3.0f);
    }

    // Blending mixes the profiles term by term, anisotropy included.
    TEST_CASE("llsettingssky: blending mixes the profiles term by term with anisotropy", "[llsettingssky]")
    {
        LLSD from = default_settings();
        from[LLSettingsSky::SETTING_RAYLEIGH_CONFIG] = llsd::array(layer(0.0f, 1.0f, -1.0f / 8000.0f, 0.0f, 0.0f));
        from[LLSettingsSky::SETTING_MIE_CONFIG] = llsd::array(layer(0.0f, 1.0f, -1.0f / 1200.0f, 0.0f, 0.0f, 0.8f));

        LLSD to = default_settings();
        to[LLSettingsSky::SETTING_RAYLEIGH_CONFIG] = llsd::array(layer(0.0f, 2.0f, -1.0f / 4000.0f, 0.0f, 0.5f));
        to[LLSettingsSky::SETTING_MIE_CONFIG] = llsd::array(layer(0.0f, 1.0f, -1.0f / 1200.0f, 0.0f, 0.0f, 0.6f));

        LLSettingsSky::ptr_t sky = make_sky(from);
        LLSettingsBase::ptr_t end = make_sky(to);
        sky->blend(end, 0.5);

        LLSD rayleigh = sky->getRayleighConfig();
        require_close("Rayleigh exp term", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_EXP_TERM), 1.5f);
        require_close("Rayleigh exp scale", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_EXP_SCALE_FACTOR),
                      0.5f * (-1.0f / 8000.0f - 1.0f / 4000.0f));
        require_close("Rayleigh constant", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.25f);
        require_close("Mie g", sky->getMieAnisotropy(), 0.7f);
    }

    // A one-layer profile blended with a two-layer one is padded by repeating its layer, taking
    // the other profile's boundary, so at 0 it still describes the same atmosphere.
    TEST_CASE("llsettingssky: a one-layer profile blends with a two-layer one by repeating its layer", "[llsettingssky]")
    {
        LLSD from = default_settings();
        from[LLSettingsSky::SETTING_ABSORPTION_CONFIG] = llsd::array(layer(0.0f, 0.0f, 0.0f, 0.0f, 0.2f));

        LLSD to = default_settings();
        to[LLSettingsSky::SETTING_ABSORPTION_CONFIG] = llsd::array(layer(25000.0f, 0.0f, 0.0f, 0.0f, 0.4f),
                                                                   layer(0.0f, 0.0f, 0.0f, 0.0f, 0.8f));

        LLSettingsBase::ptr_t end = make_sky(to);

        LLSettingsSky::ptr_t start = make_sky(from);
        start->blend(end, 0.0);
        LLSD at_start = start->getAbsorptionConfigs();
        REQUIRE(at_start.size() == size_t(2));
        require_close("boundary at 0", term(at_start[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        require_close("below the boundary at 0", term(at_start[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.2f);
        require_close("above the boundary at 0", term(at_start[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.2f);

        LLSettingsSky::ptr_t half = make_sky(from);
        half->blend(end, 0.5);
        LLSD at_half = half->getAbsorptionConfigs();
        REQUIRE(at_half.size() == size_t(2));
        require_close("boundary at 0.5", term(at_half[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        require_close("below the boundary at 0.5", term(at_half[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.3f);
        require_close("above the boundary at 0.5", term(at_half[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.5f);
    }

    // An unknown key inside a layer is stripped and the sky still validates; a sky that failed
    // would drop out of its day cycle.
    TEST_CASE("llsettingssky: an unknown key inside a layer is stripped and the sky still validates", "[llsettingssky]")
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_RAYLEIGH_CONFIG][0]["not_a_density_term"] = 1.0;

        LLSettingsSky::ptr_t sky = make_sky(settings);
        CHECK(sky->validate());
        CHECK_FALSE(sky->getRayleighConfig().has("not_a_density_term"));
    }

    // Skies saved by earlier Density tabs hold the absorption profile as [[layer], layer].
    // Validation visits the layer after the nested array too, and the getters look through it.
    TEST_CASE("llsettingssky: an absorption profile nested by earlier Density tabs still reads", "[llsettingssky]")
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_ABSORPTION_CONFIG] = llsd::array(llsd::array(layer(25000.0f, 0.0f, 0.0f, 0.0f, 0.5f)),
                                                                         layer(0.0f, 0.0f, 0.0f, 0.0f, 20.0f));

        LLSettingsSky::ptr_t sky = make_sky(settings);
        sky->validate();

        LLSD stored = sky->getAbsorptionConfigs();
        REQUIRE(stored.size() == size_t(2));
        require_close("the layer after the nested array is clamped", term(stored[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 10.0f);

        LLSD flat = LLSettingsSky::flattenDensityProfile(stored);
        REQUIRE(flat.size() == size_t(2));
        CHECK(flat[0].isMap());
        CHECK(flat[1].isMap());

        LLSD first = sky->getAbsorptionConfig();
        REQUIRE(first.isMap());
        require_close("the first layer's constant", term(first, LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.5f);
    }

    // Setting a profile marks the sky dirty, so its derived state is recomputed.
    TEST_CASE("llsettingssky: setting a profile marks the sky dirty", "[llsettingssky]")
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        sky->update();
        CHECK_FALSE(sky->isDirty());

        sky->setRayleighConfigs(llsd::array(layer(0.0f, 1.0f, -1.0f / 4000.0f, 0.0f, 0.0f)));
        CHECK(sky->isDirty());
    }

    TEST_CASE("llsettingssky: a profile with no layers has no first layer", "[llsettingssky]")
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_RAYLEIGH_CONFIG] = LLSD::emptyArray();

        LLSettingsSky::ptr_t sky = make_sky(settings);
        CHECK(sky->getRayleighConfig().isUndefined());
    }

    // Cloud cover fades in from a sky without a cloud texture toward the next sky's cover, and out toward none,
    // so neither end of the blend jumps.
    TEST_CASE("llsettingssky: cloud cover fades in from and out to a sky without a cloud texture", "[llsettingssky]")
    {
        const LLUUID cloud_texture("2bfd3884-7e27-69b9-ba3a-3e673f680004");

        LLSD clear = default_settings();
        clear[LLSettingsSky::SETTING_CLOUD_TEXTUREID] = LLUUID::null;
        clear[LLSettingsSky::SETTING_CLOUD_SHADOW] = 0.2;

        LLSD cloudy = default_settings();
        cloudy[LLSettingsSky::SETTING_CLOUD_TEXTUREID] = cloud_texture;
        cloudy[LLSettingsSky::SETTING_CLOUD_SHADOW] = 0.6;

        LLSettingsBase::ptr_t to_cloudy = make_sky(cloudy);
        for (F64 t : { 0.25, 0.5, 1.0 })
        {
            CAPTURE(t);
            LLSettingsSky::ptr_t sky = make_sky(clear);
            sky->blend(to_cloudy, t);
            require_close("fading in", sky->getCloudShadow(), 0.6f * (F32)t);
            UNSCOPED_INFO("fading in takes the next sky's texture");
            REQUIRE(sky->getCloudNoiseTextureId() == cloud_texture);
        }

        LLSettingsBase::ptr_t to_clear = make_sky(clear);
        for (F64 t : { 0.25, 0.5, 1.0 })
        {
            CAPTURE(t);
            LLSettingsSky::ptr_t sky = make_sky(cloudy);
            sky->blend(to_clear, t);
            require_close("fading out", sky->getCloudShadow(), 0.6f * (1.f - (F32)t));
            UNSCOPED_INFO("fading out keeps its own texture");
            REQUIRE(sky->getCloudNoiseTextureId() == cloud_texture);
        }

        LLSD cloudier = cloudy;
        cloudier[LLSettingsSky::SETTING_CLOUD_SHADOW] = 1.0;
        LLSettingsBase::ptr_t to_cloudier = make_sky(cloudier);
        LLSettingsSky::ptr_t sky = make_sky(cloudy);
        sky->blend(to_cloudier, 0.25);
        require_close("between two textured skies", sky->getCloudShadow(), 0.7f);
    }

    // Flattening looks through nesting to any depth, as validation and the first-layer getters do, so the
    // anisotropy setter reaches a layer nested two arrays deep.
    TEST_CASE("llsettingssky: flattening and the anisotropy setter look through any depth of nesting", "[llsettingssky]")
    {
        LLSD settings = default_settings();
        const LLSD deep = layer(0.0f, 1.0f, -1.0f / 1200.0f, 0.0f, 0.0f, 0.8f);
        const LLSD shallow = layer(0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.3f);
        settings[LLSettingsSky::SETTING_MIE_CONFIG] = llsd::array(llsd::array(llsd::array(deep)), shallow);

        LLSD flat = LLSettingsSky::flattenDensityProfile(settings[LLSettingsSky::SETTING_MIE_CONFIG]);
        REQUIRE(flat.size() == size_t(2));
        require_close("the deep layer first", term(flat[0], LLSettingsSky::SETTING_MIE_ANISOTROPY_FACTOR), 0.8f);

        LLSettingsSky::ptr_t sky = make_sky(settings);
        require_close("g from the deep layer", sky->getMieAnisotropy(), 0.8f);
        sky->setMieAnisotropy(0.6f);
        require_close("g after the setter", sky->getMieAnisotropy(), 0.6f);
        UNSCOPED_INFO("the setter keeps both layers");
        CHECK(sky->getMieConfigs().size() == size_t(2));
    }
}

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

#include "lltut.h"

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

    void ensure_close(const std::string& msg, F32 actual, F32 expected)
    {
        tut::ensure_approximately_equals_range(msg.c_str(), actual, expected, fabsf(expected) * 1.0e-5f + 1.0e-9f);
    }

    LLSD layer(F32 width, F32 exp_term, F32 exp_scale, F32 linear, F32 constant, F32 aniso = 0.0f)
    {
        return LLSettingsSky::createDensityProfileLayer(width, exp_term, exp_scale, linear, constant, aniso);
    }
}

namespace tut
{
    struct llsettingssky_data
    {
    };
    typedef test_group<llsettingssky_data> llsettingssky_group;
    typedef llsettingssky_group::object llsettingssky_object;
    tut::llsettingssky_group tut_llsettingssky("llsettingssky");

    // setMieAnisotropy changes the sky's g and the LLSD it saves.
    template<> template<>
    void llsettingssky_object::test<1>()
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        ensure_close("default g", sky->getMieAnisotropy(), 0.8f);

        sky->setMieAnisotropy(0.6f);
        ensure_close("g after the setter", sky->getMieAnisotropy(), 0.6f);
        ensure_close("g in the saved LLSD",
                     term(sky->getSettings()[LLSettingsSky::SETTING_MIE_CONFIG][0], LLSettingsSky::SETTING_MIE_ANISOTROPY_FACTOR), 0.6f);
    }

    // The default ozone profile is Bruneton's tent, 0 at 10 km, 1 at 25 km, 0 at 40 km, and it
    // survives validation, negative terms included.
    template<> template<>
    void llsettingssky_object::test<2>()
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        ensure("the default sky validates", sky->validate());

        LLSD ozone = sky->getAbsorptionConfigs();
        ensure_equals("ozone layers", ozone.size(), size_t(2));

        ensure_close("lower width", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        ensure_close("lower linear", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_LINEAR_TERM), 1.0f / 15000.0f);
        ensure_close("lower constant", term(ozone[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), -2.0f / 3.0f);
        ensure_close("upper linear", term(ozone[1], LLSettingsSky::SETTING_DENSITY_PROFILE_LINEAR_TERM), -1.0f / 15000.0f);
        ensure_close("upper constant", term(ozone[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 8.0f / 3.0f);
    }

    // Blending mixes the profiles term by term, anisotropy included.
    template<> template<>
    void llsettingssky_object::test<3>()
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
        ensure_close("Rayleigh exp term", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_EXP_TERM), 1.5f);
        ensure_close("Rayleigh exp scale", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_EXP_SCALE_FACTOR),
                     0.5f * (-1.0f / 8000.0f - 1.0f / 4000.0f));
        ensure_close("Rayleigh constant", term(rayleigh, LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.25f);
        ensure_close("Mie g", sky->getMieAnisotropy(), 0.7f);
    }

    // A one-layer profile blended with a two-layer one is padded by repeating its layer, taking
    // the other profile's boundary, so at 0 it still describes the same atmosphere.
    template<> template<>
    void llsettingssky_object::test<4>()
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
        ensure_equals("layers at 0", at_start.size(), size_t(2));
        ensure_close("boundary at 0", term(at_start[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        ensure_close("below the boundary at 0", term(at_start[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.2f);
        ensure_close("above the boundary at 0", term(at_start[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.2f);

        LLSettingsSky::ptr_t half = make_sky(from);
        half->blend(end, 0.5);
        LLSD at_half = half->getAbsorptionConfigs();
        ensure_close("boundary at 0.5", term(at_half[0], LLSettingsSky::SETTING_DENSITY_PROFILE_WIDTH), 25000.0f);
        ensure_close("below the boundary at 0.5", term(at_half[0], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.3f);
        ensure_close("above the boundary at 0.5", term(at_half[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.5f);
    }

    // An unknown key inside a layer is stripped and the sky still validates; a sky that failed
    // would drop out of its day cycle.
    template<> template<>
    void llsettingssky_object::test<5>()
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_RAYLEIGH_CONFIG][0]["not_a_density_term"] = 1.0;

        LLSettingsSky::ptr_t sky = make_sky(settings);
        ensure("the sky validates", sky->validate());
        ensure("the unknown key is stripped", !sky->getRayleighConfig().has("not_a_density_term"));
    }

    // Skies saved by earlier Density tabs hold the absorption profile as [[layer], layer].
    // Validation visits the layer after the nested array too, and the getters look through it.
    template<> template<>
    void llsettingssky_object::test<6>()
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_ABSORPTION_CONFIG] = llsd::array(llsd::array(layer(25000.0f, 0.0f, 0.0f, 0.0f, 0.5f)),
                                                                         layer(0.0f, 0.0f, 0.0f, 0.0f, 20.0f));

        LLSettingsSky::ptr_t sky = make_sky(settings);
        sky->validate();

        LLSD stored = sky->getAbsorptionConfigs();
        ensure_close("the layer after the nested array is clamped", term(stored[1], LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 10.0f);

        LLSD flat = LLSettingsSky::flattenDensityProfile(stored);
        ensure_equals("flattened layers", flat.size(), size_t(2));
        ensure("flattened layers are maps", flat[0].isMap() && flat[1].isMap());

        LLSD first = sky->getAbsorptionConfig();
        ensure("the first layer is a map", first.isMap());
        ensure_close("the first layer's constant", term(first, LLSettingsSky::SETTING_DENSITY_PROFILE_CONSTANT_TERM), 0.5f);
    }

    // Setting a profile marks the sky dirty, so its derived state is recomputed.
    template<> template<>
    void llsettingssky_object::test<7>()
    {
        LLSettingsSky::ptr_t sky = make_sky(default_settings());
        sky->update();
        ensure("clean after update", !sky->isDirty());

        sky->setRayleighConfigs(llsd::array(layer(0.0f, 1.0f, -1.0f / 4000.0f, 0.0f, 0.0f)));
        ensure("dirty after setRayleighConfigs", sky->isDirty());
    }

    // A profile with no layers has no first layer.
    template<> template<>
    void llsettingssky_object::test<8>()
    {
        LLSD settings = default_settings();
        settings[LLSettingsSky::SETTING_RAYLEIGH_CONFIG] = LLSD::emptyArray();

        LLSettingsSky::ptr_t sky = make_sky(settings);
        ensure("no first Rayleigh layer", sky->getRayleighConfig().isUndefined());
    }
}

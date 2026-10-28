/**
 * @file alcvarschema_test.cpp
 * @brief The compiled schema: handles reading their compiled defaults with or
 *        without a store, descriptors, registration, a group's read(), and
 *        each type's value in its LLSD and JSON forms.
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

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <set>

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;

    const LLUUID CLICK("4c8c3c77-de8d-bde2-b9b8-32635e0fd4a6");

    // What every handle of the schema reads when nothing has been written.
    void checkDefaults()
    {
        CHECK(render::draw_distance() == 128.f);
        CHECK(render::quality_level() == 3);
        CHECK(render::frame_count() == 0);
        CHECK_FALSE(render::vintage::enabled());
        CHECK(shadows::detail() == 2);
        CHECK(shadows::resolution_scale() == 1.f);
        CHECK(shadows::max_maps() == 4u);
        CHECK(shadows::soft());
        CHECK(shadows::bias() == 0.001);
        CHECK(shadows::tint() == LLColor4(1.f, 0.9f, 0.8f, 1.f));
        CHECK(shadows::ambient() == LLColor3(0.1f, 0.1f, 0.2f));
        CHECK(shadows::offset() == LLVector3(0.f, 0.f, 1.f));
        CHECK(shadows::anchor() == LLVector3d(128.0, 128.0, 20.0));
        CHECK(shadows::rotation() == LLQuaternion());
        CHECK(shadows::uv_scale() == LLVector2(1.f, 2.f));
        CHECK(shadows::screen_rect() == LLRect(0, 100, 200, 0));
        CHECK(ui::font_name() == "DejaVu Sans");
        CHECK(ui::recent() == std::vector<std::string>{ "one", "two" });
        CHECK(ui::curve() == std::vector<LLVector2>{ LLVector2(0.f, 0.f), LLVector2(1.f, 1.f) });
        CHECK(ui::sounds::click() == CLICK);
        CHECK(ui::sounds::blocked().empty());
        CHECK(cvartest::chat::font_size() == 1);
        CHECK(cvartest::world::maturity() == 13);
        CHECK(cvartest::net::translate::api_key().empty());
    }

    TEST_CASE("alcvarschema: every handle reads its compiled default with no store at all", "[alcvarschema]")
    {
        REQUIRE_FALSE(ALCVars::instanceExists());
        checkDefaults();
        CHECK(ui::font_name.view() == "DejaVu Sans");
        CHECK(*ui::recent.share() == std::vector<std::string>{ "one", "two" });
    }

    TEST_CASE_METHOD(TestStore, "alcvarschema: every handle reads its compiled default from a store", "[alcvarschema]")
    {
        checkDefaults();
        const LLSD layout = ui::layout();
        CHECK(layout["pane"].asInteger() == 1);
        CHECK(layout["split"][1].asReal() == 0.75);
    }

    TEST_CASE_METHOD(TestStore, "alcvarschema: registration numbers every setting and group", "[alcvarschema]")
    {
        ALCVars& cvars = store();
        CHECK(cvars.groupCount() == U32(cvartest::schema().groups.size()));

        std::set<U32> indices;
        for (U32 g = 0; g < cvars.groupCount(); ++g)
        {
            const ALCVarGroupDesc& group = cvars.groupDesc(g);
            CHECK(group.state->index == g);
            for (const ALCVarDesc& desc : group.settings)
            {
                const U32 index = group.state->first + desc.key;
                CHECK(&cvars.desc(index) == &desc);
                CHECK(cvars.groupOf(index) == g);
                indices.insert(index);
            }
        }
        CHECK(indices.size() == cvars.settingCount());
        CHECK(*indices.rbegin() == cvars.settingCount() - 1);

        CHECK(&cvars.desc(shadows::resolution_scale.index()) == &shadows::resolution_scale.desc());
        CHECK(&cvars.groupDesc(shadows::group.index()) == &shadows::group.desc());
        CHECK(cvars.parentOf(shadows::group.index()) == render::group.index());
        CHECK(cvars.parentOf(render::group.index()) == ALCVars::NONE);
        CHECK(cvars.findGroup("render.exposure.dynamic") == cvartest::render::exposure::dynamic::group.index());
        CHECK(cvars.findGroup("render.nothing") == ALCVars::NONE);
    }

    TEST_CASE("alcvarschema: a descriptor holds what its declaration says", "[alcvarschema]")
    {
        const ALCVarDesc& scale = shadows::resolution_scale.desc();
        CHECK(scale.name == "render.shadows.resolution_scale");
        CHECK(scale.label == "Resolution scale");
        CHECK(scale.help == "Shadow map size as a multiple of the window's");
        CHECK(scale.type == ALCVarType::F32);
        CHECK(scale.unit == ALCVarUnit::Ratio);
        CHECK(scale.origin == ALCVarOrigin::Linden);
        CHECK(scale.ranged);
        CHECK(scale.rangeMin == 0.25);
        CHECK(scale.rangeMax == 4.0);
        CHECK(scale.uiStep == 0.05);
        REQUIRE(scale.aliases.size() == 1);
        CHECK(scale.aliases[0].name == "RenderShadowResolutionScale");
        // From its group.
        CHECK(scale.preset == ALCVarPreset::Scene);
        CHECK(scale.backup == ALCVarBackup::Yes);
        CHECK(scale.scope == ALCVarScope::Global);
        CHECK(scale.file.ends_with("render.toml"));
        CHECK(scale.line > 0);

        const ALCVarDesc& detail = shadows::detail.desc();
        REQUIRE(detail.choices.size() == 3);
        CHECK(detail.choices[1].number == 1);
        CHECK(detail.choices[1].label == "Sun and moon");

        const ALCVarDesc& vintage = render::vintage::enabled.desc();
        REQUIRE(vintage.aliases.size() == 1);
        CHECK(vintage.aliases[0].inverted);

        const ALCVarDesc& blocked = ui::sounds::blocked.desc();
        CHECK(blocked.refers == ALCVarRefers::Experience);
        REQUIRE(blocked.aliases.size() == 1);
        CHECK(blocked.aliases[0].stringEncoded);
        CHECK(blocked.aliases[0].joined == ";");

        const ALCVarDesc& quality = ui::toast::quality.desc();
        REQUIRE(quality.choices.size() == 2);
        REQUIRE(quality.choices[1].wasTexts.size() == 1);
        CHECK(quality.choices[1].wasTexts[0] == "best");

        CHECK(cvartest::chat::font_size.desc().scope == ALCVarScope::Account);
        CHECK(render::frame_count.desc().scope == ALCVarScope::Runtime);
        CHECK(render::frame_count.desc().backup == ALCVarBackup::Never);
        CHECK(ui::scale.desc().has(ALCVAR_ACCOUNT_OVERRIDE));
        CHECK(cvartest::net::translate::api_key.desc().has(ALCVAR_SENSITIVE));
        CHECK(cvartest::world::maturity.desc().validate == "maturity");
        // Each setting's history as the lock records it: a migrate begins a
        // step of its own.
        const ALCVarDesc& level = cvartest::world::detail_level.desc();
        REQUIRE(level.steps.size() == 2);
        CHECK(level.steps[0].type == ALCVarType::S32);
        CHECK(level.steps[0].decl != level.steps[1].decl);
        CHECK(level.decl() == level.steps[1].decl);
        REQUIRE(level.migrates.size() == 1);
        CHECK(level.migrates[0].step == 2);
        CHECK(level.migrates[0].kind == ALCVarMigrate::Kind::Map);
        CHECK(cvartest::world::fog_density.desc().migrates[0].hook == "fog_from_old");
        REQUIRE(cvartest::world::sky_tint.desc().from.size() == 2);
        CHECK(cvartest::world::sky_tint.desc().from[0].name == "world.sky_tint_red");
        CHECK(cvartest::world::sky_tint.desc().from[0].hook == "sky_tint_from_two");
        CHECK(cvartest::world::sky_tint.desc().from[0].steps.size() == 1);
        const ALCVarDesc& delay = ui::toast::delay.desc();
        REQUIRE(delay.steps.size() == 2);
        CHECK(delay.steps[0].type == ALCVarType::F32);
        CHECK(delay.steps[0].unit == ALCVarUnit::Seconds);
        CHECK(delay.steps[1].type == ALCVarType::S32);
        CHECK(delay.steps[1].unit == ALCVarUnit::Milliseconds);
        CHECK(delay.migrates.empty());
        CHECK(render::draw_distance.desc().steps.size() == 1);

        const ALCVarGroupDesc& group = shadows::group.desc();
        CHECK(group.name == "render.shadows");
        CHECK(group.label == "Shadows");
        CHECK(group.parent == "render");
        CHECK(group.header == "cvartest/render/shadows.h");
    }

    TEST_CASE("alcvarschema: settings of one group sit in arrays by size", "[alcvarschema]")
    {
        using G = shadows::Group_;
        CHECK(sizeof(G::bytes) == 1);
        CHECK(sizeof(G::words) == 3 * sizeof(U32));
        CHECK(sizeof(G::dwords) == 2 * sizeof(U64));
        CHECK(sizeof(G::pairs) == 5 * sizeof(ALCVarSeqSlot<2>));
        CHECK(sizeof(G::triples) == 1 * sizeof(ALCVarSeqSlot<3>));
        // Fixed by the type: nothing in the handle.
        CHECK(std::is_empty_v<std::remove_cvref_t<decltype(shadows::resolution_scale)>>);
        CHECK(shadows::resolution_scale.key == 1);
        CHECK(G::slot[shadows::resolution_scale.key] == 1);
    }

    TEST_CASE_METHOD(TestStore, "alcvarschema: a group's read gives its numbers and small values", "[alcvarschema]")
    {
        const shadows::Values values = shadows::read();
        CHECK(values.detail == shadows::detail());
        CHECK(values.resolution_scale == shadows::resolution_scale());
        CHECK(values.max_maps == shadows::max_maps());
        CHECK(values.soft == shadows::soft());
        CHECK(values.bias == shadows::bias());
        CHECK(values.tint == shadows::tint());
        CHECK(values.ambient == shadows::ambient());
        CHECK(values.offset == shadows::offset());
        CHECK(values.anchor == shadows::anchor());
        CHECK(values.rotation == shadows::rotation());
        CHECK(values.uv_scale == shadows::uv_scale());
        CHECK(values.screen_rect == shadows::screen_rect());
    }

    TEST_CASE_METHOD(TestStore, "alcvarschema: an untyped read is the handle's value", "[alcvarschema]")
    {
        ALCVars& cvars = store();
        CHECK(cvars.get(render::draw_distance.index()) == ALCVarValue(128.f));
        CHECK(cvars.get(shadows::tint.index()) == ALCVarValue(LLColor4(1.f, 0.9f, 0.8f, 1.f)));
        CHECK(cvars.get(ui::recent.index()) == ALCVarValue(std::vector<std::string>{ "one", "two" }));
        CHECK(cvars.get(ui::sounds::click.index()) == ALCVarValue(CLICK));
        CHECK(cvars.get(ui::layout.index()).get<LLSD>()["pane"].asInteger() == 1);
        CHECK(cvars.defaultValue(shadows::anchor.index()) == ALCVarValue(LLVector3d(128.0, 128.0, 20.0)));
        CHECK(cvars.generation(render::draw_distance.index()) == 0);
        CHECK(render::draw_distance.generation() == 0);
        CHECK(cvartest::tags::shader_input.index == 1);
        CHECK(cvartest::tags::resource_input.index == 0);
    }

    TEST_CASE("alcvarschema: every type round-trips through its JSON form", "[alcvarschema]")
    {
        const std::vector<ALCVarValue> values = {
            ALCVarValue(true),
            ALCVarValue(S32(-7)),
            ALCVarValue(U32(4000000000u)),
            ALCVarValue(1.5f),
            ALCVarValue(S64(-5)),
            ALCVarValue(0.1),
            ALCVarValue(std::string("a \"quoted\" line\n")),
            ALCVarValue(CLICK),
            ALCVarValue(LLVector2(1.f, -2.f)),
            ALCVarValue(LLVector3(1.f, 2.f, 3.f)),
            ALCVarValue(LLVector3d(1.0e9, 2.0, -3.5)),
            ALCVarValue(LLQuaternion(0.f, 0.f, 0.70710677f, 0.70710677f)),
            ALCVarValue(LLRect(1, 20, 30, 4)),
            ALCVarValue(LLColor3(0.25f, 0.5f, 1.f)),
            ALCVarValue(LLColor4(0.25f, 0.5f, 1.f, 0.125f)),
            ALCVarValue(std::vector<LLUUID>{ CLICK, LLUUID::null }),
            ALCVarValue(std::vector<std::string>{ "a", "", "c" }),
            ALCVarValue(std::vector<LLVector2>{ LLVector2(0.f, 1.f) }),
        };
        for (const ALCVarValue& value : values)
        {
            INFO(ALCVarTypes::nameOf(value.type()) << ": " << value.describe());
            const std::string json = ALCVarValues::toJson(value);
            const auto        back = ALCVarValues::fromJson(value.type(), json);
            REQUIRE(back);
            CHECK(*back == value);
        }
        CHECK_FALSE(ALCVarValues::fromJson(ALCVarType::Vec3, "[1, 2]"));
        CHECK_FALSE(ALCVarValues::fromJson(ALCVarType::UUIDList, "[\"not a uuid\"]"));
        CHECK_FALSE(ALCVarValues::fromJson(ALCVarType::Rect, "[1, 2, 3, 4.5]"));
    }

    TEST_CASE("alcvarschema: values compare by their bits", "[alcvarschema]")
    {
        CHECK(ALCVarValue(1.f) == ALCVarValue(1.f));
        CHECK_FALSE(ALCVarValue(1.f) == ALCVarValue(1.0));
        // A negative zero from bits the compiler cannot see: an optimised
        // build does not keep the sign of a zero (-fno-signed-zeros), and GCC
        // for arm64 then writes the constant -0 from the zero register, as 0.
        const volatile U32 sign = 0x80000000u;
        CHECK_FALSE(ALCVarValue(0.f) == ALCVarValue(std::bit_cast<F32>(U32(sign))));
        const F32 nan = std::numeric_limits<F32>::quiet_NaN();
        CHECK(ALCVarValue(nan) == ALCVarValue(nan));
        CHECK_FALSE(ALCVarValue(S32(1)) == ALCVarValue(U32(1)));
        CHECK(ALCVarValue("x") == ALCVarValue(std::string("x")));
        CHECK(ALCVarValue(LLSD(1)) == ALCVarValue(LLSD(1)));
    }
}

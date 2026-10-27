/**
 * @file alcvarxmlimport_test.cpp
 * @brief The settings XML an LLControlGroup saved, imported once: through old
 *        names and their codecs, defaults and the quality level's values left
 *        out, other viewers' settings kept, and never twice.
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

#include "aldbfaultvfs.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "tests/wrapllerrs.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
    namespace render  = cvartest::render;
    namespace shadows = cvartest::render::shadows;
    namespace ui      = cvartest::ui;

    const LLUUID CLICK("b4ba225c-373f-446d-9f7e-6cb7b5cf9b3d");
    const LLUUID OTHER("00000000-1111-2222-3333-444444444444");

    struct FileStore
    {
        ll_test::ALDbTempDir dir;
        TestStore            test;

        ALCVars& store() { return test.store(); }
    };

    // An entry as LLControlGroup saves one.
    LLSD control(std::string_view type, const LLSD& value, bool persist = true)
    {
        LLSD entry;
        entry["Comment"] = "imported";
        entry["Persist"] = persist ? 1 : 0;
        entry["Type"]    = std::string(type);
        entry["Value"]   = value;
        return entry;
    }

    std::string writeXml(const ll_test::ALDbTempDir& dir, const std::string& name, const LLSD& settings)
    {
        const std::string path = dir.file(name);
        llofstream        out(path.c_str(), std::ios::binary);
        LLSDSerialize::toPrettyXML(settings, out);
        return path;
    }

    std::unique_ptr<ALDbConnection> raw(const std::string& path)
    {
        int         rc = SQLITE_OK;
        std::string error;
        auto        connection = ALDbConnection::open(path, ALDbConnection::Access::ReadWrite, {}, &rc, &error);
        REQUIRE(connection);
        return connection;
    }

    LLSD globalSettings()
    {
        LLSD blocked = CLICK.asString() + ";" + OTHER.asString();
        LLSD settings;
        settings["RenderFarClip"]               = control("F32", 300.0);
        settings["RenderShadowDetail"]          = control("S32", 2);
        settings["RenderDisableVintageMode"]    = control("Boolean", false);
        settings["UISndClick"]                  = control("String", CLICK.asString());
        settings["RLVaBlockedExperiences"]      = control("String", blocked);
        settings["ToastFadeTime"]               = control("F32", 1500.0);
        settings["UIScaleFactor"]               = control("F32", 3.0);
        settings["RenderShadowResolutionScale"] = control("F32", 2.0);
        settings["SomeOtherViewersSetting"]     = control("String", "kept");
        settings["NeverSaved"]                  = control("String", "gone", false);
        settings["ChatFontSize"]                = control("S32", 3);
        LLSD tint                               = LLSD::emptyArray();
        for (const F64 c : { 0.5, 0.25, 0.0, 1.0 })
        {
            tint.append(c);
        }
        settings["render.shadows.tint"] = control("Color4", tint);
        return settings;
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a settings file is imported into a new settings file once",
                     "[alcvarxmlimport]")
    {
        const std::string xml  = writeXml(dir, "settings.xml", globalSettings());
        const std::string path = dir.file("settings.db");

        ALCVarFileOptions options;
        options.importXml = xml;
        REQUIRE(store().openGlobal(path, options));
        CHECK(render::draw_distance() == 300.f);
        CHECK(render::vintage::enabled());
        CHECK(ui::sounds::click() == CLICK);
        CHECK(ui::sounds::blocked() == std::vector<LLUUID>{ CLICK, OTHER });
        CHECK(ui::toast::fade_time() == 1.5f);
        CHECK(ui::scale() == 2.5f);
        CHECK(shadows::tint() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        CHECK(shadows::resolution_scale() == 2.f);
        // An account's setting waits for the account's own file.
        CHECK(cvartest::chat::font_size() == 1);
        store().close();

        auto file = raw(path);
        CHECK(file->queryText("SELECT value FROM meta WHERE key = 'imported_xml'") == xml);
        CHECK_FALSE(file->queryText("SELECT value FROM meta WHERE key = 'import_pending'").value_or("").empty());
        CHECK(file->queryText("SELECT value FROM settings WHERE name = 'SomeOtherViewersSetting'") == "kept");
        CHECK(file->queryInt("SELECT type FROM settings WHERE name = 'SomeOtherViewersSetting'") ==
              S64(ALCVarType::String));
        // At no step: a build that declares the name takes it as its first.
        CHECK(file->queryInt("SELECT decl FROM settings WHERE name = 'SomeOtherViewersSetting'") == 0);
        // Every value is a row, an account's setting's too, under its name
        // now; what was never saved is not.
        for (const char* name : { "render.shadows.detail", "render.shadows.resolution_scale", "chat.font_size" })
        {
            INFO(name);
            CHECK(file->queryInt(std::string("SELECT count(*) FROM settings WHERE name = '") + name + "'") == 1);
        }
        for (const char* name : { "NeverSaved", "RenderFarClip" })
        {
            INFO(name);
            CHECK(file->queryInt(std::string("SELECT count(*) FROM settings WHERE name = '") + name + "'") == 0);
        }
        CHECK(file->queryText("SELECT cast(value AS TEXT) FROM settings WHERE name = 'render.draw_distance'") == "300.0");
        file.reset();

        // Never twice: an older viewer's later save of the XML stays its own.
        LLSD changed             = globalSettings();
        changed["RenderFarClip"] = control("F32", 400.0);
        changed["RenderDynamicExposureEnabled"] = control("Boolean", false);
        writeXml(dir, "settings.xml", changed);
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        ALCVars::instance().registerSchema(cvartest::schema());
        REQUIRE(ALCVars::instance().openGlobal(path, options));
        CHECK(render::draw_distance() == 300.f);
        CHECK(render::exposure::dynamic::enabled());
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a malformed value keeps its default", "[alcvarxmlimport]")
    {
        LLSD settings;
        settings["UISndClick"]    = control("String", "5e191c7b");
        settings["RenderFarClip"] = control("F32", "far");
        settings["UIScaleFactor"] = control("String", "1.25");
        const std::string xml     = writeXml(dir, "settings.xml", settings);

        ALCVarFileOptions options;
        options.importXml = xml;
        REQUIRE(store().openGlobal(dir.file("settings.db"), options));
        CHECK(ui::sounds::click() == LLUUID("4c8c3c77-de8d-bde2-b9b8-32635e0fd4a6"));
        CHECK(render::draw_distance() == 128.f);
        // A number kept as a string by mistake still parses.
        CHECK(ui::scale() == 1.25f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: an account's file is imported at its first login",
                     "[alcvarxmlimport]")
    {
        LLSD settings;
        settings["ChatFontSize"]  = control("S32", 3);
        settings["UIScaleFactor"] = control("F32", 1.25);
        const std::string xml     = writeXml(dir, "settings_per_account.xml", settings);

        REQUIRE(store().openGlobal(dir.file("settings.db")));
        ALCVarFileOptions options;
        options.importXml = xml;
        REQUIRE(store().openAccount(dir.file("account.db"), options));
        store().confirmAccount();
        CHECK(cvartest::chat::font_size() == 3);
        // An account's own override of a global setting.
        CHECK(ui::scale() == 1.25f);
        CHECK(store().layerOf(ui::scale.index()) == ALCVarLayer::Account);
        store().closeAccount();
        CHECK(cvartest::chat::font_size() == 1);
        CHECK(ui::scale() == 1.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a value the feature table saved goes when a level settles the import",
                     "[alcvarxmlimport]")
    {
        // The feature table had put the resolution scale at 2 for the level
        // in use; the user had put the draw distance at 300.
        LLSD settings;
        settings["RenderShadowResolutionScale"] = control("F32", 2.0);
        settings["RenderFarClip"]               = control("F32", 300.0);
        const std::string path                  = dir.file("settings.db");
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        // The file opens before the window, and the level is applied after.
        REQUIRE(store().openGlobal(path, options));
        const std::pair<U32, ALCVarValue> level[] = { { shadows::resolution_scale.index(), ALCVarValue(2.f) },
                                                      { render::draw_distance.index(), ALCVarValue(64.f) } };
        store().setQuality(level, false);
        CHECK(store().layerOf(shadows::resolution_scale.index()) == ALCVarLayer::Quality);
        CHECK(render::draw_distance() == 300.f);

        // Another GPU class, another level: the scale follows the table.
        const std::pair<U32, ALCVarValue> lower[] = { { shadows::resolution_scale.index(), ALCVarValue(1.f) } };
        store().setQuality(lower, false);
        CHECK(shadows::resolution_scale() == 1.f);
        store().close();
        CHECK(raw(path)->queryText("SELECT value FROM meta WHERE key = 'import_pending'") == "");

        // Settled once: the next run's level does not settle it again.
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        ALCVars::instance().registerSchema(cvartest::schema());
        REQUIRE(ALCVars::instance().openGlobal(path, options));
        ALCVars::instance().setQuality(level, false);
        CHECK(render::draw_distance() == 300.f);
        CHECK(ALCVars::instance().layerOf(shadows::resolution_scale.index()) == ALCVarLayer::Quality);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a default the XML could not hold is kept over the level's",
                     "[alcvarxmlimport]")
    {
        // The user had shadows at their default, which LLControlGroup never
        // saves; the level gives none.
        LLSD settings;
        settings["RenderFarClip"] = control("F32", 300.0);
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        REQUIRE(store().openGlobal(dir.file("settings.db"), options));
        const std::pair<U32, ALCVarValue> level[] = { { shadows::detail.index(), ALCVarValue(S32(0)) } };
        store().setQuality(level, false);
        CHECK(shadows::detail() == 2);
        CHECK(store().layerOf(shadows::detail.index()) == ALCVarLayer::Global);

        // A level in place before the file opens settles it as it imports.
        store().close();
        ALCVars::deleteSingleton();
        ALCVars::resetSlots(cvartest::schema());
        ALCVars::instance().registerSchema(cvartest::schema());
        ALCVars::instance().setQuality(level, false);
        REQUIRE(ALCVars::instance().openGlobal(dir.file("other.db"), options));
        CHECK(shadows::detail() == 2);
        CHECK(render::draw_distance() == 300.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a value of the other scope's setting is kept for the scope move",
                     "[alcvarxmlimport]")
    {
        // An account's setting in settings.xml, and a global one in an
        // account's file: each scope moved since LLControlGroup saved them.
        LLSD global;
        global["ChatFontSize"] = control("S32", 3);
        LLSD account;
        account["RenderFarClip"] = control("F32", 512.0);
        ALCVarFileOptions globalOptions;
        globalOptions.importXml = writeXml(dir, "settings.xml", global);
        ALCVarFileOptions accountOptions;
        accountOptions.importXml = writeXml(dir, "settings_per_account.xml", account);

        REQUIRE(store().openGlobal(dir.file("settings.db"), globalOptions));
        CHECK(cvartest::chat::font_size() == 1);
        REQUIRE(store().openAccount(dir.file("account.db"), accountOptions));
        store().confirmAccount();
        CHECK(cvartest::chat::font_size() == 3);
        CHECK(render::draw_distance() == 512.f);
        store().closeAccount();
        CHECK(render::draw_distance() == 512.f);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a sensitive value that does not convert is not logged",
                     "[alcvarxmlimport]")
    {
        LLSD key;
        key["endpoint"] = "https://example.invalid/";
        key["id"]       = "secret-key-1234";
        LLSD settings;
        settings["net.translate.api_key"] = control("LLSD", key);
        settings["RenderFarClip"]         = control("F32", "far");
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        CaptureLog log(LLError::LEVEL_WARN);
        REQUIRE(store().openGlobal(dir.file("settings.db"), options));
        CHECK(cvartest::net::translate::api_key.view().empty());
        // Said, without the value: an ordinary setting's is said whole.
        CHECK_FALSE(log.messageWith("net.translate.api_key", false).empty());
        CHECK(log.messageWith("secret-key-1234", false).empty());
        CHECK_FALSE(log.messageWith("far", false).empty());
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a U32 past 2^31 saved negative is read back as the U32",
                     "[alcvarxmlimport]")
    {
        // LLControlGroup writes a U32 as LLSD's signed integer.
        LLSD settings;
        settings["SomeOtherViewersMask"]    = control("U32", S32(-1294967296));
        settings["render.shadows.max_maps"] = control("U32", S32(-1));
        const std::string path              = dir.file("settings.db");
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        REQUIRE(store().openGlobal(path, options));
        // 4294967295, clamped into the range: its top, not its bottom.
        CHECK(shadows::max_maps() == 8u);
        store().close();
        CHECK(raw(path)->queryInt("SELECT value FROM settings WHERE name = 'SomeOtherViewersMask'") == 3000000000);
        CHECK(raw(path)->queryInt("SELECT type FROM settings WHERE name = 'SomeOtherViewersMask'") ==
              S64(ALCVarType::U32));
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a value is taken at its setting's first step through the name it was under",
                     "[alcvarxmlimport]")
    {
        // As LLControlGroup kept them, before any migrate: levels before they
        // doubled, under an old name; a percentage; seconds in a float; a
        // clouds' look that means nothing now.
        LLSD settings;
        settings["TerrainDetail"]     = control("S32", 3);
        settings["world.wind_scale"]  = control("F32", 150.0);
        settings["ui.toast.delay"]    = control("F32", 1.5);
        settings["world.cloud_style"] = control("S32", 2);
        const std::string path        = dir.file("settings.db");
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        REQUIRE(store().openGlobal(path, options));
        CHECK(cvartest::world::detail_level() == 6);
        CHECK(cvartest::world::wind_scale() == 1.5f);
        CHECK(ui::toast::delay() == 1500);
        CHECK(cvartest::world::cloud_style() == 0);
        store().close();

        // Written at each setting's last step, as this build has it now.
        auto file = raw(path);
        CHECK(file->queryText("SELECT cast(value AS TEXT) FROM settings WHERE name = 'world.detail_level'") == "6");
        CHECK(U64(*file->queryInt("SELECT decl FROM settings WHERE name = 'world.detail_level'")) ==
              cvartest::world::detail_level.desc().decl());
        CHECK(file->queryText("SELECT unit FROM settings WHERE name = 'ui.toast.delay'") == "milliseconds");
        CHECK(file->queryInt("SELECT count(*) FROM settings WHERE name = 'world.cloud_style'") == 0);
    }

    TEST_CASE_METHOD(FileStore, "alcvarxmlimport: a hook is given the XML's values by name and a split runs there",
                     "[alcvarxmlimport]")
    {
        // world.sun_angles, which world.sun_azimuth and world.sun_elevation
        // were split from, and world.fog_density, whose hook reads another.
        LLSD angles = LLSD::emptyArray();
        angles.append(90.0);
        angles.append(30.0);
        LLSD settings;
        settings["world.sun_angles"]  = control("LLSD", angles);
        settings["world.fog_density"] = control("F32", 0.25);
        bool seen                     = false;
        store().registerMigration("fog_from_old", [&seen](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup& lookup) {
            seen = lookup("world.sun_angles") != nullptr;
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<F32>() * 2.f));
        });
        store().registerMigration("azimuth_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[0]));
        });
        store().registerMigration("elevation_from_angles", [](const ALCVarDesc&, const ALCVarStored* own, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(own->value.get<LLVector2>().mV[1]));
        });
        const std::string path = dir.file("settings.db");
        ALCVarFileOptions options;
        options.importXml = writeXml(dir, "settings.xml", settings);
        REQUIRE(store().openGlobal(path, options));
        CHECK(seen);
        CHECK(cvartest::world::fog_density() == 0.5f);
        CHECK(cvartest::world::sun_azimuth() == 90.f);
        CHECK(cvartest::world::sun_elevation() == 30.f);
        store().close();

        // In the import's own transaction, at its stamp.
        auto file = raw(path);
        const auto stamp = file->queryText("SELECT value FROM meta WHERE key = 'import_pending'");
        REQUIRE(stamp);
        CHECK(std::to_string(file->queryInt("SELECT modified FROM settings WHERE name = 'world.sun_azimuth'").value_or(0)) == *stamp);
        CHECK(file->queryText("SELECT cast(value AS TEXT) FROM settings WHERE name = 'world.sun_elevation'") == "30.0");
    }
}

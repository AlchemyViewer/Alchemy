/**
 * @file alcvarpreview_test.cpp
 * @brief ALCVarPreview: each row of a settings file said as an open would
 *        take it, and the file left as it was.
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

#include "alcvarpreview.h"
#include "alcvartestschema.h"

#include "aldbfaultvfs.h"
#include "fsyspath.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    namespace world = cvartest::world;

    // Writes a row as some viewer might have, at the step `decl` names.
    void putRow(const std::string& path, std::string_view name, ALCVarType type, std::string_view unit, U64 decl,
                std::string_view valueSql, S64 modified, bool tombstone = false)
    {
        int         rc = SQLITE_OK;
        std::string error;
        auto        file = ALDbConnection::open(path, ALDbConnection::Access::ReadWrite, {}, &rc, &error);
        INFO(error);
        REQUIRE(file);
        file->prepare("INSERT OR REPLACE INTO settings(name, type, unit, decl, value, tombstone, modified) "
                      "VALUES (?1, ?2, ?3, ?4, " +
                      std::string(valueSql) + ", ?5, ?6)")
            .bind(1, name)
            .bind(2, S64(type))
            .bind(3, unit)
            .bind(4, S64(decl))
            .bind(5, tombstone)
            .bind(6, modified)
            .run();
        REQUIRE(file->firstError() == SQLITE_OK);
    }

    // A file's bytes and when it was last written, or nothing.
    std::string stateOf(const std::string& path)
    {
        const std::filesystem::path file = fsyspath(path);
        if (!std::filesystem::exists(file))
        {
            return "";
        }
        std::ifstream      in(file, std::ios::binary);
        std::ostringstream bytes;
        bytes << in.rdbuf();
        // In nanoseconds, a 64-bit count: libc++ keeps a file's time as a
        // 128-bit one, which std::to_string takes no overload of.
        const auto written = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::filesystem::last_write_time(file).time_since_epoch());
        return bytes.str() + "@" + std::to_string(written.count());
    }

    // The one line about `name`, or "".
    std::string lineOf(const std::vector<std::string>& lines, const std::string& name)
    {
        for (const auto& line : lines)
        {
            if (line.starts_with(name + ":") || line.starts_with(name + " ("))
            {
                return line;
            }
        }
        return "";
    }

    TEST_CASE_METHOD(TestStore, "alcvarpreview: each row is said as an open takes it and the file is left as it was",
                     "[alcvarpreview]")
    {
        ll_test::ALDbTempDir dir;
        const std::string    path = dir.file("settings.db");
        {
            ALCVarFile file(path, {});
            REQUIRE(file.open().usable());
            file.close();
        }
        ALCVars& cvars = store();
        cvars.registerMigration("azimuth_from_angles", [](const ALCVarDesc&, const ALCVarStored* source, const ALCVarStoredLookup&) {
            return std::optional<ALCVarValue>(ALCVarValue(source->value.get<LLVector2>().mV[0]));
        });

        const ALCVarDesc& wind     = world::wind_scale.desc();
        const ALCVarDesc& lifetime = cvartest::ui::toast::lifetime.desc();
        const ALCVarDesc& style    = cvartest::ui::toast::style.desc();
        putRow(path, "world.wind_scale", ALCVarType::F32, "", wind.steps[0].decl, "150.0", 1000);
        putRow(path, "world.detail_level", ALCVarType::S32, "", world::detail_level.desc().decl(), "3", 1000);
        putRow(path, "world.fog_density", ALCVarType::F32, "", world::fog_density.desc().steps[0].decl, "0.25", 1000);
        putRow(path, "ui.toast.duration", ALCVarType::F32, "seconds", lifetime.steps[0].decl, "1.5", 1000);
        putRow(path, "world.cloud_style", ALCVarType::S32, "", 0x777, "2", 1000);
        putRow(path, "ui.toast.style", ALCVarType::S32, "", style.superseded[0], "2", 1000);
        putRow(path, "render.draw_distance", ALCVarType::F32, "metres", cvartest::render::draw_distance.desc().decl(), "NULL",
               1000, true);
        putRow(path, "render.nothing", ALCVarType::F32, "", 0x123, "1.0", 1000);
        putRow(path, "world.sun_angles", ALCVarType::Vec2, "", 0, "'[90.0,30.0]'", 1000);
        putRow(path, "chat.font_size", ALCVarType::S32, "", cvartest::chat::font_size.desc().decl(), "2", 1000);

        const std::string before    = stateOf(path);
        const std::string beforeWal = stateOf(path + "-wal");
        std::string       error;
        const auto        lines = ALCVarPreview::file(cvars, path, ALCVarLayer::Global, error);
        INFO(error);
        REQUIRE(lines);
        std::string all;
        for (const auto& line : *lines)
        {
            all += line + "\n";
        }
        INFO(all);

        // Through a migrate, and written forward.
        std::string line = lineOf(*lines, "world.wind_scale");
        CHECK(line.find("step 1 -> 2 (scale)") != std::string::npos);
        CHECK(line.find("becomes 1.5") != std::string::npos);
        CHECK(line.find("written forward at step 2") != std::string::npos);
        // At its last step: as it is.
        line = lineOf(*lines, "world.detail_level");
        CHECK(line.find("at step 2") != std::string::npos);
        CHECK(line.find("as it is") != std::string::npos);
        // A hook this build does not have.
        line = lineOf(*lines, "world.fog_density");
        CHECK(line.find("fog_from_old is not registered") != std::string::npos);
        CHECK(line.find("left as it was") != std::string::npos);
        // An old name's row, through the rename.
        line = lineOf(*lines, "ui.toast.duration");
        CHECK(line.find("an old name of ui.toast.lifetime") != std::string::npos);
        CHECK(line.find("(rename)") != std::string::npos);
        CHECK(line.find("becomes 3") != std::string::npos);
        // A newer viewer's step, the setting a name was before, a removal,
        // and a name no setting has.
        CHECK(lineOf(*lines, "world.cloud_style").find("a step this build does not know (0000000000000777)") != std::string::npos);
        CHECK(lineOf(*lines, "ui.toast.style").find("was before it was declared again") != std::string::npos);
        CHECK(lineOf(*lines, "render.draw_distance").find("removed") != std::string::npos);
        CHECK(lineOf(*lines, "render.nothing").find("no setting of this build") != std::string::npos);
        // A retired source, and the setting it gives a first value.
        CHECK(lineOf(*lines, "world.sun_angles").find("world.sun_azimuth") != std::string::npos);
        line = lineOf(*lines, "world.sun_azimuth");
        CHECK(line.find("no row of its own") != std::string::npos);
        CHECK(line.find("90") != std::string::npos);
        CHECK(lineOf(*lines, "world.sun_elevation").find("elevation_from_angles is not registered") != std::string::npos);
        // Another scope's.
        CHECK(lineOf(*lines, "chat.font_size").find("an account's setting") != std::string::npos);

        // Nothing written.
        CHECK(stateOf(path) == before);
        CHECK(stateOf(path + "-wal") == beforeWal);
    }

    TEST_CASE_METHOD(TestStore, "alcvarpreview: a backup a file that is not there and another kind of file are refused",
                     "[alcvarpreview]")
    {
        ll_test::ALDbTempDir dir;
        std::string          error;
        CHECK_FALSE(ALCVarPreview::file(store(), dir.file("settings.albackup"), ALCVarLayer::Global, error));
        CHECK(error.find("no reader yet") != std::string::npos);
        CHECK_FALSE(ALCVarPreview::file(store(), dir.file("missing.db"), ALCVarLayer::Global, error));
        CHECK(error.find("cannot be read") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(fsyspath(dir.file("missing.db"))));
        {
            std::ofstream other(std::filesystem::path(fsyspath(dir.file("other.db"))), std::ios::binary);
            other << "not a database";
        }
        CHECK_FALSE(ALCVarPreview::file(store(), dir.file("other.db"), ALCVarLayer::Global, error));
        CHECK_FALSE(error.empty());
    }
}

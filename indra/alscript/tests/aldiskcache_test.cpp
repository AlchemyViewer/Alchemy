/**
 * @file aldiskcache_test.cpp
 * @brief Tests for ALDiskCache: the disk asked again only once what is kept is stale or the settings move.
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

#include "../preprocessor/aldiskcache.h"

#include "fsyspath.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace
{
    namespace fs = std::filesystem;

    // A folder of its own for each test, gone with it.
    struct Scratch
    {
        fs::path root;

        Scratch()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root             = fs::temp_directory_path() / ("aldiskcache_" + std::to_string(stamp));
            fs::create_directories(root);
            root = fs::canonical(root);
        }
        ~Scratch()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path    under(const std::string& relative) const { return (root / relative).make_preferred(); }
        std::string at(const std::string& relative) const { return fsyspath(under(relative)).string(); }
        std::string write(const std::string& relative, const std::string& text) const
        {
            const fs::path path = under(relative);
            fs::create_directories(path.parent_path());
            llofstream out(path, std::ios::binary);
            out << text;
            return fsyspath(path).string();
        }
    };
}

namespace
{
    TEST_CASE("aldiskcache: what is blessed and admitted kept until stale or the settings move", "[aldiskcache]")
    {
        Scratch           s;
        const std::string part = s.write("includes/lib/part.lsl", "integer part;\n");
        s.write("includes/.lslrc", "{\"include\": [\"lib\"]}");
        s.write("project/a/.lslrc", "{\"include\": [\"../../includes/lib\"]}");
        ALDiskCache cache;
        const std::vector<std::string> own{ s.at("includes") };

        ALDiskCache::Blessed& first = cache.blessed(own, true, s.at("project/a"), {}, 1, 100.0);
        UNSCOPED_INFO("the scripter's, and what its .lslrc lists");
        REQUIRE(first.includes.folders().size() == size_t(2));
        UNSCOPED_INFO("a file under them admitted");
        CHECK(cache.admits(first, part).value_or("") == part);
        UNSCOPED_INFO("what is not there, not");
        REQUIRE_FALSE(cache.admits(first, s.at("includes/lib/none.lsl")));

        // Taken away: still admitted while what is kept is fresh, and the
        // folders as they were.
        fs::remove(s.under("includes/lib/part.lsl"));
        ALDiskCache::Blessed& again = cache.blessed(own, true, s.at("project/a"), {}, 1, 101.0);
        {
            INFO("the same kept");
            CHECK(&again == &first);
            REQUIRE(cache.admits(again, part).has_value());
        }
        std::string text;
        UNSCOPED_INFO("but not read");
        CHECK_FALSE(cache.read(part, text));

        ALDiskCache::Blessed& later = cache.blessed(own, true, s.at("project/a"), {}, 1, 100.0 + ALDiskCache::FRESH_SECONDS + 0.5);
        UNSCOPED_INFO("a moment later, asked again");
        REQUIRE_FALSE(cache.admits(later, part));
        s.write("includes/lib/part.lsl", "integer part;\n");
        UNSCOPED_INFO("kept as asked until then");
        REQUIRE_FALSE(cache.admits(later, part));
        ALDiskCache::Blessed& moved = cache.blessed(own, true, s.at("project/a"), {}, 2, 103.0);
        UNSCOPED_INFO("the settings moved: asked again at once");
        REQUIRE(cache.admits(moved, part).has_value());

        ALDiskCache::Blessed& plain = cache.blessed(own, false, s.at("project/a"), {}, 2, 103.0);
        UNSCOPED_INFO("SLua's: no .lslrc read");
        REQUIRE(plain.includes.folders().size() == size_t(1));
        ALDiskCache::Blessed& aliased = cache.blessed(own, false, std::string(), { s.at("project") }, 2, 103.0);
        UNSCOPED_INFO("an alias's folder blessed outright");
        REQUIRE(aliased.includes.folders().size() == size_t(2));
        UNSCOPED_INFO("the file at the top of each that has it");
        REQUIRE(cache.atTop(moved, ".lslrc").size() == size_t(1));
    }

    TEST_CASE("aldiskcache: a text read again once its time or size changes; upwards kept a moment", "[aldiskcache]")
    {
        Scratch           s;
        const std::string file = s.write("lib/util.lsl", "integer one;\n");
        ALDiskCache       cache;
        std::string       text;
        REQUIRE(cache.read(file, text));
        CHECK(text == "integer one;\n");
        s.write("lib/util.lsl", "integer three;\n");
        {
            INFO("read again once it changed");
            CHECK(cache.read(file, text));
            CHECK(text == "integer three;\n");
        }
        UNSCOPED_INFO("nothing where nothing is");
        CHECK_FALSE(cache.read(s.at("lib/none.lsl"), text));

        s.write("a/.luaurc", "{}");
        s.write("a/b/c/.luaurc", "{}");
        // A copy: upwards() answers with what it keeps, which a later call
        // for the same folder fills again.
        const std::vector<std::string> up = cache.upwards(s.at("a/b/c"), ".luaurc", 1, 10.0);
        {
            INFO("nearest first, to the root");
            REQUIRE(up.size() >= 2);
            CHECK(up[0] == s.at("a/b/c/.luaurc"));
            CHECK(up[1] == s.at("a/.luaurc"));
        }
        s.write("a/b/.luaurc", "{}");
        UNSCOPED_INFO("kept a moment");
        REQUIRE(cache.upwards(s.at("a/b/c"), ".luaurc", 1, 11.0).size() == up.size());
        const std::vector<std::string>& later = cache.upwards(s.at("a/b/c"), ".luaurc", 1, 10.0 + ALDiskCache::FRESH_SECONDS + 1.0);
        REQUIRE(later.size() > 1);
        UNSCOPED_INFO("then found");
        CHECK(later[1] == s.at("a/b/.luaurc"));
    }
}

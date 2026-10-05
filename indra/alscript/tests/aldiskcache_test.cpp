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

#include "../test/lltut.h"

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

namespace tut
{
    struct aldiskcache_data
    {
    };

    typedef test_group<aldiskcache_data> aldiskcache_group;
    typedef aldiskcache_group::object    aldiskcache_object;
    aldiskcache_group                    aldiskcache_instance("aldiskcache");

    template<> template<>
    void aldiskcache_object::test<1>()
    {
        set_test_name("the folders blessed as the preprocessor blesses them, kept a moment or until the settings move; what they admit kept with them");
        Scratch           s;
        const std::string part = s.write("includes/lib/part.lsl", "integer part;\n");
        s.write("includes/.lslrc", "{\"include\": [\"lib\"]}");
        s.write("project/a/.lslrc", "{\"include\": [\"../../includes/lib\"]}");
        ALDiskCache cache;
        const std::vector<std::string> own{ s.at("includes") };

        ALDiskCache::Blessed& first = cache.blessed(own, true, s.at("project/a"), {}, 1, 100.0);
        ensure_equals("the scripter's, and what its .lslrc lists", first.includes.folders().size(), size_t(2));
        ensure_equals("a file under them admitted", cache.admits(first, part).value_or(""), part);
        ensure("what is not there, not", !cache.admits(first, s.at("includes/lib/none.lsl")));

        // Taken away: still admitted while what is kept is fresh, and the
        // folders as they were.
        fs::remove(s.under("includes/lib/part.lsl"));
        ALDiskCache::Blessed& again = cache.blessed(own, true, s.at("project/a"), {}, 1, 101.0);
        ensure("the same kept", &again == &first && cache.admits(again, part).has_value());
        std::string text;
        ensure("but not read", !cache.read(part, text));

        ALDiskCache::Blessed& later = cache.blessed(own, true, s.at("project/a"), {}, 1, 100.0 + ALDiskCache::FRESH_SECONDS + 0.5);
        ensure("a moment later, asked again", !cache.admits(later, part));
        s.write("includes/lib/part.lsl", "integer part;\n");
        ensure("kept as asked until then", !cache.admits(later, part));
        ALDiskCache::Blessed& moved = cache.blessed(own, true, s.at("project/a"), {}, 2, 103.0);
        ensure("the settings moved: asked again at once", cache.admits(moved, part).has_value());

        ALDiskCache::Blessed& plain = cache.blessed(own, false, s.at("project/a"), {}, 2, 103.0);
        ensure_equals("SLua's: no .lslrc read", plain.includes.folders().size(), size_t(1));
        ALDiskCache::Blessed& aliased = cache.blessed(own, false, std::string(), { s.at("project") }, 2, 103.0);
        ensure_equals("an alias's folder blessed outright", aliased.includes.folders().size(), size_t(2));
        ensure_equals("the file at the top of each that has it", cache.atTop(moved, ".lslrc").size(), size_t(1));
    }

    template<> template<>
    void aldiskcache_object::test<2>()
    {
        set_test_name("a text read again only once its time or size changes; the configurations up the folders found once a moment");
        Scratch           s;
        const std::string file = s.write("lib/util.lsl", "integer one;\n");
        ALDiskCache       cache;
        std::string       text;
        ensure("read", cache.read(file, text) && text == "integer one;\n");
        s.write("lib/util.lsl", "integer three;\n");
        ensure("read again once it changed", cache.read(file, text) && text == "integer three;\n");
        ensure("nothing where nothing is", !cache.read(s.at("lib/none.lsl"), text));

        s.write("a/.luaurc", "{}");
        s.write("a/b/c/.luaurc", "{}");
        const std::vector<std::string>& up = cache.upwards(s.at("a/b/c"), ".luaurc", 1, 10.0);
        ensure("nearest first, to the root", up.size() >= 2 && up[0] == s.at("a/b/c/.luaurc") && up[1] == s.at("a/.luaurc"));
        s.write("a/b/.luaurc", "{}");
        ensure_equals("kept a moment", cache.upwards(s.at("a/b/c"), ".luaurc", 1, 11.0).size(), up.size());
        ensure("then found", cache.upwards(s.at("a/b/c"), ".luaurc", 1, 10.0 + ALDiskCache::FRESH_SECONDS + 1.0)[1] == s.at("a/b/.luaurc"));
    }
}

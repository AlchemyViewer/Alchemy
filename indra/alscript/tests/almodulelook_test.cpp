/**
 * @file almodulelook_test.cpp
 * @brief Tests for ALModuleLook: what may be a module in reach, from texts and folders, each named as it might be found.
 *
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
 */

#include "linden_common.h"

#include "../preprocessor/almodulelook.h"

#include "../preprocessor/alincludeidentity.h"
#include "../luau/alluauexports.h"
#include "fsyspath.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace
{
    namespace fs = std::filesystem;

    struct Scratch
    {
        fs::path root;

        Scratch()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root             = fs::temp_directory_path() / ("almodulelook_" + std::to_string(stamp));
            fs::create_directories(root);
            root = fs::canonical(root);
        }
        ~Scratch()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
        std::string at(const std::string& relative) const { return fsyspath((root / relative).make_preferred()).string(); }
        std::string write(const std::string& relative, const std::string& text) const
        {
            const fs::path path = (root / relative).make_preferred();
            fs::create_directories(path.parent_path());
            llofstream out(path, std::ios::binary);
            out << text;
            return fsyspath(path).string();
        }
    };

    const ALModuleLook::Candidate* named(const std::vector<ALModuleLook::Candidate>& found, const std::string& name)
    {
        for (const ALModuleLook::Candidate& one : found)
        {
            if (one.name == name)
            {
                return &one;
            }
        }
        return nullptr;
    }
}

namespace
{
    struct almodulelook_data
    {
        ALModuleLook look;
    };

    TEST_CASE_METHOD(almodulelook_data, "almodulelook: LSL: open texts first then folder files by path; never the script itself", "[almodulelook]")
    {
        Scratch           s;
        const std::string util = s.write("inc/lib/util.lsl", "integer helper() { return 1; }\n#define CHANNEL -42\n");
        s.write("inc/notes.txt", "not a module\n");
        ALModuleLook::Input input;
        input.self    = "object:self";
        input.folders = { { std::string(), s.at("inc") } };
        input.texts.push_back({ "inventory:open", "open.lsl", 3, std::make_shared<const std::string>("integer opened;\n") });
        input.texts.push_back({ "object:self", "self.lsl", 1, std::make_shared<const std::string>("integer mine;\n") });
        const std::vector<ALModuleLook::Candidate> found = look.look(input);
        UNSCOPED_INFO("the open one and the file, not the script itself nor another kind");
        REQUIRE(found.size() == size_t(2));
        {
            INFO("the open one first");
            CHECK(found[0].path == "inventory:open");
            CHECK(found[0].exports == std::vector<std::string>{ "opened" });
        }
        const ALModuleLook::Candidate* file = named(found, "util");
        {
            INFO("the file, as it stands");
            REQUIRE(file);
            CHECK(file->path == ALIncludeIdentity::ofFile(util));
        }
        {
            INFO("by its path from the folder, then its own name");
            REQUIRE(file->names.size() >= 2);
            CHECK(file->names[0] == "lib/util.lsl");
            CHECK(file->names[1] == "util.lsl");
        }
        UNSCOPED_INFO("what it declares");
        CHECK((file->exports == std::vector<std::string>({ "CHANNEL", "helper" }) ||
                                       file->exports == std::vector<std::string>({ "helper", "CHANNEL" })));
    }

    TEST_CASE_METHOD(almodulelook_data, "almodulelook: SLua: a module by stem under prefix and alias; reread when its version moves", "[almodulelook]")
    {
        Scratch s;
        s.write("mods/net/init.luau", "return { get = function() end }\n");
        s.write("mods/util.luau", "local M = {}\nfunction M.clamp() end\nreturn M\n");
        ALModuleLook::Input input;
        input.lua     = true;
        input.self    = "object:self";
        input.folders = { { "@lib/", s.at("mods") } };
        input.aliases = { "lib" };
        auto text     = std::make_shared<const std::string>("local M = {}\nfunction M.one() end\nreturn M\n");
        input.texts.push_back({ "inventory:open", "open.luau", 1, text });
        std::vector<ALModuleLook::Candidate> found = look.look(input);
        const ALModuleLook::Candidate* util = named(found, "util");
        {
            INFO("by its stem, under the prefix");
            REQUIRE(util);
            REQUIRE_FALSE(util->names.empty());
            CHECK(util->names[0] == "@lib/util");
            CHECK(util->exports == std::vector<std::string>{ "clamp" });
        }
        const ALModuleLook::Candidate* folder = named(found, "init");
        {
            INFO("a folder's init by the folder's name first");
            REQUIRE(folder);
            REQUIRE_FALSE(folder->names.empty());
            CHECK(folder->names[0] == "@lib/net");
        }
        const ALModuleLook::Candidate* open = named(found, "open");
        {
            INFO("an open one under the alias too");
            REQUIRE(open);
            REQUIRE(std::find(open->names.begin(), open->names.end(), "@lib/open") != open->names.end());
        }
        UNSCOPED_INFO("what it exports");
        CHECK(open->exports == std::vector<std::string>{ "one" });

        // The same version, another text: kept as it was read.
        input.texts[0].text = std::make_shared<const std::string>("local M = {}\nfunction M.two() end\nreturn M\n");
        found = look.look(input);
        open  = named(found, "open");
        REQUIRE(open);
        UNSCOPED_INFO("the same version: as read");
        CHECK(open->exports == std::vector<std::string>{ "one" });
        input.texts[0].version = 2;
        found = look.look(input);
        open  = named(found, "open");
        REQUIRE(open);
        UNSCOPED_INFO("a new one: read again");
        CHECK(open->exports == std::vector<std::string>{ "two" });
    }

    TEST_CASE_METHOD(almodulelook_data, "almodulelook: SLua: checked exports of the same text win for files and open texts alike", "[almodulelook]")
    {
        Scratch           s;
        const std::string made_text = "local function make()\n    local t = {}\n    t.alpha = 1\n    t.beta = function() end\n    return t\nend\nreturn make()\n";
        const std::string made      = s.write("mods/made.luau", made_text);
        ALModuleLook::Input input;
        input.lua     = true;
        input.self    = "object:self";
        input.folders = { { std::string(), s.at("mods") } };
        std::vector<ALModuleLook::Candidate> found = look.look(input);
        const ALModuleLook::Candidate*       one   = named(found, "made");
        REQUIRE(one);
        UNSCOPED_INFO("the text alone says nothing of a table made by a call");
        REQUIRE(one->exports.empty());
        ALLuauExports::checked(ALIncludeIdentity::ofFile(made), made_text, { "alpha", "beta" });
        found = look.look(input);
        one   = named(found, "made");
        REQUIRE(one);
        UNSCOPED_INFO("checked: what its type says");
        CHECK(one->exports == std::vector<std::string>({ "alpha", "beta" }));

        auto text = std::make_shared<const std::string>(made_text);
        input.texts.push_back({ "inventory:open", "open.luau", 1, text });
        ALLuauExports::checked("inventory:open", made_text, { "gamma" });
        found = look.look(input);
        one   = named(found, "open");
        REQUIRE(one);
        UNSCOPED_INFO("an open text checked");
        CHECK(one->exports == std::vector<std::string>{ "gamma" });
        input.texts[0].text    = std::make_shared<const std::string>("local M = {}\nfunction M.delta() end\nreturn M\n");
        input.texts[0].version = 2;
        found = look.look(input);
        one   = named(found, "open");
        REQUIRE(one);
        UNSCOPED_INFO("its text moved on since: read as it says");
        CHECK(one->exports == std::vector<std::string>{ "delta" });
    }

    TEST_CASE_METHOD(almodulelook_data, "almodulelook: extensions match in any case and a later check of the same text is taken", "[almodulelook]")
    {
        Scratch           s;
        const std::string text  = "local M = {}\nfunction M.one() end\nreturn M\n";
        const std::string upper = s.write("mods/Upper.LUAU", text);
        s.write("mods/defs.LSLH", "integer shouted;\n");
        ALModuleLook::Input input;
        input.lua     = true;
        input.self    = "object:self";
        input.folders = { { "@lib/", s.at("mods") } };
        std::vector<ALModuleLook::Candidate> found = look.look(input);
        const ALModuleLook::Candidate*       one   = named(found, "Upper");
        {
            INFO("listed, by its stem");
            REQUIRE(one);
            REQUIRE_FALSE(one->names.empty());
            CHECK(one->names[0] == "@lib/Upper");
            CHECK(one->exports == std::vector<std::string>{ "one" });
        }

        ALLuauExports::checked(ALIncludeIdentity::ofFile(upper), text, { "first" });
        found = look.look(input);
        one   = named(found, "Upper");
        REQUIRE(one);
        UNSCOPED_INFO("checked");
        CHECK(one->exports == std::vector<std::string>{ "first" });
        ALLuauExports::checked(ALIncludeIdentity::ofFile(upper), text, { "second" });
        found = look.look(input);
        one   = named(found, "Upper");
        REQUIRE(one);
        UNSCOPED_INFO("checked again, the text the same: taken");
        CHECK(one->exports == std::vector<std::string>{ "second" });

        input.lua = false;
        found     = look.look(input);
        one       = named(found, "defs.LSLH");
        {
            INFO("an LSL include's in any case, by its name whole");
            REQUIRE(one);
            CHECK(one->exports == std::vector<std::string>{ "shouted" });
        }
    }
}

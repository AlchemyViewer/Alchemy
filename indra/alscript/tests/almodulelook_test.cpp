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
#include "fsyspath.h"
#include "llfile.h"

#include "../test/lltut.h"

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

namespace tut
{
    struct almodulelook_data
    {
        ALModuleLook look;
    };

    typedef test_group<almodulelook_data> almodulelook_group;
    typedef almodulelook_group::object    almodulelook_object;
    almodulelook_group                    almodulelook_instance("almodulelook");

    template<> template<>
    void almodulelook_object::test<1>()
    {
        set_test_name("LSL: a folder's files by their path from it, what each declares; an open text first, the script itself never");
        Scratch           s;
        const std::string util = s.write("inc/lib/util.lsl", "integer helper() { return 1; }\n#define CHANNEL -42\n");
        s.write("inc/notes.txt", "not a module\n");
        ALModuleLook::Input input;
        input.self    = "object:self";
        input.folders = { { std::string(), s.at("inc") } };
        input.texts.push_back({ "inventory:open", "open.lsl", 3, std::make_shared<const std::string>("integer opened;\n") });
        input.texts.push_back({ "object:self", "self.lsl", 1, std::make_shared<const std::string>("integer mine;\n") });
        const std::vector<ALModuleLook::Candidate> found = look.look(input);
        ensure_equals("the open one and the file, not the script itself nor another kind", found.size(), size_t(2));
        ensure("the open one first", found[0].path == "inventory:open" && found[0].exports == std::vector<std::string>{ "opened" });
        const ALModuleLook::Candidate* file = named(found, "util");
        ensure("the file, as it stands", file && file->path == ALIncludeIdentity::ofFile(util));
        ensure("by its path from the folder, then its own name", file->names.size() >= 2 && file->names[0] == "lib/util.lsl" && file->names[1] == "util.lsl");
        ensure("what it declares", file->exports == std::vector<std::string>({ "CHANNEL", "helper" }) ||
                                       file->exports == std::vector<std::string>({ "helper", "CHANNEL" }));
    }

    template<> template<>
    void almodulelook_object::test<2>()
    {
        set_test_name("SLua: a module by its stem, under a folder's prefix and each alias; an open text read again only as its version moves");
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
        ensure("by its stem, under the prefix", util && util->names[0] == "@lib/util" && util->exports == std::vector<std::string>{ "clamp" });
        const ALModuleLook::Candidate* folder = named(found, "init");
        ensure("a folder's init by the folder's name first", folder && folder->names[0] == "@lib/net");
        const ALModuleLook::Candidate* open = named(found, "open");
        ensure("an open one under the alias too", open && std::find(open->names.begin(), open->names.end(), "@lib/open") != open->names.end());
        ensure("what it exports", open->exports == std::vector<std::string>{ "one" });

        // The same version, another text: kept as it was read.
        input.texts[0].text = std::make_shared<const std::string>("local M = {}\nfunction M.two() end\nreturn M\n");
        ensure("the same version: as read", named(look.look(input), "open")->exports == std::vector<std::string>{ "one" });
        input.texts[0].version = 2;
        ensure("a new one: read again", named(look.look(input), "open")->exports == std::vector<std::string>{ "two" });
    }
}

/**
 * @file alincludesearch_test.cpp
 * @brief Tests for ALIncludeSearch: an include or a require found in the world a fake says, and on the disk.
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
 */

#include "linden_common.h"

#include "../alincludesearch.h"

#include "../alincludeidentity.h"
#include "fsyspath.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <chrono>
#include <filesystem>
#include <map>

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
            root             = fs::temp_directory_path() / ("alincludesearch_" + std::to_string(stamp));
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

    // The world as a test says it: what the object holds, what the
    // inventory has of each name, and the configurations over an item.
    struct FakeWorld final : public ALIncludeWorld
    {
        std::vector<Item>                        object;
        bool                                     objectSaid = true;
        std::map<std::string, std::vector<Item>> inventory;
        std::vector<Item>                        configs;
        bool                                     configsPending = false;

        std::vector<Item> inObject(const std::string&, const std::string& item_name, bool& unknown) override
        {
            unknown = !objectSaid;
            std::vector<Item> out;
            for (const Item& item : object)
            {
                if (item.name == item_name)
                {
                    out.push_back(item);
                }
            }
            return out;
        }
        std::vector<Item> inInventory(const std::string& item_name, const std::vector<std::string>&, const std::string&) override
        {
            const auto found = inventory.find(item_name);
            return found == inventory.end() ? std::vector<Item>() : found->second;
        }
        ALPreprocessor::Found configsOver(const std::string&, const std::string&, std::vector<Item>& out) override
        {
            out = configs;
            return configsPending ? ALPreprocessor::Found::Pending : ALPreprocessor::Found::Yes;
        }
    };
}

namespace tut
{
    struct alincludesearch_data
    {
        ALScriptTextCache texts;
        FakeWorld         world;
        ALIncludeSearch   search{ texts, world };
        const LLUUID      prim  = LLUUID::generateNewID();
        const LLUUID      self  = LLUUID::generateNewID();

        ALIncludeSearch::Asking asking(bool lua = false) const { return { ALIncludeIdentity::ofItem(prim, self), lua }; }
        static ALIncludeSearch::Where where(bool world, bool disk, std::vector<std::string> folders = {})
        {
            ALIncludeSearch::Where out;
            out.order      = { "inventory", "object", "disk" };
            out.world      = world;
            out.disk       = disk;
            out.folders    = std::move(folders);
            out.generation = 1;
            out.now        = 1.0;
            return out;
        }
        static ALPreprocessor::Ask ask(const std::string& name, bool require = false, const std::string& from = std::string())
        {
            ALPreprocessor::Ask out;
            out.name    = name;
            out.require = require;
            out.from    = from;
            return out;
        }
        ALIncludeWorld::Item item(const std::string& name, bool in_inventory)
        {
            return { ALIncludeIdentity::ofItem(in_inventory ? LLUUID::null : prim, LLUUID::generateNewID()), name, LLUUID::generateNewID() };
        }
    };

    typedef test_group<alincludesearch_data> alincludesearch_group;
    typedef alincludesearch_group::object    alincludesearch_object;
    alincludesearch_group                    alincludesearch_instance("alincludesearch");

    template<> template<>
    void alincludesearch_object::test<1>()
    {
        set_test_name("the world in the order the settings say, only where let in; a text not in hand wanted, one held taken, one that failed not asked again unless retried");
        const ALIncludeWorld::Item held   = item("lib.lsl", true);
        const ALIncludeWorld::Item theirs = item("lib.lsl", false);
        world.inventory["lib.lsl"]        = { held };
        world.object                      = { theirs };
        ALPreprocessor::Include   found;
        ALIncludeSearch::wanted_t wanted;
        ensure_equals("the world not let in: nothing", int(search.resolve(ask("lib.lsl"), found, asking(), where(false, false), &wanted, false)),
                      int(ALPreprocessor::Found::No));
        ensure_equals("let in: the inventory's first, not in hand", int(search.resolve(ask("lib.lsl"), found, asking(), where(true, false), &wanted, false)),
                      int(ALPreprocessor::Found::Pending));
        ensure("named, and wanted", found.path == held.path && wanted.contains(held.path));

        texts.put(held.path, held.assetId, "integer lib;\n");
        ensure("in hand: taken", search.resolve(ask("lib.lsl"), found, asking(), where(true, false), nullptr, false) == ALPreprocessor::Found::Yes &&
                                     found.text == "integer lib;\n" && found.assetId == held.assetId.asString());

        ALIncludeSearch::Where object_first = where(true, false);
        object_first.order                  = { "object", "inventory" };
        texts.failed(theirs.path);
        ensure("the object's, failed before: passed over for the next", search.resolve(ask("lib.lsl"), found, asking(), object_first, nullptr, false) ==
                                                                               ALPreprocessor::Found::Yes && found.path == held.path);
        wanted.clear();
        ensure("retried: wanted again", search.resolve(ask("lib.lsl"), found, asking(), object_first, &wanted, true) == ALPreprocessor::Found::Pending &&
                                            found.path == theirs.path && wanted.contains(theirs.path));

        world.inventory.clear();
        world.object.clear();
        world.objectSaid = false;
        ensure("nowhere, and the object not said: it may be there", search.resolve(ask("lib.lsl"), found, asking(), where(true, false), nullptr, false) ==
                                                                        ALPreprocessor::Found::Pending);
    }

    template<> template<>
    void alincludesearch_object::test<2>()
    {
        set_test_name("the disk: under the scripter's folders and what their .lslrc lists, a name with its extension, beside a file asking; never elsewhere");
        Scratch           s;
        const std::string part = s.write("includes/lib/part.lsl", "integer part;\n");
        s.write("includes/.lslrc", "{\"include\": [\"lib\"]}");
        s.write("elsewhere/secret.lsl", "integer secret;\n");
        const std::string        own = s.at("includes");
        ALPreprocessor::Include  found;
        ensure("the disk off: nothing", search.resolve(ask("part"), found, asking(), where(false, false, { own }), nullptr, false) == ALPreprocessor::Found::No);
        ensure("on: through the .lslrc, with its extension",
               search.resolve(ask("part"), found, asking(), where(false, true, { own }), nullptr, false) == ALPreprocessor::Found::Yes &&
                   found.path == ALIncludeIdentity::ofFile(part) && found.name == "part.lsl" && found.text == "integer part;\n");
        std::string text;
        ensure("admitted: its text may be asked", search.heldText(found.path, text) && text == "integer part;\n");
        ensure("not a path out of the folders", search.resolve(ask(s.at("elsewhere/secret.lsl")), found, asking(), where(false, true, { own }), nullptr,
                                                               false) == ALPreprocessor::Found::No);
        ensure("nor its text", !search.heldText(ALIncludeIdentity::ofFile(s.at("elsewhere/secret.lsl")), text));
        // Beside a file asking, where the file is in a blessed folder.
        const std::string beside = s.write("includes/lib/near.lsl", "integer near;\n");
        ensure("beside the asking file", search.resolve(ask("near.lsl", false, ALIncludeIdentity::ofFile(part)), found, asking(), where(false, true, { own }),
                                                        nullptr, false) == ALPreprocessor::Found::Yes &&
                                             found.path == ALIncludeIdentity::ofFile(beside));
    }

    template<> template<>
    void alincludesearch_object::test<3>()
    {
        set_test_name("SLua: an alias through the .luaurc chain over a file on disk, its folder blessed only as far as a configuration may reach; the chain's mode read");
        Scratch           s;
        const std::string script = s.write("project/main.luau", "local net = require(\"@lib/net\")\n");
        s.write("project/.luaurc", "{\"aliases\": {\"lib\": \"./modules\"}, \"languageMode\": \"strict\"}");
        const std::string net = s.write("project/modules/net.luau", "return {}\n");
        s.write("project/modules/net/init.luau", "return {}\n");
        const ALIncludeSearch::Asking from_disk{ ALIncludeIdentity::ofFile(script), true };
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("project") });
        ALPreprocessor::Include       found;
        std::vector<std::string>      aliases;
        ensure("through the alias", search.resolve(ask("@lib/net", true), found, from_disk, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes &&
                                        found.path == ALIncludeIdentity::ofFile(net));
        ensure("its folder blessed for the run", aliases.size() == 1 && aliases[0].find("modules") != std::string::npos);
        ensure("an alias nobody says: nothing", search.resolve(ask("@other/x", true), found, from_disk, disk, nullptr, false) == ALPreprocessor::Found::No);
        std::vector<ALIncludeSearch::Config> chain;
        ensure("the chain over the file", search.configsFor(from_disk.self, from_disk, disk, nullptr, false, chain) == ALPreprocessor::Found::Yes &&
                                              !chain.empty() && chain[0].path == ALIncludeIdentity::ofFile(s.at("project/.luaurc")));
        ALLuauConfig config;
        ensure("its mode", search.configOf(from_disk, disk, config) && config.mode == "strict");
        const std::vector<std::pair<std::string, std::string>> folders = search.moduleFolders(from_disk, disk);
        ensure("the module folders: the scripter's, and the alias's under its name",
               folders.size() == 2 && folders[0].first.empty() && folders[1].first == "@lib/");

        // A script in the world: the world's configurations, Pending while
        // the object has not said; above them the one at the top of the
        // scripter's folders.
        world.configsPending = true;
        ensure("the object not said", search.configsFor(asking(true).self, asking(true), where(true, true, { s.at("project") }), nullptr, false, chain) ==
                                          ALPreprocessor::Found::Pending);
        world.configsPending = false;
        ensure("the top of the scripter's folder", search.configsFor(asking(true).self, asking(true), where(true, true, { s.at("project") }), nullptr, false,
                                                                     chain) == ALPreprocessor::Found::Yes &&
                                                       chain.size() == 1);
    }

    template<> template<>
    void alincludesearch_object::test<4>()
    {
        set_test_name("every file a text includes, and theirs in turn, each once");
        Scratch           s;
        const std::string a = s.write("inc/a.lsl", "#include \"b.lsl\"\ninteger a;\n");
        const std::string b = s.write("inc/b.lsl", "#include \"a.lsl\"\ninteger b;\n");
        const std::vector<ALPreprocessor::Include> found =
            search.includedBy("#include \"a.lsl\"\n#include <b.lsl>\ndefault { state_entry() { } }\n", asking(), where(false, true, { s.at("inc") }));
        ensure_equals("both, once", found.size(), size_t(2));
        ensure("in the order met", found[0].path == ALIncludeIdentity::ofFile(a) && found[1].path == ALIncludeIdentity::ofFile(b));
        ensure("names of an include", ALIncludeSearch::itemNameOf("./lib/util.lsl") == "util.lsl" &&
                                          ALIncludeSearch::foldersOf("../lib/util.lsl") == std::vector<std::string>({ "..", "lib" }));
    }
}

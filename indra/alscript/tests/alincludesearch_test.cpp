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

#include "../preprocessor/alincludesearch.h"

#include "../preprocessor/alincludeidentity.h"
#include "../preprocessor/alrequirenavigation.h"
#include "altestlinks.h"
#include "fsyspath.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

        // The inventory as folders, for a require walked through it: each
        // folder by its id, with its parent, its items and its folders.
        struct Folder
        {
            std::string                        parent;
            std::vector<Item>                  items;
            std::map<std::string, std::string> folders;
            bool                               known = true;
        };
        std::map<std::string, Folder>      folders;
        std::map<std::string, std::string> itemFolders;

        bool folderOf(const std::string& item, std::string& folder, std::string& name) override
        {
            const auto in = itemFolders.find(item);
            if (in == itemFolders.end())
            {
                return false;
            }
            folder = in->second;
            for (const Item& held : folders[folder].items)
            {
                if (held.path == item)
                {
                    name = held.name;
                }
            }
            return true;
        }
        ALPreprocessor::Found folderAbove(const std::string& folder, std::string& out) override
        {
            const auto found = folders.find(folder);
            if (found == folders.end() || found->second.parent.empty())
            {
                return ALPreprocessor::Found::No;
            }
            out = found->second.parent;
            return ALPreprocessor::Found::Yes;
        }
        ALPreprocessor::Found named(const std::string& folder, const std::string& name, std::vector<Item>& items, std::string& subfolder) override
        {
            const auto found = folders.find(folder);
            if (found == folders.end())
            {
                return ALPreprocessor::Found::No;
            }
            if (!found->second.known)
            {
                return ALPreprocessor::Found::Pending;
            }
            for (const Item& held : found->second.items)
            {
                if (held.name == name)
                {
                    items.push_back(held);
                }
            }
            if (const auto sub = found->second.folders.find(name); sub != found->second.folders.end())
            {
                subfolder = sub->second;
            }
            return items.empty() && subfolder.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
        }
        ALPreprocessor::Found contents(const std::string& folder, std::vector<Item>& items, std::vector<std::string>& subfolders) override
        {
            const auto found = folders.find(folder);
            if (found == folders.end())
            {
                return ALPreprocessor::Found::No;
            }
            if (!found->second.known)
            {
                return ALPreprocessor::Found::Pending;
            }
            items = found->second.items;
            for (const auto& [name, id] : found->second.folders)
            {
                subfolders.push_back(name);
            }
            return ALPreprocessor::Found::Yes;
        }
    };
}

namespace ll_test
{
    struct alincludesearch_data
    {
        ALScriptTextCache texts;
        FakeWorld         world;
        ALIncludeSearch   search{ texts, world };
        const LLUUID      prim  = LLUUID::generateNewID();
        const LLUUID      self  = LLUUID::generateNewID();

        ALIncludeSearch::Asking asking(bool lua = false) const { return { ALIncludeIdentity::ofItem(prim, self), lua }; }
        // Called from a lambda by its class's name: MSVC finds no static
        // member of the fixture from one, whatever it captures.
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
        // What the search before LAD9 finds of a require, as a require found
        // nowhere asks it (ALIncludeSearch::searchedBefore): the file on
        // disk it takes first, or nothing.
        std::string searched(const std::string& name, const ALIncludeSearch::Asking& who, const ALIncludeSearch::Where& in)
        {
            bool                                          unknown = false;
            const std::vector<ALIncludeSearch::Candidate> found   = search.candidatesFor(ask(name, true, who.self), who, in, {}, unknown);
            return found.empty() ? std::string() : found.front().file;
        }
    };

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: the world in the settings' order; a failed text skipped until retried", "[alincludesearch]")
    {
        const ALIncludeWorld::Item held   = item("lib.lsl", true);
        const ALIncludeWorld::Item theirs = item("lib.lsl", false);
        world.inventory["lib.lsl"]        = { held };
        world.object                      = { theirs };
        ALPreprocessor::Include   found;
        ALIncludeSearch::wanted_t wanted;
        UNSCOPED_INFO("the world not let in: nothing");
        CHECK(int(search.resolve(ask("lib.lsl"), found, asking(), where(false, false), &wanted, false)) ==
                      int(ALPreprocessor::Found::No));
        UNSCOPED_INFO("let in: the inventory's first, not in hand");
        CHECK(int(search.resolve(ask("lib.lsl"), found, asking(), where(true, false), &wanted, false)) ==
                      int(ALPreprocessor::Found::Pending));
        {
            INFO("named, and wanted");
            CHECK(found.path == held.path);
            CHECK(wanted.contains(held.path));
        }

        texts.put(held.path, held.assetId, "integer lib;\n");
        {
            INFO("in hand: taken");
            CHECK(search.resolve(ask("lib.lsl"), found, asking(), where(true, false), nullptr, false) == ALPreprocessor::Found::Yes);
            CHECK(found.text == "integer lib;\n");
            CHECK(found.assetId == held.assetId.asString());
        }

        ALIncludeSearch::Where object_first = where(true, false);
        object_first.order                  = { "object", "inventory" };
        texts.failed(theirs.path);
        {
            INFO("the object's, failed before: passed over for the next");
            CHECK(search.resolve(ask("lib.lsl"), found, asking(), object_first, nullptr, false) ==
                                                                               ALPreprocessor::Found::Yes);
            CHECK(found.path == held.path);
        }
        wanted.clear();
        {
            INFO("retried: wanted again");
            CHECK(search.resolve(ask("lib.lsl"), found, asking(), object_first, &wanted, true) == ALPreprocessor::Found::Pending);
            CHECK(found.path == theirs.path);
            CHECK(wanted.contains(theirs.path));
        }

        world.inventory.clear();
        world.object.clear();
        world.objectSaid = false;
        UNSCOPED_INFO("nowhere, and the object not said: it may be there");
        CHECK(search.resolve(ask("lib.lsl"), found, asking(), where(true, false), nullptr, false) ==
                                                                        ALPreprocessor::Found::Pending);
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: the disk: the scripter's folders and .lslrc lists and nowhere else", "[alincludesearch]")
    {
        Scratch           s;
        const std::string part = s.write("includes/lib/part.lsl", "integer part;\n");
        s.write("includes/.lslrc", "{\"include\": [\"lib\"]}");
        s.write("elsewhere/secret.lsl", "integer secret;\n");
        const std::string        own = s.at("includes");
        ALPreprocessor::Include  found;
        UNSCOPED_INFO("the disk off: nothing");
        CHECK(search.resolve(ask("part"), found, asking(), where(false, false, { own }), nullptr, false) == ALPreprocessor::Found::No);
        {
            INFO("on: through the .lslrc, with its extension");
            CHECK(search.resolve(ask("part"), found, asking(), where(false, true, { own }), nullptr, false) == ALPreprocessor::Found::Yes);
            CHECK(found.path == ALIncludeIdentity::ofFile(part));
            CHECK(found.name == "part.lsl");
            CHECK(found.text == "integer part;\n");
        }
        std::string text;
        {
            INFO("admitted: its text may be asked");
            REQUIRE(search.heldText(found.path, text));
            CHECK(text == "integer part;\n");
        }
        UNSCOPED_INFO("not a path out of the folders");
        CHECK(search.resolve(ask(s.at("elsewhere/secret.lsl")), found, asking(), where(false, true, { own }), nullptr,
                                                               false) == ALPreprocessor::Found::No);
        UNSCOPED_INFO("nor its text");
        CHECK_FALSE(search.heldText(ALIncludeIdentity::ofFile(s.at("elsewhere/secret.lsl")), text));
        // Beside a file asking, where the file is in a blessed folder.
        const std::string beside = s.write("includes/lib/near.lsl", "integer near;\n");
        {
            INFO("beside the asking file");
            CHECK(search.resolve(ask("near.lsl", false, ALIncludeIdentity::ofFile(part)), found, asking(), where(false, true, { own }),
                                                        nullptr, false) == ALPreprocessor::Found::Yes);
            CHECK(found.path == ALIncludeIdentity::ofFile(beside));
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: SLua: a .luaurc alias on disk found and its folder blessed; mode read", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("project/main.luau", "local net = require(\"@lib/net\")\n");
        s.write("project/.luaurc", "{\"aliases\": {\"lib\": \"./modules\"}, \"languageMode\": \"strict\"}");
        const std::string net = s.write("project/modules/net.luau", "return {}\n");
        s.write("project/modules/net/init.luau", "return {}\n");
        const ALIncludeSearch::Asking from_disk{ ALIncludeIdentity::ofFile(script), true };
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("project") });
        ALPreprocessor::Include       found;
        std::vector<std::string>      aliases;
        {
            INFO("through the alias");
            CHECK(search.resolve(ask("@lib/net", true), found, from_disk, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes);
            CHECK(found.path == ALIncludeIdentity::ofFile(net));
        }
        {
            INFO("its folder blessed for the run");
            REQUIRE(aliases.size() == 1);
            REQUIRE(aliases[0].find("modules") != std::string::npos);
        }
        UNSCOPED_INFO("an alias nobody says: nothing");
        CHECK(search.resolve(ask("@other/x", true), found, from_disk, disk, nullptr, false) == ALPreprocessor::Found::No);
        std::vector<ALIncludeSearch::Config> chain;
        {
            INFO("the chain over the file");
            CHECK(search.configsFor(from_disk.self, from_disk, disk, nullptr, false, chain) == ALPreprocessor::Found::Yes);
            REQUIRE_FALSE(chain.empty());
            CHECK(chain[0].path == ALIncludeIdentity::ofFile(s.at("project/.luaurc")));
        }
        ALLuauConfig config;
        {
            INFO("its mode");
            CHECK(search.configOf(from_disk, disk, config));
            CHECK(config.mode == "strict");
        }
        const std::vector<std::pair<std::string, std::string>> folders = search.moduleFolders(from_disk, disk);
        {
            INFO("the module folders: the scripter's, and the alias's under its name");
            REQUIRE(folders.size() == 2);
            REQUIRE(folders[0].first.empty());
            CHECK(folders[1].first == "@lib/");
        }

        // A script in the world: the world's configurations, Pending while
        // the object has not said; above them the one at the top of the
        // scripter's folders.
        world.configsPending = true;
        UNSCOPED_INFO("the object not said");
        CHECK(search.configsFor(asking(true).self, asking(true), where(true, true, { s.at("project") }), nullptr, false, chain) ==
                                          ALPreprocessor::Found::Pending);
        world.configsPending = false;
        {
            INFO("the top of the scripter's folder");
            CHECK(search.configsFor(asking(true).self, asking(true), where(true, true, { s.at("project") }), nullptr, false,
                                                                     chain) == ALPreprocessor::Found::Yes);
            REQUIRE(chain.size() == 1);
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: every file a text includes and theirs in turn each once", "[alincludesearch]")
    {
        Scratch           s;
        const std::string a = s.write("inc/a.lsl", "#include \"b.lsl\"\ninteger a;\n");
        const std::string b = s.write("inc/b.lsl", "#include \"a.lsl\"\ninteger b;\n");
        const std::vector<ALPreprocessor::Include> found =
            search.includedBy("#include \"a.lsl\"\n#include <b.lsl>\ndefault { state_entry() { } }\n", asking(), where(false, true, { s.at("inc") }));
        UNSCOPED_INFO("both, once");
        REQUIRE(found.size() == size_t(2));
        {
            INFO("in the order met");
            CHECK(found[0].path == ALIncludeIdentity::ofFile(a));
            CHECK(found[1].path == ALIncludeIdentity::ofFile(b));
        }
        {
            INFO("names of an include");
            CHECK(ALIncludeSearch::itemNameOf("./lib/util.lsl") == "util.lsl");
            CHECK(ALIncludeSearch::foldersOf("../lib/util.lsl") == std::vector<std::string>({ "..", "lib" }));
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: explainMissing: the object's silence first then the world and the disk", "[alincludesearch]")
    {
        const auto missing = [](const char* key, const char* name, const std::string& file = std::string()) {
            ALScriptProblem p;
            p.key  = key;
            p.args = { name };
            p.file = file;
            return p;
        };
        ALScriptProblems problems = { missing("PreprocIncludeNotFound", "lib.lsl"), missing("PreprocModuleNotFound", "util") };
        ALIncludeSearch::Missing facts;
        facts.objectUnanswered = true;
        facts.world            = false;
        facts.disk             = true;
        facts.noFolders        = false;
        facts.inWorld          = [](const std::string& name) { return name == "lib.lsl"; };
        ALIncludeSearch::explainMissing(problems, facts);
        REQUIRE(problems.size() == size_t(3));
        UNSCOPED_INFO("the silence said first");
        CHECK(problems.front().key == std::string("PreprocObjectUnanswered"));
        UNSCOPED_INFO("one in the world, not taken from");
        CHECK(problems[1].key == std::string("PreprocIncludeInWorld"));
        UNSCOPED_INFO("one nowhere, the disk looked in: as it was");
        CHECK(problems[2].key == std::string("PreprocModuleNotFound"));

        problems = { missing("PreprocModuleNotFound", "util"), missing("PreprocIncludeNotFound", "x.lsl", "disk:/lib/a.lsl") };
        facts    = ALIncludeSearch::Missing();
        facts.disk      = true;
        facts.noFolders = true;
        ALIncludeSearch::explainMissing(problems, facts);
        REQUIRE(problems.size() == size_t(2));
        UNSCOPED_INFO("no folder of the scripter's: the disk not looked in");
        CHECK(problems[0].key == std::string("PreprocModuleNotOnDisk"));
        UNSCOPED_INFO("but one asked from a file on disk is not second-guessed");
        CHECK(problems[1].key == std::string("PreprocIncludeNotFound"));
        UNSCOPED_INFO("said with its name");
        REQUIRE(problems[0].message.find("'util'") != std::string::npos);
    }
    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: leftOut: the names once in order and the problems and the disk's route", "[alincludesearch]")
    {
        const auto said = [](const char* key, const char* name) {
            ALScriptProblem p;
            p.key      = key;
            p.severity = ALScriptProblem::Severity::Error;
            if (name)
            {
                p.args = { name };
            }
            return p;
        };
        const ALScriptProblems problems = { said("PreprocObjectUnanswered", nullptr), said("PreprocIncludeNotFound", "b.lsl"),
                                            said("PreprocSomethingElse", "c.lsl"),   said("PreprocModuleNotFound", "util"),
                                            said("PreprocIncludeNotFound", "b.lsl") };
        ALIncludeSearch::LeftOut left = ALIncludeSearch::leftOut(problems);
        UNSCOPED_INFO("each once, in order");
        CHECK(left.names == std::vector<std::string>{ "b.lsl", "util" });
        UNSCOPED_INFO("every problem that said so");
        CHECK(left.problems == 3);
        UNSCOPED_INFO("found nowhere: nothing a folder would change");
        CHECK_FALSE(left.diskRoute);

        left = ALIncludeSearch::leftOut({ said("PreprocIncludeInWorld", "a.lsl") });
        {
            INFO("one in the world, which is not taken from: the disk's route");
            CHECK(left.diskRoute);
            REQUIRE(left.names.size() == 1);
        }
        left = ALIncludeSearch::leftOut({ said("PreprocModuleNotOnDisk", "m") });
        {
            INFO("the disk not looked in: the disk's route");
            CHECK(left.diskRoute);
            CHECK(left.names == std::vector<std::string>{ "m" });
        }
        UNSCOPED_INFO("nothing said: nothing left out");
        REQUIRE(ALIncludeSearch::leftOut({}).names.empty());
    }

    // The require parity suite (LA13): what a SLua require finds on disk,
    // case by case from the two references -- Luau's own require tests
    // (tests/RequireByString.test.cpp, over tests/require/) and Linden's VS
    // Code plugin (resolveInclude and resolveFile, over its
    // src/test/workspace/set_1) -- and ours. Each case says what it finds
    // now, and what LAD9 has it find. Where those differ, or the references
    // disagree, it says whose rule LAD9 picks: the plugin's, Luau's, or
    // ours -- the nearest `.luaurc` wins, which the plugin reverses. The
    // tree is a scripter's two folders on disk, world includes off as they
    // are by default (LAD10). Studio aliases (LA22) come with LA22. The
    // search before LA15 is kept beside each, for what changed: asked of
    // the code still, which keeps it for a require found nowhere, for each
    // name it took; an alias's it read through the `.luaurc`, which is gone,
    // and is kept as it found it.
    namespace
    {
        struct RequireCase
        {
            // What it shows, and whose case it is.
            const char* what;
            // The file asking, under the tree; "object" for a script in an
            // object, which with world includes off reads nothing of the world.
            const char* from;
            const char* name;
            // What it finds, under the tree, or nothing: what the search found
            // before the navigator, and what it finds now, as LAD9 has it.
            const char* before;
            const char* chosen;
            // Whose rule LAD9 picks, where the references or we disagree: and
            // what Luau's navigator answers, where it differs from the choice.
            const char* rule;
            const char* luau;
        };
    } // anonymous namespace

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: the require parity suite: Luau's and the plugin's cases as LAD9 picks", "[alincludesearch]")
    {
        Scratch s;
        // Luau's without_config, in lw/.
        s.write("proj/lw/dependency.luau", "return 'dependency'\n");
        s.write("proj/lw/module.luau", "return require('./dependency')\n");
        s.write("proj/lw/lua_dependency.lua", "return 'lua'\n");
        s.write("proj/lw/luau/init.luau", "return 'init.luau'\n");
        s.write("proj/lw/nested/init.luau", "return require('@self/submodule')\n");
        s.write("proj/lw/nested/submodule.luau", "return 'submodule'\n");
        s.write("proj/lw/ambiguous/file/dependency.luau", "return 'luau'\n");
        s.write("proj/lw/ambiguous/file/dependency.lua", "return 'lua'\n");
        s.write("proj/lw/ambiguous/directory/dependency.luau", "return 'file'\n");
        s.write("proj/lw/ambiguous/directory/dependency/init.luau", "return 'folder'\n");
        // The plugin's set_1 forms: an extension written, a path with no prefix.
        s.write("proj/lw/extension.luau", "return 'extension'\n");
        s.write("proj/lw/sub/deeper.luau", "return 'deeper'\n");
        // A folder's init, with files beside it and beside its folder, and
        // a `.luaurc` of its own.
        s.write("proj/lw/dir/init.luau", "return require('./x')\n");
        s.write("proj/lw/dir/x.luau", "return 'dir/x'\n");
        s.write("proj/lw/dir/.luaurc", "{\"aliases\": {\"own\": \"./x\"}}");
        s.write("proj/lw/x.luau", "return 'lw/x'\n");
        s.write("proj/x.luau", "return 'proj/x'\n");
        // A module's own folder, which @self names.
        s.write("proj/lw/selfish.luau", "return require('@self/child')\n");
        s.write("proj/lw/selfish/child.luau", "return 'child'\n");
        // Aliases: the top `.luaurc`, and a nearer one that says `lib` again.
        s.write("proj/.luaurc", "{\"aliases\": {\"lib\": \"./lib\", \"far\": \"./lib/far_target\", \"chain\": \"@lib/inner\", "
                                "\"c1\": \"@c2\", \"c2\": \"@c1\", \"sl-std\": \"./lib\"}}");
        s.write("proj/lib/util.luau", "return 'lib/util'\n");
        s.write("proj/lib/inner.luau", "return 'lib/inner'\n");
        s.write("proj/lib/far_target.luau", "return 'far'\n");
        s.write("proj/lw/sub/.luaurc", "{\"aliases\": {\"lib\": \"./near_lib\"}}");
        s.write("proj/lw/sub/near_lib/util.luau", "return 'near_lib/util'\n");
        s.write("proj/lw/sub/asker.luau", "return require('@lib/util')\n");
        // The scripter's other folder, which the old search falls back to.
        s.write("elsewhere/only_here.luau", "return 'only here'\n");
        // A folder nobody blessed.
        s.write("outside/secret.luau", "return 'secret'\n");

        // Not ABSOLUTE, which Windows' headers define.
        const std::string                   ABSOLUTE_PATH = s.at("proj/lw/dependency.luau");
        const std::string                   UNBLESSED     = s.at("outside/secret.luau");
        const std::vector<RequireCase>      cases      = {
            // Where all three agree.
            { "Luau RequireSimpleRelativePath: ./ beside the file", "proj/lw/module.luau", "./dependency", "proj/lw/dependency.luau",
              "proj/lw/dependency.luau", "", "" },
            { "Luau: ../ up a folder", "proj/lw/module.luau", "../x", "proj/x.luau", "proj/x.luau", "", "" },
            { "Luau RequireLua: .lua where there is no .luau", "proj/lw/module.luau", "./lua_dependency", "proj/lw/lua_dependency.lua",
              "proj/lw/lua_dependency.lua", "", "" },
            { "Luau RequireInitLuau: a folder's init", "proj/lw/module.luau", "./luau", "proj/lw/luau/init.luau", "proj/lw/luau/init.luau", "",
              "" },
            { "Luau RequireSubmoduleUsingSelfIndirectly: a folder whose init requires @self", "proj/lw/module.luau", "./nested",
              "proj/lw/nested/init.luau", "proj/lw/nested/init.luau", "", "" },
            { "Luau RequirePathWithAlias: an alias's file", "proj/lw/module.luau", "@far", "proj/lib/far_target.luau", "proj/lib/far_target.luau",
              "", "" },
            { "Luau RequirePathWithAliasPointingToDirectory: under an alias's folder", "proj/lw/module.luau", "@lib/util", "proj/lib/util.luau",
              "proj/lib/util.luau", "", "" },
            { "Luau: an alias's name in any case", "proj/lw/module.luau", "@LIB/util", "proj/lib/util.luau", "proj/lib/util.luau", "", "" },
            { "Luau RequireAliasThatDoesNotExist", "proj/lw/module.luau", "@missing/x", "", "", "", "@missing is not a valid alias" },
            { "nothing so named", "proj/lw/module.luau", "./nowhere", "", "", "", "" },
            { "a path out of the blessed folders", "proj/lw/module.luau", "../../outside/secret", "", "", "", "" },
            { "an absolute path nobody blessed", "proj/lw/module.luau", UNBLESSED.c_str(), "", "", "", "" },

            // The plugin's rules, which LAD9 takes where Luau's differ.
            { "plugin: no prefix is ./ (Luau RequireUnprefixedPath refuses)", "proj/lw/module.luau", "dependency", "proj/lw/dependency.luau",
              "proj/lw/dependency.luau", "plugin", "require path must start with a valid prefix: ./, ../, or @" },
            { "plugin set_1 nested_with_paths: a path with no prefix", "proj/lw/module.luau", "sub/deeper", "proj/lw/sub/deeper.luau",
              "proj/lw/sub/deeper.luau", "plugin", "require path must start with a valid prefix: ./, ../, or @" },
            { "plugin set_1 test_require: an extension written", "proj/lw/module.luau", "./extension.luau", "proj/lw/extension.luau",
              "proj/lw/extension.luau", "plugin", "could not resolve child component \"extension.luau\"" },
            { "plugin: a folder's init named (Luau CannotRequireInitLuauDirectly)", "proj/lw/module.luau", "./nested/init",
              "proj/lw/nested/init.luau", "proj/lw/nested/init.luau", "plugin", "could not resolve child component \"init\"" },
            { "plugin: an absolute path, in a blessed folder (Luau RequireAbsolutePath refuses)", "proj/lw/module.luau", ABSOLUTE_PATH.c_str(),
              "proj/lw/dependency.luau", "proj/lw/dependency.luau", "plugin", "require path must start with a valid prefix: ./, ../, or @" },
            { "plugin (LAD2): ./ from a folder's init is beside the init", "proj/lw/dir/init.luau", "./x", "proj/lw/dir/x.luau",
              "proj/lw/dir/x.luau", "plugin", "proj/lw/x.luau" },
            { "plugin (LAD2): ../ from a folder's init is beside its folder", "proj/lw/dir/init.luau", "../x", "proj/lw/x.luau", "proj/lw/x.luau",
              "plugin", "proj/x.luau" },
            { "plugin (LAD2): a folder's init reads the .luaurc beside it", "proj/lw/dir/init.luau", "@own", "proj/lw/dir/x.luau",
              "proj/lw/dir/x.luau", "plugin", "@own is not a valid alias" },
            { "plugin (LAD3): .luau before .lua (Luau RequireWithFileAmbiguity)", "proj/lw/module.luau", "./ambiguous/file/dependency",
              "proj/lw/ambiguous/file/dependency.luau", "proj/lw/ambiguous/file/dependency.luau", "plugin",
              "could not resolve child component \"dependency\" (ambiguous)" },
            { "plugin (LAD3): a file before a folder's init (Luau RequireWithDirectoryAmbiguity)", "proj/lw/module.luau",
              "./ambiguous/directory/dependency", "proj/lw/ambiguous/directory/dependency.luau", "proj/lw/ambiguous/directory/dependency.luau",
              "plugin", "could not resolve child component \"dependency\" (ambiguous)" },
            { "plugin: no search -- a name not beside the file is not found in another folder", "proj/lw/module.luau", "only_here",
              "elsewhere/only_here.luau", "", "plugin", "require path must start with a valid prefix: ./, ../, or @" },
            { "plugin: no search -- ./ the same", "proj/lw/module.luau", "./only_here", "elsewhere/only_here.luau", "", "plugin",
              "could not resolve child component \"only_here\"" },
            { "plugin: @sl-* is reserved", "proj/lw/module.luau", "@sl-std/util", "proj/lib/util.luau", "", "plugin", "proj/lib/util.luau" },
            { "plugin: an alias's path may not climb", "proj/lw/module.luau", "@lib/../lw/dependency", "proj/lw/dependency.luau", "", "plugin",
              "proj/lw/dependency.luau" },
            { "plugin and LAD10: a script in an object reads nothing of the disk but through an alias", "object", "./only_here",
              "elsewhere/only_here.luau", "", "plugin", "" },
            { "plugin and LAD10: nor with no prefix", "object", "only_here", "elsewhere/only_here.luau", "", "plugin", "" },

            // Luau's forms, which the plugin has not.
            { "Luau RequireSubmoduleUsingSelfDirectly: @self from a folder's init", "proj/lw/nested/init.luau", "@self/submodule", "",
              "proj/lw/nested/submodule.luau", "luau", "" },
            { "Luau: @self from a file is the folder of its name", "proj/lw/selfish.luau", "@self/child", "", "proj/lw/selfish/child.luau", "luau",
              "" },
            { "Luau RequireChainedAliasesSuccess: an alias that names another", "proj/lw/module.luau", "@chain", "", "proj/lib/inner.luau",
              "luau", "" },
            { "Luau RequireChainedAliasesFailureCyclic: a cycle of aliases", "proj/lw/module.luau", "@c1", "", "", "luau",
              "detected alias cycle (@c1 -> @c2 -> @c1)" },

            // Ours, against the plugin's.
            { "ours: the nearest .luaurc that names an alias wins (the plugin lets the farther)", "proj/lw/sub/asker.luau", "@lib/util",
              "proj/lw/sub/near_lib/util.luau", "proj/lw/sub/near_lib/util.luau", "ours", "" },
        };

        const ALIncludeSearch::Where disk = where(false, true, { s.at("proj"), s.at("elsewhere") });
        // A file by its path from the tree, `/` between the parts.
        const auto under_tree = [&s](const std::string& file) {
            std::string path = fsyspath(s.root).string() + "/";
            std::string at   = file;
            std::replace(path.begin(), path.end(), '\\', '/');
            std::replace(at.begin(), at.end(), '\\', '/');
            return at.compare(0, path.size(), path) == 0 ? at.substr(path.size()) : at;
        };
        for (const RequireCase& one : cases)
        {
            const std::string             from = std::string(one.from) == "object" ? asking(true).self : ALIncludeIdentity::ofFile(s.at(one.from));
            const ALIncludeSearch::Asking own{ from, true };
            ALPreprocessor::Include       found;
            std::vector<std::string>      aliases;
            const ALPreprocessor::Found   said = search.resolve(ask(one.name, true, from), found, own, disk, nullptr, false, &aliases);
            std::string                   file;
            std::string                   under;
            if (said == ALPreprocessor::Found::Yes && ALIncludeIdentity::fileOf(found.path, file))
            {
                under = under_tree(file);
            }
            INFO(one.what);
            REQUIRE(under == std::string(one.chosen));
            if (*one.name != '@')
            {
                // A name the search before took: asked of it.
                const std::string before = searched(one.name, own, disk);
                REQUIRE((before.empty() ? before : under_tree(before)) == std::string(one.before));
            }
            else if (!*one.rule)
            {
                // An alias's, where all agree: found so before.
                REQUIRE(under == std::string(one.before));
            }
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: navigatorPath: ./ for no prefix; @self; @sl- reserved; no alias climbs", "[alincludesearch]")
    {
        const auto path = [](const std::string& name, const std::string& stem = "main") { return ALRequireNavigation::navigatorPath(name, stem); };
        UNSCOPED_INFO("no prefix");
        CHECK(path("util").path == std::string("./util"));
        UNSCOPED_INFO("a folder in it");
        CHECK(path("lib/util").path == std::string("./lib/util"));
        UNSCOPED_INFO("./ as it is");
        CHECK(path("./util").path == std::string("./util"));
        UNSCOPED_INFO("../ as it is");
        CHECK(path("../util").path == std::string("../util"));
        UNSCOPED_INFO("a separator of Windows's");
        CHECK(path("lib\\util").path == std::string("./lib/util"));
        UNSCOPED_INFO("an alias as it is");
        CHECK(path("@lib/util").path == std::string("@lib/util"));
        UNSCOPED_INFO("@self from a file: the folder of its name");
        CHECK(path("@self/child").path == std::string("./main/child"));
        UNSCOPED_INFO("@self from a folder's init: beside it");
        CHECK(path("@self/child", "init").path == std::string("./child"));
        UNSCOPED_INFO("@self alone");
        CHECK(path("@self").path == std::string("./main"));
        UNSCOPED_INFO("@SELF in any case");
        CHECK(path("@SELF/child", "init").path == std::string("./child"));
        {
            INFO("@sl-* reserved");
            REQUIRE_FALSE(path("@sl-std/util").error.empty());
            REQUIRE_FALSE(path("@SL-std").error.empty());
        }
        {
            INFO("an alias may not climb");
            REQUIRE_FALSE(path("@lib/../secret").error.empty());
            REQUIRE_FALSE(path("@lib/a/../../b").error.empty());
        }
        UNSCOPED_INFO("@self may, as Luau's own");
        REQUIRE(path("@self/../x").error.empty());
        UNSCOPED_INFO("a name with .. in it is no climb");
        REQUIRE(path("@lib/a..b").error.empty());
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: a require walked through the world; Pending where it has not said", "[alincludesearch]")
    {
        // The inventory: a project folder in a root, with a library folder,
        // a module's folder with its init, and a configuration.
        const auto add = [this](const std::string& folder, const std::string& name) {
            const ALIncludeWorld::Item one{ ALIncludeIdentity::ofItem(LLUUID::null, LLUUID::generateNewID()), name, LLUUID::generateNewID() };
            world.folders[folder].items.push_back(one);
            world.itemFolders[one.path] = folder;
            return one;
        };
        world.folders["root"].parent  = std::string();
        world.folders["proj"].parent  = "root";
        world.folders["lib"].parent   = "proj";
        world.folders["mod"].parent   = "proj";
        world.folders["proj"].folders = { { "lib", "lib" }, { "mod", "mod" } };
        const ALIncludeWorld::Item main   = add("proj", "main");
        const ALIncludeWorld::Item util   = add("proj", "util");
        const ALIncludeWorld::Item shared = add("root", "shared");
        const ALIncludeWorld::Item net    = add("lib", "net");
        const ALIncludeWorld::Item init   = add("mod", "init");
        const ALIncludeWorld::Item config = add("proj", ".luaurc");
        for (const ALIncludeWorld::Item& one : { util, shared, net, init })
        {
            texts.put(one.path, one.assetId, "return '" + one.name + "'\n");
        }
        texts.put(config.path, config.assetId, "{\"aliases\": {\"lib\": \"./lib\", \"far\": \"/somewhere/on/disk\"}}");

        Scratch           s;
        const std::string top = s.write("inc/.luaurc", "{\"aliases\": {\"top\": \"./libs\"}}");
        const std::string x   = s.write("inc/libs/x.luau", "return 'x'\n");
        const ALIncludeSearch::Where on  = where(true, true, { s.at("inc") });
        const ALIncludeSearch::Where off = where(false, true, { s.at("inc") });
        const ALIncludeSearch::Asking own{ main.path, true };
        const auto found = [&](const std::string& name, const ALIncludeSearch::Where& in, ALIncludeSearch::wanted_t* wanted = nullptr) {
            ALPreprocessor::Include     include;
            std::vector<std::string>    aliases;
            const ALPreprocessor::Found said = search.resolve(alincludesearch_data::ask(name, true, own.self), include, own, in, wanted, false, &aliases);
            return said == ALPreprocessor::Found::Yes ? include.path : said == ALPreprocessor::Found::Pending ? std::string("pending") : std::string();
        };
        UNSCOPED_INFO("beside it");
        CHECK(found("./util", on) == util.path);
        UNSCOPED_INFO("with no prefix");
        CHECK(found("util", on) == util.path);
        UNSCOPED_INFO("up a folder");
        CHECK(found("../shared", on) == shared.path);
        UNSCOPED_INFO("a folder's init");
        CHECK(found("./mod", on) == init.path);
        UNSCOPED_INFO("in a folder");
        CHECK(found("./lib/net", on) == net.path);
        UNSCOPED_INFO("through the folder's .luaurc");
        CHECK(found("@lib/net", on) == net.path);
        UNSCOPED_INFO("one there is not");
        CHECK(found("./nothere", on) == std::string());
        UNSCOPED_INFO("a world .luaurc names nothing on disk");
        CHECK(found("@far/x", on) == std::string());
        UNSCOPED_INFO("past the world's top, the scripter's folders' own .luaurc");
        CHECK(found("@top/x", on) == ALIncludeIdentity::ofFile(x));

        // What the world has not said yet: Pending, and what is on its way
        // wanted.
        ALIncludeSearch::wanted_t wanted;
        world.folders["lib"].known = false;
        UNSCOPED_INFO("a folder not known: Pending");
        CHECK(found("./lib/net", on, &wanted) == std::string("pending"));
        world.folders["lib"].known = true;
        ALScriptTextCache         fresh;
        ALIncludeSearch           unread(fresh, world);
        ALPreprocessor::Include   include;
        std::vector<std::string>  aliases;
        UNSCOPED_INFO("the .luaurc not in hand: Pending");
        CHECK(unread.resolve(ask("@lib/net", true, own.self), include, own, on, &wanted, false, &aliases) ==
                                                       ALPreprocessor::Found::Pending);
        UNSCOPED_INFO("and wanted");
        CHECK(wanted.contains(config.path));

        // With world includes off, nothing of the world; the scripter's
        // folders' own .luaurc still.
        UNSCOPED_INFO("off: not beside it");
        CHECK(found("./util", off) == std::string());
        UNSCOPED_INFO("off: no .luaurc of the world's");
        CHECK(found("@lib/net", off) == std::string());
        UNSCOPED_INFO("off: the scripter's folders' own");
        CHECK(found("@top/x", off) == ALIncludeIdentity::ofFile(x));

        // An object's contents, one folder.
        world.folders["contents"].parent = std::string();
        const ALIncludeWorld::Item script = add("contents", "script");
        const ALIncludeWorld::Item helper = add("contents", "helper");
        texts.put(helper.path, helper.assetId, "return 'helper'\n");
        const ALIncludeSearch::Asking in_object{ script.path, true };
        ALPreprocessor::Include       got;
        {
            INFO("an object's: beside it");
            CHECK(search.resolve(ask("./helper", true, script.path), got, in_object, on, nullptr, false, &aliases) ==
                                             ALPreprocessor::Found::Yes);
            CHECK(got.path == helper.path);
        }
        UNSCOPED_INFO("an object has no folder above");
        CHECK(search.resolve(ask("../helper", true, script.path), got, in_object, on, nullptr, false, &aliases) ==
                                                    ALPreprocessor::Found::No);
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: studio aliases reach the disk from an object; a project's .luaurc wins", "[alincludesearch]")
    {
        Scratch           s;
        const std::string util   = s.write("library/util.luau", "return 'util'\n");
        const std::string helper = s.write("library/helper.lsl", "integer helper;\n");
        s.write("project/.luaurc", "{\"aliases\": {\"shared\": \"./own_shared\"}}");
        const std::string own    = s.write("project/own_shared/util.luau", "return 'own'\n");
        const std::string script = s.write("project/main.luau", "return require('@shared/util')\n");
        s.write("inc/empty.lsl", "\n");

        ALIncludeSearch::Where disk = where(false, true, { s.at("inc") });
        disk.aliases                = { { "lib", s.at("library") }, { "shared", s.at("library") } };
        const ALIncludeSearch::Asking in_object = asking(true);
        const auto found = [&](const std::string& name, const ALIncludeSearch::Asking& who, const ALIncludeSearch::Where& in) {
            ALPreprocessor::Include  include;
            std::vector<std::string> aliases;
            return search.resolve(alincludesearch_data::ask(name, true, who.self), include, who, in, nullptr, false, &aliases) == ALPreprocessor::Found::Yes ? include.path
                                                                                                                                       : std::string();
        };
        UNSCOPED_INFO("an object's script, through the studio's alias");
        CHECK(found("@lib/util", in_object, disk) == ALIncludeIdentity::ofFile(util));
        UNSCOPED_INFO("in any case");
        CHECK(found("@LIB/util", in_object, disk) == ALIncludeIdentity::ofFile(util));
        UNSCOPED_INFO("one nobody named");
        CHECK(found("@other/util", in_object, disk) == std::string());
        ALIncludeSearch::Where unnamed = disk;
        unnamed.aliases.clear();
        UNSCOPED_INFO("unnamed: not read, the folder blessed by nothing");
        CHECK(found("@lib/util", in_object, unnamed) == std::string());
        ALIncludeSearch::Where off = disk;
        off.disk                   = false;
        UNSCOPED_INFO("the disk off: nothing");
        CHECK(found("@lib/util", in_object, off) == std::string());

        // A project's own `.luaurc` names the alias first.
        const ALIncludeSearch::Asking from_disk{ ALIncludeIdentity::ofFile(script), true };
        UNSCOPED_INFO("the project's own alias");
        CHECK(found("@shared/util", from_disk, disk) == ALIncludeIdentity::ofFile(own));
        UNSCOPED_INFO("where it names none, the studio's");
        CHECK(found("@shared/util", in_object, disk) == ALIncludeIdentity::ofFile(util));

        // Naming blesses the folder for a require, and adds nothing to an
        // include's search.
        ALPreprocessor::Include include;
        UNSCOPED_INFO("an LSL include does not search an alias's folder");
        CHECK(search.resolve(ask("helper.lsl"), include, asking(), disk, nullptr, false) == ALPreprocessor::Found::No);
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: a require only the old search found: where and each way to say it now", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('util')\n");
        s.write("proj/.luaurc", "{\"aliases\": {\"shared\": \"../inc\"}}");
        const std::string under  = s.write("proj/lib/util2.luau", "return 'util2'\n");
        const std::string util   = s.write("inc/util.luau", "return 'util'\n");
        const std::string solo   = s.write("other/solo.luau", "return 'solo'\n");
        const std::string deep   = s.write("other/deep/net.luau", "return 'net'\n");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("proj"), s.at("proj/lib"), s.at("inc"), s.at("other") });
        const ALIncludeSearch::Asking from_disk{ ALIncludeIdentity::ofFile(script), true };
        const ALIncludeSearch::Asking in_object = asking(true);
        const auto missed = [&](const std::string& name, const ALIncludeSearch::Asking& who) {
            ALPreprocessor::Include  include;
            std::vector<std::string> aliases;
            CAPTURE(name);
            REQUIRE(search.resolve(alincludesearch_data::ask(name, true, who.self), include, who, disk, nullptr, false, &aliases) ==
                                                     ALPreprocessor::Found::No);
            return include;
        };
        const auto said = [](const ALPreprocessor::Include& include) {
            std::string out;
            for (const ALPreprocessor::Include::Move& move : include.moves)
            {
                out += (out.empty() ? "" : " ") + move.require + (move.alias.empty() ? std::string() : " +@" + move.alias);
            }
            return out;
        };

        ALPreprocessor::Include include = missed("util2", from_disk);
        UNSCOPED_INFO("found under the file's own folder");
        CHECK(include.searched == under);
        UNSCOPED_INFO("written from beside it");
        CHECK(said(include) == std::string("./lib/util2"));

        include = missed("util", from_disk);
        UNSCOPED_INFO("found in another include folder");
        CHECK(include.searched == util);
        UNSCOPED_INFO("through the .luaurc's alias that reaches it");
        CHECK(said(include) == std::string("@shared/util"));

        include = missed("util", in_object);
        UNSCOPED_INFO("from an object: through the alias of the .luaurc at the top of an include folder");
        CHECK(said(include) == std::string("@shared/util"));
        include = missed("solo", in_object);
        UNSCOPED_INFO("from an object, where none reaches it");
        CHECK(include.searched == solo);
        UNSCOPED_INFO("through a studio alias of the folder that holds it, named after it");
        REQUIRE(said(include) == std::string("@other/solo +@other"));
        UNSCOPED_INFO("the folder named");
        CHECK(include.moves[0].folder == s.at("other"));
        include = missed("deep/net", in_object);
        {
            INFO("a folder in it");
            CHECK(include.searched == deep);
            CHECK(said(include) == "@other/deep/net +@other");
        }

        include = missed("nowhere", from_disk);
        {
            INFO("nowhere before either");
            REQUIRE(include.searched.empty());
            REQUIRE(include.moves.empty());
        }
        include = missed("@missing/util", from_disk);
        UNSCOPED_INFO("an alias's is no searched name");
        REQUIRE(include.searched.empty());
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: a file beside a folder's init of its name is taken and the init said", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('./both')\n");
        const std::string file   = s.write("proj/both.luau", "return 'file'\n");
        s.write("proj/both/init.luau", "return 'init'\n");
        s.write("proj/alone/init.luau", "return 'alone'\n");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("proj") });
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        ALPreprocessor::Include       found;
        std::vector<std::string>      aliases;
        CHECK(search.resolve(ask("./both", true, own.self), found, own, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes);
        CHECK(found.path == ALIncludeIdentity::ofFile(file));
        UNSCOPED_INFO("the init passed over, by its path from the folder both are in");
        CHECK(found.passedOver == std::string("both/init.luau"));
        UNSCOPED_INFO("as the file taken is, by its name");
        CHECK(found.name == std::string("both.luau"));
        ALPreprocessor::Include alone;
        {
            INFO("a folder's init alone");
            CHECK(search.resolve(ask("./alone", true, own.self), alone, own, disk, nullptr, false, &aliases) ==
                                            ALPreprocessor::Found::Yes);
            REQUIRE(alone.passedOver.empty());
        }
        ALPreprocessor::Include named;
        {
            INFO("named with its extension: the file alone, nothing passed over");
            CHECK(search.resolve(ask("./both.luau", true, own.self), named, own, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes);
            REQUIRE(named.passedOver.empty());
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: why a require found nothing: Luau's navigator's words or studio rules", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('./nowhere')\n");
        s.write("proj/.luaurc", "{\"aliases\": {\"gone\": \"./gone\", \"c1\": \"@c2\", \"c2\": \"@c1\", \"lib\": \"./lib\"}}");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("proj") });
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        const auto why = [&](const std::string& name) {
            ALPreprocessor::Include  found;
            std::vector<std::string> aliases;
            search.resolve(alincludesearch_data::ask(name, true, own.self), found, own, disk, nullptr, false, &aliases);
            return found.why;
        };
        UNSCOPED_INFO("nothing so named");
        CHECK(why("./nowhere") == std::string("could not resolve child component \"nowhere\""));
        UNSCOPED_INFO("an alias nobody names");
        CHECK(why("@missing/x") == std::string("@missing is not a valid alias"));
        UNSCOPED_INFO("aliases in a circle");
        CHECK(why("@c1") == std::string("detected alias cycle (@c1 -> @c2 -> @c1)"));
        UNSCOPED_INFO("an alias whose folder is not there");
        CHECK(why("@gone/x") == std::string("the alias stands for './gone', which is not there"));
        REQUIRE(why("@sl-std/x").find("reserved") != std::string::npos);
        REQUIRE(why("@lib/../x").find("may not climb") != std::string::npos);

        // Said as a module not found, for what explains it.
        ALScriptProblem said;
        said.key  = "PreprocRequireNoChild";
        said.args = { "./nowhere", "nowhere" };
        ALScriptProblems problems{ said };
        ALIncludeSearch::Missing facts;
        facts.disk = false;
        ALIncludeSearch::explainMissing(problems, facts);
        {
            INFO("the disk not looked in");
            REQUIRE(problems.size() == size_t(1));
            CHECK(problems[0].key == "PreprocModuleNotOnDisk");
            CHECK(problems[0].args == std::vector<std::string>{ "./nowhere" });
        }
        UNSCOPED_INFO("left out");
        CHECK(ALIncludeSearch::leftOut({ said }).names == std::vector<std::string>{ "./nowhere" });
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: suggestions as a require is typed: aliases first; each module resolves", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('./util')\n");
        s.write("proj/util.luau", "return 1\n");
        s.write("proj/lib/net.luau", "return 1\n");
        s.write("proj/lib/http.lua", "return 1\n");
        s.write("proj/lib/sub/deep.luau", "return 1\n");
        s.write("proj/.hidden/x.luau", "return 1\n");
        s.write("proj/readme.txt", "words\n");
        s.write("proj/.luaurc", "{\"aliases\": {\"shared\": \"./lib\", \"sl-std\": \"./lib\"}}");
        s.write("tools/t.luau", "return 1\n");
        const std::string inc = s.write("proj/inc.lsl", "integer x;\n");
        s.write("proj/defs.lslh", "#define X 1\n");
        ALIncludeSearch::Where disk = where(false, true, { s.at("proj") });
        disk.aliases                = { { "tools", s.at("tools") } };
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        const auto labels = [&](const std::string& typed, bool require = true) {
            std::string out;
            for (const ALRequireNavigation::Suggestion& one : search.suggest(own.self, typed, require, own, disk))
            {
                out += (out.empty() ? "" : " ") + one.label + (one.folder ? "/" : "");
            }
            return out;
        };
        UNSCOPED_INFO("first: the aliases in reach, none reserved, then ./ and ../");
        CHECK(labels("") == std::string("@shared/ @tools/ .// ..//"));
        UNSCOPED_INFO("the file's own folder: modules and folders, none hidden, nothing else");
        CHECK(labels("./") == std::string("../ lib/ main util"));
        UNSCOPED_INFO("a name part typed: the same, for the editor to narrow");
        CHECK(labels("./li") == labels("./"));
        UNSCOPED_INFO("in a folder: its own, and up");
        CHECK(labels("./lib/") == std::string("../ http net sub/"));
        UNSCOPED_INFO("a name part typed there");
        CHECK(labels("./lib/ne") == labels("./lib/"));
        UNSCOPED_INFO("through a .luaurc's alias, which may not climb: no way up");
        CHECK(labels("@shared/") == std::string("http net sub/"));
        UNSCOPED_INFO("nor further in");
        CHECK(labels("@shared/sub/") == std::string("deep"));
        UNSCOPED_INFO("through the studio's");
        CHECK(labels("@tools/") == std::string("t"));
        UNSCOPED_INFO("a reserved alias walks nowhere");
        CHECK(labels("@sl-std/") == std::string());

        // Every module offered is what a require of it finds.
        for (const std::string& typed : { std::string("./"), std::string("./lib/"), std::string("./lib/sub/"), std::string("@shared/"), std::string("@tools/") })
        {
            for (const ALRequireNavigation::Suggestion& one : search.suggest(own.self, typed, true, own, disk))
            {
                if (one.folder || one.label == "..")
                {
                    continue;
                }
                ALPreprocessor::Include  found;
                std::vector<std::string> aliases;
                CAPTURE(one.path);
                REQUIRE(search.resolve(ask(one.path, true, own.self), found, own, disk, nullptr, false, &aliases) ==
                                                    ALPreprocessor::Found::Yes);
            }
        }
        const std::vector<ALRequireNavigation::Suggestion> in_lib = search.suggest(own.self, "./lib/", true, own, disk);
        REQUIRE(in_lib.size() > 1);
        UNSCOPED_INFO("each one's whole path");
        CHECK(in_lib[1].path == std::string("./lib/http"));

        // An include's: the file's own folder, by its search, names whole --
        // an include's own `.lslh` and `.lsli` as well as `.lsl`.
        const ALIncludeSearch::Asking lsl{ ALIncludeIdentity::ofFile(inc), false };
        const auto                    included = [&](const std::string& typed) {
            std::string said;
            for (const ALRequireNavigation::Suggestion& one : search.suggest(lsl.self, typed, false, lsl, disk))
            {
                said += (said.empty() ? "" : " ") + one.path + (one.folder ? "/" : "");
            }
            return said;
        };
        UNSCOPED_INFO("an LSL include's");
        CHECK(included("") == std::string("defs.lslh inc.lsl lib/"));
        UNSCOPED_INFO("in a folder under it");
        CHECK(included("lib/") == std::string("lib/sub/"));
        // Only under the folders the search looks in, however it is typed:
        // not up out of them, nor from a root elsewhere, nor another
        // machine's share.
        UNSCOPED_INFO("not up out of them");
        CHECK(included("../") == std::string());
        UNSCOPED_INFO("nor from a root elsewhere");
        CHECK(included(fsyspath(s.root).string() + "/") == std::string());
        UNSCOPED_INFO("nor a share");
        CHECK(included("\\\\alincludesearch.invalid\\share\\") == std::string());
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: a .config.luau is read as the .luaurc it returns; beside one neither", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/src/main.luau", "return require('@lib/net')\n");
        const std::string net    = s.write("proj/modules/net.luau", "return 1\n");
        const std::string luau   = s.write("proj/.config.luau", "local root = './modules'\nreturn { luau = { languagemode = 'strict', aliases = { lib = root } } }\n");
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        // Each step's disk read again, as a change of settings has it.
        U32        generation = 0;
        const auto disk       = [&]() {
            ALIncludeSearch::Where out = alincludesearch_data::where(false, true, { s.at("proj") });
            out.generation             = ++generation;
            return out;
        };
        const auto resolved = [&](const std::string& name, std::string* why = nullptr) {
            ALPreprocessor::Include  found;
            std::vector<std::string> aliases;
            const bool yes = search.resolve(alincludesearch_data::ask(name, true, own.self), found, own, disk(), nullptr, false, &aliases) == ALPreprocessor::Found::Yes;
            if (why)
            {
                *why = found.why;
            }
            return yes ? found.path : std::string();
        };
        const auto chain = [&]() {
            std::vector<ALIncludeSearch::Config> out;
            search.configsFor(own.self, own, disk(), nullptr, false, out);
            std::string said;
            for (const ALIncludeSearch::Config& one : out)
            {
                std::string file;
                ALIncludeIdentity::fileOf(one.path, file);
                said += (said.empty() ? "" : " ") + fsyspath(fs::path(fsyspath(file)).filename()).string();
            }
            return said;
        };
        const auto suggested = [&]() {
            std::string out;
            for (const ALRequireNavigation::Suggestion& one : search.suggest(own.self, "", true, own, disk()))
            {
                out += (out.empty() ? "" : " ") + one.label;
            }
            return out;
        };
        ALLuauConfig config;

        UNSCOPED_INFO("through its alias");
        CHECK(resolved("@lib/net") == ALIncludeIdentity::ofFile(net));
        UNSCOPED_INFO("in the chain by its own name");
        CHECK(chain() == std::string(".config.luau"));
        {
            INFO("its mode the analyzer's");
            REQUIRE(search.configOf(own, disk(), config));
            CHECK(config.mode == "strict");
        }
        UNSCOPED_INFO("its alias suggested");
        CHECK(suggested() == std::string("@lib ./ ../"));

        // A nearer .luaurc over it, as Luau reads a chain.
        s.write("proj/src/.luaurc", "{\"languageMode\": \"nonstrict\"}");
        UNSCOPED_INFO("both, the nearer first");
        CHECK(chain() == std::string(".luaurc .config.luau"));
        {
            INFO("the nearer's mode");
            CHECK(search.configOf(own, disk(), config));
            CHECK(config.mode == "nonstrict");
        }
        UNSCOPED_INFO("the further one's alias still");
        CHECK(resolved("@lib/net") == ALIncludeIdentity::ofFile(net));

        // Both in one folder: neither, as Luau reads it.
        const std::string rc = s.write("proj/.luaurc", "{\"aliases\": {\"lib\": \"./modules\"}}");
        std::string       why;
        UNSCOPED_INFO("both: not found");
        REQUIRE(resolved("@lib/net", &why).empty());
        UNSCOPED_INFO("in Luau's words");
        CHECK(why == std::string("could not resolve alias \"lib\" (ambiguous configuration file)"));
        UNSCOPED_INFO("passed over in the chain");
        CHECK(chain() == std::string(".luaurc"));
        UNSCOPED_INFO("nor suggested");
        CHECK(suggested() == std::string("./ ../"));
        std::error_code ec;
        fs::remove(fsyspath(rc), ec);

        // One that does not run: an alias walked past it stops there, as
        // Luau's navigator stops at a .luaurc that does not parse, and says
        // why; passed over in the chain the analyzer reads, as such a
        // .luaurc is.
        s.write("proj/.config.luau", "error('broken')\n");
        UNSCOPED_INFO("broken: its alias gone");
        REQUIRE(resolved("@lib/net", &why).empty());
        {
            INFO("said why, naming it: " << why);
            CHECK(why.find("the configuration '") == 0);
            REQUIRE(why.find(".config.luau' could not be read: ") != std::string::npos);
        }
        UNSCOPED_INFO("nor suggested past it");
        CHECK(suggested() == std::string("./ ../"));
        UNSCOPED_INFO("not in the chain");
        CHECK(chain() == std::string(".luaurc"));

        // A script in the world, world includes off: the one at the top of
        // the scripter's include folder governs it.
        s.write("inc/.config.luau", "return { luau = { aliases = { top = './libs' } } }\n");
        const std::string            x   = s.write("inc/libs/x.luau", "return 'x'\n");
        const ALIncludeSearch::Where off = where(false, true, { s.at("inc") });
        ALPreprocessor::Include      found;
        std::vector<std::string>     aliases;
        {
            INFO("a world script, through the top's alias");
            CHECK(search.resolve(ask("@top/x", true, asking(true).self), found, asking(true), off, nullptr, false, &aliases) == ALPreprocessor::Found::Yes);
            CHECK(found.path == ALIncludeIdentity::ofFile(x));
        }
        std::vector<ALIncludeSearch::Config> top;
        {
            INFO("and its chain");
            CHECK(search.configsFor(asking(true).self, asking(true), off, nullptr, false, top) == ALPreprocessor::Found::Yes);
            REQUIRE(top.size() == 1);
            REQUIRE(top[0].text.find("\"top\": \"./libs\"") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: a studio alias to an inventory folder is walked only with the world in", "[alincludesearch]")
    {
        // An inventory folder of modules, by the id the world knows it by.
        const auto add = [this](const std::string& folder, const std::string& name) {
            const ALIncludeWorld::Item one{ ALIncludeIdentity::ofItem(LLUUID::null, LLUUID::generateNewID()), name, LLUUID::generateNewID() };
            world.folders[folder].items.push_back(one);
            world.itemFolders[one.path] = folder;
            return one;
        };
        world.folders["libfolder"].parent  = std::string();
        world.folders["net"].parent        = "libfolder";
        world.folders["libfolder"].folders = { { "net", "net" } };
        const ALIncludeWorld::Item util = add("libfolder", "util");
        const ALIncludeWorld::Item http = add("net", "http");
        texts.put(util.path, util.assetId, "return 'util'\n");
        texts.put(http.path, http.assetId, "return 'http'\n");

        ALIncludeSearch::Where on = where(true, false);
        on.aliases                = { { "inv", "libfolder" } };
        ALIncludeSearch::Where off = on;
        off.world                  = false;
        const ALIncludeSearch::Asking in_object = asking(true);
        const auto found = [&](const std::string& name, const ALIncludeSearch::Where& in) {
            ALPreprocessor::Include  include;
            std::vector<std::string> aliases;
            return search.resolve(alincludesearch_data::ask(name, true, in_object.self), include, in_object, in, nullptr, false, &aliases) == ALPreprocessor::Found::Yes
                       ? include.path
                       : std::string();
        };
        UNSCOPED_INFO("through the alias, the world let in");
        CHECK(found("@inv/util", on) == util.path);
        UNSCOPED_INFO("a folder under it");
        CHECK(found("@inv/net/http", on) == http.path);
        UNSCOPED_INFO("the world not let in: nothing");
        CHECK(found("@inv/util", off) == std::string());
        const auto offered = [&](const ALIncludeSearch::Where& in) {
            std::string out;
            for (const ALRequireNavigation::Suggestion& one : search.suggest(in_object.self, "", true, in_object, in))
            {
                out += (out.empty() ? "" : " ") + one.label;
            }
            return out;
        };
        UNSCOPED_INFO("offered while the world is let in: " << offered(on));
        REQUIRE(offered(on).find("@inv") != std::string::npos);
        UNSCOPED_INFO("not otherwise: " << offered(off));
        REQUIRE(offered(off).find("@inv") == std::string::npos);

        // Beside a folder on disk, which is read only while the disk is.
        Scratch           s;
        const std::string disk_util = s.write("library/util.luau", "return 'disk'\n");
        ALIncludeSearch::Where both = where(true, true);
        both.aliases                = { { "inv", "libfolder" }, { "lib", s.at("library") } };
        UNSCOPED_INFO("each its own");
        CHECK(found("@lib/util", both) == ALIncludeIdentity::ofFile(disk_util));
        UNSCOPED_INFO("and the world's");
        CHECK(found("@inv/util", both) == util.path);
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: written paths reach only folders let in and never a share or device", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return 1\n");
        const std::string util   = s.write("proj/lib/util.luau", "return 'util'\n");
        s.write("abroad/e.luau", "return 'e'\n");
        // Each written with `/`, which a configuration's text keeps as it is.
        const auto forward = [](std::string path) {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        };
        const std::string lib    = forward(s.at("proj/lib"));
        const std::string abroad = forward(s.at("abroad"));
        s.write("proj/.luaurc", "{\"aliases\": {\"own\": \"" + lib + "\", \"share\": \"//alincludesearch.invalid/share\", "
                                "\"nt\": \"/?\?/UNC/alincludesearch.invalid/share\", \"ntlocal\": \"/?\?/" + lib + "\", \"abroad\": \"" + abroad +
                                "\", \"sl-std\": \"./lib\"}}");
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        const auto found = [&](const std::string& name, const ALIncludeSearch::Where& in, std::string* why = nullptr) {
            ALPreprocessor::Include  include;
            std::vector<std::string> aliases;
            const bool yes = search.resolve(alincludesearch_data::ask(name, true, own.self), include, own, in, nullptr, false, &aliases) == ALPreprocessor::Found::Yes;
            if (why)
            {
                *why = include.why;
            }
            return yes ? include.path : std::string();
        };

        // Opened from the disk, in no include folder: what its configuration
        // names from a root under its own folder is all it reaches.
        const ALIncludeSearch::Where loose = where(false, true);
        UNSCOPED_INFO("from a root, under the configuration's own folder");
        CHECK(found("@own/util", loose) == ALIncludeIdentity::ofFile(util));
        UNSCOPED_INFO("not a require's own path from a root, which no folder lets in");
        CHECK(found(util, loose) == std::string());

        const ALIncludeSearch::Where disk = where(false, true, { s.at("proj") });
        std::string                  why;
        // What an alias names out of every folder it may: said so, and the
        // disk not asked to say it.
        const auto outside = [&s](const std::string& named) {
            return "the configuration in '" + s.at("proj") + "' names '" + named + "', which is outside it and outside the include folders";
        };
        UNSCOPED_INFO("under an include folder");
        CHECK(found(util, disk) == ALIncludeIdentity::ofFile(util));
        {
            INFO("an alias naming another machine's share: not asked");
            REQUIRE(found("@share/x", disk, &why).empty());
            CHECK(why == outside("//alincludesearch.invalid/share"));
        }
        {
            INFO("nor by the NT namespace's name for one");
            REQUIRE(found("@nt/x", disk, &why).empty());
            CHECK(why == outside("/?\?/UNC/alincludesearch.invalid/share"));
        }
        {
            INFO("nor its name for a local folder");
            REQUIRE(found("@ntlocal/util", disk, &why).empty());
            CHECK(why == outside("/?\?/" + lib));
        }
        {
            INFO("a folder that is there, out of every folder it may name: not said to be missing");
            REQUIRE(found("@abroad/e", disk, &why).empty());
            CHECK(why == outside(abroad));
        }
        {
            INFO("a require naming one");
            REQUIRE(found("//alincludesearch.invalid/share/x", disk, &why).empty());
            CHECK(why == "could not find '//alincludesearch.invalid/share/x'");
        }
        {
            INFO("in Windows's separators");
            REQUIRE(found("\\\\alincludesearch.invalid\\share\\x", disk).empty());
            REQUIRE(found("\\??\\UNC\\alincludesearch.invalid\\share\\x", disk).empty());
        }
        {
            INFO("a device's path to a file under the folder");
            REQUIRE(found("\\\\?\\" + util, disk).empty());
            REQUIRE(found("\\\\.\\" + util, disk).empty());
            REQUIRE(found("\\??\\" + util, disk).empty());
        }
        {
            INFO("another drive's folder by a part of a name");
            REQUIRE(found("./D:/x", disk, &why).empty());
            CHECK(why == "could not resolve child component \"D:\"");
        }
        ALPreprocessor::Include include;
        UNSCOPED_INFO("nor an LSL include naming a share");
        CHECK(search.resolve(ask("//alincludesearch.invalid/share/x.lsl"), include, asking(), disk, nullptr, false) ==
                                                        ALPreprocessor::Found::No);
        // Nor are those modules' folders, for what a script may name; nor
        // an alias Second Life keeps.
        std::string prefixes;
        for (const auto& [prefix, folder] : search.moduleFolders(own, disk))
        {
            prefixes += prefix + " ";
        }
        UNSCOPED_INFO("the module folders: the scripter's, and the alias's that may be read");
        CHECK(prefixes == std::string(" @own/ "));

        // A configuration's folder reached through a link names its own
        // folder as it stands, and is let in so.
        const auto reaches = [&](const std::string& link)
        {
            const std::string             through = ALIncludeIdentity::ofFile(s.at(link + "/main.luau"));
            const ALIncludeSearch::Asking linked{ through, true };
            ALPreprocessor::Include       reached;
            std::vector<std::string>      aliases;
            CHECK(search.resolve(ask("@own/util", true, through), reached, linked, loose, nullptr, false, &aliases) ==
                                     ALPreprocessor::Found::Yes);
            CHECK(reached.path == ALIncludeIdentity::ofFile(util));
        };
#if LL_WINDOWS
        {
            // Windows' own link to a folder, which any user may make, so it
            // is tried on every Windows machine, where a symlink may not be.
            INFO("through a junction");
            REQUIRE(!ll_test::createDirectoryJunction(s.root / "proj", s.root / "junction"));
            reaches("junction");
        }
#endif
        std::error_code ec;
        fs::create_directory_symlink(s.root / "proj", s.root / "linked", ec);
        if (ec)
        {
            SKIP("no symlinks here: " << ec.message());
        }
        INFO("through a link");
        reaches("linked");
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: why a require that reached a file found nothing or stopped at a config", "[alincludesearch]")
    {
        Scratch           s;
        const std::string loose = s.write("loose/main.luau", "return 1\n");
        s.write("loose/sibling.luau", "return 1\n");
        s.write("loose/.luaurc", "{\"aliases\": {\"out\": \"../outside\"}}");
        s.write("outside/x.luau", "return 1\n");
        s.write("inc/empty.luau", "return 1\n");
        const auto why = [&](const std::string& name, const std::string& from, const ALIncludeSearch::Where& in) {
            const ALIncludeSearch::Asking who{ from, true };
            ALPreprocessor::Include       include;
            std::vector<std::string>      aliases;
            const ALPreprocessor::Found   said = search.resolve(alincludesearch_data::ask(name, true, from), include, who, in, nullptr, false, &aliases);
            return said == ALPreprocessor::Found::Yes ? std::string("found") : include.why;
        };
        const ALIncludeSearch::Where disk = where(false, true, { s.at("inc") });
        UNSCOPED_INFO("a file beside one opened from the disk, in no include folder");
        CHECK(why("./sibling", ALIncludeIdentity::ofFile(loose), disk) ==
                      "'" + s.at("loose/sibling.luau") + "' is there, but no include folder or alias lets a require read it");
        UNSCOPED_INFO("an alias reaching out of its configuration's folder");
        CHECK(why("@out/x", ALIncludeIdentity::ofFile(loose), disk) ==
                      "the configuration in '" + s.at("loose") + "' names '" + s.at("outside") +
                          "', which is outside it and outside the include folders");

        // A configuration that does not parse ends a walk for an alias past
        // it, as Luau's navigator's ends: the farther one's alias is not
        // reached through it.
        s.write("proj/.luaurc", "{\"aliases\": {\"lib\": \"./lib\"}}");
        s.write("proj/lib/x.luau", "return 1\n");
        s.write("proj/src/.luaurc", "{not json");
        s.write("proj/src/near.luau", "return 1\n");
        const std::string            inner = ALIncludeIdentity::ofFile(s.write("proj/src/main.luau", "return 1\n"));
        const ALIncludeSearch::Where both  = where(false, true, { s.at("inc"), s.at("proj") });
        const std::string            said  = why("@lib/x", inner, both);
        UNSCOPED_INFO("not reached through it: " << said);
        CHECK(said.find("the configuration '" + s.at("proj/src/.luaurc") + "' could not be read: ") == 0);
        UNSCOPED_INFO("a path walks as ever");
        CHECK(why("./near", inner, both) == std::string("found"));

        // A world .luaurc's alias naming the disk: why it is not walked.
        const ALIncludeWorld::Item main{ ALIncludeIdentity::ofItem(LLUUID::null, LLUUID::generateNewID()), "main", LLUUID::generateNewID() };
        const ALIncludeWorld::Item config{ ALIncludeIdentity::ofItem(LLUUID::null, LLUUID::generateNewID()), ".luaurc", LLUUID::generateNewID() };
        world.folders["wproj"].parent = std::string();
        world.folders["wproj"].items  = { main, config };
        world.itemFolders[main.path]  = "wproj";
        texts.put(config.path, config.assetId, "{\"aliases\": {\"far\": \"/somewhere/on/disk\"}}");
        UNSCOPED_INFO("a world configuration naming the disk");
        CHECK(why("@far/x", main.path, where(true, true, { s.at("inc") })) ==
                      std::string("the alias stands for '/somewhere/on/disk', a path on disk, which a configuration in the world may not name"));
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: suggestions in an object with the world off: the top .luaurc's aliases", "[alincludesearch]")
    {
        Scratch s;
        s.write("inc/.luaurc", "{\"aliases\": {\"top\": \"./libs\"}}");
        s.write("inc/libs/x.luau", "return 1\n");
        s.write("inc/libs/selfish.luau", "return require('@self/child')\n");
        s.write("inc/libs/selfish/child.luau", "return 1\n");
        const ALIncludeSearch::Where  off       = where(false, true, { s.at("inc") });
        const ALIncludeSearch::Asking in_object = asking(true);
        const auto labels = [&](const std::string& typed) {
            std::string out;
            for (const ALRequireNavigation::Suggestion& one : search.suggest(in_object.self, typed, true, in_object, off))
            {
                out += (out.empty() ? "" : " ") + one.label + (one.folder ? "/" : "");
            }
            return out;
        };
        UNSCOPED_INFO("the top's alias, then ./ and ../");
        CHECK(labels("") == std::string("@top/ .// ..//"));
        UNSCOPED_INFO("a module and its folder, each");
        CHECK(labels("@top/") == std::string("selfish selfish/ x"));
        UNSCOPED_INFO("into the folder");
        CHECK(labels("@top/selfish/") == std::string("child"));
        ALPreprocessor::Include  found;
        std::vector<std::string> aliases;
        UNSCOPED_INFO("and walked so");
        CHECK(search.resolve(ask("@top/selfish/child", true, in_object.self), found, in_object, off, nullptr, false, &aliases) ==
                                    ALPreprocessor::Found::Yes);
    }

    TEST_CASE_METHOD(alincludesearch_data, "alincludesearch: an alias to a module lets in its file alone; one to @sl- walks nowhere", "[alincludesearch]")
    {
        Scratch           s;
        const std::string script = s.write("loose/main.luau", "return 1\n");
        const std::string json   = s.write("loose/vendor/json.luau", "return 'json'\n");
        const std::string util   = s.write("loose/lib/util.luau", "return 'util'\n");
        s.write("loose/vendor/other.luau", "return 'other'\n");
        s.write("loose/.luaurc", "{\"aliases\": {\"json\": \"./vendor/json\", \"lib\": \"./lib\", \"sl-std\": \"./lib\", \"x\": \"@sl-std\"}}");
        s.write("inc/empty.luau", "return 1\n");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("inc") });
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        // One run's: what an alias blesses stays blessed for the rest of it.
        std::vector<std::string> run;
        const auto found = [&](const std::string& name, std::string* why = nullptr) {
            ALPreprocessor::Include include;
            const bool yes = search.resolve(alincludesearch_data::ask(name, true, own.self), include, own, disk, nullptr, false, &run) == ALPreprocessor::Found::Yes;
            if (why)
            {
                *why = include.why;
            }
            return yes ? include.path : std::string();
        };
        std::string why;
        UNSCOPED_INFO("an alias naming a module");
        CHECK(found("@json") == ALIncludeIdentity::ofFile(json));
        {
            INFO("blessed for the run, alone");
            REQUIRE(run.size() == 1);
            CHECK(run[0] == s.at("loose/vendor/json.luau"));
        }
        UNSCOPED_INFO("its file alone, the rest of the run: nothing beside it");
        CHECK(found("./vendor/other") == std::string());
        UNSCOPED_INFO("an alias naming a folder");
        CHECK(found("@lib/util") == ALIncludeIdentity::ofFile(util));
        {
            INFO("one naming an alias Second Life keeps: no configuration's");
            REQUIRE(found("@x/util", &why).empty());
            CHECK(why == "@sl-std is not a valid alias");
        }

        // A studio alias named after an include folder, where an alias of
        // the .luaurc at its top -- in reach of a script in an object -- has
        // that name already, and a require would go through it first.
        s.write("other/.luaurc", "{\"aliases\": {\"other\": \"./sub\"}}");
        s.write("other/sub/z.luau", "return 1\n");
        const std::string            solo  = s.write("other/solo.luau", "return 'solo'\n");
        const ALIncludeSearch::Where mine  = where(false, true, { s.at("other") });
        ALPreprocessor::Include      include;
        std::vector<std::string>     aliases;
        UNSCOPED_INFO("found nothing now");
        CHECK(search.resolve(ask("solo", true, asking(true).self), include, asking(true), mine, nullptr, false, &aliases) ==
                                        ALPreprocessor::Found::No);
        UNSCOPED_INFO("found before");
        CHECK(include.searched == solo);
        {
            INFO("named past the .luaurc's own");
            REQUIRE(include.moves.size() == 1);
            CHECK(include.moves[0].require == "@other2/solo");
            CHECK(include.moves[0].alias == "other2");
        }
    }
}

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

    template<> template<>
    void alincludesearch_object::test<5>()
    {
        set_test_name("a name not found said as why: the object's silence first, one in the world that is not taken from, the disk not looked in");
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
        ensure_equals("the silence said first", problems.front().key, std::string("PreprocObjectUnanswered"));
        ensure_equals("one in the world, not taken from", problems[1].key, std::string("PreprocIncludeInWorld"));
        ensure_equals("one nowhere, the disk looked in: as it was", problems[2].key, std::string("PreprocModuleNotFound"));

        problems = { missing("PreprocModuleNotFound", "util"), missing("PreprocIncludeNotFound", "x.lsl", "disk:/lib/a.lsl") };
        facts    = ALIncludeSearch::Missing();
        facts.disk      = true;
        facts.noFolders = true;
        ALIncludeSearch::explainMissing(problems, facts);
        ensure_equals("no folder of the scripter's: the disk not looked in", problems[0].key, std::string("PreprocModuleNotOnDisk"));
        ensure_equals("but one asked from a file on disk is not second-guessed", problems[1].key, std::string("PreprocIncludeNotFound"));
        ensure("said with its name", problems[0].message.find("'util'") != std::string::npos);
    }
    template<> template<>
    void alincludesearch_object::test<6>()
    {
        set_test_name("what a run left out: the names not found each once in order, the problems that said so, and whether a folder on disk would have let one in");
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
        ensure("each once, in order", left.names == std::vector<std::string>{ "b.lsl", "util" });
        ensure_equals("every problem that said so", left.problems, 3);
        ensure("found nowhere: nothing a folder would change", !left.diskRoute);

        left = ALIncludeSearch::leftOut({ said("PreprocIncludeInWorld", "a.lsl") });
        ensure("one in the world, which is not taken from: the disk's route", left.diskRoute && left.names.size() == 1);
        left = ALIncludeSearch::leftOut({ said("PreprocModuleNotOnDisk", "m") });
        ensure("the disk not looked in: the disk's route", left.diskRoute && left.names == std::vector<std::string>{ "m" });
        ensure("nothing said: nothing left out", ALIncludeSearch::leftOut({}).names.empty());
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
    // search before LA15 is kept beside each, for what changed.
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

    template<> template<>
    void alincludesearch_object::test<7>()
    {
        set_test_name("the require parity suite: each case from Luau's tests, the plugin's and ours, as LAD9 picks, beside what the search found before");
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

        const std::string                   ABSOLUTE   = s.at("proj/lw/dependency.luau");
        const std::string                   UNBLESSED  = s.at("outside/secret.luau");
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
            { "plugin: an absolute path, in a blessed folder (Luau RequireAbsolutePath refuses)", "proj/lw/module.luau", ABSOLUTE.c_str(),
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
        const std::string            root = s.root.generic_string() + "/";
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
                under = fsyspath(file).generic_string();
                under = under.compare(0, root.size(), root) == 0 ? under.substr(root.size()) : under;
            }
            ensure_equals(std::string(one.what) + ": as LAD9 has it", under, std::string(one.chosen));
            // A case where all agree was found so before.
            if (!*one.rule)
            {
                ensure_equals(std::string(one.what) + ": as before", std::string(one.chosen), std::string(one.before));
            }
        }
    }

    template<> template<>
    void alincludesearch_object::test<8>()
    {
        set_test_name("a require's path in the navigator's terms: no prefix is ./; @self from beside the file; @sl-* reserved; an alias may not climb");
        const auto path = [](const std::string& name, const std::string& stem = "main") { return ALRequireNavigation::navigatorPath(name, stem); };
        ensure_equals("no prefix", path("util").path, std::string("./util"));
        ensure_equals("a folder in it", path("lib/util").path, std::string("./lib/util"));
        ensure_equals("./ as it is", path("./util").path, std::string("./util"));
        ensure_equals("../ as it is", path("../util").path, std::string("../util"));
        ensure_equals("a separator of Windows's", path("lib\\util").path, std::string("./lib/util"));
        ensure_equals("an alias as it is", path("@lib/util").path, std::string("@lib/util"));
        ensure_equals("@self from a file: the folder of its name", path("@self/child").path, std::string("./main/child"));
        ensure_equals("@self from a folder's init: beside it", path("@self/child", "init").path, std::string("./child"));
        ensure_equals("@self alone", path("@self").path, std::string("./main"));
        ensure_equals("@SELF in any case", path("@SELF/child", "init").path, std::string("./child"));
        ensure("@sl-* reserved", !path("@sl-std/util").error.empty() && !path("@SL-std").error.empty());
        ensure("an alias may not climb", !path("@lib/../secret").error.empty() && !path("@lib/a/../../b").error.empty());
        ensure("@self may, as Luau's own", path("@self/../x").error.empty());
        ensure("a name with .. in it is no climb", path("@lib/a..b").error.empty());
    }

    template<> template<>
    void alincludesearch_object::test<9>()
    {
        set_test_name("a require walked through the world: an inventory folder's items and folders, its .luaurc notecard, an object's contents; Pending where the world has not said; nothing of it with world includes off but the scripter's folders' own .luaurc");
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
            const ALPreprocessor::Found said = search.resolve(ask(name, true, own.self), include, own, in, wanted, false, &aliases);
            return said == ALPreprocessor::Found::Yes ? include.path : said == ALPreprocessor::Found::Pending ? std::string("pending") : std::string();
        };
        ensure_equals("beside it", found("./util", on), util.path);
        ensure_equals("with no prefix", found("util", on), util.path);
        ensure_equals("up a folder", found("../shared", on), shared.path);
        ensure_equals("a folder's init", found("./mod", on), init.path);
        ensure_equals("in a folder", found("./lib/net", on), net.path);
        ensure_equals("through the folder's .luaurc", found("@lib/net", on), net.path);
        ensure_equals("one there is not", found("./nothere", on), std::string());
        ensure_equals("a world .luaurc names nothing on disk", found("@far/x", on), std::string());
        ensure_equals("past the world's top, the scripter's folders' own .luaurc", found("@top/x", on), ALIncludeIdentity::ofFile(x));

        // What the world has not said yet: Pending, and what is on its way
        // wanted.
        ALIncludeSearch::wanted_t wanted;
        world.folders["lib"].known = false;
        ensure_equals("a folder not known: Pending", found("./lib/net", on, &wanted), std::string("pending"));
        world.folders["lib"].known = true;
        ALScriptTextCache         fresh;
        ALIncludeSearch           unread(fresh, world);
        ALPreprocessor::Include   include;
        std::vector<std::string>  aliases;
        ensure("the .luaurc not in hand: Pending", unread.resolve(ask("@lib/net", true, own.self), include, own, on, &wanted, false, &aliases) ==
                                                       ALPreprocessor::Found::Pending);
        ensure("and wanted", wanted.contains(config.path));

        // With world includes off, nothing of the world; the scripter's
        // folders' own .luaurc still.
        ensure_equals("off: not beside it", found("./util", off), std::string());
        ensure_equals("off: no .luaurc of the world's", found("@lib/net", off), std::string());
        ensure_equals("off: the scripter's folders' own", found("@top/x", off), ALIncludeIdentity::ofFile(x));

        // An object's contents, one folder.
        world.folders["contents"].parent = std::string();
        const ALIncludeWorld::Item script = add("contents", "script");
        const ALIncludeWorld::Item helper = add("contents", "helper");
        texts.put(helper.path, helper.assetId, "return 'helper'\n");
        const ALIncludeSearch::Asking in_object{ script.path, true };
        ALPreprocessor::Include       got;
        ensure("an object's: beside it", search.resolve(ask("./helper", true, script.path), got, in_object, on, nullptr, false, &aliases) ==
                                             ALPreprocessor::Found::Yes &&
                                             got.path == helper.path);
        ensure("an object has no folder above", search.resolve(ask("../helper", true, script.path), got, in_object, on, nullptr, false, &aliases) ==
                                                    ALPreprocessor::Found::No);
    }

    template<> template<>
    void alincludesearch_object::test<10>()
    {
        set_test_name("studio aliases: a script in an object reaches a library on disk through one, world includes off; a project's own .luaurc wins; naming blesses only for a require");
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
            return search.resolve(ask(name, true, who.self), include, who, in, nullptr, false, &aliases) == ALPreprocessor::Found::Yes ? include.path
                                                                                                                                       : std::string();
        };
        ensure_equals("an object's script, through the studio's alias", found("@lib/util", in_object, disk), ALIncludeIdentity::ofFile(util));
        ensure_equals("in any case", found("@LIB/util", in_object, disk), ALIncludeIdentity::ofFile(util));
        ensure_equals("one nobody named", found("@other/util", in_object, disk), std::string());
        ALIncludeSearch::Where unnamed = disk;
        unnamed.aliases.clear();
        ensure_equals("unnamed: not read, the folder blessed by nothing", found("@lib/util", in_object, unnamed), std::string());
        ALIncludeSearch::Where off = disk;
        off.disk                   = false;
        ensure_equals("the disk off: nothing", found("@lib/util", in_object, off), std::string());

        // A project's own `.luaurc` names the alias first.
        const ALIncludeSearch::Asking from_disk{ ALIncludeIdentity::ofFile(script), true };
        ensure_equals("the project's own alias", found("@shared/util", from_disk, disk), ALIncludeIdentity::ofFile(own));
        ensure_equals("where it names none, the studio's", found("@shared/util", in_object, disk), ALIncludeIdentity::ofFile(util));

        // Naming blesses the folder for a require, and adds nothing to an
        // include's search.
        ALPreprocessor::Include include;
        ensure("an LSL include does not search an alias's folder",
               search.resolve(ask("helper.lsl"), include, asking(), disk, nullptr, false) == ALPreprocessor::Found::No);
    }

    template<> template<>
    void alincludesearch_object::test<11>()
    {
        set_test_name("a require the search found before, and finds nothing now: where it found it, and each way to say it now that finds that very file");
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
            ensure(name + ": found nothing now", search.resolve(ask(name, true, who.self), include, who, disk, nullptr, false, &aliases) ==
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
        ensure_equals("found under the file's own folder", include.searched, under);
        ensure_equals("written from beside it", said(include), std::string("./lib/util2"));

        include = missed("util", from_disk);
        ensure_equals("found in another include folder", include.searched, util);
        ensure_equals("through the .luaurc's alias that reaches it", said(include), std::string("@shared/util"));

        include = missed("util", in_object);
        ensure_equals("from an object: through the alias of the .luaurc at the top of an include folder", said(include), std::string("@shared/util"));
        include = missed("solo", in_object);
        ensure_equals("from an object, where none reaches it", include.searched, solo);
        ensure_equals("through a studio alias of the folder that holds it, named after it", said(include), std::string("@other/solo +@other"));
        ensure_equals("the folder named", include.moves[0].folder, s.at("other"));
        include = missed("deep/net", in_object);
        ensure("a folder in it", include.searched == deep && said(include) == "@other/deep/net +@other");

        include = missed("nowhere", from_disk);
        ensure("nowhere before either", include.searched.empty() && include.moves.empty());
        include = missed("@missing/util", from_disk);
        ensure("an alias's is no searched name", include.searched.empty());
    }

    template<> template<>
    void alincludesearch_object::test<12>()
    {
        set_test_name("a file and a folder's init of one name: the file taken, the init passed over said; either alone, nothing to say");
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('./both')\n");
        const std::string file   = s.write("proj/both.luau", "return 'file'\n");
        const std::string init   = s.write("proj/both/init.luau", "return 'init'\n");
        s.write("proj/alone/init.luau", "return 'alone'\n");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("proj") });
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        ALPreprocessor::Include       found;
        std::vector<std::string>      aliases;
        ensure("found", search.resolve(ask("./both", true, own.self), found, own, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes);
        ensure_equals("the file", found.path, ALIncludeIdentity::ofFile(file));
        ensure_equals("the init passed over", found.passedOver, init);
        ALPreprocessor::Include alone;
        ensure("a folder's init alone", search.resolve(ask("./alone", true, own.self), alone, own, disk, nullptr, false, &aliases) ==
                                            ALPreprocessor::Found::Yes &&
                                            alone.passedOver.empty());
        ALPreprocessor::Include named;
        ensure("named with its extension: the file alone, nothing passed over",
               search.resolve(ask("./both.luau", true, own.self), named, own, disk, nullptr, false, &aliases) == ALPreprocessor::Found::Yes &&
                   named.passedOver.empty());
    }

    template<> template<>
    void alincludesearch_object::test<13>()
    {
        set_test_name("why a require found nothing, in Luau's navigator's words or the studio's rules'; said as a module not found, and as the disk not looked in where it was not");
        Scratch           s;
        const std::string script = s.write("proj/main.luau", "return require('./nowhere')\n");
        s.write("proj/.luaurc", "{\"aliases\": {\"gone\": \"./gone\", \"c1\": \"@c2\", \"c2\": \"@c1\", \"lib\": \"./lib\"}}");
        const ALIncludeSearch::Where  disk = where(false, true, { s.at("proj") });
        const ALIncludeSearch::Asking own{ ALIncludeIdentity::ofFile(script), true };
        const auto why = [&](const std::string& name) {
            ALPreprocessor::Include  found;
            std::vector<std::string> aliases;
            search.resolve(ask(name, true, own.self), found, own, disk, nullptr, false, &aliases);
            return found.why;
        };
        ensure_equals("nothing so named", why("./nowhere"), std::string("could not resolve child component \"nowhere\""));
        ensure_equals("an alias nobody names", why("@missing/x"), std::string("@missing is not a valid alias"));
        ensure_equals("aliases in a circle", why("@c1"), std::string("detected alias cycle (@c1 -> @c2 -> @c1)"));
        ensure_equals("an alias whose folder is not there", why("@gone/x"), std::string("the alias stands for './gone', which is not there"));
        ensure("reserved", why("@sl-std/x").find("reserved") != std::string::npos);
        ensure("climbing", why("@lib/../x").find("may not climb") != std::string::npos);

        // Said as a module not found, for what explains it.
        ALScriptProblem said;
        said.key  = "PreprocRequireNoChild";
        said.args = { "./nowhere", "nowhere" };
        ALScriptProblems problems{ said };
        ALIncludeSearch::Missing facts;
        facts.disk = false;
        ALIncludeSearch::explainMissing(problems, facts);
        ensure("the disk not looked in", problems[0].key == "PreprocModuleNotOnDisk" && problems[0].args == std::vector<std::string>{ "./nowhere" });
        ensure("left out", ALIncludeSearch::leftOut({ said }).names == std::vector<std::string>{ "./nowhere" });
    }
}

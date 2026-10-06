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
    // are by default (LAD10). Studio aliases (LA22) come with LA22.
    struct RequireCase
    {
        // What it shows, and whose case it is.
        const char* what;
        // The file asking, under the tree; "object" for a script in an
        // object, which with world includes off reads nothing of the world.
        const char* from;
        const char* name;
        // What it finds, under the tree, or nothing: now, and as LAD9 has it.
        const char* now;
        const char* chosen;
        // Whose rule LAD9 picks, where the references or we disagree: and
        // what Luau's navigator answers, where it differs from the choice.
        const char* rule;
        const char* luau;
    };

    template<> template<>
    void alincludesearch_object::test<7>()
    {
        set_test_name("the require parity suite: each case from Luau's tests, the plugin's and ours, as it resolves now, beside what LAD9 picks");
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
            ensure_equals(std::string(one.what) + ": now", under, std::string(one.now));
            // A case where all agree is found as LAD9 has it already.
            if (!*one.rule)
            {
                ensure_equals(std::string(one.what) + ": as LAD9 has it", std::string(one.chosen), std::string(one.now));
            }
        }
    }
}

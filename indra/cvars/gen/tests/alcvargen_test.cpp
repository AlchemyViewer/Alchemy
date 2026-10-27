/**
 * @file alcvargen_test.cpp
 * @brief alcvargen: broken declarations failing at their line, headers that
 *        change only with keys, the changes since a release, and --where.
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

#include "alcvargen.h"
#include "alcvargenjson.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;

    // A directory of the case's own, removed as it ends.
    class Scratch
    {
    public:
        Scratch()
        {
            static std::atomic<int> count{ 0 };
            mPath = fs::temp_directory_path() /
                    ("alcvargen_test_" + std::to_string(std::random_device()()) + "_" + std::to_string(count++));
            fs::remove_all(mPath);
            fs::create_directories(mPath);
        }
        ~Scratch()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }

        std::string write(const std::string& name, const std::string& text) const
        {
            const fs::path path = mPath / name;
            std::ofstream(path, std::ios::binary) << text;
            return path.generic_string();
        }

        std::string read(const std::string& name) const
        {
            std::ifstream     in(mPath / name, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        std::string path(const std::string& name) const { return (mPath / name).generic_string(); }

        std::string out() const { return (mPath / "out").generic_string(); }

    private:
        fs::path mPath;
    };

    const std::string GOOD = R"([render]
label = "Rendering"

[render.draw_distance]
type    = "F32"
default = 128.0
range   = [32.0, 1024.0]
unit    = "metres"
alias   = "RenderFarClip"
help    = "How far the view reaches"

[render.shadows]
label = "Shadows"

[render.shadows.detail]
type    = "S32"
default = 2
choices = { 0 = "None", 1 = "Sun", 2 = "Sun and projectors" }
help    = "Which lights cast shadows"
)";

    ALCVarGen::Options optionsFor(const Scratch& scratch, const std::vector<std::string>& files, const std::string& lock)
    {
        ALCVarGen::Options options;
        options.inputs     = files;
        options.outDir     = scratch.out();
        options.root       = "cvt";
        options.includeDir = "cvt";
        options.lock       = lock;
        return options;
    }

    ALCVarGen::Result generate(const Scratch& scratch, const std::vector<std::string>& files, const std::string& lock = "")
    {
        return ALCVarGen::run(optionsFor(scratch, files, lock));
    }

    // What the lock's target does: the schema's new steps written into the
    // lock, and nothing else.
    ALCVarGen::Result record(const Scratch& scratch, const std::vector<std::string>& files, const std::string& lock)
    {
        ALCVarGen::Options options = optionsFor(scratch, files, lock);
        options.writeLock          = true;
        return ALCVarGen::run(options);
    }

    std::vector<std::string> linesOf(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream       in(text);
        for (std::string line; std::getline(in, line);)
        {
            lines.push_back(line);
        }
        return lines;
    }

    // Whether one note, wherever it is, says this.
    bool hasNote(const ALCVarGen::Result& result, const std::string& note)
    {
        return std::any_of(result.notes.begin(), result.notes.end(),
                           [&](const std::string& said) { return said.ends_with(": note: " + note); });
    }

    // GOOD with one entry's text replaced.
    std::string changed(std::string text, const std::string& from, const std::string& to)
    {
        const size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        return text.replace(at, from.size(), to);
    }

    const std::string DISTANCE = "type    = \"F32\"\ndefault = 128.0\nrange   = [32.0, 1024.0]\nunit    = \"metres\"";

    // Whether one error is at file:line, of any column, and says this.
    bool hasError(const ALCVarGen::Result& result, const std::string& at, const std::string& says)
    {
        for (const auto& error : result.errors)
        {
            if (error.starts_with(at + ":") && error.find(": error: ") != std::string::npos &&
                error.find(says) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::string errorsOf(const ALCVarGen::Result& result)
    {
        std::string text;
        for (const auto& error : result.errors)
        {
            text += error + "\n";
        }
        return text;
    }

    // One broken declaration: a group, then the setting at line 4.
    ALCVarGen::Result oneSetting(const Scratch& scratch, const std::string& body, std::string& file)
    {
        file = scratch.write("broken.toml", "[render]\nlabel = \"Rendering\"\n\n" + body);
        return generate(scratch, { file });
    }

    TEST_CASE("alcvargen: a good schema writes headers sources and the schema files", "[alcvargen]")
    {
        Scratch                 scratch;
        const std::string       file   = scratch.write("render.toml", GOOD);
        const ALCVarGen::Result result = generate(scratch, { file });
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const std::string header = scratch.read("out/include/cvt/render/shadows.h");
        CHECK(header.find("namespace cvt::render::shadows") != std::string::npos);
        CHECK(header.find("inline constexpr ALCVar<S32, Group_, 0> detail{};") != std::string::npos);
        CHECK(header.find("// [render.shadows.detail]") != std::string::npos);
        CHECK(header.find("inline Values read() noexcept") != std::string::npos);
        CHECK(scratch.read("out/include/cvt/render.h").find("ALCVar<F32, Group_, 0> draw_distance{}") != std::string::npos);
        CHECK(scratch.read("out/include/cvt/render/all.h").find("#include \"cvt/render/shadows.h\"") != std::string::npos);
        const std::string source = scratch.read("out/src/cvt_render.cpp");
        CHECK(source.find("constinit U32 Group_::words[1]") != std::string::npos);
        CHECK(source.find("0x43000000u") != std::string::npos);
        CHECK(source.find(".name = \"RenderFarClip\"") != std::string::npos);
        CHECK(scratch.read("out/cvars.json").find("\"name\": \"render.draw_distance\"") != std::string::npos);
        CHECK(scratch.read("out/alcvar.natvis").find("ALCVar&lt;*,cvt::render::shadows::Group_,0&gt;") != std::string::npos);
        CHECK(scratch.read("out/alcvar_lldb.py").find("\"render.shadows.detail\"") != std::string::npos);
        const std::string manifest = scratch.read("out/cvars.outputs");
        CHECK(manifest.find("source src/cvt_render.cpp") != std::string::npos);
        CHECK(manifest.find("source src/cvt_registry.cpp") != std::string::npos);
    }

    TEST_CASE("alcvargen: a broken name fails at its line", "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;

        auto result = oneSetting(scratch, "[render.DrawDistance]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "render.DrawDistance: 'DrawDistance' is not lower_snake"));

        result = oneSetting(scratch, "[render.far]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "render.far: 'far' is a macro"));

        result = oneSetting(scratch, "[render.type]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "render.type: 'type' is a field's name"));

        result = oneSetting(scratch, "[render.read]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "'read' is a name the generator writes itself"));

        result = oneSetting(scratch, "[render.al_glow]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "'al' says where a setting came from"));

        result = oneSetting(scratch, "[render.disable_soft]\ntype = \"bool\"\ndefault = false\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "booleans name what true means"));

        result = oneSetting(scratch, "[render.fade_ms]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "'ms' is a unit"));

        result = oneSetting(scratch, "[render.text_seperator]\ntype = \"string\"\ndefault = \"\"\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "'seperator' is 'separator'"));

        const std::string other = scratch.write("other.toml", "[gadgets]\nlabel = \"Gadgets\"\n");
        result                  = generate(scratch, { other });
        CHECK(hasError(result, other + ":1", "gadgets: 'gadgets' is not an area"));
    }

    TEST_CASE("alcvargen: a broken declaration fails at its line", "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;

        auto result = oneSetting(scratch,
                                 "[render.glow]\ntype = \"F32\"\nrange = [0.0, 1.0]\ndefault = 2.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":7", "render.glow: the default 2.0 is outside the range [0.0, 1.0]"));

        result = oneSetting(scratch, "[render.sound]\ntype = \"uuid\"\ndefault = \"00000000-0000-0000-0000-000000000000\"\nhelp = \"x\"\n",
                            file);
        CHECK(hasError(result, file + ":4", "a UUID says what it refers to"));

        result = oneSetting(scratch,
                            "[render.proxy]\ntype = \"string\"\ndefault = \"\"\nflags = [\"dangerous\"]\npreset = \"look\"\nhelp = \"x\"\n",
                            file);
        CHECK(hasError(result, file + ":4", "a dangerous setting is in no preset"));

        result = oneSetting(scratch,
                            "[render.key]\ntype = \"string\"\ndefault = \"\"\nflags = [\"sensitive\"]\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "a sensitive setting is backed up only when asked"));

        result = oneSetting(scratch, "[render.level]\ntype = \"S32\"\ndefault = 3\nchoices = { 0 = \"Off\", 1 = \"On\" }\nhelp = \"x\"\n",
                            file);
        CHECK(hasError(result, file + ":4", "the default is not among the choices"));

        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\n", file);
        CHECK(hasError(result, file + ":4", "no help"));

        result = oneSetting(scratch, "[render.glow]\ntype = \"F23\"\ndefault = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":5", "'F23' is not a type"));

        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefualt = 1.0\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":6", "a setting has no field 'defualt'"));

        result = oneSetting(scratch, "[render.lut]\ntype = \"string\"\ndefault = \"Kodachrome.cube\"\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":4", "names a file"));

        result = oneSetting(scratch,
                            "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nalias = \"render.glow\"\nhelp = \"x\"\n", file);
        CHECK(hasError(result, file + ":7", "the alias render.glow is a current name"));
    }

    TEST_CASE("alcvargen: a number the schema gives is finite", "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;
        for (const char* body :
             { "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nrange = [0.0, inf]\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nrange = [-inf, 0.0]\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nrange = [nan, 1.0]\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nui_step = inf\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nmigrate = [{ step = 2, scale = inf }]\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nmigrate = [{ step = 2, map = { inf = 1.0 } }]\nhelp = \"x\"\n",
               "[render.reach]\ntype = \"F32\"\ndefault = 1.0\nmigrate = [{ step = 2, map = { 1 = nan } }]\nhelp = \"x\"\n" })
        {
            INFO(body);
            const auto result = oneSetting(scratch, body, file);
            CHECK_FALSE(result.ok);
            CHECK(errorsOf(result).find("render.reach") != std::string::npos);
        }
    }

    TEST_CASE("alcvargen: names that flatten alike get identifiers of their own", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", "[render]\nlabel = \"Rendering\"\n\n"
                                                              "[render.shadow]\nlabel = \"Shadow\"\n\n"
                                                              "[render.shadow.map_size]\ntype = \"S32\"\ndefault = 1\n"
                                                              "alias = \"OldShadowMapSize\"\nhelp = \"x\"\n\n"
                                                              "[render.shadow_map]\nlabel = \"Shadow map\"\n\n"
                                                              "[render.shadow_map.size]\ntype = \"S32\"\ndefault = 1\n"
                                                              "alias = \"OldMapSize\"\nhelp = \"x\"\n");
        const ALCVarGen::Result result = generate(scratch, { file });
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        // Every array the area's source declares, declared once.
        const std::string     source = scratch.read("out/src/cvt_render.cpp");
        const std::regex      declared(R"(constexpr [A-Za-z_:<>]+ (\w+)\[\] =)");
        std::set<std::string> names;
        for (auto it = std::sregex_iterator(source.begin(), source.end(), declared); it != std::sregex_iterator(); ++it)
        {
            INFO((*it)[1].str());
            CHECK(names.insert((*it)[1].str()).second);
        }
        CHECK(names.size() >= 2);
    }

    TEST_CASE("alcvargen: an llsd default is JSON the viewer reads", "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;
        for (const char* json : { R"({"scale": .5})", "[01]", R"({"a": +1})", R"({"a": 1.})", "[1e999]", "[-]" })
        {
            INFO(json);
            const auto result =
                oneSetting(scratch, std::string("[render.layout]\ntype = \"llsd\"\ndefault = '") + json + "'\nhelp = \"x\"\n",
                           file);
            CHECK_FALSE(result.ok);
        }
        const auto result = oneSetting(
            scratch, "[render.layout]\ntype = \"llsd\"\ndefault = '{\"scale\": 0.5, \"n\": -1e-3, \"z\": [0, 10]}'\nhelp = \"x\"\n",
            file);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a tag is a name C++ and every platform leave free", "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;
        for (const char* tag : { "default", "new", "2d", "near", "far", "_input" })
        {
            INFO(tag);
            const auto result = oneSetting(
                scratch, std::string("[render.reach]\ntype = \"F32\"\ndefault = 1.0\ntags = [\"") + tag + "\"]\nhelp = \"x\"\n",
                file);
            CHECK_FALSE(result.ok);
        }
        const auto result =
            oneSetting(scratch, "[render.reach]\ntype = \"F32\"\ndefault = 1.0\ntags = [\"shader_input\"]\nhelp = \"x\"\n", file);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a setting needs its group declared before it", "[alcvargen]")
    {
        Scratch scratch;
        const std::string before = scratch.write(
            "before.toml",
            "[render]\nlabel = \"R\"\n\n[render.shadows.detail]\ntype = \"S32\"\ndefault = 1\nhelp = \"x\"\n\n"
            "[render.shadows]\nlabel = \"Shadows\"\n");
        auto result = generate(scratch, { before });
        CHECK(hasError(result, before + ":4", "render.shadows.detail: declared before its group render.shadows"));

        const std::string none = scratch.write(
            "none.toml", "[render]\nlabel = \"R\"\n\n[render.shadows.detail]\ntype = \"S32\"\ndefault = 1\nhelp = \"x\"\n");
        result = generate(scratch, { none });
        CHECK(hasError(result, none + ":4", "render.shadows: no group declares this"));
    }

    TEST_CASE("alcvargen: a name or an alias is declared once across every file", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string first  = scratch.write("first.toml", GOOD);
        const std::string second = scratch.write(
            "second.toml", "[ui]\nlabel = \"UI\"\n\n[ui.scale]\ntype = \"F32\"\ndefault = 1.0\nalias = \"RenderFarClip\"\nhelp = \"x\"\n\n"
                           "[render.extra]\ntype = \"F32\"\ndefault = 1.0\nhelp = \"x\"\n");
        const auto result = generate(scratch, { first, second });
        CHECK(hasError(result, second + ":7", "the alias RenderFarClip is already render.draw_distance's"));
        CHECK(hasError(result, second + ":10", "the area render is declared in " + first));
    }

    TEST_CASE("alcvargen: a header changes only when its group's keys do", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        REQUIRE(generate(scratch, { file }).ok);
        const std::string render  = scratch.read("out/include/cvt/render.h");
        const std::string shadows = scratch.read("out/include/cvt/render/shadows.h");
        const std::string source  = scratch.read("out/src/cvt_render.cpp");

        // A default, help and a range edited, and a line inserted above
        // every entry.
        std::string edited = "# a comment\n" + GOOD;
        edited.replace(edited.find("default = 128.0"), 15, "default = 256.0");
        edited.replace(edited.find("How far the view reaches"), 24, "How far one sees");
        edited.replace(edited.find("[32.0, 1024.0]"), 14, "[16.0, 2048.0]");
        scratch.write("render.toml", edited);
        auto result = generate(scratch, { file });
        REQUIRE(result.ok);
        CHECK(scratch.read("out/include/cvt/render.h") == render);
        CHECK(scratch.read("out/include/cvt/render/shadows.h") == shadows);
        CHECK(scratch.read("out/src/cvt_render.cpp") != source);
        CHECK(std::find(result.written.begin(), result.written.end(), "include/cvt/render.h") == result.written.end());
        CHECK(std::find(result.written.begin(), result.written.end(), "src/cvt_render.cpp") != result.written.end());

        // A new key changes its group's header alone.
        scratch.write("render.toml", edited + "\n[render.shadows.soft]\ntype = \"bool\"\ndefault = true\nhelp = \"x\"\n");
        result = generate(scratch, { file });
        REQUIRE(result.ok);
        CHECK(scratch.read("out/include/cvt/render.h") == render);
        CHECK(scratch.read("out/include/cvt/render/shadows.h") != shadows);
    }

    TEST_CASE("alcvargen: the lock records each setting's first step and a convertible change as its next",
              "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");

        // No lock yet: every setting is a step past it.
        auto result = generate(scratch, { file }, lock);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, lock, "behind the schema"));
        CHECK(hasNote(result, "render.draw_distance: declared (step 1)"));

        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto first = linesOf(scratch.read("cvars.lock"));
        REQUIRE(first.size() == 2);
        CHECK(first[0].starts_with(R"({"name": "render.draw_distance", "step": 1, "decl": ")"));
        CHECK(first[0].ends_with(R"(", "parent": "", "type": "F32", "unit": "metres", "choices": [], "migrate": ""})"));
        CHECK(first[1].starts_with(R"({"name": "render.shadows.detail", "step": 1, "decl": ")"));
        CHECK(first[1].ends_with(
            R"("type": "S32", "unit": "", "choices": [{"value": 0}, {"value": 1}, {"value": 2}], "migrate": ""})"));
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(result.notes.empty());

        // F32 to F64 converts by itself: said, and a step the lock records.
        scratch.write("render.toml", changed(GOOD, "type    = \"F32\"", "type    = \"F64\""));
        result = generate(scratch, { file }, lock);
        CHECK_FALSE(result.ok);
        CHECK(hasNote(result, "render.draw_distance: F32 -> F64 (step 2); stored values convert exactly"));
        CHECK(hasError(result, lock, "behind the schema"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto second = linesOf(scratch.read("cvars.lock"));
        REQUIRE(second.size() == 3);
        CHECK(second[0] == first[0]);
        CHECK(second[1].starts_with(R"({"name": "render.draw_distance", "step": 2, "decl": ")"));
        CHECK(second[1].find("\"parent\": \"" + first[0].substr(first[0].find("\"decl\": ") + 9, 16) + "\"") !=
              std::string::npos);
        CHECK(second[1].ends_with(R"(", "type": "F64", "unit": "metres", "choices": [], "migrate": ""})"));
        CHECK(second[2] == first[1]);
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(scratch.read("out/cvars.json").find("\"step\": 2") != std::string::npos);
    }

    TEST_CASE("alcvargen: a change nothing converts fails with the step to write and passes once answered",
              "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);

        const std::string rect = changed(GOOD, DISTANCE, "type    = \"rect\"\ndefault = [0, 1, 1, 0]");
        scratch.write("render.toml", rect);
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, file + ":4",
                       "render.draw_distance: F32 -> rect (step 2) has no automatic conversion; add migrate = [{ step = 2, "
                       "fn = \"...\" }], or [{ step = 2, reset = true }] to take the default"));

        // A rect is no number a map or a scale takes, the first step has
        // nothing before it, and one change is one step.
        for (const char* migrate : { "migrate = [{ step = 2, scale = 2.0 }]\n", "migrate = [{ step = 2, map = { 1 = 2 } }]\n",
                                     "migrate = [{ step = 1, reset = true }]\n", "migrate = [{ step = 3, reset = true }]\n" })
        {
            INFO(migrate);
            scratch.write("render.toml", changed(rect, "alias", std::string(migrate) + "alias"));
            result = generate(scratch, { file }, lock);
            INFO(errorsOf(result));
            CHECK(hasError(result, file + ":7", "render.draw_distance: "));
        }

        // fn or reset of the step answers it.
        scratch.write("render.toml", changed(rect, "alias", "migrate = [{ step = 2, reset = true }]\nalias"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        CHECK(hasNote(result, "render.draw_distance: F32 -> rect (step 2); stored values migrate as the entry says"));
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a lock behind the schema fails the build with the command and a configure goes on",
              "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string before = scratch.read("cvars.lock");
        scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"kilometres\""));

        ALCVarGen::Options options = optionsFor(scratch, { file }, lock);
        options.lockCommand        = "cmake --build build --target cvt_lock";
        auto result                = ALCVarGen::run(options);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, lock, "behind the schema; run cmake --build build --target cvt_lock, and commit it with the change"));
        CHECK(hasNote(result, "render.draw_distance: metres -> kilometres (step 2); stored values scale by 0.001"));

        // A configure writes the tree that builds the lock's target: it says
        // so, and generates with the step the change will be.
        options.provisional = true;
        result              = ALCVarGen::run(options);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(result.errors.empty());
        bool said = false;
        for (const auto& warning : result.warnings)
        {
            said = said || warning.find("behind the schema; run cmake --build build --target cvt_lock") != std::string::npos;
        }
        CHECK(said);
        CHECK(scratch.read("out/cvars.json").find("\"step\": 2") != std::string::npos);
        CHECK(scratch.read("cvars.lock") == before);
    }

    TEST_CASE("alcvargen: recording the lock refuses an unanswered change and touches no other line", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string before = scratch.read("cvars.lock");

        // Two changes and a new setting, one change unanswered: nothing is
        // written.
        std::string schema = changed(GOOD, DISTANCE, "type    = \"rect\"\ndefault = [0, 1, 1, 0]");
        schema             = changed(schema, "1 = \"Sun\", ", "");
        schema += "\n[render.shadows.soft]\ntype    = \"bool\"\ndefault = true\nhelp    = \"x\"\n";
        scratch.write("render.toml", schema);
        auto result = record(scratch, { file }, lock);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, file + ":4", "render.draw_distance: F32 -> rect (step 2) has no automatic conversion"));
        CHECK(scratch.read("cvars.lock") == before);

        // Answered: the new steps added, sorted among the lines there were,
        // and those lines as they were.
        scratch.write("render.toml", changed(schema, "alias", "migrate = [{ step = 2, reset = true }]\nalias"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        CHECK(hasNote(result, "render.shadows.detail: the choice 1 is gone (step 2); stored values of it take the default"));
        CHECK(hasNote(result, "render.shadows.soft: declared (step 1)"));
        const auto was = linesOf(before);
        const auto now = linesOf(scratch.read("cvars.lock"));
        REQUIRE(now.size() == 5);
        CHECK(now[0] == was[0]);
        CHECK(now[1].starts_with(R"({"name": "render.draw_distance", "step": 2, )"));
        CHECK(now[2] == was[1]);
        CHECK(now[3].starts_with(R"({"name": "render.shadows.detail", "step": 2, )"));
        CHECK(now[4].starts_with(R"({"name": "render.shadows.soft", "step": 1, )"));
    }

    TEST_CASE("alcvargen: a migrate deleted while its step is in the lock fails the build", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string help     = "help    = \"Which lights cast shadows\"";
        const std::string migrated =
            changed(GOOD, help, help + "\nmigrate = [{ step = 2, map = { 1 = 2 }, else = \"keep\" }]");
        const std::string file = scratch.write("render.toml", migrated);
        const std::string lock     = scratch.path("cvars.lock");
        auto              result   = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        // Declared and migrated at once: its first step, and the one the
        // migrate begins.
        CHECK(linesOf(scratch.read("cvars.lock")).size() == 3);
        CHECK(hasNote(result, "render.shadows.detail: migrated (step 2); stored values migrate as the entry says"));
        REQUIRE(generate(scratch, { file }, lock).ok);

        scratch.write("render.toml", GOOD);
        result = generate(scratch, { file }, lock);
        CHECK(hasError(result, file + ":15", "render.shadows.detail: step 2 is not the step the lock records"));
        const std::string before = scratch.read("cvars.lock");
        CHECK_FALSE(record(scratch, { file }, lock).ok);
        CHECK(scratch.read("cvars.lock") == before);

        // One that answered a change nothing converts.
        const std::string other = scratch.path("other.lock");
        REQUIRE(record(scratch, { file }, other).ok);
        const std::string rect = changed(GOOD, DISTANCE, "type    = \"rect\"\ndefault = [0, 1, 1, 0]");
        scratch.write("render.toml", changed(rect, "alias", "migrate = [{ step = 2, reset = true }]\nalias"));
        REQUIRE(record(scratch, { file }, other).ok);
        scratch.write("render.toml", rect);
        result = generate(scratch, { file }, other);
        CHECK(hasError(result, file + ":4", "render.draw_distance: F32 -> rect (step 2) has no automatic conversion"));
    }

    TEST_CASE("alcvargen: an entry changed back takes a new step", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        scratch.write("render.toml", changed(GOOD, "type    = \"F32\"", "type    = \"F64\""));
        REQUIRE(record(scratch, { file }, lock).ok);

        scratch.write("render.toml", GOOD);
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, lock, "behind the schema"));
        CHECK(hasNote(result,
                      "render.draw_distance: F64 -> F32 (step 3); stored values convert where they fit, clamped or rounded"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto lines = linesOf(scratch.read("cvars.lock"));
        REQUIRE(lines.size() == 4);
        CHECK(lines[2].starts_with(R"({"name": "render.draw_distance", "step": 3, )"));
        CHECK(lines[2].ends_with(R"("type": "F32", "unit": "metres", "choices": [], "migrate": ""})"));
        // Step 1 and step 3 declare the same, and are told apart.
        auto decl = [](const std::string& line) { return line.substr(line.find("\"decl\": ") + 9, 16); };
        CHECK(decl(lines[0]) != decl(lines[2]));
        CHECK(generate(scratch, { file }, lock).ok);
    }

    TEST_CASE("alcvargen: a default range help or label edited or a line moved leaves the lock byte for byte",
              "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string before = scratch.read("cvars.lock");

        std::string edited = "# a comment\n\n" + GOOD;
        edited             = changed(edited, "default = 128.0", "default = 256.0");
        edited             = changed(edited, "How far the view reaches", "How far one sees");
        edited             = changed(edited, "[32.0, 1024.0]", "[16.0, 2048.0]");
        edited             = changed(edited, "label = \"Shadows\"", "label = \"Shade\"");
        edited             = changed(edited, "1 = \"Sun\"", "1 = \"The sun\"");
        edited             = changed(edited, "alias   = \"RenderFarClip\"\n", "");
        edited             = changed(edited, "unit    = \"metres\"", "alias   = \"RenderFarClip\"\nunit    = \"metres\"");
        scratch.write("render.toml", edited);
        auto result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(result.notes.empty());
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(scratch.read("cvars.lock") == before);
    }

    TEST_CASE("alcvargen: lines a union merge leaves in any order are read as one lock", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string base = scratch.path("base.lock");
        REQUIRE(record(scratch, { file }, base).ok);
        scratch.write("a.lock", scratch.read("base.lock"));
        scratch.write("b.lock", scratch.read("base.lock"));

        // One branch changes a setting's type, the other another's choices.
        const std::string ours   = changed(GOOD, "type    = \"F32\"", "type    = \"F64\"");
        const std::string theirs = changed(GOOD, "1 = \"Sun\", ", "");
        scratch.write("render.toml", ours);
        REQUIRE(record(scratch, { file }, scratch.path("a.lock")).ok);
        scratch.write("render.toml", theirs);
        REQUIRE(record(scratch, { file }, scratch.path("b.lock")).ok);

        // Each side's lines once, theirs first and ours backwards.
        std::vector<std::string> merged;
        const auto               common = linesOf(scratch.read("base.lock"));
        for (const auto& line : linesOf(scratch.read("b.lock")))
        {
            if (std::find(common.begin(), common.end(), line) == common.end())
            {
                merged.push_back(line);
            }
        }
        auto a = linesOf(scratch.read("a.lock"));
        merged.insert(merged.end(), a.rbegin(), a.rend());
        REQUIRE(merged.size() == 4);
        std::string text;
        for (const auto& line : merged)
        {
            text += line + "\n";
        }
        const std::string lock = scratch.write("merged.lock", text);

        scratch.write("render.toml", changed(ours, "1 = \"Sun\", ", ""));
        auto result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(result.notes.empty());
    }

    TEST_CASE("alcvargen: two branches' steps of one setting are a fork until a join answers it", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        REQUIRE(record(scratch, { file }, scratch.path("a.lock")).ok);
        scratch.write("b.lock", scratch.read("a.lock"));

        // Both branches change one setting, each its own way: two step 2s,
        // told apart by fingerprint.
        const std::string ours = changed(GOOD, "type    = \"F32\"", "type    = \"F64\"");
        scratch.write("render.toml", ours);
        REQUIRE(record(scratch, { file }, scratch.path("a.lock")).ok);
        scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"kilometres\""));
        REQUIRE(record(scratch, { file }, scratch.path("b.lock")).ok);
        const auto a = linesOf(scratch.read("a.lock"));
        const auto b = linesOf(scratch.read("b.lock"));
        REQUIRE(a.size() == 3);
        REQUIRE(b.size() == 3);
        auto decl = [](const std::string& line) { return line.substr(line.find("\"decl\": ") + 9, 16); };
        CHECK(decl(a[1]) != decl(b[1]));
        std::string text;
        for (const auto& line : { a[0], a[1], b[1], a[2] })
        {
            text += line + "\n";
        }
        const std::string lock = scratch.write("merged.lock", text);

        // Refused, naming both, and never by deleting a line.
        scratch.write("render.toml", ours);
        auto result = generate(scratch, { file }, lock);
        CHECK_FALSE(result.ok);
        const std::string errors = errorsOf(result);
        CHECK(errors.find("render.draw_distance: two steps follow step 1") != std::string::npos);
        CHECK(errors.find(decl(a[1])) != std::string::npos);
        CHECK(errors.find(decl(b[1])) != std::string::npos);
        CHECK_FALSE(record(scratch, { file }, lock).ok);
        CHECK(scratch.read("merged.lock") == text);

        // The merged entry keeps its branch's steps and joins the other's.
        scratch.write("render.toml", changed(ours, "alias", "migrate = [{ step = 3, join = { \"" + decl(b[1]) +
                                                                "\" = { scale = 1000.0 } } }]\nalias"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto joined = linesOf(scratch.read("merged.lock"));
        REQUIRE(joined.size() == 5);
        CHECK(std::find(joined.begin(), joined.end(), b[1]) != joined.end());
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);

        // A line's type changed without its fingerprint.
        std::string edited = a[1];
        edited.replace(edited.find("\"F64\""), 5, "\"F32\"");
        scratch.write("edited.lock", a[0] + "\n" + edited + "\n" + a[2] + "\n");
        result = generate(scratch, { file }, scratch.path("edited.lock"));
        CHECK(hasError(result, scratch.path("edited.lock") + ":2", "render.draw_distance: step 2's decl is not its own"));
    }

    TEST_CASE("alcvargen: a migrate edited after it was recorded fails at its entry", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string help = "help    = \"Which lights cast shadows\"";
        const std::string file = scratch.write("render.toml", changed(GOOD, help, help + "\nmigrate = [{ step = 2, scale = 2.0 }]"));
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        REQUIRE(generate(scratch, { file }, lock).ok);
        const std::string before = scratch.read("cvars.lock");

        scratch.write("render.toml", changed(GOOD, help, help + "\nmigrate = [{ step = 2, scale = 3.0 }]"));
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, file + ":15", "render.shadows.detail: step 2 is not the step the lock records"));
        CHECK_FALSE(record(scratch, { file }, lock).ok);
        CHECK(scratch.read("cvars.lock") == before);
    }

    TEST_CASE("alcvargen: a line taken out of the lock fails and so does one the base branch has", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string f64 = changed(GOOD, "type    = \"F32\"", "type    = \"F64\"");
        scratch.write("render.toml", f64);
        REQUIRE(record(scratch, { file }, lock).ok);
        const auto lines = linesOf(scratch.read("cvars.lock"));
        REQUIRE(lines.size() == 3);
        const std::string base = scratch.write("base.lock", scratch.read("cvars.lock"));

        // The first step gone: the second follows a step the lock lacks.
        scratch.write("cvars.lock", lines[1] + "\n" + lines[2] + "\n");
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, lock + ":1", "render.draw_distance: step 2 follows a step the lock does not hold"));

        // The second gone, as a revert of its commit takes it: the entry is
        // reverted too, so the lock alone cannot tell; the base branch's can.
        scratch.write("render.toml", GOOD);
        scratch.write("cvars.lock", lines[0] + "\n" + lines[2] + "\n");
        REQUIRE(generate(scratch, { file }, lock).ok);
        ALCVarGen::Options options = optionsFor(scratch, { file }, lock);
        options.base               = base;
        result                     = ALCVarGen::run(options);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, base + ":2", "render.draw_distance: step 2"));
        CHECK(errorsOf(result).find("is missing from the lock") != std::string::npos);
    }

    TEST_CASE("alcvargen: a setting deleted fails until the lock retires it", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);

        // Gone without a word, with a new setting that might be its new name.
        const std::string entry = "[render.shadows.detail]\ntype    = \"S32\"\ndefault = 2\n"
                                  "choices = { 0 = \"None\", 1 = \"Sun\", 2 = \"Sun and projectors\" }\n"
                                  "help    = \"Which lights cast shadows\"\n";
        const std::string gone = changed(GOOD, entry, changed(entry, "[render.shadows.detail]", "[render.shadows.casters]"));
        scratch.write("render.toml", gone);
        auto result = generate(scratch, { file }, lock);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, lock + ":2", "render.shadows.detail: in the lock but declared nowhere"));
        CHECK(errorsOf(result).find("alias = \"render.shadows.detail\" to render.shadows.casters") != std::string::npos);

        // Recorded as retired: its rows are kept and ignored.
        scratch.write("render.toml", changed(GOOD, entry, ""));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto lines = linesOf(scratch.read("cvars.lock"));
        REQUIRE(lines.size() == 3);
        CHECK(lines[2].starts_with(R"({"name": "render.shadows.detail", "decl": ")"));
        CHECK(lines[2].ends_with(R"(", "retired": true})"));
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);

        // Declared again: only as a chain of its own.
        scratch.write("render.toml", GOOD);
        result = generate(scratch, { file }, lock);
        CHECK(hasError(result, file + ":15", "render.shadows.detail: retired; declaring the name again needs reuse = true"));
        scratch.write("render.toml", changed(GOOD, entry, changed(entry, "help ", "reuse   = true\nhelp ")));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto again = linesOf(scratch.read("cvars.lock"));
        REQUIRE(again.size() == 4);
        const std::string retired = again[2].substr(again[2].find("\"decl\": ") + 9, 16);
        CHECK(again[3].find("\"parent\": \"" + retired + "\"") != std::string::npos);
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a rename continues the old name's chain", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string help     = "help    = \"Which lights cast shadows\"";
        const std::string migrated = changed(GOOD, help, help + "\nmigrate = [{ step = 2, map = { 1 = 2 }, else = \"keep\" }]");
        const std::string file     = scratch.write("render.toml", migrated);
        const std::string lock     = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const auto before = linesOf(scratch.read("cvars.lock"));
        REQUIRE(before.size() == 3);

        // Renamed, its migrate list kept: the step after the old name's last.
        const std::string renamed = changed(changed(migrated, "[render.shadows.detail]", "[render.shadows.casters]"),
                                            help, "alias   = \"render.shadows.detail\"\n" + help);
        scratch.write("render.toml", renamed);
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, lock, "behind the schema"));
        CHECK(hasNote(result, "render.shadows.detail -> render.shadows.casters (step 3); stored values follow the rename"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto after = linesOf(scratch.read("cvars.lock"));
        REQUIRE(after.size() == 5);
        const std::string last = before[2].substr(before[2].find("\"decl\": ") + 9, 16);
        CHECK(after[1].starts_with(R"({"name": "render.shadows.casters", "step": 3, )"));
        CHECK(after[1].find("\"parent\": \"" + last + "\"") != std::string::npos);
        CHECK(after[1].find("\"migrate\": \"\"") == std::string::npos);
        CHECK(after[4].ends_with(R"(", "renamed_to": "render.shadows.casters"})"));
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);

        // The old name declared again.
        scratch.write("render.toml", renamed + "\n" + changed(migrated.substr(migrated.find("[render.shadows.detail]")),
                                                             "migrate = [{ step = 2, map = { 1 = 2 }, else = \"keep\" }]\n", ""));
        result = generate(scratch, { file }, lock);
        CHECK(errorsOf(result).find("render.shadows.detail: renamed to render.shadows.casters; declaring the name again "
                                    "needs reuse = true") != std::string::npos);
    }

    TEST_CASE("alcvargen: a choice added makes no step", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string before = scratch.read("cvars.lock");
        scratch.write("render.toml", changed(GOOD, "2 = \"Sun and projectors\"", "2 = \"Sun and projectors\", 3 = \"All\""));
        auto result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        CHECK(result.notes.empty());
        CHECK(record(scratch, { file }, lock).ok);
        CHECK(scratch.read("cvars.lock") == before);
    }

    TEST_CASE("alcvargen: a was that is still a choice and a map that does not say what is left are refused",
              "[alcvargen]")
    {
        Scratch     scratch;
        std::string file;
        auto        result = oneSetting(scratch,
                                        "[render.level]\ntype = \"S32\"\ndefault = 0\nchoices = [{ value = 0 }, { value = 1 }, "
                                        "{ value = 2, was = [1] }]\nhelp = \"x\"\n",
                                        file);
        CHECK(hasError(result, file + ":4", "render.level: 1 is still a choice, and cannot be what 2 was"));

        result = oneSetting(scratch, "[render.level]\ntype = \"S32\"\ndefault = 0\nmigrate = [{ step = 2, map = { 1 = 2 } }]\nhelp = \"x\"\n",
                            file);
        CHECK(hasError(result, file + ":7", "render.level: a map says what becomes of a value it does not list"));

        // Its keys are values the step before could hold.
        result = oneSetting(scratch,
                            "[render.level]\ntype = \"S32\"\ndefault = 0\nchoices = { 0 = \"Off\", 1 = \"On\" }\n"
                            "migrate = [{ step = 2, map = { 5 = 1 }, else = \"keep\" }]\nhelp = \"x\"\n",
                            file);
        CHECK(hasError(result, file + ":8", "render.level: the map's key 5 is not among step 1's choices"));
        result = oneSetting(scratch,
                            "[render.level]\ntype = \"S32\"\ndefault = 0\nchoices = { 0 = \"Off\", 1 = \"On\" }\n"
                            "migrate = [{ step = 2, map = { 0 = 1 }, else = \"default\" }]\nhelp = \"x\"\n",
                            file);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a setting takes its first value from settings the lock holds", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);

        // Two settings split one: both may name it.
        const std::string split = GOOD + "\n[render.shadows.close_reach]\ntype    = \"F32\"\ndefault = 1.0\n"
                                         "from    = [{ name = \"render.shadows.detail\", fn = \"near_from_detail\" }]\nhelp    = \"x\"\n"
                                         "\n[render.shadows.far_reach]\ntype    = \"F32\"\ndefault = 1.0\n"
                                         "from    = [{ name = \"render.shadows.detail\", fn = \"reach_from_detail\" }]\nhelp    = \"x\"\n";
        scratch.write("render.toml", split);
        auto result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);

        // A source the lock does not hold.
        scratch.write("render.toml", changed(split, "name = \"render.shadows.detail\", fn = \"reach_from_detail\"",
                                             "name = \"RenderShadowReach\", fn = \"reach_from_detail\""));
        result = generate(scratch, { file }, lock);
        CHECK(errorsOf(result).find("render.shadows.far_reach: RenderShadowReach is not in the lock") != std::string::npos);
    }

    TEST_CASE("alcvargen: a unit named or dropped keeps stored values as they are", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);

        std::string schema = changed(GOOD, "unit    = \"metres\"\n", "");
        schema             = changed(schema, "choices = { 0 = \"None\"", "unit    = \"pixels\"\nchoices = { 0 = \"None\"");
        scratch.write("render.toml", schema);
        auto result = generate(scratch, { file }, lock);
        REQUIRE(result.errors.size() == 1);
        CHECK(hasError(result, lock, "behind the schema"));
        REQUIRE(result.notes.size() == 2);
        CHECK(result.notes[0].ends_with(": note: render.draw_distance: metres -> no unit (step 2); stored values are kept as they are"));
        CHECK(result.notes[1].ends_with(": note: render.shadows.detail: no unit -> pixels (step 2); stored values are kept as they are"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
    }

    TEST_CASE("alcvargen: a unit that does not scale is answered by a migrate of its step", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);

        const std::string seconds = changed(GOOD, "unit    = \"metres\"", "unit    = \"seconds\"");
        scratch.write("render.toml", seconds);
        auto result = generate(scratch, { file }, lock);
        CHECK(hasError(result, file + ":4",
                       "render.draw_distance: metres -> seconds (step 2) do not scale into one another; add migrate = [{ step "
                       "= 2, scale = ... }], or [{ step = 2, reset = true }]"));

        scratch.write("render.toml", changed(seconds, "alias", "migrate = [{ step = 2, scale = 0.5 }]\nalias"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        CHECK(hasNote(result, "render.draw_distance: metres -> seconds (step 2); stored values migrate as the entry says"));
        CHECK(generate(scratch, { file }, lock).ok);
    }

    TEST_CASE("alcvargen: where says a setting's file line and header", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);

        ALCVarGen::Options options;
        options.inputs = { file };
        options.root   = "cvt";
        options.includeDir = "cvt";
        options.where  = "render.shadows.detail";
        auto result    = ALCVarGen::run(options);
        CHECK(result.ok);
        CHECK(result.where.find(file + ":15: render.shadows.detail") != std::string::npos);
        CHECK(result.where.find("header: cvt/render/shadows.h") != std::string::npos);

        options.where = "RenderFarClip";
        result        = ALCVarGen::run(options);
        CHECK(result.ok);
        CHECK(result.where.find("RenderFarClip is an old name of render.draw_distance") != std::string::npos);
        CHECK(result.where.find(file + ":4: render.draw_distance") != std::string::npos);

        // From the schema the build wrote, without the declarations.
        REQUIRE(generate(scratch, { file }).ok);
        options.inputs = { scratch.out() + "/cvars.json" };
        options.where  = "render.shadows.detail";
        result         = ALCVarGen::run(options);
        CHECK(result.ok);
        CHECK(result.where.find(file + ":15: render.shadows.detail") != std::string::npos);

        options.where = "render.nothing";
        result        = ALCVarGen::run(options);
        CHECK_FALSE(result.ok);
    }

    // The working directory, put back as the scope ends.
    class InDirectory
    {
    public:
        explicit InDirectory(const fs::path& path) : mWas(fs::current_path()) { fs::current_path(path); }
        ~InDirectory()
        {
            std::error_code ec;
            fs::current_path(mWas, ec);
        }
        InDirectory(const InDirectory&)            = delete;
        InDirectory& operator=(const InDirectory&) = delete;

    private:
        fs::path mWas;
    };

    TEST_CASE("alcvargen: messages take the compilers' form at the entry's absolute path and column", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string body = "[render]\nlabel = \"Rendering\"\n\n[render.glow]\ntype = \"F32\"\n  defualt = 1.0\nhelp = \"x\"\n";
        const std::string file = scratch.write("broken.toml", body);
        auto              result = generate(scratch, { file });
        INFO(errorsOf(result));
        REQUIRE(result.errors.size() == 2);
        CHECK(result.errors[0] == file + ":6:3: error: render.glow: a setting has no field 'defualt'; did you mean 'default'?");
        CHECK(result.errors[1] == file + ":4:1: error: render.glow: no default");

        // As Visual Studio's error list reads them.
        ALCVarGen::Options options = optionsFor(scratch, { file }, "");
        options.msvc               = true;
        result                     = ALCVarGen::run(options);
        REQUIRE(result.errors.size() == 2);
        const std::string native = fs::path(file).make_preferred().string();
        CHECK(result.errors[0] ==
              native + "(6,3): error CV0001: render.glow: a setting has no field 'defualt'; did you mean 'default'?");

        // A path given relative to where it runs is said whole.
        {
            InDirectory here(fs::path(file).parent_path());
            result = generate(scratch, { "broken.toml" });
        }
        REQUIRE(result.errors.size() == 2);
        INFO(result.errors[0]);
        // Whole, and the same file, though not always spelled as the case
        // wrote it: the working directory comes back through no link, and
        // macOS's temporary directory is reached through one (/var is
        // /private/var).
        const size_t at = result.errors[0].find(":6:3: error: ");
        REQUIRE(at != std::string::npos);
        const fs::path said = result.errors[0].substr(0, at);
        CHECK(said.is_absolute());
        CHECK(fs::equivalent(said, fs::path(file)));

        // A note at its entry, the lock's error at its first line, and a
        // file that is not there as a whole.
        const std::string good = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        result                 = generate(scratch, { good }, lock);
        CHECK(std::find(result.notes.begin(), result.notes.end(),
                        good + ":4:1: note: render.draw_distance: declared (step 1)") != result.notes.end());
        CHECK(std::find(result.errors.begin(), result.errors.end(),
                        lock + ":1:1: error: behind the schema; run cmake --build <tree> --target cvars_lock, and commit "
                               "it with the change") != result.errors.end());
        const std::string missing = scratch.path("missing.toml");
        result                    = generate(scratch, { missing });
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0] == missing + ": error: cannot be read");

        // TOML's own errors, at toml++'s line and column.
        const std::string bad = scratch.write("bad.toml", "[render]\nlabel = \n");
        result                = generate(scratch, { bad });
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].starts_with(bad + ":2:"));
        CHECK(result.errors[0].find(": error: ") != std::string::npos);
    }

    TEST_CASE("alcvargen: a message says one thing and what to write", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string lock = scratch.path("cvars.lock");
        const std::string base = "[render]\nlabel = \"Rendering\"\n\n"
                                 "[render.tint]\ntype    = \"vec3\"\ndefault = [1.0, 1.0, 1.0]\nhelp    = \"x\"\n\n"
                                 "[render.size]\ntype    = \"F32\"\ndefault = 0.5\nunit    = \"ratio\"\nhelp    = \"x\"\n";
        const std::string file = scratch.write("render.toml", base);
        REQUIRE(record(scratch, { file }, lock).ok);

        // A change nothing converts is an error, and nothing says the step
        // converts, or migrates as an entry with no migrate says.
        scratch.write("render.toml", changed(changed(base, "\"vec3\"\ndefault = [1.0, 1.0, 1.0]", "\"F32\"\ndefault = 1.0"),
                                             "\"ratio\"", "\"pixels\""));
        auto result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(hasError(result, file + ":4", "render.tint: vec3 -> F32 (step 2) has no automatic conversion"));
        CHECK(hasError(result, file + ":9", "render.size: ratio -> pixels (step 2) do not scale into one another"));
        for (const auto& note : result.notes)
        {
            INFO(note);
            CHECK(note.find("render.tint") == std::string::npos);
            CHECK(note.find("render.size") == std::string::npos);
        }

        // An integer's numbers as integers.
        std::string one;
        result = oneSetting(scratch, "[render.count]\ntype = \"U32\"\ndefault = 9\nrange = [1, 4]\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":6", "render.count: the default 9 is outside the range [1, 4]"));

        // A word nearly right, and what it nearly is.
        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nunit = \"metre\"\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":7", "render.glow: 'metre' is not a unit"));
        CHECK(errorsOf(result).find("; did you mean 'metres'?") != std::string::npos);
        result = oneSetting(scratch, "[render.glow]\ntype = \"F23\"\ndefault = 1.0\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":5", "'F23' is not a type"));
        CHECK(errorsOf(result).find("; did you mean 'F32'?") != std::string::npos);
        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nscope = \"globl\"\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":7", "scope is one of"));
        CHECK(errorsOf(result).find("; did you mean 'global'?") != std::string::npos);
        result = oneSetting(scratch,
                            "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nmigrate = [{ step = 2, scael = 2.0 }]\nhelp = \"x\"\n",
                            one);
        CHECK(errorsOf(result).find("migrate has no field 'scael'; did you mean 'scale'?") != std::string::npos);
        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nflags = [\"hiden\"]\nhelp = \"x\"\n", one);
        CHECK(errorsOf(result).find("; did you mean 'hidden'?") != std::string::npos);
        const std::string group = scratch.write("group.toml", "[render]\nlable = \"Rendering\"\n");
        result                  = generate(scratch, { group });
        CHECK(hasError(result, group + ":2", "render: a group has no field 'lable'; did you mean 'label'?"));

        // Nothing near: no guess.
        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nwhatever = 1\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":7", "a setting has no field 'whatever'"));
        CHECK(errorsOf(result).find("did you mean") == std::string::npos);
    }

    // The first name any object of `json` holds twice, or "".
    std::string heldTwice(const ALCVarGen::Json& json)
    {
        std::set<std::string> names;
        for (const auto& member : json.members)
        {
            if (!names.insert(member.name).second)
            {
                return member.name;
            }
            if (const std::string inner = heldTwice(member.value); !inner.empty())
            {
                return inner;
            }
        }
        for (const auto& item : json.items)
        {
            if (const std::string inner = heldTwice(item); !inner.empty())
            {
                return inner;
            }
        }
        return "";
    }

    TEST_CASE("alcvargen: cvars.json has one key a meaning and a slider's increment is ui_step", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"metres\"\nui_step = 8.0"));
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        auto result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);

        ALCVarGen::Json json;
        std::string     error;
        REQUIRE(ALCVarGen::parseJson(scratch.read("out/cvars.json"), json, error));
        CHECK(heldTwice(json) == "");
        const ALCVarGen::Json* settings = json.find("settings");
        REQUIRE(settings);
        REQUIRE(!settings->items.empty());
        const ALCVarGen::Json& distance = settings->items[0];
        CHECK(distance.string("name") == "render.draw_distance");
        CHECK(distance.numberOr("ui_step", 0.0) == 8.0);
        CHECK(distance.numberOr("step", 0.0) == 1.0);
        CHECK(scratch.read("out/src/cvt_render.cpp").find(".uiStep      = 8.0,") != std::string::npos);

        // step is the lock's.
        std::string one;
        result = oneSetting(scratch, "[render.glow]\ntype = \"F32\"\ndefault = 1.0\nstep = 0.1\nhelp = \"x\"\n", one);
        CHECK(hasError(result, one + ":7", "render.glow: a setting has no field 'step': a slider's increment is ui_step"));
    }

    const std::string SOFT = "\n[render.shadows.soft]\ntype    = \"bool\"\ndefault = true\nhelp    = \"x\"\n";

    TEST_CASE("alcvargen: a local build records a new setting's first step and refuses every other change",
              "[alcvargen]")
    {
        Scratch            scratch;
        const std::string  file    = scratch.write("render.toml", GOOD);
        const std::string  lock    = scratch.path("cvars.lock");
        ALCVarGen::Options options = optionsFor(scratch, { file }, lock);
        options.recordNew          = true;
        auto result                = ALCVarGen::run(options);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        CHECK(hasNote(result, "render.draw_distance: declared (step 1); recorded in the lock"));
        // As the lock's target records them.
        const std::string first = scratch.read("cvars.lock");
        CHECK(linesOf(first).size() == 2);
        REQUIRE(record(scratch, { file }, scratch.path("other.lock")).ok);
        CHECK(scratch.read("other.lock") == first);

        // A new setting beside a change: the one recorded, the other refused
        // until the lock's target records it; a new setting declared with a
        // migrate at once is a change too.
        const std::string level = "\n[render.shadows.level]\ntype    = \"S32\"\ndefault = 0\n"
                                  "migrate = [{ step = 2, reset = true }]\nhelp    = \"x\"\n";
        scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"kilometres\"") + SOFT + level);
        result = ALCVarGen::run(options);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, lock, "behind the schema"));
        auto lines = linesOf(scratch.read("cvars.lock"));
        REQUIRE(lines.size() == 3);
        CHECK(lines[0] == linesOf(first)[0]);
        CHECK(lines[2].starts_with(R"({"name": "render.shadows.soft", "step": 1, )"));

        // A schema with errors records nothing.
        const std::string before = scratch.read("cvars.lock");
        scratch.write("render.toml", GOOD + SOFT + "\n[render.shadows.hard]\ntype    = \"bool\"\ndefualt = true\nhelp    = \"x\"\n");
        CHECK_FALSE(ALCVarGen::run(options).ok);
        CHECK(scratch.read("cvars.lock") == before);

        // Strict, as CI runs it: every line the lock does not carry fails, a
        // new setting's too.
        options.recordNew = false;
        scratch.write("render.toml", GOOD + SOFT + "\n[render.shadows.hard]\ntype    = \"bool\"\ndefault = true\nhelp    = \"x\"\n");
        result = ALCVarGen::run(options);
        CHECK_FALSE(result.ok);
        CHECK(hasError(result, lock, "behind the schema"));
        CHECK(scratch.read("cvars.lock") == before);
    }

    TEST_CASE("alcvargen: recording folds the steps a setting gained since the committed lock into one", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string file = scratch.write("render.toml", GOOD);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string committed = scratch.write("committed.lock", scratch.read("cvars.lock"));

        // Changed and recorded, then changed again before a commit.
        scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"kilometres\"") + SOFT);
        REQUIRE(record(scratch, { file }, lock).ok);
        const std::string recorded = scratch.read("cvars.lock");
        REQUIRE(linesOf(recorded).size() == 4);
        scratch.write("render.toml", changed(GOOD, "unit    = \"metres\"", "unit    = \"centimetres\"") +
                                         changed(changed(SOFT, "\"bool\"", "\"S32\""), "default = true", "default = 1"));

        ALCVarGen::Options options = optionsFor(scratch, { file }, lock);
        options.writeLock          = true;
        options.committed          = committed;
        auto result                = ALCVarGen::run(options);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        CHECK(hasNote(result, "render.draw_distance: metres -> centimetres (step 2); stored values scale by 100.0"));
        CHECK(hasNote(result, "render.shadows.soft: declared (step 1)"));
        const auto folded = linesOf(scratch.read("cvars.lock"));
        REQUIRE(folded.size() == 4);
        CHECK(folded[0] == linesOf(scratch.read("committed.lock"))[0]);
        CHECK(folded[1].starts_with(R"({"name": "render.draw_distance", "step": 2, )"));
        CHECK(folded[1].ends_with(R"("type": "F32", "unit": "centimetres", "choices": [], "migrate": ""})"));
        CHECK(folded[3].starts_with(R"({"name": "render.shadows.soft", "step": 1, )"));
        CHECK(folded[3].find(R"("type": "S32")") != std::string::npos);
        result = generate(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);

        // No committed lock to read -- no git -- records as it would with no
        // fold: a step a recording.
        scratch.write("cvars.lock", recorded);
        options.committed = scratch.path("absent.lock");
        result            = ALCVarGen::run(options);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const auto stepped = linesOf(scratch.read("cvars.lock"));
        REQUIRE(stepped.size() == 6);
        CHECK(stepped[2].starts_with(R"({"name": "render.draw_distance", "step": 3, )"));
    }

    TEST_CASE("alcvargen: a map takes values of the step before to values of its own strings too", "[alcvargen]")
    {
        Scratch           scratch;
        const std::string mood = "[render]\nlabel = \"Rendering\"\n\n[render.mood]\ntype    = \"string\"\ndefault = \"calm\"\n"
                                 "choices = { calm = \"Calm\", busy = \"Busy\" }\nhelp    = \"x\"\n";
        const std::string file = scratch.write("render.toml", mood);
        const std::string lock = scratch.path("cvars.lock");
        REQUIRE(record(scratch, { file }, lock).ok);
        auto with = [&](const std::string& entry, const std::string& from) {
            scratch.write("render.toml", changed(from, "help    = \"x\"", entry + "\nhelp    = \"x\""));
            return generate(scratch, { file }, lock);
        };

        // A key the step before never held, a value the setting does not
        // offer, and a number for a string.
        auto result = with("migrate = [{ step = 2, map = { calm = \"busy\", idle = \"calm\" }, else = \"keep\" }]", mood);
        CHECK(hasError(result, file + ":8", "render.mood: the map's key idle is not among step 1's choices"));
        result = with("migrate = [{ step = 2, map = { calm = \"sleepy\" }, else = \"keep\" }]", mood);
        CHECK(hasError(result, file + ":8", "render.mood: the map gives sleepy, which is not among the choices"));
        result = with("migrate = [{ step = 2, map = { calm = 1 }, else = \"keep\" }]", mood);
        CHECK(hasError(result, file + ":8", "render.mood: the map gives 1, and step 2 holds a string"));
        result = with("migrate = [{ step = 2, map = { calm = \"busy\" }, else = 3 }]", mood);
        CHECK(hasError(result, file + ":8", "render.mood: the map's else gives 3, and step 2 holds a string"));

        // Remapped: recorded.
        const std::string remapped = changed(mood, "help    = \"x\"",
                                             "migrate = [{ step = 2, map = { calm = \"busy\" }, else = \"calm\" }]\nhelp    = \"x\"");
        scratch.write("render.toml", remapped);
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        REQUIRE(generate(scratch, { file }, lock).ok);
        CHECK(scratch.read("out/src/cvt_render.cpp").find(R"(.fromText = "calm", .toText = "busy")") != std::string::npos);

        // Its choices numbers now: the keys are the strings they were, and a
        // number is none of them.
        const std::string numbered = changed(changed(changed(remapped, "\"string\"", "\"S32\""), "default = \"calm\"", "default = 0"),
                                             "choices = { calm = \"Calm\", busy = \"Busy\" }", "choices = { 0 = \"Calm\", 1 = \"Busy\" }");
        result = with("", changed(numbered, "else = \"calm\" }]", "else = \"calm\" },\n           { step = 3, map = { 1 = 0 }, else = \"default\" }]"));
        CHECK(hasError(result, file + ":9", "render.mood: the map's key 1 is not among step 2's choices"));
        scratch.write("render.toml",
                      changed(numbered, "else = \"calm\" }]", "else = \"calm\" },\n           { step = 3, map = { calm = 0, busy = 1 }, else = \"default\" }]"));
        result = record(scratch, { file }, lock);
        INFO(errorsOf(result));
        CHECK(result.ok);
        // A choice gone, and what its map makes of it.
        CHECK(hasNote(result, "render.mood: the choice calm is gone (step 3); stored values of it become 0"));
        CHECK(hasNote(result, "render.mood: the choice busy is gone (step 3); stored values of it become 1"));
    }

    TEST_CASE("alcvargen: help names every option", "[alcvargen]")
    {
        std::ostringstream said;
        std::streambuf*    was    = std::cout.rdbuf(said.rdbuf());
        char               name[] = "alcvargen";
        char               help[] = "--help";
        char*              argv[] = { name, help };
        const int          code   = ALCVarGen::main(2, argv);
        std::cout.rdbuf(was);
        CHECK(code == 0);
        for (const char* option : { "--out", "--root", "--include-dir", "--lock", "--lock-command", "--base", "--committed",
                                    "--record-new", "--provisional", "--write-lock", "--words", "--check", "--where",
                                    "--diagnostics", "--json-schema", "--help" })
        {
            INFO(option);
            CHECK(said.str().find(std::string(option) + " ") != std::string::npos);
        }
        CHECK(said.str().find("doc/CVARS.md") != std::string::npos);
    }

    TEST_CASE("alcvargen: the JSON Schema an editor checks the TOML by is the one committed", "[alcvargen]")
    {
        Scratch            scratch;
        ALCVarGen::Options options;
        options.jsonSchema = scratch.path("cvars.schema.json");
        const auto result  = ALCVarGen::run(options);
        INFO(errorsOf(result));
        REQUIRE(result.ok);
        const std::string text = scratch.read("cvars.schema.json");

        ALCVarGen::Json json;
        std::string     error;
        REQUIRE(ALCVarGen::parseJson(text, json, error));
        CHECK(heldTwice(json) == "");
        CHECK(json.string("$schema") == "http://json-schema.org/draft-07/schema#");
        const ALCVarGen::Json* definitions = json.find("definitions");
        REQUIRE(definitions);
        const ALCVarGen::Json* setting = definitions->find("setting");
        REQUIRE(setting);
        const ALCVarGen::Json* fields = setting->find("properties");
        REQUIRE(fields);
        for (const char* field : { "type", "default", "help", "ui_step", "unit", "choices", "migrate", "from", "reuse" })
        {
            INFO(field);
            const ALCVarGen::Json* property = fields->find(field);
            REQUIRE(property);
            CHECK(!property->string("description").empty());
        }
        CHECK(fields->find("step") == nullptr);
        CHECK(text.find("\"metres\"") != std::string::npos);
        CHECK(text.find("\"F32\"") != std::string::npos);

        std::ifstream     in(fs::path(ALCVARGEN_COMMITTED_SCHEMA), std::ios::binary);
        std::stringstream committed;
        committed << in.rdbuf();
        INFO("write it again with: alcvargen --json-schema indra/cvars/cvars.schema.json");
        CHECK(committed.str() == text);
    }
}

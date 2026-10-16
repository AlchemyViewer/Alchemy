/**
 * @file alscripttheme_test.cpp
 * @brief The code editor's themes: the ones shipped, what their colours are
 * called, and which is chosen.
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

#include "../alscripttheme.h"

#include "alcolorsheet.h"
#include "alsurface.h"
#include "llcontrol.h"
#include "llui.h"
#include "llxmlnode.h"

#include "../../llui/tests/alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace
{
    struct alscripttheme_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static bool spoken(const std::string& name)
        {
            const std::vector<std::string>& names = ALScriptTheme::names();
            return std::find(names.begin(), names.end(), name) != names.end();
        }
    };

    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: every shipped theme loads and colours only names the editor knows", "[alscripttheme]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        S32 shipped = 0;
        for (const ALScriptTheme& theme : ALScriptTheme::available())
        {
            if (theme.own)
            {
                continue;
            }
            ++shipped;
            CAPTURE(theme.path);
            REQUIRE_FALSE(theme.name.empty());
            REQUIRE_FALSE(theme.colors.empty());
            for (const auto& [name, color] : theme.colors)
            {
                UNSCOPED_INFO("it colours " << name << ", which no editor is painted by");
                REQUIRE(spoken(name));
            }
        }
        CHECK(shipped > 1);
    }

    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: editor colours come first and every colour name has a label", "[alscripttheme]")
    {
        const std::vector<std::string>& names = ALScriptTheme::names();
        REQUIRE_FALSE(names.empty());
        UNSCOPED_INFO("the text first");
        CHECK(names.front() == std::string("ScriptText"));
        CHECK(ALScriptTheme::isEditorColor("ScriptBackground"));
        UNSCOPED_INFO("read as a person reads it");
        CHECK(ALScriptTheme::labelOf("ScriptBackground") == std::string("Background"));
        UNSCOPED_INFO("a kind is not the editor's");
        CHECK_FALSE(ALScriptTheme::isEditorColor(names.back()));
        UNSCOPED_INFO("a kind's name spaced");
        CHECK(ALScriptTheme::labelOf("ScriptDocComment") == std::string("Doc comment"));
        for (const std::string& name : names)
        {
            CAPTURE(name);
            REQUIRE_FALSE(ALScriptTheme::labelOf(name).empty());
        }
    }

    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: the chosen theme is read and written through the UI's setting", "[alscripttheme]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlGroup* config = LLUI::getInstance()->mSettingGroups["config"];
        if (!config->controlExists("ALScriptStudioTheme"))
        {
            config->declareString("ALScriptStudioTheme", std::string(), "The script editor's theme");
        }
        // The UI's settings are every UI test's: the theme chosen before put
        // back however this ends.
        struct ChosenBack
        {
            const std::string was = ALScriptTheme::chosen();
            ~ChosenBack() { ALScriptTheme::setChosen(was); }
        } chosen_back;
        ALScriptTheme::setChosen("Monokai");
        UNSCOPED_INFO("the setting says it");
        CHECK(config->getString("ALScriptStudioTheme") == std::string("Monokai"));
        UNSCOPED_INFO("and so does the theme");
        CHECK(ALScriptTheme::chosen() == std::string("Monokai"));
        ALScriptTheme::setChosen(std::string());
        REQUIRE(ALScriptTheme::chosen().empty());
    }

    // What reads less than it should: code against both grounds, the text against its bands, the line numbers
    // against the gutter.
    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: illegible reports each colour too faint against its ground", "[alscripttheme]")
    {
        ALScriptTheme theme;
        theme.colors["ScriptBackground"] = LLColor4(1.f, 1.f, 1.f, 1.f);
        theme.colors["ScriptText"]       = LLColor4(0.f, 0.f, 0.f, 1.f);
        theme.colors["ScriptComment"]    = LLColor4(0.2f, 0.4f, 0.2f, 1.f);
        UNSCOPED_INFO("black and a dark green on white read");
        REQUIRE(theme.illegible().empty());

        theme.colors["ScriptString"] = LLColor4(0.9f, 0.6f, 0.6f, 1.f);
        std::vector<ALScriptTheme::Illegible> said = theme.illegible();
        {
            INFO("a pale pink does not");
            REQUIRE(said.size() == 1);
            CHECK(said[0].name == "ScriptString");
            CHECK(said[0].against == "ScriptBackground");
            CHECK(said[0].contrast < ALSurface::LEGIBLE);
        }
        theme.colors.erase("ScriptString");

        theme.colors["ScriptBgReadOnlyColor"] = LLColor4(0.3f, 0.3f, 0.3f, 1.f);
        said                                  = theme.illegible();
        {
            INFO("a dark read-only ground takes both from the text");
            REQUIRE(said.size() == 2);
            CHECK(said[0].against == "ScriptBgReadOnlyColor");
        }
        theme.colors.erase("ScriptBgReadOnlyColor");

        theme.colors["ScriptSelectionColor"] = LLColor4(0.f, 0.f, 0.3f, 0.9f);
        said                                 = theme.illegible();
        {
            INFO("a selection the text is lost in, as laid over the ground");
            REQUIRE(said.size() == 1);
            CHECK(said[0].name == "ScriptText");
            CHECK(said[0].against == "ScriptSelectionColor");
        }
        theme.colors["ScriptSelectionColor"] = LLColor4(0.f, 0.f, 0.3f, 0.2f);
        UNSCOPED_INFO("thin enough, it reads");
        REQUIRE(theme.illegible().empty());

        theme.colors["ScriptGutterColor"]     = LLColor4(0.95f, 0.95f, 0.95f, 1.f);
        theme.colors["ScriptLineNumberColor"] = LLColor4(0.75f, 0.75f, 0.75f, 1.f);
        said                                  = theme.illegible();
        {
            INFO("faint numbers on the gutter");
            REQUIRE(said.size() == 1);
            CHECK(said[0].against == "ScriptGutterColor");
        }
        theme.colors["ScriptLineNumberColor"] = LLColor4(0.5f, 0.5f, 0.5f, 1.f);
        {
            INFO("numbers are held to less than the code");
            REQUIRE(theme.illegible().empty());
            REQUIRE_FALSE(theme.illegible(7.f, 4.5f).empty());
        }
    }

    // Every theme the viewer ships reads: the text and each kind of code at 4.5:1, the line numbers at 3:1; the
    // high-contrast ones at 7 and 4.5.
    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: every shipped theme reads at its contrast thresholds", "[alscripttheme]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        S32 high = 0;
        for (const ALScriptTheme& theme : ALScriptTheme::available())
        {
            if (theme.own)
            {
                continue;
            }
            CAPTURE(theme.name);
            const bool contrast = theme.name.find("High Contrast") != std::string::npos;
            high += contrast;
            std::string said;
            for (const ALScriptTheme::Illegible& each : contrast ? theme.illegible(7.f, 4.5f) : theme.illegible())
            {
                said += "\n  " + each.name + " on " + each.against + ": " + std::to_string(each.contrast);
            }
            UNSCOPED_INFO("colours that read too faintly:" << said);
            REQUIRE(said.empty());
        }
        UNSCOPED_INFO("a dark and a light high-contrast theme");
        CHECK(high == 2);
    }

    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: every colour a theme speaks for has its label in strings.xml", "[alscripttheme]")
    {
#ifdef LLUI_TEST_APP_DIR
        std::ifstream in(std::string(LLUI_TEST_APP_DIR) + "/skins/default/xui/en/strings.xml");
        if (!in)
        {
            SKIP("no source tree: LLUI_TEST_APP_DIR does not point at newview");
        }
        const std::string strings((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const std::string& name : ALScriptTheme::names())
        {
            CAPTURE(name);
            REQUIRE(strings.find("name=\"ScriptColorLabel_" + name + "\"") != std::string::npos);
        }
#else
        SKIP("no LLUI_TEST_APP_DIR");
#endif
    }

    // Every shipped skin's script colours read: the text and each kind of
    // code against the ground and the read-only ground, the text against
    // the caret's line and the selection, the line numbers against the
    // gutter -- the dark skins included, whose defaults were once the
    // white skin's.
    TEST_CASE_METHOD(alscripttheme_data, "alscripttheme: every shipped skin's script colours read and its studio parts differ", "[alscripttheme]")
    {
#ifdef LLUI_TEST_APP_DIR
        const std::string skins = std::string(LLUI_TEST_APP_DIR) + "/skins/";
        LLXMLNodePtr default_root;
        if (!LLXMLNode::parseFile(skins + "default/colors.xml", default_root, nullptr))
        {
            SKIP("no source tree: LLUI_TEST_APP_DIR does not point at newview");
        }
        for (const char* skin : { "default", "alchemy", "gemini", "heretic", "ionic" })
        {
            CAPTURE(skin);
            ALColorSheet sheet;
            sheet.read(default_root, "default/colors.xml");
            if (std::string(skin) != "default")
            {
                LLXMLNodePtr root;
                REQUIRE(LLXMLNode::parseFile(skins + skin + "/colors.xml", root, nullptr));
                sheet.read(root, std::string(skin) + "/colors.xml");
            }
            sheet.resolve();
            std::string said;
            const auto  lookup = [&sheet](const std::string& name) -> std::optional<LLColor4> {
                const LLColor4* found = sheet.find(name);
                return found ? std::optional<LLColor4>(*found) : std::nullopt;
            };
            for (const ALScriptTheme::Illegible& each : ALScriptTheme::illegible(lookup))
            {
                said += "\n  " + each.name + " on " + each.against + ": " + std::to_string(each.contrast);
            }
            UNSCOPED_INFO("script colours that read too faintly:" << said);
            REQUIRE(said.empty());
            // And the studios' parts told apart by what they are: a divider
            // from the panes it divides, a tab not chosen from the chosen
            // one, the notice and the strips from the panes.
            const auto require_apart = [&](const char* a, const char* b, F32 least) {
                CAPTURE(a, b);
                const LLColor4* x = sheet.find(a);
                const LLColor4* y = sheet.find(b);
                REQUIRE(x);
                REQUIRE(y);
                REQUIRE(ALSurface::contrast(*x, *y) >= least);
            };
            require_apart("StudioDividerColor", "StudioPaneColor", 1.3f);
            require_apart("StudioTabColor", "PanelDefaultBackgroundColor", 1.08f);
            require_apart("StudioNoticeColor", "StudioPaneColor", 1.08f);
            require_apart("StudioStripColor", "StudioPaneColor", 1.05f);
        }
#else
        SKIP("no LLUI_TEST_APP_DIR");
#endif
    }
}

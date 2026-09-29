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

#include "../test/lltut.h"

#include <algorithm>
#include <fstream>
#include <iterator>

class LLAvatarName;
const std::string gScriptThemeTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gScriptThemeTestAnonName;
}

namespace tut
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

    typedef test_group<alscripttheme_data> alscripttheme_test;
    typedef alscripttheme_test::object     alscripttheme_object;
    tut::alscripttheme_test alscripttheme_testgroup("alscripttheme");

    template<> template<>
    void alscripttheme_object::test<1>()
    {
        set_test_name("every theme the viewer ships reads, and colours only what a theme speaks for");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        S32 shipped = 0;
        for (const ALScriptTheme& theme : ALScriptTheme::available())
        {
            if (theme.own)
            {
                continue;
            }
            ++shipped;
            ensure("a name: " + theme.path, !theme.name.empty());
            ensure("colours: " + theme.name, !theme.colors.empty());
            for (const auto& [name, color] : theme.colors)
            {
                ensure(theme.name + " colours " + name + ", which no editor is painted by", spoken(name));
            }
        }
        ensure("the viewer ships themes", shipped > 1);
    }

    template<> template<>
    void alscripttheme_object::test<2>()
    {
        set_test_name("the editor's own colours come first, and every name reads as words");
        const std::vector<std::string>& names = ALScriptTheme::names();
        ensure("names", !names.empty());
        ensure_equals("the text first", names.front(), std::string("ScriptText"));
        ensure("an editor colour", ALScriptTheme::isEditorColor("ScriptBackground"));
        ensure_equals("read as a person reads it", ALScriptTheme::labelOf("ScriptBackground"), std::string("Background"));
        ensure("a kind is not the editor's", !ALScriptTheme::isEditorColor(names.back()));
        ensure_equals("a kind's name spaced", ALScriptTheme::labelOf("ScriptDocComment"), std::string("Doc comment"));
        for (const std::string& name : names)
        {
            ensure("a label for " + name, !ALScriptTheme::labelOf(name).empty());
        }
    }

    template<> template<>
    void alscripttheme_object::test<3>()
    {
        set_test_name("the theme chosen is the UI's setting, read and written through it");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlGroup* config = LLUI::getInstance()->mSettingGroups["config"];
        if (!config->controlExists("ALScriptStudioTheme"))
        {
            config->declareString("ALScriptStudioTheme", std::string(), "The script editor's theme");
        }
        ALScriptTheme::setChosen("Monokai");
        ensure_equals("the setting says it", config->getString("ALScriptStudioTheme"), std::string("Monokai"));
        ensure_equals("and so does the theme", ALScriptTheme::chosen(), std::string("Monokai"));
        ALScriptTheme::setChosen(std::string());
        ensure("none", ALScriptTheme::chosen().empty());
    }

    template<> template<>
    void alscripttheme_object::test<4>()
    {
        set_test_name("what reads less than it should: code against both grounds, the text against its bands, the line numbers against the gutter");
        ALScriptTheme theme;
        theme.colors["ScriptBackground"] = LLColor4(1.f, 1.f, 1.f, 1.f);
        theme.colors["ScriptText"]       = LLColor4(0.f, 0.f, 0.f, 1.f);
        theme.colors["ScriptComment"]    = LLColor4(0.2f, 0.4f, 0.2f, 1.f);
        ensure("black and a dark green on white read", theme.illegible().empty());

        theme.colors["ScriptString"] = LLColor4(0.9f, 0.6f, 0.6f, 1.f);
        std::vector<ALScriptTheme::Illegible> said = theme.illegible();
        ensure("a pale pink does not", said.size() == 1 && said[0].name == "ScriptString" && said[0].against == "ScriptBackground" &&
                                           said[0].contrast < ALSurface::LEGIBLE);
        theme.colors.erase("ScriptString");

        theme.colors["ScriptBgReadOnlyColor"] = LLColor4(0.3f, 0.3f, 0.3f, 1.f);
        said                                  = theme.illegible();
        ensure("a dark read-only ground takes both from the text", said.size() == 2 && said[0].against == "ScriptBgReadOnlyColor");
        theme.colors.erase("ScriptBgReadOnlyColor");

        theme.colors["ScriptSelectionColor"] = LLColor4(0.f, 0.f, 0.3f, 0.9f);
        said                                 = theme.illegible();
        ensure("a selection the text is lost in, as laid over the ground",
               said.size() == 1 && said[0].name == "ScriptText" && said[0].against == "ScriptSelectionColor");
        theme.colors["ScriptSelectionColor"] = LLColor4(0.f, 0.f, 0.3f, 0.2f);
        ensure("thin enough, it reads", theme.illegible().empty());

        theme.colors["ScriptGutterColor"]     = LLColor4(0.95f, 0.95f, 0.95f, 1.f);
        theme.colors["ScriptLineNumberColor"] = LLColor4(0.75f, 0.75f, 0.75f, 1.f);
        said                                  = theme.illegible();
        ensure("faint numbers on the gutter", said.size() == 1 && said[0].against == "ScriptGutterColor");
        theme.colors["ScriptLineNumberColor"] = LLColor4(0.5f, 0.5f, 0.5f, 1.f);
        ensure("numbers are held to less than the code", theme.illegible().empty() && !theme.illegible(7.f, 4.5f).empty());
    }

    template<> template<>
    void alscripttheme_object::test<5>()
    {
        set_test_name("every theme the viewer ships reads: the text and each kind of code at 4.5:1, the line numbers at 3:1; the high-contrast ones at 7 and 4.5");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        S32 high = 0;
        for (const ALScriptTheme& theme : ALScriptTheme::available())
        {
            if (theme.own)
            {
                continue;
            }
            const bool contrast = theme.name.find("High Contrast") != std::string::npos;
            high += contrast;
            std::string said;
            for (const ALScriptTheme::Illegible& each : contrast ? theme.illegible(7.f, 4.5f) : theme.illegible())
            {
                said += "\n  " + each.name + " on " + each.against + ": " + std::to_string(each.contrast);
            }
            ensure(theme.name + " reads:" + said, said.empty());
        }
        ensure_equals("a dark and a light high-contrast theme", high, 2);
    }

    template<> template<>
    void alscripttheme_object::test<6>()
    {
        set_test_name("every colour a theme speaks for has its label in strings.xml, to be translated");
#ifdef LLUI_TEST_APP_DIR
        std::ifstream in(std::string(LLUI_TEST_APP_DIR) + "/skins/default/xui/en/strings.xml");
        if (!in)
        {
            skip("no source tree: LLUI_TEST_APP_DIR does not point at newview");
        }
        const std::string strings((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const std::string& name : ALScriptTheme::names())
        {
            ensure("a label for " + name, strings.find("name=\"ScriptColorLabel_" + name + "\"") != std::string::npos);
        }
#else
        skip("no LLUI_TEST_APP_DIR");
#endif
    }

    // Every shipped skin's script colours read: the text and each kind of
    // code against the ground and the read-only ground, the text against
    // the caret's line and the selection, the line numbers against the
    // gutter -- the dark skins included, whose defaults were once the
    // white skin's.
    template<> template<>
    void alscripttheme_object::test<7>()
    {
#ifdef LLUI_TEST_APP_DIR
        const std::string skins = std::string(LLUI_TEST_APP_DIR) + "/skins/";
        LLXMLNodePtr default_root;
        if (!LLXMLNode::parseFile(skins + "default/colors.xml", default_root, nullptr))
        {
            skip("no source tree: LLUI_TEST_APP_DIR does not point at newview");
        }
        for (const char* skin : { "default", "alchemy", "gemini", "heretic", "ionic" })
        {
            ALColorSheet sheet;
            sheet.read(default_root, "default/colors.xml");
            if (std::string(skin) != "default")
            {
                LLXMLNodePtr root;
                ensure(std::string(skin) + " parses", LLXMLNode::parseFile(skins + skin + "/colors.xml", root, nullptr));
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
            ensure(std::string(skin) + "'s script colours read:" + said, said.empty());
            // And the studios' parts told apart by what they are: a divider
            // from the panes it divides, a tab not chosen from the chosen
            // one, the notice and the strips from the panes.
            const auto apart = [&](const char* a, const char* b, F32 least) {
                const LLColor4* x = sheet.find(a);
                const LLColor4* y = sheet.find(b);
                ensure(std::string(skin) + " has " + a + " and " + b, x && y);
                ensure(std::string(skin) + ": " + a + " apart from " + b, ALSurface::contrast(*x, *y) >= least);
            };
            apart("StudioDividerColor", "StudioPaneColor", 1.3f);
            apart("StudioTabColor", "PanelDefaultBackgroundColor", 1.08f);
            apart("StudioNoticeColor", "StudioPaneColor", 1.08f);
            apart("StudioStripColor", "StudioPaneColor", 1.05f);
        }
#else
        skip("no LLUI_TEST_APP_DIR");
#endif
    }
}

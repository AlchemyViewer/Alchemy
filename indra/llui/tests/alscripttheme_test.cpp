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

#include "llcontrol.h"
#include "llui.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <algorithm>

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

}

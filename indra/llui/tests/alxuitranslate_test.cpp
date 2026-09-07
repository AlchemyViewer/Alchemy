/**
 * @file alxuitranslate_test.cpp
 * @brief What a language has, what the merge does with it, and what writing it does to the file.
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

#include "../alxuitranslate.h"

#include "../alxuiedit.h"

#include "../test/lltut.h"

#include <pugixml.hpp>

class LLAvatarName;
const std::string gTranslateTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gTranslateTestAnonName;
}

namespace tut
{
    struct alxuitranslate_data
    {
        // A file with one of each thing a translator meets: a label, a
        // tool tip with a placeholder, a body text, a value that is a
        // label and a value that is not, a number, and an element the
        // file says is not to be translated.
        static const char* english()
        {
            return "<panel name=\"root\" title=\"Places\">\n"
                   "    <text name=\"greeting\">Hello there</text>\n"
                   "    <button name=\"ok\" label=\"OK\" tool_tip=\"Save [COUNT] items\"/>\n"
                   "    <panel name=\"inner\">\n"
                   "        <text name=\"deep\" value=\"Nested\"/>\n"
                   "    </panel>\n"
                   "    <text name=\"count\" value=\"1024\"/>\n"
                   "    <text name=\"size\" value=\"800 x 600\"/>\n"
                   "    <text name=\"code\" translate=\"false\" value=\"HTTP\"/>\n"
                   "</panel>\n";
        }

        struct Doc
        {
            pugi::xml_document doc;

            pugi::xml_node load(const char* text)
            {
                doc.load_string(text, ALXmlDocumentFlags);
                return doc.document_element();
            }

            static constexpr unsigned int ALXmlDocumentFlags = pugi::parse_default | pugi::parse_ws_pcdata;
        };

        static const ALXUITranslate::Unit* find(const ALXUITranslate& units, const std::string& path,
                                                const std::string& field)
        {
            for (const ALXUITranslate::Unit& unit : units.units())
            {
                std::string joined;
                for (const std::string& step : unit.path)
                {
                    joined += joined.empty() ? step : "/" + step;
                }
                if (joined == path && unit.field == field)
                {
                    return &unit;
                }
            }
            return nullptr;
        }

        static std::string describe(const ALXUITranslate& units)
        {
            std::string text;
            for (const ALXUITranslate::Unit& unit : units.units())
            {
                for (const std::string& step : unit.path)
                {
                    text += step + "/";
                }
                text += "@" + (unit.field.empty() ? std::string("text") : unit.field)
                      + "=" + std::to_string((int)unit.state) + " ";
            }
            return text;
        }
    };

    typedef test_group<alxuitranslate_data> alxuitranslate_test;
    typedef alxuitranslate_test::object     alxuitranslate_object;
    tut::alxuitranslate_test alxuitranslate_testgroup("alxuitranslate");

    // What counts as a string someone translates, which is the rule the
    // translation tables have been built from.
    template<> template<>
    void alxuitranslate_object::test<1>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());
        ALXUITranslate units;
        units.scan(root, pugi::xml_node());

        ensure("the title is a unit", find(units, "", "title"));
        ensure("a label is a unit", find(units, "ok", "label"));
        ensure("a tool tip is a unit", find(units, "ok", "tool_tip"));
        ensure("body text is a unit", find(units, "greeting", ""));
        ensure("a value with no text or label of its own is a unit", find(units, "inner/deep", "value"));
        ensure("a number is not: " + describe(units), !find(units, "count", "value"));
        ensure("a resolution is not", !find(units, "size", "value"));
        ensure("what the file forbids is not", !find(units, "code", "value"));
        ensure_equals("and with no language, every one of them is missing",
                      units.count(ALXUITranslate::State::Missing), (S32)units.units().size());
    }

    // The four states a language's value can be in.
    template<> template<>
    void alxuitranslate_object::test<2>()
    {
        Doc base;
        Doc other;
        pugi::xml_node root = base.load(english());
        pugi::xml_node overlay = other.load(
            "<panel name=\"root\" title=\"Lugares\">\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <button name=\"ok\" label=\"Aceptar\" tool_tip=\"Guardar elementos\"/>\n"
            "    <text name=\"deep\" value=\"Anidado\"/>\n"
            "    <text name=\"gone\" value=\"Nada\"/>\n"
            "    <text name=\"code\" value=\"HTTP\"/>\n"
            "</panel>\n");

        ALXUITranslate units;
        units.scan(root, overlay);

        const ALXUITranslate::Unit* title = find(units, "", "title");
        ensure("the title is there", title);
        ensure_equals("and applies", (int)title->state, (int)ALXUITranslate::State::Translated);

        const ALXUITranslate::Unit* tip = find(units, "ok", "tool_tip");
        ensure("the tool tip is there", tip);
        ensure_equals("and lost its placeholder", (int)tip->state, (int)ALXUITranslate::State::Placeholders);

        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        ensure("the nested one is there", deep);
        ensure_equals("written at the wrong path, so it applies to nothing: " + describe(units),
                      (int)deep->state, (int)ALXUITranslate::State::NotApplied);
        ensure_equals("and the reason is that the base has it elsewhere",
                      (int)deep->miss, (int)ALXUITranslate::Miss::Moved);

        const ALXUITranslate::Unit* code = find(units, "code", "value");
        ensure("what the file forbids, translated anyway, is shown", code);
        ensure_equals("as forbidden", (int)code->state, (int)ALXUITranslate::State::Forbidden);

        const ALXUITranslate::Unit* gone = find(units, "gone", "value");
        ensure("a name the base does not have is shown", gone);
        ensure_equals("as applying to nothing", (int)gone->state, (int)ALXUITranslate::State::NotApplied);
        ensure_equals("with nowhere to put it", (int)gone->miss, (int)ALXUITranslate::Miss::Absent);
    }

    // A missing unit is created with the ancestors the base gives it,
    // each carrying nothing but its name.
    template<> template<>
    void alxuitranslate_object::test<3>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer("<panel name=\"root\">\n</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        ensure("the unit is there", deep);

        std::string error;
        ensure("writes: " + error, ALXUITranslate::write(overlay, root, *deep, "Anidado", error));
        ensure_equals("the chain, and nothing else on it", overlay.text(),
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // A unit the language put at the wrong path is moved to the right
    // one, and every other byte of the file is left alone.
    template<> template<>
    void alxuitranslate_object::test<4>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <!-- a comment that stays -->\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <text name=\"deep\" value=\"viejo\"/>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        ensure("the unit is there", deep);
        ensure_equals("and the language has it in the wrong place",
                      (int)deep->state, (int)ALXUITranslate::State::NotApplied);

        std::string error;
        ensure("writes: " + error, ALXUITranslate::write(overlay, root, *deep, "Anidado", error));
        ensure_equals("moved under the ancestor it needed", overlay.text(),
            "<panel name=\"root\">\n"
            "    <!-- a comment that stays -->\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // The four ways an overlay is broken by writing it, each refused.
    template<> template<>
    void alxuitranslate_object::test<5>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer("<panel name=\"root\">\n</panel>\n"));
        const std::string before = overlay.text();

        ALXUITranslate units;
        units.scan(root, overlay.root());

        std::string error;
        ALXUITranslate::Unit unit;
        unit.path = { "ok" };
        unit.field = "label";

        ensure("an empty translation is refused", !ALXUITranslate::write(overlay, root, unit, "", error));
        ensure("and says why", !error.empty());

        unit.field = "left";
        ensure("a layout attribute is refused", !ALXUITranslate::write(overlay, root, unit, "10", error));

        unit.field = "description";
        ensure("an attribute the base does not carry is refused",
               !ALXUITranslate::write(overlay, root, unit, "Algo", error));

        unit.path = { "code" };
        unit.field = "value";
        ensure("what the file forbids is refused", !ALXUITranslate::write(overlay, root, unit, "HTTP", error));

        ensure_equals("and none of them wrote anything", overlay.text(), before);
    }
}

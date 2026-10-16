/**
 * @file alxuicatalog_test.cpp
 * @brief The catalog sees every layer of every file, and finds what is in them.
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

#include "alxuicatalog.h"
#include "alxuiselection.h"
#include "../llbutton.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "altesttempdir.h"

#include "lldir.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace
{
    // A skin tree of two skins and two languages: one floater in every
    // layer but "other/de", a panel and a template in default/en only, one
    // file that does not parse, and a menu.
    struct alxuicatalog_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A tree of its own for each test, removed as it ends.
        ll_test::TempDir mTemp{ "alxuicatalog_test" };
        std::string      mSkins = mTemp.string();

        alxuicatalog_data()
        {
            write("default/xui/en/floater_a.xml",
                  "<floater name=\"a\" title=\"Floater A\" width=\"100\" height=\"50\">\n"
                  "  <floater.string name=\"greeting\">Hello</floater.string>\n"
                  "  <panel name=\"body\" width=\"10\" height=\"10\">\n"
                  "    <button name=\"ok\" label=\"OK\"/>\n"
                  "    <button name=\"ok\" label=\"Also OK\"/>\n"
                  "  </panel>\n"
                  "</floater>\n");
            write("default/xui/de/floater_a.xml",
                  "<floater name=\"a\" title=\"Fenster A\">\n"
                  "  <panel name=\"body\">\n"
                  "    <button name=\"ok\" label=\"Gut\"/>\n"
                  "  </panel>\n"
                  "</floater>\n");
            write("other/xui/en/floater_a.xml",
                  "<floater name=\"a\" title=\"Other A\" width=\"200\" height=\"60\"/>\n");
            write("default/xui/en/panel_b.xml",
                  "<panel name=\"b\" width=\"10\" height=\"10\"/>\n");
            write("default/xui/en/menu_c.xml",
                  "<menu name=\"c\"><menu_item_call name=\"item\" label=\"Item\"/></menu>\n");
            write("default/xui/en/widgets/button.xml",
                  "<button name=\"button\" font=\"SansSerif\"/>\n");
            write("default/xui/de/broken.xml",
                  "<floater name=\"broken\">\n<panel>\n");
        }

        void write(const std::string& relative, const std::string& text)
        {
            std::string path = mSkins;
            std::string dir = mSkins;
            LLFile::mkdir(dir);
            size_t start = 0;
            while (true)
            {
                const size_t slash = relative.find('/', start);
                if (slash == std::string::npos)
                {
                    break;
                }
                dir = gDirUtilp->add(dir, relative.substr(start, slash - start));
                LLFile::mkdir(dir);
                start = slash + 1;
            }
            path = gDirUtilp->add(dir, relative.substr(start));
            llofstream out(path, std::ios::binary);
            out << text;
        }

        static size_t hitsIn(const std::vector<ALXUICatalog::Hit>& hits, const char* file)
        {
            size_t n = 0;
            for (const ALXUICatalog::Hit& hit : hits)
            {
                n += hit.entry->name == file;
            }
            return n;
        }
    };

    // Every file appears once with every layer that exists, its kind from
    // the root tag, and the skins and languages in the order the tool
    // offers them.
    TEST_CASE_METHOD(alxuicatalog_data, "alxuicatalog: scan lists each file once with its layers and its kind from the root tag", "[alxuicatalog]")
    {
        ALXUICatalog catalog;
        catalog.scan(mSkins);

        REQUIRE(catalog.skins().size() == 2u);
        CHECK(catalog.skins()[0] == std::string("default"));
        REQUIRE(catalog.languages().size() == 2u);
        CHECK(catalog.languages()[0] == std::string("en"));
        CHECK(catalog.entries().size() == 5u);

        const ALXUICatalog::Entry* a = catalog.find("floater_a.xml");
        REQUIRE(a != nullptr);
        CHECK((int)a->kind == (int)ALXUICatalog::Kind::Floater);
        CHECK(a->title == std::string("Floater A"));
        CHECK(a->layers.size() == 3u);
        CHECK(a->layer("default", "en") != nullptr);
        CHECK(a->layer("default", "de") != nullptr);
        CHECK(a->layer("other", "en") != nullptr);
        CHECK(a->layer("other", "de") == nullptr);

        const ALXUICatalog::Entry* panel = catalog.find("panel_b.xml");
        REQUIRE(panel != nullptr);
        CHECK((int)panel->kind == (int)ALXUICatalog::Kind::Panel);
        const ALXUICatalog::Entry* menu = catalog.find("menu_c.xml");
        REQUIRE(menu != nullptr);
        CHECK((int)menu->kind == (int)ALXUICatalog::Kind::Menu);
        const ALXUICatalog::Entry* tmpl = catalog.find("widgets/button.xml");
        UNSCOPED_INFO("template catalogued under its subdirectory");
        REQUIRE(tmpl != nullptr);
        CHECK((int)tmpl->kind == (int)ALXUICatalog::Kind::Template);

        const ALXUICatalog::Entry* broken = catalog.find("broken.xml");
        UNSCOPED_INFO("a file that does not parse is still catalogued");
        REQUIRE(broken != nullptr);
        REQUIRE(broken->layers.size() == 1u);
        CHECK(broken->layers[0].doc == nullptr);
        UNSCOPED_INFO("and the reason");
        CHECK_FALSE(broken->layers[0].error.empty());
        UNSCOPED_INFO("on its line");
        CHECK(broken->layers[0].errorLine >= 2);
    }

    // The files a build reads, in the order the merge reads them: a skin's
    // base-language file replaces default's, and the language file comes
    // from whichever skin has it.
    TEST_CASE_METHOD(alxuicatalog_data, "alxuicatalog: layersFor gives the files a build reads in the order the merge reads them", "[alxuicatalog]")
    {
        ALXUICatalog catalog;
        catalog.scan(mSkins);
        const ALXUICatalog::Entry* found = catalog.find("floater_a.xml");
        REQUIRE(found != nullptr);
        const ALXUICatalog::Entry& a = *found;

        std::vector<const ALXUICatalog::Layer*> layers = catalog.layersFor(a, "default", "en");
        UNSCOPED_INFO("default/en: one file");
        REQUIRE(layers.size() == 1u);
        CHECK(layers[0]->skin == std::string("default"));

        layers = catalog.layersFor(a, "default", "de");
        UNSCOPED_INFO("default/de: two files");
        REQUIRE(layers.size() == 2u);
        UNSCOPED_INFO("base first");
        CHECK(layers[0]->language == std::string("en"));
        UNSCOPED_INFO("then the language");
        CHECK(layers[1]->language == std::string("de"));

        layers = catalog.layersFor(a, "other", "de");
        UNSCOPED_INFO("other/de: two files");
        REQUIRE(layers.size() == 2u);
        UNSCOPED_INFO("the skin's own base");
        CHECK(layers[0]->skin == std::string("other"));
        UNSCOPED_INFO("default's language file");
        CHECK(layers[1]->skin == std::string("default"));

        const ALXUICatalog::Entry* b = catalog.find("panel_b.xml");
        REQUIRE(b != nullptr);
        layers = catalog.layersFor(*b, "other", "de");
        UNSCOPED_INFO("a file the skin lacks falls back to default");
        REQUIRE(layers.size() == 1u);
        CHECK(layers[0]->skin == std::string("default"));
    }

    // Find answers by tag, by attribute name, by attribute value, by name
    // and by text, with the file and line of each hit; and a name path
    // resolves back to the element, ordinals included.
    TEST_CASE_METHOD(alxuicatalog_data, "alxuicatalog: find searches each field and a name path resolves back to its element", "[alxuicatalog]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // The path rules count widget siblings, which asks the registries;
        // naming the widgets' parameter blocks links their registrations.
        LLUICtrlFactory::getDefaultParams<LLPanel>();
        LLUICtrlFactory::getDefaultParams<LLButton>();

        ALXUICatalog catalog;
        catalog.scan(mSkins);

        std::vector<ALXUICatalog::Hit> hits = catalog.find("button", ALXUICatalog::Field::Tag);
        UNSCOPED_INFO("buttons by tag: two in en, one in de, the template");
        REQUIRE(hits.size() == 4u);
        CHECK(hitsIn(hits, "floater_a.xml") == 3u);

        hits = catalog.find("button", ALXUICatalog::Field::Tag, ALXUICatalog::Match::Containing, "default", "en");
        UNSCOPED_INFO("scoped to a skin and language");
        CHECK(hits.size() == 3u);

        hits = catalog.find("title", ALXUICatalog::Field::Attribute);
        UNSCOPED_INFO("title attributes: one per floater layer");
        CHECK(hits.size() == 3u);

        hits = catalog.find("also ok", ALXUICatalog::Field::Value);
        UNSCOPED_INFO("a value, case folded");
        REQUIRE(hits.size() == 1u);
        CHECK(hits[0].line == 5);
        CHECK(hits[0].path == std::string("body/ok#1"));
        CHECK(hits[0].snippet == std::string("label=\"Also OK\""));

        hits = catalog.find("greeting", ALXUICatalog::Field::Name);
        REQUIRE(hits.size() == 1u);
        UNSCOPED_INFO("on the string's line");
        CHECK(hits[0].line == 2);

        hits = catalog.find("hello", ALXUICatalog::Field::Text);
        REQUIRE(hits.size() == 1u);
        CHECK(hits[0].snippet == std::string("Hello"));

        hits = catalog.find("Fenster", ALXUICatalog::Field::Any);
        REQUIRE(hits.size() == 1u);
        UNSCOPED_INFO("in the German layer");
        CHECK(hits[0].layer->language == std::string("de"));

        const ALXUICatalog::Entry* floater = catalog.find("floater_a.xml");
        REQUIRE(floater != nullptr);
        const ALXUICatalog::Layer* en = floater->layer("default", "en");
        REQUIRE(en != nullptr);
        pugi::xml_node second = ALXUICatalog::resolve(en->root(), ALXUISelection::fromString("body/ok#1"));
        UNSCOPED_INFO("the second button resolves");
        REQUIRE(second);
        CHECK(std::string(second.attribute("label").value()) == std::string("Also OK"));
        UNSCOPED_INFO("a path nothing has resolves to nothing");
        CHECK_FALSE(ALXUICatalog::resolve(en->root(), ALXUISelection::fromString("body/ok#2")));
        UNSCOPED_INFO("and back again");
        CHECK(ALXUISelection::toString(ALXUICatalog::namePath(second)) == std::string("body/ok#1"));
    }

    // A file element with no name is known by its value in the file's
    // vocabulary, as the merge knows a combo box's items, and by "unnamed"
    // in a view's, which is what the widget built from it is called. Both
    // reach it: a path taken from a view finds the element the merge would.
    TEST_CASE_METHOD(alxuicatalog_data, "alxuicatalog: an element with no name is known by its value in the file's vocabulary", "[alxuicatalog]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLUICtrlFactory::getDefaultParams<LLPanel>();

        const std::string xml =
            "<panel name=\"p\">\n"
            "  <combo_box name=\"c\">\n"
            "    <item value=\"v1\" label=\"One\"/>\n"
            "    <item value=\"v2\" label=\"Two\"/>\n"
            "    <item label=\"Three\"/>\n"
            "  </combo_box>\n"
            "  <panel value=\"x\" width=\"1\"/>\n"
            "  <panel value=\"y\" width=\"2\"/>\n"
            "</panel>\n";
        pugi::xml_document doc;
        REQUIRE(doc.load_string(xml.c_str()));
        const pugi::xml_node root = doc.document_element();
        const pugi::xml_node two = root.child("combo_box").child("item").next_sibling("item");
        const pugi::xml_node three = two.next_sibling("item");
        const pugi::xml_node y = root.last_child();

        UNSCOPED_INFO("an item is known by its value");
        CHECK(ALXUISelection::toString(ALXUICatalog::namePath(two, true)) ==
                      std::string("c/v2"));
        // One with neither is unnamed, and counted among every sibling with
        // no name, since that is what a step of "unnamed" counts.
        UNSCOPED_INFO("and one with neither is unnamed");
        CHECK(ALXUISelection::toString(ALXUICatalog::namePath(three, true)) ==
                      std::string("c/unnamed#2"));
        UNSCOPED_INFO("the value resolves");
        CHECK(ALXUICatalog::resolve(root, ALXUISelection::fromString("c/v2"), true) == two);
        UNSCOPED_INFO("and unnamed, counted among the ones with no name, reaches the third");
        CHECK(ALXUICatalog::resolve(root, ALXUISelection::fromString("c/unnamed#2"), true) == three);
        UNSCOPED_INFO("a value the file lacks resolves to nothing");
        CHECK_FALSE(ALXUICatalog::resolve(root, ALXUISelection::fromString("c/v3"), true));

        // A widget with a value and no name: the view is "unnamed", and a
        // path taken from the view reaches the file's element all the same.
        UNSCOPED_INFO("a widget's path says unnamed");
        CHECK(ALXUISelection::toString(ALXUICatalog::namePath(y)) ==
                      std::string("unnamed#1"));
        UNSCOPED_INFO("the file's says its value");
        CHECK(ALXUISelection::toString(ALXUICatalog::namePath(y, true)) ==
                      std::string("y"));
        UNSCOPED_INFO("the view's path reaches it in the file's vocabulary");
        CHECK(ALXUICatalog::resolve(root, ALXUISelection::fromString("unnamed#1"), true) == y);
        UNSCOPED_INFO("and so does the file's");
        CHECK(ALXUICatalog::resolve(root, ALXUISelection::fromString("y"), true) == y);
    }

    TEST_CASE_METHOD(alxuicatalog_data, "alxuicatalog: isCoreWidgetTag tells the library's tags and isBuilt the built kinds", "[alxuicatalog]")
    {
        UNSCOPED_INFO("a button is the library's");
        CHECK(ALXUICatalog::isCoreWidgetTag("button"));
        UNSCOPED_INFO("so is a layout stack");
        CHECK(ALXUICatalog::isCoreWidgetTag("layout_stack"));
        UNSCOPED_INFO("a chat history is the viewer's");
        CHECK_FALSE(ALXUICatalog::isCoreWidgetTag("chat_history"));
        UNSCOPED_INFO("no tag is nobody's");
        CHECK_FALSE(ALXUICatalog::isCoreWidgetTag(""));

        using Kind = ALXUICatalog::Kind;
        for (Kind kind : { Kind::Floater, Kind::Panel, Kind::Menu, Kind::Widget, Kind::Template })
        {
            INFO("built: " << ALXUICatalog::kindName(kind));
            REQUIRE(ALXUICatalog::isBuilt(kind));
        }
        for (Kind kind : { Kind::Notifications, Kind::Strings, Kind::Other })
        {
            INFO("not built: " << ALXUICatalog::kindName(kind));
            REQUIRE_FALSE(ALXUICatalog::isBuilt(kind));
        }
    }
}

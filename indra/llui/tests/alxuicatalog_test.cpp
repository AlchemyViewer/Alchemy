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

#include "../alxuicatalog.h"
#include "../alxuiselection.h"
#include "../llbutton.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "lldir.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <fstream>

class LLAvatarName;
const std::string gCatalogTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gCatalogTestAnonName;
}

namespace tut
{
    // A skin tree of two skins and two languages: one floater in every
    // layer but "other/de", a panel and a template in default/en only, one
    // file that does not parse, and a menu.
    struct alxuicatalog_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        std::string mSkins;

        alxuicatalog_data()
        {
            mSkins = gDirUtilp->add(gDirUtilp->getTempDir(), "alxuicatalog_test");
            gDirUtilp->deleteDirAndContents(mSkins);
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

        ~alxuicatalog_data()
        {
            gDirUtilp->deleteDirAndContents(mSkins);
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

    typedef test_group<alxuicatalog_data> alxuicatalog_test;
    typedef alxuicatalog_test::object     alxuicatalog_object;
    tut::alxuicatalog_test alxuicatalog_testgroup("alxuicatalog");

    // Every file appears once with every layer that exists, its kind from
    // the root tag, and the skins and languages in the order the tool
    // offers them.
    template<> template<>
    void alxuicatalog_object::test<1>()
    {
        ALXUICatalog catalog;
        catalog.scan(mSkins);

        ensure_equals("skins", catalog.skins().size(), 2u);
        ensure_equals("default first", catalog.skins()[0], std::string("default"));
        ensure_equals("languages", catalog.languages().size(), 2u);
        ensure_equals("en first", catalog.languages()[0], std::string("en"));
        ensure_equals("entries", catalog.entries().size(), 5u);

        const ALXUICatalog::Entry* a = catalog.find("floater_a.xml");
        ensure("floater_a.xml catalogued", a != nullptr);
        ensure_equals("floater kind", (int)a->kind, (int)ALXUICatalog::Kind::Floater);
        ensure_equals("floater title", a->title, std::string("Floater A"));
        ensure_equals("floater layers", a->layers.size(), 3u);
        ensure("default/en", a->layer("default", "en") != nullptr);
        ensure("default/de", a->layer("default", "de") != nullptr);
        ensure("other/en", a->layer("other", "en") != nullptr);
        ensure("no other/de", a->layer("other", "de") == nullptr);

        ensure_equals("panel kind", (int)catalog.find("panel_b.xml")->kind, (int)ALXUICatalog::Kind::Panel);
        ensure_equals("menu kind", (int)catalog.find("menu_c.xml")->kind, (int)ALXUICatalog::Kind::Menu);
        const ALXUICatalog::Entry* tmpl = catalog.find("widgets/button.xml");
        ensure("template catalogued under its subdirectory", tmpl != nullptr);
        ensure_equals("template kind", (int)tmpl->kind, (int)ALXUICatalog::Kind::Template);

        const ALXUICatalog::Entry* broken = catalog.find("broken.xml");
        ensure("a file that does not parse is still catalogued", broken != nullptr);
        ensure("with no document", broken->layers[0].doc == nullptr);
        ensure("and the reason", !broken->layers[0].error.empty());
        ensure("on its line", broken->layers[0].errorLine >= 2);
    }

    // The files a build reads, in the order the merge reads them: a skin's
    // base-language file replaces default's, and the language file comes
    // from whichever skin has it.
    template<> template<>
    void alxuicatalog_object::test<2>()
    {
        ALXUICatalog catalog;
        catalog.scan(mSkins);
        const ALXUICatalog::Entry& a = *catalog.find("floater_a.xml");

        std::vector<const ALXUICatalog::Layer*> layers = catalog.layersFor(a, "default", "en");
        ensure_equals("default/en: one file", layers.size(), 1u);
        ensure_equals("which is default's", layers[0]->skin, std::string("default"));

        layers = catalog.layersFor(a, "default", "de");
        ensure_equals("default/de: two files", layers.size(), 2u);
        ensure_equals("base first", layers[0]->language, std::string("en"));
        ensure_equals("then the language", layers[1]->language, std::string("de"));

        layers = catalog.layersFor(a, "other", "de");
        ensure_equals("other/de: two files", layers.size(), 2u);
        ensure_equals("the skin's own base", layers[0]->skin, std::string("other"));
        ensure_equals("default's language file", layers[1]->skin, std::string("default"));

        layers = catalog.layersFor(*catalog.find("panel_b.xml"), "other", "de");
        ensure_equals("a file the skin lacks falls back to default", layers.size(), 1u);
        ensure_equals("default/en", layers[0]->skin, std::string("default"));
    }

    // Find answers by tag, by attribute name, by attribute value, by name
    // and by text, with the file and line of each hit; and a name path
    // resolves back to the element, ordinals included.
    template<> template<>
    void alxuicatalog_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // The path rules count widget siblings, which asks the registries;
        // naming the widgets' parameter blocks links their registrations.
        LLUICtrlFactory::getDefaultParams<LLPanel>();
        LLUICtrlFactory::getDefaultParams<LLButton>();

        ALXUICatalog catalog;
        catalog.scan(mSkins);

        std::vector<ALXUICatalog::Hit> hits = catalog.find("button", ALXUICatalog::Field::Tag);
        ensure_equals("buttons by tag: two in en, one in de, the template", hits.size(), 4u);
        ensure_equals("in floater_a", hitsIn(hits, "floater_a.xml"), 3u);

        hits = catalog.find("button", ALXUICatalog::Field::Tag, ALXUICatalog::Match::Containing, "default", "en");
        ensure_equals("scoped to a skin and language", hits.size(), 3u);

        hits = catalog.find("title", ALXUICatalog::Field::Attribute);
        ensure_equals("title attributes: one per floater layer", hits.size(), 3u);

        hits = catalog.find("also ok", ALXUICatalog::Field::Value);
        ensure_equals("a value, case folded", hits.size(), 1u);
        ensure_equals("on its line", hits[0].line, 5);
        ensure_equals("with its path", hits[0].path, std::string("body/ok#1"));
        ensure_equals("as it reads", hits[0].snippet, std::string("label=\"Also OK\""));

        hits = catalog.find("greeting", ALXUICatalog::Field::Name);
        ensure_equals("by name", hits.size(), 1u);
        ensure_equals("on the string's line", hits[0].line, 2);

        hits = catalog.find("hello", ALXUICatalog::Field::Text);
        ensure_equals("by text", hits.size(), 1u);
        ensure_equals("the text itself", hits[0].snippet, std::string("Hello"));

        hits = catalog.find("Fenster", ALXUICatalog::Field::Any);
        ensure_equals("any field", hits.size(), 1u);
        ensure_equals("in the German layer", hits[0].layer->language, std::string("de"));

        const ALXUICatalog::Layer* en = catalog.find("floater_a.xml")->layer("default", "en");
        pugi::xml_node second = ALXUICatalog::resolve(en->root(), ALXUISelection::fromString("body/ok#1"));
        ensure("the second button resolves", second);
        ensure_equals("to the right one", std::string(second.attribute("label").value()), std::string("Also OK"));
        ensure("a path nothing has resolves to nothing",
               !ALXUICatalog::resolve(en->root(), ALXUISelection::fromString("body/ok#2")));
        ensure_equals("and back again", ALXUISelection::toString(ALXUICatalog::namePath(second)), std::string("body/ok#1"));
    }

    // A file element with no name is known by its value in the file's
    // vocabulary, as the merge knows a combo box's items, and by "unnamed"
    // in a view's, which is what the widget built from it is called. Both
    // reach it: a path taken from a view finds the element the merge would.
    template<> template<>
    void alxuicatalog_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("parses", doc.load_string(xml.c_str()));
        const pugi::xml_node root = doc.document_element();
        const pugi::xml_node two = root.child("combo_box").child("item").next_sibling("item");
        const pugi::xml_node three = two.next_sibling("item");
        const pugi::xml_node y = root.last_child();

        ensure_equals("an item is known by its value", ALXUISelection::toString(ALXUICatalog::namePath(two, true)),
                      std::string("c/v2"));
        // One with neither is unnamed, and counted among every sibling with
        // no name, since that is what a step of "unnamed" counts.
        ensure_equals("and one with neither is unnamed", ALXUISelection::toString(ALXUICatalog::namePath(three, true)),
                      std::string("c/unnamed#2"));
        ensure("the value resolves", ALXUICatalog::resolve(root, ALXUISelection::fromString("c/v2"), true) == two);
        ensure("and unnamed, counted among the ones with no name, reaches the third",
               ALXUICatalog::resolve(root, ALXUISelection::fromString("c/unnamed#2"), true) == three);
        ensure("a value the file lacks resolves to nothing",
               !ALXUICatalog::resolve(root, ALXUISelection::fromString("c/v3"), true));

        // A widget with a value and no name: the view is "unnamed", and a
        // path taken from the view reaches the file's element all the same.
        ensure_equals("a widget's path says unnamed", ALXUISelection::toString(ALXUICatalog::namePath(y)),
                      std::string("unnamed#1"));
        ensure_equals("the file's says its value", ALXUISelection::toString(ALXUICatalog::namePath(y, true)),
                      std::string("y"));
        ensure("the view's path reaches it in the file's vocabulary",
               ALXUICatalog::resolve(root, ALXUISelection::fromString("unnamed#1"), true) == y);
        ensure("and so does the file's", ALXUICatalog::resolve(root, ALXUISelection::fromString("y"), true) == y);
    }
}

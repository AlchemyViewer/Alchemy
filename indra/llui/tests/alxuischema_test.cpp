/**
 * @file alxuischema_test.cpp
 * @brief What the widget registries say a XUI file may write.
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

#include "../alxuischema.h"

// Naming a widget's parameter block is what links the object that registers
// it, so the tags below are in the schema this binary builds.
#include "../llbutton.h"
#include "../llcombobox.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <pugixml.hpp>

#include <algorithm>

class LLAvatarName;
const std::string gSchemaTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSchemaTestAnonName;
}

namespace tut
{
    struct alxuischema_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // Constructing each block is what puts its tag in reach: a static
        // registrar lives in the same object file as the block's own
        // constructor, and a static library links neither without a
        // reference to one of them.
        alxuischema_data()
        {
            LLButton::Params button;
            LLComboBox::Params combo;
            LLLineEditor::Params line;
            LLPanel::Params panel;
            (void)button.name;
            (void)combo.name;
            (void)line.name;
            (void)panel.name;
        }

        const ALXUISchema& schema() { return ALXUISchema::get(); }

        static bool has(const ALXUISchema::Tag& tag, const char* name)
        {
            return std::any_of(tag.attributes.begin(), tag.attributes.end(),
                               [name](const ALXUISchema::Attribute& a) { return a.name == name; });
        }

        static const ALXUISchema::Attribute* find(const ALXUISchema::Tag& tag, const char* name)
        {
            const auto it = std::find_if(tag.attributes.begin(), tag.attributes.end(),
                                         [name](const ALXUISchema::Attribute& a) { return a.name == name; });
            return it == tag.attributes.end() ? nullptr : &*it;
        }
    };

    typedef test_group<alxuischema_data> alxuischema_test;
    typedef alxuischema_test::object     alxuischema_object;
    tut::alxuischema_test alxuischema_testgroup("alxuischema");

    // The registries answer at all, and a tag nothing registers is not in
    // the model rather than empty in it.
    template<> template<>
    void alxuischema_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        ensure("some tags", !schema().tags().empty());
        ensure("button is one of them", schema().tag("button") != nullptr);
        ensure("and a tag nobody registers is not", schema().tag("no_such_widget") == nullptr);
        ensure("nor does it accept anything", !schema().accepts("no_such_widget", "name"));
    }

    // LLView keeps its rect as an unnamed parameter, which is what lets a
    // file write left and width with no rect in front of them. Every widget
    // there is inherits that, so it is the flattening's own gate.
    template<> template<>
    void alxuischema_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        for (const ALXUISchema::Tag& tag : schema().tags())
        {
            ensure("left on " + tag.name, has(tag, "left"));
            ensure("width on " + tag.name, has(tag, "width"));
            ensure("height on " + tag.name, has(tag, "height"));
            ensure("name on " + tag.name, has(tag, "name"));
            ensure("follows on " + tag.name, has(tag, "follows"));
        }
    }

    // A block that declares no parameters of its own reads whatever it is
    // handed, which is what LLSD does and what value is. It has an element
    // form too, and a file may write either.
    template<> template<>
    void alxuischema_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* button = schema().tag("button");
        ensure("button", button != nullptr);
        ensure("value is an attribute", has(*button, "value"));
        ensure("and initial_value beside it", has(*button, "initial_value"));
        ensure("and an element as well",
               std::any_of(button->elements.begin(), button->elements.end(),
                           [](const ALXUISchema::Element& e) { return e.name == "button.value"; }));
        ensure("a callback's parameter is LLSD too",
               has(*button, "commit_callback.parameter"));
        ensure("the tag reads its own text", button->text);
    }

    // A block reached by name carries its leaves under dots; one whose only
    // unnamed parameter is a scalar is also writable as that scalar.
    template<> template<>
    void alxuischema_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* button = schema().tag("button");
        ensure("button", button != nullptr);
        ensure("the font's own name", has(*button, "font.name"));
        ensure("and the font written as one", has(*button, "font"));
        ensure("a colour's components", has(*button, "label_color.red"));
        ensure("and the colour written as one", has(*button, "label_color"));
    }

    // A C++ enumeration is a closed set of names and is written as one. A
    // type that takes a name or any string besides is not.
    template<> template<>
    void alxuischema_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* button = schema().tag("button");
        ensure("button", button != nullptr);
        const ALXUISchema::Attribute* halign = find(*button, "halign");
        ensure("halign", halign != nullptr);
        ensure_equals("its three names", halign->values.size(), 3u);
        ensure("centre", std::find(halign->values.begin(), halign->values.end(), "center")
                             != halign->values.end());

        const ALXUISchema::Attribute* name = find(*button, "name");
        ensure("name", name != nullptr);
        ensure("a string names nothing", name->values.empty());
        ensure_equals("and is one", (int)name->value, (int)ALParamType::STRING);
    }

    // What the lint asks, on a name each of the two kinds of answer.
    template<> template<>
    void alxuischema_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        ensure("a button takes a label", schema().accepts("button", "label"));
        ensure("and a line editor a watermark", schema().accepts("line_editor", "watermark_text"));
        // A synonym for a parameter of a nested block is dropped where it is
        // written: the descriptor it names belongs to the inner block, and
        // the outer block's table has no entry with that handle. The name a
        // file writes is max_length_chars, and max_length is nothing.
        ensure("the nested synonym is not a name", !schema().accepts("line_editor", "max_length"));
        ensure("the one it meant is", schema().accepts("line_editor", "max_length_chars"));
    }

    // The document parses, and says the same thing the model does.
    template<> template<>
    void alxuischema_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const std::string xsd = schema().asXSD();
        pugi::xml_document document;
        const pugi::xml_parse_result result = document.load_string(xsd.c_str());
        ensure("the schema parses", result.status == pugi::status_ok);

        const pugi::xml_node root = document.document_element();
        ensure_equals("a schema", std::string(root.name()), std::string("xs:schema"));

        size_t elements = 0;
        for (pugi::xml_node child : root.children("xs:element"))
        {
            ++elements;
            ensure("every element is a tag",
                   schema().tag(child.attribute("name").as_string()) != nullptr);
        }
        ensure_equals("one global element per tag", elements, schema().tags().size());
    }
}

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

#include "../alxuinotes.h"

// Naming a widget's parameter block is what links the object that registers
// it, so the tags below are in the schema this binary builds.
#include "../llbutton.h"
#include "../llcombobox.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"

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
            LLFloater::Params floater;
            LLLineEditor::Params line;
            LLPanel::Params panel;
            (void)button.name;
            (void)combo.name;
            (void)floater.name;
            (void)line.name;
            (void)panel.name;
        }

        static bool hasElement(const ALXUISchema::Tag& tag, const char* name)
        {
            return std::any_of(tag.elements.begin(), tag.elements.end(),
                               [name](const ALXUISchema::Element& e) { return e.name == name; });
        }

        static bool hasChild(const ALXUISchema::Tag& tag, const char* name)
        {
            return std::find(tag.children.begin(), tag.children.end(), name) != tag.children.end();
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

    // A registry of widgets valid below a tag is filled by static registrars,
    // and each of those files into a scope of the registry's own rather than
    // into its default registrar. Reading only the default registrar said
    // every widget in the tree may contain nothing at all.
    template<> template<>
    void alxuischema_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* panel = schema().tag("panel");
        ensure("panel", panel != nullptr);
        ensure("a panel takes a button", hasChild(*panel, "button"));
        ensure("and a panel", hasChild(*panel, "panel"));
    }

    // A floater is the root of its file and never a child of anything, so no
    // child registry names it: without a registration of its own the schema
    // would not know the tag at all, and a third of the tree is floaters.
    template<> template<>
    void alxuischema_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* floater = schema().tag("floater");
        ensure("floater", floater != nullptr);
        ensure("with the widgets a floater holds", hasChild(*floater, "button"));
        ensure("it is a view, so it has a rect", has(*floater, "left"));
        ensure("and the tag it shares its block with", schema().tag("multi_floater") != nullptr);
    }

    // Both spellings of a parameter written as an element. A child whose tag
    // is not a widget is read as a parameter of the element it is under, so
    // <panel><string> is <panel.string>, and shipped files write both.
    template<> template<>
    void alxuischema_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* panel = schema().tag("panel");
        ensure("panel", panel != nullptr);
        ensure("under the tag that owns it", hasElement(*panel, "panel.string"));
        ensure("and on its own", hasElement(*panel, "string"));

        // A name that is both a tag and a parameter is read as the parameter,
        // since a child is only taken for a widget once the block refuses it.
        const ALXUISchema::Tag* button = schema().tag("button");
        ensure("button", button != nullptr);
        ensure("badge is a parameter of a button", hasElement(*button, "badge"));
        ensure("so it is not offered as a widget below one", !hasChild(*button, "badge"));
    }

    // Nothing is required, and a number is written the way C writes one.
    template<> template<>
    void alxuischema_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const std::string xsd = schema().asXSD();
        ensure("a widget template supplies what a file leaves out, so nothing is required",
               xsd.find("use=\"required\"") == std::string::npos);

        pugi::xml_document document;
        ensure("the schema parses", document.load_string(xsd.c_str()).status == pugi::status_ok);

        // stat_bar writes bar_max="100.f", and xs:decimal refuses the suffix.
        bool found = false;
        for (pugi::xml_node type : document.document_element().children("xs:simpleType"))
        {
            found = found || std::string_view(type.attribute("name").as_string()) == "al_real";
        }
        ensure("a real is a type of its own", found);

        const ALXUISchema::Attribute* alpha = find(*schema().tag("panel"), "bg_alpha_color.alpha");
        ensure("a colour component is a real", alpha != nullptr);
        ensure_equals("and reads as one", (int)alpha->value, (int)ALParamType::REAL);
    }

    // The question a drop asks. A container with a child registry of its
    // own answers for the few tags in it, which is the thing that made a
    // property grid declared inside a scroll container never get built.
    template<> template<>
    void alxuischema_object::test<12>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        ensure("a panel takes a button", schema().acceptsChild("panel", "button"));
        ensure("a scroll container takes a panel", schema().acceptsChild("scroll_container", "panel"));
        ensure("and takes nothing else it was not given",
               !schema().acceptsChild("scroll_container", "button"));
        ensure("a layout stack takes layout panels",
               schema().acceptsChild("layout_stack", "layout_panel"));
        ensure("a tag the schema does not know accepts nothing",
               !schema().acceptsChild("no_such_widget", "button"));
    }

    // A block that reads a value written whole. CustomParamValue does that
    // read by hand rather than through a parameter of its own, so nothing
    // in the descriptor table mentions it and the schema used to call
    // text_color="White" an attribute no widget declares -- which is what
    // every shipped file writes.
    template<> template<>
    void alxuischema_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        const ALXUISchema::Tag* text = schema().tag("text");
        ensure("text", text != nullptr);

        const ALXUISchema::Attribute* colour = find(*text, "text_color");
        ensure("a colour is written whole", colour != nullptr);
        ensure_equals("as the type it is", colour->type, std::string("LLUIColor"));
        ensure("and its parts are still there beside it", find(*text, "text_color.red") != nullptr);

        ensure("so is a font", find(*text, "font") != nullptr);
        ensure("and its parts", find(*text, "font.name") != nullptr);
        ensure("and an image", find(*schema().tag("button"), "image_unselected") != nullptr);
    }

    // A registry knows that a button takes a label and cannot know what a
    // button is for. That sentence is written by a person, in a file beside
    // the generated schema, and every tag the registries offer has one --
    // because a vocabulary of a hundred and forty is only usable if the
    // first question about each of them has an answer.
    //
    // What this reaches is the tags THIS BINARY registers, which is llui's
    // share of them and not the viewer's: a widget that only newview
    // registers is not in here to be asked. The wider net is the schema the
    // viewer writes out -- every tag in xui.xsd carries its sentence, and
    // that is what to check after adding a widget.
    template<> template<>
    void alxuischema_object::test<14>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        std::vector<std::string> unwritten;
        for (const ALXUISchema::Tag& tag : schema().tags())
        {
            if (ALXUINotes::get().note(tag.name).empty())
            {
                unwritten.push_back(tag.name);
            }
        }
        std::string missing;
        for (const std::string& name : unwritten)
        {
            missing += (missing.empty() ? "" : ", ") + name;
        }
        ensure("every tag says what it is for; these do not: " + missing, unwritten.empty());

        // And a tag nobody has written one for is not an error, it is one
        // fewer line: the schema still knows the tag.
        ensure("a name nobody wrote about has nothing to say",
               ALXUINotes::get().note("not_a_tag_anybody_registered").empty());
    }

    // The generated schema carries them, so an editor pointed at xui.xsd
    // says what a tag is for as well as what it takes.
    template<> template<>
    void alxuischema_object::test<15>()
    {
        if (!ui.ok())
        {
            skip("the source tree is not where the build said it was");
        }
        pugi::xml_document document;
        const std::string xsd = schema().asXSD();
        ensure("the schema parses", document.load_string(xsd.c_str()).status == pugi::status_ok);

        for (pugi::xml_node element : document.document_element().children("xs:element"))
        {
            const std::string name = element.attribute("name").as_string();
            const std::string said = element.child("xs:annotation").child_value("xs:documentation");
            ensure("<" + name + "> carries its sentence", !said.empty());
            ensure_equals("and it is the one that was written", said, ALXUINotes::get().note(name));
        }
    }

    // What a parameter block writes when nothing has been asked of it. The
    // rules a file is read under keep what was provided, and nothing is
    // provided on a block nobody wrote to, so a file written from one of
    // those is empty. What is wanted here is the other question -- what an
    // element carries when its file says nothing -- and that is every
    // parameter that holds a value, which is the rule below.
    template<> template<>
    void alxuischema_object::test<16>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLButton::Params defaults;
        LLXUIParser parser;

        LLXMLNodePtr provided = new LLXMLNode("button", false);
        parser.writeXUI(provided, defaults);
        ensure_equals("what was provided is nothing", provided->mAttributes.size(), 0u);

        LLXMLNodePtr held = new LLXMLNode("button", false);
        parser.writeXUI(held, defaults,
                        ll_make_predicate(LLInitParam::VALID) && !ll_make_predicate(LLInitParam::EMPTY));
        ensure("what is held is not", held->mAttributes.size() > 0u);

        // But what C++ declares is not what an element carries: a button is
        // twenty-three pixels tall because the widget's own template says so,
        // and the block above has never read one.
        std::string value;
        ensure("the block says how tall it is", held->getAttributeString("height", value));
        ensure_equals("and C++ says nothing about that", value, std::string("0"));

        // The schema reads the block a widget is actually built from, which
        // is that one with its template over it and its base blocks' under.
        const ALXUISchema::Tag* button = schema().tag("button");
        ensure("button", button != nullptr);
        const ALXUISchema::Attribute* height = find(*button, "height");
        ensure("height is in the schema", height != nullptr);
        ensure("and the schema says what it holds", height->holds);
        ensure_equals("which is the height a button is", height->held, std::string("23"));

        // Not every parameter holds a value to write: a block written as one
        // thing does not also write its parts, and a parameter nobody can
        // spell a value for writes none. Holding an empty value is a third
        // thing again, and the flag is what tells it from holding nothing.
        ensure("some of them hold nothing",
               std::any_of(button->attributes.begin(), button->attributes.end(),
                           [](const ALXUISchema::Attribute& a) { return !a.holds; }));
    }

    // What a name was probably meant to be. The whole vocabulary of a tag is
    // here, so a slip is one comparison away from the name it slipped from --
    // and the same comparison has to refuse, because a suggestion nobody
    // asked for that is wrong is worse than none at all.
    template<> template<>
    void alxuischema_object::test<17>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ensure_equals("a character missing", schema().nearestSpelling("button", "tool_tp"),
                      std::string("tool_tip"));
        ensure_equals("a character too many", schema().nearestSpelling("button", "tool_tiip"),
                      std::string("tool_tip"));

        // Near nothing, so nothing is offered.
        ensure("a name near nothing", schema().nearestSpelling("button", "qqzzxwvu").empty());

        // A short name has no room to be wrong in. Two letters swapped is
        // two edits, and over five characters two edits reach a different
        // word: `lable` is as near `label` as it is to `table`, and a guess
        // between them is a coin toss with the file as the stake.
        ensure("two swapped in a short name", schema().nearestSpelling("button", "lable").empty());

        // And under four characters nothing is offered at all: `top` and
        // `pad` are each one edit from a great many things, every one of
        // which is a real name somebody meant.
        ensure("a short name is never a slip", schema().nearestSpelling("button", "top").empty());

        // A tag nobody knows has no vocabulary to compare against.
        ensure("no tag, no suggestion", schema().nearestSpelling("no_such_tag", "labl").empty());
    }
}

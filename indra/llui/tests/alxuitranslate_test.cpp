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

#include "alxuitranslate.h"

#include "alxuiedit.h"

#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>

#include <iterator>

namespace
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

    // What counts as a string someone translates, which is the rule the
    // translation tables have been built from.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: which strings count as units someone translates", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());
        ALXUITranslate units;
        units.scan(root, pugi::xml_node());

        CHECK(find(units, "", "title"));
        CHECK(find(units, "ok", "label"));
        CHECK(find(units, "ok", "tool_tip"));
        UNSCOPED_INFO("body text is a unit");
        CHECK(find(units, "greeting", ""));
        UNSCOPED_INFO("a value with no text or label of its own is a unit");
        CHECK(find(units, "inner/deep", "value"));
        UNSCOPED_INFO("a number is not: " + describe(units));
        CHECK_FALSE(find(units, "count", "value"));
        UNSCOPED_INFO("a resolution is not");
        CHECK_FALSE(find(units, "size", "value"));
        UNSCOPED_INFO("what the file forbids is not");
        CHECK_FALSE(find(units, "code", "value"));
        UNSCOPED_INFO("and with no language, every one of them is missing");
        CHECK(units.count(ALXUITranslate::State::Missing) == (S32)units.units().size());
    }

    // The four states a language's value can be in.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: each state a language's value can be in", "[alxuitranslate]")
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
        REQUIRE(title);
        CHECK((int)title->state == (int)ALXUITranslate::State::Translated);

        const ALXUITranslate::Unit* tip = find(units, "ok", "tool_tip");
        REQUIRE(tip);
        UNSCOPED_INFO("and lost its placeholder");
        CHECK((int)tip->state == (int)ALXUITranslate::State::Placeholders);

        // Written where the base used to have the element. The merge looks
        // below for the one element of that name, and there is one, so the
        // value arrives -- and the unit says both that it arrives and that
        // the base has moved on from where it was written.
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        REQUIRE(deep);
        UNSCOPED_INFO("written at a path the base moved on from, and applied all the same: " + describe(units));
        CHECK((int)deep->state == (int)ALXUITranslate::State::Rescued);
        UNSCOPED_INFO("which is applying");
        CHECK(deep->applies());
        UNSCOPED_INFO("and the reason it is worth moving is that the base has it elsewhere");
        CHECK((int)deep->miss == (int)ALXUITranslate::Miss::Moved);
        UNSCOPED_INFO("which is where the language put it");
        CHECK(deep->where == std::string("deep"));

        const ALXUITranslate::Unit* code = find(units, "code", "value");
        UNSCOPED_INFO("what the file forbids, translated anyway, is shown");
        REQUIRE(code);
        CHECK((int)code->state == (int)ALXUITranslate::State::Forbidden);

        const ALXUITranslate::Unit* gone = find(units, "gone", "value");
        UNSCOPED_INFO("a name the base does not have is shown");
        REQUIRE(gone);
        UNSCOPED_INFO("as applying to nothing");
        CHECK((int)gone->state == (int)ALXUITranslate::State::NotApplied);
        UNSCOPED_INFO("with nowhere to put it");
        CHECK((int)gone->miss == (int)ALXUITranslate::Miss::Absent);
    }

    // A missing unit is created with the ancestors the base gives it,
    // each carrying nothing but its name.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: writing a missing unit creates its ancestors carrying only their names", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer("<panel name=\"root\">\n</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        REQUIRE(deep);

        std::string error;
        const bool wrote = ALXUITranslate::write(overlay, root, *deep, "Anidado", error);
        UNSCOPED_INFO("writes: " + error);
        CHECK(wrote);
        UNSCOPED_INFO("the chain, and nothing else on it");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // A unit the language put at the wrong path is moved to the right
    // one, and every other byte of the file is left alone.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: writing a unit at the wrong path moves it and leaves other bytes alone", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <!-- a comment that stays -->\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <text name=\"deep\" value=\"viejo\"/>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        REQUIRE(deep);
        UNSCOPED_INFO("and the language has it in the wrong place, where the merge rescues it");
        CHECK((int)deep->state == (int)ALXUITranslate::State::Rescued);
        UNSCOPED_INFO("which is worth moving all the same");
        CHECK((int)deep->miss == (int)ALXUITranslate::Miss::Moved);

        std::string error;
        const bool wrote = ALXUITranslate::write(overlay, root, *deep, "Anidado", error);
        UNSCOPED_INFO("writes: " + error);
        CHECK(wrote);
        UNSCOPED_INFO("moved under the ancestor it needed");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <!-- a comment that stays -->\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // The four ways an overlay is broken by writing it, each refused.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: writes that would break an overlay are refused and write nothing", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer("<panel name=\"root\">\n</panel>\n"));
        const std::string before = overlay.text();

        ALXUITranslate units;
        units.scan(root, overlay.root());

        std::string error;
        ALXUITranslate::Unit unit;
        unit.path = { "ok" };
        unit.field = "label";

        UNSCOPED_INFO("an empty translation is refused");
        CHECK_FALSE(ALXUITranslate::write(overlay, root, unit, "", error));
        UNSCOPED_INFO("and says why");
        CHECK_FALSE(error.empty());

        unit.field = "left";
        UNSCOPED_INFO("a layout attribute is refused");
        CHECK_FALSE(ALXUITranslate::write(overlay, root, unit, "10", error));

        unit.field = "description";
        UNSCOPED_INFO("an attribute the base does not carry is refused");
        CHECK_FALSE(ALXUITranslate::write(overlay, root, unit, "Algo", error));

        unit.path = { "code" };
        unit.field = "value";
        UNSCOPED_INFO("what the file forbids is refused");
        CHECK_FALSE(ALXUITranslate::write(overlay, root, unit, "HTTP", error));

        UNSCOPED_INFO("and none of them wrote anything");
        CHECK(overlay.text() == before);
    }

    // A name means one thing under its parent. Two combo boxes each with
    // an item called "1" are not ambiguous: the item is told from the
    // other by the box it is in.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: repair leaves a stray item that either of two parents could claim", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <combo_box name=\"first\">\n"
            "        <combo_box.item name=\"1\" label=\"Near\"/>\n"
            "    </combo_box>\n"
            "    <combo_box name=\"second\">\n"
            "        <combo_box.item name=\"1\" label=\"Far\"/>\n"
            "    </combo_box>\n"
            "</panel>\n");

        // The language has both, and has put one of them at the wrong
        // depth: the box is there, the item is outside it.
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <combo_box name=\"first\">\n"
            "        <combo_box.item name=\"1\" label=\"Cerca\"/>\n"
            "    </combo_box>\n"
            "    <combo_box name=\"second\"/>\n"
            "    <combo_box.item name=\"1\" label=\"Lejos\"/>\n"
            "</panel>\n"));

        // The one under "first" is where the base has one of that name,
        // so it is not the stray. The stray is left where it is: one
        // unclaimed element is not evidence for which of the two boxes it
        // belongs to, and pairing them off by name loses a translation
        // and duplicates another where a base names three the same. This
        // is the case an assignment between the two files would settle,
        // and the case this refuses until there is one.
        const std::string before = overlay.text();
        std::string error;
        UNSCOPED_INFO("the stray is not guessed at");
        CHECK(ALXUITranslate::repair(overlay, root, error) == 0);
        UNSCOPED_INFO("and nothing is written");
        CHECK(overlay.text() == before);
    }

    // An element already sitting where the base has one of that name is
    // that one: a repair moves what the language wrote, and never takes
    // what is already in its place or writes a second copy of it.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: repair leaves an element already where the base has its name", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <text name=\"here\" value=\"One\"/>\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"here\" value=\"Two\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        // The language has one "here", at a path the base also has.
        ALXUIEdit overlay;
        const std::string before =
            "<panel name=\"root\">\n"
            "    <text name=\"here\" value=\"Uno\"/>\n"
            "</panel>\n";
        REQUIRE(overlay.loadBuffer(before));

        std::string error;
        UNSCOPED_INFO("nothing moves");
        CHECK(ALXUITranslate::repair(overlay, root, error) == 0);
        UNSCOPED_INFO("and nothing is written");
        CHECK(overlay.text() == before);
    }

    // A move that needs an ancestor another move has not made yet is done
    // on the pass after it, and the shells the moves leave behind go.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: repair makes missing ancestors over passes and drops emptied shells", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"outer\">\n"
            "        <panel name=\"middle\">\n"
            "            <text name=\"a\" value=\"One\"/>\n"
            "            <text name=\"b\" value=\"Two\"/>\n"
            "        </panel>\n"
            "    </panel>\n"
            "</panel>\n");

        // Flat, and wrapped in a shell of its own that says nothing.
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"stale\">\n"
            "        <text name=\"a\" value=\"Uno\"/>\n"
            "        <text name=\"b\" value=\"Dos\"/>\n"
            "    </panel>\n"
            "</panel>\n"));

        std::string error;
        const S32 moved = ALXUITranslate::repair(overlay, root, error);
        UNSCOPED_INFO("both move: " + error);
        CHECK(moved == 2);

        const std::string& text = overlay.text();
        const size_t middle = text.find("<panel name=\"middle\">");
        UNSCOPED_INFO("the chain the base gives them is written: " + text);
        REQUIRE(middle != std::string::npos);
        UNSCOPED_INFO("under the ancestor above it");
        CHECK(text.find("<panel name=\"outer\">") < middle);
        UNSCOPED_INFO("the shell they came out of is gone");
        CHECK(text.find("stale") == std::string::npos);
        {
            INFO("and both values are inside it");
            CHECK(text.find("Uno", middle) != std::string::npos);
            CHECK(text.find("Dos", middle) != std::string::npos);
        }
        UNSCOPED_INFO("each on its own line, indented for where it landed");
        CHECK(text.find("\n            <text") != std::string::npos);
    }

    // What a file has to say, and what it has outlived.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: weigh counts arriving and absent values to tell a renamed file", "[alxuitranslate]")
    {
        Doc base;
        Doc other;
        pugi::xml_node root = base.load(english());
        pugi::xml_node overlay = other.load(
            "<panel name=\"root\" title=\"Lugares\">\n"
            "    <text name=\"greeting\">Hola</text>\n"
            "    <text name=\"gone\" value=\"Nada\"/>\n"
            "    <text name=\"also_gone\" value=\"Nada\"/>\n"
            "</panel>\n");

        ALXUITranslate units;
        units.scan(root, overlay);
        S32 arrives = 0;
        S32 absent = 0;
        units.weigh(arrives, absent);
        UNSCOPED_INFO("two of its values name what the base has");
        CHECK(arrives == 2);
        UNSCOPED_INFO("and two name what it does not");
        CHECK(absent == 2);

        // A root with no name at all is an omission, whatever the counts
        // say; a root with a name is this file when more of it arrives
        // than does not.
        UNSCOPED_INFO("a nameless root is always this file");
        CHECK(units.sameFileRenamed(std::string_view()));
        UNSCOPED_INFO("an even split is not enough to say so");
        CHECK_FALSE(units.sameFileRenamed("something_else"));
    }

    // Where the base repeats a name among siblings, the language's value
    // is the first of them -- which is what the merge makes of it too --
    // and the second cannot be addressed by a chain of names at all.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: a repeated name's value goes to the first and the second has no address", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"twice\" value=\"One\"/>\n"
            "        <text name=\"twice\" value=\"Two\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <text name=\"twice\" value=\"Uno\"/>\n"
            "</panel>\n"));

        std::string error;
        UNSCOPED_INFO("the one it has moves to the first of them");
        CHECK(ALXUITranslate::repair(overlay, root, error) == 1);
        UNSCOPED_INFO("and it is the only thing written");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"twice\" value=\"Uno\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        // A second value for the second of them has nowhere to go: the
        // ancestors this writes carry names, and a name the base repeats
        // is not an address.
        ALXUIEdit second;
        const std::string before =
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"twice\" value=\"Uno\"/>\n"
            "    </panel>\n"
            "    <text name=\"twice\" value=\"Dos\"/>\n"
            "</panel>\n";
        REQUIRE(second.loadBuffer(before));
        UNSCOPED_INFO("nothing is moved");
        CHECK(ALXUITranslate::repair(second, root, error) == 0);
        UNSCOPED_INFO("and the file is untouched");
        CHECK(second.text() == before);
    }

    // A name can carry a hash of its own, and a path step writes an
    // ordinal with one: only digits after it are the ordinal.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: a hash in a name is part of the name when repairing", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"Give to #RLV\" value=\"Give\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <text name=\"Give to #RLV\" value=\"Dar\"/>\n"
            "</panel>\n"));

        std::string error;
        const S32 moved = ALXUITranslate::repair(overlay, root, error);
        UNSCOPED_INFO("the hash in the name is part of it: " + error);
        CHECK(moved == 1);
        UNSCOPED_INFO("so it moves like any other name");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"Give to #RLV\" value=\"Dar\"/>\n"
            "    </panel>\n"
            "</panel>\n");
    }

    // Body text is what the widget will be given, not what the file's own
    // formatting put around it. A `<text>` element written over three lines
    // carries the newline and the indent of the line it sits on, and
    // LLXMLNode::getTextContents trims both off before any widget sees them
    // -- so a translation table that did not was comparing, measuring and
    // showing a string the viewer never builds.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: body text is trimmed of the file's own formatting", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <text name=\"greeting\">\n"
            "        Show direction to:\n"
            "    </text>\n"
            "</panel>\n");

        Doc theirs;
        pugi::xml_node mine = theirs.load(
            "<panel name=\"root\">\n"
            "    <text name=\"greeting\">\n"
            "        Zeige Richtung zu:\n"
            "    </text>\n"
            "</panel>\n");

        ALXUITranslate units;
        units.scan(root, mine);

        const ALXUITranslate::Unit* unit = find(units, "greeting", "");
        REQUIRE(unit != nullptr);
        UNSCOPED_INFO("the English is what a widget would be given");
        CHECK(unit->english == std::string("Show direction to:"));
        UNSCOPED_INFO("and so is the translation");
        CHECK(unit->translation == std::string("Zeige Richtung zu:"));
    }

    // The way down to an element, written into an overlay that has none of
    // it. This is what a translation needs and it is not about translation:
    // a skin that overrides one number in a file it otherwise says nothing
    // about needs the same chain, for the same reason -- the merge matches on
    // names, so a name is all an overlay has to say about the way down.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: ensureChain writes the way down to an element with names alone", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        // An overlay with nothing but the root: everything below is written.
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer("<panel name=\"root\"/>\n"));

        std::string error;
        REQUIRE(ALXUITranslate::ensureChain(overlay, root, { "inner", "deep" }, error));
        UNSCOPED_INFO("with nothing to say about it: " + error);
        CHECK(error.empty());
        UNSCOPED_INFO("and the element is now there");
        CHECK(overlay.resolve({ "inner", "deep" }));

        // Each ancestor carries nothing but its name, because that is all the
        // merge reads and anything else would be an override nobody asked for.
        const pugi::xml_node inner = overlay.resolve({ "inner" });
        REQUIRE(inner);
        const S32 attributes = (S32)std::distance(inner.attributes_begin(), inner.attributes_end());
        UNSCOPED_INFO("carrying only its name");
        CHECK(attributes == 1);

        // Which is what an override is written onto.
        UNSCOPED_INFO("the override is written");
        CHECK(overlay.setAttribute({ "inner", "deep" }, "width", "40"));
        UNSCOPED_INFO("and the file says so");
        CHECK(overlay.text().find("width=\"40\"") != std::string::npos);

        // An element the base does not have is not one to make a way down to.
        UNSCOPED_INFO("no base element, no chain");
        CHECK_FALSE(ALXUITranslate::ensureChain(overlay, root, { "inner", "nowhere" }, error));
        UNSCOPED_INFO("and it says why");
        CHECK_FALSE(error.empty());
    }

    // The merge rescues a moved element only where exactly one of its
    // name sits below the element it matched the overlay's parent to.
    // Two below is a guess it does not make, and the table says so: the
    // value applies to nothing, and a repair moves it.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: a merge rescues a moved element only where one of its name is below", "[alxuitranslate]")
    {
        Doc base;
        Doc other;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Nested\"/>\n"
            "    </panel>\n"
            "    <panel name=\"other\">\n"
            "        <text name=\"deep\" value=\"Also nested\"/>\n"
            "    </panel>\n"
            "</panel>\n");
        pugi::xml_node overlay = other.load(
            "<panel name=\"root\">\n"
            "    <text name=\"deep\" value=\"Anidado\"/>\n"
            "</panel>\n");

        ALXUITranslate units;
        units.scan(root, overlay);
        const ALXUITranslate::Unit* one = find(units, "inner/deep", "value");
        const ALXUITranslate::Unit* two = find(units, "other/deep", "value");
        {
            INFO("both of the base's are there: " + describe(units));
            REQUIRE(one);
            REQUIRE(two);
        }
        UNSCOPED_INFO("neither is rescued, since the merge would be guessing");
        CHECK((int)one->state == (int)ALXUITranslate::State::NotApplied);
        UNSCOPED_INFO("neither of them");
        CHECK((int)two->state == (int)ALXUITranslate::State::NotApplied);
        {
            INFO("and both say the base has the name elsewhere");
            CHECK((int)one->miss == (int)ALXUITranslate::Miss::Moved);
            CHECK((int)two->miss == (int)ALXUITranslate::Miss::Moved);
        }
        {
            INFO("so neither applies");
            CHECK_FALSE(one->applies());
            CHECK_FALSE(two->applies());
        }

        // Rescued under the parent the language did write: the merge pairs
        // the parents first, and looks below the paired one only.
        Doc placed;
        pugi::xml_node under = placed.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"other\">\n"
            "        <panel name=\"wrong\">\n"
            "            <text name=\"deep\" value=\"Tambien\"/>\n"
            "        </panel>\n"
            "    </panel>\n"
            "</panel>\n");
        // The base has no `wrong` under `other`, so the merge drops the
        // panel and everything in it: nothing rescues an element whose
        // own parent applies to nothing.
        units.scan(root, under);
        two = find(units, "other/deep", "value");
        UNSCOPED_INFO("the unit is there: " + describe(units));
        REQUIRE(two);
        UNSCOPED_INFO("and applies to nothing, since its parent does");
        CHECK((int)two->state == (int)ALXUITranslate::State::NotApplied);

        // And a repair moves a rescued value as it moves a dropped one,
        // since the rescue lasts only until the base grows another of the
        // name.
        ALXUIEdit fix;
        REQUIRE(fix.loadBuffer(
            "<panel name=\"root\">\n"
            "    <text name=\"deep\" value=\"Anidado\"/>\n"
            "</panel>\n"));
        Doc single;
        pugi::xml_node one_below = single.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Nested\"/>\n"
            "    </panel>\n"
            "</panel>\n");
        units.scan(one_below, fix.root());
        one = find(units, "inner/deep", "value");
        {
            INFO("rescued before the repair: " + describe(units));
            REQUIRE(one);
            CHECK(one->state == ALXUITranslate::State::Rescued);
        }
        std::string error;
        const S32 moved = ALXUITranslate::repair(fix, one_below, error);
        UNSCOPED_INFO("the repair moves it: " + error);
        CHECK(moved == 1);
        units.scan(one_below, fix.root());
        one = find(units, "inner/deep", "value");
        {
            INFO("and it is translated outright afterwards: " + describe(units));
            REQUIRE(one);
            CHECK(one->state == ALXUITranslate::State::Translated);
        }
    }

    // A combo box's items have no name, and the merge matches them by
    // value: a language that translates the second item alone translates
    // the second, not whichever item it lists first. And an item written
    // for a base that lacks it is made with the value that names it.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: combo box items are matched and written by their value", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <combo_box name=\"c\">\n"
            "        <item value=\"v1\" label=\"One\"/>\n"
            "        <item value=\"v2\" label=\"Two\"/>\n"
            "    </combo_box>\n"
            "</panel>\n");
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <combo_box name=\"c\">\n"
            "        <item value=\"v2\" label=\"Zwei\"/>\n"
            "    </combo_box>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* one = find(units, "c/v1", "label");
        const ALXUITranslate::Unit* two = find(units, "c/v2", "label");
        {
            INFO("both items are units, by value: " + describe(units));
            REQUIRE(one);
            REQUIRE(two);
        }
        CHECK((int)one->state == (int)ALXUITranslate::State::Missing);
        CHECK((int)two->state == (int)ALXUITranslate::State::Translated);
        CHECK(two->translation == std::string("Zwei"));

        std::string error;
        const bool wrote = ALXUITranslate::write(overlay, root, *one, "Eins", error);
        UNSCOPED_INFO("writes the first: " + error);
        CHECK(wrote);
        UNSCOPED_INFO("as an item named by its value");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <combo_box name=\"c\">\n"
            "        <item value=\"v2\" label=\"Zwei\"/>\n"
            "        <item value=\"v1\" label=\"Eins\"/>\n"
            "    </combo_box>\n"
            "</panel>\n");
        units.scan(root, overlay.root());
        one = find(units, "c/v1", "label");
        {
            INFO("and it is translated now");
            REQUIRE(one);
            CHECK(one->state == ALXUITranslate::State::Translated);
        }
    }

    // A value that names nothing the base has is taken out of the file:
    // the attribute, or the element where its text was all it carried, and
    // the shells left above it go too.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: remove takes out a value the base lacks and the shells above it", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(english());
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\" tool_tip=\"Nada\"/>\n"
            "        <text name=\"gone\">Fuera</text>\n"
            "    </panel>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* tip = find(units, "inner/deep", "tool_tip");
        UNSCOPED_INFO("the attribute the base lacks is a unit: " + describe(units));
        REQUIRE(tip);
        UNSCOPED_INFO("applying to nothing");
        CHECK((int)tip->miss == (int)ALXUITranslate::Miss::AttributeAbsent);
        std::string error;
        const bool tip_out = ALXUITranslate::remove(overlay, *tip, error);
        UNSCOPED_INFO("taken out: " + error);
        CHECK(tip_out);
        UNSCOPED_INFO("the attribute alone");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "        <text name=\"gone\">Fuera</text>\n"
            "    </panel>\n"
            "</panel>\n");

        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* gone = find(units, "inner/gone", "");
        UNSCOPED_INFO("the element the base lacks is a unit: " + describe(units));
        REQUIRE(gone);
        const bool gone_out = ALXUITranslate::remove(overlay, *gone, error);
        UNSCOPED_INFO("taken out: " + error);
        CHECK(gone_out);
        UNSCOPED_INFO("the whole element, since its text was all it said");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        REQUIRE(deep);
        const bool deep_out = ALXUITranslate::remove(overlay, *deep, error);
        UNSCOPED_INFO("taken out as well: " + error);
        CHECK(deep_out);
        UNSCOPED_INFO("and the shell above it goes with it");
        CHECK(overlay.text() ==
            "<panel name=\"root\">\n"
            "</panel>\n");
    }

    // Everything the language writes for something the base has nowhere,
    // taken out at once: an element of a lost name, an attribute the base's
    // element does not carry, an element with no name to match by. What
    // the base has moved is left for the repair, and what it has at more
    // than one path is left for a person.
    TEST_CASE_METHOD(alxuitranslate_data, "alxuitranslate: removeOrphans takes out at once what the base has nowhere", "[alxuitranslate]")
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\" title=\"Places\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Nested\"/>\n"
            "    </panel>\n"
            "    <button name=\"twice\" label=\"A\"/>\n"
            "    <button name=\"twice\" label=\"B\"/>\n"
            "</panel>\n");
        ALXUIEdit overlay;
        REQUIRE(overlay.loadBuffer(
            "<panel name=\"root\" title=\"Lugares\">\n"
            "    <text name=\"deep\" value=\"Anidado\" tool_tip=\"Nada\"/>\n"
            "    <text name=\"gone\">Fuera</text>\n"
            "    <text value=\"Sin nombre\"/>\n"
            "    <button name=\"twice\" label=\"Dos\"/>\n"
            "</panel>\n"));

        std::string error;
        const S32 gone = ALXUITranslate::removeOrphans(overlay, root, error);
        UNSCOPED_INFO("three orphans went: " + error);
        CHECK(gone == 3);
        UNSCOPED_INFO("the moved value and the ambiguous one stay");
        CHECK(overlay.text() ==
            "<panel name=\"root\" title=\"Lugares\">\n"
            "    <text name=\"deep\" value=\"Anidado\"/>\n"
            "    <button name=\"twice\" label=\"Dos\"/>\n"
            "</panel>\n");

        UNSCOPED_INFO("and a second sweep finds nothing");
        CHECK(ALXUITranslate::removeOrphans(overlay, root, error) == 0);
    }
}

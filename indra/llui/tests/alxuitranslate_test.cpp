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

        // Written where the base used to have the element. The merge looks
        // below for the one element of that name, and there is one, so the
        // value arrives -- and the unit says both that it arrives and that
        // the base has moved on from where it was written.
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        ensure("the nested one is there", deep);
        ensure_equals("written at a path the base moved on from, and applied all the same: " + describe(units),
                      (int)deep->state, (int)ALXUITranslate::State::Rescued);
        ensure("which is applying", deep->applies());
        ensure_equals("and the reason it is worth moving is that the base has it elsewhere",
                      (int)deep->miss, (int)ALXUITranslate::Miss::Moved);
        ensure_equals("which is where the language put it", deep->where, std::string("deep"));

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
        ensure_equals("and the language has it in the wrong place, where the merge rescues it",
                      (int)deep->state, (int)ALXUITranslate::State::Rescued);
        ensure_equals("which is worth moving all the same",
                      (int)deep->miss, (int)ALXUITranslate::Miss::Moved);

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

    // A name means one thing under its parent. Two combo boxes each with
    // an item called "1" are not ambiguous: the item is told from the
    // other by the box it is in.
    template<> template<>
    void alxuitranslate_object::test<6>()
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
        ensure("loads", overlay.loadBuffer(
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
        ensure_equals("the stray is not guessed at", ALXUITranslate::repair(overlay, root, error), 0);
        ensure_equals("and nothing is written", overlay.text(), before);
    }

    // An element already sitting where the base has one of that name is
    // that one: a repair moves what the language wrote, and never takes
    // what is already in its place or writes a second copy of it.
    template<> template<>
    void alxuitranslate_object::test<7>()
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
        ensure("loads", overlay.loadBuffer(before));

        std::string error;
        ensure_equals("nothing moves", ALXUITranslate::repair(overlay, root, error), 0);
        ensure_equals("and nothing is written", overlay.text(), before);
    }

    // A move that needs an ancestor another move has not made yet is done
    // on the pass after it, and the shells the moves leave behind go.
    template<> template<>
    void alxuitranslate_object::test<8>()
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
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"stale\">\n"
            "        <text name=\"a\" value=\"Uno\"/>\n"
            "        <text name=\"b\" value=\"Dos\"/>\n"
            "    </panel>\n"
            "</panel>\n"));

        std::string error;
        ensure_equals("both move: " + error, ALXUITranslate::repair(overlay, root, error), 2);

        const std::string& text = overlay.text();
        const size_t middle = text.find("<panel name=\"middle\">");
        ensure("the chain the base gives them is written: " + text, middle != std::string::npos);
        ensure("under the ancestor above it", text.find("<panel name=\"outer\">") < middle);
        ensure("the shell they came out of is gone", text.find("stale") == std::string::npos);
        ensure("and both values are inside it", text.find("Uno", middle) != std::string::npos
                                             && text.find("Dos", middle) != std::string::npos);
        ensure("each on its own line, indented for where it landed",
               text.find("\n            <text") != std::string::npos);
    }

    // What a file has to say, and what it has outlived.
    template<> template<>
    void alxuitranslate_object::test<9>()
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
        ensure_equals("two of its values name what the base has", arrives, 2);
        ensure_equals("and two name what it does not", absent, 2);

        // A root with no name at all is an omission, whatever the counts
        // say; a root with a name is this file when more of it arrives
        // than does not.
        ensure("a nameless root is always this file", units.sameFileRenamed(std::string_view()));
        ensure("an even split is not enough to say so", !units.sameFileRenamed("something_else"));
    }

    // Where the base repeats a name among siblings, the language's value
    // is the first of them -- which is what the merge makes of it too --
    // and the second cannot be addressed by a chain of names at all.
    template<> template<>
    void alxuitranslate_object::test<10>()
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
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <text name=\"twice\" value=\"Uno\"/>\n"
            "</panel>\n"));

        std::string error;
        ensure_equals("the one it has moves to the first of them",
                      ALXUITranslate::repair(overlay, root, error), 1);
        ensure_equals("and it is the only thing written", overlay.text(),
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
        ensure("loads", second.loadBuffer(before));
        ensure_equals("nothing is moved", ALXUITranslate::repair(second, root, error), 0);
        ensure_equals("and the file is untouched", second.text(), before);
    }

    // A name can carry a hash of its own, and a path step writes an
    // ordinal with one: only digits after it are the ordinal.
    template<> template<>
    void alxuitranslate_object::test<11>()
    {
        Doc base;
        pugi::xml_node root = base.load(
            "<panel name=\"root\">\n"
            "    <panel name=\"holder\">\n"
            "        <text name=\"Give to #RLV\" value=\"Give\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <text name=\"Give to #RLV\" value=\"Dar\"/>\n"
            "</panel>\n"));

        std::string error;
        ensure_equals("the hash in the name is part of it: " + error,
                      ALXUITranslate::repair(overlay, root, error), 1);
        ensure_equals("so it moves like any other name", overlay.text(),
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
    template<> template<>
    void alxuitranslate_object::test<12>()
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
        ensure("body text is a unit", unit != nullptr);
        ensure_equals("the English is what a widget would be given",
                      unit->english, std::string("Show direction to:"));
        ensure_equals("and so is the translation",
                      unit->translation, std::string("Zeige Richtung zu:"));
    }

    // The way down to an element, written into an overlay that has none of
    // it. This is what a translation needs and it is not about translation:
    // a skin that overrides one number in a file it otherwise says nothing
    // about needs the same chain, for the same reason -- the merge matches on
    // names, so a name is all an overlay has to say about the way down.
    template<> template<>
    void alxuitranslate_object::test<13>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());

        // An overlay with nothing but the root: everything below is written.
        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer("<panel name=\"root\"/>\n"));

        std::string error;
        ensure("the chain is written",
               ALXUITranslate::ensureChain(overlay, root, { "inner", "deep" }, error));
        ensure("with nothing to say about it: " + error, error.empty());
        ensure("and the element is now there", overlay.resolve({ "inner", "deep" }));

        // Each ancestor carries nothing but its name, because that is all the
        // merge reads and anything else would be an override nobody asked for.
        const pugi::xml_node inner = overlay.resolve({ "inner" });
        ensure("the ancestor is there", inner);
        S32 attributes = 0;
        for (pugi::xml_attribute one : inner.attributes())
        {
            ++attributes;
        }
        ensure_equals("carrying only its name", attributes, 1);

        // Which is what an override is written onto.
        ensure("the override is written",
               overlay.setAttribute({ "inner", "deep" }, "width", "40"));
        ensure("and the file says so",
               overlay.text().find("width=\"40\"") != std::string::npos);

        // An element the base does not have is not one to make a way down to.
        ensure("no base element, no chain",
               !ALXUITranslate::ensureChain(overlay, root, { "inner", "nowhere" }, error));
        ensure("and it says why", !error.empty());
    }

    // The merge rescues a moved element only where exactly one of its
    // name sits below the element it matched the overlay's parent to.
    // Two below is a guess it does not make, and the table says so: the
    // value applies to nothing, and a repair moves it.
    template<> template<>
    void alxuitranslate_object::test<14>()
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
        ensure("both of the base's are there: " + describe(units), one && two);
        ensure_equals("neither is rescued, since the merge would be guessing",
                      (int)one->state, (int)ALXUITranslate::State::NotApplied);
        ensure_equals("neither of them", (int)two->state, (int)ALXUITranslate::State::NotApplied);
        ensure_equals("and both say the base has the name elsewhere",
                      (int)one->miss, (int)ALXUITranslate::Miss::Moved);
        ensure("so neither applies", !one->applies() && !two->applies());

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
        ensure("the unit is there: " + describe(units), two);
        ensure_equals("and applies to nothing, since its parent does",
                      (int)two->state, (int)ALXUITranslate::State::NotApplied);

        // And a repair moves a rescued value as it moves a dropped one,
        // since the rescue lasts only until the base grows another of the
        // name.
        ALXUIEdit fix;
        ensure("loads", fix.loadBuffer(
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
        ensure("rescued before the repair: " + describe(units),
               one && one->state == ALXUITranslate::State::Rescued);
        std::string error;
        ensure_equals("the repair moves it: " + error, ALXUITranslate::repair(fix, one_below, error), 1);
        units.scan(one_below, fix.root());
        one = find(units, "inner/deep", "value");
        ensure("and it is translated outright afterwards: " + describe(units),
               one && one->state == ALXUITranslate::State::Translated);
    }

    // A combo box's items have no name, and the merge matches them by
    // value: a language that translates the second item alone translates
    // the second, not whichever item it lists first. And an item written
    // for a base that lacks it is made with the value that names it.
    template<> template<>
    void alxuitranslate_object::test<15>()
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
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <combo_box name=\"c\">\n"
            "        <item value=\"v2\" label=\"Zwei\"/>\n"
            "    </combo_box>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* one = find(units, "c/v1", "label");
        const ALXUITranslate::Unit* two = find(units, "c/v2", "label");
        ensure("both items are units, by value: " + describe(units), one && two);
        ensure_equals("the first is missing", (int)one->state, (int)ALXUITranslate::State::Missing);
        ensure_equals("the second is translated", (int)two->state, (int)ALXUITranslate::State::Translated);
        ensure_equals("with the language's word", two->translation, std::string("Zwei"));

        std::string error;
        ensure("writes the first: " + error, ALXUITranslate::write(overlay, root, *one, "Eins", error));
        ensure_equals("as an item named by its value", overlay.text(),
            "<panel name=\"root\">\n"
            "    <combo_box name=\"c\">\n"
            "        <item value=\"v2\" label=\"Zwei\"/>\n"
            "        <item value=\"v1\" label=\"Eins\"/>\n"
            "    </combo_box>\n"
            "</panel>\n");
        units.scan(root, overlay.root());
        one = find(units, "c/v1", "label");
        ensure("and it is translated now", one && one->state == ALXUITranslate::State::Translated);
    }

    // A value that names nothing the base has is taken out of the file:
    // the attribute, or the element where its text was all it carried, and
    // the shells left above it go too.
    template<> template<>
    void alxuitranslate_object::test<16>()
    {
        Doc base;
        pugi::xml_node root = base.load(english());
        ALXUIEdit overlay;
        ensure("loads", overlay.loadBuffer(
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\" tool_tip=\"Nada\"/>\n"
            "        <text name=\"gone\">Fuera</text>\n"
            "    </panel>\n"
            "</panel>\n"));

        ALXUITranslate units;
        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* tip = find(units, "inner/deep", "tool_tip");
        ensure("the attribute the base lacks is a unit: " + describe(units), tip);
        ensure_equals("applying to nothing", (int)tip->miss, (int)ALXUITranslate::Miss::AttributeAbsent);
        std::string error;
        ensure("taken out: " + error, ALXUITranslate::remove(overlay, *tip, error));
        ensure_equals("the attribute alone", overlay.text(),
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "        <text name=\"gone\">Fuera</text>\n"
            "    </panel>\n"
            "</panel>\n");

        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* gone = find(units, "inner/gone", "");
        ensure("the element the base lacks is a unit: " + describe(units), gone);
        ensure("taken out: " + error, ALXUITranslate::remove(overlay, *gone, error));
        ensure_equals("the whole element, since its text was all it said", overlay.text(),
            "<panel name=\"root\">\n"
            "    <panel name=\"inner\">\n"
            "        <text name=\"deep\" value=\"Anidado\"/>\n"
            "    </panel>\n"
            "</panel>\n");

        units.scan(root, overlay.root());
        const ALXUITranslate::Unit* deep = find(units, "inner/deep", "value");
        ensure("the translated value is a unit", deep);
        ensure("taken out as well: " + error, ALXUITranslate::remove(overlay, *deep, error));
        ensure_equals("and the shell above it goes with it", overlay.text(),
            "<panel name=\"root\">\n"
            "</panel>\n");
    }
}

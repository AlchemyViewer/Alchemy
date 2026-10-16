/**
 * @file alcolorsheet_test.cpp
 * @brief Colours declared across files resolve once, and every fault has a line.
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

#include "alcolorsheet.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using Kind = ALColorSheet::Diagnostic::Kind;

    struct alcolorsheet_data
    {
        // One <color> per line after the root, so a line number in a
        // diagnostic is the entry's position plus one.
        static bool read(ALColorSheet& sheet, const std::string& body, const char* file)
        {
            const std::string xml = "<colors>\n" + body + "</colors>\n";
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), root))
            {
                return false;
            }
            return sheet.read(root, file);
        }

        static size_t count(const ALColorSheet& sheet, Kind kind)
        {
            size_t n = 0;
            for (const ALColorSheet::Diagnostic& d : sheet.diagnostics())
            {
                n += d.kind == kind;
            }
            return n;
        }

        static std::string first(const ALColorSheet& sheet, Kind kind)
        {
            for (const ALColorSheet::Diagnostic& d : sheet.diagnostics())
            {
                if (d.kind == kind)
                {
                    return d.message;
                }
            }
            return std::string();
        }

        static std::string all(const ALColorSheet& sheet)
        {
            std::string text;
            for (const ALColorSheet::Diagnostic& d : sheet.diagnostics())
            {
                text += d.message + "\n";
            }
            return text;
        }

        static bool is(const ALColorSheet& sheet, const char* name, F32 r, F32 g, F32 b, F32 a = 1.f)
        {
            const LLColor4* color = sheet.find(name);
            return color && *color == LLColor4(r, g, b, a);
        }
    };

    // Values, references, a chain, a reference declared before what it
    // names, and three numbers making an opaque colour.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: values and references resolve in any declaration order", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        REQUIRE(read(sheet,
            "<color name=\"Early\" reference=\"Base\"/>\n"
            "<color name=\"Base\" value=\"0.1 0.2 0.3 0.4\"/>\n"
            "<color name=\"Mid\" reference=\"Base\"/>\n"
            "<color name=\"Late\" reference=\"Mid\"/>\n"
            "<color name=\"Opaque\" value=\"0.5 0.6 0.7\"/>\n", "a.xml"));
        sheet.resolve();

        UNSCOPED_INFO("a value");
        CHECK(is(sheet, "Base", 0.1f, 0.2f, 0.3f, 0.4f));
        UNSCOPED_INFO("a reference");
        CHECK(is(sheet, "Mid", 0.1f, 0.2f, 0.3f, 0.4f));
        UNSCOPED_INFO("a chain");
        CHECK(is(sheet, "Late", 0.1f, 0.2f, 0.3f, 0.4f));
        UNSCOPED_INFO("a reference ahead of its value");
        CHECK(is(sheet, "Early", 0.1f, 0.2f, 0.3f, 0.4f));
        UNSCOPED_INFO("three numbers are opaque");
        CHECK(is(sheet, "Opaque", 0.5f, 0.6f, 0.7f, 1.f));
        CHECK(sheet.diagnostics().empty());
        CHECK(sheet.find("Nope") == nullptr);

        const ALColorSheet::Declaration* used = sheet.declarationOf("Late");
        {
            INFO("the declaration used is Late's own");
            REQUIRE(used);
            CHECK(used->name == "Late");
            CHECK(used->line == 5);
        }
        UNSCOPED_INFO("in the file it came from");
        CHECK(std::string(sheet.fileOf(*used)) == "a.xml");
    }

    // A second file refers to a name only the first declares, and redefines
    // a name the first refers to: the first's reference follows the
    // redefinition. Both directions of override work.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a second file overrides the first both ways and references follow", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Base\" value=\"1 0 0 1\"/>\n"
            "<color name=\"Uses\" reference=\"Base\"/>\n"
            "<color name=\"Only\" value=\"0 1 0 1\"/>\n"
            "<color name=\"WasValue\" value=\"0 0 1 1\"/>\n"
            "<color name=\"WasRef\" reference=\"Only\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Base\" value=\"0 0 0 1\"/>\n"
            "<color name=\"Mine\" reference=\"Only\"/>\n"
            "<color name=\"WasValue\" reference=\"Only\"/>\n"
            "<color name=\"WasRef\" value=\"1 1 1 1\"/>\n", "skin.xml");
        sheet.resolve();

        UNSCOPED_INFO("the skin's Base");
        CHECK(is(sheet, "Base", 0, 0, 0));
        UNSCOPED_INFO("the default's reference to Base follows the skin");
        CHECK(is(sheet, "Uses", 0, 0, 0));
        UNSCOPED_INFO("the skin refers to a default-only name");
        CHECK(is(sheet, "Mine", 0, 1, 0));
        UNSCOPED_INFO("a reference over a value");
        CHECK(is(sheet, "WasValue", 0, 1, 0));
        UNSCOPED_INFO("a value over a reference");
        CHECK(is(sheet, "WasRef", 1, 1, 1));
        UNSCOPED_INFO("across files is not a duplicate");
        CHECK(sheet.diagnostics().empty());

        const ALColorSheet::Declaration* base = sheet.declarationOf("Base");
        {
            INFO("Base is the skin's declaration");
            REQUIRE(base);
            CHECK(sheet.fileOf(*base) == "skin.xml");
            CHECK(base->line == 2);
        }
        {
            INFO("which shadows the default's");
            REQUIRE(base->shadowed >= 0);
            REQUIRE(base->shadowed < static_cast<S32>(sheet.declarations().size()));
            CHECK(sheet.fileOf(sheet.declarations()[base->shadowed]) == "default.xml");
        }
    }

    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a name declared twice in one file takes the last and says so", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Twice\" value=\"1 0 0 1\"/>\n"
            "<color name=\"Other\" value=\"0 0 0 1\"/>\n"
            "<color name=\"Twice\" value=\"0 1 0 1\"/>\n", "a.xml");
        sheet.resolve();

        UNSCOPED_INFO("the last declaration wins");
        CHECK(is(sheet, "Twice", 0, 1, 0));
        CHECK(count(sheet, Kind::Duplicate) == 1u);
        UNSCOPED_INFO("naming both lines");
        CHECK(first(sheet, Kind::Duplicate) ==
                      "a.xml(4): \"Twice\" is declared again; line 2 is shadowed.");
    }

    // A reference to nothing: the name is undefined, the line names the
    // target, and the names undefined through it are listed there rather
    // than reported one by one.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a dangling reference is one line naming its target and dependents", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Fine\" value=\"1 1 1 1\"/>\n"
            "<color name=\"Dangling\" reference=\"Nope\"/>\n"
            "<color name=\"Through\" reference=\"Dangling\"/>\n"
            "<color name=\"Further\" reference=\"Through\"/>\n", "a.xml");
        sheet.resolve();

        CHECK(sheet.find("Dangling") == nullptr);
        CHECK(sheet.find("Through") == nullptr);
        CHECK(sheet.find("Further") == nullptr);
        CHECK(is(sheet, "Fine", 1, 1, 1));
        UNSCOPED_INFO("one line for the root cause: " << all(sheet));
        CHECK(sheet.diagnostics().size() == 1u);
        UNSCOPED_INFO("which names the target and the dependents");
        CHECK(first(sheet, Kind::Undefined) ==
                      "a.xml(3): \"Dangling\" refers to \"Nope\", which no colors.xml defines. "
                      "It is undefined, and so are the colours that refer to it: Through, Further.");
    }

    // A skin entry that dangles leaves the default's entry in force, and
    // the line says which file's declaration is used instead.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a dangling skin entry leaves the default's in force", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Kept\" value=\"0.5 0.5 0.5 1\"/>\n"
            "<color name=\"Uses\" reference=\"Kept\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Kept\" reference=\"Nope\"/>\n", "skin.xml");
        sheet.resolve();

        UNSCOPED_INFO("the default's declaration is used");
        CHECK(is(sheet, "Kept", 0.5f, 0.5f, 0.5f));
        UNSCOPED_INFO("and followed");
        CHECK(is(sheet, "Uses", 0.5f, 0.5f, 0.5f));
        const ALColorSheet::Declaration* used = sheet.declarationOf("Kept");
        {
            INFO("declarationOf is the default's");
            REQUIRE(used);
            CHECK(sheet.fileOf(*used) == "default.xml");
        }
        CHECK(sheet.diagnostics().size() == 1u);
        UNSCOPED_INFO("saying what is used instead");
        CHECK(first(sheet, Kind::Undefined) ==
                      "skin.xml(2): \"Kept\" refers to \"Nope\", which no colors.xml defines; default.xml(2) is used instead.");
    }

    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a colour that refers to itself is undefined", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Self\" reference=\"Self\"/>\n"
            "<color name=\"Onlooker\" value=\"1 1 1 1\"/>\n", "a.xml");
        sheet.resolve();

        CHECK(sheet.find("Self") == nullptr);
        UNSCOPED_INFO("Onlooker is not");
        CHECK(is(sheet, "Onlooker", 1, 1, 1));
        CHECK(count(sheet, Kind::Cycle) == 1u);
        UNSCOPED_INFO("a loop of one");
        CHECK(first(sheet, Kind::Cycle) ==
                      "a.xml(2): \"Self\" refers to itself. It is undefined.");
    }

    // Two and three colours around a loop: one line each, in reference
    // order with every member's line, the members and their dependents
    // undefined, and a fourth colour untouched.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: loops of two and of three are one line each in reference order", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"A\" reference=\"B\"/>\n"
            "<color name=\"B\" reference=\"A\"/>\n"
            "<color name=\"Onlooker\" value=\"1 1 1 1\"/>\n"
            "<color name=\"X\" reference=\"Y\"/>\n"
            "<color name=\"Y\" reference=\"Z\"/>\n"
            "<color name=\"Z\" reference=\"X\"/>\n"
            "<color name=\"Behind\" reference=\"Z\"/>\n", "a.xml");
        sheet.resolve();

        for (const char* name : { "A", "B", "X", "Y", "Z", "Behind" })
        {
            CAPTURE(name);
            REQUIRE(sheet.find(name) == nullptr);
        }
        UNSCOPED_INFO("Onlooker is not");
        CHECK(is(sheet, "Onlooker", 1, 1, 1));
        UNSCOPED_INFO("two lines: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 2u);
        UNSCOPED_INFO("the pair");
        CHECK(sheet.diagnostics()[0].message ==
                      "a.xml(2): \"A\" refers to itself through \"B\" (a.xml(3)). "
                      "None of the 2 declarations is used: \"A\" is undefined, \"B\" is undefined.");
        UNSCOPED_INFO("the triple, with the colour behind it");
        CHECK(sheet.diagnostics()[1].message ==
                      "a.xml(5): \"X\" refers to itself through \"Y\" (a.xml(6)) and \"Z\" (a.xml(7)). "
                      "None of the 3 declarations is used: \"X\" is undefined, \"Y\" is undefined, \"Z\" is undefined, "
                      "and so are the colours that refer to them: Behind.");
    }

    // A loop through two files: both members fall back to the first file's
    // declarations, which are not in the loop, and resolve.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a loop across two files falls back to the first file's colours", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"A\" value=\"1 0 0 1\"/>\n"
            "<color name=\"B\" value=\"0 1 0 1\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"A\" reference=\"B\"/>\n"
            "<color name=\"B\" reference=\"A\"/>\n", "skin.xml");
        sheet.resolve();

        UNSCOPED_INFO("A is the default's");
        CHECK(is(sheet, "A", 1, 0, 0));
        UNSCOPED_INFO("B is the default's");
        CHECK(is(sheet, "B", 0, 1, 0));
        UNSCOPED_INFO("one line: " << all(sheet));
        CHECK(sheet.diagnostics().size() == 1u);
        UNSCOPED_INFO("saying where each member went");
        CHECK(first(sheet, Kind::Cycle) ==
                      "skin.xml(2): \"A\" refers to itself through \"B\" (skin.xml(3)). "
                      "None of the 2 declarations is used: \"A\" takes default.xml(2), \"B\" takes default.xml(3).");
    }

    // A name in the value attribute: reported as such, with the hint, and
    // not as a reference to nothing.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a name in value is reported with a hint to use reference", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"LtGray\" value=\"0.75 0.75 0.75 1\"/>\n"
            "<color name=\"Named\" value=\"LtGray\"/>\n"
            "<color name=\"Short\" value=\"0.5 0.5\"/>\n", "a.xml");
        sheet.resolve();

        CHECK(sheet.find("Named") == nullptr);
        CHECK(sheet.find("Short") == nullptr);
        UNSCOPED_INFO("two bad values and nothing else: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 2u);
        UNSCOPED_INFO("none of them an undefined reference");
        CHECK(count(sheet, Kind::Undefined) == 0u);
        UNSCOPED_INFO("the name, with the hint");
        CHECK(sheet.diagnostics()[0].message ==
                      "a.xml(3): \"Named\" has value=\"LtGray\", which is not three or four numbers; "
                      "a name goes in reference=\"LtGray\". It is undefined.");
        UNSCOPED_INFO("the two numbers, without");
        CHECK(sheet.diagnostics()[1].message ==
                      "a.xml(4): \"Short\" has value=\"0.5 0.5\", which is not three or four numbers. It is undefined.");
    }

    // An attribute the table does not read, leaving the entry with no
    // colour; and an entry with no name at all.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: an unread attribute and a missing name or colour are each reported", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Capital\" Reference=\"LtGray\"/>\n"
            "<color value=\"1 1 1 1\"/>\n"
            "<color name=\"Empty\"/>\n", "a.xml");
        sheet.resolve();

        CHECK(sheet.find("Capital") == nullptr);
        CHECK(sheet.find("Empty") == nullptr);
        UNSCOPED_INFO("four lines: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 4u);
        UNSCOPED_INFO("the attribute");
        CHECK(sheet.diagnostics()[0].message ==
                      "a.xml(2): \"Capital\" has an attribute \"Reference\" the colour table does not read; "
                      "it reads name, value and reference.");
        UNSCOPED_INFO("then the colour it does not have");
        CHECK(sheet.diagnostics()[1].message ==
                      "a.xml(2): \"Capital\" has neither value nor reference. It is undefined.");
        UNSCOPED_INFO("the nameless one");
        CHECK(sheet.diagnostics()[2].message ==
                      "a.xml(3): a <color> with no name is ignored.");
        UNSCOPED_INFO("the empty one");
        CHECK(sheet.diagnostics()[3].message ==
                      "a.xml(4): \"Empty\" has neither value nor reference. It is undefined.");
    }

    // Text after the numbers: the numbers are kept, the rest is reported.
    // Both a value and a reference: the reference is used, and reported.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: text past the numbers and a value with a reference are reported", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Comma\" value=\"0.9 0.0 0.66, 1\"/>\n"
            "<color name=\"Five\" value=\"1 2 3 4 5\"/>\n"
            "<color name=\"Both\" value=\"0 0 0 1\" reference=\"Comma\"/>\n"
            "<color name=\"Glued\" value=\"0.85882 0.858820.85882 1\"/>\n", "a.xml");
        sheet.resolve();

        UNSCOPED_INFO("Comma keeps its three numbers, opaque");
        CHECK(is(sheet, "Comma", 0.9f, 0.0f, 0.66f, 1.f));
        UNSCOPED_INFO("Five keeps four");
        CHECK(is(sheet, "Five", 1, 2, 3, 4));
        UNSCOPED_INFO("Both takes the reference");
        CHECK(is(sheet, "Both", 0.9f, 0.0f, 0.66f, 1.f));
        UNSCOPED_INFO("Glued is not two numbers");
        CHECK(sheet.find("Glued") == nullptr);
        UNSCOPED_INFO("four lines: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 4u);
        UNSCOPED_INFO("the comma");
        CHECK(sheet.diagnostics()[0].message ==
                      "a.xml(2): \"Comma\" has value=\"0.9 0.0 0.66, 1\"; only the first 3 numbers are read.");
        UNSCOPED_INFO("the fifth");
        CHECK(sheet.diagnostics()[1].message ==
                      "a.xml(3): \"Five\" has value=\"1 2 3 4 5\"; only the first 4 numbers are read.");
        UNSCOPED_INFO("both");
        CHECK(sheet.diagnostics()[2].message ==
                      "a.xml(4): \"Both\" has both value=\"0 0 0 1\" and reference=\"Comma\"; the reference is used.");
        UNSCOPED_INFO("glued");
        CHECK(sheet.diagnostics()[3].message ==
                      "a.xml(5): \"Glued\" has value=\"0.85882 0.858820.85882 1\", which is not three or four numbers. "
                      "It is undefined.");
    }

    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a base sheet answers only the names this one lacks", "[alcolorsheet]")
    {
        ALColorSheet base;
        read(base,
            "<color name=\"Base\" value=\"1 0 0 1\"/>\n"
            "<color name=\"Both\" value=\"0 1 0 1\"/>\n", "loaded.xml");
        base.resolve();

        ALColorSheet user;
        read(user,
            "<color name=\"Both\" value=\"0 0 1 1\"/>\n"
            "<color name=\"ToBase\" reference=\"Base\"/>\n"
            "<color name=\"ToBoth\" reference=\"Both\"/>\n"
            "<color name=\"ToNothing\" reference=\"Nope\"/>\n", "user.xml");
        user.resolve(&base);

        UNSCOPED_INFO("a base name resolves");
        CHECK(is(user, "ToBase", 1, 0, 0));
        UNSCOPED_INFO("a name in both is this sheet's");
        CHECK(is(user, "ToBoth", 0, 0, 1));
        UNSCOPED_INFO("the base's names are not the sheet's");
        CHECK(user.find("Base") == nullptr);
        UNSCOPED_INFO("the base does not answer a name neither has");
        CHECK(user.find("ToNothing") == nullptr);
        CHECK(user.diagnostics().size() == 1u);
        {
            INFO("the base is untouched");
            CHECK(is(base, "Both", 0, 1, 0));
            CHECK(base.diagnostics().empty());
        }
    }

    // A bad skin entry over a good default one: the default's colour, and
    // the line says so. The same for a shadowed duplicate within a file.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a bad skin entry falls back to the default and says so", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Named\" value=\"0.2 0.2 0.2 1\"/>\n"
            "<color name=\"Capital\" value=\"0.3 0.3 0.3 1\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Named\" value=\"LtGray\"/>\n"
            "<color name=\"Capital\" Reference=\"LtGray\"/>\n"
            "<color name=\"Good\" value=\"1 1 1 1\"/>\n"
            "<color name=\"Good\" value=\"nope\"/>\n", "skin.xml");
        sheet.resolve();

        UNSCOPED_INFO("Named is the default's");
        CHECK(is(sheet, "Named", 0.2f, 0.2f, 0.2f));
        UNSCOPED_INFO("Capital is the default's");
        CHECK(is(sheet, "Capital", 0.3f, 0.3f, 0.3f));
        UNSCOPED_INFO("Good is its earlier self");
        CHECK(is(sheet, "Good", 1, 1, 1));
        UNSCOPED_INFO("five lines: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 5u);
        UNSCOPED_INFO("the bad value falls back");
        CHECK(sheet.diagnostics()[0].message ==
                      "skin.xml(2): \"Named\" has value=\"LtGray\", which is not three or four numbers; "
                      "a name goes in reference=\"LtGray\"; default.xml(2) is used instead.");
        UNSCOPED_INFO("the attribute is reported");
        CHECK(sheet.diagnostics()[1].kind == Kind::UnknownAttribute);
        UNSCOPED_INFO("and the missing colour falls back");
        CHECK(sheet.diagnostics()[2].message ==
                      "skin.xml(3): \"Capital\" has neither value nor reference; default.xml(3) is used instead.");
        UNSCOPED_INFO("the duplicate");
        CHECK(sheet.diagnostics()[3].message ==
                      "skin.xml(5): \"Good\" is declared again; line 4 is shadowed.");
        UNSCOPED_INFO("which falls back to what it shadowed");
        CHECK(sheet.diagnostics()[4].message ==
                      "skin.xml(5): \"Good\" has value=\"nope\", which is not three or four numbers; "
                      "a name goes in reference=\"nope\"; skin.xml(4) is used instead.");
    }

    // A reference to a name that ended undefined, with a fallback of its
    // own: its own line, saying the target is undefined and what is used.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a reference to an undefined name falls back on a line of its own", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Panel\" value=\"0.1 0.1 0.1 1\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Background\" value=\"Obsidian\"/>\n"
            "<color name=\"Panel\" reference=\"Background\"/>\n"
            "<color name=\"Menu\" reference=\"Panel\"/>\n", "skin.xml");
        sheet.resolve();

        CHECK(sheet.find("Background") == nullptr);
        UNSCOPED_INFO("Panel is the default's");
        CHECK(is(sheet, "Panel", 0.1f, 0.1f, 0.1f));
        UNSCOPED_INFO("Menu follows Panel");
        CHECK(is(sheet, "Menu", 0.1f, 0.1f, 0.1f));
        UNSCOPED_INFO("two lines: " << all(sheet));
        REQUIRE(sheet.diagnostics().size() == 2u);
        UNSCOPED_INFO("the root cause, undefined with nothing behind it");
        CHECK(sheet.diagnostics()[0].message ==
                      "skin.xml(2): \"Background\" has value=\"Obsidian\", which is not three or four numbers; "
                      "a name goes in reference=\"Obsidian\". It is undefined.");
        UNSCOPED_INFO("the one that fell back");
        CHECK(sheet.diagnostics()[1].message ==
                      "skin.xml(3): \"Panel\" refers to \"Background\", which is undefined; default.xml(2) is used instead.");
    }

    // Order: diagnostics come in declaration order, the resolved names in
    // first-declaration order, and a root not <colors> is refused.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: diagnostics and resolved names come in declaration order", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Zed\" value=\"1 1 1 1\"/>\n"
            "<color name=\"Bad1\" value=\"x\"/>\n"
            "<color name=\"Alpha\" reference=\"Zed\"/>\n"
            "<color name=\"Bad2\" reference=\"Nope\"/>\n", "a.xml");
        read(sheet,
            "<color name=\"Alpha\" value=\"0 0 0 1\"/>\n"
            "<color name=\"Bad3\" reference=\"Nope\"/>\n", "b.xml");
        sheet.resolve();

        std::vector<std::string> names;
        sheet.forEachResolved([&](std::string_view name, const LLColor4&) { names.emplace_back(name); });
        REQUIRE(names.size() == 2u);
        CHECK(names[0] == "Zed");
        UNSCOPED_INFO("then Alpha, first declared in a.xml");
        CHECK(names[1] == "Alpha");

        REQUIRE(sheet.diagnostics().size() == 3u);
        UNSCOPED_INFO("a.xml first");
        CHECK(sheet.diagnostics()[0].declaration == 1);
        UNSCOPED_INFO("a.xml second");
        CHECK(sheet.diagnostics()[1].declaration == 3);
        UNSCOPED_INFO("b.xml last");
        CHECK(sheet.diagnostics()[2].declaration == 5);

        ALColorSheet other;
        LLXMLNodePtr root;
        const std::string xml = "<colours><color name=\"A\" value=\"1 1 1 1\"/></colours>";
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), root));
        UNSCOPED_INFO("a root not <colors> is not a colours file");
        CHECK_FALSE(other.read(root, "wrong.xml"));
        {
            INFO("and left nothing behind");
            CHECK(other.declarations().empty());
            CHECK(other.files().empty());
        }
    }

    // The real thing: every colors.xml in the source tree, the default and
    // then each skin over it, resolves; what each has to say is counted so
    // a new fault in a shipped file fails here first.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: every skin's colors.xml over the default resolves cleanly", "[alcolorsheet]")
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
            REQUIRE(sheet.read(default_root, "default/colors.xml"));
            if (std::string(skin) != "default")
            {
                LLXMLNodePtr root;
                REQUIRE(LLXMLNode::parseFile(skins + skin + "/colors.xml", root, nullptr));
                REQUIRE(sheet.read(root, std::string(skin) + "/colors.xml"));
            }
            sheet.resolve();
            REQUIRE(sheet.find("EmphasisColor") != nullptr);
            UNSCOPED_INFO("has nothing to report:\n" << all(sheet));
            REQUIRE(sheet.diagnostics().empty());
        }
#else
        SKIP("no LLUI_TEST_APP_DIR");
#endif
    }

    // Reading again after resolving: what the first resolution said about
    // a declaration the new file shadows is not said twice, and the second
    // resolution stands on its own.
    TEST_CASE_METHOD(alcolorsheet_data, "alcolorsheet: a file read after resolving clears what the first resolution said", "[alcolorsheet]")
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"A\" reference=\"Nope\"/>\n"
            "<color name=\"B\" reference=\"A\"/>\n", "first.xml");
        sheet.resolve();
        UNSCOPED_INFO("one line the first time");
        CHECK(sheet.diagnostics().size() == 1u);

        read(sheet,
            "<color name=\"A\" value=\"1 1 1 1\"/>\n", "second.xml");
        sheet.resolve();
        UNSCOPED_INFO("A is now defined");
        CHECK(is(sheet, "A", 1, 1, 1));
        UNSCOPED_INFO("and so is B");
        CHECK(is(sheet, "B", 1, 1, 1));
        UNSCOPED_INFO("and nothing is left to say: " << all(sheet));
        CHECK(sheet.diagnostics().size() == 0u);
    }
}

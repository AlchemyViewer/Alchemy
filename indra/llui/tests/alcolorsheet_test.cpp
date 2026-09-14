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

#include "../alcolorsheet.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gColorSheetTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gColorSheetTestAnonName;
}

namespace tut
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

    typedef test_group<alcolorsheet_data> alcolorsheet_test;
    typedef alcolorsheet_test::object     alcolorsheet_object;
    tut::alcolorsheet_test alcolorsheet_testgroup("alcolorsheet");

    // Values, references, a chain, a reference declared before what it
    // names, and three numbers making an opaque colour.
    template<> template<>
    void alcolorsheet_object::test<1>()
    {
        ALColorSheet sheet;
        ensure("reads", read(sheet,
            "<color name=\"Early\" reference=\"Base\"/>\n"
            "<color name=\"Base\" value=\"0.1 0.2 0.3 0.4\"/>\n"
            "<color name=\"Mid\" reference=\"Base\"/>\n"
            "<color name=\"Late\" reference=\"Mid\"/>\n"
            "<color name=\"Opaque\" value=\"0.5 0.6 0.7\"/>\n", "a.xml"));
        sheet.resolve();

        ensure("a value", is(sheet, "Base", 0.1f, 0.2f, 0.3f, 0.4f));
        ensure("a reference", is(sheet, "Mid", 0.1f, 0.2f, 0.3f, 0.4f));
        ensure("a chain", is(sheet, "Late", 0.1f, 0.2f, 0.3f, 0.4f));
        ensure("a reference ahead of its value", is(sheet, "Early", 0.1f, 0.2f, 0.3f, 0.4f));
        ensure("three numbers are opaque", is(sheet, "Opaque", 0.5f, 0.6f, 0.7f, 1.f));
        ensure("nothing to say", sheet.diagnostics().empty());
        ensure("nothing else", sheet.find("Nope") == nullptr);

        const ALColorSheet::Declaration* used = sheet.declarationOf("Late");
        ensure("the declaration used is Late's own", used && used->name == "Late" && used->line == 5);
        ensure_equals("in the file it came from", std::string(sheet.fileOf(*used)), "a.xml");
    }

    // A second file refers to a name only the first declares, and redefines
    // a name the first refers to: the first's reference follows the
    // redefinition. Both directions of override work.
    template<> template<>
    void alcolorsheet_object::test<2>()
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

        ensure("the skin's Base", is(sheet, "Base", 0, 0, 0));
        ensure("the default's reference to Base follows the skin", is(sheet, "Uses", 0, 0, 0));
        ensure("the skin refers to a default-only name", is(sheet, "Mine", 0, 1, 0));
        ensure("a reference over a value", is(sheet, "WasValue", 0, 1, 0));
        ensure("a value over a reference", is(sheet, "WasRef", 1, 1, 1));
        ensure("across files is not a duplicate", sheet.diagnostics().empty());

        const ALColorSheet::Declaration* base = sheet.declarationOf("Base");
        ensure("Base is the skin's declaration", base && sheet.fileOf(*base) == "skin.xml" && base->line == 2);
        ensure("which shadows the default's", base->shadowed >= 0
               && sheet.fileOf(sheet.declarations()[base->shadowed]) == "default.xml");
    }

    // The same name twice in one file: the last wins and the line says so.
    template<> template<>
    void alcolorsheet_object::test<3>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Twice\" value=\"1 0 0 1\"/>\n"
            "<color name=\"Other\" value=\"0 0 0 1\"/>\n"
            "<color name=\"Twice\" value=\"0 1 0 1\"/>\n", "a.xml");
        sheet.resolve();

        ensure("the last declaration wins", is(sheet, "Twice", 0, 1, 0));
        ensure_equals("one duplicate", count(sheet, Kind::Duplicate), 1u);
        ensure_equals("naming both lines", first(sheet, Kind::Duplicate),
                      "a.xml(4): \"Twice\" is declared again; line 2 is shadowed.");
    }

    // A reference to nothing: the name is undefined, the line names the
    // target, and the names undefined through it are listed there rather
    // than reported one by one.
    template<> template<>
    void alcolorsheet_object::test<4>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Fine\" value=\"1 1 1 1\"/>\n"
            "<color name=\"Dangling\" reference=\"Nope\"/>\n"
            "<color name=\"Through\" reference=\"Dangling\"/>\n"
            "<color name=\"Further\" reference=\"Through\"/>\n", "a.xml");
        sheet.resolve();

        ensure("Dangling is undefined", sheet.find("Dangling") == nullptr);
        ensure("and so is Through", sheet.find("Through") == nullptr);
        ensure("and Further", sheet.find("Further") == nullptr);
        ensure("Fine is fine", is(sheet, "Fine", 1, 1, 1));
        ensure_equals("one line for the root cause: " + all(sheet), sheet.diagnostics().size(), 1u);
        ensure_equals("which names the target and the dependents", first(sheet, Kind::Undefined),
                      "a.xml(3): \"Dangling\" refers to \"Nope\", which no colors.xml defines. "
                      "It is undefined, and so are the colours that refer to it: Through, Further.");
    }

    // A skin entry that dangles leaves the default's entry in force, and
    // the line says which file's declaration is used instead.
    template<> template<>
    void alcolorsheet_object::test<5>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Kept\" value=\"0.5 0.5 0.5 1\"/>\n"
            "<color name=\"Uses\" reference=\"Kept\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Kept\" reference=\"Nope\"/>\n", "skin.xml");
        sheet.resolve();

        ensure("the default's declaration is used", is(sheet, "Kept", 0.5f, 0.5f, 0.5f));
        ensure("and followed", is(sheet, "Uses", 0.5f, 0.5f, 0.5f));
        const ALColorSheet::Declaration* used = sheet.declarationOf("Kept");
        ensure("declarationOf is the default's", used && sheet.fileOf(*used) == "default.xml");
        ensure_equals("one line", sheet.diagnostics().size(), 1u);
        ensure_equals("saying what is used instead", first(sheet, Kind::Undefined),
                      "skin.xml(2): \"Kept\" refers to \"Nope\", which no colors.xml defines; default.xml(2) is used instead.");
    }

    // A colour that refers to itself.
    template<> template<>
    void alcolorsheet_object::test<6>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Self\" reference=\"Self\"/>\n"
            "<color name=\"Onlooker\" value=\"1 1 1 1\"/>\n", "a.xml");
        sheet.resolve();

        ensure("Self is undefined", sheet.find("Self") == nullptr);
        ensure("Onlooker is not", is(sheet, "Onlooker", 1, 1, 1));
        ensure_equals("one cycle", count(sheet, Kind::Cycle), 1u);
        ensure_equals("a loop of one", first(sheet, Kind::Cycle),
                      "a.xml(2): \"Self\" refers to itself. It is undefined.");
    }

    // Two and three colours around a loop: one line each, in reference
    // order with every member's line, the members and their dependents
    // undefined, and a fourth colour untouched.
    template<> template<>
    void alcolorsheet_object::test<7>()
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
            ensure(std::string(name) + " is undefined", sheet.find(name) == nullptr);
        }
        ensure("Onlooker is not", is(sheet, "Onlooker", 1, 1, 1));
        ensure_equals("two lines: " + all(sheet), sheet.diagnostics().size(), 2u);
        ensure_equals("the pair", sheet.diagnostics()[0].message,
                      "a.xml(2): \"A\" refers to itself through \"B\" (a.xml(3)). "
                      "None of the 2 declarations is used: \"A\" is undefined, \"B\" is undefined.");
        ensure_equals("the triple, with the colour behind it", sheet.diagnostics()[1].message,
                      "a.xml(5): \"X\" refers to itself through \"Y\" (a.xml(6)) and \"Z\" (a.xml(7)). "
                      "None of the 3 declarations is used: \"X\" is undefined, \"Y\" is undefined, \"Z\" is undefined, "
                      "and so are the colours that refer to them: Behind.");
    }

    // A loop through two files: both members fall back to the first file's
    // declarations, which are not in the loop, and resolve.
    template<> template<>
    void alcolorsheet_object::test<8>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"A\" value=\"1 0 0 1\"/>\n"
            "<color name=\"B\" value=\"0 1 0 1\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"A\" reference=\"B\"/>\n"
            "<color name=\"B\" reference=\"A\"/>\n", "skin.xml");
        sheet.resolve();

        ensure("A is the default's", is(sheet, "A", 1, 0, 0));
        ensure("B is the default's", is(sheet, "B", 0, 1, 0));
        ensure_equals("one line: " + all(sheet), sheet.diagnostics().size(), 1u);
        ensure_equals("saying where each member went", first(sheet, Kind::Cycle),
                      "skin.xml(2): \"A\" refers to itself through \"B\" (skin.xml(3)). "
                      "None of the 2 declarations is used: \"A\" takes default.xml(2), \"B\" takes default.xml(3).");
    }

    // A name in the value attribute: reported as such, with the hint, and
    // not as a reference to nothing.
    template<> template<>
    void alcolorsheet_object::test<9>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"LtGray\" value=\"0.75 0.75 0.75 1\"/>\n"
            "<color name=\"Named\" value=\"LtGray\"/>\n"
            "<color name=\"Short\" value=\"0.5 0.5\"/>\n", "a.xml");
        sheet.resolve();

        ensure("Named is undefined", sheet.find("Named") == nullptr);
        ensure("Short is undefined", sheet.find("Short") == nullptr);
        ensure_equals("two bad values and nothing else: " + all(sheet), sheet.diagnostics().size(), 2u);
        ensure_equals("none of them an undefined reference", count(sheet, Kind::Undefined), 0u);
        ensure_equals("the name, with the hint", sheet.diagnostics()[0].message,
                      "a.xml(3): \"Named\" has value=\"LtGray\", which is not three or four numbers; "
                      "a name goes in reference=\"LtGray\". It is undefined.");
        ensure_equals("the two numbers, without", sheet.diagnostics()[1].message,
                      "a.xml(4): \"Short\" has value=\"0.5 0.5\", which is not three or four numbers. It is undefined.");
    }

    // An attribute the table does not read, leaving the entry with no
    // colour; and an entry with no name at all.
    template<> template<>
    void alcolorsheet_object::test<10>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Capital\" Reference=\"LtGray\"/>\n"
            "<color value=\"1 1 1 1\"/>\n"
            "<color name=\"Empty\"/>\n", "a.xml");
        sheet.resolve();

        ensure("Capital is undefined", sheet.find("Capital") == nullptr);
        ensure("Empty is undefined", sheet.find("Empty") == nullptr);
        ensure_equals("four lines: " + all(sheet), sheet.diagnostics().size(), 4u);
        ensure_equals("the attribute", sheet.diagnostics()[0].message,
                      "a.xml(2): \"Capital\" has an attribute \"Reference\" the colour table does not read; "
                      "it reads name, value and reference.");
        ensure_equals("then the colour it does not have", sheet.diagnostics()[1].message,
                      "a.xml(2): \"Capital\" has neither value nor reference. It is undefined.");
        ensure_equals("the nameless one", sheet.diagnostics()[2].message,
                      "a.xml(3): a <color> with no name is ignored.");
        ensure_equals("the empty one", sheet.diagnostics()[3].message,
                      "a.xml(4): \"Empty\" has neither value nor reference. It is undefined.");
    }

    // Text after the numbers: the numbers are kept, the rest is reported.
    // Both a value and a reference: the reference is used, and reported.
    template<> template<>
    void alcolorsheet_object::test<11>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Comma\" value=\"0.9 0.0 0.66, 1\"/>\n"
            "<color name=\"Five\" value=\"1 2 3 4 5\"/>\n"
            "<color name=\"Both\" value=\"0 0 0 1\" reference=\"Comma\"/>\n"
            "<color name=\"Glued\" value=\"0.85882 0.858820.85882 1\"/>\n", "a.xml");
        sheet.resolve();

        ensure("Comma keeps its three numbers, opaque", is(sheet, "Comma", 0.9f, 0.0f, 0.66f, 1.f));
        ensure("Five keeps four", is(sheet, "Five", 1, 2, 3, 4));
        ensure("Both takes the reference", is(sheet, "Both", 0.9f, 0.0f, 0.66f, 1.f));
        ensure("Glued is not two numbers", sheet.find("Glued") == nullptr);
        ensure_equals("four lines: " + all(sheet), sheet.diagnostics().size(), 4u);
        ensure_equals("the comma", sheet.diagnostics()[0].message,
                      "a.xml(2): \"Comma\" has value=\"0.9 0.0 0.66, 1\"; only the first 3 numbers are read.");
        ensure_equals("the fifth", sheet.diagnostics()[1].message,
                      "a.xml(3): \"Five\" has value=\"1 2 3 4 5\"; only the first 4 numbers are read.");
        ensure_equals("both", sheet.diagnostics()[2].message,
                      "a.xml(4): \"Both\" has both value=\"0 0 0 1\" and reference=\"Comma\"; the reference is used.");
        ensure_equals("glued", sheet.diagnostics()[3].message,
                      "a.xml(5): \"Glued\" has value=\"0.85882 0.858820.85882 1\", which is not three or four numbers. "
                      "It is undefined.");
    }

    // A base sheet answers the names this one lacks and none of the ones it
    // has.
    template<> template<>
    void alcolorsheet_object::test<12>()
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

        ensure("a base name resolves", is(user, "ToBase", 1, 0, 0));
        ensure("a name in both is this sheet's", is(user, "ToBoth", 0, 0, 1));
        ensure("the base is not the sheet", user.find("Base") == nullptr);
        ensure("the base does not answer nothing", user.find("ToNothing") == nullptr);
        ensure_equals("one line", user.diagnostics().size(), 1u);
        ensure("the base is untouched", is(base, "Both", 0, 1, 0) && base.diagnostics().empty());
    }

    // A bad skin entry over a good default one: the default's colour, and
    // the line says so. The same for a shadowed duplicate within a file.
    template<> template<>
    void alcolorsheet_object::test<13>()
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

        ensure("Named is the default's", is(sheet, "Named", 0.2f, 0.2f, 0.2f));
        ensure("Capital is the default's", is(sheet, "Capital", 0.3f, 0.3f, 0.3f));
        ensure("Good is its earlier self", is(sheet, "Good", 1, 1, 1));
        ensure_equals("five lines: " + all(sheet), sheet.diagnostics().size(), 5u);
        ensure_equals("the bad value falls back", sheet.diagnostics()[0].message,
                      "skin.xml(2): \"Named\" has value=\"LtGray\", which is not three or four numbers; "
                      "a name goes in reference=\"LtGray\"; default.xml(2) is used instead.");
        ensure("the attribute is reported", sheet.diagnostics()[1].kind == Kind::UnknownAttribute);
        ensure_equals("and the missing colour falls back", sheet.diagnostics()[2].message,
                      "skin.xml(3): \"Capital\" has neither value nor reference; default.xml(3) is used instead.");
        ensure_equals("the duplicate", sheet.diagnostics()[3].message,
                      "skin.xml(5): \"Good\" is declared again; line 4 is shadowed.");
        ensure_equals("which falls back to what it shadowed", sheet.diagnostics()[4].message,
                      "skin.xml(5): \"Good\" has value=\"nope\", which is not three or four numbers; "
                      "a name goes in reference=\"nope\"; skin.xml(4) is used instead.");
    }

    // A reference to a name that ended undefined, with a fallback of its
    // own: its own line, saying the target is undefined and what is used.
    template<> template<>
    void alcolorsheet_object::test<14>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"Panel\" value=\"0.1 0.1 0.1 1\"/>\n", "default.xml");
        read(sheet,
            "<color name=\"Background\" value=\"Obsidian\"/>\n"
            "<color name=\"Panel\" reference=\"Background\"/>\n"
            "<color name=\"Menu\" reference=\"Panel\"/>\n", "skin.xml");
        sheet.resolve();

        ensure("Background is undefined", sheet.find("Background") == nullptr);
        ensure("Panel is the default's", is(sheet, "Panel", 0.1f, 0.1f, 0.1f));
        ensure("Menu follows Panel", is(sheet, "Menu", 0.1f, 0.1f, 0.1f));
        ensure_equals("two lines: " + all(sheet), sheet.diagnostics().size(), 2u);
        ensure_equals("the root cause, undefined with nothing behind it", sheet.diagnostics()[0].message,
                      "skin.xml(2): \"Background\" has value=\"Obsidian\", which is not three or four numbers; "
                      "a name goes in reference=\"Obsidian\". It is undefined.");
        ensure_equals("the one that fell back", sheet.diagnostics()[1].message,
                      "skin.xml(3): \"Panel\" refers to \"Background\", which is undefined; default.xml(2) is used instead.");
    }

    // Order: diagnostics come in declaration order, the resolved names in
    // first-declaration order, and a root not <colors> is refused.
    template<> template<>
    void alcolorsheet_object::test<15>()
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
        ensure_equals("two resolved", names.size(), 2u);
        ensure_equals("Zed first", names[0], "Zed");
        ensure_equals("then Alpha, first declared in a.xml", names[1], "Alpha");

        ensure_equals("three lines", sheet.diagnostics().size(), 3u);
        ensure_equals("a.xml first", sheet.diagnostics()[0].declaration, 1);
        ensure_equals("a.xml second", sheet.diagnostics()[1].declaration, 3);
        ensure_equals("b.xml last", sheet.diagnostics()[2].declaration, 5);

        ALColorSheet other;
        LLXMLNodePtr root;
        const std::string xml = "<colours><color name=\"A\" value=\"1 1 1 1\"/></colours>";
        ensure("parses", LLXMLNode::parseBuffer(xml.data(), xml.size(), root));
        ensure("but is not a colours file", !other.read(root, "wrong.xml"));
        ensure("and left nothing behind", other.declarations().empty() && other.files().empty());
    }

    // The real thing: every colors.xml in the source tree, the default and
    // then each skin over it, resolves; what each has to say is counted so
    // a new fault in a shipped file fails here first.
    template<> template<>
    void alcolorsheet_object::test<16>()
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
            ensure("the default reads", sheet.read(default_root, "default/colors.xml"));
            if (std::string(skin) != "default")
            {
                LLXMLNodePtr root;
                ensure(std::string(skin) + " parses", LLXMLNode::parseFile(skins + skin + "/colors.xml", root, nullptr));
                ensure(std::string(skin) + " reads", sheet.read(root, std::string(skin) + "/colors.xml"));
            }
            sheet.resolve();
            ensure(std::string(skin) + " has EmphasisColor", sheet.find("EmphasisColor") != nullptr);
            ensure(std::string(skin) + " has nothing to report:\n" + all(sheet), sheet.diagnostics().empty());
        }
#else
        skip("no LLUI_TEST_APP_DIR");
#endif
    }

    // Reading again after resolving: what the first resolution said about
    // a declaration the new file shadows is not said twice, and the second
    // resolution stands on its own.
    template<> template<>
    void alcolorsheet_object::test<17>()
    {
        ALColorSheet sheet;
        read(sheet,
            "<color name=\"A\" reference=\"Nope\"/>\n"
            "<color name=\"B\" reference=\"A\"/>\n", "first.xml");
        sheet.resolve();
        ensure_equals("one line the first time", sheet.diagnostics().size(), 1u);

        read(sheet,
            "<color name=\"A\" value=\"1 1 1 1\"/>\n", "second.xml");
        sheet.resolve();
        ensure("A is now defined", is(sheet, "A", 1, 1, 1));
        ensure("and so is B", is(sheet, "B", 1, 1, 1));
        ensure_equals("and nothing is left to say: " + all(sheet), sheet.diagnostics().size(), 0u);
    }
}

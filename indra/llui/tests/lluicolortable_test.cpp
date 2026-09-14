/**
 * @file lluicolortable_test.cpp
 * @brief The colour table over its sheets: handles that outlive a reload, and the user's layer.
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

#include "../lluicolortable.h"

#include "../test/lltut.h"

#include <sstream>

class LLAvatarName;
const std::string gColorTableTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gColorTableTestAnonName;
}

namespace tut
{
    // The table is a singleton and lives across the tests; each uses names
    // of its own.
    struct lluicolortable_data
    {
        static LLXMLNodePtr parse(const std::string& body)
        {
            const std::string xml = "<colors>\n" + body + "</colors>\n";
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), root))
            {
                return nullptr;
            }
            return root;
        }

        static bool load(const std::vector<std::string>& skins, const std::string& user = std::string())
        {
            std::vector<LLUIColorTable::document_t> documents;
            for (size_t i = 0; i < skins.size(); ++i)
            {
                LLXMLNodePtr root = parse(skins[i]);
                if (root.isNull())
                {
                    return false;
                }
                documents.emplace_back(root, "skin" + std::to_string(i) + ".xml");
            }
            LLXMLNodePtr user_root;
            if (!user.empty())
            {
                user_root = parse(user);
                if (user_root.isNull())
                {
                    return false;
                }
            }
            return LLUIColorTable::instance().load(documents, user_root, "user.xml");
        }

        static bool is(const char* name, F32 r, F32 g, F32 b, F32 a = 1.f)
        {
            return LLUIColorTable::instance().getColor(name).get() == LLColor4(r, g, b, a);
        }

        template <typename MAP>
        static bool has(const MAP& table, const char* name)
        {
            return table.find(name) != table.end();
        }

        static std::string written(bool scrub = false)
        {
            std::ostringstream out;
            LLUIColorTable::instance().userSettingsDocument(scrub)->writeToOstream(out);
            return out.str();
        }
    };

    typedef test_group<lluicolortable_data> lluicolortable_test;
    typedef lluicolortable_test::object     lluicolortable_object;
    tut::lluicolortable_test lluicolortable_testgroup("lluicolortable");

    // Two skin documents: an inherited name, an overridden one, and one the
    // default declares by reference to a name the skin overrides.
    template<> template<>
    void lluicolortable_object::test<1>()
    {
        ensure("loads", load({
            "<color name=\"T1Base\" value=\"1 0 0 1\"/>\n"
            "<color name=\"T1Inherited\" value=\"0 1 0 1\"/>\n"
            "<color name=\"T1Follows\" reference=\"T1Base\"/>\n",
            "<color name=\"T1Base\" value=\"0 0 1 1\"/>\n" }));
        const LLUIColorTable& table = LLUIColorTable::instance();

        ensure("inherited", is("T1Inherited", 0, 1, 0));
        ensure("overridden", is("T1Base", 0, 0, 1));
        ensure("the default's reference follows the skin", is("T1Follows", 0, 0, 1));
        ensure("all three exist", table.colorExists("T1Base") && table.colorExists("T1Inherited") && table.colorExists("T1Follows"));
        ensure("and are default", table.isDefault("T1Base") && table.isDefault("T1Follows"));
        ensure("an unknown name is the caller's default", table.getColor("T1Nope", LLColor4::yellow).get() == LLColor4::yellow);
        ensure("and does not exist", !table.colorExists("T1Nope"));
        ensure_equals("the sheet knows both files", table.getLoadedSheet().files().size(), 2u);
        ensure("and where T1Base came from", table.getLoadedSheet().declarationOf("T1Base")->file == 1);
    }

    // A handle taken before a reload reads the colour after it. A name the
    // reload no longer declares is magenta, and still exists: its handles
    // are pointers into the table.
    template<> template<>
    void lluicolortable_object::test<2>()
    {
        load({ "<color name=\"T2A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T2Gone\" value=\"0 1 0 1\"/>\n" });
        const LLUIColor a = LLUIColorTable::instance().getColor("T2A");
        const LLUIColor gone = LLUIColorTable::instance().getColor("T2Gone");
        ensure("the handle is a pointer", a.isReference() && gone.isReference());
        ensure("and reads the first load", a.get() == LLColor4(1, 0, 0, 1));

        load({ "<color name=\"T2A\" value=\"0 0 1 1\"/>\n" });
        ensure("the handle reads the second", a.get() == LLColor4(0, 0, 1, 1));
        ensure("the dropped name is magenta", gone.get() == LLColor4::magenta);
        ensure("and still exists", LLUIColorTable::instance().colorExists("T2Gone"));
    }

    // The user's document over the loaded one: the user's colour wins, is
    // not default, resets to the loaded one, and the handle follows all of
    // it. A user reference resolves against the user's own colours and lands
    // in the user table, never the loaded one.
    template<> template<>
    void lluicolortable_object::test<3>()
    {
        load({ "<color name=\"T3A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T3Loaded\" value=\"0 1 0 1\"/>\n" },
             "<color name=\"T3A\" value=\"0 0 1 1\"/>\n"
             "<color name=\"T3B\" reference=\"T3A\"/>\n"
             "<color name=\"T3C\" reference=\"T3Loaded\"/>\n");
        LLUIColorTable& table = LLUIColorTable::instance();

        const LLUIColor a = table.getColor("T3A");
        ensure("the user's colour", a.get() == LLColor4(0, 0, 1, 1));
        ensure("is not default", !table.isDefault("T3A"));
        ensure("a user reference to a user colour", is("T3B", 0, 0, 1));
        ensure("a user reference to a loaded colour", is("T3C", 0, 1, 0));
        ensure("both in the user table", has(table.getUserColors(), "T3B") && has(table.getUserColors(), "T3C"));
        ensure("neither in the loaded one", !has(table.getLoadedColors(), "T3B") && !has(table.getLoadedColors(), "T3C"));
        ensure("the user sheet says nothing", table.getUserSheet().diagnostics().empty());

        table.resetToDefault("T3A");
        ensure("reset to the loaded colour", a.get() == LLColor4(1, 0, 0, 1));
        ensure("which is default again", table.isDefault("T3A"));
    }

    // A colour set at runtime moves to the user layer, and the document
    // written back holds only what differs from the loaded colours, in the
    // form the old writer used; read back, it reproduces the table. A user
    // colour set since the last load survives a reload.
    template<> template<>
    void lluicolortable_object::test<4>()
    {
        load({ "<color name=\"T4A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T4Same\" value=\"0 1 0 1\"/>\n"
               "<color name=\"ColorPaletteEntry99\" value=\"0 0 0 1\"/>\n" },
             "<color name=\"T4Same\" value=\"0 1 0 1\"/>\n");
        LLUIColorTable& table = LLUIColorTable::instance();

        const LLUIColor a = table.getColor("T4A");
        table.setColor("T4A", LLColor4(0.5f, 0.25f, 0.f, 1.f));
        table.setColor("ColorPaletteEntry99", LLColor4(1, 1, 1, 1));
        ensure("the handle reads the user's colour", a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        ensure("which is not default", !table.isDefault("T4A"));
        ensure("the loaded colour is kept aside", table.getLoadedColors().find("T4A")->second.get() == LLColor4(1, 0, 0, 1));

        const std::string text = written();
        ensure_contains("the changed colour is written", text, "name=\"T4A\"");
        ensure_contains("as the old writer wrote it", text, "value=\"0.5 0.25 0 1\"");
        ensure_does_not_contain("a user colour equal to the loaded one is not", text, "T4Same");
        ensure_contains("scrub keeps the palette", written(true), "ColorPaletteEntry99");
        ensure_does_not_contain("and nothing else", written(true), "T4A");

        LLXMLNodePtr round_trip;
        ensure("what was written parses", LLXMLNode::parseBuffer(text.data(), text.size(), round_trip));
        load({ "<color name=\"T4A\" value=\"1 0 0 1\"/>\n" });
        ensure("a reload without the user document keeps the runtime colour", a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        std::vector<LLUIColorTable::document_t> documents = { { parse("<color name=\"T4A\" value=\"1 0 0 1\"/>\n"), "skin0.xml" } };
        table.load(documents, round_trip, "user.xml");
        ensure("and the written document reproduces it", a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        ensure("in the user table", table.getUserColors().find("T4A")->second.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
    }

    // What the skins' files have to say reaches the loaded sheet, and a
    // file that is not a colours file is refused without losing the rest.
    template<> template<>
    void lluicolortable_object::test<5>()
    {
        std::vector<LLUIColorTable::document_t> documents;
        documents.emplace_back(parse("<color name=\"T5A\" value=\"1 0 0 1\"/>\n"
                                     "<color name=\"T5Bad\" value=\"Nope\"/>\n"), "good.xml");
        LLXMLNodePtr wrong;
        const std::string xml = "<colours/>";
        LLXMLNode::parseBuffer(xml.data(), xml.size(), wrong);
        documents.emplace_back(wrong, "wrong.xml");
        LLUIColorTable& table = LLUIColorTable::instance();

        ensure("one good file is a load", table.load(documents, nullptr, "user.xml"));
        ensure("its colours are in", is("T5A", 1, 0, 0));
        ensure_equals("and its fault is on record", table.getLoadedSheet().diagnostics().size(), 1u);
        ensure_equals("one file was read", table.getLoadedSheet().files().size(), 1u);

        documents.erase(documents.begin());
        ensure("no good file is not", !table.load(documents, nullptr, "user.xml"));
    }
}

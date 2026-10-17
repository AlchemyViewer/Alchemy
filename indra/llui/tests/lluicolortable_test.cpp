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

#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

namespace
{
    // The table is a singleton and lives across the tests; each uses names
    // of its own, and what it loads and sets is put back as it found it.
    struct lluicolortable_data
    {
        ll_test::ColorTableScope colours{ ll_test::ColorTableScope::USER_AND_LOADED };

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

    // Two skin documents: an inherited name, an overridden one, and one the
    // default declares by reference to a name the skin overrides.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: a second skin overrides a name and a reference follows it", "[lluicolortable]")
    {
        CHECK(load({
            "<color name=\"T1Base\" value=\"1 0 0 1\"/>\n"
            "<color name=\"T1Inherited\" value=\"0 1 0 1\"/>\n"
            "<color name=\"T1Follows\" reference=\"T1Base\"/>\n",
            "<color name=\"T1Base\" value=\"0 0 1 1\"/>\n" }));
        const LLUIColorTable& table = LLUIColorTable::instance();

        CHECK(is("T1Inherited", 0, 1, 0));
        CHECK(is("T1Base", 0, 0, 1));
        UNSCOPED_INFO("the default's reference follows the skin");
        CHECK(is("T1Follows", 0, 0, 1));
        CHECK(table.colorExists("T1Base"));
        CHECK(table.colorExists("T1Inherited"));
        CHECK(table.colorExists("T1Follows"));
        CHECK(table.isDefault("T1Base"));
        CHECK(table.isDefault("T1Follows"));
        CHECK(table.getColor("T1Nope", LLColor4::yellow).get() == LLColor4::yellow);
        CHECK_FALSE(table.colorExists("T1Nope"));
        CHECK(table.getLoadedSheet().files().size() == 2u);
        const ALColorSheet::Declaration* base = table.getLoadedSheet().declarationOf("T1Base");
        REQUIRE(base != nullptr);
        UNSCOPED_INFO("T1Base was last declared in the second file");
        CHECK(base->file == 1);
    }

    // A handle taken before a reload reads the colour after it. A name the
    // reload no longer declares is magenta, and still exists: its handles
    // are pointers into the table.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: a handle taken before a reload reads the colour after it", "[lluicolortable]")
    {
        load({ "<color name=\"T2A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T2Gone\" value=\"0 1 0 1\"/>\n" });
        const LLUIColor a = LLUIColorTable::instance().getColor("T2A");
        const LLUIColor gone = LLUIColorTable::instance().getColor("T2Gone");
        {
            INFO("the handle is a pointer");
            CHECK(a.isReference());
            CHECK(gone.isReference());
        }
        CHECK(a.get() == LLColor4(1, 0, 0, 1));

        load({ "<color name=\"T2A\" value=\"0 0 1 1\"/>\n" });
        CHECK(a.get() == LLColor4(0, 0, 1, 1));
        CHECK(gone.get() == LLColor4::magenta);
        CHECK(LLUIColorTable::instance().colorExists("T2Gone"));
    }

    // The user's document over the loaded one: the user's colour wins, is
    // not default, resets to the loaded one, and the handle follows all of
    // it. A user reference resolves against the user's own colours and lands
    // in the user table, never the loaded one.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: the user's document wins over the loaded one until reset", "[lluicolortable]")
    {
        load({ "<color name=\"T3A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T3Loaded\" value=\"0 1 0 1\"/>\n" },
             "<color name=\"T3A\" value=\"0 0 1 1\"/>\n"
             "<color name=\"T3B\" reference=\"T3A\"/>\n"
             "<color name=\"T3C\" reference=\"T3Loaded\"/>\n");
        LLUIColorTable& table = LLUIColorTable::instance();

        const LLUIColor a = table.getColor("T3A");
        CHECK(a.get() == LLColor4(0, 0, 1, 1));
        CHECK_FALSE(table.isDefault("T3A"));
        UNSCOPED_INFO("a user reference to a user colour");
        CHECK(is("T3B", 0, 0, 1));
        UNSCOPED_INFO("a user reference to a loaded colour");
        CHECK(is("T3C", 0, 1, 0));
        CHECK(has(table.getUserColors(), "T3B"));
        CHECK(has(table.getUserColors(), "T3C"));
        CHECK_FALSE(has(table.getLoadedColors(), "T3B"));
        CHECK_FALSE(has(table.getLoadedColors(), "T3C"));
        CHECK(table.getUserSheet().diagnostics().empty());

        table.resetToDefault("T3A");
        CHECK(a.get() == LLColor4(1, 0, 0, 1));
        CHECK(table.isDefault("T3A"));
    }

    // A colour set at runtime moves to the user layer, and the document
    // written back holds only what differs from the loaded colours, in the
    // form the old writer used; read back, it reproduces the table. A user
    // colour set since the last load survives a reload.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: a colour set at runtime is written back and survives a reload", "[lluicolortable]")
    {
        load({ "<color name=\"T4A\" value=\"1 0 0 1\"/>\n"
               "<color name=\"T4Same\" value=\"0 1 0 1\"/>\n"
               "<color name=\"ColorPaletteEntry99\" value=\"0 0 0 1\"/>\n" },
             "<color name=\"T4Same\" value=\"0 1 0 1\"/>\n");
        LLUIColorTable& table = LLUIColorTable::instance();

        const LLUIColor a = table.getColor("T4A");
        table.setColor("T4A", LLColor4(0.5f, 0.25f, 0.f, 1.f));
        table.setColor("ColorPaletteEntry99", LLColor4(1, 1, 1, 1));
        CHECK(a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        CHECK_FALSE(table.isDefault("T4A"));
        const auto loaded = table.getLoadedColors().find("T4A");
        REQUIRE(loaded != table.getLoadedColors().end());
        CHECK(loaded->second.get() == LLColor4(1, 0, 0, 1));

        const std::string text = written();
        CHECK_THAT(text, Catch::Matchers::ContainsSubstring("name=\"T4A\""));
        UNSCOPED_INFO("as the old writer wrote it");
        CHECK_THAT(text, Catch::Matchers::ContainsSubstring("value=\"0.5 0.25 0 1\""));
        UNSCOPED_INFO("a user colour equal to the loaded one is not");
        CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("T4Same"));
        UNSCOPED_INFO("scrub keeps the palette");
        CHECK_THAT(written(true), Catch::Matchers::ContainsSubstring("ColorPaletteEntry99"));
        UNSCOPED_INFO("scrub drops the rest");
        CHECK_THAT(written(true), !Catch::Matchers::ContainsSubstring("T4A"));

        LLXMLNodePtr round_trip;
        REQUIRE(LLXMLNode::parseBuffer(text.data(), text.size(), round_trip));
        load({ "<color name=\"T4A\" value=\"1 0 0 1\"/>\n" });
        UNSCOPED_INFO("a reload without the user document keeps the runtime colour");
        CHECK(a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        std::vector<LLUIColorTable::document_t> documents = { { parse("<color name=\"T4A\" value=\"1 0 0 1\"/>\n"), "skin0.xml" } };
        table.load(documents, round_trip, "user.xml");
        UNSCOPED_INFO("the written document reproduces it");
        CHECK(a.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
        const auto user = table.getUserColors().find("T4A");
        REQUIRE(user != table.getUserColors().end());
        CHECK(user->second.get() == LLColor4(0.5f, 0.25f, 0.f, 1.f));
    }

    // What the skins' files have to say reaches the loaded sheet, and a
    // file that is not a colours file is refused without losing the rest.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: a skin file's faults are recorded and a wrong file is refused", "[lluicolortable]")
    {
        std::vector<LLUIColorTable::document_t> documents;
        documents.emplace_back(parse("<color name=\"T5A\" value=\"1 0 0 1\"/>\n"
                                     "<color name=\"T5Bad\" value=\"Nope\"/>\n"), "good.xml");
        LLXMLNodePtr wrong;
        const std::string xml = "<colours/>";
        LLXMLNode::parseBuffer(xml.data(), xml.size(), wrong);
        documents.emplace_back(wrong, "wrong.xml");
        LLUIColorTable& table = LLUIColorTable::instance();

        UNSCOPED_INFO("one good file is a load");
        CHECK(table.load(documents, nullptr, "user.xml"));
        CHECK(is("T5A", 1, 0, 0));
        UNSCOPED_INFO("T5Bad's value is on record");
        CHECK(table.getLoadedSheet().diagnostics().size() == 1u);
        UNSCOPED_INFO("the wrong file was not read");
        CHECK(table.getLoadedSheet().files().size() == 1u);

        documents.erase(documents.begin());
        UNSCOPED_INFO("no good file is not a load");
        CHECK_FALSE(table.load(documents, nullptr, "user.xml"));
    }

    // Putting a colour back removes the user's entry rather than copying
    // the skin's value into it, and every handle taken before or during
    // the edit reads what is in force; a colour only the user made goes
    // away with it, its handles reading magenta until it is set again.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: resetToDefault removes the user's entry and every handle follows", "[lluicolortable]")
    {
        CHECK(load({ "<color name=\"T6Base\" value=\"1 0 0 1\"/>" }));
        auto& table = LLUIColorTable::instance();
        const LLUIColor before = table.getColor("T6Base");
        table.setColor("T6Base", LLColor4::blue);
        const LLUIColor during = table.getColor("T6Base");
        table.resetToDefault("T6Base");
        CHECK_FALSE(has(table.getUserColors(), "T6Base"));
        CHECK(before.get() == LLColor4::red);
        CHECK(during.get() == LLColor4::red);
        table.setColor("T6Base", LLColor4::green);
        CHECK(before.get() == LLColor4::green);
        CHECK(during.get() == LLColor4::green);
        table.resetToDefault("T6Base");
        table.setColor("T6New", LLColor4::blue);
        const LLUIColor added = table.getColor("T6New");
        table.resetToDefault("T6New");
        CHECK_FALSE(has(table.getUserColors(), "T6New"));
        UNSCOPED_INFO("the removed colour's handle stays valid");
        CHECK(added.get() == LLColor4::magenta);
        CHECK_FALSE(table.colorExists("T6New"));
        CHECK(table.getColor("T6New", LLColor4::yellow).get() == LLColor4::yellow);
        CHECK_THAT(written(), !Catch::Matchers::ContainsSubstring("T6New"));
        table.setColor("T6New", LLColor4::green);
        UNSCOPED_INFO("set again, the old handle reads it");
        CHECK(added.get() == LLColor4::green);
    }

    // A colour the skins declare as a reference follows its target when the
    // user sets the target, at runtime and from the user's document, and
    // goes back with it; a colour the user set for itself does not.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: a skin reference follows its target as the user sets and resets it", "[lluicolortable]")
    {
        CHECK(load({ "<color name=\"T7Accent\" value=\"1 0 0 1\"/>\n"
                               "<color name=\"T7Link\" reference=\"T7Accent\"/>\n"
                               "<color name=\"T7Chain\" reference=\"T7Link\"/>\n"
                               "<color name=\"T7Own\" reference=\"T7Accent\"/>\n" }));
        auto& table = LLUIColorTable::instance();
        const LLUIColor link = table.getColor("T7Link");
        const LLUIColor chain = table.getColor("T7Chain");

        table.setColor("T7Own", LLColor4::green);
        table.setColor("T7Accent", LLColor4::blue);
        CHECK(link.get() == LLColor4::blue);
        CHECK(chain.get() == LLColor4::blue);
        UNSCOPED_INFO("a colour the user set keeps it");
        CHECK(is("T7Own", 0, 1, 0));
        CHECK(table.isDefault("T7Link"));
        CHECK(table.isDefault("T7Chain"));
        UNSCOPED_INFO("references are not written as the user's");
        CHECK_THAT(written(), !Catch::Matchers::ContainsSubstring("T7Link"));

        table.setColor("T7Link", LLColor4::yellow);
        UNSCOPED_INFO("a chain takes the nearest colour the user set");
        CHECK(chain.get() == LLColor4::yellow);
        table.resetToDefault("T7Link");
        UNSCOPED_INFO("and follows the accent again once that is reset");
        CHECK(chain.get() == LLColor4::blue);
        table.resetToDefault("T7Accent");
        CHECK(link.get() == LLColor4::red);
        CHECK(chain.get() == LLColor4::red);

        CHECK(load({ "<color name=\"T7Accent\" value=\"1 0 0 1\"/>\n"
                                                   "<color name=\"T7Link\" reference=\"T7Accent\"/>\n" },
                                                 "<color name=\"T7Accent\" value=\"0 0 1 1\"/>\n"));
        UNSCOPED_INFO("a reference follows the user's saved accent");
        CHECK(is("T7Link", 0, 0, 1));
    }

    // The generation moves with every change and not otherwise.
    TEST_CASE_METHOD(lluicolortable_data, "lluicolortable: the generation moves with every change and not otherwise", "[lluicolortable]")
    {
        CHECK(load({ "<color name=\"T8Base\" value=\"1 0 0 1\"/>" }));
        auto& table = LLUIColorTable::instance();
        const U32 loaded = table.generation();
        table.resetToDefault("T8Base");
        UNSCOPED_INFO("putting back what was never set moves nothing");
        CHECK(table.generation() == loaded);
        table.setColor("T8Base", LLColor4::blue);
        CHECK(table.generation() > loaded);
        const U32 set = table.generation();
        table.resetToDefault("T8Base");
        CHECK(table.generation() > set);
    }
}

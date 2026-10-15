/**
 * @file alxuioverlay_test.cpp
 * @brief Which layers wrote a value, and which of them is the one in force.
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

#include "alxuioverlay.h"

#include "alxmllayermerge.h"
#include "llxmlnode.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct alxuioverlay_data
    {
        // An attribute node, the way the merge holds one.
        static LLXMLNodePtr attribute(const std::string& name, const std::string& value, S32 line)
        {
            LLXMLNodePtr node = new LLXMLNode(name.c_str(), true);
            node->setValue(value);
            node->setLineNumber(line);
            return node;
        }
    };

    // A value nobody wrote over has no writers: the base wrote it, and the
    // base is not a layer that overrode anything.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: a value nobody wrote over has no writers and no origin", "[alxuioverlay]")
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        CHECK(overlay.writersOf(base.get()).empty());
        CHECK(overlay.originOf(base.get()) == nullptr);
    }

    // Every layer that wrote it is kept, in the order they were applied,
    // with what each of them said.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: every layer that wrote a value is kept in order with what it said", "[alxuioverlay]")
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const std::vector<ALXUIOverlay::Origin>& writers = overlay.writersOf(base.get());
        REQUIRE((S32)writers.size() == 2);
        UNSCOPED_INFO("the first is the one applied first");
        CHECK(writers[0].layer == 1);
        CHECK(writers[0].value == std::string("120"));
        CHECK(writers[0].line == 7);
        UNSCOPED_INFO("the second is the later one");
        CHECK(writers[1].layer == 2);
        CHECK(writers[1].value == std::string("160"));
    }

    // The one in force is the last to have written it, which is what the
    // rest of the tool has always asked this for.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: the origin in force is the last layer to have written the value", "[alxuioverlay]")
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const ALXUIOverlay::Origin* winner = overlay.originOf(base.get());
        REQUIRE(winner != nullptr);
        UNSCOPED_INFO("the last one did");
        CHECK(winner->layer == 2);
        UNSCOPED_INFO("and the line is that layer's");
        CHECK(winner->line == 9);
    }

    // Two attributes of the same element are two questions.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: two attributes of one element keep separate writers", "[alxuioverlay]")
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr width = attribute("width", "100", 4);
        LLXMLNodePtr label = attribute("label", "Go", 5);
        LLXMLNodePtr overlaid = attribute("label", "Los", 11);

        overlay.attributeApplied(1, label.get(), overlaid.get());

        UNSCOPED_INFO("the one written over has a writer");
        CHECK((S32)overlay.writersOf(label.get()).size() == 1);
        UNSCOPED_INFO("the one beside it has none");
        CHECK(overlay.writersOf(width.get()).empty());
    }

    // Clearing forgets all of it, which is what a fresh build of the file
    // wants.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: clearing forgets every writer", "[alxuioverlay]")
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.clear();
        CHECK(overlay.writersOf(base.get()).empty());
    }

    // Through a merge: every decision that applied nothing arrives as a
    // drop naming its sentence and the names in it, and a value applied
    // where the base moved the element arrives as a rescue saying from
    // where to where.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: a merge reports its drops by sentence and its rescue from where to where", "[alxuioverlay]")
    {
        const std::string base_xml =
            "<floater name=\"f\" title=\"Title\">\n"
            "  <panel name=\"stack\">\n"
            "    <text name=\"note\">Base note</text>\n"
            "    <button name=\"ok\" label=\"OK\"/>\n"
            "  </panel>\n"
            "</floater>\n";
        const std::string overlay_xml =
            "<floater name=\"f\" title=\"Titel\" extra=\"x\">\n"
            "  <text name=\"note\">Notiz</text>\n"
            "  <text name=\"gone\">Weg</text>\n"
            "  <text>Ohne Namen</text>\n"
            "</floater>\n";
        LLXMLNodePtr base;
        LLXMLNodePtr layer;
        REQUIRE(LLXMLNode::parseBuffer(base_xml.data(), base_xml.size(), base));
        REQUIRE(LLXMLNode::parseBuffer(overlay_xml.data(), overlay_xml.size(), layer));

        ALXUIOverlay overlay;
        overlay.layerParsed(0, "base.xml");
        overlay.layerParsed(1, "de/base.xml");
        ALXmlLayerMerge::merge(base, layer, 1, &overlay);

        REQUIRE(overlay.rescues().size() == 1u);
        const ALXUIOverlay::Rescue& r = overlay.rescues().front();
        UNSCOPED_INFO("from where the layer wrote it");
        CHECK(r.from == std::string("note"));
        UNSCOPED_INFO("to where the base has it");
        CHECK(r.to == std::string("stack/note"));
        UNSCOPED_INFO("in the layer");
        CHECK(r.layer == 1);
        UNSCOPED_INFO("on the line it was written");
        CHECK(r.line == 2);

        UNSCOPED_INFO("three drops: the attribute, the name nowhere, the unnamed");
        CHECK(overlay.drops().size() == 3u);
        const auto dropped = [&overlay](const char* key) -> const ALXUIOverlay::Drop*
        {
            for (const ALXUIOverlay::Drop& d : overlay.drops())
            {
                if (d.key == key)
                {
                    return &d;
                }
            }
            return nullptr;
        };
        const ALXUIOverlay::Drop* attribute = dropped("LintDropAttribute");
        UNSCOPED_INFO("the attribute the base lacks");
        REQUIRE(attribute != nullptr);
        CHECK(attribute->what == std::string("extra"));
        UNSCOPED_INFO("at the root");
        CHECK(attribute->path == std::string());
        const ALXUIOverlay::Drop* nowhere = dropped("LintDropNotBelow");
        UNSCOPED_INFO("the name the base has nowhere");
        REQUIRE(nowhere != nullptr);
        UNSCOPED_INFO("names the element");
        CHECK(nowhere->what == std::string("<text>"));
        UNSCOPED_INFO("and the parent it was looked for under");
        CHECK(nowhere->args.at("[PARENT]")() == std::string("f"));
        UNSCOPED_INFO("on its line");
        CHECK(nowhere->line == 3);
        const ALXUIOverlay::Drop* unnamed = dropped("LintDropUnnamed");
        UNSCOPED_INFO("the one with no name");
        REQUIRE(unnamed != nullptr);
        UNSCOPED_INFO("on its line");
        CHECK(unnamed->line == 4);
        UNSCOPED_INFO("and the layer's file is known by its index");
        CHECK(overlay.layerPath(1) == std::string("de/base.xml"));
    }

    // An element with no text where the base has some is an empty text
    // only when it says nothing else: one written for an attribute of its
    // own said what it came to say, and one that is a bare name did not.
    TEST_CASE_METHOD(alxuioverlay_data, "alxuioverlay: an element with no text is an empty text only if it says nothing else", "[alxuioverlay]")
    {
        const std::string base_xml =
            "<panel name=\"p\">\n"
            "  <text name=\"a\" font=\"Sans\">Base a</text>\n"
            "  <text name=\"b\">Base b</text>\n"
            "</panel>\n";
        const std::string overlay_xml =
            "<panel name=\"p\">\n"
            "  <text name=\"a\" font=\"Serif\"/>\n"
            "  <text name=\"b\"/>\n"
            "</panel>\n";
        LLXMLNodePtr base;
        LLXMLNodePtr layer;
        REQUIRE(LLXMLNode::parseBuffer(base_xml.data(), base_xml.size(), base));
        REQUIRE(LLXMLNode::parseBuffer(overlay_xml.data(), overlay_xml.size(), layer));
        ALXUIOverlay overlay;
        ALXmlLayerMerge::merge(base, layer, 1, &overlay);
        REQUIRE(overlay.drops().size() == 1u);
        UNSCOPED_INFO("the bare name");
        CHECK(overlay.drops().front().key == std::string("LintDropTextEmpty"));
        UNSCOPED_INFO("on its line");
        CHECK(overlay.drops().front().line == 3);
    }
}

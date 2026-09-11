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

#include "../alxuioverlay.h"

#include "alxmllayermerge.h"
#include "llxmlnode.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gOverlayTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gOverlayTestAnonName;
}

namespace tut
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

    typedef test_group<alxuioverlay_data> alxuioverlay_test;
    typedef alxuioverlay_test::object     alxuioverlay_object;
    tut::alxuioverlay_test alxuioverlay_testgroup("alxuioverlay");

    // A value nobody wrote over has no writers: the base wrote it, and the
    // base is not a layer that overrode anything.
    template<> template<>
    void alxuioverlay_object::test<1>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        ensure("nothing wrote over it", overlay.writersOf(base.get()).empty());
        ensure("so nothing is its origin", overlay.originOf(base.get()) == nullptr);
    }

    // Every layer that wrote it is kept, in the order they were applied,
    // with what each of them said.
    template<> template<>
    void alxuioverlay_object::test<2>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const std::vector<ALXUIOverlay::Origin>& writers = overlay.writersOf(base.get());
        ensure_equals("both layers are kept", (S32)writers.size(), 2);
        ensure_equals("the first is the one applied first", writers[0].layer, 1);
        ensure_equals("with what it said", writers[0].value, std::string("120"));
        ensure_equals("and the line it said it on", writers[0].line, 7);
        ensure_equals("the second is the later one", writers[1].layer, 2);
        ensure_equals("with what it said", writers[1].value, std::string("160"));
    }

    // The one in force is the last to have written it, which is what the
    // rest of the tool has always asked this for.
    template<> template<>
    void alxuioverlay_object::test<3>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const ALXUIOverlay::Origin* winner = overlay.originOf(base.get());
        ensure("something wrote it", winner != nullptr);
        ensure_equals("the last one did", winner->layer, 2);
        ensure_equals("and the line is that layer's", winner->line, 9);
    }

    // Two attributes of the same element are two questions.
    template<> template<>
    void alxuioverlay_object::test<4>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr width = attribute("width", "100", 4);
        LLXMLNodePtr label = attribute("label", "Go", 5);
        LLXMLNodePtr overlaid = attribute("label", "Los", 11);

        overlay.attributeApplied(1, label.get(), overlaid.get());

        ensure_equals("the one written over has a writer", (S32)overlay.writersOf(label.get()).size(), 1);
        ensure("the one beside it has none", overlay.writersOf(width.get()).empty());
    }

    // Clearing forgets all of it, which is what a fresh build of the file
    // wants.
    template<> template<>
    void alxuioverlay_object::test<5>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.clear();
        ensure("nothing is remembered", overlay.writersOf(base.get()).empty());
    }

    // Through a merge: every decision that applied nothing arrives as a
    // drop naming its sentence and the names in it, and a value applied
    // where the base moved the element arrives as a rescue saying from
    // where to where.
    template<> template<>
    void alxuioverlay_object::test<6>()
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
        ensure("the base parses", LLXMLNode::parseBuffer(base_xml.data(), base_xml.size(), base));
        ensure("the layer parses", LLXMLNode::parseBuffer(overlay_xml.data(), overlay_xml.size(), layer));

        ALXUIOverlay overlay;
        overlay.layerParsed(0, "base.xml");
        overlay.layerParsed(1, "de/base.xml");
        ALXmlLayerMerge::merge(base, layer, 1, &overlay);

        ensure_equals("one rescue", overlay.rescues().size(), 1u);
        const ALXUIOverlay::Rescue& r = overlay.rescues().front();
        ensure_equals("from where the layer wrote it", r.from, std::string("note"));
        ensure_equals("to where the base has it", r.to, std::string("stack/note"));
        ensure_equals("in the layer", r.layer, 1);
        ensure_equals("on the line it was written", r.line, 2);

        ensure_equals("three drops: the attribute, the name nowhere, the unnamed", overlay.drops().size(), 3u);
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
        ensure("the attribute the base lacks", attribute != nullptr);
        ensure_equals("named", attribute->what, std::string("extra"));
        ensure_equals("at the root", attribute->path, std::string());
        const ALXUIOverlay::Drop* nowhere = dropped("LintDropNotBelow");
        ensure("the name the base has nowhere", nowhere != nullptr);
        ensure_equals("names the element", nowhere->what, std::string("<text>"));
        ensure_equals("and the parent it was looked for under", nowhere->args.at("[PARENT]")(), std::string("f"));
        ensure_equals("on its line", nowhere->line, 3);
        const ALXUIOverlay::Drop* unnamed = dropped("LintDropUnnamed");
        ensure("the one with no name", unnamed != nullptr);
        ensure_equals("on its line", unnamed->line, 4);
        ensure_equals("and the layer's file is known by its index", overlay.layerPath(1), std::string("de/base.xml"));
    }

    // An element with no text where the base has some is an empty text
    // only when it says nothing else: one written for an attribute of its
    // own said what it came to say, and one that is a bare name did not.
    template<> template<>
    void alxuioverlay_object::test<7>()
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
        ensure("parses", LLXMLNode::parseBuffer(base_xml.data(), base_xml.size(), base)
                      && LLXMLNode::parseBuffer(overlay_xml.data(), overlay_xml.size(), layer));
        ALXUIOverlay overlay;
        ALXmlLayerMerge::merge(base, layer, 1, &overlay);
        ensure_equals("one drop", overlay.drops().size(), 1u);
        ensure_equals("the bare name", overlay.drops().front().key, std::string("LintDropTextEmpty"));
        ensure_equals("on its line", overlay.drops().front().line, 3);
    }
}

/**
 * @file llxmlnode_test.cpp
 * @brief LLXMLNode unit tests
 *
 * $LicenseInfo:firstyear=2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "../test/lltut.h"

#include "../llxmlnode.h"

#include <cmath>
#include <sstream>

namespace tut
{
    struct llxmlnode_data
    {
    };
    typedef test_group<llxmlnode_data> llxmlnode_test;
    typedef llxmlnode_test::object llxmlnode_object;
    tut::llxmlnode_test llxmlnode_testcase("llxmlnode");

    // Element name, id, attributes (string + int) and text value round-trip.
    template<> template<>
    void llxmlnode_object::test<1>()
    {
        const std::string xml =
            "<thing id=\"abc\" name=\"hello world\" count=\"42\">body text</thing>";
        LLXMLNodePtr node;
        ensure("parseBuffer succeeds",
               LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure("node non-null", node.notNull());
        ensure("node name", node->hasName("thing"));
        ensure_equals("id attribute", node->getID(), std::string("abc"));
        ensure_equals("text value", node->getValue(), std::string("body text"));

        std::string name;
        ensure("name attr present", node->getAttributeString("name", name));
        ensure_equals("name attr value", name, std::string("hello world"));

        S32 count = 0;
        ensure("count attr present", node->getAttributeS32("count", count));
        ensure_equals("count attr value", count, 42);
    }

    // Escaped-string handling: a value wholly wrapped in double quotes is
    // unescaped (\" -> ", \\ -> \) since sStripEscapedStrings defaults to true.
    template<> template<>
    void llxmlnode_object::test<2>()
    {
        const std::string xml = "<v>\"abc\\\"def\"</v>"; // raw: <v>"abc\"def"</v>
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("unescaped value", node->getValue(), std::string("abc\"def"));
    }

    // A long body taken through parseStream rather than parseBuffer. Guards the
    // stream path and value accumulation (the append path).
    template<> template<>
    void llxmlnode_object::test<3>()
    {
        const size_t N = 5000;
        std::string xml = "<big>";
        xml.append(N, 'x');
        xml += "</big>";
        std::istringstream stream(xml);
        LLXMLNodePtr node;
        ensure("parseStream", LLXMLNode::parseStream(stream, node, nullptr));
        ensure_equals("accumulated length", node->getValue().size(), N);
        ensure("all content preserved", node->getValue() == std::string(N, 'x'));
    }

    // Typed float array parsing via a child element.
    template<> template<>
    void llxmlnode_object::test<4>()
    {
        const std::string xml =
            "<root><pos type=\"float\">1.5 -2.25 3.0</pos></root>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));

        LLXMLNodePtr pos;
        ensure("getChild pos", node->getChild("pos", pos));
        F32 v[3] = { 0.f, 0.f, 0.f };
        ensure_equals("3 floats parsed", (S32)pos->getFloatValue(3, v), 3);
        ensure("v0", std::fabs(v[0] - 1.5f) < 1e-5f);
        ensure("v1", std::fabs(v[1] + 2.25f) < 1e-5f);
        ensure("v2", std::fabs(v[2] - 3.0f) < 1e-5f);
    }

    // Boolean array serialization (guards the whitespace-joined append path).
    template<> template<>
    void llxmlnode_object::test<5>()
    {
        LLXMLNodePtr node = new LLXMLNode("flags", false);
        const bool vals[3] = { true, false, true };
        node->setBoolValue(3, vals);
        ensure_equals("bool serialization", node->getValue(),
                      std::string("true false true"));

        bool out[3] = { false, false, false };
        ensure_equals("read back count", (S32)node->getBoolValue(3, out), 3);
        ensure("b0", out[0] == true);
        ensure("b1", out[1] == false);
        ensure("b2", out[2] == true);
    }

    // XML escaping of the five special characters.
    template<> template<>
    void llxmlnode_object::test<6>()
    {
        ensure_equals("escapeXML",
                      LLXMLNode::escapeXML("a<b>&\"'c"),
                      std::string("a&lt;b&gt;&amp;&quot;&apos;c"));
    }

    // deepCopy uses the copy constructor; verify fields survive the copy.
    template<> template<>
    void llxmlnode_object::test<7>()
    {
        const std::string xml = "<a id=\"i1\" k=\"v\">text</a>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));

        LLXMLNodePtr copy = node->deepCopy();
        ensure("copy non-null", copy.notNull());
        ensure("copy name", copy->hasName("a"));
        ensure_equals("copy id", copy->getID(), std::string("i1"));
        ensure_equals("copy value", copy->getValue(), std::string("text"));

        std::string k;
        ensure("copy attr present", copy->getAttributeString("k", k));
        ensure_equals("copy attr value", k, std::string("v"));
    }

    // A quotation inside a longer run keeps its quotes: the escaped-string
    // strip applies to a value that is wholly quoted, and this one is not.
    template<> template<>
    void llxmlnode_object::test<8>()
    {
        const std::string xml = "<v>he said &quot;hello&quot; loudly</v>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("quotes preserved", node->getValue(),
                      std::string("he said \"hello\" loudly"));

        std::istringstream stream(xml);
        LLXMLNodePtr snode;
        ensure("parseStream", LLXMLNode::parseStream(stream, snode, nullptr));
        ensure_equals("stream agrees", snode->getValue(), node->getValue());
    }

    // The same run with nothing either side is wholly quoted, so it is stripped.
    template<> template<>
    void llxmlnode_object::test<9>()
    {
        const std::string xml = "<v>&quot;hello&quot;</v>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("outer quotes stripped", node->getValue(), std::string("hello"));
    }

    // CDATA reaches the value verbatim, entities and all.
    template<> template<>
    void llxmlnode_object::test<10>()
    {
        const std::string xml = "<v><![CDATA[raw <x> &amp; y]]></v>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("cdata verbatim", node->getValue(), std::string("raw <x> &amp; y"));
    }

    // Text either side of a child element concatenates, including the run
    // between two children that is nothing but whitespace.
    template<> template<>
    void llxmlnode_object::test<11>()
    {
        const std::string xml = "<a>x<b/>   <c/>y</a>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("interior whitespace kept", node->getValue(), std::string("x   y"));
    }

    // Line numbers are reported for elements and for their attributes.
    template<> template<>
    void llxmlnode_object::test<12>()
    {
        const std::string xml = "<a>\n  <b k=\"v\"/>\n  <c/>\n</a>";
        LLXMLNodePtr node;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        ensure_equals("root line", node->getLineNumber(), 1);

        LLXMLNodePtr b;
        ensure("getChild b", node->getChild("b", b));
        ensure_equals("b line", b->getLineNumber(), 2);

        LLXMLNodePtr attr;
        ensure("attribute node", b->getAttribute("k", attr));
        ensure_equals("attribute line", attr->getLineNumber(), 2);

        LLXMLNodePtr c;
        ensure("getChild c", node->getChild("c", c));
        ensure_equals("c line", c->getLineNumber(), 3);
    }
}

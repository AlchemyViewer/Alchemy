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
#include <string>
#include <vector>

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

    // Siblings that share a name. The child map is a multimap keyed by the
    // name, and multimap::find promises only some entry with the key: since
    // libc++ 22 it is whichever the tree descent reaches rather than the
    // first, so anything that walked forward from find() could start past
    // the child it wanted. Every same-named child must be deletable whatever
    // its place in the run, getChild must answer the first in document
    // order, and getChildren all of them. Eight children make the tree deep
    // enough that a descent rarely lands on the first.
    template<> template<>
    void llxmlnode_object::test<13>()
    {
        const std::string xml =
            "<root>"
            "<s n=\"0\"/><s n=\"1\"/><s n=\"2\"/><s n=\"3\"/>"
            "<s n=\"4\"/><s n=\"5\"/><s n=\"6\"/><s n=\"7\"/>"
            "<other/>"
            "</root>";
        LLXMLNodePtr root;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), root, nullptr));
        ensure_equals("nine children", root->getChildCount(), 9u);

        LLXMLNodePtr first;
        ensure("getChild finds one", root->getChild("s", first));
        std::string n;
        ensure("it has n", first->getAttributeString("n", n));
        ensure_equals("and is the first in the file", n, std::string("0"));

        LLXMLNodeList all;
        root->getChildren("s", all);
        ensure_equals("getChildren finds them all", all.size(), 8u);

        // Delete in document order: each deletion is of the front of the
        // run, which is where a descent is least likely to land.
        for (int expected = 0; expected < 8; ++expected)
        {
            LLXMLNodePtr child;
            ensure("still one to find", root->getChild("s", child));
            ensure("attribute", child->getAttributeString("n", n));
            ensure_equals("in document order", n, std::to_string(expected));
            ensure("deleted", root->deleteChild(child));
            ensure("and orphaned", child->mParent == nullptr);
            ensure_equals("count follows", root->getChildCount(), (U32)(8 - expected));

            // The sibling list agrees with the map about what is left.
            U32 listed = 0;
            for (LLXMLNodePtr c = root->getFirstChild(); c.notNull(); c = c->getNextSibling())
            {
                ++listed;
            }
            ensure_equals("sibling list agrees", listed, root->getChildCount());
        }

        LLXMLNodePtr none;
        ensure("no s is left", !root->getChild("s", none));
        LLXMLNodePtr other;
        ensure("the other child remains", root->getChild("other", other));
    }

    // The same run deleted from the back and from the middle, so the
    // target is never at the front of the equal range either.
    template<> template<>
    void llxmlnode_object::test<14>()
    {
        const std::string xml =
            "<root>"
            "<s n=\"0\"/><s n=\"1\"/><s n=\"2\"/><s n=\"3\"/>"
            "<s n=\"4\"/><s n=\"5\"/><s n=\"6\"/><s n=\"7\"/>"
            "</root>";
        LLXMLNodePtr root;
        ensure("parse", LLXMLNode::parseBuffer(xml.data(), xml.size(), root, nullptr));

        std::vector<LLXMLNodePtr> children;
        for (LLXMLNodePtr c = root->getFirstChild(); c.notNull(); c = c->getNextSibling())
        {
            children.push_back(c);
        }
        ensure_equals("eight in the list", children.size(), 8u);

        for (size_t index : { 7u, 3u, 0u, 5u, 1u, 6u, 2u, 4u })
        {
            ensure("deleted from wherever it sits", root->deleteChild(children[index]));
        }
        ensure_equals("none left", root->getChildCount(), 0u);
        ensure("and the list is empty", root->getFirstChild().isNull());
    }
}

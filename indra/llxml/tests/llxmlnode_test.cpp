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
#include <catch2/catch_test_macros.hpp>

#include "../llxmlnode.h"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // Element name, id, attributes (string + int) and text value round-trip.
    TEST_CASE("llxmlnode: parseBuffer gives the element name and id and attributes and text", "[llxmlnode]")
    {
        const std::string xml =
            "<thing id=\"abc\" name=\"hello world\" count=\"42\">body text</thing>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        REQUIRE(node.notNull());
        CHECK(node->hasName("thing"));
        CHECK(node->getID() == std::string("abc"));
        CHECK(node->getValue() == std::string("body text"));

        std::string name;
        REQUIRE(node->getAttributeString("name", name));
        CHECK(name == std::string("hello world"));

        S32 count = 0;
        CHECK(node->getAttributeS32("count", count));
        CHECK(count == 42);
    }

    // Escaped-string handling: a value wholly wrapped in double quotes is
    // unescaped (\" -> ", \\ -> \) since sStripEscapedStrings defaults to true.
    TEST_CASE("llxmlnode: a value wholly in double quotes is unescaped", "[llxmlnode]")
    {
        const std::string xml = "<v>\"abc\\\"def\"</v>"; // raw: <v>"abc\"def"</v>
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        CHECK(node->getValue() == std::string("abc\"def"));
    }

    // A long body taken through parseStream rather than parseBuffer. Guards the
    // stream path and value accumulation (the append path).
    TEST_CASE("llxmlnode: parseStream keeps the whole of a long body", "[llxmlnode]")
    {
        const size_t N = 5000;
        std::string xml = "<big>";
        xml.append(N, 'x');
        xml += "</big>";
        std::istringstream stream(xml);
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseStream(stream, node, nullptr));
        UNSCOPED_INFO("accumulated length");
        REQUIRE(node->getValue().size() == N);
        CHECK(node->getValue() == std::string(N, 'x'));
    }

    // Typed float array parsing via a child element.
    TEST_CASE("llxmlnode: getFloatValue parses a typed float array in a child element", "[llxmlnode]")
    {
        const std::string xml =
            "<root><pos type=\"float\">1.5 -2.25 3.0</pos></root>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));

        LLXMLNodePtr pos;
        REQUIRE(node->getChild("pos", pos));
        F32 v[3] = { 0.f, 0.f, 0.f };
        CHECK((S32)pos->getFloatValue(3, v) == 3);
        CHECK(std::fabs(v[0] - 1.5f) < 1e-5f);
        CHECK(std::fabs(v[1] + 2.25f) < 1e-5f);
        CHECK(std::fabs(v[2] - 3.0f) < 1e-5f);
    }

    // Boolean array serialization (guards the whitespace-joined append path).
    TEST_CASE("llxmlnode: setBoolValue writes a space-joined array getBoolValue reads", "[llxmlnode]")
    {
        LLXMLNodePtr node = new LLXMLNode("flags", false);
        const bool vals[3] = { true, false, true };
        node->setBoolValue(3, vals);
        CHECK(node->getValue() == std::string("true false true"));

        bool out[3] = { false, false, false };
        CHECK((S32)node->getBoolValue(3, out) == 3);
        CHECK(out[0]);
        CHECK_FALSE(out[1]);
        CHECK(out[2]);
    }

    TEST_CASE("llxmlnode: escapeXML escapes the five special characters", "[llxmlnode]")
    {
        CHECK(LLXMLNode::escapeXML("a<b>&\"'c") == std::string("a&lt;b&gt;&amp;&quot;&apos;c"));
    }

    // deepCopy uses the copy constructor; verify fields survive the copy.
    TEST_CASE("llxmlnode: deepCopy keeps the name and id and value and attributes", "[llxmlnode]")
    {
        const std::string xml = "<a id=\"i1\" k=\"v\">text</a>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));

        LLXMLNodePtr copy = node->deepCopy();
        REQUIRE(copy.notNull());
        CHECK(copy->hasName("a"));
        CHECK(copy->getID() == std::string("i1"));
        CHECK(copy->getValue() == std::string("text"));

        std::string k;
        REQUIRE(copy->getAttributeString("k", k));
        CHECK(k == std::string("v"));
    }

    // A quotation inside a longer run keeps its quotes: the escaped-string
    // strip applies to a value that is wholly quoted, and this one is not.
    TEST_CASE("llxmlnode: a quotation inside a longer run keeps its quotes", "[llxmlnode]")
    {
        const std::string xml = "<v>he said &quot;hello&quot; loudly</v>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        CHECK(node->getValue() == std::string("he said \"hello\" loudly"));

        std::istringstream stream(xml);
        LLXMLNodePtr snode;
        REQUIRE(LLXMLNode::parseStream(stream, snode, nullptr));
        UNSCOPED_INFO("parseStream agrees with parseBuffer");
        CHECK(snode->getValue() == node->getValue());
    }

    // The same run with nothing either side is wholly quoted, so it is stripped.
    TEST_CASE("llxmlnode: the same quotation with nothing either side is stripped", "[llxmlnode]")
    {
        const std::string xml = "<v>&quot;hello&quot;</v>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        CHECK(node->getValue() == std::string("hello"));
    }

    // CDATA reaches the value verbatim, entities and all.
    TEST_CASE("llxmlnode: CDATA reaches the value verbatim with its entities", "[llxmlnode]")
    {
        const std::string xml = "<v><![CDATA[raw <x> &amp; y]]></v>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        CHECK(node->getValue() == std::string("raw <x> &amp; y"));
    }

    // Text either side of a child element concatenates, including the run
    // between two children that is nothing but whitespace.
    TEST_CASE("llxmlnode: text either side of child elements concatenates", "[llxmlnode]")
    {
        const std::string xml = "<a>x<b/>   <c/>y</a>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        UNSCOPED_INFO("interior whitespace kept");
        CHECK(node->getValue() == std::string("x   y"));
    }

    TEST_CASE("llxmlnode: line numbers are reported for elements and their attributes", "[llxmlnode]")
    {
        const std::string xml = "<a>\n  <b k=\"v\"/>\n  <c/>\n</a>";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), node, nullptr));
        CHECK(node->getLineNumber() == 1);

        LLXMLNodePtr b;
        REQUIRE(node->getChild("b", b));
        CHECK(b->getLineNumber() == 2);

        LLXMLNodePtr attr;
        REQUIRE(b->getAttribute("k", attr));
        CHECK(attr->getLineNumber() == 2);

        LLXMLNodePtr c;
        REQUIRE(node->getChild("c", c));
        CHECK(c->getLineNumber() == 3);
    }

    // Siblings that share a name. The child map is a multimap keyed by the
    // name, and multimap::find promises only some entry with the key: since
    // libc++ 22 it is whichever the tree descent reaches rather than the
    // first, so anything that walked forward from find() could start past
    // the child it wanted. Every same-named child must be deletable whatever
    // its place in the run, getChild must answer the first in document
    // order, and getChildren all of them. Eight children make the tree deep
    // enough that a descent rarely lands on the first.
    TEST_CASE("llxmlnode: same-named siblings are found in order and each can be deleted", "[llxmlnode]")
    {
        const std::string xml =
            "<root>"
            "<s n=\"0\"/><s n=\"1\"/><s n=\"2\"/><s n=\"3\"/>"
            "<s n=\"4\"/><s n=\"5\"/><s n=\"6\"/><s n=\"7\"/>"
            "<other/>"
            "</root>";
        LLXMLNodePtr root;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), root, nullptr));
        CHECK(root->getChildCount() == 9u);

        LLXMLNodePtr first;
        REQUIRE(root->getChild("s", first));
        std::string n;
        REQUIRE(first->getAttributeString("n", n));
        UNSCOPED_INFO("getChild answers the first in the file");
        CHECK(n == std::string("0"));

        LLXMLNodeList all;
        root->getChildren("s", all);
        UNSCOPED_INFO("getChildren finds them all");
        CHECK(all.size() == 8u);

        // Delete in document order: each deletion is of the front of the
        // run, which is where a descent is least likely to land.
        for (int expected = 0; expected < 8; ++expected)
        {
            CAPTURE(expected);
            LLXMLNodePtr child;
            UNSCOPED_INFO("still one to find");
            REQUIRE(root->getChild("s", child));
            REQUIRE(child->getAttributeString("n", n));
            UNSCOPED_INFO("in document order");
            REQUIRE(n == std::to_string(expected));
            REQUIRE(root->deleteChild(child));
            UNSCOPED_INFO("and orphaned");
            REQUIRE(child->mParent == nullptr);
            REQUIRE(root->getChildCount() == (U32)(8 - expected));

            // The sibling list agrees with the map about what is left.
            U32 listed = 0;
            for (LLXMLNodePtr c = root->getFirstChild(); c.notNull(); c = c->getNextSibling())
            {
                ++listed;
            }
            UNSCOPED_INFO("sibling list agrees");
            REQUIRE(listed == root->getChildCount());
        }

        LLXMLNodePtr none;
        CHECK_FALSE(root->getChild("s", none));
        LLXMLNodePtr other;
        UNSCOPED_INFO("the other child remains");
        CHECK(root->getChild("other", other));
    }

    // The same run deleted from the back and from the middle, so the
    // target is never at the front of the equal range either.
    TEST_CASE("llxmlnode: same-named siblings delete from the back and the middle", "[llxmlnode]")
    {
        const std::string xml =
            "<root>"
            "<s n=\"0\"/><s n=\"1\"/><s n=\"2\"/><s n=\"3\"/>"
            "<s n=\"4\"/><s n=\"5\"/><s n=\"6\"/><s n=\"7\"/>"
            "</root>";
        LLXMLNodePtr root;
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), xml.size(), root, nullptr));

        std::vector<LLXMLNodePtr> children;
        for (LLXMLNodePtr c = root->getFirstChild(); c.notNull(); c = c->getNextSibling())
        {
            children.push_back(c);
        }
        REQUIRE(children.size() == 8u);

        for (size_t index : { 7u, 3u, 0u, 5u, 1u, 6u, 2u, 4u })
        {
            CAPTURE(index);
            UNSCOPED_INFO("deleted from wherever it sits");
            REQUIRE(root->deleteChild(children[index]));
        }
        CHECK(root->getChildCount() == 0u);
        UNSCOPED_INFO("and the list is empty");
        CHECK(root->getFirstChild().isNull());
    }
}

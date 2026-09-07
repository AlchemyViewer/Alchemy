/**
 * @file alxmllayermerge_test.cpp
 * @brief The merge tells its observer every decision, and decides the same with none.
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

#include "../alxmllayermerge.h"

#include "lldir.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <cstring>
#include <sstream>

namespace tut
{
    // Every event, as one line: the kind, the layer, and the element or
    // attribute it was about, so a run can be compared with what the
    // rules say should happen.
    struct RecordingObserver final : public ALXmlMergeObserver
    {
        std::vector<std::string> events;
        S32 matched = 0;
        S32 unmatched = 0;
        S32 applied = 0;
        S32 dropped = 0;
        S32 text = 0;
        S32 blanked = 0;
        S32 roots = 0;
        S32 refused = 0;
        S32 parsed = 0;
        S32 skipped = 0;
        S32 skippedLine = 0;

        static std::string nameOf(LLXMLNode* node)
        {
            std::string name;
            if (node->mIsAttribute)
            {
                return std::string(node->getName()->mString) + "=" + node->getValue();
            }
            node->getAttributeString("name", name);
            return std::string(node->getName()->mString) + "[" + name + "]";
        }

        void add(const char* kind, S32 layer, LLXMLNode* node)
        {
            events.push_back(std::string(kind) + " " + std::to_string(layer) + " " + nameOf(node));
        }

        void layerParsed(S32 layer, const std::string& path) override { ++parsed; }
        void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) override
        {
            ++skipped;
            skippedLine = line;
            events.push_back("skipped " + std::to_string(layer));
        }
        void rootMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++roots; add("root", layer, base); }
        void rootRefused(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++refused; add("refused", layer, overlay); }
        void childMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++matched; add("match", layer, base); }
        void childUnmatched(S32 layer, LLXMLNode* parent, LLXMLNode* overlay, Miss why) override
        {
            ++unmatched;
            add(why == Miss::Unnamed ? "unnamed" : "nosibling", layer, overlay);
        }
        void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++text; add("text", layer, base); }
        void textBlanked(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++blanked; add("blanked", layer, base); }
        void attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute) override
        {
            ++applied;
            add("attr", layer, overlay_attribute);
        }
        void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override
        {
            ++dropped;
            add("dropped", layer, overlay_attribute);
        }

        bool has(const std::string& event) const
        {
            return std::find(events.begin(), events.end(), event) != events.end();
        }
    };

    struct alxmllayermerge_data
    {
        // The base: a repeated name among siblings, an element with body
        // text, an unnamed child, and items matched by value.
        static constexpr const char* BASE =
            "<floater name=\"f\" title=\"Title\" width=\"100\">\n"
            "  <panel name=\"p\" width=\"10\">\n"
            "    <button name=\"b\" label=\"First\"/>\n"
            "    <button name=\"b\" label=\"Second\"/>\n"
            "    <text name=\"t\">Base text</text>\n"
            "    <text name=\"u\">Keep me</text>\n"
            "    <item/>\n"
            "    <combo_box name=\"c\"><item value=\"v1\" label=\"One\"/><item value=\"v2\" label=\"Two\"/></combo_box>\n"
            "  </panel>\n"
            "</floater>\n";

        // The overlay: a title, an attribute the base lacks, both repeated
        // names, text, a value= where the base has body text, a child the
        // base has nowhere, a child with no name, and one item by value.
        static constexpr const char* OVERLAY =
            "<floater name=\"f\" title=\"Titel\" extra=\"x\">\n"
            "  <panel name=\"p\">\n"
            "    <button name=\"b\" label=\"Erste\"/>\n"
            "    <button name=\"b\" label=\"Zweite\"/>\n"
            "    <text name=\"t\">Text</text>\n"
            "    <text name=\"u\" value=\"Wert\"/>\n"
            "    <text name=\"missing\">Nirgends</text>\n"
            "    <text>Ohne Namen</text>\n"
            "    <combo_box name=\"c\"><item value=\"v2\" label=\"Zwei\"/></combo_box>\n"
            "  </panel>\n"
            "</floater>\n";

        static LLXMLNodePtr parse(const char* xml)
        {
            LLXMLNodePtr node;
            LLXMLNode::parseBuffer(xml, std::strlen(xml), node, nullptr);
            return node;
        }

        static std::string serialize(LLXMLNodePtr node)
        {
            std::ostringstream out;
            node->writeToOstream(out);
            return out.str();
        }

        static std::string attr(LLXMLNodePtr node, const char* path, const char* name)
        {
            LLXMLNodePtr cur = node;
            for (const char* p = path; *p; )
            {
                const char* end = std::strchr(p, '/');
                const std::string step(p, end ? end - p : std::strlen(p));
                LLXMLNodePtr next;
                // The nth child of that name, as "name#n".
                std::string want = step;
                S32 ordinal = 0;
                if (const size_t hash = want.find('#'); hash != std::string::npos)
                {
                    ordinal = std::atoi(want.c_str() + hash + 1);
                    want = want.substr(0, hash);
                }
                S32 seen = 0;
                for (LLXMLNodePtr child = cur->getFirstChild(); child.notNull(); child = child->getNextSibling())
                {
                    std::string child_name;
                    child->getAttributeString("name", child_name);
                    std::string child_value;
                    child->getAttributeString("value", child_value);
                    if ((child_name == want || (child_name.empty() && child_value == want)) && seen++ == ordinal)
                    {
                        next = child;
                        break;
                    }
                }
                if (next.isNull())
                {
                    return "<no " + step + ">";
                }
                cur = next;
                p = end ? end + 1 : p + std::strlen(p);
            }
            if (std::strcmp(name, "text()") == 0)
            {
                return cur->getTextContents();
            }
            std::string value;
            return cur->getAttributeString(name, value) ? value : std::string("<none>");
        }

        std::string mDir;

        alxmllayermerge_data()
        {
            mDir = gDirUtilp->add(gDirUtilp->getTempDir(), "alxmllayermerge_test");
            gDirUtilp->deleteDirAndContents(mDir);
            LLFile::mkdir(mDir);
        }

        ~alxmllayermerge_data()
        {
            gDirUtilp->deleteDirAndContents(mDir);
        }

        std::string write(const char* name, const char* text)
        {
            const std::string path = gDirUtilp->add(mDir, name);
            llofstream out(path, std::ios::binary);
            out << text;
            return path;
        }
    };

    typedef test_group<alxmllayermerge_data> alxmllayermerge_test;
    typedef alxmllayermerge_test::object     alxmllayermerge_object;
    tut::alxmllayermerge_test alxmllayermerge_testgroup("alxmllayermerge");

    // The rules as shipped, one event per decision: repeated names in
    // order, text overwritten, an attribute the base lacks dropped, a
    // value= against body text dropped and the text blanked, and two
    // children that match nothing.
    template<> template<>
    void alxmllayermerge_object::test<1>()
    {
        LLXMLNodePtr base = parse(BASE);
        LLXMLNodePtr overlay = parse(OVERLAY);
        ensure("fixture parses", base.notNull() && overlay.notNull());

        RecordingObserver rec;
        ALXmlLayerMerge::merge(base, overlay, 1, &rec);

        ensure_equals("title", attr(base, "", "title"), std::string("Titel"));
        ensure_equals("the base's width is untouched", attr(base, "", "width"), std::string("100"));
        ensure_equals("first of a repeated name", attr(base, "p/b", "label"), std::string("Erste"));
        ensure_equals("second of a repeated name", attr(base, "p/b#1", "label"), std::string("Zweite"));
        ensure_equals("text", attr(base, "p/t", "text()"), std::string("Text"));
        ensure_equals("an item by value", attr(base, "p/c/v2", "label"), std::string("Zwei"));
        ensure_equals("the other item is untouched", attr(base, "p/c/v1", "label"), std::string("One"));
        ensure_equals("value= against body text is dropped", attr(base, "p/u", "value"), std::string("<none>"));
        ensure_equals("and the text is blanked", attr(base, "p/u", "text()"), std::string(""));

        ensure("the extra attribute was reported dropped", rec.has("dropped 1 extra=x"));
        ensure("the value= was reported dropped", rec.has("dropped 1 value=Wert"));
        ensure_equals("two attributes dropped", rec.dropped, 2);
        ensure("the blanked text was reported", rec.has("blanked 1 text[u]"));
        ensure_equals("one text blanked", rec.blanked, 1);
        ensure("the missing child was reported", rec.has("nosibling 1 text[missing]"));
        ensure("the unnamed child was reported", rec.has("unnamed 1 text[]"));
        ensure_equals("two children unmatched", rec.unmatched, 2);
        // p, b, b, t, u, c, and the item: seven matches.
        ensure_equals("matches", rec.matched, 7);
        ensure("the second button matched the second base button", rec.has("match 1 button[b]"));
        ensure_equals("texts applied: t", rec.text, 1);
        // name on f, p, b, b, t, u, c; title; label on b, b; value and label on the item.
        ensure_equals("attributes applied", rec.applied, 12);
    }

    // With no observer the tree comes out the same: a rule that changes
    // changes in one place.
    template<> template<>
    void alxmllayermerge_object::test<2>()
    {
        LLXMLNodePtr observed_base = parse(BASE);
        LLXMLNodePtr observed_overlay = parse(OVERLAY);
        RecordingObserver rec;
        ALXmlLayerMerge::merge(observed_base, observed_overlay, 1, &rec);

        LLXMLNodePtr silent_base = parse(BASE);
        LLXMLNodePtr silent_overlay = parse(OVERLAY);
        ALXmlLayerMerge::merge(silent_base, silent_overlay);

        ensure_equals("the same tree with and without an observer", serialize(silent_base), serialize(observed_base));
        ensure("and the observer saw something", !rec.events.empty());
    }

    // Loading: the base and each layer in turn; a layer whose root name
    // differs is refused whole; a layer that does not parse fails the
    // load, as it does today; an empty path is passed over.
    template<> template<>
    void alxmllayermerge_object::test<3>()
    {
        const std::string base = write("base.xml", BASE);
        const std::string overlay = write("overlay.xml", OVERLAY);
        const std::string other = write("other.xml", "<floater name=\"g\" title=\"Other\"/>\n");
        const std::string broken = write("broken.xml", "<floater name=\"f\">\n<panel>\n");

        {
            RecordingObserver rec;
            LLXMLNodePtr root;
            ensure("loads", ALXmlLayerMerge::load({ base, "", overlay, other }, root, &rec));
            ensure_equals("title from the overlay", attr(root, "", "title"), std::string("Titel"));
            ensure_equals("layers parsed: base, overlay, other", rec.parsed, 3);
            ensure_equals("one root matched", rec.roots, 1);
            ensure("at layer 2, after the empty path", rec.has("root 2 floater[f]"));
            ensure_equals("one root refused", rec.refused, 1);
            ensure("the other file, at layer 3", rec.has("refused 3 floater[g]"));
        }
        {
            RecordingObserver rec;
            LLXMLNodePtr root;
            ensure("a broken layer fails the load", !ALXmlLayerMerge::load({ base, broken }, root, &rec));
            ensure_equals("and is reported skipped", rec.skipped, 1);
            ensure("as layer 1", rec.has("skipped 1"));
            ensure("with a line past the first", rec.skippedLine >= 2);
        }
        {
            LLXMLNodePtr root;
            ensure("a broken base fails the load", !ALXmlLayerMerge::load({ broken, overlay }, root));
            ensure("nothing loads from nothing", !ALXmlLayerMerge::load({}, root));
        }
    }
}

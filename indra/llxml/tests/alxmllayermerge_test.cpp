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
#include "lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <sstream>

namespace
{
    // Every event, as one line: the kind, the layer, and the element or
    // attribute it was about, so a run can be compared with what the
    // rules say should happen.
    struct RecordingObserver final : public ALXmlMergeObserver
    {
        std::vector<std::string> events;
        S32 matched = 0;
        S32 rescued = 0;
        S32 unmatched = 0;
        S32 applied = 0;
        S32 dropped = 0;
        S32 text = 0;
        S32 kept = 0;
        S32 valueAsText = 0;
        S32 roots = 0;
        S32 renamed = 0;
        S32 retagged = 0;
        S32 parsed = 0;
        S32 skipped = 0;
        S32 skippedLine = 0;
        std::string skippedReason;

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
            skippedReason = reason;
            events.push_back("skipped " + std::to_string(layer));
        }
        void rootMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++roots; add("root", layer, base); }
        void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++renamed; add("renamed", layer, overlay); }
        void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++retagged; add("retagged", layer, overlay); }
        void childMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++matched; add("match", layer, base); }
        void childRescued(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++rescued; add("rescued", layer, base); }
        void childUnmatched(S32 layer, LLXMLNode* parent, LLXMLNode* overlay, Miss why) override
        {
            ++unmatched;
            add(why == Miss::Unnamed ? "unnamed"
                : why == Miss::Duplicate ? "duplicate"
                : why == Miss::Ambiguous ? "ambiguous" : "nosibling", layer, overlay);
        }
        void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++text; add("text", layer, base); }
        void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override { ++kept; add("kept", layer, base); }
        void valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override
        {
            ++valueAsText;
            add("value-as-text", layer, base);
        }
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
        // The base: a repeated name among siblings, elements with body
        // text, an unnamed child, and items matched by value.
        static constexpr const char* BASE =
            "<floater name=\"f\" title=\"Title\" width=\"100\">\n"
            "  <panel name=\"p\" width=\"10\">\n"
            "    <button name=\"b\" label=\"First\"/>\n"
            "    <button name=\"b\" label=\"Second\"/>\n"
            "    <text name=\"t\">Base text</text>\n"
            "    <text name=\"u\">Keep me</text>\n"
            "    <text name=\"w\" font=\"Sans\">Keep me too</text>\n"
            "    <item/>\n"
            "    <combo_box name=\"c\"><item value=\"v1\" label=\"One\"/><item value=\"v2\" label=\"Two\"/></combo_box>\n"
            "  </panel>\n"
            "</floater>\n";

        // The overlay: a title, an attribute the base lacks, both repeated
        // names, text, a value= where the base has body text, an element
        // with an attribute and no text where the base has text, a child
        // the base has nowhere, a child with no name, and one item by value.
        static constexpr const char* OVERLAY =
            "<floater name=\"f\" title=\"Titel\" extra=\"x\">\n"
            "  <panel name=\"p\">\n"
            "    <button name=\"b\" label=\"Erste\"/>\n"
            "    <button name=\"b\" label=\"Zweite\"/>\n"
            "    <text name=\"t\">Text</text>\n"
            "    <text name=\"u\" value=\"Wert\"/>\n"
            "    <text name=\"w\" font=\"Serif\"/>\n"
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

        // A folder of its own each time, so two runs at once never share one.
        alxmllayermerge_data()
        {
            mDir = gDirUtilp->add(gDirUtilp->getTempDir(), "alxmllayermerge_test_" + LLUUID::generateNewID().asString());
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


    // One event per decision: repeated names in order, text overwritten
    // where the overlay has text and kept where it has none, a value=
    // against body text applied as the text, an attribute the base lacks
    // dropped, and two children that match nothing.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: merge applies an overlay and reports one event per decision", "[alxmllayermerge]")
    {
        LLXMLNodePtr base = parse(BASE);
        LLXMLNodePtr overlay = parse(OVERLAY);
        {
            INFO("fixture parses");
            REQUIRE(base.notNull());
            REQUIRE(overlay.notNull());
        }

        RecordingObserver rec;
        ALXmlLayerMerge::merge(base, overlay, 1, &rec);

        CHECK(attr(base, "", "title") == std::string("Titel"));
        UNSCOPED_INFO("the base's width is untouched");
        CHECK(attr(base, "", "width") == std::string("100"));
        UNSCOPED_INFO("first of a repeated name");
        CHECK(attr(base, "p/b", "label") == std::string("Erste"));
        UNSCOPED_INFO("second of a repeated name");
        CHECK(attr(base, "p/b#1", "label") == std::string("Zweite"));
        CHECK(attr(base, "p/t", "text()") == std::string("Text"));
        UNSCOPED_INFO("an item by value");
        CHECK(attr(base, "p/c/v2", "label") == std::string("Zwei"));
        UNSCOPED_INFO("the other item is untouched");
        CHECK(attr(base, "p/c/v1", "label") == std::string("One"));
        UNSCOPED_INFO("value= against body text becomes the text");
        CHECK(attr(base, "p/u", "text()") == std::string("Wert"));
        UNSCOPED_INFO("and no value attribute appears");
        CHECK(attr(base, "p/u", "value") == std::string("<none>"));
        UNSCOPED_INFO("an overlay without text leaves the base's text");
        CHECK(attr(base, "p/w", "text()") == std::string("Keep me too"));
        UNSCOPED_INFO("while its attribute applies");
        CHECK(attr(base, "p/w", "font") == std::string("Serif"));

        CHECK(rec.has("dropped 1 extra=x"));
        CHECK(rec.dropped == 1);
        CHECK(rec.has("value-as-text 1 text[u]"));
        CHECK(rec.valueAsText == 1);
        CHECK(rec.has("kept 1 text[w]"));
        CHECK(rec.kept == 1);
        CHECK(rec.has("nosibling 1 text[missing]"));
        CHECK(rec.has("unnamed 1 text[]"));
        CHECK(rec.unmatched == 2);
        // p, b, b, t, u, w, c, and the item: eight matches.
        CHECK(rec.matched == 8);
        UNSCOPED_INFO("the second button matched the second base button");
        CHECK(rec.has("match 1 button[b]"));
        UNSCOPED_INFO("texts applied: t");
        CHECK(rec.text == 1);
        // title; label on b, b; font on w; value and label on the item.
        // Names are keys, and are not written.
        CHECK(rec.applied == 6);
        UNSCOPED_INFO("the name is never written");
        CHECK_FALSE(rec.has("attr 1 name=f"));
    }

    // With no observer the tree comes out the same: a rule that changes
    // changes in one place.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: merge gives the same tree with or without an observer", "[alxmllayermerge]")
    {
        LLXMLNodePtr observed_base = parse(BASE);
        LLXMLNodePtr observed_overlay = parse(OVERLAY);
        RecordingObserver rec;
        ALXmlLayerMerge::merge(observed_base, observed_overlay, 1, &rec);

        LLXMLNodePtr silent_base = parse(BASE);
        LLXMLNodePtr silent_overlay = parse(OVERLAY);
        ALXmlLayerMerge::merge(silent_base, silent_overlay);

        CHECK(serialize(silent_base) == serialize(observed_base));
        UNSCOPED_INFO("and the observer saw something");
        CHECK_FALSE(rec.events.empty());
    }

    // Loading: the base and each layer in turn; a layer whose root name
    // or tag differs is merged all the same and said so; a layer that
    // does not parse is passed over and the layers after it still apply;
    // a base that does not parse fails the load; an empty path is passed
    // over.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: load merges each layer in turn and passes over broken ones", "[alxmllayermerge]")
    {
        const std::string base = write("base.xml", BASE);
        const std::string overlay = write("overlay.xml", OVERLAY);
        const std::string renamed = write("renamed.xml", "<floater name=\"g\" title=\"Renamed\"><panel name=\"p\" width=\"7\"/></floater>\n");
        const std::string retagged = write("retagged.xml", "<panel name=\"f\" title=\"Retagged\"/>\n");
        const std::string broken = write("broken.xml", "<floater name=\"f\">\n<panel>\n");

        {
            RecordingObserver rec;
            LLXMLNodePtr root;
            REQUIRE(ALXmlLayerMerge::load({ base, "", overlay, renamed, retagged }, root, &rec));
            UNSCOPED_INFO("layers parsed: base, overlay, renamed, retagged");
            CHECK(rec.parsed == 4);
            CHECK(rec.roots == 3);
            UNSCOPED_INFO("the overlay at layer 2, after the empty path");
            CHECK(rec.has("root 2 floater[f]"));
            CHECK(rec.renamed == 1);
            CHECK(rec.has("renamed 3 floater[g]"));
            UNSCOPED_INFO("the base keeps its name");
            CHECK(attr(root, "", "name") == std::string("f"));
            UNSCOPED_INFO("its child matched by name all the same");
            CHECK(attr(root, "p", "width") == std::string("7"));
            CHECK(rec.retagged == 1);
            CHECK(rec.has("retagged 4 panel[f]"));
            UNSCOPED_INFO("and its title applied all the same");
            CHECK(attr(root, "", "title") == std::string("Retagged"));
        }
        {
            RecordingObserver rec;
            LLXMLNodePtr root;
            UNSCOPED_INFO("a broken layer does not fail the load");
            REQUIRE(ALXmlLayerMerge::load({ base, broken, overlay }, root, &rec));
            CHECK(rec.skipped == 1);
            CHECK(rec.has("skipped 1"));
            UNSCOPED_INFO("with a line past the first");
            CHECK(rec.skippedLine >= 2);
            UNSCOPED_INFO("the base is intact");
            CHECK(attr(root, "", "width") == std::string("100"));
            UNSCOPED_INFO("and the layer after it still applies");
            CHECK(attr(root, "", "title") == std::string("Titel"));
            UNSCOPED_INFO("at its own index");
            CHECK(rec.has("root 2 floater[f]"));
        }
        {
            LLXMLNodePtr root;
            UNSCOPED_INFO("a broken base fails the load");
            CHECK_FALSE(ALXmlLayerMerge::load({ broken, overlay }, root));
            UNSCOPED_INFO("nothing loads from nothing");
            CHECK_FALSE(ALXmlLayerMerge::load({}, root));
        }

        // A caller that passes no observer gets the installed one, which
        // is how the viewer's own loads reach the log.
        {
            RecordingObserver installed;
            ALXmlLayerMerge::setDefaultObserver(&installed);
            LLXMLNodePtr root;
            CHECK(ALXmlLayerMerge::load({ base, overlay }, root));
            UNSCOPED_INFO("the default observer heard it");
            CHECK(installed.roots == 1);
            ALXmlLayerMerge::setDefaultObserver(nullptr);

            LLXMLNodePtr again;
            CHECK(ALXmlLayerMerge::load({ base, overlay }, again));
            UNSCOPED_INFO("and hears nothing once it is gone");
            CHECK(installed.roots == 1);
        }
    }

    // A repeated name matches in document order whatever lies between:
    // the second overlay child of a name takes the second base child of
    // it, never the first again.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: a repeated name matches in document order whatever lies between", "[alxmllayermerge]")
    {
        LLXMLNodePtr base = parse(
            "<panel name=\"p\"><button name=\"b\" label=\"1\"/><button name=\"b\" label=\"2\"/><text name=\"x\">X</text></panel>");
        LLXMLNodePtr overlay = parse(
            "<panel name=\"p\"><button name=\"b\" label=\"A\"/><text name=\"x\">Y</text><button name=\"b\" label=\"B\"/></panel>");
        ALXmlLayerMerge::merge(base, overlay);
        CHECK(attr(base, "b", "label") == std::string("A"));
        UNSCOPED_INFO("the second b, with a match between them");
        CHECK(attr(base, "b#1", "label") == std::string("B"));
        UNSCOPED_INFO("and the one between");
        CHECK(attr(base, "x", "text()") == std::string("Y"));
    }

    // A child written where the base used to have the element is applied
    // where the base has it now, when exactly one element of the name
    // sits below; two below is a guess and none is a name the base no
    // longer has. And a copy left at the old path never takes the element
    // from a copy written at the new one, whichever the file lists first:
    // what is written where the base has it is matched before anything
    // is looked for below.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: a child the base moved deeper is applied where the base has it now", "[alxmllayermerge]")
    {
        static constexpr const char* MOVED =
            "<floater name=\"f\">\n"
            "  <panel name=\"stack\">\n"
            "    <text name=\"note\">Base note</text>\n"
            "    <panel name=\"deeper\"><button name=\"twice\" label=\"1\"/></panel>\n"
            "  </panel>\n"
            "  <panel name=\"other\"><button name=\"twice\" label=\"2\"/></panel>\n"
            "</floater>\n";

        {
            LLXMLNodePtr base = parse(MOVED);
            LLXMLNodePtr overlay = parse(
                "<floater name=\"f\">\n"
                "  <text name=\"note\">Notiz</text>\n"
                "  <button name=\"twice\" label=\"Zwei\"/>\n"
                "  <button name=\"gone\" label=\"Weg\"/>\n"
                "</floater>\n");
            RecordingObserver rec;
            ALXmlLayerMerge::merge(base, overlay, 1, &rec);
            UNSCOPED_INFO("the one element of the name below is where it went");
            CHECK(attr(base, "stack/note", "text()") == std::string("Notiz"));
            CHECK(rec.has("rescued 1 text[note]"));
            UNSCOPED_INFO("two of the name below is a guess not made");
            CHECK(attr(base, "stack/deeper/twice", "label") == std::string("1"));
            UNSCOPED_INFO("either of them");
            CHECK(attr(base, "other/twice", "label") == std::string("2"));
            CHECK(rec.has("ambiguous 1 button[twice]"));
            UNSCOPED_INFO("a name the base has nowhere");
            CHECK(rec.has("nosibling 1 button[gone]"));
            CHECK(rec.rescued == 1);
            CHECK(rec.unmatched == 2);
        }

        // The stray copy first in the file, then the right one.
        for (const bool stray_first : { true, false })
        {
            const std::string right = "  <panel name=\"stack\"><text name=\"note\">Richtig</text></panel>\n";
            const std::string stray = "  <text name=\"note\">Alt</text>\n";
            const std::string xml = "<floater name=\"f\">\n" + (stray_first ? stray + right : right + stray) + "</floater>\n";
            LLXMLNodePtr base = parse(MOVED);
            LLXMLNodePtr overlay = parse(xml.c_str());
            RecordingObserver rec;
            ALXmlLayerMerge::merge(base, overlay, 1, &rec);
            CAPTURE(stray_first);
            UNSCOPED_INFO("the copy written where the base has it wins");
            REQUIRE(attr(base, "stack/note", "text()") == std::string("Richtig"));
            UNSCOPED_INFO("and the stray applies to nothing");
            REQUIRE(rec.has("nosibling 1 text[note]"));
            REQUIRE(rec.rescued == 0);
        }
    }

    // A layer given as text is merged from the text and not from the disk
    // under the same name, and a text that does not parse is skipped with
    // the text's own error and line, not the disk's.
    TEST_CASE_METHOD(alxmllayermerge_data, "alxmllayermerge: a layer given as text is merged from the text not the disk", "[alxmllayermerge]")
    {
        const std::string base = write("base.xml", BASE);
        const std::string overlay = write("overlay.xml", OVERLAY);
        const std::string held = "<floater name=\"f\" title=\"Gehalten\"/>\n";
        {
            RecordingObserver rec;
            LLXMLNodePtr root;
            REQUIRE(ALXmlLayerMerge::loadSources({ { base, nullptr }, { overlay, &held } }, root, &rec));
            UNSCOPED_INFO("the title is the held text's, not the file's");
            CHECK(attr(root, "", "title") == std::string("Gehalten"));
            UNSCOPED_INFO("and the file's children were not read");
            CHECK(rec.matched == 0);
        }
        {
            const std::string broken = "<floater name=\"f\">\n  <panel>\n";
            RecordingObserver rec;
            LLXMLNodePtr root;
            UNSCOPED_INFO("loads past a held text that does not parse");
            REQUIRE(ALXmlLayerMerge::loadSources({ { base, nullptr }, { overlay, &broken } }, root, &rec));
            CHECK(rec.skipped == 1);
            UNSCOPED_INFO("with the text's line, past the first");
            CHECK(rec.skippedLine >= 2);
            {
                CAPTURE(rec.skippedReason);
                CHECK_FALSE(rec.skippedReason.empty());
                CHECK(rec.skippedReason.find("No error") == std::string::npos);
            }
            UNSCOPED_INFO("and the file under that name did not apply either");
            CHECK(attr(root, "", "title") == std::string("Title"));
        }
    }
}

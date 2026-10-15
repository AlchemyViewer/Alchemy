/**
 * @file alxuilint_test.cpp
 * @brief One file per rule, each carrying exactly the finding it is written to catch.
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

#include "alxuilint.h"
#include "alxuioverlay.h"
#include "alxmllayermerge.h"

#include "alxuicatalog.h"
#include "alxuisourcemap.h"
#include "../llpanel.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "altesttempdir.h"

#include "alxmldocument.h"

#include "lldir.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

namespace
{
    // Every element here is a panel. The rules that read the document
    // read it whatever the tag is, and a widget that draws text cannot be
    // built at all in this fixture: measuring a glyph asserts, since the
    // fonts stand up without their textures -- the atlas owns the GL, so
    // text is measured here and the truncation rule runs like the rest.
    struct alxuilint_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

        // The colour rule asks the table, and the fixture stands up a UI
        // without one; the source tree's colors.xml is what the viewer
        // would have.
        alxuilint_data()
        {
            ll_test::loadSkinColours(ui);
        }

        struct Run
        {
            LLPanel*        panel = nullptr;
            LLXMLNodePtr    node;
            ALXmlDocument   authored;
            ALXUISourceMap  map;
            ALXUILint       lint;
            std::string     source;     // the bytes the rules were run over
            const ALXUICatalog* catalog = nullptr;  // where a rule reads one

            ~Run() { delete panel; }

            bool build(const std::string& body, bool decisive = false)
            {
                // The root says topleft, as every shipped file does; a
                // file that does not is laid out from the bottom.
                const std::string xml =
                    "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n" + body + "\n</panel>\n";
                source = xml;
                if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
                {
                    return false;
                }
                if (!authored.loadBuffer(xml.data(), xml.size()))
                {
                    return false;
                }

                LLPanel::Params params(LLUICtrlFactory::getDefaultParams<LLPanel>());
                params.rect = LLRect(0, 100, 200, 0);
                panel = LLUICtrlFactory::create<TestPanel>(params);
                LLUICtrlFactory::instance().pushFileName("lint_test.xml");
                const bool ok = panel->initPanelXML(node, nullptr, LLUICtrlFactory::getDefaultParams<LLPanel>());
                LLUICtrlFactory::instance().popFileName();
                if (!ok)
                {
                    return false;
                }
                map.build(panel, node);

                ALXUILint::Input input;
                input.root = panel;
                input.sourceMap = &map;
                input.authored = authored.document().document_element();
                input.file = "lint_test.xml";
                input.callbacksAreDecisive = decisive;
                input.catalog = catalog;
                lint.run(input);
                return true;
            }

            S32 count(ALXUILint::Rule rule) const
            {
                S32 n = 0;
                for (const ALXUILint::Finding& f : lint.findings())
                {
                    n += f.rule == rule;
                }
                return n;
            }

            const ALXUILint::Finding* first(ALXUILint::Rule rule) const
            {
                for (const ALXUILint::Finding& f : lint.findings())
                {
                    if (f.rule == rule)
                    {
                        return &f;
                    }
                }
                return nullptr;
            }

            // The element as the built tree has it, which is what a fix that
            // moves or sizes something is measured from.
            ALXUIEdit::Anchor anchorFor(const ALXUISelection::path_t& path) const
            {
                ALXUIEdit::Anchor now;
                LLView* view = ALXUISelection::resolve(panel, path);
                if (!view || !view->getParent())
                {
                    return now;
                }
                const LLRect& rect = view->getRect();
                now.left = rect.mLeft;
                now.top = view->getParent()->getRect().getHeight() - rect.mTop;
                now.bottom = rect.mBottom;
                now.width = rect.getWidth();
                now.height = rect.getHeight();
                now.topLeft = view->isLayoutTopLeft();
                return now;
            }

            std::string describe() const
            {
                std::string text;
                for (const ALXUILint::Finding& f : lint.findings())
                {
                    text += std::string(ALXUILint::ruleName(f.rule)) + " on " + f.what + "; ";
                }
                return text;
            }
        };
    };

    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a clean file has no findings", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        REQUIRE(run.build(
            "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
            "  <panel name=\"b\" left=\"100\" top=\"0\" width=\"90\" height=\"40\"/>"));
        UNSCOPED_INFO("findings: " + run.describe());
        CHECK((S32)run.lint.findings().size() == 0);
    }

    // Two siblings shown together that intersect, against two where one
    // is hidden, which files do on purpose.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: intersecting shown siblings overlap and a hidden one is an alternative", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
                "  <panel name=\"b\" left=\"40\" top=\"0\" width=\"90\" height=\"40\"/>"));
            UNSCOPED_INFO("one overlap: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::Overlap) == 1);
            CHECK(run.count(ALXUILint::Rule::Alternatives) == 0);
            UNSCOPED_INFO("named for the pair");
            CHECK(run.first(ALXUILint::Rule::Overlap)->what == std::string("a"));
            CHECK(ALXUISelection::toString(run.first(ALXUILint::Rule::Overlap)->path) == std::string("a"));
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
                "  <panel name=\"b\" left=\"40\" top=\"0\" width=\"90\" height=\"40\" visible=\"false\"/>"));
            UNSCOPED_INFO("stacked alternatives are a note: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::Alternatives) == 1);
            CHECK(run.count(ALXUILint::Rule::Overlap) == 0);
            CHECK((int)run.first(ALXUILint::Rule::Alternatives)->severity ==
                          (int)ALXUILint::Severity::Note);
        }
    }

    // A child outside its parent, an empty rect, and two siblings of one
    // name, each exactly once.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: an outside child and an empty rect and a repeated name each found once", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"150\" top=\"0\" width=\"90\" height=\"40\"/>"));
            UNSCOPED_INFO("out of bounds: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::OutOfBounds) == 1);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"0\" height=\"40\"/>"));
            UNSCOPED_INFO("empty rect: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::EmptyRect) == 1);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"40\" height=\"20\"/>\n"
                "  <panel name=\"a\" left=\"0\" top=\"50\" width=\"40\" height=\"20\"/>"));
            UNSCOPED_INFO("one collision for two of a name: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::NameCollision) == 1);
            UNSCOPED_INFO("reported on the second");
            CHECK(ALXUISelection::toString(run.first(ALXUILint::Rule::NameCollision)->path) == std::string("a#1"));
        }
    }

    // The reference rules: a font, a colour and a control nobody has, and
    // a callback no registry knows.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: reference rules flag a font colour control or callback nobody has", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" font=\"NoSuchFont\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("dangling font: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::DanglingFont) == 1);
        }
        {
            // The names every shipped file uses answer to the registry
            // rather than to the four legacy ones.
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" font=\"SansSerif\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"b\" font=\"SansSerifSmall\" left=\"0\" top=\"30\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"c\" font=\"Monospace\" left=\"0\" top=\"60\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("a font fonts.xml declares is not dangling: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::DanglingFont) == 0);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" bg_opaque_color=\"NoSuchColour\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("dangling colour: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::DanglingColor) == 1);
        }
        {
            // A literal is not a name and is passed over, and so is a
            // colour the table has.
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" bg_opaque_color=\"1 1 1 1\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"b\" bg_opaque_color=\"White\" left=\"0\" top=\"40\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("neither is dangling: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::DanglingColor) == 0);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" control_name=\"NoSuchSetting\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("control missing: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::ControlMissing) == 1);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" control_name=\"UIScrollbarSize\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            UNSCOPED_INFO("a setting a group has is not missing: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::ControlMissing) == 0);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"20\">\n"
                "    <panel.commit_callback function=\"NoSuch.Function\"/>\n"
                "  </panel>", /*decisive=*/true));
            UNSCOPED_INFO("callback not registered: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::CallbackNotRegistered) == 1);
            UNSCOPED_INFO("decisive here");
            CHECK((int)run.first(ALXUILint::Rule::CallbackNotRegistered)->severity ==
                          (int)ALXUILint::Severity::Error);
        }
        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"20\">\n"
                "    <panel.commit_callback function=\"NoSuch.Function\"/>\n"
                "  </panel>", /*decisive=*/false));
            UNSCOPED_INFO("callback not registered: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::CallbackNotRegistered) == 1);
            UNSCOPED_INFO("advisory in a shell build");
            CHECK((int)run.first(ALXUILint::Rule::CallbackNotRegistered)->severity ==
                          (int)ALXUILint::Severity::Note);
        }
    }

    // The counts a collapsed row shows: a finding is counted on its
    // element and on every ancestor.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a finding is counted on its element and on every ancestor", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        REQUIRE(run.build(
            "  <panel name=\"outer\" left=\"0\" top=\"0\" width=\"150\" height=\"80\">\n"
            "    <panel name=\"t\" font=\"NoSuchFont\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
            "  </panel>"));
        UNSCOPED_INFO("one finding: " + run.describe());
        CHECK((S32)run.lint.findings().size() == 1);
        UNSCOPED_INFO("on the element");
        CHECK(run.lint.countUnder(ALXUISelection::fromString("outer/t")) == 1);
        UNSCOPED_INFO("on its parent");
        CHECK(run.lint.countUnder(ALXUISelection::fromString("outer")) == 1);
        UNSCOPED_INFO("and on the root");
        CHECK(run.lint.countUnder(ALXUISelection::path_t()) == 1);
        UNSCOPED_INFO("nowhere else");
        CHECK(run.lint.countUnder(ALXUISelection::fromString("nobody")) == 0);
    }

    // The rules that read the catalog rather than a build: a template
    // written under a root that is not the tag it configures, and a layer
    // that did not parse.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: catalog rules find a template under another root and an unparsable layer", "[alxuilint]")
    {
        // A tree of the test's own, removed as it ends.
        const ll_test::TempDir temp("alxuilint_catalog_test");
        const std::string      skins = temp.string();
        auto write = [&](const std::string& relative, const std::string& text)
        {
            std::string dir = skins;
            LLFile::mkdir(dir);
            size_t start = 0;
            while (true)
            {
                const size_t slash = relative.find('/', start);
                if (slash == std::string::npos)
                {
                    break;
                }
                dir = gDirUtilp->add(dir, relative.substr(start, slash - start));
                LLFile::mkdir(dir);
                start = slash + 1;
            }
            llofstream out(gDirUtilp->add(dir, relative.substr(start)), std::ios::binary);
            out << text;
        };
        write("default/xui/en/widgets/button.xml", "<button name=\"button\"/>\n");
        write("default/xui/en/widgets/check_box.xml", "<checkbox name=\"check_box\"/>\n");
        write("default/xui/en/broken.xml", "<panel name=\"b\">\n<panel>\n");

        ALXUICatalog catalog;
        catalog.scan(skins);
        const std::vector<ALXUILint::Finding> findings = ALXUILint::checkCatalog(catalog);

        S32 mismatches = 0;
        S32 parse_errors = 0;
        for (const ALXUILint::Finding& f : findings)
        {
            mismatches += f.rule == ALXUILint::Rule::TemplateRootMismatch;
            parse_errors += f.rule == ALXUILint::Rule::ParseError;
            if (f.rule == ALXUILint::Rule::TemplateRootMismatch)
            {
                UNSCOPED_INFO("the root that was written");
                REQUIRE(f.what == std::string("checkbox"));
                // The parser reads the root's attributes whatever it is
                // called, so this is worth a look and not a defect.
                REQUIRE((int)f.severity == (int)ALXUILint::Severity::Note);
            }
        }
        UNSCOPED_INFO("one template written under another tag");
        CHECK(mismatches == 1);
        UNSCOPED_INFO("and one layer that did not parse");
        CHECK(parse_errors == 1);
    }

    // A layout panel has one dimension the stack reads -- the one along the
    // axis the stack runs -- and three names for it. A name for the other axis
    // sets that same parameter, and two names on one panel are one value.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a layout panel dimension named on the cross axis is reported", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "<layout_stack name=\"stack\" orientation=\"horizontal\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <layout_panel name=\"across\" min_height=\"20\"/>\n"
            "  <layout_panel name=\"twice\" min_width=\"30\" min_height=\"40\"/>\n"
            "  <layout_panel name=\"along\" min_width=\"50\"/>\n"
            "</layout_stack>"));

        UNSCOPED_INFO(run.describe());
        CHECK(run.count(ALXUILint::Rule::LayoutDimension) == 2);

        const ALXUILint::Finding* first = run.first(ALXUILint::Rule::LayoutDimension);
        REQUIRE(first != nullptr);
        UNSCOPED_INFO("the cross-axis name is the one reported");
        CHECK(first->what == std::string("min_height"));
    }

    // An attribute written as the value the element already carries. The
    // schema knows what that is because it serializes the block a widget is
    // built from, so this is a thing the tool could not say at all until it
    // did. Worth a look rather than a fault: a number written where a reader
    // would otherwise have to know it is a reasonable thing to write.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: an attribute written as its default is a note to take it out", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "<panel name=\"root\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\" height=\"23\"/>\n"
            "  <button name=\"quiet\" left=\"0\" top=\"40\" width=\"80\"/>\n"
            "</panel>"));

        UNSCOPED_INFO(run.describe());
        CHECK(run.count(ALXUILint::Rule::WroteTheDefault) == 1);
        const ALXUILint::Finding* said = run.first(ALXUILint::Rule::WroteTheDefault);
        REQUIRE(said != nullptr);
        UNSCOPED_INFO("named by attribute");
        CHECK(said->what == std::string("height"));
        UNSCOPED_INFO("and it is worth a look rather than a fault");
        CHECK(said->severity == ALXUILint::Severity::Note);
        UNSCOPED_INFO("which is one attribute away from not being said at all");
        CHECK(said->fix.did == ALXUILint::Fix::Do::TakeAttributeOut);
    }

    // A finding that says what right would be. A name one slip from a real
    // one was meant to be that one; a name near nothing is a name nothing
    // reads; and an element outside what holds it moves by a delta the rule
    // has already worked out to say how far outside it is.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: unknown attributes and an outside child each offer the fix for them", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "<panel name=\"root\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tp=\"near\"/>\n"
            "  <button name=\"nothing\" left=\"0\" top=\"30\" width=\"80\" qqzzxw=\"far\"/>\n"
            "  <panel name=\"out\" left=\"150\" top=\"60\" width=\"90\" height=\"20\"/>\n"
            "</panel>"));

        UNSCOPED_INFO(run.describe());
        CHECK(run.count(ALXUILint::Rule::UnknownAttribute) == 2);

        const ALXUILint::Finding* slip = nullptr;
        const ALXUILint::Finding* nothing = nullptr;
        for (const ALXUILint::Finding& f : run.lint.findings())
        {
            if (f.rule != ALXUILint::Rule::UnknownAttribute) { continue; }
            if (f.what == "tool_tp") { slip = &f; }
            if (f.what == "qqzzxw") { nothing = &f; }
        }

        REQUIRE(slip != nullptr);
        UNSCOPED_INFO("the near one is offered the spelling it meant");
        CHECK(slip->fix.did == ALXUILint::Fix::Do::SpellAttribute);
        UNSCOPED_INFO("which is the real name");
        CHECK(slip->fix.spelling == std::string("tool_tip"));

        REQUIRE(nothing != nullptr);
        UNSCOPED_INFO("the far one can only be taken out");
        CHECK(nothing->fix.did == ALXUILint::Fix::Do::TakeAttributeOut);

        const ALXUILint::Finding* out = run.first(ALXUILint::Rule::OutOfBounds);
        REQUIRE(out != nullptr);
        UNSCOPED_INFO("the one outside can be moved in");
        CHECK(out->fix.did == ALXUILint::Fix::Do::MoveInside);
        UNSCOPED_INFO("by exactly how far outside it is");
        CHECK(out->fix.dx == -40);
        UNSCOPED_INFO("and not at all on the axis it is inside on");
        CHECK(out->fix.dy == 0);
    }

    // Something wider than what holds it leaves by the far edge whichever
    // way it goes, so there is no move to offer: a fix that does not fix it
    // is worse than none, because it is a button that says it will.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: something wider than its parent is reported with no move offered", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "<panel name=\"wider\" left=\"10\" top=\"0\" width=\"300\" height=\"20\"/>"));

        const ALXUILint::Finding* out = run.first(ALXUILint::Rule::OutOfBounds);
        UNSCOPED_INFO("it is still reported");
        REQUIRE(out != nullptr);
        UNSCOPED_INFO("and offers nothing");
        CHECK(out->fix.did == ALXUILint::Fix::Do::Nothing);
    }

    // A fix that fixes the wrong thing writes its wrongness into the file
    // instead of reporting it, so what each of them puts in the file is
    // watched byte for byte. These two write an attribute.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: attribute fixes take out a default or respell a slip and undo whole", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Written as the value it already carries: it comes off, and the
        // whitespace that separated it comes off with it.
        {
            Run run;
            REQUIRE(run.build(
                "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\" height=\"23\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::WroteTheDefault);
            REQUIRE(f != nullptr);

            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(run.source));
            CHECK(f->applyFix(edit, run.anchorFor(f->path)));
            UNSCOPED_INFO("the height is gone and nothing else moved");
            CHECK(edit.text() ==
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\"/>\n"
                "</panel>\n");
            UNSCOPED_INFO("as one step");
            CHECK(edit.undo());
            UNSCOPED_INFO("put back whole");
            CHECK(edit.text() == run.source);
        }

        // A name one slip from a real one: the name changes where it stands
        // and the author's value is untouched.
        {
            Run run;
            REQUIRE(run.build(
                "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tp=\"near\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::UnknownAttribute);
            REQUIRE(f != nullptr);
            CHECK(f->fix.did == ALXUILint::Fix::Do::SpellAttribute);

            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(run.source));
            CHECK(f->applyFix(edit, run.anchorFor(f->path)));
            UNSCOPED_INFO("spelt right, in place");
            CHECK(edit.text() ==
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tip=\"near\"/>\n"
                "</panel>\n");
            UNSCOPED_INFO("as one step");
            CHECK(edit.undo());
            UNSCOPED_INFO("put back whole");
            CHECK(edit.text() == run.source);
        }
    }

    // And these two write a number, worked out from where the element is in
    // the tree that was built. The attribute that moves is the one the author
    // used: no positioning form is ever converted into another.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: move and widen fixes write their number to the attribute the author used", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        {
            Run run;
            REQUIRE(run.build(
                "  <panel name=\"out\" left=\"150\" top=\"60\" width=\"90\" height=\"20\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::OutOfBounds);
            REQUIRE(f != nullptr);
            CHECK(f->fix.dx == -40);

            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(run.source));
            CHECK(f->applyFix(edit, run.anchorFor(f->path)));
            UNSCOPED_INFO("the left it was written with is the left that moved");
            CHECK(edit.text() ==
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <panel name=\"out\" left=\"110\" top=\"60\" width=\"90\" height=\"20\"/>\n"
                "</panel>\n");
            UNSCOPED_INFO("as one step");
            CHECK(edit.undo());
            UNSCOPED_INFO("put back whole");
            CHECK(edit.text() == run.source);
        }

        // How wide the text needs to be is the font's answer and not this
        // test's, so the width asserted is the one the rule worked out. What
        // is being watched is that it lands on `width` and nowhere else.
        {
            Run run;
            REQUIRE(run.build(
                "  <text name=\"long\" left=\"0\" top=\"0\" width=\"10\" height=\"16\">A good deal of text</text>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::Truncation);
            UNSCOPED_INFO("found: " + run.describe());
            REQUIRE(f != nullptr);
            CHECK(f->fix.did == ALXUILint::Fix::Do::WidenBy);
            CHECK(f->fix.dx > 0);

            ALXUIEdit edit;
            REQUIRE(edit.loadBuffer(run.source));
            CHECK(f->applyFix(edit, run.anchorFor(f->path)));
            UNSCOPED_INFO("only the width moved");
            CHECK(edit.text() ==
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <text name=\"long\" left=\"0\" top=\"0\" width=\"" + std::to_string(10 + f->fix.dx)
                + "\" height=\"16\">A good deal of text</text>\n"
                "</panel>\n");
            UNSCOPED_INFO("as one step");
            CHECK(edit.undo());
            UNSCOPED_INFO("put back whole");
            CHECK(edit.text() == run.source);
        }
    }

    // A parent that lays its children out itself reads no position from them,
    // and the file's own root sits where the tool put it. Both are still
    // reported; neither is offered a fix, because the fix would write a
    // number nothing reads.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: children a stack lays out are reported but offered no fix", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "  <layout_stack name=\"stack\" orientation=\"vertical\" left=\"0\" top=\"0\""
            " width=\"200\" height=\"60\">\n"
            "    <layout_panel name=\"tall\" auto_resize=\"false\" height=\"40\"/>\n"
            "    <layout_panel name=\"also\" auto_resize=\"false\" height=\"40\"/>\n"
            "  </layout_stack>"));

        S32 placed = 0;
        for (const ALXUILint::Finding& f : run.lint.findings())
        {
            if (f.rule == ALXUILint::Rule::OutOfBounds || f.rule == ALXUILint::Rule::Truncation)
            {
                ++placed;
                UNSCOPED_INFO("nothing a stack places is offered a move");
                REQUIRE(f.fix.did == ALXUILint::Fix::Do::Nothing);
            }
        }
        UNSCOPED_INFO("and there is one to have refused: " + run.describe());
        CHECK(placed > 0);
    }

    // A name that exists, works, and should not be used. The block registered
    // two names for one parameter -- which said that both work and will keep
    // working -- and a person wrote down which of them to write. Nothing is
    // broken, so it is worth a look rather than a fault, and it is one edit
    // from being right.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a deprecated attribute name is a note offering the name to write", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "  <text name=\"old\" left=\"0\" top=\"0\" width=\"80\" height=\"20\""
            " word_wrap=\"true\">Hello</text>\n"
            "  <text name=\"new\" left=\"0\" top=\"30\" width=\"80\" height=\"20\""
            " wrap=\"true\">Hello</text>"));

        UNSCOPED_INFO(run.describe());
        CHECK(run.count(ALXUILint::Rule::DeprecatedAttribute) == 1);
        const ALXUILint::Finding* f = run.first(ALXUILint::Rule::DeprecatedAttribute);
        REQUIRE(f != nullptr);
        UNSCOPED_INFO("the old spelling is the one reported");
        CHECK(f->what == std::string("word_wrap"));
        UNSCOPED_INFO("nothing is broken, so it is worth a look");
        CHECK(f->severity == ALXUILint::Severity::Note);
        UNSCOPED_INFO("and it offers the name to write instead");
        CHECK(f->fix.did == ALXUILint::Fix::Do::SpellAttribute);
        UNSCOPED_INFO("which is the one the notes name");
        CHECK(f->fix.spelling == std::string("wrap"));

        // Applied, it is the same edit as any other spelling fix.
        ALXUIEdit edit;
        REQUIRE(edit.loadBuffer(run.source));
        CHECK(f->applyFix(edit, run.anchorFor(f->path)));
        UNSCOPED_INFO("the old name is gone");
        CHECK(edit.text().find("word_wrap=\"true\"") == std::string::npos);
        UNSCOPED_INFO("and the new one is where it was");
        CHECK(edit.text().find("wrap=\"true\">Hello</text>") != std::string::npos);
    }

    // An element already carrying both names is not one to write a second of:
    // two of a name on one element is one value chosen by the parser.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: an element carrying both the old and new name is offered no fix", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        REQUIRE(run.build(
            "  <text name=\"both\" left=\"0\" top=\"0\" width=\"80\" height=\"20\""
            " word_wrap=\"true\" wrap=\"true\">Hello</text>"));

        const ALXUILint::Finding* f = run.first(ALXUILint::Rule::DeprecatedAttribute);
        UNSCOPED_INFO("still reported");
        REQUIRE(f != nullptr);
        UNSCOPED_INFO("but there is nothing to offer");
        CHECK(f->fix.did == ALXUILint::Fix::Do::Nothing);
    }

    // A file an element names is a file the catalog has or has not. The
    // rule was written after a gate that turns any value with a dot in it
    // away as a path, and every file name has one, so it never ran.
    //
    // A panel naming a file builds only if the file loads, and it loads from
    // the skins the fixture stands on rather than from the catalog asked
    // here: so the file named is a real one, the catalog is a directory of
    // this test's own, and whether the catalog has the name is the question.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a file name the catalog lacks is reported and one it has is not", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // A tree of the test's own, removed as it ends.
        const ll_test::TempDir temp("alxuilint_file_test");
        const std::string      skins = temp.string();
        std::string dir = skins;
        for (const char* step : { "default", "xui", "en" })
        {
            LLFile::mkdir(dir);
            dir = gDirUtilp->add(dir, step);
        }
        LLFile::mkdir(dir);
        static const char* const REAL = "panel_chat_separator.xml";
        {
            llofstream out(gDirUtilp->add(dir, REAL), std::ios::binary);
            out << "<panel name=\"there\" width=\"10\" height=\"10\"/>\n";
        }
        ALXUICatalog has_it;
        has_it.scan(skins);
        REQUIRE(has_it.find(REAL) != nullptr);
        ALXUICatalog has_not;

        const std::string body = std::string("  <panel name=\"a\" filename=\"") + REAL
            + "\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>";
        {
            Run run;
            run.catalog = &has_it;
            REQUIRE(run.build(body));
            UNSCOPED_INFO("the panel was built");
            CHECK(run.panel->getChildCount() > 0);
            UNSCOPED_INFO("a file the catalog has: " + run.describe());
            CHECK(run.count(ALXUILint::Rule::FileMissing) == 0);
        }
        {
            Run run;
            run.catalog = &has_not;
            CHECK(run.build(body));
            UNSCOPED_INFO("and one it has not: " + run.describe());
            REQUIRE(run.count(ALXUILint::Rule::FileMissing) == 1);
            CHECK(run.first(ALXUILint::Rule::FileMissing)->what == std::string("filename"));
        }
    }

    // Every finding names its sentence and the names in it, and says it
    // as the file has it, filled: what a person reads and what a filter
    // matches are the same words.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: every finding says its sentence key filled with its names", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        REQUIRE(run.build(
            "  <panel name=\"a\" tool_tp=\"slip\" left=\"0\" top=\"0\" width=\"300\" height=\"20\"/>\n"
            "  <panel name=\"a\" left=\"0\" top=\"30\" width=\"20\" height=\"0\"/>\n"
            "  <panel name=\"b\" color=\"NoSuchColour\" left=\"0\" top=\"40\" width=\"20\" height=\"20\"/>"));
        UNSCOPED_INFO("there are findings to check: " + run.describe());
        REQUIRE_FALSE(run.lint.findings().empty());
        for (const ALXUILint::Finding& f : run.lint.findings())
        {
            UNSCOPED_INFO("every finding names its sentence: " + f.message);
            REQUIRE_FALSE(f.key.empty());
            UNSCOPED_INFO("and the sentence is one the lint asks for: " + f.key);
            REQUIRE(std::any_of(ALXUILint::keys().begin(), ALXUILint::keys().end(),
                               [&f](const char* key) { return f.key == key; }));
            UNSCOPED_INFO("and the words are that sentence, filled: " + f.key);
            REQUIRE(f.message == ALXUILint::wording(f.key, f.args));
            UNSCOPED_INFO("which is not the key said bare: " + f.message);
            REQUIRE(f.message.rfind(f.key, 0) != 0);
        }

        const ALXUILint::Finding* slip = run.first(ALXUILint::Rule::UnknownAttribute);
        REQUIRE(slip != nullptr);
        UNSCOPED_INFO("as the sentence with the spelling in it");
        CHECK(slip->key == std::string("LintUnknownAttributeSlip"));
        UNSCOPED_INFO("which names the tag");
        CHECK(slip->args.at("[TAG]")() == std::string("panel"));
        UNSCOPED_INFO("the name");
        CHECK(slip->args.at("[NAME]")() == std::string("tool_tp"));
        UNSCOPED_INFO("and the one meant");
        CHECK(slip->args.at("[MEANT]")() == std::string("tool_tip"));

        const ALXUILint::Finding* empty = run.first(ALXUILint::Rule::EmptyRect);
        REQUIRE(empty != nullptr);
        UNSCOPED_INFO("with its height");
        CHECK(empty->args.at("[HEIGHT]")() == std::string("0"));
    }

    // Every sentence the lint can ask for, every rule and every severity
    // is a line in xui_lint.xml, so nothing it says arrives as the bare
    // name of a line nobody wrote.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: every sentence rule and severity has a line in xui_lint.xml", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLStringUtil::format_map_t none;
        for (const char* key : ALXUILint::keys())
        {
            const std::string said = ALXUILint::wording(key, none);
            {
                INFO("the file has a line for " << key);
                REQUIRE_FALSE(said.empty());
                REQUIRE(said != key);
            }
        }
        for (U8 i = 0; i <= (U8)ALXUILint::Rule::FileMissing; ++i)
        {
            const ALXUILint::Rule rule = (ALXUILint::Rule)i;
            const std::string label = ALXUILint::ruleLabel(rule);
            {
                INFO("the file names the rule " << ALXUILint::ruleKey(rule));
                REQUIRE_FALSE(label.empty());
                REQUIRE(label != ALXUILint::ruleKey(rule));
            }
            ALXUILint::Rule back;
            {
                INFO("and the rule's name comes back to it");
                REQUIRE(ALXUILint::ruleNamed(ALXUILint::ruleName(rule), back));
                REQUIRE(back == rule);
            }
        }
        for (ALXUILint::Severity severity : { ALXUILint::Severity::Error, ALXUILint::Severity::Warning,
                                              ALXUILint::Severity::Note })
        {
            const std::string label = ALXUILint::severityLabel(severity);
            {
                INFO("the file names the severity " << ALXUILint::severityKey(severity));
                REQUIRE_FALSE(label.empty());
                REQUIRE(label != ALXUILint::severityKey(severity));
            }
        }

        // And a line, filled: the brackets take the names the finding gives.
        LLStringUtil::format_map_t args;
        args["[WIDTH]"] = "3";
        args["[HEIGHT]"] = "4";
        UNSCOPED_INFO("filled");
        CHECK(ALXUILint::wording("LintEmptyRect", args) == std::string("shown with a rect of 3 by 4"));
    }

    // An overlap is reported on the element's own path. The second of two
    // siblings of one name is the second of them, and a finding that named
    // the first would badge and select the wrong element.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: an overlap is reported on the element's own path", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        REQUIRE(run.build(
            "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"20\" height=\"20\"/>\n"
            "  <panel name=\"a\" left=\"100\" top=\"0\" width=\"20\" height=\"20\"/>\n"
            "  <panel name=\"b\" left=\"110\" top=\"0\" width=\"20\" height=\"20\"/>"));
        UNSCOPED_INFO("one overlap: " + run.describe());
        REQUIRE(run.count(ALXUILint::Rule::Overlap) == 1);
        const ALXUILint::Finding* overlap = run.first(ALXUILint::Rule::Overlap);
        UNSCOPED_INFO("on the second of the name");
        CHECK(ALXUISelection::toString(overlap->path) ==
                      ALXUISelection::step("a", 1));
        UNSCOPED_INFO("with the other named");
        CHECK(overlap->args.at("[OTHER]")() == std::string("b"));
    }

    // What the merge dropped is a warning on the element it was looked for
    // under, and what it rescued is a note on the element it landed on:
    // applied, and holding only until the base grows another of the name.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a merge drop is a warning and a merge rescue a note on where it landed", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string base_xml =
            "<floater name=\"f\">\n"
            "  <panel name=\"stack\"><text name=\"note\">Base note</text></panel>\n"
            "</floater>\n";
        const std::string overlay_xml =
            "<floater name=\"f\" extra=\"x\">\n"
            "  <text name=\"note\">Notiz</text>\n"
            "</floater>\n";
        LLXMLNodePtr base;
        LLXMLNodePtr layer;
        REQUIRE(LLXMLNode::parseBuffer(base_xml.data(), base_xml.size(), base));
        REQUIRE(LLXMLNode::parseBuffer(overlay_xml.data(), overlay_xml.size(), layer));
        ALXUIOverlay overlay;
        overlay.layerParsed(0, "floater_x.xml");
        overlay.layerParsed(1, "de/floater_x.xml");
        ALXmlLayerMerge::merge(base, layer, 1, &overlay);

        ALXUILint lint;
        ALXUILint::Input input;
        input.overlay = &overlay;
        input.file = "floater_x.xml";
        lint.run(input);

        const ALXUILint::Finding* drop = nullptr;
        const ALXUILint::Finding* rescue = nullptr;
        for (const ALXUILint::Finding& f : lint.findings())
        {
            if (f.rule == ALXUILint::Rule::OverlayDrop)   { drop = &f; }
            if (f.rule == ALXUILint::Rule::OverlayRescue) { rescue = &f; }
        }
        UNSCOPED_INFO("the attribute the base lacks is a drop");
        REQUIRE(drop != nullptr);
        CHECK(drop->severity == ALXUILint::Severity::Warning);
        UNSCOPED_INFO("in the layer's file");
        CHECK(drop->file == std::string("de/floater_x.xml"));
        CHECK(drop->what == std::string("extra"));
        UNSCOPED_INFO("in the sentence's words");
        CHECK(drop->message == ALXUILint::wording("LintDropAttribute", drop->args));

        UNSCOPED_INFO("the value applied where the base moved it is a rescue");
        REQUIRE(rescue != nullptr);
        CHECK(rescue->severity == ALXUILint::Severity::Note);
        UNSCOPED_INFO("on the element it landed on");
        CHECK(ALXUISelection::toString(rescue->path) == std::string("stack/note"));
        UNSCOPED_INFO("saying where it was written");
        CHECK(rescue->args.at("[FROM]")() == std::string("note"));
        UNSCOPED_INFO("and where it went");
        CHECK(rescue->args.at("[TO]")() == std::string("stack/note"));
        UNSCOPED_INFO("in the layer's file");
        CHECK(rescue->file == std::string("de/floater_x.xml"));
        UNSCOPED_INFO("on the line it was written");
        CHECK(rescue->line == 2);
        UNSCOPED_INFO("under a rule with a name");
        CHECK(std::string(ALXUILint::ruleName(rescue->rule)) == "overlay rescue");
        ALXUILint::Rule back;
        {
            INFO("that comes back");
            CHECK(ALXUILint::ruleNamed("overlay rescue", back));
            CHECK(back == ALXUILint::Rule::OverlayRescue);
        }
    }

    // A parameter element and a widget under one parent sharing a name: the
    // merge matches by name whatever the tag, and a path of names cannot
    // tell them apart. Two widgets of a name are the collision rule's, and
    // two parameter elements are the merge's own duplicate.
    TEST_CASE_METHOD(alxuilint_data, "alxuilint: a parameter element and a widget sharing a name are one collision", "[alxuilint]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        REQUIRE(run.build(
            "  <string name=\"shared\">Hello</string>\n"
            "  <panel name=\"shared\" left=\"0\" top=\"0\" width=\"20\" height=\"20\"/>\n"
            "  <string name=\"alone\">Alone</string>\n"
            "  <panel name=\"other\" left=\"40\" top=\"0\" width=\"20\" height=\"20\"/>"));
        UNSCOPED_INFO("one collision: " + run.describe());
        REQUIRE(run.count(ALXUILint::Rule::NameCollision) == 1);
        const ALXUILint::Finding* f = run.first(ALXUILint::Rule::NameCollision);
        UNSCOPED_INFO("said as the parameter sentence");
        CHECK(f->key == std::string("LintNameCollisionParameter"));
        CHECK(f->what == std::string("shared"));
        UNSCOPED_INFO("the parameter element");
        CHECK(f->args.at("[TAG]")() == std::string("string"));
        UNSCOPED_INFO("and the widget");
        CHECK(f->args.at("[OTHER]")() == std::string("panel"));
        UNSCOPED_INFO("under the parent");
        CHECK(f->args.at("[PARENT]")() == std::string("root"));
        UNSCOPED_INFO("on the parent's path, since a path cannot name the pair");
        CHECK(f->path.empty());
    }
}

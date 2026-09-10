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

#include "../alxuilint.h"

#include "../alxuicatalog.h"
#include "../alxuisourcemap.h"
#include "../llpanel.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "alxmldocument.h"

#include "lldir.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <cstring>

class LLAvatarName;
const std::string gLintTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gLintTestAnonName;
}

namespace tut
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
            static bool loaded = false;
            if (ui.ok() && !loaded)
            {
                loaded = LLUIColorTable::instance().loadFromSettings();
            }
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

    typedef test_group<alxuilint_data> alxuilint_test;
    typedef alxuilint_test::object     alxuilint_object;
    tut::alxuilint_test alxuilint_testgroup("alxuilint");

    // A clean file carries nothing.
    template<> template<>
    void alxuilint_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        ensure("builds", run.build(
            "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
            "  <panel name=\"b\" left=\"100\" top=\"0\" width=\"90\" height=\"40\"/>"));
        ensure_equals("a clean file has no findings: " + run.describe(), (S32)run.lint.findings().size(), 0);
    }

    // Two siblings shown together that intersect, against two where one
    // is hidden, which files do on purpose.
    template<> template<>
    void alxuilint_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
                "  <panel name=\"b\" left=\"40\" top=\"0\" width=\"90\" height=\"40\"/>"));
            ensure_equals("one overlap: " + run.describe(), run.count(ALXUILint::Rule::Overlap), 1);
            ensure_equals("and no alternatives", run.count(ALXUILint::Rule::Alternatives), 0);
            ensure_equals("named for the pair", run.first(ALXUILint::Rule::Overlap)->what, std::string("a"));
            ensure_equals("with the element's path",
                          ALXUISelection::toString(run.first(ALXUILint::Rule::Overlap)->path), std::string("a"));
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"40\"/>\n"
                "  <panel name=\"b\" left=\"40\" top=\"0\" width=\"90\" height=\"40\" visible=\"false\"/>"));
            ensure_equals("stacked alternatives are a note: " + run.describe(),
                          run.count(ALXUILint::Rule::Alternatives), 1);
            ensure_equals("and not an overlap", run.count(ALXUILint::Rule::Overlap), 0);
            ensure_equals("a note", (int)run.first(ALXUILint::Rule::Alternatives)->severity,
                          (int)ALXUILint::Severity::Note);
        }
    }

    // A child outside its parent, an empty rect, and two siblings of one
    // name, each exactly once.
    template<> template<>
    void alxuilint_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"150\" top=\"0\" width=\"90\" height=\"40\"/>"));
            ensure_equals("out of bounds: " + run.describe(), run.count(ALXUILint::Rule::OutOfBounds), 1);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"0\" height=\"40\"/>"));
            ensure_equals("empty rect: " + run.describe(), run.count(ALXUILint::Rule::EmptyRect), 1);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"40\" height=\"20\"/>\n"
                "  <panel name=\"a\" left=\"0\" top=\"50\" width=\"40\" height=\"20\"/>"));
            ensure_equals("one collision for two of a name: " + run.describe(),
                          run.count(ALXUILint::Rule::NameCollision), 1);
            ensure_equals("reported on the second",
                          ALXUISelection::toString(run.first(ALXUILint::Rule::NameCollision)->path), std::string("a#1"));
        }
    }

    // The reference rules: a font, a colour and a control nobody has, and
    // a callback no registry knows.
    template<> template<>
    void alxuilint_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" font=\"NoSuchFont\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            ensure_equals("dangling font: " + run.describe(), run.count(ALXUILint::Rule::DanglingFont), 1);
        }
        {
            // The names every shipped file uses answer to the registry
            // rather than to the four legacy ones.
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" font=\"SansSerif\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"b\" font=\"SansSerifSmall\" left=\"0\" top=\"30\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"c\" font=\"Monospace\" left=\"0\" top=\"60\" width=\"90\" height=\"20\"/>"));
            ensure_equals("a font fonts.xml declares is not dangling: " + run.describe(),
                          run.count(ALXUILint::Rule::DanglingFont), 0);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" bg_opaque_color=\"NoSuchColour\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            ensure_equals("dangling colour: " + run.describe(), run.count(ALXUILint::Rule::DanglingColor), 1);
        }
        {
            // A literal is not a name and is passed over, and so is a
            // colour the table has.
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" bg_opaque_color=\"1 1 1 1\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
                "  <panel name=\"b\" bg_opaque_color=\"White\" left=\"0\" top=\"40\" width=\"90\" height=\"20\"/>"));
            ensure_equals("neither is dangling: " + run.describe(), run.count(ALXUILint::Rule::DanglingColor), 0);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" control_name=\"NoSuchSetting\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            ensure_equals("control missing: " + run.describe(), run.count(ALXUILint::Rule::ControlMissing), 1);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" control_name=\"UIScrollbarSize\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>"));
            ensure_equals("a setting a group has is not missing: " + run.describe(),
                          run.count(ALXUILint::Rule::ControlMissing), 0);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"20\">\n"
                "    <panel.commit_callback function=\"NoSuch.Function\"/>\n"
                "  </panel>", /*decisive=*/true));
            ensure_equals("callback not registered: " + run.describe(),
                          run.count(ALXUILint::Rule::CallbackNotRegistered), 1);
            ensure_equals("decisive here", (int)run.first(ALXUILint::Rule::CallbackNotRegistered)->severity,
                          (int)ALXUILint::Severity::Error);
        }
        {
            Run run;
            ensure("builds", run.build(
                "  <panel name=\"a\" left=\"0\" top=\"0\" width=\"90\" height=\"20\">\n"
                "    <panel.commit_callback function=\"NoSuch.Function\"/>\n"
                "  </panel>", /*decisive=*/false));
            ensure_equals("advisory in a shell build", (int)run.first(ALXUILint::Rule::CallbackNotRegistered)->severity,
                          (int)ALXUILint::Severity::Note);
        }
    }

    // The counts a collapsed row shows: a finding is counted on its
    // element and on every ancestor.
    template<> template<>
    void alxuilint_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Run run;
        ensure("builds", run.build(
            "  <panel name=\"outer\" left=\"0\" top=\"0\" width=\"150\" height=\"80\">\n"
            "    <panel name=\"t\" font=\"NoSuchFont\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>\n"
            "  </panel>"));
        ensure_equals("one finding: " + run.describe(), (S32)run.lint.findings().size(), 1);
        ensure_equals("on the element", run.lint.countUnder(ALXUISelection::fromString("outer/t")), 1);
        ensure_equals("on its parent", run.lint.countUnder(ALXUISelection::fromString("outer")), 1);
        ensure_equals("and on the root", run.lint.countUnder(ALXUISelection::path_t()), 1);
        ensure_equals("nowhere else", run.lint.countUnder(ALXUISelection::fromString("nobody")), 0);
    }

    // The rules that read the catalog rather than a build: a template
    // written under a root that is not the tag it configures, and a layer
    // that did not parse.
    template<> template<>
    void alxuilint_object::test<6>()
    {
        const std::string skins = gDirUtilp->add(gDirUtilp->getTempDir(), "alxuilint_catalog_test");
        gDirUtilp->deleteDirAndContents(skins);
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
                ensure_equals("the root that was written", f.what, std::string("checkbox"));
                // The parser reads the root's attributes whatever it is
                // called, so this is worth a look and not a defect.
                ensure_equals("a note", (int)f.severity, (int)ALXUILint::Severity::Note);
            }
        }
        ensure_equals("one template written under another tag", mismatches, 1);
        ensure_equals("and one layer that did not parse", parse_errors, 1);

        gDirUtilp->deleteDirAndContents(skins);
    }

    // A layout panel has one dimension the stack reads -- the one along the
    // axis the stack runs -- and three names for it. A name for the other axis
    // sets that same parameter, and two names on one panel are one value.
    template<> template<>
    void alxuilint_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "<layout_stack name=\"stack\" orientation=\"horizontal\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <layout_panel name=\"across\" min_height=\"20\"/>\n"
            "  <layout_panel name=\"twice\" min_width=\"30\" min_height=\"40\"/>\n"
            "  <layout_panel name=\"along\" min_width=\"50\"/>\n"
            "</layout_stack>"));

        ensure_equals(run.describe(), run.count(ALXUILint::Rule::LayoutDimension), 2);

        const ALXUILint::Finding* first = run.first(ALXUILint::Rule::LayoutDimension);
        ensure("the cross-axis name is the one reported", first != nullptr);
        ensure_equals("named by attribute", first->what, std::string("min_height"));
    }

    // An attribute written as the value the element already carries. The
    // schema knows what that is because it serializes the block a widget is
    // built from, so this is a thing the tool could not say at all until it
    // did. Worth a look rather than a fault: a number written where a reader
    // would otherwise have to know it is a reasonable thing to write.
    template<> template<>
    void alxuilint_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "<panel name=\"root\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\" height=\"23\"/>\n"
            "  <button name=\"quiet\" left=\"0\" top=\"40\" width=\"80\"/>\n"
            "</panel>"));

        ensure_equals(run.describe(), run.count(ALXUILint::Rule::WroteTheDefault), 1);
        const ALXUILint::Finding* said = run.first(ALXUILint::Rule::WroteTheDefault);
        ensure("the one that wrote it is the one reported", said != nullptr);
        ensure_equals("named by attribute", said->what, std::string("height"));
        ensure("and it is worth a look rather than a fault",
               said->severity == ALXUILint::Severity::Note);
        ensure("which is one attribute away from not being said at all",
               said->fix.did == ALXUILint::Fix::Do::TakeAttributeOut);
    }

    // A finding that says what right would be. A name one slip from a real
    // one was meant to be that one; a name near nothing is a name nothing
    // reads; and an element outside what holds it moves by a delta the rule
    // has already worked out to say how far outside it is.
    template<> template<>
    void alxuilint_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "<panel name=\"root\" left=\"0\" top=\"0\" width=\"200\" height=\"100\">\n"
            "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tp=\"near\"/>\n"
            "  <button name=\"nothing\" left=\"0\" top=\"30\" width=\"80\" qqzzxw=\"far\"/>\n"
            "  <panel name=\"out\" left=\"150\" top=\"60\" width=\"90\" height=\"20\"/>\n"
            "</panel>"));

        ensure_equals(run.describe(), run.count(ALXUILint::Rule::UnknownAttribute), 2);

        const ALXUILint::Finding* slip = nullptr;
        const ALXUILint::Finding* nothing = nullptr;
        for (const ALXUILint::Finding& f : run.lint.findings())
        {
            if (f.rule != ALXUILint::Rule::UnknownAttribute) { continue; }
            if (f.what == "tool_tp") { slip = &f; }
            if (f.what == "qqzzxw") { nothing = &f; }
        }

        ensure("the near one is found", slip != nullptr);
        ensure("and is offered the spelling it meant",
               slip->fix.did == ALXUILint::Fix::Do::SpellAttribute);
        ensure_equals("which is the real name", slip->fix.spelling, std::string("tool_tip"));

        ensure("the far one is found", nothing != nullptr);
        ensure("and there is nothing to be but gone",
               nothing->fix.did == ALXUILint::Fix::Do::TakeAttributeOut);

        const ALXUILint::Finding* out = run.first(ALXUILint::Rule::OutOfBounds);
        ensure("the one outside is found", out != nullptr);
        ensure("and can be moved in", out->fix.did == ALXUILint::Fix::Do::MoveInside);
        ensure_equals("by exactly how far outside it is", out->fix.dx, -40);
        ensure_equals("and not at all on the axis it is inside on", out->fix.dy, 0);
    }

    // Something wider than what holds it leaves by the far edge whichever
    // way it goes, so there is no move to offer: a fix that does not fix it
    // is worse than none, because it is a button that says it will.
    template<> template<>
    void alxuilint_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "<panel name=\"wider\" left=\"10\" top=\"0\" width=\"300\" height=\"20\"/>"));

        const ALXUILint::Finding* out = run.first(ALXUILint::Rule::OutOfBounds);
        ensure("it is still reported", out != nullptr);
        ensure("and offers nothing", out->fix.did == ALXUILint::Fix::Do::Nothing);
    }

    // A fix that fixes the wrong thing writes its wrongness into the file
    // instead of reporting it, so what each of them puts in the file is
    // watched byte for byte. These two write an attribute.
    template<> template<>
    void alxuilint_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Written as the value it already carries: it comes off, and the
        // whitespace that separated it comes off with it.
        {
            Run run;
            ensure("built", run.build(
                "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\" height=\"23\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::WroteTheDefault);
            ensure("found", f != nullptr);

            ALXUIEdit edit;
            ensure("loads the same bytes", edit.loadBuffer(run.source));
            ensure("fixed", f->applyFix(edit, run.anchorFor(f->path)));
            ensure_equals("the height is gone and nothing else moved", edit.text(),
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <button name=\"said\" left=\"0\" top=\"0\" width=\"80\"/>\n"
                "</panel>\n");
            ensure("as one step", edit.undo());
            ensure_equals("put back whole", edit.text(), run.source);
        }

        // A name one slip from a real one: the name changes where it stands
        // and the author's value is untouched.
        {
            Run run;
            ensure("built", run.build(
                "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tp=\"near\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::UnknownAttribute);
            ensure("found", f != nullptr);
            ensure("and it is the one", f->fix.did == ALXUILint::Fix::Do::SpellAttribute);

            ALXUIEdit edit;
            ensure("loads the same bytes", edit.loadBuffer(run.source));
            ensure("fixed", f->applyFix(edit, run.anchorFor(f->path)));
            ensure_equals("spelt right, in place", edit.text(),
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <button name=\"slip\" left=\"0\" top=\"0\" width=\"80\" tool_tip=\"near\"/>\n"
                "</panel>\n");
            ensure("as one step", edit.undo());
            ensure_equals("put back whole", edit.text(), run.source);
        }
    }

    // And these two write a number, worked out from where the element is in
    // the tree that was built. The attribute that moves is the one the author
    // used: no positioning form is ever converted into another.
    template<> template<>
    void alxuilint_object::test<12>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        {
            Run run;
            ensure("built", run.build(
                "  <panel name=\"out\" left=\"150\" top=\"60\" width=\"90\" height=\"20\"/>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::OutOfBounds);
            ensure("found", f != nullptr);
            ensure_equals("forty pixels over", f->fix.dx, -40);

            ALXUIEdit edit;
            ensure("loads the same bytes", edit.loadBuffer(run.source));
            ensure("fixed", f->applyFix(edit, run.anchorFor(f->path)));
            ensure_equals("the left it was written with is the left that moved", edit.text(),
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <panel name=\"out\" left=\"110\" top=\"60\" width=\"90\" height=\"20\"/>\n"
                "</panel>\n");
            ensure("as one step", edit.undo());
            ensure_equals("put back whole", edit.text(), run.source);
        }

        // How wide the text needs to be is the font's answer and not this
        // test's, so the width asserted is the one the rule worked out. What
        // is being watched is that it lands on `width` and nowhere else.
        {
            Run run;
            ensure("built", run.build(
                "  <text name=\"long\" left=\"0\" top=\"0\" width=\"10\" height=\"16\">A good deal of text</text>"));
            const ALXUILint::Finding* f = run.first(ALXUILint::Rule::Truncation);
            ensure("found: " + run.describe(), f != nullptr);
            ensure("and it can be widened", f->fix.did == ALXUILint::Fix::Do::WidenBy);
            ensure("by something", f->fix.dx > 0);

            ALXUIEdit edit;
            ensure("loads the same bytes", edit.loadBuffer(run.source));
            ensure("fixed", f->applyFix(edit, run.anchorFor(f->path)));
            ensure_equals("only the width moved", edit.text(),
                "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n"
                "  <text name=\"long\" left=\"0\" top=\"0\" width=\"" + std::to_string(10 + f->fix.dx)
                + "\" height=\"16\">A good deal of text</text>\n"
                "</panel>\n");
            ensure("as one step", edit.undo());
            ensure_equals("put back whole", edit.text(), run.source);
        }
    }

    // A parent that lays its children out itself reads no position from them,
    // and the file's own root sits where the tool put it. Both are still
    // reported; neither is offered a fix, because the fix would write a
    // number nothing reads.
    template<> template<>
    void alxuilint_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
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
                ensure("nothing a stack places is offered a move",
                       f.fix.did == ALXUILint::Fix::Do::Nothing);
            }
        }
        ensure("and there is one to have refused: " + run.describe(), placed > 0);
    }

    // A name that exists, works, and should not be used. The block registered
    // two names for one parameter -- which said that both work and will keep
    // working -- and a person wrote down which of them to write. Nothing is
    // broken, so it is worth a look rather than a fault, and it is one edit
    // from being right.
    template<> template<>
    void alxuilint_object::test<14>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "  <text name=\"old\" left=\"0\" top=\"0\" width=\"80\" height=\"20\""
            " word_wrap=\"true\">Hello</text>\n"
            "  <text name=\"new\" left=\"0\" top=\"30\" width=\"80\" height=\"20\""
            " wrap=\"true\">Hello</text>"));

        ensure_equals(run.describe(), run.count(ALXUILint::Rule::DeprecatedAttribute), 1);
        const ALXUILint::Finding* f = run.first(ALXUILint::Rule::DeprecatedAttribute);
        ensure("found", f != nullptr);
        ensure_equals("the old spelling is the one reported", f->what, std::string("word_wrap"));
        ensure("nothing is broken, so it is worth a look",
               f->severity == ALXUILint::Severity::Note);
        ensure("and it offers the name to write instead",
               f->fix.did == ALXUILint::Fix::Do::SpellAttribute);
        ensure_equals("which is the one the notes name", f->fix.spelling, std::string("wrap"));

        // Applied, it is the same edit as any other spelling fix.
        ALXUIEdit edit;
        ensure("loads the same bytes", edit.loadBuffer(run.source));
        ensure("fixed", f->applyFix(edit, run.anchorFor(f->path)));
        ensure("the old name is gone",
               edit.text().find("word_wrap=\"true\"") == std::string::npos);
        ensure("and the new one is where it was",
               edit.text().find("wrap=\"true\">Hello</text>") != std::string::npos);
    }

    // An element already carrying both names is not one to write a second of:
    // two of a name on one element is one value chosen by the parser.
    template<> template<>
    void alxuilint_object::test<15>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        Run run;
        ensure("built", run.build(
            "  <text name=\"both\" left=\"0\" top=\"0\" width=\"80\" height=\"20\""
            " word_wrap=\"true\" wrap=\"true\">Hello</text>"));

        const ALXUILint::Finding* f = run.first(ALXUILint::Rule::DeprecatedAttribute);
        ensure("still reported", f != nullptr);
        ensure("but there is nothing to offer", f->fix.did == ALXUILint::Fix::Do::Nothing);
    }

    // A file an element names is a file the catalog has or has not. The
    // rule was written after a gate that turns any value with a dot in it
    // away as a path, and every file name has one, so it never ran.
    //
    // A panel naming a file builds only if the file loads, and it loads from
    // the skins the fixture stands on rather than from the catalog asked
    // here: so the file named is a real one, the catalog is a directory of
    // this test's own, and whether the catalog has the name is the question.
    template<> template<>
    void alxuilint_object::test<16>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string skins = gDirUtilp->add(gDirUtilp->getTempDir(), "alxuilint_file_test");
        gDirUtilp->deleteDirAndContents(skins);
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
        ensure("the catalog has the file", has_it.find(REAL) != nullptr);
        ALXUICatalog has_not;

        const std::string body = std::string("  <panel name=\"a\" filename=\"") + REAL
            + "\" left=\"0\" top=\"0\" width=\"90\" height=\"20\"/>";
        {
            Run run;
            run.catalog = &has_it;
            ensure("builds", run.build(body));
            ensure("the panel was built", run.panel->getChildCount() > 0);
            ensure_equals("a file the catalog has: " + run.describe(), run.count(ALXUILint::Rule::FileMissing), 0);
        }
        {
            Run run;
            run.catalog = &has_not;
            ensure("builds", run.build(body));
            ensure_equals("and one it has not: " + run.describe(), run.count(ALXUILint::Rule::FileMissing), 1);
            ensure_equals("named", run.first(ALXUILint::Rule::FileMissing)->what, std::string("filename"));
        }
        gDirUtilp->deleteDirAndContents(skins);
    }
}

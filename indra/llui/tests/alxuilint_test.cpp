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
    // fonts stand up without their textures. The truncation rule is the
    // one this cannot reach, and the viewer's Lint all is where it runs.
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

            ~Run() { delete panel; }

            bool build(const std::string& body, bool decisive = false)
            {
                // The root says topleft, as every shipped file does; a
                // file that does not is laid out from the bottom.
                const std::string xml =
                    "<panel name=\"root\" layout=\"topleft\" width=\"200\" height=\"100\">\n" + body + "\n</panel>\n";
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
}

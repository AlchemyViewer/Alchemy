/**
 * @file alxuidiagnostics_test.cpp
 * @brief The diagnostics sink sees what the parser and factory keep quiet about.
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

#include "alxuidiagnostics.h"
#include "../llfloater.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace
{
    struct alxuidiagnostics_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // One attribute the floater does not know, one on a nested element
        // that the parser would keep quiet about, one child widget whose
        // attributes fail against the floater by design, one dotted element
        // in the wrong scope, and one element that is not a widget at all.
        // Nothing here measures text: the headless fixture has no glyphs.
        static constexpr const char* XUI =
            "<floater name=\"f\" width=\"200\" height=\"100\" bogus_top=\"1\">"
            "<floater.string name=\"s\" bogus_nested=\"1\" translate=\"false\">text</floater.string>"
            "<panel name=\"inner\" width=\"10\" height=\"10\"/>"
            "<panel.string name=\"p\">text</panel.string>"
            "<nonesuch name=\"n\"/>"
            "</floater>";

        struct TestFloater : public LLFloater
        {
            TestFloater(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
        };

        static LLFloaterView* floaterView()
        {
            LLFloaterView::Params p;
            p.name = "floater_view";
            p.rect = LLRect(0, 600, 800, 0);
            return LLUICtrlFactory::create<LLFloaterView>(p);
        }

        // A floater is not a child tag; the viewer builds one from a node
        // through initFloaterXML with the file name pushed on the factory,
        // which is where the children's parsers and the factory's own
        // reports read it from, and so does this.
        static LLFloater* build(LLFloaterView* parent)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(XUI, std::strlen(XUI), root))
            {
                return nullptr;
            }
            LLFloater* floater = new TestFloater(LLFloater::getDefaultParams());
            LLUICtrlFactory::instance().pushFileName("diag_test.xml");
            const bool ok = floater->initFloaterXML(root, parent, "diag_test.xml");
            LLUICtrlFactory::instance().popFileName();
            if (!ok)
            {
                delete floater;
                return nullptr;
            }
            return floater;
        }

        static bool has(const ALXUIDiagnostics& sink, ALXUIDiagnostics::Kind kind, S32 depth, const char* path)
        {
            for (const ALXUIDiagnostics::Entry& e : sink.entries())
            {
                if (e.kind == kind && e.depth == depth && e.path == path)
                {
                    return true;
                }
            }
            return false;
        }
    };

    // With a sink alive, a build reports the attribute the viewer would log,
    // the nested one it would not, the mis-scoped dotted element, and the
    // element the factory cannot build; each carries the file it came from.
    TEST_CASE_METHOD(alxuidiagnostics_data, "alxuidiagnostics: a live sink collects each finding of a build with its file", "[alxuidiagnostics]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        {
            ALXUIDiagnostics sink;
            UNSCOPED_INFO("the sink is active while alive");
            CHECK(ALXUIDiagnostics::active() == &sink);

            LLFloater* view = build(fv.get());
            UNSCOPED_INFO("the floater builds in spite of its problems");
            REQUIRE(view != nullptr);

            UNSCOPED_INFO("the floater's own unknown attribute is reported at depth 0");
            CHECK(has(sink, ALXUIDiagnostics::Kind::UnknownAttribute, 0, "bogus_top"));
            UNSCOPED_INFO("the nested element's unknown attribute is reported at depth 1");
            CHECK(has(sink, ALXUIDiagnostics::Kind::UnknownAttribute, 1, "string.bogus_nested"));
            UNSCOPED_INFO("a dotted element in the wrong scope is reported");
            CHECK(has(sink, ALXUIDiagnostics::Kind::MisScopedElement, 1, "panel.string"));
            UNSCOPED_INFO("an element that is not a widget is reported by the factory");
            CHECK(has(sink, ALXUIDiagnostics::Kind::CreateFailed, 0, "nonesuch"));
            // translate is a cue for translation tools, carried by 642
            // strings in the default skin; the string block swallows it
            // as LLView does, so it is not a finding.
            UNSCOPED_INFO("translate on a string is not an unknown attribute");
            CHECK_FALSE(has(sink, ALXUIDiagnostics::Kind::UnknownAttribute, 1, "string.translate"));

            // The parser names the file it was handed. The factory names its
            // current file as the skin resolves it, and a file that is not on
            // disk resolves to nothing; in the viewer every file is.
            for (const ALXUIDiagnostics::Entry& e : sink.entries())
            {
                const bool from_factory = e.kind == ALXUIDiagnostics::Kind::InvalidChild
                                       || e.kind == ALXUIDiagnostics::Kind::CreateFailed;
                INFO("file on " << ALXUIDiagnostics::kindName(e.kind) << " " << e.path);
                REQUIRE(e.file == (from_factory ? std::string() : std::string("diag_test.xml")));
            }
        }
        UNSCOPED_INFO("the sink detaches when it dies");
        CHECK(ALXUIDiagnostics::active() == nullptr);

        fv.reset();
    }

    // The attributes of a child widget fail against the parent's parameter
    // block by design and are parsed again by the child's own parser; the
    // sink sees them under the child's tag so a reader can set them aside,
    // and never as the child's own at depth 0.
    TEST_CASE_METHOD(alxuidiagnostics_data, "alxuidiagnostics: a child widget's attributes show as the parent's misses not its own", "[alxuidiagnostics]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        ALXUIDiagnostics sink;
        LLFloater* view = build(fv.get());
        REQUIRE(view != nullptr);

        UNSCOPED_INFO("the child's known attribute shows up only as the parent's miss");
        CHECK(has(sink, ALXUIDiagnostics::Kind::UnknownAttribute, 1, "panel.name"));
        UNSCOPED_INFO("and not as the child's own");
        CHECK_FALSE(has(sink, ALXUIDiagnostics::Kind::UnknownAttribute, 0, "name"));

        fv.reset();
    }
}

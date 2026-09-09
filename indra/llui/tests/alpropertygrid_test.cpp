/**
 * @file alpropertygrid_test.cpp
 * @brief Where a property grid's rows are, and that they stay there.
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

#include "../alpropertygrid.h"

#include "../llaccordionctrl.h"
#include "../llaccordionctrltab.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gPropertyGridTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gPropertyGridTestAnonName;
}

namespace tut
{
    struct alpropertygrid_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr S32 ROW = 22;

        static ALPropertyGrid* build(S32 width = 400, S32 height = 500)
        {
            ALPropertyGrid::Params p(LLUICtrlFactory::getDefaultParams<ALPropertyGrid>());
            p.name = "grid";
            p.rect = LLRect(0, height, width, 0);
            p.row_height = ROW;
            return LLUICtrlFactory::create<ALPropertyGrid>(p);
        }

        static ALPropertyGrid::Field field(const std::string& name, S32 group)
        {
            ALPropertyGrid::Field f;
            f.name = name;
            f.value = "0";
            f.source = "base";
            f.authored = true;
            f.kind = ALParamType::OTHER;
            f.group = group;
            return f;
        }

        // Each row of a section, in the coordinates of the panel that stacks
        // them -- and, on the way, that a row's own parts are inside it.
        static std::vector<LLRect> rowsOf(ALPropertyGrid* grid, const std::string& group,
                                          const std::vector<std::string>& names)
        {
            LLPanel* rows = grid->getChild<LLPanel>(group + "_rows", true);
            std::vector<LLRect> out;
            for (const std::string& name : names)
            {
                LLPanel* row = rows->getChild<LLPanel>(name + "_row", true);
                const LLRect label = row->getChild<LLTextBox>(name + "_label", true)->getRect();
                ensure(name + ": its label " + where(label) + " is inside the row "
                           + where(row->getLocalRect()),
                       row->getLocalRect().contains(label));
                out.push_back(row->getRect());
            }
            return out;
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }

        // Rows run from the top of their panel, one row-height apart, and none
        // of them is outside it. Everything the grid draws depends on this and
        // nothing else re-establishes it.
        static void ensureStacked(const std::string& what, LLPanel* rows,
                                  const std::vector<LLRect>& labels)
        {
            const LLRect within = rows->getLocalRect();
            for (size_t i = 0; i < labels.size(); ++i)
            {
                const LLRect& r = labels[i];
                ensure(what + ": row " + std::to_string(i) + " at " + where(r)
                           + " is inside " + where(within),
                       within.contains(r));
                if (i > 0)
                {
                    ensure_equals(what + ": row " + std::to_string(i) + " is one row below row "
                                      + std::to_string(i - 1),
                                  labels[i - 1].mTop - r.mTop, ROW);
                }
            }
        }
    };

    typedef test_group<alpropertygrid_data>  alpropertygrid_test;
    typedef alpropertygrid_test::object      alpropertygrid_object;
    tut::alpropertygrid_test alpropertygrid_testgroup("alpropertygrid");

    // Freshly filled, the rows of a section are stacked from its top.
    template<> template<>
    void alpropertygrid_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        ensureStacked("identity", grid->getChild<LLPanel>("identity_rows", true),
                      rowsOf(grid, "identity", { "name", "value" }));
        ensureStacked("position", grid->getChild<LLPanel>("position_rows", true),
                      rowsOf(grid, "position", { "left", "top", "width" }));
        grid->die();
    }

    // And they are still stacked from its top after the grid is resized, which
    // is the whole reason a resize no longer rebuilds them.
    template<> template<>
    void alpropertygrid_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        for (auto size : { std::make_pair(300, 500), std::make_pair(700, 500),
                           std::make_pair(400, 200), std::make_pair(400, 900) })
        {
            grid->reshape(size.first, size.second);
            const std::string what = std::to_string(size.first) + "x" + std::to_string(size.second);
            ensureStacked(what + " identity", grid->getChild<LLPanel>("identity_rows", true),
                          rowsOf(grid, "identity", { "name", "value" }));
            ensureStacked(what + " position", grid->getChild<LLPanel>("position_rows", true),
                          rowsOf(grid, "position", { "left", "top", "width" }));
        }
        grid->die();
    }

    // A section's panel is exactly as tall as the rows it holds, so the tab it
    // is in is told the right height and nothing is drawn outside it.
    template<> template<>
    void alpropertygrid_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        ensure_equals("two rows", grid->getChild<LLPanel>("identity_rows", true)->getRect().getHeight(),
                      2 * ROW);
        ensure_equals("three rows", grid->getChild<LLPanel>("position_rows", true)->getRect().getHeight(),
                      3 * ROW);
        grid->die();
    }

    // The panel a section's rows are in belongs to its tab: it may not stick
    // out of it, in either direction, or it draws over the section above.
    template<> template<>
    void alpropertygrid_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        for (const std::string& group : { std::string("identity"), std::string("position") })
        {
            LLAccordionCtrlTab* tab = grid->getChild<LLAccordionCtrlTab>(group, true);
            LLPanel* rows = grid->getChild<LLPanel>(group + "_rows", true);
            ensure(group + ": the panel " + where(rows->getRect()) + " is inside its tab "
                       + where(tab->getLocalRect()),
                   tab->getLocalRect().contains(rows->getRect()));
        }
        grid->die();
    }

    // Two fields that are the same thought share a row: left and top are a
    // position, and reading them on one line is how anybody says it. The
    // named half gets no row of its own, and the section is as tall as the
    // rows it will hold rather than the fields it was given.
    template<> template<>
    void alpropertygrid_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "position" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("left", 0));
        fields.back().pairWith = "top";
        fields.push_back(field("top", 0));
        fields.push_back(field("name", 0));
        grid->setFields(fields);

        LLPanel* rows = grid->getChild<LLPanel>("position_rows", true);
        ensure("the pair is one row", rows->findChild<LLPanel>("left_row", true) != nullptr);
        ensure("and the other half has none of its own",
               rows->findChild<LLPanel>("top_row", true) == nullptr);
        ensure("the unpaired field still has one",
               rows->findChild<LLPanel>("name_row", true) != nullptr);

        // Both editors are on the one row, and inside it.
        LLPanel* row = rows->getChild<LLPanel>("left_row", true);
        LLView* first = row->findChild<LLView>("left", true);
        LLView* second = row->findChild<LLView>("top", true);
        ensure("the first editor is on the row", first != nullptr);
        ensure("and so is the second", second != nullptr);
        ensure("the first is inside it " + where(first->getRect()),
               row->getLocalRect().contains(first->getRect()));
        ensure("the second is inside it " + where(second->getRect()),
               row->getLocalRect().contains(second->getRect()));
        ensure("and the second is to the right of the first",
               second->getRect().mLeft >= first->getRect().mRight);

        // Two rows, not three: the section is as tall as what it shows.
        ensure_equals("the section holds two rows", rows->getRect().getHeight(), 2 * ROW);
        grid->die();
    }

    // A field whose value is a set of edges is a picture, and a picture
    // needs a taller row than a value does. The rows below it are stacked
    // under what it actually took, not under a row height.
    template<> template<>
    void alpropertygrid_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "position" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("left", 0));
        fields.push_back(field("follows", 0));
        fields.back().edges = { "left", "bottom", "right", "top" };
        fields.back().value = "left|top";
        fields.push_back(field("name", 0));
        grid->setFields(fields);

        // Rows are shown in the order the grid sorts them into, which for
        // three fields nobody grouped apart is by name.
        LLPanel* rows = grid->getChild<LLPanel>("position_rows", true);
        const LLRect picture = rows->getChild<LLPanel>("follows_row", true)->getRect();
        const LLRect second = rows->getChild<LLPanel>("left_row", true)->getRect();
        const LLRect last = rows->getChild<LLPanel>("name_row", true)->getRect();

        ensure_equals("an ordinary row is one row", second.getHeight(), ROW);
        ensure("a picture takes more than one " + where(picture), picture.getHeight() > ROW);
        ensure_equals("the picture starts at the top of the section",
                      picture.mTop, rows->getRect().getHeight());
        ensure_equals("the row after it begins where it ended", second.mTop, picture.mBottom);
        ensure_equals("and the one after that where the second ended", last.mTop, second.mBottom);
        ensure_equals("the section is as tall as the three of them",
                      rows->getRect().getHeight(), picture.getHeight() + second.getHeight() + last.getHeight());

        // The label still sits in the ordinary row at the top of the tall
        // one, so the label column reads straight down the pane.
        LLPanel* row = rows->getChild<LLPanel>("follows_row", true);
        const LLRect label = row->getChild<LLTextBox>("follows_label", true)->getRect();
        ensure("the label is in the top row of it " + where(label),
               label.mBottom >= picture.getHeight() - ROW && label.mTop <= picture.getHeight());
        ensure("the picture is on the row " + where(row->getLocalRect()),
               row->findChild<LLView>("follows", true) != nullptr);
        grid->die();
    }

    // A value some other layer writes as well is marked beside its row, and
    // the mark says where. A value nobody disagrees about is not marked: a
    // mark on every row is a column, which is what this replaced.
    template<> template<>
    void alpropertygrid_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        fields.push_back(field("label", 0));
        fields.back().alsoWritten = { "ja = Los", "gemini = Go on" };
        grid->setFields(fields);

        LLPanel* rows = grid->getChild<LLPanel>("identity_rows", true);
        LLUICtrl* mark = rows->findChild<LLUICtrl>("label_gutter", true);
        ensure("the row somebody disagrees about is marked", mark != nullptr);
        ensure("and the one nobody does is not",
               rows->findChild<LLUICtrl>("name_gutter", true) == nullptr);
        ensure("the mark says both places", mark->getToolTip().find("gemini") != std::string::npos
                                         && mark->getToolTip().find("ja") != std::string::npos);
        ensure("it is in the margin beside the label " + where(mark->getRect()),
               mark->getRect().mRight <= rows->getChild<LLPanel>("label_row", true)
                   ->getChild<LLTextBox>("label_label", true)->getRect().mLeft);

        // Clicking it is how the caller is asked to show where else.
        std::string asked;
        grid->onFieldGutter([&asked](const std::string& name) { asked = name; });
        mark->handleMouseDown(1, 1, MASK_NONE);
        ensure_equals("the click names the field", asked, std::string("label"));
        grid->die();
    }
}

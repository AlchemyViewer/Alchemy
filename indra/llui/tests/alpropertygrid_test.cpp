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
}

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

#include "alpropertygrid.h"

#include "../llbutton.h"
#include "../lllineeditor.h"
#include "alcolorfield.h"
#include "../llaccordionctrltab.h"

#include "../llaccordionctrl.h"
#include "../llaccordionctrltab.h"
#include "../llspinctrl.h"
#include "../lltextbox.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"
#include "llcallbacklist.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
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
        static std::vector<LLRect> require_rows_of(ALPropertyGrid* grid, const std::string& group,
                                                   const std::vector<std::string>& names)
        {
            LLPanel* rows = grid->getChild<LLPanel>(group + "_rows", true);
            std::vector<LLRect> out;
            for (const std::string& name : names)
            {
                LLPanel* row = rows->getChild<LLPanel>(name + "_row", true);
                const LLRect label = row->getChild<LLTextBox>(name + "_label", true)->getRect();
                CAPTURE(name, label, row->getLocalRect());
                REQUIRE(row->getLocalRect().contains(label));
                out.push_back(row->getRect());
            }
            return out;
        }

        // Rows run from the top of their panel, one row-height apart, and none
        // of them is outside it. Everything the grid draws depends on this and
        // nothing else re-establishes it.
        static void require_stacked(const std::string& what, LLPanel* rows,
                                    const std::vector<LLRect>& labels)
        {
            INFO(what);
            const LLRect within = rows->getLocalRect();
            for (size_t i = 0; i < labels.size(); ++i)
            {
                const LLRect& r = labels[i];
                CAPTURE(i, r, within);
                REQUIRE(within.contains(r));
                if (i == 0)
                {
                    REQUIRE(r.mTop == within.mTop);
                }
                else
                {
                    REQUIRE(labels[i - 1].mTop - r.mTop == ROW);
                }
            }
        }
    };

    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: freshly filled rows stack one row apart from the top of their section", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        require_stacked("identity", grid->getChild<LLPanel>("identity_rows", true),
                        require_rows_of(grid, "identity", { "name", "value" }));
        require_stacked("position", grid->getChild<LLPanel>("position_rows", true),
                        require_rows_of(grid, "position", { "left", "top", "width" }));
        grid->die();
    }

    // And they are still stacked from its top after the grid is resized, which
    // is the whole reason a resize no longer rebuilds them.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: rows stay stacked from the top of their section after a resize", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        for (auto size : { std::make_pair(300, 500), std::make_pair(700, 500),
                           std::make_pair(400, 200), std::make_pair(400, 900) })
        {
            CAPTURE(size.first, size.second);
            grid->reshape(size.first, size.second);
            require_stacked("identity", grid->getChild<LLPanel>("identity_rows", true),
                            require_rows_of(grid, "identity", { "name", "value" }));
            require_stacked("position", grid->getChild<LLPanel>("position_rows", true),
                            require_rows_of(grid, "position", { "left", "top", "width" }));
        }
        grid->die();
    }

    // A section's panel is exactly as tall as the rows it holds, so the tab it
    // is in is told the right height and nothing is drawn outside it.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a section's panel is exactly as tall as its rows", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        CHECK(grid->getChild<LLPanel>("identity_rows", true)->getRect().getHeight() ==
                      2 * ROW);
        CHECK(grid->getChild<LLPanel>("position_rows", true)->getRect().getHeight() ==
                      3 * ROW);
        grid->die();
    }

    // The panel a section's rows are in belongs to its tab: it may not stick
    // out of it, in either direction, or it draws over the section above.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a section's rows panel lies inside its tab", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });
        grid->setFields({ field("name", 0), field("value", 0),
                          field("left", 1), field("top", 1), field("width", 1) });

        for (const std::string& group : { std::string("identity"), std::string("position") })
        {
            LLAccordionCtrlTab* tab = grid->getChild<LLAccordionCtrlTab>(group, true);
            LLPanel* rows = grid->getChild<LLPanel>(group + "_rows", true);
            CAPTURE(group, rows->getRect(), tab->getLocalRect());
            REQUIRE(tab->getLocalRect().contains(rows->getRect()));
        }
        grid->die();
    }

    // Two fields that are the same thought share a row: left and top are a
    // position, and reading them on one line is how anybody says it. The
    // named half gets no row of its own, and the section is as tall as the
    // rows it will hold rather than the fields it was given.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a paired field shares its partner's row", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        UNSCOPED_INFO("the pair is one row");
        REQUIRE(rows->findChild<LLPanel>("left_row", true) != nullptr);
        UNSCOPED_INFO("and the other half has none of its own");
        CHECK(rows->findChild<LLPanel>("top_row", true) == nullptr);
        UNSCOPED_INFO("the unpaired field still has one");
        CHECK(rows->findChild<LLPanel>("name_row", true) != nullptr);

        // Both editors are on the one row, and inside it.
        LLPanel* row = rows->getChild<LLPanel>("left_row", true);
        LLView* first = row->findChild<LLView>("left", true);
        LLView* second = row->findChild<LLView>("top", true);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        UNSCOPED_INFO("the first editor " << first->getRect() << " is inside the row");
        CHECK(row->getLocalRect().contains(first->getRect()));
        UNSCOPED_INFO("the second editor " << second->getRect() << " is inside the row");
        CHECK(row->getLocalRect().contains(second->getRect()));
        CHECK(second->getRect().mLeft >= first->getRect().mRight);

        // Two rows, not three: the section is as tall as what it shows.
        CHECK(rows->getRect().getHeight() == 2 * ROW);
        grid->die();
    }

    // A field whose value is a set of edges is a picture, and a picture
    // needs a taller row than a value does. The rows below it are stacked
    // under what it actually took, not under a row height.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: an edges field draws as a taller picture row", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        CHECK(second.getHeight() == ROW);
        CHECK(picture.getHeight() > ROW);
        UNSCOPED_INFO("the picture starts at the top of the section");
        CHECK(picture.mTop == rows->getRect().getHeight());
        UNSCOPED_INFO("the row after it begins where it ended");
        CHECK(second.mTop == picture.mBottom);
        UNSCOPED_INFO("and the one after that where the second ended");
        CHECK(last.mTop == second.mBottom);
        CHECK(rows->getRect().getHeight() == picture.getHeight() + second.getHeight() + last.getHeight());

        // The label still sits in the ordinary row at the top of the tall
        // one, so the label column reads straight down the pane.
        LLPanel* row = rows->getChild<LLPanel>("follows_row", true);
        const LLRect label = row->getChild<LLTextBox>("follows_label", true)->getRect();
        {
            INFO("the label " << label << " is in the top row of it");
            CHECK(label.mBottom >= picture.getHeight() - ROW);
            CHECK(label.mTop <= picture.getHeight());
        }
        CHECK(row->findChild<LLView>("follows", true) != nullptr);
        grid->die();
    }

    // A value some other layer writes as well is marked beside its row, and
    // the mark says where. A value nobody disagrees about is not marked: a
    // mark on every row is a column, which is what this replaced.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a value other layers also write gets a gutter mark naming them", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        UNSCOPED_INFO("the row somebody disagrees about is marked");
        REQUIRE(mark != nullptr);
        UNSCOPED_INFO("and the one nobody does is not");
        CHECK(rows->findChild<LLUICtrl>("name_gutter", true) == nullptr);
        {
            INFO("the mark says both places");
            CHECK(mark->getToolTip().find("gemini") != std::string::npos);
            CHECK(mark->getToolTip().find("ja") != std::string::npos);
        }
        UNSCOPED_INFO("it is in the margin beside the label: " << mark->getRect());
        CHECK(mark->getRect().mRight <= rows->getChild<LLPanel>("label_row", true)
                   ->getChild<LLTextBox>("label_label", true)->getRect().mLeft);

        // Clicking it is how the caller is asked to show where else.
        std::string asked;
        grid->onFieldGutter([&asked](const std::string& name) { asked = name; });
        mark->handleMouseDown(1, 1, MASK_NONE);
        UNSCOPED_INFO("the click names the field");
        CHECK(asked == std::string("label"));
        grid->die();
    }

    // A row says what it is wherever the pointer rests along it, in the
    // caller's words: the two things that have to be said rather than shown
    // are which layer wrote what is in force, and that on an unwritten row
    // nobody wrote it at all.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a row's tooltips say its type and source in the caller's words", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        ALPropertyGrid::Tips tips;
        tips.field = "[NAME]";
        tips.fieldTyped = "[NAME] is a [TYPE]";
        tips.source = "written in [SOURCE]";
        tips.unwritten = "nobody wrote this";
        tips.remove = "take [NAME] out again";
        grid->setTips(tips);

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("width", 0));
        fields.back().type = "S32";
        fields.push_back(field("height", 0));
        fields.back().type = "S32";
        fields.back().authored = false;
        fields.back().source.clear();
        grid->setFields(fields);

        LLPanel* rows = grid->getChild<LLPanel>("identity_rows", true);
        LLPanel* row = rows->getChild<LLPanel>("width_row", true);
        const std::string tip = row->getChild<LLTextBox>("width_label", true)->getToolTip();
        UNSCOPED_INFO("it names the field: " << tip);
        CHECK(tip.find("width is a S32") != std::string::npos);
        UNSCOPED_INFO("and says where what is in force came from: " << tip);
        CHECK(tip.find("written in base") != std::string::npos);
        UNSCOPED_INFO("a row somebody wrote does not say nobody did: " << tip);
        CHECK(tip.find("nobody wrote this") == std::string::npos);

        // The editor answers the same question as the label.
        UNSCOPED_INFO("the editor says what the label says");
        CHECK(row->getChild<LLView>("width", true)->getToolTip() == tip);
        UNSCOPED_INFO("and the way back says what it takes out");
        CHECK(row->getChild<LLView>("width_remove", true)->getToolTip()
                   .find("take width out again") != std::string::npos);

        // The row nobody wrote says so, and has no way back to offer.
        LLPanel* unwritten = rows->getChild<LLPanel>("height_row", true);
        const std::string quiet = unwritten->getChild<LLTextBox>("height_label", true)->getToolTip();
        UNSCOPED_INFO("an unwritten row says nobody wrote it: " << quiet);
        CHECK(quiet.find("nobody wrote this") != std::string::npos);
        UNSCOPED_INFO("and it names no layer: " << quiet);
        CHECK(quiet.find("written in") == std::string::npos);
        UNSCOPED_INFO("nor is there anything to take out");
        CHECK_FALSE(unwritten->getChild<LLView>("height_remove", true)->getVisible());
        grid->die();
    }

    // A number is stepped by its arrows, by the wheel over it and by a scrub
    // along it, and all three take the one step its spinner is given, which
    // is what this checks. The gestures are LLSpinCtrl's own, and its arrows
    // and wheel read the keyboard's modifiers, which a headless test has no
    // keyboard for. A field carrying no decimals rounds whatever the step
    // leaves it at, so a step of less than one would land back on the number
    // it started from: the arrows would write what is already there and only
    // the keyboard could change anything.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a whole number is given a step of at least one and a real less", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("width", 0));
        fields.back().kind = ALParamType::INTEGER;
        fields.back().value = "744";
        fields.push_back(field("alpha", 0));
        fields.back().kind = ALParamType::REAL;
        fields.back().value = "0.5";
        grid->setFields(fields);

        LLPanel* rows = grid->getChild<LLPanel>("identity_rows", true);
        LLSpinCtrl* whole = rows->getChild<LLPanel>("width_row", true)
                                ->getChild<LLSpinCtrl>("width", true);
        CHECK(whole->getIncrement() >= 1.f);

        LLSpinCtrl* real = rows->getChild<LLPanel>("alpha_row", true)
                               ->getChild<LLSpinCtrl>("alpha", true);
        CHECK(real->getIncrement() > 0.f);
        CHECK(real->getIncrement() < 1.f);
        grid->die();
    }

    // A value that is several numbers, edited as several numbers. Four in a
    // string is four numbers a reader has to count and a writer has to keep
    // in order; a box each, captioned, is the same value said so that both
    // are obvious.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a several-number value gets a captioned box per part", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("rect", 0));
        fields.back().kind = ALParamType::INTEGER;
        fields.back().components = { "L", "T", "R", "B" };
        fields.back().value = "10 20 30 40";
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)
                           ->getChild<LLPanel>("rect_row", true);

        // One box per part, each holding its own number, each captioned with
        // what that part is called.
        const char* parts[] = { "L", "T", "R", "B" };
        const F32 wanted[] = { 10.f, 20.f, 30.f, 40.f };
        for (S32 i = 0; i < 4; ++i)
        {
            const std::string name = std::string("rect.") + parts[i];
            CAPTURE(name);
            LLSpinCtrl* box = row->getChild<LLSpinCtrl>(name, true);
            REQUIRE(box != nullptr);
            REQUIRE((F32)box->getValue().asReal() == wanted[i]);
            REQUIRE(row->findChild<LLView>(name + "_caption", true) != nullptr);
        }

        // Every box commits the whole value, because the field is one field.
        std::string said_name;
        std::string said_value;
        grid->onFieldCommit([&](const std::string& n, const std::string& v)
        {
            said_name = n;
            said_value = v;
        });
        row->getChild<LLSpinCtrl>("rect.R", true)->setValue(99);
        row->getChild<LLSpinCtrl>("rect.R", true)->onCommit();
        UNSCOPED_INFO("the field, not the part");
        CHECK(said_name == std::string("rect"));
        UNSCOPED_INFO("and the whole of it");
        CHECK(said_value == std::string("10 20 99 40"));

        // A row of parts is taller than a row of one value, since each part
        // carries a line saying which it is.
        std::vector<ALPropertyGrid::Field> plain;
        plain.push_back(field("width", 0));
        plain.back().kind = ALParamType::INTEGER;
        plain.back().value = "744";
        ALPropertyGrid* other = build();
        other->setGroups({ "identity" });
        other->setFields(plain);
        const S32 tall = row->getRect().getHeight();
        const S32 ordinary = other->getChild<LLPanel>("identity_rows", true)
                                  ->getChild<LLPanel>("width_row", true)->getRect().getHeight();
        CHECK(tall > ordinary);
        other->die();
        grid->die();
    }

    // A value arriving with commas between its parts is the same value: an
    // LLSD one is written that way and a file writes spaces.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: commas between parts split them and missing parts are nought", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("colour", 0));
        fields.back().kind = ALParamType::REAL;
        fields.back().components = { "R", "G", "B" };
        fields.back().value = "0.5, 0.25, 1.0";
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)
                           ->getChild<LLPanel>("colour_row", true);
        UNSCOPED_INFO("split on the commas");
        CHECK((F32)row->getChild<LLSpinCtrl>("colour.G", true)->getValue().asReal() == 0.25f);

        // Fewer parts than boxes is nought in the rest rather than a hole.
        std::vector<ALPropertyGrid::Field> short_one;
        short_one.push_back(field("short", 0));
        short_one.back().kind = ALParamType::REAL;
        short_one.back().components = { "X", "Y", "Z" };
        short_one.back().value = "1 2";
        grid->setFields(short_one);
        CHECK((F32)grid->getChild<LLPanel>("identity_rows", true)
                               ->getChild<LLPanel>("short_row", true)
                               ->getChild<LLSpinCtrl>("short.Z", true)->getValue().asReal() == 0.f);
        grid->die();
    }

    // A grid of one section has nothing to fold it away from, so the heading
    // is a bar with no job: it names the only thing there and offers to hide
    // the only thing there. And a section with no heading is as tall as its
    // rows -- the tab used to be told to leave room for a header it was not
    // drawing, and handed the difference to its panel as blank space.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a lone section has no heading and two sections have them", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* one = build();
        one->setGroups({ "only" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        one->setFields(fields);

        LLAccordionCtrlTab* tab = one->getChild<LLAccordionCtrlTab>("only", true);
        REQUIRE(tab != nullptr);
        CHECK(tab->getHeaderHeight() == 0);
        UNSCOPED_INFO("so it is as tall as its one row");
        CHECK(one->getChild<LLPanel>("only_rows", true)->getRect().getHeight() == ROW);

        // Two sections is where a heading starts doing something: it says
        // which of them you are looking at, and folds one away from the other.
        ALPropertyGrid* two = build();
        two->setGroups({ "first", "second" });
        std::vector<ALPropertyGrid::Field> both;
        both.push_back(field("name", 0));
        both.push_back(field("width", 1));
        two->setFields(both);
        UNSCOPED_INFO("a heading over the first");
        CHECK(two->getChild<LLAccordionCtrlTab>("first", true)->getHeaderHeight() > 0);
        UNSCOPED_INFO("and one over the second");
        CHECK(two->getChild<LLAccordionCtrlTab>("second", true)->getHeaderHeight() > 0);

        one->die();
        two->die();
    }

    // A colour is several numbers and is not a row of boxes: it has a swatch
    // and a picker. A caller that names its parts as well -- because the
    // value really is four numbers -- gets the better editor rather than the
    // more literal one, and does not get the caption line either.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a colour with parts gets the colour field and not boxes", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("tint", 0));
        fields.back().kind = ALParamType::REAL;
        fields.back().type = "LLColor4";
        fields.back().components = { "R", "G", "B", "A" };
        fields.back().value = "0.5 0.25 1 1";
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)
                           ->getChild<LLPanel>("tint_row", true);
        CHECK(row->findChild<ALColorField>("tint", true) != nullptr);
        CHECK(row->findChild<LLSpinCtrl>("tint.R", true) == nullptr);
        UNSCOPED_INFO("nor the line that would have captioned them");
        CHECK(row->getRect().getHeight() == ROW);
        grid->die();
    }

    // The field Debug Settings hands the grid for a colour, in the grid the
    // way Debug Settings has it: a colour at its default, in a pane two
    // hundred and forty wide with a labelled row of one section.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a Debug Settings colour row shows its editor in an open section", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid::Params p(LLUICtrlFactory::getDefaultParams<ALPropertyGrid>());
        p.name = "setting_editor";
        p.rect = LLRect(0, 120, 240, 0);
        p.label_width = 96;
        p.row_height = 24;
        ALPropertyGrid* grid = LLUICtrlFactory::create<ALPropertyGrid>(p);
        grid->setGroups({ "Setting" });
        grid->setEnabled(true);

        ALPropertyGrid::Field field;
        field.name = "RenderVignetteColor";
        field.value = "1 1 1";
        field.kind = ALParamType::REAL;
        field.type = "LLColor3";
        field.authored = false;
        field.source = "as it shipped";
        field.description = "Vignette edge color";
        grid->setFields({ field });

        LLPanel* row = grid->findChild<LLPanel>("RenderVignetteColor_row", true);
        REQUIRE(row != nullptr);
        CHECK(row->getVisible());
        LLView* editor = row->findChild<ALColorField>("RenderVignetteColor", true);
        REQUIRE(editor != nullptr);
        CHECK(editor->getVisible());
        CHECK(editor->getRect().getWidth() > 0);
        CHECK(editor->getRect().getHeight() > 0);

        // The part that was actually wrong. A section nothing is written in
        // arrives folded, which is right for a page of eighty fields and is
        // hiding the grid when the section is the only one -- and a grid of
        // one section draws no heading, so nothing could unfold it. A value
        // at its default showed a blank strip and nothing else.
        LLAccordionCtrlTab* tab = grid->getChild<LLAccordionCtrlTab>("Setting", true);
        UNSCOPED_INFO("the only section is open, written in or not");
        CHECK(tab->getDisplayChildren());
        UNSCOPED_INFO("and the row is inside a tab tall enough to hold it");
        CHECK(tab->getRect().getHeight() >= row->getRect().getHeight());
        grid->die();
    }

    // And with more than one section the rule stands: what is written is
    // open and what is not is folded under its heading, which is there to
    // unfold it with.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: of two sections an unwritten one arrives folded under a heading", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "written", "quiet" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        fields.back().authored = true;
        fields.push_back(field("width", 1));
        fields.back().authored = false;
        grid->setFields(fields);

        CHECK(grid->getChild<LLAccordionCtrlTab>("written", true)->getDisplayChildren());
        CHECK_FALSE(grid->getChild<LLAccordionCtrlTab>("quiet", true)->getDisplayChildren());
        CHECK(grid->getChild<LLAccordionCtrlTab>("quiet", true)->getHeaderHeight() > 0);
        grid->die();
    }

    // A row calls a field what the caller says, where that is not its name:
    // a caller that has said the name once already, over the pane, says
    // something else on the row rather than the same word smaller. And the
    // way back is called what the caller says too, and is as wide as that.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a row and its way back take the caller's labels over the name", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid::Params p(LLUICtrlFactory::getDefaultParams<ALPropertyGrid>());
        p.name = "grid";
        p.rect = LLRect(0, 300, 400, 0);
        p.remove_label = "Reset";
        ALPropertyGrid* grid = LLUICtrlFactory::create<ALPropertyGrid>(p);
        grid->setGroups({ "identity" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("EmojiSkinTonePreference", 0));
        fields.back().label = "S32";
        fields.back().authored = true;
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)
                           ->getChild<LLPanel>("EmojiSkinTonePreference_row", true);
        LLTextBox* label = row->getChild<LLTextBox>("EmojiSkinTonePreference_label", true);
        CHECK(label->getText() == std::string("S32"));

        LLButton* back = row->getChild<LLButton>("EmojiSkinTonePreference_remove", true);
        CHECK(back->getLabelUnselected() == std::string("Reset"));
        UNSCOPED_INFO("which is wider than a letter");
        CHECK(back->getRect().getWidth() > 16);
        CHECK(row->getLocalRect().contains(back->getRect()));

        // Left empty, the row says the name, which is what a row usually calls
        // a field.
        std::vector<ALPropertyGrid::Field> plain;
        plain.push_back(field("width", 0));
        grid->setFields(plain);
        CHECK(grid->getChild<LLPanel>("identity_rows", true)
                          ->getChild<LLPanel>("width_row", true)
                          ->getChild<LLTextBox>("width_label", true)->getText() ==
                      std::string("width"));
        grid->die();
    }

    // The first commit on an unwritten row is what makes it a written one,
    // and the row says so at once -- the ink, the way back, the words --
    // without being made again under whoever typed into it. Debug Settings
    // showed a setting changed from its default with no Reset until it was
    // chosen again.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: setAuthored redraws a row in place both ways", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });
        ALPropertyGrid::Tips tips;
        tips.source = "written in [SOURCE]";
        tips.unwritten = "nobody wrote this";
        grid->setTips(tips);

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("width", 0));
        fields.back().authored = false;
        fields.back().source.clear();
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)
                           ->getChild<LLPanel>("width_row", true);
        LLView* editor = row->getChild<LLView>("width", true);
        LLView* back = row->getChild<LLView>("width_remove", true);
        LLTextBox* label = row->getChild<LLTextBox>("width_label", true);
        UNSCOPED_INFO("unwritten, there is no way back to see");
        CHECK_FALSE(back->getVisible());
        const LLColor4 quiet = label->getColor().get();

        grid->setAuthored("width", true, "base");
        UNSCOPED_INFO("written now, the way back is there");
        CHECK(back->getVisible());
        UNSCOPED_INFO("in the other ink");
        CHECK(label->getColor().get() != quiet);
        UNSCOPED_INFO("saying where from: " << label->getToolTip());
        CHECK(label->getToolTip().find("written in base") != std::string::npos);
        UNSCOPED_INFO("and no longer that nobody did: " << label->getToolTip());
        CHECK(label->getToolTip().find("nobody wrote this") == std::string::npos);
        UNSCOPED_INFO("the editor says the same");
        CHECK(editor->getToolTip() == label->getToolTip());
        UNSCOPED_INFO("and it is the same editor, not a new one in its place");
        CHECK(row->getChild<LLView>("width", true) == editor);
        REQUIRE(grid->fields().size() == 1u);
        UNSCOPED_INFO("which the grid remembers");
        CHECK(grid->fields().front().authored);

        // And back, when what was written is taken out again.
        grid->setAuthored("width", false, std::string());
        UNSCOPED_INFO("unwritten again, the way back has gone");
        CHECK_FALSE(back->getVisible());
        UNSCOPED_INFO("in the quiet ink");
        CHECK(label->getColor().get() == quiet);
        UNSCOPED_INFO("saying so: " << label->getToolTip());
        CHECK(label->getToolTip().find("nobody wrote this") != std::string::npos);

        // A grid showing only what is written has a row fewer or more, and
        // that is a rebuild's decision.
        grid->setAuthoredOnly(true);
        UNSCOPED_INFO("nothing written, nothing shown");
        CHECK(grid->findChild<LLPanel>("width_row", true) == nullptr);
        grid->setAuthored("width", true, "base");
        UNSCOPED_INFO("written, and the row is back");
        CHECK(grid->findChild<LLPanel>("width_row", true) != nullptr);
        grid->die();
    }

    // A name that works and should not be used says so on the row that uses
    // it, and says what to write where the vocabulary has a word for it.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a deprecated name's tooltip says so and names its replacement", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });
        ALPropertyGrid::Tips tips;
        tips.field = "[NAME]";
        tips.deprecated = "[NAME] works, and nothing new should use it";
        tips.deprecatedFor = "[NAME] works, and nothing new should use it; write [INSTEAD]";
        grid->setTips(tips);

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("dynamicwidth", 0));
        fields.back().deprecated = true;
        fields.back().instead = "dynamic_width";
        fields.push_back(field("hover", 0));
        fields.back().deprecated = true;
        fields.push_back(field("width", 0));
        grid->setFields(fields);

        LLPanel* rows = grid->getChild<LLPanel>("identity_rows", true);
        const std::string spelt = rows->getChild<LLPanel>("dynamicwidth_row", true)
                                      ->getChild<LLTextBox>("dynamicwidth_label", true)->getToolTip();
        UNSCOPED_INFO("a deprecated name says so: " << spelt);
        CHECK(spelt.find("nothing new should use it") != std::string::npos);
        UNSCOPED_INFO("and says what to write: " << spelt);
        CHECK(spelt.find("write dynamic_width") != std::string::npos);

        const std::string bare = rows->getChild<LLPanel>("hover_row", true)
                                     ->getChild<LLTextBox>("hover_label", true)->getToolTip();
        {
            INFO("one with nothing to write instead says only that: " << bare);
            CHECK(bare.find("nothing new should use it") != std::string::npos);
            CHECK(bare.find("write") == std::string::npos);
        }

        const std::string fine = rows->getChild<LLPanel>("width_row", true)
                                     ->getChild<LLTextBox>("width_label", true)->getToolTip();
        UNSCOPED_INFO("and a name in good standing says nothing of the kind: " << fine);
        CHECK(fine.find("should") == std::string::npos);
        grid->die();
    }

    // A paired row has one way back for its two fields: it is shown when
    // either is written, it takes out whichever of them the file writes,
    // and the field that has no row of its own still reaches it.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a paired row's one way back removes whichever half is written", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "size" });
        std::vector<std::string> removed;
        grid->onFieldRemove([&removed](const std::string& name) { removed.push_back(name); });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("width", 0));
        fields.back().pairWith = "height";
        fields.back().authored = false;
        fields.back().source.clear();
        fields.push_back(field("height", 0));
        grid->setFields(fields);

        LLPanel* row = grid->getChild<LLPanel>("size_rows", true)->getChild<LLPanel>("width_row", true);
        LLButton* back = row->findChild<LLButton>("width_remove", true);
        REQUIRE(back != nullptr);
        UNSCOPED_INFO("the way back is there for the half that is written");
        CHECK(back->getVisible());
        back->onCommit();
        UNSCOPED_INFO("and takes out that half alone");
        REQUIRE(removed.size() == 1u);
        UNSCOPED_INFO("which is the second");
        CHECK(removed.front() == std::string("height"));

        // The second half told it is unwritten reaches the row it shares.
        grid->setAuthored("height", false, std::string());
        UNSCOPED_INFO("and nothing is left to take out");
        CHECK_FALSE(back->getVisible());
        grid->setAuthored("width", true, "base");
        UNSCOPED_INFO("until the first is written");
        CHECK(back->getVisible());
        removed.clear();
        back->onCommit();
        UNSCOPED_INFO("and then it takes out the first");
        REQUIRE(removed.size() == 1u);
        CHECK(removed.front() == std::string("width"));
        grid->die();
    }

    // A font row is three fields, and answers by its parts: a name typed
    // into it is written once, not once as a part and again as the whole.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a name typed into a font row is written once", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "typeface" });
        std::vector<std::pair<std::string, std::string> > written;
        grid->onFieldCommit([&written](const std::string& name, const std::string& value)
        {
            written.emplace_back(name, value);
        });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("font", 0));
        fields.back().type = "const LLFontGL *";
        fields.back().value = "SansSerif";
        grid->setFields(fields);

        LLLineEditor* line = grid->getChild<LLLineEditor>("text", true);
        line->setText(std::string("Monospace"));
        line->onCommit();
        REQUIRE(written.size() == 1u);
        CHECK(written.front().first == std::string("font"));
        CHECK(written.front().second == std::string("Monospace"));
        grid->die();
    }

    // A field filed under a heading the caller never named goes under the
    // last one, as the field says it will; it used to go nowhere.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: a group past either end files under the first or last heading", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity", "position" });

        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        fields.push_back(field("stray", 7));
        fields.push_back(field("below", -3));
        grid->setFields(fields);

        LLPanel* position = grid->getChild<LLPanel>("position_rows", true);
        LLPanel* identity = grid->getChild<LLPanel>("identity_rows", true);
        UNSCOPED_INFO("a field past the last heading is under the last");
        CHECK(position->findChild<LLPanel>("stray_row", true) != nullptr);
        UNSCOPED_INFO("and one before the first is under the first");
        CHECK(identity->findChild<LLPanel>("below_row", true) != nullptr);
        grid->die();
    }

    // A caller answering a commit by filling the grid again -- XUI Studio
    // does, after any edit it cannot apply in place -- must not delete the
    // editor that is still committing. It did: Return in a row, the row's
    // editor committing, the grid rebuilt from inside the commit, and the
    // editor's own onCommit carried on into freed memory. The rebuild is held
    // until the commit is over, and done once it is.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: refilling the grid from a commit waits until the commit is over", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("label", 0));
        fields.back().value = "Before";
        grid->setFields(fields);

        LLLineEditor* editor = grid->getChild<LLLineEditor>("label", true);
        bool outlived = false;
        grid->onFieldCommit([grid, editor, &outlived](const std::string& name, const std::string& value)
        {
            std::vector<ALPropertyGrid::Field> refilled;
            refilled.push_back(alpropertygrid_data::field(name, 0));
            refilled.back().value = value;
            grid->setFields(refilled);
            // Still the grid's own editor, not a new one in its place and not
            // a pointer to nothing.
            outlived = grid->findChild<LLLineEditor>("label", true) == editor;
        });

        editor->setText(std::string("After"));
        editor->onCommit();
        UNSCOPED_INFO("the editor that committed outlives its commit");
        CHECK(outlived);

        // And once the commit is over, the grid is what it was asked to be.
        gIdleCallbacks.callFunctions();
        LLLineEditor* now = grid->findChild<LLLineEditor>("label", true);
        REQUIRE(now != nullptr);
        UNSCOPED_INFO("with what was committed");
        CHECK(now->getText() == std::string("After"));
        grid->die();
    }

    // The same rows with new values are taken without a rebuild: the
    // editors are the same objects afterwards, saying the new values.
    // Different rows are refused, and then the grid is filled again.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: updateFields keeps the editors of the same rows and refuses others", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("label", 0));
        fields.back().value = "Before";
        fields.push_back(field("width", 0));
        fields.back().kind = ALParamType::INTEGER;
        fields.back().value = "10";
        grid->setFields(fields);
        gIdleCallbacks.callFunctions();

        LLLineEditor* label = grid->getChild<LLLineEditor>("label", true);
        LLSpinCtrl* width = grid->getChild<LLSpinCtrl>("width", true);

        fields[0].value = "After";
        fields[1].value = "20";
        fields[1].authored = false;
        CHECK(grid->updateFields(fields));
        {
            INFO("the editors are the ones that were there");
            CHECK(grid->findChild<LLLineEditor>("label", true) == label);
            CHECK(grid->findChild<LLSpinCtrl>("width", true) == width);
        }
        CHECK(label->getText() == std::string("After"));
        CHECK(width->getValue().asInteger() == 20);
        REQUIRE(grid->fields().size() == 2u);
        UNSCOPED_INFO("and the grid knows it");
        CHECK(grid->fields()[1].value == std::string("20"));

        fields.push_back(field("height", 0));
        UNSCOPED_INFO("a row more is refused");
        CHECK_FALSE(grid->updateFields(fields));
        fields.pop_back();
        fields[1].kind = ALParamType::REAL;
        UNSCOPED_INFO("a row of another kind is refused");
        CHECK_FALSE(grid->updateFields(fields));
        grid->die();
    }

    // Rows keep the order they were given when asked to, however they are
    // written: a vocabulary has an order of its own.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: setKeepsOrder keeps fields in the order given", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setKeepsOrder(true);
        grid->setGroups({ "identity" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("radius", 0));
        fields.back().authored = false;
        fields.push_back(field("corners", 0));
        fields.back().authored = false;
        fields.push_back(field("border", 0));
        grid->setFields(fields);
        REQUIRE(grid->fields().size() == 3u);
        CHECK(grid->fields()[0].name == std::string("radius"));
        CHECK(grid->fields()[1].name == std::string("corners"));
        UNSCOPED_INFO("the written one stays where it was");
        CHECK(grid->fields()[2].name == std::string("border"));
        grid->die();
    }

    // A kept row is put right in every way it can change under an update:
    // what its tip says follows the field, a part being scrubbed keeps the
    // number under the hand, and a field that would build a different
    // control -- another step -- is refused rather than kept stale.
    TEST_CASE_METHOD(alpropertygrid_data, "alpropertygrid: an update refreshes a kept row's tip and values but not its step", "[alpropertygrid]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid* grid = build();
        grid->setGroups({ "identity" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("rect", 0));
        fields.back().kind = ALParamType::INTEGER;
        fields.back().components = { "L", "T", "R", "B" };
        fields.back().value = "10 20 30 40";
        grid->setFields(fields);
        gIdleCallbacks.callFunctions();

        LLPanel* row = grid->getChild<LLPanel>("identity_rows", true)->getChild<LLPanel>("rect_row", true);
        LLTextBox* label = row->getChild<LLTextBox>("rect_label", true);
        LLSpinCtrl* top = row->getChild<LLSpinCtrl>("rect.T", true);

        fields[0].description = "Where it sits.";
        UNSCOPED_INFO("a new description keeps the row");
        CHECK(grid->updateFields(fields));
        UNSCOPED_INFO("and the tip says it");
        CHECK(label->getToolTip().find("Where it sits.") != std::string::npos);

        // Scrubbing T: the update lands in the other three and not in T.
        gFocusMgr.setMouseCapture(top);
        fields[0].value = "11 21 31 41";
        UNSCOPED_INFO("a new value keeps the row");
        CHECK(grid->updateFields(fields));
        UNSCOPED_INFO("the part under the hand keeps its number");
        CHECK(top->getValue().asInteger() == 20);
        UNSCOPED_INFO("and the others take theirs");
        CHECK(row->getChild<LLSpinCtrl>("rect.L", true)->getValue().asInteger() == 11);
        gFocusMgr.setMouseCapture(nullptr);

        fields[0].step = 5.f;
        UNSCOPED_INFO("another step is another control, so the rows differ");
        CHECK_FALSE(grid->updateFields(fields));
        grid->die();
    }
}

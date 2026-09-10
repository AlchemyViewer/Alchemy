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

#include "../llbutton.h"
#include "../alcolorfield.h"
#include "../llaccordionctrltab.h"

#include "../llaccordionctrl.h"
#include "../llaccordionctrltab.h"
#include "../llspinctrl.h"
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

    // A row says what it is wherever the pointer rests along it, in the
    // caller's words: the two things that have to be said rather than shown
    // are which layer wrote what is in force, and that on an unwritten row
    // nobody wrote it at all.
    template<> template<>
    void alpropertygrid_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("it names the field: " + tip, tip.find("width is a S32") != std::string::npos);
        ensure("and says where what is in force came from: " + tip,
               tip.find("written in base") != std::string::npos);
        ensure("a row somebody wrote does not say nobody did: " + tip,
               tip.find("nobody wrote this") == std::string::npos);

        // The editor answers the same question as the label.
        ensure_equals("the editor says what the label says",
                      row->getChild<LLView>("width", true)->getToolTip(), tip);
        ensure("and the way back says what it takes out",
               row->getChild<LLView>("width_remove", true)->getToolTip()
                   .find("take width out again") != std::string::npos);

        // The row nobody wrote says so, and has no way back to offer.
        LLPanel* unwritten = rows->getChild<LLPanel>("height_row", true);
        const std::string quiet = unwritten->getChild<LLTextBox>("height_label", true)->getToolTip();
        ensure("an unwritten row says nobody wrote it: " + quiet,
               quiet.find("nobody wrote this") != std::string::npos);
        ensure("and it names no layer: " + quiet, quiet.find("written in") == std::string::npos);
        ensure("nor is there anything to take out",
               unwritten->findChild<LLView>("height_remove", true) == nullptr);
        grid->die();
    }

    // A number is stepped by its arrows, by the wheel over it and by a scrub
    // along it, and all three take the same step. A field carrying no
    // decimals rounds whatever the step leaves it at, so a step of less than
    // one lands back on the number it started from: the arrows write what is
    // already there and only the keyboard can change anything.
    template<> template<>
    void alpropertygrid_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("a whole number steps by a whole number: "
                   + std::to_string(whole->getIncrement()),
               whole->getIncrement() >= 1.f);

        LLSpinCtrl* real = rows->getChild<LLPanel>("alpha_row", true)
                               ->getChild<LLSpinCtrl>("alpha", true);
        ensure("and one with decimals steps by less: " + std::to_string(real->getIncrement()),
               real->getIncrement() > 0.f && real->getIncrement() < 1.f);
        grid->die();
    }

    // A value that is several numbers, edited as several numbers. Four in a
    // string is four numbers a reader has to count and a writer has to keep
    // in order; a box each, captioned, is the same value said so that both
    // are obvious.
    template<> template<>
    void alpropertygrid_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            LLSpinCtrl* box = row->getChild<LLSpinCtrl>(name, true);
            ensure("a box for " + name, box != nullptr);
            ensure_equals("holding its own number", (F32)box->getValue().asReal(), wanted[i]);
            ensure("and captioned with what it is",
                   row->findChild<LLView>(name + "_caption", true) != nullptr);
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
        ensure_equals("the field, not the part", said_name, std::string("rect"));
        ensure_equals("and the whole of it", said_value, std::string("10 20 99 40"));

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
        ensure("a captioned row is taller: " + std::to_string(tall) + " vs "
                   + std::to_string(ordinary), tall > ordinary);
        other->die();
        grid->die();
    }

    // A value arriving with commas between its parts is the same value: an
    // LLSD one is written that way and a file writes spaces.
    template<> template<>
    void alpropertygrid_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("split on the commas",
                      (F32)row->getChild<LLSpinCtrl>("colour.G", true)->getValue().asReal(), 0.25f);

        // Fewer parts than boxes is nought in the rest rather than a hole.
        std::vector<ALPropertyGrid::Field> short_one;
        short_one.push_back(field("short", 0));
        short_one.back().kind = ALParamType::REAL;
        short_one.back().components = { "X", "Y", "Z" };
        short_one.back().value = "1 2";
        grid->setFields(short_one);
        ensure_equals("the part nobody gave is nought",
                      (F32)grid->getChild<LLPanel>("identity_rows", true)
                               ->getChild<LLPanel>("short_row", true)
                               ->getChild<LLSpinCtrl>("short.Z", true)->getValue().asReal(), 0.f);
        grid->die();
    }

    // A grid of one section has nothing to fold it away from, so the heading
    // is a bar with no job: it names the only thing there and offers to hide
    // the only thing there. And a section with no heading is as tall as its
    // rows -- the tab used to be told to leave room for a header it was not
    // drawing, and handed the difference to its panel as blank space.
    template<> template<>
    void alpropertygrid_object::test<12>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* one = build();
        one->setGroups({ "only" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        one->setFields(fields);

        LLAccordionCtrlTab* tab = one->getChild<LLAccordionCtrlTab>("only", true);
        ensure("the section is there", tab != nullptr);
        ensure("and says nothing over it", tab->getHeaderHeight() == 0);
        ensure_equals("so it is as tall as its one row",
                      one->getChild<LLPanel>("only_rows", true)->getRect().getHeight(), ROW);

        // Two sections is where a heading starts doing something: it says
        // which of them you are looking at, and folds one away from the other.
        ALPropertyGrid* two = build();
        two->setGroups({ "first", "second" });
        std::vector<ALPropertyGrid::Field> both;
        both.push_back(field("name", 0));
        both.push_back(field("width", 1));
        two->setFields(both);
        ensure("a heading over each",
               two->getChild<LLAccordionCtrlTab>("first", true)->getHeaderHeight() > 0);

        one->die();
        two->die();
    }

    // A colour is several numbers and is not a row of boxes: it has a swatch
    // and a picker. A caller that names its parts as well -- because the
    // value really is four numbers -- gets the better editor rather than the
    // more literal one, and does not get the caption line either.
    template<> template<>
    void alpropertygrid_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("a colour gets the colour editor",
               row->findChild<ALColorField>("tint", true) != nullptr);
        ensure("and not a box per part",
               row->findChild<LLSpinCtrl>("tint.R", true) == nullptr);
        ensure_equals("nor the line that would have captioned them",
                      row->getRect().getHeight(), ROW);
        grid->die();
    }

    // The field Debug Settings hands the grid for a colour, in the grid the
    // way Debug Settings has it: a colour at its default, in a pane two
    // hundred and forty wide with a labelled row of one section.
    template<> template<>
    void alpropertygrid_object::test<14>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the row is there", row != nullptr);
        ensure("and shown", row->getVisible());
        LLView* editor = row->findChild<ALColorField>("RenderVignetteColor", true);
        ensure("with the colour editor on it", editor != nullptr);
        ensure("shown", editor->getVisible());
        ensure("and somewhere to be seen: " + std::to_string(editor->getRect().getWidth()),
               editor->getRect().getWidth() > 0 && editor->getRect().getHeight() > 0);

        // The part that was actually wrong. A section nothing is written in
        // arrives folded, which is right for a page of eighty fields and is
        // hiding the grid when the section is the only one -- and a grid of
        // one section draws no heading, so nothing could unfold it. A value
        // at its default showed a blank strip and nothing else.
        LLAccordionCtrlTab* tab = grid->getChild<LLAccordionCtrlTab>("Setting", true);
        ensure("the only section is open, written in or not", tab->getDisplayChildren());
        ensure("and the row is inside a tab tall enough to hold it",
               tab->getRect().getHeight() >= row->getRect().getHeight());
        grid->die();
    }

    // And with more than one section the rule stands: what is written is
    // open and what is not is folded under its heading, which is there to
    // unfold it with.
    template<> template<>
    void alpropertygrid_object::test<15>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALPropertyGrid* grid = build();
        grid->setGroups({ "written", "quiet" });
        std::vector<ALPropertyGrid::Field> fields;
        fields.push_back(field("name", 0));
        fields.back().authored = true;
        fields.push_back(field("width", 1));
        fields.back().authored = false;
        grid->setFields(fields);

        ensure("the written section is open",
               grid->getChild<LLAccordionCtrlTab>("written", true)->getDisplayChildren());
        ensure("and the quiet one arrives folded",
               !grid->getChild<LLAccordionCtrlTab>("quiet", true)->getDisplayChildren());
        ensure("under a heading that can open it",
               grid->getChild<LLAccordionCtrlTab>("quiet", true)->getHeaderHeight() > 0);
        grid->die();
    }

    // A row calls a field what the caller says, where that is not its name:
    // a caller that has said the name once already, over the pane, says
    // something else on the row rather than the same word smaller. And the
    // way back is called what the caller says too, and is as wide as that.
    template<> template<>
    void alpropertygrid_object::test<16>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the row says what the caller said", label->getText(), std::string("S32"));

        LLButton* back = row->getChild<LLButton>("EmojiSkinTonePreference_remove", true);
        ensure_equals("and so does the way back", back->getLabelUnselected(), std::string("Reset"));
        ensure("which is wider than a letter", back->getRect().getWidth() > 16);
        ensure("and still inside the row", row->getLocalRect().contains(back->getRect()));

        // Left empty, the row says the name, which is what a row usually calls
        // a field.
        std::vector<ALPropertyGrid::Field> plain;
        plain.push_back(field("width", 0));
        grid->setFields(plain);
        ensure_equals("the name by default",
                      grid->getChild<LLPanel>("identity_rows", true)
                          ->getChild<LLPanel>("width_row", true)
                          ->getChild<LLTextBox>("width_label", true)->getText(),
                      std::string("width"));
        grid->die();
    }
}

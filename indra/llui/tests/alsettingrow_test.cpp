/**
 * @file alsettingrow_test.cpp
 * @brief One setting on one line, bound once, with its way back to the default.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "alsettingrow.h"

#include "../llbutton.h"
#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llslider.h"
#include "../llsliderctrl.h"
#include "../llspinctrl.h"
#include "../lltextbox.h"
#include "../lltrans.h"
#include "../llui.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"
#include "llcontrol.h"
#include "llxmlnode.h"
#include "v3math.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

namespace
{
    struct alsettingrow_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // The settings a test asked for, put back to their defaults when it
        // ends as well as when the next asks.
        ll_test::SettingsScope settings;
        bool                   gave_words = false;

        // What the reset button's words were given, taken back: nothing else
        // in these tests gives LLTrans strings, and it has no way to hand
        // back what it held, so it is left as empty as it was found.
        ~alsettingrow_data()
        {
            if (gave_words)
            {
                const std::string none = "<strings></strings>";
                LLXMLNodePtr      node;
                if (LLXMLNode::parseBuffer(none.data(), none.size(), node))
                {
                    LLTrans::parseStrings(node, {});
                }
            }
        }

        static LLControlGroup& config()
        {
            return *LLUI::getInstance()->getSettingGroup("config");
        }

        // One setting per test, put back to its default, so no test sees what
        // another left behind.
        LLControlVariable* setting(const std::string& name, F32 value)
        {
            LLControlVariable* control = config().getControl(name).get();
            if (!control)
            {
                control = config().declareF32(name, value, std::string("A setting row test value"));
            }
            control->resetToDefault(false);
            return settings.keep(control);
        }

        static ALSettingRow::Params params(const std::string& control_name, F32 min_value = 0.f,
                                           F32 max_value = 1.f, S32 decimal_digits = 2)
        {
            ALSettingRow::Params p(LLUICtrlFactory::getDefaultParams<ALSettingRow>());
            p.name = "row";
            p.rect = LLRect(0, 18, 360, 0);
            p.label = "Strength";
            p.min_value = min_value;
            p.max_value = max_value;
            p.increment = 0.01f;
            p.decimal_digits = decimal_digits;
            if (!control_name.empty())
            {
                p.control_name = control_name;
            }
            return p;
        }

        static ALSettingRow* row(const std::string& control_name, F32 min_value = 0.f,
                                 F32 max_value = 1.f, S32 decimal_digits = 2)
        {
            ALSettingRow::Params p = params(control_name, min_value, max_value, decimal_digits);
            return LLUICtrlFactory::create<ALSettingRow>(p);
        }

        // What the reset button says, as the viewer's strings have it:
        // nothing here loads them. Given for each test that asks, since
        // parsing strings anywhere replaces every string there was.
        void resetWords()
        {
            const std::string source = "<strings><string name=\"SettingRowReset\">Reset to [VALUE]</string></strings>";
            LLXMLNodePtr      node;
            gave_words = LLXMLNode::parseBuffer(source.data(), source.size(), node) && LLTrans::parseStrings(node, {});
        }

        // A vector setting, put back to its default.
        LLControlVariable* vector(const std::string& name, const LLVector3& value)
        {
            LLControlVariable* control = config().getControl(name).get();
            if (!control)
            {
                control = config().declareVec3(name, value, std::string("A setting row test vector"));
            }
            control->resetToDefault(false);
            return settings.keep(control);
        }

        static bool sameParts(const LLSD& value, F64 x, F64 y, F64 z)
        {
            return closeTo(value[0].asReal(), x) && closeTo(value[1].asReal(), y) && closeTo(value[2].asReal(), z);
        }

        // Every view under @a root that is bound to a setting.
        static S32 boundUnder(LLView* root)
        {
            S32 bound = 0;
            for (LLView* child : *root->getChildList())
            {
                LLUICtrl* ctrl = ALViewType::as<LLUICtrl>(child);
                if (ctrl && ctrl->getControlVariable())
                {
                    ++bound;
                }
                bound += boundUnder(child);
            }
            return bound;
        }

        // A slider's value box.
        static LLLineEditor* valueBoxOf(LLSliderCtrl* slider)
        {
            for (LLView* child : *slider->getChildList())
            {
                if (LLLineEditor* editor = ALViewType::as<LLLineEditor>(child))
                {
                    return editor;
                }
            }
            return nullptr;
        }

        static bool closeTo(F64 a, F64 b)
        {
            return std::fabs(a - b) < 1e-5;
        }
    };

    // A slider row becomes a setting row by changing its tag, so every one of
    // the slider's attributes has to be read by the name a slider writes.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: XUI reads every slider attribute by the slider's own names", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const std::string source =
            "<setting_row name=\"r\" label=\"Knee\" label_width=\"120\" min_val=\"-1\" max_val=\"2\""
            " increment=\"0.05\" decimal_digits=\"3\" text_width=\"56\" show_text=\"true\""
            " can_edit_text=\"false\" show_reset=\"false\" control_name=\"RenderBloomKnee\"/>\n";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(source.data(), source.size(), node));

        ALSettingRow::Params p;
        LLXUIParser parser;
        parser.readXUI(node, p, "setting_row_test.xml");
        CHECK(p.label() == std::string("Knee"));
        CHECK(p.label_width() == 120);
        CHECK(closeTo(p.min_value(), -1.0));
        CHECK(closeTo(p.max_value(), 2.0));
        CHECK(closeTo(p.increment(), 0.05));
        CHECK(p.decimal_digits() == 3);
        CHECK(p.text_width.isProvided());
        CHECK(p.text_width() == 56);
        CHECK_FALSE(p.can_edit_text());
        CHECK_FALSE(p.show_reset());
        CHECK(p.control_name() == std::string("RenderBloomKnee"));

        // And what a file leaves out is what every Lightbox row writes today.
        ALSettingRow::Params defaults;
        UNSCOPED_INFO("label_width defaults to the rows' 140");
        CHECK(defaults.label_width() == 140);
        CHECK(defaults.can_edit_text());
        CHECK(defaults.show_reset());
        CHECK_FALSE(defaults.text_width.isProvided());
        {
            INFO("a scalar unless said otherwise");
            CHECK(defaults.components().empty());
            CHECK(defaults.part() == -1);
        }

        // A vector's letters and a part's index, by the names a row writes.
        const std::string vector_source = "<setting_row name=\"v\" components=\"R G B\" part=\"1\" letter_width=\"18\"/>\n";
        LLXMLNodePtr      vector_node;
        REQUIRE(LLXMLNode::parseBuffer(vector_source.data(), vector_source.size(), vector_node));
        ALSettingRow::Params vector_p;
        parser.readXUI(vector_node, vector_p, "setting_row_test.xml");
        CHECK(vector_p.components() == std::string("R G B"));
        CHECK(vector_p.part() == 1);
        CHECK(vector_p.letter_width() == 18);
        UNSCOPED_INFO("a letter's width unless said");
        CHECK(defaults.letter_width() == 10);
    }

    // Bound both ways, and a commit is one write: the row writes the setting,
    // the setting's signal comes back to the row, and nothing writes it again.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a row follows its setting and a commit writes it once", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        CHECK(closeTo(r->getValue().asReal(), 0.5));

        control->set(LLSD(0.75));
        CHECK(closeTo(r->getValue().asReal(), 0.75));
        CHECK(closeTo(r->getSlider()->getValueF32(), 0.75));

        S32 writes = 0;
        boost::signals2::scoped_connection watching = control->getSignal()->connect(
            [&writes](LLControlVariable*, const LLSD&, const LLSD&) { ++writes; });
        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        r->getSlider()->setValue(0.25f);
        r->getSlider()->onCommit();
        CHECK(closeTo(control->getValue().asReal(), 0.25));
        CHECK(writes == 1);
        CHECK(commits == 1);
        r->die();
    }

    // The row is the only thing bound, so a walk for what a panel is bound to
    // finds one setting per row and not one per part of it.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: only the row itself is bound and none of its parts", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        CHECK(r->getControlVariable() != nullptr);
        CHECK(boundUnder(r) == 0);
        r->die();
    }

    // The reset button is there while there is something to reset, pressing it
    // puts the default back and goes, and the row does not say it committed:
    // what cares about the setting heard the setting change.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: reset shows when changed and restores the default without a commit", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestReset", 0.5f);
        ALSettingRow* r = row("SettingRowTestReset");
        UNSCOPED_INFO("hidden at the default");
        CHECK_FALSE(r->getResetButton()->getVisible());

        control->set(LLSD(0.8));
        UNSCOPED_INFO("shown once changed");
        CHECK(r->getResetButton()->getVisible());
        CHECK(r->isModified());

        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        r->getResetButton()->onCommit();
        CHECK(closeTo(control->getValue().asReal(), 0.5));
        CHECK(closeTo(r->getSlider()->getValueF32(), 0.5));
        CHECK_FALSE(r->getResetButton()->getVisible());
        UNSCOPED_INFO("the row did not commit");
        CHECK(commits == 0);
        r->die();
    }

    // As shown, not as stored: a value that rounds to the default at the
    // row's decimals is the default as far as anyone looking can tell, so a
    // drag that came back to it does not leave a reset button lit.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a value that rounds to the default as shown lights no reset", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestRounding", 0.7f);
        ALSettingRow* r = row("SettingRowTestRounding", 0.f, 1.f, 2);

        control->set(LLSD(0.6999999));
        UNSCOPED_INFO("not the default to the letter");
        CHECK_FALSE(control->isDefault());
        UNSCOPED_INFO("but not modified as shown");
        CHECK_FALSE(r->isModified());
        CHECK_FALSE(r->getResetButton()->getVisible());

        control->set(LLSD(0.69));
        UNSCOPED_INFO("a change that shows, shows it");
        CHECK(r->getResetButton()->getVisible());
        r->die();
    }

    // The button's place is kept whether it shows or not, at any width: the
    // slider ends where it always ends, so nothing moves when the button
    // appears, and a row and the rows above it line up.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: the reset button's slot is kept at the right at any width", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        const S32 width = r->getRect().getWidth();
        const S32 slot = ALSettingRow::RESET_SIZE + ALSettingRow::RESET_GAP;
        CHECK(r->getResetButton()->getRect().mRight == width);
        CHECK(r->getSlider()->getRect().mRight == width - slot);

        r->reshape(width + 50, r->getRect().getHeight());
        CHECK(r->getResetButton()->getRect().mRight == width + 50);
        CHECK(r->getSlider()->getRect().mRight == width + 50 - slot);
        r->die();
    }

    // Greyed as one thing: the slider, its label and value, and the button,
    // whether by hand or by the control the row's enabled_control names.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: disabling by hand or by enabled_control greys slider and button", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        r->setEnabled(false);
        CHECK_FALSE(r->getSlider()->getEnabled());
        CHECK_FALSE(r->getResetButton()->getEnabled());
        r->setEnabled(true);
        CHECK(r->getSlider()->getEnabled());
        CHECK(r->getResetButton()->getEnabled());
        r->die();

        LLControlVariable* gate = config().getControl("SettingRowTestGate").get();
        if (!gate)
        {
            gate = config().declareBOOL("SettingRowTestGate", true, std::string("A setting row test gate"));
        }
        gate->set(LLSD(true));
        ALSettingRow::Params p = params("SettingRowTestBinding");
        p.enabled_controls.enabled = "SettingRowTestGate";
        ALSettingRow* gated = LLUICtrlFactory::create<ALSettingRow>(p);
        UNSCOPED_INFO("open while the gate is");
        CHECK(gated->getSlider()->getEnabled());
        gate->set(LLSD(false));
        {
            INFO("greyed when it closes");
            CHECK_FALSE(gated->getSlider()->getEnabled());
            CHECK_FALSE(gated->getResetButton()->getEnabled());
        }
        gate->set(LLSD(true));
        gated->die();
    }

    // Bound to nothing it is still a slider: it takes and gives a value and
    // says when it changed -- which is all the vector rows need of a control --
    // and it never offers a reset it has nothing to reset with.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: an unbound row takes and gives a value and never offers a reset", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        r->setValue(LLSD(0.4));
        CHECK(closeTo(r->getValue().asReal(), 0.4));

        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        r->getSlider()->setValue(0.6f);
        r->getSlider()->onCommit();
        CHECK(commits == 1);
        CHECK(closeTo(r->getValue().asReal(), 0.6));
        CHECK_FALSE(r->getResetButton()->getVisible());
        CHECK_FALSE(r->isModified());
        r->die();
    }

    // The value box is wide enough for every number the range holds. A
    // slider sizes it from the maximum's logarithm, which under 1 has no room
    // for the leading zero; the row sizes it itself there, and only there.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: the value box fits a leading zero under 1 and matches a slider over", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const auto slider_like = [](F32 max_value)
        {
            LLSliderCtrl::Params sp(LLUICtrlFactory::getDefaultParams<LLSliderCtrl>());
            sp.name = "plain";
            sp.rect = LLRect(0, 16, 336, 0);
            sp.label = "Strength";
            sp.label_width = 140;
            sp.min_value = 0.f;
            sp.max_value = max_value;
            sp.increment = 0.01f;
            sp.decimal_digits = 3;
            sp.can_edit_text = true;
            return LLUICtrlFactory::create<LLSliderCtrl>(sp);
        };

        ALSettingRow* small = row(std::string(), 0.f, 0.05f, 3);
        LLSliderCtrl* plain_small = slider_like(0.05f);
        LLLineEditor* row_box = valueBoxOf(small->getSlider());
        LLLineEditor* plain_box = valueBoxOf(plain_small);
        REQUIRE(row_box);
        REQUIRE(plain_box);
        UNSCOPED_INFO("under 1, the row's is wider than a slider's");
        CHECK(row_box->getRect().getWidth() > plain_box->getRect().getWidth());

        ALSettingRow* large = row(std::string(), 0.f, 10.f, 3);
        LLSliderCtrl* plain_large = slider_like(10.f);
        LLLineEditor* large_box = valueBoxOf(large->getSlider());
        LLLineEditor* plain_large_box = valueBoxOf(plain_large);
        REQUIRE(large_box);
        REQUIRE(plain_large_box);
        UNSCOPED_INFO("over 1, exactly a slider's");
        CHECK(large_box->getRect().getWidth() == plain_large_box->getRect().getWidth());

        small->die();
        plain_small->die();
        large->die();
        plain_large->die();
    }

    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a row is found and highlighted by its label as a slider is", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        CHECK(r->getSearchText().find("Strength") != std::string::npos);
        CHECK(r->getLabel() == std::string("Strength"));
        LLTextBox* box = r->getLabelBox();
        REQUIRE(box != nullptr);
        CHECK(box->getText() == std::string("Strength"));
        // The column every row's label takes when the file does not say: a
        // default the slider did not get would have it measure the word.
        UNSCOPED_INFO("as wide as the rows' label column");
        CHECK(box->getRect().getWidth() == 140);

        r->setHighlighted(true);
        CHECK(r->getSlider()->getHighlighted());
        r->setHighlighted(false);
        CHECK_FALSE(r->getSlider()->getHighlighted());
        r->die();
    }

    // A reset button that goes away with the keyboard in it would leave the
    // keyboard nowhere.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a reset button that hides gives up the keyboard", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestFocus", 0.5f);
        ALSettingRow* r = row("SettingRowTestFocus");
        gFloaterView->addChild(r);
        control->set(LLSD(0.9));
        r->getResetButton()->setFocus(true);
        CHECK(r->getResetButton()->hasFocus());

        control->resetToDefault(true);
        CHECK_FALSE(r->getResetButton()->getVisible());
        CHECK_FALSE(r->getResetButton()->hasFocus());
        r->die();
    }

    // An owner can take the reset: the button asks it instead of resetting,
    // once, with the row, and the row does nothing itself -- the owner that
    // took it is the one that resets.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a reset handler is asked instead and may reset the row itself", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestHandler", 0.5f);
        ALSettingRow* r = row("SettingRowTestHandler");
        control->set(LLSD(0.9));

        S32 asked = 0;
        ALSettingRow* asked_by = nullptr;
        r->setResetHandler([&asked, &asked_by](ALSettingRow* from)
        {
            ++asked;
            asked_by = from;
        });
        r->getResetButton()->onCommit();
        CHECK(asked == 1);
        CHECK(asked_by == r);
        UNSCOPED_INFO("and the row did not reset it itself");
        CHECK(closeTo(control->getValue().asReal(), 0.9));

        // The owner resetting is a reset like any other.
        r->setResetHandler([](ALSettingRow* from) { from->getControlVariable()->resetToDefault(true); });
        r->getResetButton()->onCommit();
        UNSCOPED_INFO("reset by the owner");
        CHECK(closeTo(control->getValue().asReal(), 0.5));
        CHECK_FALSE(r->getResetButton()->getVisible());
        r->die();
    }

    // A row says when a drag begins and ends, the way a slider does and by the
    // names a slider writes: a setting that is dear to apply waits for the
    // release. The callbacks are named in the file, so they are resolved
    // against the registrar in scope while the row is built.
    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: mouse_down and mouse_up callbacks from XUI tell a drag's ends", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        setting("SettingRowTestHeld", 0.5f);
        S32 pressed = 0;
        S32 released = 0;
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
        registrar.add("SettingRowTest.Down", [&pressed](LLUICtrl*, const LLSD&) { ++pressed; });
        registrar.add("SettingRowTest.Up", [&released](LLUICtrl*, const LLSD&) { ++released; });

        const std::string source =
            "<setting_row name=\"r\" label=\"Grime\" min_val=\"0\" max_val=\"1\""
            " control_name=\"SettingRowTestHeld\">\n"
            "  <setting_row.mouse_down_callback function=\"SettingRowTest.Down\"/>\n"
            "  <setting_row.mouse_up_callback function=\"SettingRowTest.Up\"/>\n"
            "</setting_row>\n";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(source.data(), source.size(), node));
        ALSettingRow::Params p(LLUICtrlFactory::getDefaultParams<ALSettingRow>());
        LLXUIParser parser;
        parser.readXUI(node, p, "setting_row_test.xml");
        CHECK(p.mouse_down_callback.isProvided());
        CHECK(p.mouse_up_callback.isProvided());
        p.rect = LLRect(0, 18, 360, 0);

        ALSettingRow* r = LLUICtrlFactory::create<ALSettingRow>(p);
        gFloaterView->addChild(r);
        // The track a row's slider presses on.
        const auto barOf = [](ALSettingRow* of)
        {
            LLSlider* bar = nullptr;
            for (LLView* child : *of->getSlider()->getChildList())
            {
                if (LLSlider* candidate = ALViewType::as<LLSlider>(child))
                {
                    bar = candidate;
                }
            }
            return bar;
        };
        LLSlider* bar = barOf(r);
        REQUIRE(bar != nullptr);

        // On the track and off the thumb, which sits at the middle for 0.5:
        // a press on the thumb measures the skin's thumb image, and the
        // headless skin has none.
        const S32 x = 1;
        const S32 y = bar->getRect().getHeight() / 2;
        bar->handleMouseDown(x, y, MASK_NONE);
        CHECK(pressed == 1);
        CHECK(released == 0);
        bar->handleMouseUp(x, y, MASK_NONE);
        CHECK(released == 1);
        CHECK(pressed == 1);

        // A row that names neither is a row like every other: a press and a
        // release on its track are heard by neither callback.
        ALSettingRow* plain = row("SettingRowTestHeld");
        gFloaterView->addChild(plain);
        LLSlider* plain_bar = barOf(plain);
        REQUIRE(plain_bar != nullptr);
        plain_bar->handleMouseDown(x, plain_bar->getRect().getHeight() / 2, MASK_NONE);
        plain_bar->handleMouseUp(x, plain_bar->getRect().getHeight() / 2, MASK_NONE);
        {
            INFO("the plain row's ends are not heard");
            CHECK(pressed == 1);
            CHECK(released == 1);
        }
        plain->die();
        r->die();
    }

    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a vector row shows a lettered box per part and writes only that part", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlVariable*   control = vector("SettingRowTestWeights", LLVector3(1.f, 0.5f, 0.7f));
        ALSettingRow::Params p       = params("SettingRowTestWeights");
        p.name                       = "weights";
        p.label                      = "Lum weights";
        p.label_width                = 110;
        p.components                 = "R G";
        ALSettingRow* r              = LLUICtrlFactory::create<ALSettingRow>(p);
        CHECK(r->getSlider() == nullptr);
        REQUIRE(r->getLabelBox());
        CHECK(r->getLabelBox()->getText() == "Lum weights");
        LLSpinCtrl* red   = r->findChild<LLSpinCtrl>("weights.R");
        LLSpinCtrl* green = r->findChild<LLSpinCtrl>("weights.G");
        {
            INFO("a box per letter");
            REQUIRE(red);
            REQUIRE(green);
            CHECK_FALSE(r->findChild<LLSpinCtrl>("weights.B"));
        }
        {
            INFO("after the caption, as wide as a number");
            CHECK(red->getRect().mLeft == 110);
            CHECK(red->getRect().getWidth() == 68);
        }
        CHECK(green->getRect().mLeft == red->getRect().mRight + 4);
        CHECK(closeTo(red->getValue().asReal(), 1.0));
        CHECK(closeTo(green->getValue().asReal(), 0.5));
        bool lettered = false;
        for (LLView* child : *red->getChildList())
        {
            const LLTextBox* letter = ALViewType::as<LLTextBox>(child);
            lettered                = lettered || (letter && letter->getText() == "R");
        }
        {
            INFO("each named by its letter beside it, which scrubs");
            CHECK(lettered);
            CHECK(red->getScrub());
        }
        UNSCOPED_INFO("with no caption under it");
        CHECK_FALSE(r->findChild<LLTextBox>("weights.R_caption"));
        r->setFocus(true);
        UNSCOPED_INFO("the keyboard to the first box");
        CHECK((red->hasFocus() || gFocusMgr.childHasKeyboardFocus(red)));
        r->setFocus(false);
        r->setHighlighted(true);
        UNSCOPED_INFO("lit, its caption is");
        CHECK(r->getLabelBox()->ll::ui::SearchableControl::getHighlighted());
        r->setHighlighted(false);
        UNSCOPED_INFO("the row's value is its parts");
        CHECK(closeTo(r->getValue()[1].asReal(), 0.5));

        S32 writes = 0;
        boost::signals2::scoped_connection watching = control->getSignal()->connect(
            [&writes](LLControlVariable*, const LLSD&, const LLSD&) { ++writes; });
        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        green->setValue(LLSD(0.25));
        green->onCommit();
        UNSCOPED_INFO("its part written, the rest as they stood");
        CHECK(sameParts(control->getValue(), 1.0, 0.25, 0.7));
        CHECK(writes == 1);
        CHECK(commits == 1);

        control->set(LLSD().with(0, 0.5).with(1, 0.25).with(2, 0.3));
        UNSCOPED_INFO("a change from elsewhere shown");
        CHECK(closeTo(red->getValue().asReal(), 0.5));
        green->setValue(LLSD(0.5));
        green->onCommit();
        UNSCOPED_INFO("a part no box shows kept");
        CHECK(sameParts(control->getValue(), 0.5, 0.5, 0.3));

        r->setEnabled(false);
        CHECK_FALSE(red->getEnabled());
        CHECK_FALSE(green->getEnabled());
        r->die();
    }

    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a vector row resets the whole setting when a shown part differs", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        resetWords();
        LLControlVariable*   control = vector("SettingRowTestCentre", LLVector3(0.f, 0.f, 0.f));
        ALSettingRow::Params p       = params("SettingRowTestCentre", -1.f, 1.f);
        p.name                       = "centre";
        p.components                 = "X Y";
        p.letter_width               = 18;
        ALSettingRow* r              = LLUICtrlFactory::create<ALSettingRow>(p);
        const LLSpinCtrl* x_box      = r->findChild<LLSpinCtrl>("centre.X");
        REQUIRE(x_box != nullptr);
        const LLTextBox* letter      = nullptr;
        for (LLView* child : *x_box->getChildList())
        {
            letter = letter ? letter : ALViewType::as<LLTextBox>(child);
        }
        {
            INFO("the letters as wide as said");
            REQUIRE(letter);
            CHECK(letter->getRect().getWidth() == 18);
        }
        UNSCOPED_INFO("hidden at the default");
        CHECK_FALSE(r->getResetButton()->getVisible());
        control->set(LLSD().with(0, 0.0).with(1, 0.0).with(2, 0.4));
        {
            INFO("a part it does not show is not its difference");
            CHECK_FALSE(r->isModified());
            CHECK_FALSE(r->getResetButton()->getVisible());
        }
        control->set(LLSD().with(0, 0.0).with(1, 0.3).with(2, 0.4));
        {
            INFO("shown once a part it shows differs");
            CHECK(r->isModified());
            CHECK(r->getResetButton()->getVisible());
        }
        UNSCOPED_INFO("saying the default's parts");
        CHECK(r->getResetButton()->getToolTip().find("0.00, 0.00") != std::string::npos);
        r->getResetButton()->onCommit();
        UNSCOPED_INFO("the whole setting back");
        CHECK(sameParts(control->getValue(), 0.0, 0.0, 0.0));
        const LLSpinCtrl* y_box = r->findChild<LLSpinCtrl>("centre.Y");
        REQUIRE(y_box != nullptr);
        UNSCOPED_INFO("the boxes show it");
        CHECK(closeTo(y_box->getValue().asReal(), 0.0));
        r->die();
    }

    TEST_CASE_METHOD(alsettingrow_data, "alsettingrow: a one-part row writes and compares and resets only its part", "[alsettingrow]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        resetWords();
        LLControlVariable*   control = vector("SettingRowTestEffect", LLVector3(0.8f, 1.f, 0.f));
        ALSettingRow::Params p       = params("SettingRowTestEffect", 0.f, 2.f);
        p.part                       = 1;
        ALSettingRow* r              = LLUICtrlFactory::create<ALSettingRow>(p);
        REQUIRE(r->getSlider() != nullptr);
        UNSCOPED_INFO("over its part");
        CHECK(closeTo(r->getSlider()->getValueF32(), 1.0));
        r->getSlider()->setValue(0.5f);
        r->getSlider()->onCommit();
        UNSCOPED_INFO("its part written, the others kept");
        CHECK(sameParts(control->getValue(), 0.8, 0.5, 0.0));
        {
            INFO("its part differs");
            CHECK(r->isModified());
            CHECK(r->getResetButton()->getVisible());
        }
        UNSCOPED_INFO("saying its default");
        CHECK(r->getResetButton()->getToolTip().find("1.00") != std::string::npos);

        control->set(LLSD().with(0, 0.2).with(1, 1.0).with(2, 0.0));
        UNSCOPED_INFO("another part differing is not its difference");
        CHECK_FALSE(r->isModified());
        control->set(LLSD().with(0, 0.2).with(1, 0.5).with(2, 0.0));
        r->getResetButton()->onCommit();
        UNSCOPED_INFO("its part back, the other part left");
        CHECK(sameParts(control->getValue(), 0.2, 1.0, 0.0));
        r->die();
    }
}

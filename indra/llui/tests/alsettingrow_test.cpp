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

#include "../alsettingrow.h"

#include "../llbutton.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llslider.h"
#include "../llsliderctrl.h"
#include "../lltextbox.h"
#include "../llui.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"
#include "llcontrol.h"
#include "llxmlnode.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gSettingRowTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSettingRowTestAnonName;
}

namespace tut
{
    struct alsettingrow_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLControlGroup& config()
        {
            return *LLUI::getInstance()->getSettingGroup("config");
        }

        // One setting per test, put back to its default, so no test sees what
        // another left behind.
        static LLControlVariable* setting(const std::string& name, F32 value)
        {
            LLControlVariable* control = config().getControl(name).get();
            if (!control)
            {
                control = config().declareF32(name, value, std::string("A setting row test value"));
            }
            control->resetToDefault(false);
            return control;
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

    typedef test_group<alsettingrow_data> alsettingrow_test;
    typedef alsettingrow_test::object     alsettingrow_object;
    tut::alsettingrow_test alsettingrow_testgroup("alsettingrow");

    // A slider row becomes a setting row by changing its tag, so every one of
    // the slider's attributes has to be read by the name a slider writes.
    template<> template<>
    void alsettingrow_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const std::string source =
            "<setting_row name=\"r\" label=\"Knee\" label_width=\"120\" min_val=\"-1\" max_val=\"2\""
            " increment=\"0.05\" decimal_digits=\"3\" text_width=\"56\" show_text=\"true\""
            " can_edit_text=\"false\" show_reset=\"false\" control_name=\"RenderBloomKnee\"/>\n";
        LLXMLNodePtr node;
        ensure("parses", LLXMLNode::parseBuffer(source.data(), source.size(), node));

        ALSettingRow::Params p;
        LLXUIParser parser;
        parser.readXUI(node, p, "setting_row_test.xml");
        ensure_equals("label", p.label(), std::string("Knee"));
        ensure_equals("label_width", p.label_width(), 120);
        ensure("min_val", closeTo(p.min_value(), -1.0));
        ensure("max_val", closeTo(p.max_value(), 2.0));
        ensure("increment", closeTo(p.increment(), 0.05));
        ensure_equals("decimal_digits", p.decimal_digits(), 3);
        ensure("text_width said", p.text_width.isProvided());
        ensure_equals("text_width", p.text_width(), 56);
        ensure("can_edit_text", !p.can_edit_text());
        ensure("show_reset", !p.show_reset());
        ensure_equals("control_name", p.control_name(), std::string("RenderBloomKnee"));

        // And what a file leaves out is what every Lightbox row writes today.
        ALSettingRow::Params defaults;
        ensure_equals("label_width defaults to the rows' 140", defaults.label_width(), 140);
        ensure("can_edit_text defaults on", defaults.can_edit_text());
        ensure("show_reset defaults on", defaults.show_reset());
        ensure("text_width is not said unless said", !defaults.text_width.isProvided());
    }

    // Bound both ways, and a commit is one write: the row writes the setting,
    // the setting's signal comes back to the row, and nothing writes it again.
    template<> template<>
    void alsettingrow_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        ensure("the row reads the setting", closeTo(r->getValue().asReal(), 0.5));

        control->set(LLSD(0.75));
        ensure("and follows it", closeTo(r->getValue().asReal(), 0.75));
        ensure("the slider shows it", closeTo(r->getSlider()->getValueF32(), 0.75));

        S32 writes = 0;
        boost::signals2::scoped_connection watching = control->getSignal()->connect(
            [&writes](LLControlVariable*, const LLSD&, const LLSD&) { ++writes; });
        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        r->getSlider()->setValue(0.25f);
        r->getSlider()->onCommit();
        ensure("the slider's value reaches the setting", closeTo(control->getValue().asReal(), 0.25));
        ensure_equals("written once", writes, 1);
        ensure_equals("and the row says so once", commits, 1);
        r->die();
    }

    // The row is the only thing bound, so a walk for what a panel is bound to
    // finds one setting per row and not one per part of it.
    template<> template<>
    void alsettingrow_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        ensure("the row is bound", r->getControlVariable() != nullptr);
        ensure_equals("and nothing in it is", boundUnder(r), 0);
        r->die();
    }

    // The reset button is there while there is something to reset, pressing it
    // puts the default back and goes, and the row does not say it committed:
    // what cares about the setting heard the setting change.
    template<> template<>
    void alsettingrow_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestReset", 0.5f);
        ALSettingRow* r = row("SettingRowTestReset");
        ensure("hidden at the default", !r->getResetButton()->getVisible());

        control->set(LLSD(0.8));
        ensure("shown once changed", r->getResetButton()->getVisible());
        ensure("and the row says so", r->isModified());

        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        r->getResetButton()->onCommit();
        ensure("the default is back", closeTo(control->getValue().asReal(), 0.5));
        ensure("the slider shows it", closeTo(r->getSlider()->getValueF32(), 0.5));
        ensure("the button has gone", !r->getResetButton()->getVisible());
        ensure_equals("and the row did not commit", commits, 0);
        r->die();
    }

    // As shown, not as stored: a value that rounds to the default at the
    // row's decimals is the default as far as anyone looking can tell, so a
    // drag that came back to it does not leave a reset button lit.
    template<> template<>
    void alsettingrow_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestRounding", 0.7f);
        ALSettingRow* r = row("SettingRowTestRounding", 0.f, 1.f, 2);

        control->set(LLSD(0.6999999));
        ensure("not the default to the letter", !control->isDefault());
        ensure("but not modified as shown", !r->isModified());
        ensure("so no button", !r->getResetButton()->getVisible());

        control->set(LLSD(0.69));
        ensure("a change that shows, shows it", r->getResetButton()->getVisible());
        r->die();
    }

    // The button's place is kept whether it shows or not, at any width: the
    // slider ends where it always ends, so nothing moves when the button
    // appears, and a row and the rows above it line up.
    template<> template<>
    void alsettingrow_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        const S32 width = r->getRect().getWidth();
        const S32 slot = ALSettingRow::RESET_SIZE + ALSettingRow::RESET_GAP;
        ensure_equals("the button at the right edge", r->getResetButton()->getRect().mRight, width);
        ensure_equals("the slider ends before its slot", r->getSlider()->getRect().mRight, width - slot);

        r->reshape(width + 50, r->getRect().getHeight());
        ensure_equals("still at the right edge, wider", r->getResetButton()->getRect().mRight, width + 50);
        ensure_equals("and the slider still before it", r->getSlider()->getRect().mRight, width + 50 - slot);
        r->die();
    }

    // Greyed as one thing: the slider, its label and value, and the button,
    // whether by hand or by the control the row's enabled_control names.
    template<> template<>
    void alsettingrow_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        setting("SettingRowTestBinding", 0.5f);
        ALSettingRow* r = row("SettingRowTestBinding");
        r->setEnabled(false);
        ensure("the slider greys", !r->getSlider()->getEnabled());
        ensure("and the button", !r->getResetButton()->getEnabled());
        r->setEnabled(true);
        ensure("and both come back", r->getSlider()->getEnabled() && r->getResetButton()->getEnabled());
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
        ensure("open while the gate is", gated->getSlider()->getEnabled());
        gate->set(LLSD(false));
        ensure("greyed when it closes", !gated->getSlider()->getEnabled() && !gated->getResetButton()->getEnabled());
        gate->set(LLSD(true));
        gated->die();
    }

    // Bound to nothing it is still a slider: it takes and gives a value and
    // says when it changed -- which is all the vector rows need of a control --
    // and it never offers a reset it has nothing to reset with.
    template<> template<>
    void alsettingrow_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        r->setValue(LLSD(0.4));
        ensure("takes a value", closeTo(r->getValue().asReal(), 0.4));

        S32 commits = 0;
        r->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        r->getSlider()->setValue(0.6f);
        r->getSlider()->onCommit();
        ensure_equals("says when it changed", commits, 1);
        ensure("and gives the new value", closeTo(r->getValue().asReal(), 0.6));
        ensure("never a reset", !r->getResetButton()->getVisible());
        ensure("nor a claim to be modified", !r->isModified());
        r->die();
    }

    // The value box is wide enough for every number the range holds. A
    // slider sizes it from the maximum's logarithm, which under 1 has no room
    // for the leading zero; the row sizes it itself there, and only there.
    template<> template<>
    void alsettingrow_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("both have a value box", row_box && plain_box);
        ensure("under 1, the row's is wider than a slider's " + std::to_string(row_box->getRect().getWidth())
                   + " > " + std::to_string(plain_box->getRect().getWidth()),
               row_box->getRect().getWidth() > plain_box->getRect().getWidth());

        ALSettingRow* large = row(std::string(), 0.f, 10.f, 3);
        LLSliderCtrl* plain_large = slider_like(10.f);
        ensure_equals("over 1, exactly a slider's",
                      valueBoxOf(large->getSlider())->getRect().getWidth(),
                      valueBoxOf(plain_large)->getRect().getWidth());

        small->die();
        plain_small->die();
        large->die();
        plain_large->die();
    }

    // Found by its name and lit up by it, the way a slider is.
    template<> template<>
    void alsettingrow_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALSettingRow* r = row(std::string());
        ensure("searched for by its label", r->getSearchText().find("Strength") != std::string::npos);
        ensure_equals("which it can say", r->getLabel(), std::string("Strength"));
        LLTextBox* box = r->getLabelBox();
        ensure("and point at", box != nullptr);
        ensure_equals("the text box that shows it", box->getText(), std::string("Strength"));
        // The column every row's label takes when the file does not say: a
        // default the slider did not get would have it measure the word.
        ensure_equals("as wide as the rows' label column", box->getRect().getWidth(), 140);

        r->setHighlighted(true);
        ensure("lit, the slider's label is lit", r->getSlider()->getHighlighted());
        r->setHighlighted(false);
        ensure("and out again", !r->getSlider()->getHighlighted());
        r->die();
    }

    // A reset button that goes away with the keyboard in it would leave the
    // keyboard nowhere.
    template<> template<>
    void alsettingrow_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* control = setting("SettingRowTestFocus", 0.5f);
        ALSettingRow* r = row("SettingRowTestFocus");
        gFloaterView->addChild(r);
        control->set(LLSD(0.9));
        r->getResetButton()->setFocus(true);
        ensure("the button has the keyboard", r->getResetButton()->hasFocus());

        control->resetToDefault(true);
        ensure("gone", !r->getResetButton()->getVisible());
        ensure("and the keyboard with it", !r->getResetButton()->hasFocus());
        r->die();
    }

    // A row says when a drag begins and ends, the way a slider does and by the
    // names a slider writes: a setting that is dear to apply waits for the
    // release. The callbacks are named in the file, so they are resolved
    // against the registrar in scope while the row is built.
    template<> template<>
    void alsettingrow_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("parses", LLXMLNode::parseBuffer(source.data(), source.size(), node));
        ALSettingRow::Params p(LLUICtrlFactory::getDefaultParams<ALSettingRow>());
        LLXUIParser parser;
        parser.readXUI(node, p, "setting_row_test.xml");
        ensure("press said", p.mouse_down_callback.isProvided());
        ensure("release said", p.mouse_up_callback.isProvided());
        p.rect = LLRect(0, 18, 360, 0);

        ALSettingRow* r = LLUICtrlFactory::create<ALSettingRow>(p);
        gFloaterView->addChild(r);
        LLSlider* bar = nullptr;
        for (LLView* child : *r->getSlider()->getChildList())
        {
            if (LLSlider* candidate = ALViewType::as<LLSlider>(child))
            {
                bar = candidate;
            }
        }
        ensure("the row's slider has a bar", bar != nullptr);

        // On the track and off the thumb, which sits at the middle for 0.5:
        // a press on the thumb measures the skin's thumb image, and the
        // headless skin has none.
        const S32 x = 1;
        const S32 y = bar->getRect().getHeight() / 2;
        bar->handleMouseDown(x, y, MASK_NONE);
        ensure_equals("the press is told once", pressed, 1);
        ensure_equals("and no release yet", released, 0);
        bar->handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the release is told once", released, 1);
        ensure_equals("and no second press", pressed, 1);

        // A row that names neither is a row like every other.
        ALSettingRow* plain = row("SettingRowTestHeld");
        ensure("no press asked for", !params("SettingRowTestHeld").mouse_down_callback.isProvided());
        plain->die();
        r->die();
    }
}

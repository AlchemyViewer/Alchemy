/**
 * @file alfloaterinpanelprobe.cpp
 * @brief Builds a floater inside a panel and reports what that costs.
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterinpanelprobe.h"

#include "alxuishellbuild.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfocusmgr.h"
#include "llmultifloater.h"
#include "llpanel.h"
#include "lltexteditor.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include <fmt/format.h>

namespace
{
    // The multi_floater with the least behind it: a tab container, a title,
    // and no viewer class of its own.
    constexpr const char* HOST_FILE = "floater_script_debug.xml";

    std::string typeName(const LLView* view)
    {
        return view ? view->viewType()->mName : "-";
    }

    std::string named(const LLView* view)
    {
        return view ? fmt::format("{} ({})", view->getName(), typeName(view)) : "none";
    }

    std::string rectText(const LLRect& r)
    {
        return fmt::format("{},{} to {},{}  [{} x {}]",
                           r.mLeft, r.mBottom, r.mRight, r.mTop, r.getWidth(), r.getHeight());
    }

    std::string yesNo(bool b) { return b ? "yes" : "no"; }
}

ALFloaterInPanelProbe::ALFloaterInPanelProbe(const LLSD& key)
:   LLFloater(key)
{
}

ALFloaterInPanelProbe::~ALFloaterInPanelProbe()
{
    onClear();
}

bool ALFloaterInPanelProbe::postBuild()
{
    mStage = getChild<LLPanel>("stage");
    mFile = getChild<LLComboBox>("file_combo");
    mTame = getChild<LLCheckBoxCtrl>("tame_check");
    mMulti = getChild<LLCheckBoxCtrl>("multi_check");
    mReport = getChild<LLTextEditor>("report");
    mLive = getChild<LLTextEditor>("live");

    mFile->add("floater_beacons.xml");
    mFile->add("floater_about_land.xml");
    mFile->add("floater_camera.xml");
    mFile->add("floater_stats.xml");
    mFile->add("floater_preferences.xml");
    mFile->selectFirstItem();

    getChild<LLButton>("build_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onBuild(); });
    getChild<LLButton>("clear_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onClear(); });
    getChild<LLButton>("focus_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onFocusBuilt(); });

    mReport->setValue(
        "Choose a floater file and press Build.\n"
        "\n"
        "The two spinners above are the other half of this: both ask for\n"
        "scrub=\"true\", and neither is wired to anything.\n"
        "\n"
        "  Scrub [    ]  has a label, so the label is the handle and the drag\n"
        "                is sideways.\n"
        "        [    ]  has none, so its up and down buttons are the handle\n"
        "                and the drag is vertical. A press that does not\n"
        "                travel is still the click it always was.\n"
        "\n"
        "  Shift is a hundredth of the increment, control a tenth, alt ten\n"
        "  times -- the three the buttons already answer to.\n");
    return true;
}

void ALFloaterInPanelProbe::draw()
{
    if (mLiveTimer.getElapsedTimeF32() > 0.25f)
    {
        mLiveTimer.reset();
        writeLive();
    }
    LLFloater::draw();
}

// The floater's own gestures, turned off the way Phase A would have to turn
// them off: the canvas moves and sizes what it holds, and a preview that
// drags itself out of its canvas is not a preview.
void ALFloaterInPanelProbe::tame(LLFloater* built)
{
    built->setCanResize(false);
    built->enableResizeCtrls(false);
    built->setCanClose(false);
    built->setCanMinimize(false);
    built->setCanTearOff(false);
    built->setCanDrag(false);
}

void ALFloaterInPanelProbe::onClear()
{
    if (LLFloater* built = mBuilt.get())
    {
        built->closeFloater();
    }
    if (LLFloater* host = mHost.get())
    {
        host->closeFloater();
    }
    mBuilt = LLHandle<LLFloater>();
    mHost = LLHandle<LLFloater>();
}

void ALFloaterInPanelProbe::onFocusBuilt()
{
    if (LLFloater* built = mBuilt.get())
    {
        built->setFocus(true);
    }
}

void ALFloaterInPanelProbe::onBuild()
{
    onClear();

    const std::string file = mFile->getSimple();
    std::string out;

    LLXMLNodePtr node;
    if (file.empty() || !LLUICtrlFactory::getLayeredXMLNode(file, node))
    {
        mReport->setValue(fmt::format("Could not read {}.", file));
        return;
    }
    if (!node->hasName("floater") && !node->hasName("multi_floater"))
    {
        mReport->setValue(fmt::format("{} is a <{}>, not a floater.", file, node->getName()->mString));
        return;
    }

    S32 asked_width = 0;
    S32 asked_height = 0;
    node->getAttributeS32("width", asked_width);
    node->getAttributeS32("height", asked_height);
    bool saves_rect = false;
    node->getAttributeBOOL("save_rect", saves_rect);

    // The layers merged, the class= panels built as plain panels, and the
    // build kept out of the viewer's own singletons.
    ALXUIShellBuild shell;

    LLMultiFloater* host = nullptr;
    if (mMulti->get())
    {
        LLXMLNodePtr host_node;
        if (LLUICtrlFactory::getLayeredXMLNode(HOST_FILE, host_node))
        {
            host = new LLMultiFloater(LLSD(), LLFloater::getDefaultParams());
            mStage->addChild(host);
            if (host->initFloaterXML(host_node, mStage, HOST_FILE))
            {
                host->setTitle("multi_floater in a panel");
                host->setOrigin(0, 0);
                host->reshape(mStage->getRect().getWidth(), mStage->getRect().getHeight());
                host->setVisible(true);
                mHost = host->getHandle();
            }
            else
            {
                mStage->removeChild(host);
                delete host;
                host = nullptr;
            }
        }
    }

    LLFloater* built = new LLFloater(LLSD(), LLFloater::getDefaultParams());

    // Parented before the XML is read, because the last thing
    // LLFloater::initFloater does is give itself to the floater view if
    // nobody else has claimed it.
    mStage->addChild(built);
    const bool claimed = built->getParent() == mStage;

    if (!built->initFloaterXML(node, mStage, file))
    {
        mStage->removeChild(built);
        delete built;
        mReport->setValue(fmt::format("Could not build {}.", file));
        return;
    }
    mBuilt = built->getHandle();

    const LLRect after_build = built->getRect();

    if (host)
    {
        host->addFloater(built, true);
    }
    if (mTame->get())
    {
        tame(built);
    }
    built->setVisible(true);

    out += fmt::format("{}\n\n", file);

    out += "BUILD\n";
    out += fmt::format("  the file asks for       {} x {}\n", asked_width, asked_height);
    out += fmt::format("  parented before init    {}\n", yesNo(claimed));
    out += fmt::format("  parent now              {}\n", named(built->getParent()));
    out += fmt::format("  in the floater view     {}\n", yesNo(built->getParent() == gFloaterView));
    out += fmt::format("  rect after the build    {}\n", rectText(after_build));
    out += fmt::format("  rect now                {}\n", rectText(built->getRect()));
    out += fmt::format("  the stage is            {}\n", rectText(mStage->getLocalRect()));
    out += fmt::format("  inside the stage        {}\n",
                       yesNo(mStage->getLocalRect().contains(built->getRect())));
    out += fmt::format("  resized by the build    {}\n",
                       yesNo(asked_width && (after_build.getWidth() != asked_width
                                             || after_build.getHeight() != asked_height)));
    out += fmt::format("  the file saves its rect {}\n", yesNo(saves_rect));
    out += "\n";

    out += "WHAT IT STILL BELIEVES\n";
    out += fmt::format("  isResizable             {}\n", yesNo(built->isResizable()));
    out += fmt::format("  isMinimizeable          {}\n", yesNo(built->isMinimizeable()));
    out += fmt::format("  isFrontmost             {}\n", yesNo(built->isFrontmost()));
    out += fmt::format("  getHost                 {}\n", named(built->getHost()));
    out += fmt::format("  getCanDrag              {}\n", yesNo(built->getCanDrag()));
    out += fmt::format("  chrome tamed            {}\n", yesNo(mTame->get()));
    out += "\n";

    out += "WHAT THE READING SAYS TO EXPECT\n";
    out += "  LLFloaterView::adjustToFitScreen returns early for a floater whose\n";
    out += "  parent is not the floater view, and bringToFront, setFrontmost and\n";
    out += "  handleReshape all ask getParentAs<LLFloaterView>(), which is null\n";
    out += "  here. isFrontmost asks getParentByType instead, so it walks up to\n";
    out += "  this window's own floater view and answers no forever -- which is\n";
    out += "  what makes a nested floater draw with the inactive transparency.\n";
    out += "\n";

    out += "TRY THESE BY HAND\n";
    out += "  1. Drag its title bar. Does it leave the stage?\n";
    out += "  2. Drag its resize corner. Does it resize, and against what limit?\n";
    out += "  3. Click inside it, then read the live focus lines below.\n";
    out += "  4. Press Escape with focus inside it. Which floater closes?\n";
    out += "  5. Click its close button. Does the stage keep it?\n";
    out += "  6. Resize this window. Does the stage clip it or does it draw over\n";
    out += "     the report?\n";
    out += "  7. Compare its title bar's ink with this window's. Is it dimmed?\n";
    out += "  8. Build it again with the chrome tamed, and repeat 1, 2 and 5.\n";
    out += "  9. Build it again in a multi_floater, and repeat all of them.\n";
    out += " 10. In a multi_floater, press its tear-off button. It should land\n";
    out += "     in the stage, beside its host. The live lines say where it\n";
    out += "     went.\n";

    mReport->setValue(out);
    writeLive();
}

void ALFloaterInPanelProbe::writeLive()
{
    if (!mLive)
    {
        return;
    }
    LLFloater* built = mBuilt.get();
    if (!built)
    {
        mLive->setValue("Nothing built.");
        return;
    }

    LLView* focus = gFocusMgr.getKeyboardFocusView();
    const std::string out = fmt::format(
        "rect            {}\n"
        "parent          {}{}\n"
        "host            {}\n"
        "visible         {}   frontmost   {}\n"
        "keyboard focus  {}\n"
        "  inside it     {}\n"
        "top ctrl        {}\n"
        "mouse capture   {}\n"
        "the front floater of the floater view is {}",
        rectText(built->getRect()),
        named(built->getParent()),
        built->getParent() == gFloaterView ? "   <- it has left the stage" : "",
        named(built->getHost()),
        yesNo(built->getVisible()), yesNo(built->isFrontmost()),
        named(focus),
        yesNo(focus && (focus == built || focus->hasAncestor(built))),
        named(gFocusMgr.getTopCtrl()),
        named(gFocusMgr.getMouseCaptureView()),
        named(gFloaterView ? gFloaterView->getFrontmost() : nullptr));

    mLive->setValue(out);
}

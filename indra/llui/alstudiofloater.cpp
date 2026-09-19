/**
 * @file alstudiofloater.cpp
 * @brief A window laid out as a studio is, and what every such window keeps
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

#include "alstudiofloater.h"

#include "alpopover.h"
#include "llcontrol.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llmenugl.h"
#include "lltextbox.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

ALStudioFloater::ALStudioFloater(const LLSD& key, std::string state_setting)
:   LLFloater(key),
    mStateSetting(std::move(state_setting))
{
}

void ALStudioFloater::setMenuBar(LLMenuBarGL* menu_bar)
{
    mMenuBar = menu_bar;
    mUndoItem = menu_bar ? menu_bar->findChild<LLMenuItemGL>("undo", true) : nullptr;
    mRedoItem = menu_bar ? menu_bar->findChild<LLMenuItemGL>("redo", true) : nullptr;
}

bool ALStudioFloater::handleMenuAccelerator(KEY key, MASK mask)
{
    return mMenuBar && mMenuBar->handleAcceleratorKey(key, mask);
}

bool ALStudioFloater::handleUndoKeys(KEY key, MASK mask)
{
    const bool undo_key = (key == 'Z' && mask == MASK_CONTROL);
    const bool redo_key = (key == 'Y' && mask == MASK_CONTROL) || (key == 'Z' && mask == (MASK_CONTROL | MASK_SHIFT));
    if (!undo_key && !redo_key)
    {
        return false;
    }

    LLEditMenuHandler* text = LLEditMenuHandler::gEditMenuHandler;
    LLView* text_view = text ? text->asView() : nullptr;
    if (text_view && text_view->hasAncestor(this))
    {
        if (undo_key && text->canUndo())
        {
            text->undo();
            return true;
        }
        if (redo_key && text->canRedo())
        {
            text->redo();
            return true;
        }
    }

    if (undo_key)
    {
        undo();
    }
    else
    {
        redo();
    }
    return true;
}

void ALStudioFloater::sayUndoRedo(const std::string& undo_what, const std::string& redo_what)
{
    if (mUndoItem)
    {
        mUndoItem->setLabel(undo_what.empty() ? getString("MenuUndo")
                                              : getString("MenuUndoWhat", LLStringUtil::format_map_t{ { "[WHAT]", undo_what } }));
    }
    if (mRedoItem)
    {
        mRedoItem->setLabel(redo_what.empty() ? getString("MenuRedo")
                                              : getString("MenuRedoWhat", LLStringUtil::format_map_t{ { "[WHAT]", redo_what } }));
    }
}

void ALStudioFloater::showHistory(ALHistoryList* list, std::vector<ALHistoryList::Step> steps, size_t in_force)
{
    const std::string undo_what = in_force > 0 && in_force <= steps.size() ? steps[in_force - 1].what : std::string();
    const std::string redo_what = in_force < steps.size() ? steps[in_force].what : std::string();
    sayUndoRedo(undo_what, redo_what);
    if (list)
    {
        list->setSteps(std::move(steps), in_force);
    }
}

void ALStudioFloater::quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder,
                                const std::string& title, std::function<void(const std::string&)> chose,
                                LLView* anchor, S32 width)
{
    // Asked for again while it is up -- the same key pressed twice -- it is
    // what was typed that is wanted back, not a fresh field.
    if (LLView* up = mQuickPopover.get())
    {
        if (ALQuickOpen* quick = up->findChild<ALQuickOpen>("quick_open"))
        {
            quick->setCandidates(std::move(candidates));
            quick->takeFocus();
            return;
        }
        if (ALPopover* popover = ALViewType::as<ALPopover>(up))
        {
            popover->escape();
        }
        else
        {
            up->die();
        }
    }
    mQuickPopover.markDead();

    constexpr S32 WIDTH = 460;
    constexpr S32 HEIGHT = 300;

    ALQuickOpen::Params qp(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
    qp.name = "quick_open";
    qp.rect = LLRect(0, HEIGHT, width > 0 ? width : WIDTH, 0);
    qp.placeholder = placeholder;
    ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(qp);
    quick->setCandidates(std::move(candidates));

    // The popover takes the content, and takes it even when it cannot show.
    ALPopover* popover = ALPopover::show(anchor ? anchor : this, quick, title);
    if (!popover)
    {
        return;
    }
    mQuickPopover = popover->getHandle();
    LLHandle<ALPopover> held = popover->getDerivedHandle<ALPopover>();
    quick->onChose([held, chose = std::move(chose)](const std::string& value)
    {
        // Settled first, so the keyboard comes back to the window before
        // what was chosen is acted on -- a choice that puts the keyboard
        // somewhere needs it back to give.
        if (ALPopover* up = held.get())
        {
            up->settle();
        }
        chose(value);
    });
    popover->onClosed([this](bool) { mQuickPopover.markDead(); });
    quick->takeFocus();
}

void ALStudioFloater::setStatus(const std::string& text, bool failure)
{
    static const LLUIColor normal = LLUIColorTable::instance().getColor("TextFgColor", LLColor4::white);
    static const LLUIColor alarm = LLUIColorTable::instance().getColor("LtOrange", LLColor4::yellow);

    if (mStatus)
    {
        mStatus->setColor(failure ? alarm : normal);
        mStatus->setText(text);
    }
}

// Before anything else: a pane left in a window this one does not own
// goes down with it.
void ALStudioFloater::onClose(bool app_quitting)
{
    saveState();
    mFolds.dockAll();
    LLFloater::onClose(app_quitting);
}

void ALStudioFloater::draw()
{
    rememberShape();
    LLFloater::draw();
}

// Told to the per-account control the floater reads its rect from, so
// that the floater's own bookkeeping -- where it sits relative to the
// screen, what it writes back -- runs as it would have. The relative
// position the account kept is let go of, since it would otherwise
// outrank the rect it was derived from.
bool ALStudioFloater::applyRectControl()
{
    if (!mRestoredRect.isEmpty() && !mRectControl.empty())
    {
        LLControlGroup* group = getControlGroup();
        group->setRect(mRectControl, mRestoredRect);
        for (const std::string& name : { mPosXControl, mPosYControl })
        {
            if (LLControlVariable* control = group->getControl(name))
            {
                control->resetToDefault();
            }
        }
    }
    return LLFloater::applyRectControl();
}

// Not in the middle of a drag: the shape worth keeping is the one it ends
// at. And not while minimized, when the rect is a title bar.
void ALStudioFloater::rememberShape()
{
    if (gFocusMgr.getMouseCapture() || isMinimized())
    {
        return;
    }
    if (getRect() != mShapeRect || mFolds.dims() != mShapeDims)
    {
        saveState();
    }
}

void ALStudioFloater::saveState()
{
    LLSD state;
    writeState(state);
    // Which regions are folded, how big each is, and which are out in
    // windows of their own and where; a folded region remembers the size
    // it unfolds to.
    mFolds.save(state);
    // And the window itself, left, bottom, right, top.
    const LLRect r = getRect();
    state["rect"] = LLSD::emptyArray();
    state["rect"].append(r.mLeft);
    state["rect"].append(r.mBottom);
    state["rect"].append(r.mRight);
    state["rect"].append(r.mTop);
    mShapeRect = r;
    mShapeDims = mFolds.dims();
    if (LLControlGroup* settings = LLUI::getInstance()->getSettingGroup("config"))
    {
        settings->setLLSD(mStateSetting, state);
    }
}

void ALStudioFloater::loadState()
{
    LLControlGroup* settings = LLUI::getInstance()->getSettingGroup("config");
    const LLSD state = settings ? settings->getLLSD(mStateSetting) : LLSD();
    if (!state.isMap())
    {
        return;
    }
    readState(state);
    mFolds.load(state);
    if (state.has("rect") && state["rect"].size() == 4)
    {
        mRestoredRect = LLRect(state["rect"][0].asInteger(), state["rect"][3].asInteger(),
                               state["rect"][2].asInteger(), state["rect"][1].asInteger());
    }
}

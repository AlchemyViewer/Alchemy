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

#include "alsurface.h"
#include "llcontrol.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llmenugl.h"
#include "lltextbox.h"
#include "lltimer.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <memory>
#include <utility>

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

void ALStudioFloater::addCommand(const KeyedCommand& command, std::function<bool()> run)
{
    mCommands.push_back({ command, std::move(run) });
}

bool ALStudioFloater::runCommandKey(KEY pressed, MASK mask)
{
    if (pressed == KEY_NONE)
    {
        return false;
    }
    const KEY key = alKeyAsBound(pressed);
    if (runChord(ALKeyChord{ key, mask }))
    {
        return true;
    }
    // The first of two: the next key is this window's, wherever the
    // keyboard is.
    const bool leads = std::any_of(mCommands.begin(), mCommands.end(), [&](const Registered& one) {
        const std::vector<ALKeyChord> keys = keysOf(one.command);
        return std::any_of(keys.begin(), keys.end(), [&](const ALKeyChord& chord) { return chord.ledBy(key, mask); });
    });
    if (!leads)
    {
        return false;
    }
    const LLHandle<LLFloater> handle = getHandle();
    ALKeyChords::wait(this, key, mask, [handle, key, mask](KEY second, MASK second_mask) {
        if (ALStudioFloater* window = ALViewType::as<ALStudioFloater>(handle.get()))
        {
            window->finishChord(key, mask, second, second_mask);
        }
    });
    if (hasString("ChordWaiting"))
    {
        setStatus(getString("ChordWaiting", LLStringUtil::format_map_t{ { "[KEYS]", ALKeyChord{ key, mask }.describe() } }));
    }
    return true;
}

bool ALStudioFloater::runChord(const ALKeyChord& chord)
{
    for (const Registered& one : mCommands)
    {
        const std::vector<ALKeyChord> keys = keysOf(one.command);
        if (std::find(keys.begin(), keys.end(), chord) != keys.end() && one.run && one.run())
        {
            return true;
        }
    }
    return false;
}

void ALStudioFloater::finishChord(KEY lead_key, MASK lead_mask, KEY key, MASK mask)
{
    // The wait said no longer, before the command says anything of its own.
    if (hasString("ChordWaiting"))
    {
        setStatus(std::string());
    }
    const ALKeyChord chord{ key, mask, lead_key, lead_mask };
    const MASK       held = lead_mask & (MASK_CONTROL | MASK_MAC_CONTROL);
    const bool       was  = std::exchange(mByKeys, true);
    const bool       ran  = runChord(chord) || (held != MASK_NONE && (mask & held) == held && runChord(ALKeyChord{ key, static_cast<MASK>(mask & ~held), lead_key, lead_mask }));
    mByKeys               = was;
    if (ran)
    {
        return;
    }
    if (hasString("ChordNone"))
    {
        setStatus(getString("ChordNone", LLStringUtil::format_map_t{ { "[KEYS]", chord.describe() } }), true);
    }
}

bool ALStudioFloater::handleStudioKeys(KEY key, MASK mask)
{
    const bool was = std::exchange(mByKeys, true);
    const bool taken = handleMenuAccelerator(key, mask) || handleUndoKeys(key, mask) || runCommandKey(key, mask);
    mByKeys = was;
    return taken;
}

namespace
{
    // Held with Control, Command or Alt: not the modifier on its own, which
    // is no chord yet.
    bool isChord(KEY key, MASK mask)
    {
        return (mask & (MASK_CONTROL | MASK_ALT | MASK_MAC_CONTROL)) != 0 && key != KEY_CONTROL && key != KEY_ALT && key != KEY_SHIFT;
    }

    // What a window keeping its chords lets go on: Quit, and moving between
    // windows, with the Mac's own Control key as the viewer takes it.
    bool passesOn(KEY key, MASK mask)
    {
        return (key == 'Q' && mask == MASK_CONTROL) || (key == KEY_TAB && (mask & (MASK_CONTROL | MASK_MAC_CONTROL)) != 0);
    }
}

bool ALStudioFloater::handleKeyHere(KEY key, MASK mask)
{
    if (handleStudioKeys(key, mask) || LLFloater::handleKeyHere(key, mask))
    {
        return true;
    }
    if (!mKeepChords || !isChord(key, mask) || passesOn(key, mask))
    {
        return false;
    }
    handleEditKeys(key, mask);
    return true;
}

bool ALStudioFloater::handleEditKeys(KEY key, MASK mask)
{
    // The one the keyboard is in, wherever that window is: the key came
    // from it, up through this window or home to it.
    LLEditMenuHandler* text = LLEditMenuHandler::gEditMenuHandler;
    LLView*            view = text ? text->asView() : nullptr;
    if (mask != MASK_CONTROL || !view || !gFocusMgr.childHasKeyboardFocus(view))
    {
        return false;
    }
    switch (key)
    {
        case 'X':
            if (text->canCut())
            {
                text->cut();
            }
            return true;
        case 'C':
            if (text->canCopy())
            {
                text->copy();
            }
            return true;
        case 'V':
            if (text->canPaste())
            {
                text->paste();
            }
            return true;
        case 'A':
            if (text->canSelectAll())
            {
                text->selectAll();
            }
            return true;
        default:
            return false;
    }
}

ALStudioFloater::UndoKey ALStudioFloater::undoKeyOf(KEY key, MASK mask) const
{
    if (key == 'Z' && mask == MASK_CONTROL)
    {
        return UndoKey::Undo;
    }
    return (key == 'Y' && mask == MASK_CONTROL) || (key == 'Z' && mask == (MASK_CONTROL | MASK_SHIFT)) ? UndoKey::Redo : UndoKey::None;
}

bool ALStudioFloater::handleUndoKeys(KEY key, MASK mask)
{
    const UndoKey which    = undoKeyOf(key, mask);
    const bool    undo_key = which == UndoKey::Undo;
    const bool    redo_key = which == UndoKey::Redo;
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

// static
void ALStudioFloater::goToStep(size_t target, const std::function<size_t()>& in_force, const std::function<bool()>& back,
                               const std::function<bool()>& forward, size_t bound)
{
    while (in_force() > target && bound-- > 0)
    {
        if (!back())
        {
            return;
        }
    }
    while (in_force() < target && bound-- > 0)
    {
        if (!forward())
        {
            return;
        }
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

ALQuickOpen* ALStudioFloater::quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder,
                                        const std::string& title, std::function<void(const std::string&)> chose,
                                        LLView* anchor, S32 width, S32 height, std::function<void()> escaped,
                                        std::function<void(const std::string&)> hold, std::function<void()> left)
{
    return mQuickAsk.ask(std::move(candidates), placeholder, title, std::move(chose), anchor ? anchor : this, width, height, std::move(escaped),
                         std::move(hold), std::move(left));
}

void ALStudioFloater::setStatus(const std::string& text, bool failure)
{
    // The text box's own ink: the field's, TextFgColor, is black in a skin
    // whose fields are light, and the status line is on the panel.
    const LLUIColor& normal = ALSurface::text();
    static const LLUIColor alarm = LLUIColorTable::instance().getColor("LtOrange", LLColor4::yellow);

    if (mStatus)
    {
        mStatus->setColor(failure ? alarm : normal);
        mStatus->setText(text);
    }
    mStatusSaidAt  = LLTimer::getTotalSeconds();
    mStatusFailure = failure;
    mStatusQuiet   = text.empty();
}

LLColor4 ALStudioFloater::quietStatusColor() const
{
    // The skin's quiet words are a quarter of white in some skins, which
    // over a dark window reads at about 2:1: quiet, not gone.
    return ALSurface::legible(ALSurface::text().get(), ALSurface::quiet().get(), getBackgroundColor());
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
    // What the status line said, gone quiet once it is old: a quarter of a
    // minute for news, a minute for a failure, which is read later.
    constexpr F64 NEWS_SECONDS    = 15.0;
    constexpr F64 FAILURE_SECONDS = 60.0;
    if (mStatus && !mStatusQuiet && LLTimer::getTotalSeconds() - mStatusSaidAt > (mStatusFailure ? FAILURE_SECONDS : NEWS_SECONDS))
    {
        mStatus->setColor(quietStatusColor());
        mStatusQuiet = true;
    }
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
    if (getRect() != mShapeRect || !mFolds.dimsAre(mShapeDims))
    {
        saveState();
    }
}

void ALStudioFloater::saveState()
{
    if (mStateSetting.empty())
    {
        return;
    }
    LLSD state;
    writeState(state);
    // Which regions are folded, how big each is, and which are out in
    // windows of their own and where; a folded region remembers the size
    // it unfolds to.
    mFolds.save(state);
    // And the window itself, left, bottom, right, top: as it opens, which
    // while it is minimized is not the title bar it is drawn as.
    const LLRect r = isMinimized() && !getExpandedRect().isEmpty() ? getExpandedRect() : getRect();
    state["rect"] = LLSD::emptyArray();
    state["rect"].append(r.mLeft);
    state["rect"].append(r.mBottom);
    state["rect"].append(r.mRight);
    state["rect"].append(r.mTop);
    mShapeRect = r;
    mShapeDims = mFolds.dims();
    // What the next open puts back is the shape as it is now, not as it
    // was when the state was read: a window closed and opened again
    // stays where it was left.
    mRestoredRect = r;
    if (LLControlGroup* settings = LLUI::getInstance()->getSettingGroup("config"))
    {
        settings->setLLSD(mStateSetting, state);
    }
}

bool ALStudioFloater::loadState()
{
    if (mStateSetting.empty())
    {
        return false;
    }
    LLControlGroup* settings = LLUI::getInstance()->getSettingGroup("config");
    const LLSD state = settings ? settings->getLLSD(mStateSetting) : LLSD();
    if (!state.isMap())
    {
        return false;
    }
    readState(state);
    mFolds.load(state);
    if (state.has("rect") && state["rect"].size() == 4)
    {
        mRestoredRect = LLRect(state["rect"][0].asInteger(), state["rect"][3].asInteger(),
                               state["rect"][2].asInteger(), state["rect"][1].asInteger());
    }
    return true;
}

/**
 * @file alkeycapture.cpp
 * @brief Keys pressed to be given to a command, in a popover of their own.
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

#include "alkeycapture.h"

#include "alpopover.h"
#include "llbutton.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

namespace
{
    constexpr S32 WIDTH    = 360;
    constexpr S32 HEIGHT   = 140;
    constexpr S32 PAD      = 8;
    constexpr S32 BUTTON_W = 80;
    constexpr S32 BUTTON_H = 22;

    LLPanel::Params panelParams()
    {
        LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
        p.name = "key_capture";
        p.rect = LLRect(0, HEIGHT, WIDTH, 0);
        return p;
    }
}

ALKeyCapture::ALKeyCapture(const Words& words, bool two_keys, about_t about, chosen_t chosen)
:   LLPanel(panelParams()),
    mWords(words),
    mTwoKeys(two_keys),
    mAbout(std::move(about)),
    mChosen(std::move(chosen))
{
    // The panel itself has the keyboard, which the viewer gives the wait
    // before anything; a click on a button leaves it where it is.
    setTabStop(true);

    LLTextBox::Params text(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    text.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP);
    text.wrap(true);
    text.font(LLFontGL::getFontSansSerifSmall());
    text.name("prompt");
    text.rect(LLRect(PAD, HEIGHT - PAD, WIDTH - PAD, HEIGHT - PAD - 30));
    text.initial_value(mWords.prompt);
    addChild(LLUICtrlFactory::create<LLTextBox>(text));

    text.name("keys");
    text.wrap(false);
    text.font(LLFontGL::getFontSansSerifBig());
    text.rect(LLRect(PAD, HEIGHT - PAD - 34, WIDTH - PAD, HEIGHT - PAD - 58));
    text.initial_value(mWords.nothing);
    mKeys = LLUICtrlFactory::create<LLTextBox>(text);
    addChild(mKeys);

    text.name("said");
    text.wrap(true);
    text.font(LLFontGL::getFontSansSerifSmall());
    text.rect(LLRect(PAD, HEIGHT - PAD - 62, WIDTH - PAD, PAD + BUTTON_H + 4));
    text.initial_value(std::string());
    mSaid = LLUICtrlFactory::create<LLTextBox>(text);
    addChild(mSaid);

    LLButton::Params button(LLUICtrlFactory::getDefaultParams<LLButton>());
    button.follows.flags(FOLLOWS_RIGHT | FOLLOWS_BOTTOM);
    button.tab_stop(false);
    button.name("set");
    button.label(mWords.set);
    button.rect(LLRect(WIDTH - PAD - 2 * BUTTON_W - 6, PAD + BUTTON_H, WIDTH - PAD - BUTTON_W - 6, PAD));
    button.commit_callback.function([this](LLUICtrl*, const LLSD&) { give(); });
    mSet = LLUICtrlFactory::create<LLButton>(button);
    addChild(mSet);

    button.name("cancel");
    button.label(mWords.cancel);
    button.rect(LLRect(WIDTH - PAD - BUTTON_W, PAD + BUTTON_H, WIDTH - PAD, PAD));
    button.commit_callback.function([this](LLUICtrl*, const LLSD&) {
        if (ALPopover* up = popover())
        {
            up->escape();
        }
    });
    addChild(LLUICtrlFactory::create<LLButton>(button));
    showPressed();
}

// static
ALKeyCapture* ALKeyCapture::show(LLView* anchor, const Words& words, bool two_keys, about_t about, chosen_t chosen)
{
    ALKeyCapture* capture = new ALKeyCapture(words, two_keys, std::move(about), std::move(chosen));
    // Over the list it was asked from, as a prompt over a window goes.
    if (!ALPopover::showOver(anchor, capture, words.title))
    {
        return nullptr;
    }
    capture->setFocus(true);
    capture->listen();
    return capture;
}

ALPopover* ALKeyCapture::popover() const
{
    return getParentByType<ALPopover>();
}

void ALKeyCapture::listen()
{
    const LLHandle<LLPanel> handle = getHandle();
    ALKeyChords::wait(this, KEY_NONE, MASK_NONE, [handle](KEY key, MASK mask) {
        if (ALKeyCapture* capture = ALViewType::as<ALKeyCapture>(handle.get()))
        {
            capture->press(key, mask);
            if (!capture->isDead() && capture->getVisible() && capture->hasFocus())
            {
                capture->listen();
            }
        }
    });
}

void ALKeyCapture::press(KEY key, MASK mask)
{
    if (key == KEY_SHIFT || key == KEY_CONTROL || key == KEY_ALT)
    {
        return;
    }
    if (gKeyboard && gKeyboard->getKeyRepeated(key))
    {
        // Held down: the key it was, pressed once.
        return;
    }
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        if (ALPopover* up = popover())
        {
            up->escape();
        }
        return;
    }
    if (key == KEY_RETURN && mask == MASK_NONE)
    {
        give();
        return;
    }
    // A second key after one, where there may be two; otherwise, and after
    // two, a key starts again.
    if (mTwoKeys && !mPressed.none() && !mPressed.twoKeys())
    {
        mPressed = ALKeyChord{ key, mask, mPressed.key, mPressed.mask };
    }
    else
    {
        mPressed = ALKeyChord{ key, mask };
    }
    showPressed();
}

void ALKeyCapture::give()
{
    if (mPressed.none())
    {
        return;
    }
    const ALKeyChord pressed = mPressed;
    const chosen_t   chosen  = mChosen;
    if (ALPopover* up = popover())
    {
        up->settle();
    }
    if (chosen)
    {
        chosen(pressed);
    }
}

void ALKeyCapture::showPressed()
{
    mKeys->setText(mPressed.none() ? mWords.nothing : mPressed.describe());
    mSaid->setText(mPressed.none() || !mAbout ? std::string() : mAbout(mPressed));
    mSet->setEnabled(!mPressed.none());
}

void ALKeyCapture::draw()
{
    // Clicked back into after a look at something in it: the keys are its
    // own again.
    if (hasFocus() && !ALKeyChords::waiting())
    {
        listen();
    }
    LLPanel::draw();
}

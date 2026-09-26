/**
 * @file alkeycapture.h
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

#ifndef AL_ALKEYCAPTURE_H
#define AL_ALKEYCAPTURE_H

#include "alkeychord.h"
#include "llpanel.h"

#include <functional>
#include <string>

class ALPopover;
class LLButton;
class LLTextBox;

// Keys pressed to be given to a command: a key, or two in turn, taken
// before anything else would take them -- the viewer's menus' Control-S, a
// text's plain S -- and said back with what giving them would mean, which
// whoever asked says. Return gives them and Escape keeps what was; any
// other key is taken, a third starting again. Shown over the list it was
// asked from, in a popover of its own, which goes as the keyboard does.
class ALKeyCapture : public LLPanel
{
public:
    // What the owner says of keys pressed -- whose they are now, what they
    // would put out of reach -- or nothing.
    typedef std::function<std::string(const ALKeyChord&)> about_t;
    typedef std::function<void(const ALKeyChord&)>        chosen_t;

    // The words, which are the owner's to translate.
    struct Words
    {
        std::string title;
        // What to do: press the keys, Return, Escape.
        std::string prompt;
        // Said while nothing has been pressed.
        std::string nothing;
        std::string set;
        std::string cancel;
    };

    // Over `anchor`, the keyboard taken. Two keys in turn only where
    // `two_keys`: a third, or a second where one is all there may be,
    // starts again. Null where there is nothing to show it over.
    static ALKeyCapture* show(LLView* anchor, const Words& words, bool two_keys, about_t about, chosen_t chosen);

    // A key pressed, as the popover hears it: Return gives what was
    // pressed, Escape keeps what was, a modifier alone is nothing yet, and
    // anything else is pressed.
    void press(KEY key, MASK mask);
    const ALKeyChord& pressed() const { return mPressed; }
    ALPopover*        popover() const;

    void draw() override;

protected:
    ALKeyCapture(const Words& words, bool two_keys, about_t about, chosen_t chosen);

private:
    // The next key the viewer hears, this one's (ALKeyChords).
    void listen();
    void showPressed();
    void give();

    Words      mWords;
    bool       mTwoKeys = false;
    about_t    mAbout;
    chosen_t   mChosen;
    ALKeyChord mPressed;
    LLTextBox* mKeys  = nullptr;
    LLTextBox* mSaid  = nullptr;
    LLButton*  mSet   = nullptr;
};

#endif // AL_ALKEYCAPTURE_H

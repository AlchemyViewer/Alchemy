/**
 * @file alscriptnoticebar.h
 * @brief Script Studio's notice over the editor: what the tab in front has to reckon with, and up to two things to do about it.
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

#pragma once

#include "llpanel.h"

#include <string>
#include <utility>

class ALScriptStudioServices;
class LLButton;
class LLTextBox;

// The strip over a Script Studio window's editor that says what the tab in
// front has to reckon with -- a text kept from an earlier session, its
// object out of sight, its item gone, the connection lost, the file gone
// from disk -- with up to two things to be done about it and a way to hide
// it. Its layout panel is shown while it has something to say. What it
// says is ALScriptStudioOrphans's; what its buttons do is the window's.
class ALScriptNoticeBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptNoticeBar, LLPanel);

    // What the bar asks of the window beyond its services.
    class Window
    {
    public:
        // One of its buttons pressed, by its action; "close" for the way
        // out.
        virtual void noticeAction(const std::string& action) = 0;

    protected:
        ~Window() = default;
    };

    // What it says, and up to two things to be done about it, the first
    // leftmost: each an action and the name of the window's words for its
    // label, whose tip is the same name with Tip after it; none where the
    // action is empty. Nothing to say hides it.
    struct Notice
    {
        std::string                         text;
        std::pair<std::string, std::string> buttons[2];
    };

    bool postBuild() override;
    void show(const Notice& notice);
    // What each button does now, first and second.
    const std::string& action(size_t which) const { return mActions[which < 2 ? which : 0]; }

private:
    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    LLTextBox*              mText     = nullptr;
    LLButton*               mButtons[2] = { nullptr, nullptr };
    std::string             mActions[2];
};

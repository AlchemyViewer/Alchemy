/**
 * @file alscriptstudioselections.h
 * @brief A Script Studio window's selections compared: one held, then compared with another.
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

#include "alscriptstudiodoc.h"

#include <memory>
#include <optional>
#include <string>

class ALCodeEditor;
class ALScriptStudioServices;
class LLView;

// A Script Studio window's selections compared: one held -- Select for
// Compare -- and later compared with what is selected then, in the same
// tab or another, in that tab's place: the held on the left, the selection
// now on the right, each under its tab's name and its lines.
//
// Where the selection now is in the tab's source, the comparison's right
// stands for that stretch of it while the tab still holds it there: what
// is typed in the comparison goes on in the source at the same place, and
// a change taken back is an edit of that stretch. Once the tab has moved
// under it, typing goes to the stretch's start and nothing is taken back.
class ALScriptStudioSelections
{
public:
    typedef ALScriptStudioDoc Doc;

    // What comparing selections asks of the window.
    class Window
    {
    public:
        // Two texts side by side in a tab's place, each under its title.
        virtual void    compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                                const std::string& right_title) = 0;
        // The tab's source in sight, the keyboard in it and its caret at a
        // place: where typing in a comparison goes on.
        virtual LLView* typeInSource(Doc& doc, const ALTextPos& at) = 0;

    protected:
        ~Window() = default;
    };

    // A selection held: its text, and what it is called.
    struct Held
    {
        std::string text;
        std::string title;
    };

    ALScriptStudioSelections(ALScriptStudioServices& services, Window& window);

    // Whether the editor in sight in a tab has something selected.
    static bool               canHold(const Doc& doc);
    // It held, in place of any held before, and said.
    void                      hold(Doc& doc);
    const std::optional<Held>& held() const { return mHeld; }
    // Whether there is one held and something selected in the tab to
    // compare it with.
    bool                      canCompare(const Doc& doc) const;
    // The held compared with the tab's selection, in its place.
    void                      compare(Doc& doc);

private:
    // A tab's selection as it is said: its name and its lines.
    std::string titleOf(const Doc& doc, const ALTextRange& range) const;

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    std::optional<Held>     mHeld;
};

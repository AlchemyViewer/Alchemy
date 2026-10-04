/**
 * @file alscriptstudiohistory.h
 * @brief A Script Studio window's way back to what its items were saved as.
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

#include "alquickopen.h"
#include "alsavehistory.h"
#include "alscriptstudiodoc.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's way back to what its tabs' items were saved as,
// in the history the workspace keeps of every save (ALRecovery::history):
// File > Local History lists the front tab's saves, newest first, each
// with when, how long, and how much longer or shorter than the one before;
// one chosen is compared with the tab's text as it is now, and the notice
// over the editor offers it back -- as one step to undo, not a save, which
// stays the scripter's to make.
class ALScriptStudioHistory
{
public:
    typedef ALScriptStudioDoc Doc;

    // What the history asks of the window.
    class Window
    {
    public:
        // Two texts side by side in a tab's place, each under its title.
        virtual void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                             const std::string& right_title) = 0;
        // Another text beside the tab's own, which the comparison follows.
        virtual void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string& own_title,
                                    const std::vector<std::pair<S32, S32>>& anchors) = 0;
        // A list to pick from, over the editors: what is chosen, and what
        // Shift-Return is pressed on.
        virtual void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                          std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) = 0;
        // The notice over the editor said again, for the tab in front.
        virtual void refreshNotice() = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioHistory(ALScriptStudioServices& services, Window& window);

    // The key a tab's item's saves are kept under; none for a file on
    // disk, which is saved there rather than sent.
    static std::string keyOf(const Doc& doc);
    // What a list of saves, newest first, says of each: when, how long,
    // how much longer or shorter than the one before, and which is what is
    // saved now -- in the words of whichever window lists them, the notecard
    // window's as well as the studio's: a word by its name, or with a count
    // (HistoryBytes, HistoryLonger, HistoryShorter) in the form the count
    // takes. Each one's value is its place in the list.
    typedef std::function<std::string(const char* name, std::optional<S32> count)> words_t;
    static std::vector<ALQuickOpen::Candidate> candidatesOf(const std::vector<ALSavedText>& saves, const LLUUID& current, const words_t& words,
                                                            const std::function<std::string(const std::vector<std::string>&)>& listed);
    // The saves of a tab's item offered to compare with it; said where
    // there are none.
    void show(Doc& doc);
    // A save compared with the tab's text as it is now -- once it has
    // loaded, where it has not -- and offered back by the notice. False
    // where its text cannot be read.
    bool compare(Doc& doc, ALSavedText saved);

private:
    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Whether this is still here, for what the list calls back.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

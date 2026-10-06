/**
 * @file alscriptstudiocomparewith.h
 * @brief A Script Studio window's Compare With: whatever a tab may be set beside, in one list.
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
#include "alscriptstudiodoc.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's Compare With: whatever the tab in front may be
// set beside, in one list ranked as the quick open ranks -- its text as
// last saved, and as saved elsewhere where that came up; each other tab's
// text as it is now; the clipboard's; a file on disk, picked or opened
// lately; its saves kept here, listed in turn (ALScriptStudioHistory); and
// each script or notecard of its kind in the objects in hand, those of its
// name first. The one chosen is set beside the tab's text, which the
// comparison follows.
class ALScriptStudioCompareWith
{
public:
    typedef ALScriptStudioDoc Doc;

    // An item of the tab's kind in an object in hand, that may be read:
    // what it is, what it is called, and where it is in words.
    struct Item
    {
        ALScriptRef ref;
        std::string name;
        std::string place;
    };

    // What Compare With asks of the window.
    class Window
    {
    public:
        // Another text beside the tab's own, which the comparison follows.
        virtual void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string& own_title,
                                    const ALTextDiff::ranges_t& ranges) = 0;
        // An item's text, as the region or the inventory has it, set
        // beside the tab's once it has loaded, under its title.
        virtual void compareWithItem(Doc& doc, const Item& item, const std::string& title) = 0;
        // A list to pick from, over the editors: what is chosen, and what
        // Shift-Return is pressed on.
        virtual void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                          std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) = 0;
        // Files chosen on disk, one or several; none where none were.
        virtual void pickFilesToOpen(bool several, std::function<void(const std::vector<std::string>& files)> chosen) = 0;
        // The tab's saves kept here, offered to compare with it.
        virtual void offerHistory(Doc& doc) = 0;
        // What the clipboard holds as text; none where it holds none.
        virtual std::optional<std::string> clipboardText() const = 0;
        // The files opened lately, newest first.
        virtual std::vector<std::string> recentFiles() const = 0;
        // The items of the tab's kind in the objects in hand that may be
        // read, those of its name first; none for a file.
        virtual std::vector<Item> itemsLike(const Doc& doc) const = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioCompareWith(ALScriptStudioServices& services, Window& window);

    // Whether a tab may be compared with anything: once its text is there.
    static bool canCompare(const Doc& doc);
    // What a tab is offered, as the list will have it: once what is most
    // asked for -- its saved text, the other tabs, the clipboard, a file,
    // its saves -- then the files opened lately and the items like it.
    void show(Doc& doc);

private:
    // What the list was made of, which its values point into.
    struct Offer
    {
        std::vector<std::string> recent;
        std::vector<Item>        items;
        // The other tabs' names by id, for one closed before it is chosen.
        std::map<std::string, std::string> tabs;
    };
    std::vector<ALQuickOpen::Candidate> candidatesFor(const Doc& doc, Offer& offer) const;
    // A row's value, chosen for the tab.
    void chosen(Doc& doc, const std::string& value, const Offer& offer);
    // A file's text set beside the tab's, under the file's name; said
    // where it cannot be read.
    void compareWithFile(Doc& doc, const std::string& path);
    // How many of a tab's saves are kept here.
    static size_t savesKept(const Doc& doc);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Whether this is still here, for what the lists call back.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

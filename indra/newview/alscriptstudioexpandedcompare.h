/**
 * @file alscriptstudioexpandedcompare.h
 * @brief A Script Studio window's source compared with what a save sends of it, lined up by the preprocessor's map.
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
#include "altextdiff.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <string>

class ALScriptStudioServices;
class ALSourceMap;
class LLView;

// A Script Studio window's source compared with what a save sends of it:
// preprocessed -- includes in, macros made -- and optimized where the
// optimizer runs. The two are lined up by the map the preprocessor keeps
// between them, each line of the source beside what it became: a line
// that became several bracketed to them across the gap, the caret on one
// lighting the other, what an include put in alone on the right. What is
// typed in it goes on in the source where the map says the right's place
// came from. Made from the tab as it is: where the expansion is older than
// the text, once it has been made again.
class ALScriptStudioExpandedCompare
{
public:
    typedef ALScriptStudioDoc Doc;

    // What the comparison asks of the window.
    class Window
    {
    public:
        // Two texts side by side in a tab's place, each under its title,
        // lined up at the ranges.
        virtual void    compareRanged(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                                      const std::string& right_title, const ALTextDiff::ranges_t& ranges) = 0;
        // The tab preprocessed again, as a save would send it; told by
        // expanded() when it has been.
        virtual void    preprocess(Doc& doc) = 0;
        // The tab's source in sight, the keyboard in it and its caret at a
        // place: where typing in a comparison goes on.
        virtual LLView* typeInSource(Doc& doc, const ALTextPos& at) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioExpandedCompare(ALScriptStudioServices& services, Window& window);

    // The stretches of a source and of its output that stand for each
    // other, by the map: each output line beside the lines of the script's
    // own it came from, from the least to the most, those after it beside
    // the same lines joined to it. Lines of an include, or nothing's, are
    // in none.
    static ALTextDiff::ranges_t rangesOf(const ALSourceMap& map);

    // Whether a tab is one a save preprocesses, loaded.
    static bool canCompare(const Doc& doc);
    // The comparison shown, now where the expansion is of the text as it
    // is, else once it has been made again.
    void compare(Doc& doc);
    // A tab's expansion made: its comparison shown, where one waits on it;
    // made again where it is of an older text.
    void expanded(Doc& doc);
    // Whether a tab's comparison waits on an expansion.
    bool waiting(const std::string& id) const { return mWaiting.contains(id); }

private:
    // What a tab showed when its comparison was asked for: its view, and
    // a comparison's left, which another comparison sets anew.
    struct Shown
    {
        Doc::View   view = Doc::View::Source;
        size_t      left = 0;

        bool operator==(const Shown& other) const { return view == other.view && left == other.left; }
    };
    static Shown shownOf(const Doc& doc);

    void show(Doc& doc);
    // The tabs closed while they waited let go of.
    void forgetClosed();

    ALScriptStudioServices&                         mServices;
    Window&                                         mWindow;
    // The tabs whose comparison waits on an expansion, by id, with what
    // each showed then.
    boost::unordered_flat_map<std::string, Shown>   mWaiting;
};

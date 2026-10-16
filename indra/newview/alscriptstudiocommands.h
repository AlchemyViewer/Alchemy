/**
 * @file alscriptstudiocommands.h
 * @brief Script Studio's commands by name: what each does, whether it can now, and whether it is on.
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

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// A Script Studio window's commands by the names its menus give them: what
// each does, whether it can be done now, and, for a toggle, whether it is
// on. The menus' items come here by name, and so do the palette, the tab
// strip's menu, the Open Recent list, vim's ex commands and the keys; each
// command is registered once, by whatever owns it -- the window, or a unit
// split out of it as the unit is made.
//
// check_script_strings.py holds the table to the skin: every item of the
// menus names a command some `add` registers, and every command `add`
// registers is an item. One that no item of the menu bar gives is
// registered with `addUnlisted` instead, and is otherwise the same.
class ALScriptStudioCommands
{
public:
    typedef std::function<void()> run_t;
    typedef std::function<bool()> test_t;

    // A command of the menus', by its item's name: what it does; whether
    // it can be done now, always where that is not given; and for a
    // toggle, whether it is on -- where that is not given, the command is
    // no toggle, and says it is off. False, and said in the log, for a
    // name registered already, which keeps what it had.
    bool add(const std::string& name, run_t run, test_t enabled = nullptr, test_t checked = nullptr);
    // One no item of the menu bar gives: the tab strip's menu, the Open
    // Recent list.
    bool addUnlisted(const std::string& name, run_t run, test_t enabled = nullptr, test_t checked = nullptr);

    bool has(std::string_view name) const;
    // As the menus ask. A name nothing registered does nothing, cannot be
    // done, and is off; it is said in the log the first time it is asked
    // for, and kept (unknownAsked).
    void run(std::string_view name);
    bool enabled(std::string_view name);
    bool checked(std::string_view name);
    // Done where it can be done now, as a key or an ex command asks; false
    // where it could not.
    bool runIfEnabled(std::string_view name);

    // Every name registered, in no order; and the names asked for that
    // none was, each once.
    std::vector<std::string>        names() const;
    const std::vector<std::string>& unknownAsked() const { return mUnknownAsked; }

private:
    struct Command
    {
        run_t  run;
        test_t enabled;
        test_t checked;
    };
    const Command* find(std::string_view name);

    boost::unordered_flat_map<std::string, Command, ll::string_hash, std::equal_to<>> mCommands;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>          mUnknownSaid;
    std::vector<std::string>                                                          mUnknownAsked;
};

/**
 * @file almastermatch.h
 * @brief Which file on disk a script in the world may have as its master: by what it says, or by its name.
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

#include "aldiskincludes.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The files a script in the world may be offered as its master, for the
// scripter to link with a click: the one the script names, and those of its
// name. Never chosen for them, and never more than they let a script reach.
//
// A script says what it likes, and anybody's script says it: one in a
// stranger's object may name `secrets/keys.lsl`. So what it names is found
// only where an `#include` in the same script could find it -- an ordinary
// file of a sensible size under a folder the scripter blessed
// (ALDiskIncludes) -- and only a script of the item's own language. A path
// from a root is judged as written before the disk is asked anything of it,
// so that a share named is never reached for. Nothing here writes to the
// disk.
struct ALMasterMatch
{
    // How far into a script's source an @file comment is looked for, as the
    // VS Code plugin looks.
    static constexpr size_t HINT_LINES = 10;
    // How far the blessed folders are looked through by name: as deep as
    // the studio's own look across a scripter's scripts, and no more than so
    // many entries looked at, nor files found, in each.
    static constexpr int    NAME_DEPTH   = 6;
    static constexpr size_t NAME_ENTRIES = 32768;
    static constexpr size_t NAME_FILES   = 4096;

    // What a notecard's master may be called: text, a notecard's own, or
    // JSON, which scripts read from notecards.
    static const std::vector<std::string>& notecardExtensions();

    // The file a script says it was made from: an `// @file <path>` comment
    // (`-- @file` in SLua) on a line of its own among the first HINT_LINES
    // of its source, the path the rest of the line, quotes taken off; else
    // the @file of an upload header at the start of the compiled half,
    // ours or the plugin's (ALUploadHeader), past the target line where it
    // is given one. Nothing where it names none.
    static std::optional<std::string> hintOf(std::string_view source, std::string_view compiled, bool lua);

    // A hint, as the file it names, where it stands once its links are
    // followed; or nothing, with why said in plain English. In order:
    //  1. a path from no root is joined to each blessed folder in turn, the
    //     first found winning; `@alias/rest` is taken from the folder of the
    //     alias so named in `aliases` (name, with or without its `@`, then
    //     folder), in any case as Luau takes an alias; each path made, and a
    //     path from a root as given, must be under a blessed folder as it is
    //     written (ALDiskIncludes::lexicallyUnder), before anything on the
    //     disk is asked. So the aliases' folders are given as the blessed
    //     ones are held, their links followed;
    //  2. ALDiskIncludes::admits must take it: an ordinary file, at most
    //     MAX_BYTES, under a blessed folder once its links are followed;
    //  3. it must be a script of the language (ALDiskIncludes::
    //     scriptExtensions), both as it is named -- looked at first, before
    //     the disk -- and as it stands.
    static std::optional<std::string> resolve(const std::string& hint, const ALDiskIncludes& blessed,
                                              const std::vector<std::pair<std::string, std::string>>& aliases, bool lua, std::string& why);

    // The files under the blessed folders an item of this name may be
    // mastered by, best first, each once, where it stands: the caller
    // never picks one of several. Found as the VS Code plugin finds a
    // master by name: a file named `<name>.<ext>` anywhere under a folder --
    // a name with a `/` in it, by its last parts -- then one whose path from
    // the folder, its `/`s taken out or made `_` or a space, is that:
    // `net/door.lsl` for an item called `netdoor`, `net_door` or `net door`.
    // Each form the same name in its own case first, then in any. A script
    // is `.lsl`, or `.luau` then `.lua` (ALDiskIncludes::namesFor's): not an
    // LSL include's `.lslh` or `.lsli`. An item whose name ends in one is
    // looked for as it is, first. Among equals, the folder blessed first,
    // then the nearer its top, then by path.
    static std::vector<std::string> byName(const std::string& item_name, const ALDiskIncludes& blessed, bool lua);

    // The scripts byName looks among, listed once for many names -- every
    // script of an object linked at once -- in the order it ranks them;
    // and byName over such a list. Given up part way, with what was found,
    // once `stopped` says so, as ALDiskIncludes::filesUnder is.
    static std::vector<ALDiskIncludes::Listed> listing(const ALDiskIncludes& blessed, bool lua, const std::function<bool()>& stopped = {});
    static std::vector<std::string>            byName(const std::string& item_name, const std::vector<ALDiskIncludes::Listed>& listed, bool lua);
};

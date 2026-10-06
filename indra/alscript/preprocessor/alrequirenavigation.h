/**
 * @file alrequirenavigation.h
 * @brief A SLua require walked by Luau's own navigator, over the places a script's modules may be.
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

#include "lluuid.h"
#include "stdtypes.h"

#include <optional>
#include <string>
#include <vector>

// The places a require is walked through: folders, and the files in them,
// wherever they are -- on disk, in the inventory, among an object's
// contents -- seen as one tree. A folder is known by an id of the source's
// own: `disk:<path>` for a folder on disk. Written once for the search
// itself, which answers Pending where the world has not said yet, and once
// for a snapshot of what a script can reach.
class ALRequirePlaces
{
public:
    enum class Known : U8
    {
        Yes,
        No,
        Pending
    };
    // A module a name may stand for: an item in the world by its identity,
    // name and asset, or a file on disk, read on the spot.
    struct File
    {
        std::string path;
        std::string name;
        LLUUID      assetId;
        std::string file;
    };

    virtual ~ALRequirePlaces() = default;

    // The folder a file asking is in, and the name it is a module by: its
    // own, a script's extension taken off -- `init` for a folder's init,
    // which is a file in its folder like any other (LAD2). False where it
    // is in no folder known.
    virtual bool placeOf(const std::string& path, std::string& folder, std::string& name) = 0;
    // The same of a path from a root, which is on disk or nowhere.
    virtual bool placeOfAbsolute(const std::string& path, std::string& folder, std::string& name) = 0;
    // The folder above one; No at the top.
    virtual Known parentOf(const std::string& folder, std::string& out) = 0;
    // The folder of a name in a folder.
    virtual Known subfolder(const std::string& folder, const std::string& name, std::string& out) = 0;
    // The files a name in a folder stands for, in the order tried: as
    // written where it has an extension; else the name with a script's
    // extensions, and in the world the item of the name itself.
    virtual Known files(const std::string& folder, const std::string& name, std::vector<File>& out) = 0;
    // A folder's configuration, as a `.luaurc` reads: its `.luaurc`, or on
    // disk its `.config.luau` run and read as one (ALLuauConfigScript).
    struct Config
    {
        std::string text;
        // Whether it is on disk, and the folder its aliases' paths are
        // from: its own, or for one at the top of a scripter's include
        // folder, which governs a script in the world, that folder.
        bool        onDisk = false;
        std::string base;
        // Both a `.luaurc` and a `.config.luau` there, which Luau takes as
        // neither; or a `.config.luau` that did not run, no text, and why.
        bool        ambiguous = false;
        std::string error;
    };
    // Yes where it has one, ambiguous or not; Pending where its text is on
    // its way.
    virtual Known config(const std::string& folder, Config& out) = 0;
    // The folder a studio alias stands for, where no configuration names
    // the alias (LA22): Script Studio's own, by the name in lower case.
    virtual Known studioAlias(const std::string& alias, std::string& folder) = 0;
    // Every studio alias's name, for what is offered after an @.
    virtual std::vector<std::string> studioAliasNames() = 0;
    // What a folder holds as a require names it: each script as a module,
    // a script's extension taken off, and each folder; nothing where what
    // it holds is not known, and no more than a few hundred.
    struct Child
    {
        std::string name;
        bool        folder = false;
    };
    virtual Known children(const std::string& folder, std::vector<Child>& out) = 0;
    // An alias of the configuration of `config_folder`, which is on disk,
    // has reached a folder: blessed for the run where the configuration
    // may bless it (ALDiskIncludes::blessFromConfig).
    virtual void aliasReached(const std::string& config_folder, const std::string& folder) = 0;
};

// A SLua require as the plugin's rules have it (LAD9), walked by Luau's
// own navigator (Luau::Require::Navigator) over a source of places:
// relative to the file asking, a folder's init included; aliases through
// the nearest `.luaurc` that names them, an alias naming another followed,
// a cycle an error, then Script Studio's own; `@self` the module's own
// folder; no search.
namespace ALRequireNavigation
{
    // What a walk found: the files a require stands for, in the order
    // tried; Pending where a place on the way was not known yet; or
    // nothing, and why, in Luau's words where they are Luau's.
    struct Walked
    {
        ALRequirePlaces::Known           found = ALRequirePlaces::Known::No;
        std::vector<ALRequirePlaces::File> files;
        // A file and a folder's init of the same name both there: the file
        // taken (LAD3), and the init that was not.
        std::optional<ALRequirePlaces::File> passedOver;
        std::string                          error;
    };
    Walked walk(ALRequirePlaces& places, const std::string& from, const std::string& name);

    // A require's path in the navigator's terms, or why it may not be
    // walked: a path with no prefix is `./`; `@self` is written as the
    // path from beside the file asking, whose module name is `stem`, so
    // that a configuration's own `self` stands for nothing; `@sl-*` is
    // reserved; an alias's path may not climb out of its folder.
    struct Path
    {
        std::string path;
        std::string error;
    };
    Path navigatorPath(const std::string& name, const std::string& stem);

    // What could follow a require's path typed so far, as Luau's own
    // suggester offers it (Luau::RequireSuggester) over the places, walked
    // by the same rules as a require: before the first slash, the aliases
    // in reach of the file and `./` and `../`; after it, what the folder
    // the path reaches holds, and `..`. Each with the whole path it puts in
    // the string, escaped as a string holds it, and whether it is a folder,
    // which a path goes on through.
    struct Suggestion
    {
        std::string label;
        std::string path;
        bool        folder = false;
    };
    std::vector<Suggestion> suggest(ALRequirePlaces& places, const std::string& from, const std::string& typed);
}

/**
 * @file aldiskincludes.h
 * @brief Which files on disk a script's includes may be read from, and the reading of them.
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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The disk is the one place a script's text can reach past the world, and a
// script is opened -- and its includes read -- before anybody has read it:
// one from somebody else's object, or saved by another viewer in the
// preprocessor's envelope, is expanded the moment it is on screen. So a
// file is read for an include only where somebody blessed the folder it is
// in: the include folders of the scripter's own settings, or what a
// `.lslrc` or a `.luaurc` that is itself on disk lists. A configuration in
// the world blesses nothing, whatever it says; nor does the folder a
// script happens to be in, nor a path from a root. A configuration on disk
// blesses only folders under its own, or under one the scripter blessed:
// one that came with a download reaches no further into the disk than the
// scripter already let it. And what is read is an ordinary file of a
// sensible size -- never a device, a pipe or a folder -- under a blessed
// folder once every link on the way is followed.
class ALDiskIncludes
{
public:
    // More than any script, or anything it would include, has reason to be.
    static constexpr std::uintmax_t MAX_BYTES = 4u * 1024u * 1024u;

    // A folder blessed, where it is one: kept as it stands once its links
    // are followed. An ordinary file is blessed alone, nothing beside it --
    // what an alias of a configuration names as a module -- and is no
    // folder. Anything else -- no such folder, a device -- is passed over.
    void bless(const std::string& folder);
    bool blessed() const { return !mFolders.empty(); }
    // Whether a configuration on disk in `config_folder` may bless
    // `folder`: one under the folder the configuration is in, or under
    // one already blessed -- the scripter's own, blessed first. Links
    // followed, both; another machine's share, or a device, asked nothing
    // of unless it is under one of those as written (lexicallyUnder).
    bool mayFromConfig(const std::string& folder, const std::string& config_folder) const;
    // Blessed where mayFromConfig says so; false, and said once in the
    // log, where not.
    bool blessFromConfig(const std::string& folder, const std::string& config_folder);
    // The folders blessed, each as it stands.
    const std::vector<std::string>& folders() const { return mFolders; }

    // Where a file stands, once its links are followed, where it may be
    // read: an ordinary file of at most MAX_BYTES under a blessed folder,
    // or blessed itself. Nothing for anything else.
    std::optional<std::string> admits(const std::string& file) const;

    // Whether a path from a root is under one of `folders` as both are
    // written, part by part and in any case, nothing on the disk asked: what
    // a path a script wrote must be before the disk is asked anything of
    // it. A path on another machine's share -- which asking would send it
    // who asks -- or a device's is under none but a folder of the same.
    // Only that: where a file may be read is still `admits`'s to say.
    static bool lexicallyUnder(const std::string& path, const std::vector<std::string>& folders);

    // What a script of each language is named with on disk, the modern
    // first: SLua's `.luau` and `.lua`; LSL's `.lsl`, and its includes'
    // `.lslh` and `.lsli`. And how long the one of `extensions` a name ends
    // with is, in any case, past a name of its own; nought for none.
    static const std::vector<std::string>& scriptExtensions(bool lua);
    static size_t                          extensionOf(std::string_view name, const std::vector<std::string>& extensions);

    // The files a name may stand for in a folder, in the order they are
    // looked for: the name as written, then with its language's
    // extensions; and for a require of SLua whose name has no extension
    // of its own, the `init.luau` and then the `init.lua` in a folder of
    // that name, which is the folder's module -- as Luau's require takes
    // it, and as the Second Life VS Code plugin does.
    static std::vector<std::string> namesFor(const std::string& name, bool lua, bool require);

    // The file of this name at the top of each blessed folder that has one
    // it admits, in the order the folders were blessed: the configurations
    // a scripter keeps with their includes, which serve a script that is
    // not itself on disk and so has no folders of its own to look up
    // through.
    std::vector<std::string> atTop(const std::string& name) const;

    // The files under the blessed folder `folder`, and the folders under
    // it, whose names end in one of `extensions`, each where it stands
    // once its links are followed and by its path from the folder, `/`
    // between the parts: what a script might name one of them by. No
    // hidden folder, nor a link to a folder, nor anything more than
    // `depth` folders down; no more than `entries` looked at, nor `files`
    // found -- a scripter's include folder may be a home folder. Nothing
    // for a folder that is not blessed. Given up part way, with what was
    // found so far, once `stopped` says so: asked before each entry, for a
    // look on a thread that is to end -- a slow drive may take a while over
    // every one.
    struct Listed
    {
        std::string file;
        std::string relative;
    };
    std::vector<Listed> filesUnder(const std::string& folder, const std::vector<std::string>& extensions, int depth, size_t entries,
                                   size_t files, const std::function<bool()>& stopped = {}) const;

    // The scripts of a language under each of `folders` -- SLua's .luau and
    // .lua, LSL's .lsl and its includes' .lslh and .lsli -- each folder
    // blessed for the look and listed as filesUnder lists it, `depth`
    // folders down, each script once where it stands: no more than `most`
    // in all. For a look across a scripter's scripts on disk, given only
    // folders a script may read from: the scripter's own, what their
    // configurations bless, the studio's aliases. Given up part way once
    // `stopped` says so, as filesUnder is.
    static std::vector<std::string> scriptsUnder(const std::vector<std::string>& folders, bool lua, int depth, size_t most,
                                                 const std::function<bool()>& stopped = {});

    // An ordinary file of at most MAX_BYTES, read whole; false for
    // anything else, and for one that grew past the limit while it was
    // being read. Which files may be read at all is `admits`'s to say;
    // this only makes the reading safe.
    static bool readOrdinary(const std::string& file, std::string& out);

    // The folders a `.lslrc` in a folder lists -- `{"include": ["lib",
    // "../shared"]}`, each relative to the folder the file is in unless
    // from a root -- or none where there is no such file or it is not
    // what it should be; and the nearest `.lslrc` up from a folder's, with
    // the folder it is in. What it lists may be blessed only as
    // mayFromConfig says.
    static std::vector<std::string> lslrcFolders(const std::string& folder);
    static std::vector<std::string> nearestLslrcFolders(std::string folder, std::string* found_in = nullptr);

private:
    std::vector<std::string> mFolders;
    std::vector<std::string> mFiles;
};

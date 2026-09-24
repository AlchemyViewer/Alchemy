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
#include <optional>
#include <string>
#include <vector>

// The disk is the one place a script's text can reach past the world, and a
// script is opened -- and its includes read -- before anybody has read it:
// one from somebody else's object, or saved by another viewer in the
// preprocessor's envelope, is expanded the moment it is on screen. So a
// file is read for an include only where somebody blessed the folder it is
// in: the include folders of the scripter's own settings, or what a
// `.lslrc` or a `.luaurc` that is itself on disk lists. A configuration in
// the world blesses nothing, whatever it says; nor does the folder a
// script happens to be in, nor a path from a root. And what is read is an
// ordinary file of a sensible size -- never a device, a pipe or a folder --
// under a blessed folder once every link on the way is followed.
class ALDiskIncludes
{
public:
    // More than any script, or anything it would include, has reason to be.
    static constexpr std::uintmax_t MAX_BYTES = 4u * 1024u * 1024u;

    // A folder blessed, where it is one: kept as it stands once its links
    // are followed. Anything else -- no such folder, a file -- is passed
    // over.
    void bless(const std::string& folder);
    bool blessed() const { return !mFolders.empty(); }
    // The folders blessed, each as it stands.
    const std::vector<std::string>& folders() const { return mFolders; }

    // Where a file stands, once its links are followed, where it may be
    // read: an ordinary file of at most MAX_BYTES under a blessed folder.
    // Nothing for anything else.
    std::optional<std::string> admits(const std::string& file) const;

    // The files under the blessed folder `folder`, and the folders under
    // it, whose names end in one of `extensions`, each where it stands
    // once its links are followed and by its path from the folder, `/`
    // between the parts: what a script might name one of them by. No
    // hidden folder, nor a link to a folder, nor anything more than
    // `depth` folders down; no more than `entries` looked at, nor `files`
    // found -- a scripter's include folder may be a home folder. Nothing
    // for a folder that is not blessed.
    struct Listed
    {
        std::string file;
        std::string relative;
    };
    std::vector<Listed> filesUnder(const std::string& folder, const std::vector<std::string>& extensions, int depth, size_t entries,
                                   size_t files) const;

    // An ordinary file of at most MAX_BYTES, read whole; false for
    // anything else, and for one that grew past the limit while it was
    // being read. Which files may be read at all is `admits`'s to say;
    // this only makes the reading safe.
    static bool readOrdinary(const std::string& file, std::string& out);

    // The folders a `.lslrc` in a folder lists -- `{"include": ["../lib",
    // "/abs/path"]}`, each relative to the folder the file is in unless
    // from a root -- or none where there is no such file or it is not
    // what it should be; and the nearest `.lslrc` up from a folder's.
    static std::vector<std::string> lslrcFolders(const std::string& folder);
    static std::vector<std::string> nearestLslrcFolders(std::string folder);

private:
    std::vector<std::string> mFolders;
};

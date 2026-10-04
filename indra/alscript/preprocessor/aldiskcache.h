/**
 * @file aldiskcache.h
 * @brief What the disk says of a script's includes, kept a moment: the folders blessed, what they admit, the texts.
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
#include "alwatchedfile.h"
#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// What the disk says to the preprocessor, kept a moment: which folders an
// include may be read from, which files there it may read, the files'
// texts, and the configurations up the folders from a script. A check runs
// a moment after each keystroke and asks all of it again for every include
// a script names -- the folders blessed, a `.lslrc` read and parsed, one
// looked for up to the root, each candidate's links followed -- which is
// the disk asked hundreds of times a keystroke. Here it is asked again once
// what is kept is a couple of seconds old, so that a file made or a
// configuration changed is seen then; or at once when the settings that
// decide it change, which `generation` counts. A text is read again only
// once its time or its size changes. One thread's.
class ALDiskCache
{
public:
    static constexpr F64 FRESH_SECONDS = 2.0;

    // Folders blessed as the preprocessor blesses them for an include
    // (ALScriptPreprocessor::blessedFor), and what they admit, kept
    // together: a file admitted under one set of folders says nothing of
    // another.
    struct Blessed
    {
        ALDiskIncludes includes;
        F64            at         = 0.0;
        U32            generation = 0;
        boost::unordered_flat_map<std::string, std::optional<std::string>, ll::string_hash, std::equal_to<>> admitted;
    };

    // `own` blessed, the scripter's include folders; with `lslrc`, what a
    // `.lslrc` at the top of each lists, and what the nearest `.lslrc` up
    // from `from_dir` lists where that is not empty, each as a
    // configuration may bless (ALDiskIncludes::blessFromConfig); and
    // `extra` blessed outright -- the aliases of a `.luaurc` on disk a run
    // has gone through, which the preprocessor has already weighed.
    Blessed& blessed(const std::vector<std::string>& own, bool lslrc, const std::string& from_dir, const std::vector<std::string>& extra,
                     U32 generation, F64 now);
    // Where a file stands, as the folders admit it (ALDiskIncludes::admits),
    // kept with them.
    std::optional<std::string> admits(Blessed& blessed, const std::string& file);
    // The file of this name at the top of each of the folders that admits
    // one, in their order (ALDiskIncludes::atTop), kept likewise.
    std::vector<std::string> atTop(Blessed& blessed, const std::string& name);
    // A file's text (ALDiskIncludes::readOrdinary), read again only once
    // its time or its size changes; false for one that cannot be read.
    bool read(const std::string& file, std::string& out);
    // The files of this name in `dir` and in each folder above it to the
    // root, nearest first, kept as the folders are.
    const std::vector<std::string>& upwards(const std::string& dir, const std::string& name, U32 generation, F64 now);

    void clear();

private:
    struct Text
    {
        ALFileStamp stamp;
        std::string text;
    };
    struct Up
    {
        F64                      at         = 0.0;
        U32                      generation = 0;
        std::vector<std::string> files;
    };
    // Nodes: what blessed() hands out stays where it is while others come.
    boost::unordered_node_map<std::string, Blessed, ll::string_hash, std::equal_to<>> mBlessed;
    boost::unordered_flat_map<std::string, Text, ll::string_hash, std::equal_to<>>    mTexts;
    size_t                                                                            mTextBytes = 0;
    boost::unordered_flat_map<std::string, Up, ll::string_hash, std::equal_to<>>      mUp;
};

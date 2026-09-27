/**
 * @file almodulelook.h
 * @brief What may be a module in reach of a script, looked for off the main thread from what was gathered for it.
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
#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// What may be a module in reach of a script -- a text open, one held, or a
// file of the script's language under a folder a require or an include
// reads -- with what each gives (ALLuauExports, ALLSLExports) and the names
// a require or an include might find it by. Looked for off the main thread
// from what the main thread gathered (Input), which is everything it
// reads but the disk. One thread's: what it keeps between looks -- each
// text's exports while its version is the same, each file's while its time
// and its size are -- is its own.
class ALModuleLook
{
public:
    // A text open or held: by its version, which what it gives is kept
    // for, or zero for one held, known by what it holds.
    struct Text
    {
        std::string                        path;
        std::string                        name;
        U32                                version = 0;
        std::shared_ptr<const std::string> text;
    };
    // What a look is given: the script's language and its own identity,
    // which is never a module of its own; the texts; the folders a
    // require or an include reads, each with what a name under it starts
    // with; and the aliases a SLua configuration names.
    struct Input
    {
        bool                                             lua = false;
        std::string                                      self;
        std::vector<Text>                                texts;
        std::vector<std::pair<std::string, std::string>> folders;
        std::vector<std::string>                         aliases;
    };
    // Something that may be a module: what it gives, and the names a
    // require or an include might find it by, in the order tried.
    struct Candidate
    {
        std::string              path;
        std::string              name;
        std::vector<std::string> names;
        std::vector<std::string> exports;

        bool operator==(const Candidate&) const = default;
    };

    // Everything that could be a module, each once: what is open first,
    // since it is what the module is becoming; then what is held; then the
    // files of the folders. Each named as the folders name it, then by its
    // own name, without its extension, and under each alias.
    std::vector<Candidate> look(const Input& look);

private:
    // The files of a folder and the folders under it, each as a candidate
    // named by its path from the folder, `prefix` before it.
    void listFolder(const std::string& prefix, const std::string& folder, const ALDiskIncludes& blessed, bool lua, std::vector<Candidate>& found,
                    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>>& at);
    // What a text gives, by its identity and language, while its version
    // is the same -- or, for one held, its text.
    const std::vector<std::string>& exportsOf(const std::string& path, U32 version, const std::string& text, bool lua);
    // What a file gives, kept while its time and its size are the same: a
    // folder's files are looked at every few seconds, and read and parsed
    // again only once they change. Null for one that cannot be read.
    const std::vector<std::string>* fileExports(const std::string& file, bool lua);

    struct Read
    {
        U32                      version = 0;
        size_t                   hash    = 0;
        std::vector<std::string> exports;
    };
    boost::unordered_flat_map<std::string, Read, ll::string_hash, std::equal_to<>> mRead;
    struct OnDisk
    {
        std::filesystem::file_time_type time;
        std::uintmax_t                  size     = 0;
        bool                            readable = false;
        std::vector<std::string>        exports;
    };
    boost::unordered_flat_map<std::string, OnDisk, ll::string_hash, std::equal_to<>> mOnDisk;
};

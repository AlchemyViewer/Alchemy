/**
 * @file allinelabel.h
 * @brief What a `@line` comment names a file by: a file on disk by its path from the script's own folder.
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

#include "alsourcemap.h"

#include <string>
#include <string_view>
#include <vector>

// What the preprocessor's `@line` comments (ALPreprocessor::Options::
// lineLabel) name each file of a run by, for a reader that has only the
// text: the VS Code plugin, whose line mapper takes the first quoted string
// after `@line` as it stands and, where it has no scheme, resolves it
// against the folder of the script it is the master of. So a file on disk
// is named by its path from the script's own folder, `/` between the parts
// and `..` for each folder climbed, however it was reached -- an
// `#include`, a `require`, an alias -- and two modules of one name in
// different folders read apart. Where the script is no file on disk, or
// there is no path from its folder to the file -- one on another drive --
// the file is named by its path from the blessed folder it is under, as an
// `#include` names it, the outermost where several hold it. The script is
// named by its own base name on disk, or the name it is given; an include
// from the world by its name, as before. Never a path from a root, a share
// or a UUID, nor anything a quote, a backslash or a line break would cut
// short.
//
// Lexical, all of it: nothing on the disk is asked, so the script's path
// and the folders must be as the preprocessor names what it admits -- every
// link followed -- or a path between them climbs further than it need.
// Paths of both kinds are read on every platform: `C:\a\b`, `\\host\share\a`
// and `\\?\C:\a` as Windows writes them, a part the same in any case; and
// `/a/b`, where only `/` parts them and case tells parts apart.
class ALLineLabel
{
public:
    // The script's own identity -- `disk:<path>` for a file on disk, and
    // anything else, or nothing, for a script that is not -- and the
    // folders on disk its includes and requires may be read from: the
    // scripter's include folders, what a configuration on disk blesses, the
    // aliases' folders.
    ALLineLabel(const std::string& script, std::vector<std::string> folders);

    // The label of a file of a run's source map, made quotable; `script`
    // for the first of them, the script itself.
    std::string of(const ALSourceMap::File& file, bool script) const;
    std::string operator()(const ALSourceMap::File& file, bool script) const { return of(file, script); }

    // The path from a folder to a file, `/` between the parts and `..` for
    // each folder climbed: empty where there is none -- each on its own
    // drive or share, either no path from a root, the file the folder
    // itself or one of those it is in.
    static std::string relative(std::string_view folder, std::string_view file);
    // A label as it may stand between two quotes and be read back whole by
    // a reader that knows no escapes: a quote made `'`, a backslash `/`,
    // and a line break, or any other control character, `_`.
    static std::string quotable(std::string_view label);

private:
    // The script's path on disk, where it is a file; empty where not.
    std::string              mScript;
    std::vector<std::string> mFolders;
};

/**
 * @file alfilewrite.h
 * @brief A file written whole or not at all.
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

#include "llpreprocessor.h"

#include <string>
#include <string_view>

// A file somebody keeps -- a script on disk, a settings file of theirs --
// written so that what is there afterwards is the whole of the old text or
// the whole of the new, never the start of one: the new text is written
// beside it and put in its place. A link is written through, to the file
// it names, and stays a link; the file keeps the permissions it had.
//
// A file that could not be written in place is not written at all, as it
// would not have been: one made read-only is not replaced. Where the text
// will not all go beside it -- a full disk -- the file is left whole.
// Where nothing can be put beside it -- a folder only its files may be
// written in -- or it cannot be replaced -- a program on Windows holding it
// open -- it is written in place, as it always was.
namespace ALFileWrite
{
    // False, and the file as it was, where it could not be written.
    LL_COMMON_API bool whole(const std::string& path, std::string_view text);

    // What is written beside a file on the way: the name it is given.
    LL_COMMON_API std::string besideOf(const std::string& path);
}

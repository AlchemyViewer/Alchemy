/**
 * @file alcvarpreview.h
 * @brief What opening a settings file would do to its rows, said a row a
 *        line, with nothing written.
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

#include "alcvarfile.h"
#include "alcvars.h"

#include <optional>
#include <string>
#include <vector>

namespace ALCVarPreview
{
    // A line a row, as the store over its schema would take each into
    // `which`'s layer at open: the step it is at, what it becomes and
    // through which steps, or what is left alone and why; and a line for
    // each setting with no row of its own that its sources give a value.
    // Sorted by name. Hooks run as registered, and one that is not is said.
    std::vector<std::string> describe(const ALCVars& store, const std::vector<ALCVarRow>& rows,
                                      const std::vector<ALCVarRow>& prior, ALCVarLayer which);

    // The lines for the settings file at `path`: a copy of it and its
    // journal, opened in a directory of its own and deleted after, so that
    // nothing touches the file itself. Nothing, with `error` saying why,
    // where it cannot be read, is no settings file, or is a backup, which
    // has no reader yet.
    std::optional<std::vector<std::string>> file(const ALCVars& store, const std::string& path, ALCVarLayer which,
                                                 std::string& error);
}

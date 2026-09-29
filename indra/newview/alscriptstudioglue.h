/**
 * @file alscriptstudioglue.h
 * @brief What Script Studio asks of the system through the viewer: an editor of the person's own, and the file pickers.
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

#include "stdtypes.h"

#include <functional>
#include <string>
#include <vector>

// What Script Studio asks of the system through the viewer: an editor of
// the person's own started on a file, and the system's pickers for files
// to open and a file to save as. Here, so that what they need -- boost's
// processes and asio, the Mac's Carbon -- is compiled here rather than
// into the studio's window, the viewer's slowest file to compile.
namespace ALScriptStudioGlue
{
    // The editor LL_SCRIPT_EDITOR names, started on a file at a line from
    // one: false, with why in words, where it could not be.
    bool startEditor(const std::string& filename, S32 line, std::string& error);

    // The system's picker for scripts to open -- one, or several -- and
    // for a file to save one as, named as given: told the files chosen,
    // none where none was, on the main thread.
    typedef std::function<void(const std::vector<std::string>& files)> chosen_t;
    void pickToOpen(bool several, chosen_t chosen);
    void pickToSave(const std::string& name, chosen_t chosen);
}

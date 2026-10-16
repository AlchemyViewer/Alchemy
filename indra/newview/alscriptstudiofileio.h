/**
 * @file alscriptstudiofileio.h
 * @brief Script Studio's files read and written: whole, a temp file safely, and one watched for changes made outside.
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

#include "alscripttempfiles.h"
#include "alwatchedfile.h"
#include "llfile.h"

#include <memory>
#include <string>
#include <string_view>

// What the studio's window and its units share of reading files: a file's
// text whole, its line endings as an editor here keeps them, and a file
// watched for the changes made to it outside. Writing is ALFileWrite's: a
// file the author keeps whole or not at all, a temp file safely.
namespace ALScriptFileIO
{
    bool fileTooLarge(const std::string& path);
    bool readWholeFile(const std::string& path, std::string& text);

    // The temp file an external editor is given, watched for its saves,
    // and held while it is watched: gone from disk once nobody holds it
    // (ALScriptTempFiles). What is written here is marked seen as it is
    // written (ALWatchedFile::seen), and so is no save of the editor's.
    class StudioLiveFile final : public ALWatchedFile
    {
    public:
        // A copy in the temp folder is held with the watch; a file the
        // author keeps on disk, held by nobody, stays.
        StudioLiveFile(const std::string& path, changed_t changed, std::shared_ptr<ALScriptTempFiles::Claim> held)
        :   ALWatchedFile(path, std::move(changed)),
            mHeld(std::move(held))
        {
            // Twice a second: a save is heard within a second of it, once
            // it has held still from one look to the next.
            poll(0.5f);
        }

    private:
        std::shared_ptr<ALScriptTempFiles::Claim> mHeld;
    };
}

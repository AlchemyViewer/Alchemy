/**
 * @file alscriptstudiofileio.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiofileio.h"

#include "alfilewrite.h"
#include "fsyspath.h"

namespace ALScriptFileIO
{
    // The most a file opened here may hold. A script's text goes up as at
    // most 256 KB (ALScriptEnvelope::MAX_ASSET_BYTES) and a notecard 64 KB;
    // this is room for any include, snippets file or log anyone edits by
    // hand, and short of a file picked by mistake -- which is read on the
    // main thread, and put in an editor whole.
    constexpr std::uintmax_t MOST_FILE_BYTES = 8 * 1024 * 1024;

    bool fileTooLarge(const std::string& path)
    {
        return ALFileStamp::of(path).size > MOST_FILE_BYTES;
    }

    // A file's text, whole, its line endings as an editor here keeps them:
    // CRLF and a lone CR as LF. Compared as it came, a file saved with CRLF
    // -- as an editor on Windows saves one -- is never the text it was
    // taken as, and every save made to it after the first reads as made
    // on both sides at once. False where it could not be opened, or holds
    // more than a file opened here may.
    bool readWholeFile(const std::string& path, std::string& text)
    {
        // An ordinary file, and never more than a file opened here may
        // hold: a pipe or a device picked by mistake would be read for
        // ever.
        std::string bytes;
        if (!ALFileRead::whole(path, bytes, MOST_FILE_BYTES))
        {
            return false;
        }
        text.clear();
        text.reserve(bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (bytes[i] != '\r')
            {
                text += bytes[i];
                continue;
            }
            text += '\n';
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n')
            {
                ++i;
            }
        }
        return true;
    }
}

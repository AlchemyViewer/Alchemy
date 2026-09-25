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

#include <fstream>

#if !LL_WINDOWS
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ALScriptFileIO
{
    // The most a file opened here may hold. A script's text goes up as at
    // most 256 KB (ALScriptEnvelope::MAX_ASSET_BYTES) and a notecard 64 KB;
    // this is room for any include, snippets file or log anyone edits by
    // hand, and short of a file picked by mistake -- which is read on the
    // main thread, and put in an editor whole.
    constexpr S64 MOST_FILE_BYTES = 8 * 1024 * 1024;

    bool fileTooLarge(const std::string& path)
    {
        return LLFile::size(path) > MOST_FILE_BYTES;
    }

    // A file's text, whole, its line endings as an editor here keeps them:
    // CRLF and a lone CR as LF. Compared as it came, a file saved with CRLF
    // -- as an editor on Windows saves one -- is never the text it was
    // taken as, and every save made to it after the first reads as made
    // on both sides at once. False where it could not be opened, or holds
    // more than a file opened here may.
    bool readWholeFile(const std::string& path, std::string& text)
    {
        if (fileTooLarge(path))
        {
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

    // A file of the studio's own written in place -- the external editor's
    // copy and its log, which nobody keeps; false where any of it did not
    // go, which the last of it, written as the file closes, is the
    // likeliest not to. A file the author keeps is written by ALFileWrite,
    // whole or not at all.
    //
    // They go in the temp folder, which on Linux is everyone's, by names
    // anyone can work out: so the user's alone to read, never written
    // through a link, and never one somebody else made by that name first,
    // or linked to a file elsewhere.
    bool writeTempFile(const std::string& path, std::string_view text)
    {
#if LL_WINDOWS
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        return !out.fail();
#else
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
        if (fd < 0)
        {
            return false;
        }
        struct stat st;
        bool        whole = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() && st.st_nlink == 1 &&
                     fchmod(fd, S_IRUSR | S_IWUSR) == 0 && ftruncate(fd, 0) == 0;
        size_t done = 0;
        while (whole && done < text.size())
        {
            const ssize_t wrote = ::write(fd, text.data() + done, text.size() - done);
            if (wrote < 0 && errno == EINTR)
            {
                continue;
            }
            whole = wrote > 0;
            done += wrote > 0 ? static_cast<size_t>(wrote) : 0;
        }
        return ::close(fd) == 0 && whole;
#endif
    }
}

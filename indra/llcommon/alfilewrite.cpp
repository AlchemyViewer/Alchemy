/**
 * @file alfilewrite.cpp
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

#include "linden_common.h"

#include "alfilewrite.h"

#include "alwatchedfile.h"
#include "fsyspath.h"
#include "llfile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#if LL_WINDOWS
#include "llwin32headers.h"

#include <chrono>
#include <io.h>
#include <thread>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    enum class Wrote
    {
        Done,
        // Not so much as made: nothing of the text is anywhere.
        NotOpened,
        // Made, and not all of it went -- a full disk.
        Failed
    };

    // Every byte of the text, the file closed: the last of it is written as
    // the file closes, and is the likeliest not to go. Durable, forced out
    // to the disk first, as far as the disk says it can be.
    Wrote writeAll(const std::filesystem::path& file, std::string_view text, bool durable)
    {
        LLFILE* out = LLFile::fopen(file, LLFILE_MODE("wb"));
        if (!out)
        {
            return Wrote::NotOpened;
        }
        bool all = fwrite(text.data(), 1, text.size(), out) == text.size() && fflush(out) == 0;
        if (all && durable)
        {
#if LL_WINDOWS
            _commit(_fileno(out));
#else
            fsync(fileno(out));
#endif
        }
        all = fclose(out) == 0 && all;
        return all ? Wrote::Done : Wrote::Failed;
    }

#if LL_WINDOWS
    // The longest of the waits a durable replace makes for a program
    // holding the file, each twice the last from a millisecond: half a
    // second in all.
    constexpr U32 REPLACE_WAIT_MOST_MS = 256;

    // Whether a replace was refused because another program has the file
    // open.
    bool heldOpen(const std::error_code& ec)
    {
        const int error = ec.value();
        return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
    }
#endif

    // `beside` put in the place of `target`. On Windows a file another
    // program has open cannot be replaced, and a virus scanner or an indexer
    // opens one for a moment once it is written: a durable write, which has
    // nowhere else to go, waits for it to be let go of, half a second at
    // most. Anything else refused is refused at once.
    bool replace(const std::filesystem::path& beside, const std::filesystem::path& target, [[maybe_unused]] bool durable)
    {
        std::error_code ec;
        std::filesystem::rename(beside, target, ec);
#if LL_WINDOWS
        for (U32 wait_ms = 1; ec && durable && heldOpen(ec) && wait_ms <= REPLACE_WAIT_MOST_MS; wait_ms *= 2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            std::filesystem::rename(beside, target, ec);
        }
#endif
        return !ec;
    }
}

namespace ALFileWrite
{
std::string besideOf(const std::string& path)
{
    // A name nothing else would give a file, since whatever is there by it
    // is written over and then gone.
    return path + std::string(BESIDE);
}

bool whole(const std::string& path, std::string_view text, bool durable)
{
    std::error_code ec;
    std::filesystem::path target = fsyspath(path);
    // Through a link, to what it names: a link replaced would be a file
    // where somebody keeps a link. One that names nothing has nowhere for
    // the text to go.
    if (std::filesystem::is_symlink(target, ec))
    {
        const std::filesystem::path named = std::filesystem::canonical(target, ec);
        if (ec)
        {
            return false;
        }
        target = named;
    }
    // One there already is written only where it could be written in place:
    // made read-only, or held by a program that lets nobody write it, it is
    // left as it is. Opened to add to, so that nothing of it changes.
    const std::filesystem::file_status was    = std::filesystem::status(target, ec);
    const bool                         exists = !ec && std::filesystem::exists(was);
    if (exists)
    {
        llofstream probe(target, std::ios::binary | std::ios::app);
        if (!probe.is_open())
        {
            return false;
        }
    }
    const fsyspath beside(besideOf(fsyspath(target).string()));
    const Wrote    wrote = writeAll(beside, text, durable);
    if (wrote == Wrote::Done)
    {
        // What anyone could do with the old file, they can with the new.
        if (exists)
        {
            std::filesystem::permissions(beside, was.permissions(), ec);
        }
        if (replace(beside, target, durable))
        {
            return true;
        }
    }
    // What was made beside it goes; what was there by that name and could
    // not be written over -- nothing of this -- stays.
    if (wrote != Wrote::NotOpened)
    {
        std::filesystem::remove(beside, ec);
    }
    // Not all of it would go beside it -- the disk is full: in place it
    // would go no better, and would take the old text with it. A durable
    // write is whole or nothing.
    if (wrote == Wrote::Failed || durable)
    {
        return false;
    }
    // Nothing could be put beside it, or it could not be replaced: in place,
    // as it always was written.
    return writeAll(target, text, false) == Wrote::Done;
}

bool temp(const std::string& path, std::string_view text)
{
#if LL_WINDOWS
    llofstream out(fsyspath(path), std::ios::binary);
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
    bool        all = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() && st.st_nlink == 1 &&
               fchmod(fd, S_IRUSR | S_IWUSR) == 0 && ftruncate(fd, 0) == 0;
    size_t done = 0;
    while (all && done < text.size())
    {
        const ssize_t wrote = ::write(fd, text.data() + done, text.size() - done);
        if (wrote < 0 && errno == EINTR)
        {
            continue;
        }
        all = wrote > 0;
        done += wrote > 0 ? static_cast<size_t>(wrote) : 0;
    }
    return ::close(fd) == 0 && all;
#endif
}
}

namespace ALFileRead
{
bool whole(const std::string& path, std::string& out, std::uintmax_t most)
{
    out.clear();
    // What one look says it is, links followed: a device or a pipe would
    // be read for ever, or wait for somebody to write it, and a folder not
    // read at all.
    const ALFileStamp stamp = ALFileStamp::of(path);
    if (!stamp.exists || stamp.size > most)
    {
        return false;
    }
    llifstream in(fsyspath(path), std::ios::binary);
    if (!in)
    {
        return false;
    }
    // As much as the look said and a byte more, to see a file that grew
    // since: one that did is read on, a piece at a time, but never past
    // the limit.
    std::string text;
    size_t      want = static_cast<size_t>(stamp.size) + 1;
    for (;;)
    {
        const size_t at = text.size();
        text.resize(at + want);
        in.read(text.data() + at, static_cast<std::streamsize>(want));
        const std::streamsize got = in.gcount();
        if (got < 0)
        {
            return false;
        }
        text.resize(at + static_cast<size_t>(got));
        if (text.size() > most)
        {
            return false;
        }
        if (static_cast<size_t>(got) < want)
        {
            break;
        }
        want = static_cast<size_t>(std::min<std::uintmax_t>(64 * 1024, most + 1 - text.size()));
    }
    out = std::move(text);
    return true;
}
}

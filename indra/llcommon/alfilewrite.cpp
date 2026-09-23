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

#include "fsyspath.h"

#include <filesystem>
#include <fstream>

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
    // the file closes, and is the likeliest not to go.
    Wrote writeAll(const std::filesystem::path& file, std::string_view text)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            return Wrote::NotOpened;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        return out.fail() ? Wrote::Failed : Wrote::Done;
    }
}

namespace ALFileWrite
{
std::string besideOf(const std::string& path)
{
    // A name nothing else would give a file, since whatever is there by it
    // is written over and then gone.
    return path + ".saving";
}

bool whole(const std::string& path, std::string_view text)
{
    std::error_code ec;
    fsyspath        target(path);
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
        std::ofstream probe(target, std::ios::binary | std::ios::app);
        if (!probe.is_open())
        {
            return false;
        }
    }
    const fsyspath beside(besideOf(target.string()));
    const Wrote    wrote = writeAll(beside, text);
    if (wrote == Wrote::Done)
    {
        // What anyone could do with the old file, they can with the new.
        if (exists)
        {
            std::filesystem::permissions(beside, was.permissions(), ec);
        }
        std::filesystem::rename(beside, target, ec);
        if (!ec)
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
    // would go no better, and would take the old text with it.
    if (wrote == Wrote::Failed)
    {
        return false;
    }
    // Nothing could be put beside it, or it could not be replaced: in place,
    // as it always was written.
    return writeAll(target, text) == Wrote::Done;
}
}

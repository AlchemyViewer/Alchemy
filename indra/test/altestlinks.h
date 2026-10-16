/**
 * @file altestlinks.h
 * @brief A directory junction for a test, Windows' link to a folder that
 *        needs no privilege.
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

#ifndef AL_ALTESTLINKS_H
#define AL_ALTESTLINKS_H

#include <filesystem>
#include <system_error>

namespace ll_test
{
    // A directory junction at link, naming target, as mklink /J makes one:
    // Windows' own link to a folder, which any user may make where a symlink
    // takes a privilege or Developer Mode, and which std::filesystem's
    // canonical() follows as it follows a symlink. So a test of what a link
    // out of a folder may reach has a link to try on every Windows machine.
    // Elsewhere there is no such thing, and the error says so
    // (std::errc::not_supported); a test there makes a symlink, and skips
    // where it cannot.
    std::error_code createDirectoryJunction(const std::filesystem::path& target, const std::filesystem::path& link);
}

#endif // AL_ALTESTLINKS_H

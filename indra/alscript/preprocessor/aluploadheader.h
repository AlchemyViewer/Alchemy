/**
 * @file aluploadheader.h
 * @brief The lines a save can put at the top of the code it sends: where it came from, a hash of it, when, and who sent it.
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

#include <optional>
#include <string>
#include <string_view>

class LLDate;

// A few comment lines a save can put at the top of the code it sends, saying
// which file it was made from, a hash of what it is, when, and -- where the
// scripter asks -- who sent it. Ours, byte for byte:
//
//   // ================ alchemy meta ================
//   // @file net/door.lsl
//   // @hash xxh128:<32 hex digits>
//   // @date 2026-10-07 14:03:11
//   // @creator <name>
//   // ==============================================
//
// SLua gets the same with `--`. It goes in an envelope's compiled half, after
// the target line (ALScriptEnvelope::header), where the source map, the
// optimizer and the author's own text never see it; never at the top of a
// plain script, where it would land in the author's text.
//
// The VS Code plugin writes a block of its own at the top of what it sends,
// the same lines under a banner naming itself, with a sha256 for its hash
// and a @creatorID line as well. That is read too, so a script it sent says
// where it came from.
struct ALUploadHeader
{
    // The master file, from the blessed folder it is under, with `/`, or
    // `@alias/rest`. Never absolute: written only where it is not.
    std::string file;
    // `xxh128:` and the hash's hex (hashOf), in ours; the plugin's is bare
    // sha256 hex. Taken as read, never trusted: a hash read from the world
    // is worked out again from what it came with.
    std::string hash;
    // `YYYY-MM-DD HH:MM:SS`, in UTC (dateOf).
    std::string date;
    // Who sent it, where they chose to say; and their key, as the plugin
    // writes it (@creatorID). Each is written only where there is one.
    std::string creator;
    std::string creatorId;
    // Whether the banner read was ours; false for the plugin's.
    bool        ours = true;

    // The header's lines, each ending in a newline: the banner, a line a
    // field there is, and the closing line.
    std::string write(bool lua) const;

    // The header at the very start of a text, ours or the plugin's, or
    // nothing where the text does not start with one. take() also steps
    // past it, closing line and newline and all.
    static std::optional<ALUploadHeader> parse(std::string_view text, bool lua);
    static std::optional<ALUploadHeader> take(std::string_view& text, bool lua);

    // What is sent, hashed: what it is compiled for, then the author's source
    // and the code it expanded to -- after the optimizer, without the header
    // -- for a wrapped upload; the text alone for a plain one. After `v1`,
    // each part follows a nought byte, which no script's text holds, so that
    // where one part ends is hashed too. Answers `xxh128:` and the hash's 32
    // hex digits, as xxhsum writes them.
    static std::string hashOf(std::string_view target, std::string_view source, std::string_view expanded);
    static std::string hashOfPlain(std::string_view target, std::string_view text);

    // A time as @date writes it.
    static std::string dateOf(const LLDate& when);

    // Whether a path names a place from the root of a disk, a drive, a share
    // or a home folder, and so would say more of the scripter's machine than
    // where the file is in its folder.
    static bool absolute(std::string_view path);
};

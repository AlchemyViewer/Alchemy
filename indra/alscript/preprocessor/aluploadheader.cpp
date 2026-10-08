/**
 * @file aluploadheader.cpp
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

#include "linden_common.h"

#include "aluploadheader.h"

#include "hbxxh.h"
#include "lldate.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
    // Ours, and the plugin's banner's words between its runs of `=`.
    const std::string_view OUR_BANNER  = "================ alchemy meta ================";
    const std::string_view OUR_NAME    = "alchemy meta";
    const std::string_view PLUGIN_NAME = "sl-vscode-plugin meta";
    const std::string_view HASH_SCHEME = "xxh128:";
    // The plugin writes five fields at most; a block running on far past
    // that is some other comment, not a header.
    constexpr size_t MAX_FIELD_LINES = 16;

    std::string_view trimmed(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    // The next line of a text, without its newline; steps past it.
    std::string_view nextLine(std::string_view& rest)
    {
        const size_t           end  = rest.find('\n');
        const std::string_view line = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        return line;
    }

    bool allEquals(std::string_view text) { return !text.empty() && text.find_first_not_of('=') == std::string_view::npos; }

    bool sameWord(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return (x >= 'A' && x <= 'Z' ? x - 'A' + 'a' : x) == (y >= 'A' && y <= 'Z' ? y - 'A' + 'a' : y);
               });
    }

    // A value kept to its line: a line break in it would end the comment and
    // leave the rest of it as code.
    std::string oneLine(std::string_view value)
    {
        std::string out(value);
        std::replace_if(out.begin(), out.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
        return out;
    }

    void hashPart(HBXXH128& hash, std::string_view part)
    {
        const char nought = 0;
        hash.update(&nought, 1);
        hash.update(part.data(), part.size());
    }

    // HBXXH128 keeps the low half of the hash first, in the machine's own
    // order; xxhsum writes the high half first, most significant digit first.
    std::string hexOf(const LLUUID& digest)
    {
        U64 low  = 0;
        U64 high = 0;
        std::memcpy(&low, digest.mData, sizeof(low));
        std::memcpy(&high, digest.mData + sizeof(low), sizeof(high));
        char out[33];
        std::snprintf(out, sizeof(out), "%016llx%016llx", static_cast<unsigned long long>(high), static_cast<unsigned long long>(low));
        return std::string(HASH_SCHEME) + out;
    }

    std::string finished(HBXXH128& hash)
    {
        hash.finalize();
        return hexOf(hash.digest());
    }
}

std::string ALUploadHeader::write(bool lua) const
{
    const std::string lead = lua ? "-- " : "// ";
    std::string       out  = lead + std::string(OUR_BANNER) + "\n";
    auto              line = [&out, &lead](std::string_view word, std::string_view value) {
        if (!value.empty())
        {
            out += lead + std::string(word) + " " + oneLine(value) + "\n";
        }
    };
    line("@file", absolute(file) ? std::string_view() : std::string_view(file));
    line("@hash", hash);
    line("@date", date);
    line("@creator", creator);
    line("@creatorID", creatorId);
    out += lead + std::string(OUR_BANNER.size(), '=') + "\n";
    return out;
}

// static
std::optional<ALUploadHeader> ALUploadHeader::parse(std::string_view text, bool lua)
{
    return take(text, lua);
}

// static
std::optional<ALUploadHeader> ALUploadHeader::take(std::string_view& text, bool lua)
{
    const std::string_view lead = lua ? "--" : "//";
    std::string_view       rest = text;
    // Every line of it a comment of its own, from the very start of the
    // line: what is left of one after the lead, or nothing where the line
    // is not a comment.
    auto comment = [&lead](std::string_view line) -> std::optional<std::string_view> {
        if (line.substr(0, lead.size()) != lead)
        {
            return std::nullopt;
        }
        return trimmed(line.substr(lead.size()));
    };

    // The banner: runs of `=` either side of the words that say whose.
    const std::optional<std::string_view> banner = comment(nextLine(rest));
    if (!banner || banner->empty() || banner->front() != '=' || banner->back() != '=')
    {
        return std::nullopt;
    }
    std::string_view words = *banner;
    words.remove_prefix(std::min(words.find_first_not_of('='), words.size()));
    words.remove_suffix(words.size() - (words.find_last_not_of('=') + 1));
    words = trimmed(words);
    ALUploadHeader header;
    if (words == OUR_NAME)
    {
        header.ours = true;
    }
    else if (words == PLUGIN_NAME)
    {
        header.ours = false;
    }
    else
    {
        return std::nullopt;
    }

    for (size_t fields = 0; !rest.empty() && fields <= MAX_FIELD_LINES; ++fields)
    {
        const std::optional<std::string_view> body = comment(nextLine(rest));
        if (!body)
        {
            return std::nullopt;
        }
        if (allEquals(*body))
        {
            text = rest;
            return header;
        }
        if (body->empty() || body->front() != '@')
        {
            return std::nullopt;
        }
        const size_t           gap   = body->find_first_of(" \t");
        const std::string_view word  = body->substr(1, gap == std::string_view::npos ? std::string_view::npos : gap - 1);
        const std::string_view value = gap == std::string_view::npos ? std::string_view() : trimmed(body->substr(gap));
        if (sameWord(word, "file"))
        {
            header.file = value;
        }
        else if (sameWord(word, "hash"))
        {
            header.hash = value;
        }
        else if (sameWord(word, "date"))
        {
            header.date = value;
        }
        else if (sameWord(word, "creator"))
        {
            header.creator = value;
        }
        else if (sameWord(word, "creatorID"))
        {
            header.creatorId = value;
        }
        // Any other field is someone's to come, and passed over.
    }
    // No closing line.
    return std::nullopt;
}

// static
std::string ALUploadHeader::hashOf(std::string_view target, std::string_view source, std::string_view expanded)
{
    HBXXH128 hash;
    hash.update("v1", 2);
    hashPart(hash, target);
    hashPart(hash, source);
    hashPart(hash, expanded);
    return finished(hash);
}

// static
std::string ALUploadHeader::hashOfPlain(std::string_view target, std::string_view text)
{
    HBXXH128 hash;
    hash.update("v1", 2);
    hashPart(hash, target);
    hashPart(hash, text);
    return finished(hash);
}

// static
std::string ALUploadHeader::dateOf(const LLDate& when)
{
    S32 year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!when.split(&year, &month, &day, &hour, &minute, &second))
    {
        return std::string();
    }
    char out[64];
    std::snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
    return out;
}

// static
bool ALUploadHeader::absolute(std::string_view path)
{
    if (path.empty())
    {
        return false;
    }
    // From the root, a share (`\\host`), or a home folder.
    if (path.front() == '/' || path.front() == '\\' || path.front() == '~')
    {
        return true;
    }
    // A drive, with or without a separator after it: `C:` alone is wherever
    // that drive was last, and no more a folder's own.
    const char first = path.front();
    return path.size() >= 2 && path[1] == ':' && ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'));
}

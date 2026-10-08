/**
 * @file allinelabel.cpp
 * @brief What a `@line` comment names a file by: a file on disk by its path from the script's own folder.
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

#include "allinelabel.h"

#include "alincludeidentity.h"

#include <utility>

namespace
{
    bool slash(char c)
    {
        return c == '/' || c == '\\';
    }

    char lower(char c)
    {
        return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
    }

    bool letter(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    // Two parts of a path the same: in any case for Windows's, which is
    // the case its files are found in whatever it is written in -- the
    // letters of ASCII, which is what a drive, a share and most names are.
    bool same(std::string_view a, std::string_view b, bool any_case)
    {
        if (!any_case)
        {
            return a == b;
        }
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (lower(a[i]) != lower(b[i]))
            {
                return false;
            }
        }
        return true;
    }

    // A path from a root, by its parts: the root as two of the same root
    // compare -- `/`, a drive's `c:`, a share's `//host/share`, Windows's in
    // lower case -- and every part after it, `.` passed over and `..`
    // taking back the part before. Not `valid` for a path from no root, nor
    // for a device's.
    struct Path
    {
        bool                     valid   = false;
        // Windows's: `\` parts it as `/` does, and a part is the same in
        // any case.
        bool                     windows = false;
        std::string              root;
        std::vector<std::string> parts;
    };

    Path parse(std::string_view text)
    {
        Path out;
        // Windows's own forms of a path, `\\?\C:\a` and `\\?\UNC\host\share\a`,
        // `\\.\` the same and the NT namespace's `\??\` -- what a path whose
        // links were followed may come back as -- read as the drive's path
        // or the share's they stand for. Anything else after one is a
        // device's.
        bool share = false;
        if (text.size() >= 4 && slash(text[0]) && slash(text[3]) &&
            ((slash(text[1]) && (text[2] == '?' || text[2] == '.')) || (text[1] == '?' && text[2] == '?')))
        {
            text.remove_prefix(4);
            if (text.size() >= 4 && lower(text[0]) == 'u' && lower(text[1]) == 'n' && lower(text[2]) == 'c' && slash(text[3]))
            {
                text.remove_prefix(4);
                share = true;
            }
            else if (text.size() < 2 || !letter(text[0]) || text[1] != ':')
            {
                return out;
            }
        }
        else if (text.size() >= 2 && slash(text[0]) && slash(text[1]))
        {
            text.remove_prefix(2);
            share = true;
        }
        if (share)
        {
            // The machine and the share on it, both there.
            std::string_view parts[2];
            for (std::string_view& part : parts)
            {
                size_t end = 0;
                while (end < text.size() && !slash(text[end]))
                {
                    ++end;
                }
                part = text.substr(0, end);
                text.remove_prefix(end < text.size() ? end + 1 : end);
                if (part.empty())
                {
                    return out;
                }
            }
            out.windows = true;
            out.root    = "//" + std::string(parts[0]) + "/" + std::string(parts[1]);
        }
        else if (text.size() >= 2 && letter(text[0]) && text[1] == ':')
        {
            out.windows = true;
            out.root    = std::string(text.substr(0, 2));
            text.remove_prefix(2);
        }
        else if (!text.empty() && text[0] == '/')
        {
            out.root = "/";
            text.remove_prefix(1);
        }
        else if (!text.empty() && text[0] == '\\')
        {
            // From the root of whatever drive Windows is on.
            out.windows = true;
            out.root    = "\\";
            text.remove_prefix(1);
        }
        else
        {
            return out;
        }
        if (out.windows)
        {
            for (char& c : out.root)
            {
                c = lower(c);
            }
        }
        while (!text.empty())
        {
            size_t end = 0;
            while (end < text.size() && !(out.windows ? slash(text[end]) : text[end] == '/'))
            {
                ++end;
            }
            const std::string_view part = text.substr(0, end);
            text.remove_prefix(end < text.size() ? end + 1 : end);
            if (part.empty() || part == ".")
            {
                continue;
            }
            if (part == "..")
            {
                // As the disk takes it: no further up than the root.
                if (!out.parts.empty())
                {
                    out.parts.pop_back();
                }
                continue;
            }
            out.parts.emplace_back(part);
        }
        out.valid = true;
        return out;
    }

    // The path from one to the other, as ALLineLabel::relative gives it.
    std::string between(const Path& from, const Path& to)
    {
        if (!from.valid || !to.valid || from.windows != to.windows || from.root != to.root)
        {
            return std::string();
        }
        size_t common = 0;
        while (common < from.parts.size() && common < to.parts.size() && same(from.parts[common], to.parts[common], from.windows))
        {
            ++common;
        }
        // The file is the folder, or a folder the folder is in.
        if (common == to.parts.size())
        {
            return std::string();
        }
        std::string out;
        for (size_t i = common; i < from.parts.size(); ++i)
        {
            out += "../";
        }
        for (size_t i = common; i < to.parts.size(); ++i)
        {
            if (i > common)
            {
                out += '/';
            }
            out += to.parts[i];
        }
        return out;
    }
}

ALLineLabel::ALLineLabel(const std::string& script, std::vector<std::string> folders)
    : mFolders(std::move(folders))
{
    if (!ALIncludeIdentity::fileOf(script, mScript))
    {
        mScript.clear();
    }
}

std::string ALLineLabel::of(const ALSourceMap::File& file, bool script) const
{
    Path folder = parse(mScript);
    if (script)
    {
        return quotable(folder.valid && !folder.parts.empty() ? folder.parts.back() : file.name);
    }
    std::string path;
    if (!ALIncludeIdentity::fileOf(file.path, path))
    {
        // From the world, or named by the caller's resolver some other way:
        // its name, as ever.
        return quotable(file.name);
    }
    const Path to = parse(path);
    // From the script's own folder, the folder up from its file.
    if (folder.valid && !folder.parts.empty())
    {
        folder.parts.pop_back();
        if (const std::string label = between(folder, to); !label.empty())
        {
            return quotable(label);
        }
    }
    // From the blessed folder it is under: the outermost of those that
    // hold it, which says the most of where it is.
    std::string best;
    for (const std::string& blessed : mFolders)
    {
        const std::string label = between(parse(blessed), to);
        if (!label.empty() && label.compare(0, 3, "../") != 0 && label.size() > best.size())
        {
            best = label;
        }
    }
    return quotable(best.empty() ? file.name : best);
}

// static
std::string ALLineLabel::relative(std::string_view folder, std::string_view file)
{
    return between(parse(folder), parse(file));
}

// static
std::string ALLineLabel::quotable(std::string_view label)
{
    std::string out(label);
    for (char& c : out)
    {
        if (c == '"')
        {
            c = '\'';
        }
        else if (c == '\\')
        {
            c = '/';
        }
        else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
        {
            c = '_';
        }
    }
    return out;
}

/**
 * @file almastermatch.cpp
 * @brief Which file on disk a script in the world may have as its master: by what it says, or by its name.
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

#include "almastermatch.h"

#include "alluauconfig.h"
#include "aluploadheader.h"

#include "fsyspath.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>

namespace
{
    char lower(char c)
    {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    // Two names the same, in any case where asked: the letters of ASCII,
    // which is what most names are.
    bool same(std::string_view a, std::string_view b, bool any_case)
    {
        if (!any_case)
        {
            return a == b;
        }
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
    }

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

    // A path as a hint gives it: trimmed, and out of the quotes it may be
    // put in for its spaces.
    std::string pathOf(std::string_view value)
    {
        value = trimmed(value);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = trimmed(value.substr(1, value.size() - 2));
        }
        return std::string(value);
    }

    // The path an `@file` comment line gives, or nothing for any other
    // line: the comment's lead first on the line but for blanks, then
    // `@file` in any case, then a blank, then the path.
    std::optional<std::string> commentHint(std::string_view line, std::string_view lead)
    {
        line = trimmed(line);
        if (line.substr(0, lead.size()) != lead)
        {
            return std::nullopt;
        }
        line = trimmed(line.substr(lead.size()));
        constexpr std::string_view WORD = "@file";
        if (line.size() <= WORD.size() || !same(line.substr(0, WORD.size()), WORD, true) || (line[WORD.size()] != ' ' && line[WORD.size()] != '\t'))
        {
            return std::nullopt;
        }
        std::string path = pathOf(line.substr(WORD.size()));
        if (path.empty())
        {
            return std::nullopt;
        }
        return path;
    }

    std::optional<std::string> headerHint(std::string_view text, bool lua)
    {
        const std::optional<ALUploadHeader> header = ALUploadHeader::parse(text, lua);
        if (!header)
        {
            return std::nullopt;
        }
        std::string path = pathOf(header->file);
        if (path.empty())
        {
            return std::nullopt;
        }
        return path;
    }

    // What a script of a language is called: LSL's `.lsl`, SLua's `.luau`
    // and `.lua`. An include's own `.lslh` and `.lsli` master no script.
    const std::vector<std::string>& masterExtensions(bool lua)
    {
        static const std::vector<std::string> LUA{ ".luau", ".lua" };
        static const std::vector<std::string> LSL{ ".lsl" };
        return lua ? LUA : LSL;
    }

    std::string joined(const std::string& folder, const std::string& relative)
    {
        return fsyspath((fsyspath(folder) / fsyspath(relative)).lexically_normal()).string();
    }

    size_t depthOf(const std::string& relative)
    {
        return static_cast<size_t>(std::count(relative.begin(), relative.end(), '/'));
    }

    // Whether a file, by its path from the folder it was found under, is
    // named `want` there: the whole path, or its last parts.
    bool namedAs(std::string_view relative, std::string_view want, bool any_case)
    {
        if (same(relative, want, any_case))
        {
            return true;
        }
        return relative.size() > want.size() && relative[relative.size() - want.size() - 1] == '/' &&
               same(relative.substr(relative.size() - want.size()), want, any_case);
    }

    // Whether its path from the folder, the folders run into its name as
    // the VS Code plugin runs them -- `/` taken out, or made `_` or a
    // space -- is `want`.
    bool looselyNamedAs(const std::string& relative, std::string_view want, bool any_case)
    {
        if (relative.find('/') == std::string::npos)
        {
            return false;
        }
        std::string run;
        std::copy_if(relative.begin(), relative.end(), std::back_inserter(run), [](char c) { return c != '/'; });
        if (same(run, want, any_case))
        {
            return true;
        }
        for (const char between : { '_', ' ' })
        {
            run = relative;
            std::replace(run.begin(), run.end(), '/', between);
            if (same(run, want, any_case))
            {
                return true;
            }
        }
        return false;
    }
}

// static
std::optional<std::string> ALMasterMatch::hintOf(std::string_view source, std::string_view compiled, bool lua)
{
    const std::string_view     lead = lua ? "--" : "//";
    constexpr std::string_view BOM  = "\xEF\xBB\xBF";
    if (source.substr(0, BOM.size()) == BOM)
    {
        source.remove_prefix(BOM.size());
    }
    std::string_view rest = source;
    for (size_t line = 0; line < HINT_LINES && !rest.empty(); ++line)
    {
        if (std::optional<std::string> hint = commentHint(nextLine(rest), lead))
        {
            return hint;
        }
    }
    if (std::optional<std::string> hint = headerHint(compiled, lua))
    {
        return hint;
    }
    // The compiled half as the asset has it, its target line first.
    std::string_view       after  = compiled;
    const std::string_view target = trimmed(nextLine(after));
    if (target == (lua ? "--luau" : "//mono") || (!lua && target == "//lsl2"))
    {
        return headerHint(after, lua);
    }
    return std::nullopt;
}

// static
std::optional<std::string> ALMasterMatch::resolve(const std::string& hint, const ALDiskIncludes& blessed,
                                                  const std::vector<std::pair<std::string, std::string>>& aliases, bool lua, std::string& why)
{
    why.clear();
    const std::string written = pathOf(hint);
    if (written.empty())
    {
        why = "the @file hint names no file";
        return std::nullopt;
    }
    const std::vector<std::string>& extensions = ALDiskIncludes::scriptExtensions(lua);
    const std::string               kind       = lua ? "an SLua script (.luau or .lua)" : "an LSL script (.lsl, .lslh or .lsli)";
    if (ALDiskIncludes::extensionOf(written, extensions) == 0)
    {
        why = "'" + written + "' is not " + kind;
        return std::nullopt;
    }

    // The paths it may be, each from a root.
    std::vector<std::string> paths;
    if (written.front() == '@')
    {
        std::string alias;
        std::string rest;
        if (!ALLuauConfig::aliasOf(written, alias, rest) || rest.empty())
        {
            why = "'" + written + "' names no alias and file in it";
            return std::nullopt;
        }
        const auto named = std::find_if(aliases.begin(), aliases.end(), [&alias](const std::pair<std::string, std::string>& one) {
            const std::string_view name = !one.first.empty() && one.first.front() == '@' ? std::string_view(one.first).substr(1) : one.first;
            return same(name, alias, true);
        });
        if (named == aliases.end())
        {
            why = "'" + written + "' names the alias @" + alias + ", which is not set";
            return std::nullopt;
        }
        if (!ALLuauConfig::absolute(named->second))
        {
            why = "the alias @" + alias + " is no folder on disk";
            return std::nullopt;
        }
        std::replace(rest.begin(), rest.end(), '\\', '/');
        paths.push_back(joined(named->second, rest));
    }
    else if (ALUploadHeader::absolute(written))
    {
        if (written.front() == '~')
        {
            why = "'" + written + "' is from a home folder, which a hint is not followed to";
            return std::nullopt;
        }
        paths.push_back(written);
    }
    else
    {
        if (blessed.folders().empty())
        {
            why = "no include folders are set, so '" + written + "' is looked for nowhere";
            return std::nullopt;
        }
        std::string relative = written;
        std::replace(relative.begin(), relative.end(), '\\', '/');
        for (const std::string& folder : blessed.folders())
        {
            paths.push_back(joined(folder, relative));
        }
    }

    size_t outside = 0;
    bool   other   = false;
    for (const std::string& path : paths)
    {
        // Judged as written before the disk is asked a thing: a share named
        // is never reached for.
        if (!ALDiskIncludes::lexicallyUnder(path, blessed.folders()))
        {
            ++outside;
            continue;
        }
        const std::optional<std::string> file = blessed.admits(path);
        if (!file)
        {
            continue;
        }
        // A script as it stands, too: a link of a script's name to
        // something else is not one.
        if (ALDiskIncludes::extensionOf(*file, extensions) == 0)
        {
            other = true;
            continue;
        }
        return file;
    }
    if (other)
    {
        why = "'" + written + "' leads to a file that is not " + kind;
    }
    else if (outside == paths.size())
    {
        why = "'" + written + "' is outside the include folders";
    }
    else
    {
        why = "'" + written + "' is not a file in the include folders, or not an ordinary file of at most " +
              std::to_string(ALDiskIncludes::MAX_BYTES / (1024 * 1024)) + " MB";
    }
    return std::nullopt;
}

// static
std::vector<ALDiskIncludes::Listed> ALMasterMatch::listing(const ALDiskIncludes& blessed, bool lua, const std::function<bool()>& stopped)
{
    std::vector<ALDiskIncludes::Listed> out;
    for (const std::string& folder : blessed.folders())
    {
        if (stopped && stopped())
        {
            break;
        }
        std::vector<ALDiskIncludes::Listed> found = blessed.filesUnder(folder, masterExtensions(lua), NAME_DEPTH, NAME_ENTRIES, NAME_FILES, stopped);
        // The nearer the folder's top the likelier, then by path: the order
        // the disk lists them in is its own.
        std::sort(found.begin(), found.end(), [](const ALDiskIncludes::Listed& a, const ALDiskIncludes::Listed& b) {
            const size_t x = depthOf(a.relative);
            const size_t y = depthOf(b.relative);
            return x != y ? x < y : a.relative < b.relative;
        });
        std::move(found.begin(), found.end(), std::back_inserter(out));
    }
    return out;
}

// static
std::vector<std::string> ALMasterMatch::byName(const std::string& item_name, const ALDiskIncludes& blessed, bool lua)
{
    if (item_name.empty())
    {
        return {};
    }
    return byName(item_name, listing(blessed, lua), lua);
}

// static
std::vector<std::string> ALMasterMatch::byName(const std::string& item_name, const std::vector<ALDiskIncludes::Listed>& listed, bool lua)
{
    if (item_name.empty())
    {
        return {};
    }
    const std::vector<std::string>& extensions = masterExtensions(lua);
    std::vector<std::string>        wanted;
    if (ALDiskIncludes::extensionOf(item_name, extensions) > 0)
    {
        wanted.push_back(item_name);
    }
    for (const std::string& extension : extensions)
    {
        wanted.push_back(item_name + extension);
    }

    // Each file by the best way it matched: the name, then the loose
    // forms, each in its own case and then in any; within each, the
    // wanted names in order.
    struct Found
    {
        size_t rank;
        size_t at;
    };
    std::vector<Found> found;
    for (size_t at = 0; at < listed.size(); ++at)
    {
        const std::string& relative = listed[at].relative;
        size_t             best     = SIZE_MAX;
        for (size_t tier = 0; tier < 4 && best == SIZE_MAX; ++tier)
        {
            const bool loose    = tier >= 2;
            const bool any_case = (tier & 1) != 0;
            for (size_t want = 0; want < wanted.size(); ++want)
            {
                if (loose ? looselyNamedAs(relative, wanted[want], any_case) : namedAs(relative, wanted[want], any_case))
                {
                    best = tier * wanted.size() + want;
                    break;
                }
            }
        }
        if (best != SIZE_MAX)
        {
            found.push_back({ best, at });
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.rank < b.rank; });

    std::vector<std::string>                                                  out;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const Found& one : found)
    {
        if (seen.insert(listed[one.at].file).second)
        {
            out.push_back(listed[one.at].file);
        }
    }
    return out;
}

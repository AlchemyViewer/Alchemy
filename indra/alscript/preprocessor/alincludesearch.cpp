/**
 * @file alincludesearch.cpp
 * @brief What an include or a require names, found as the preprocessor finds it: the world asked, the disk read.
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
 */

#include "linden_common.h"

#include "alincludesearch.h"

#include "alincludeidentity.h"
#include "almessagemap.h"
#include "alluauconfigscript.h"
#include "alrequirenavigation.h"
#include "fsyspath.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <filesystem>

namespace
{
    // A path's folder, as far as its last separator; nothing for a bare
    // name.
    std::string dirOf(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    // A name under a folder, one separator between them.
    std::string joined(const std::string& dir, const std::string& name)
    {
        if (dir.empty() || name.empty())
        {
            return dir + name;
        }
#if LL_WINDOWS
        constexpr char SEPARATOR = '\\';
#else
        constexpr char SEPARATOR = '/';
#endif
        const bool ends   = dir.back() == '/' || dir.back() == '\\';
        const bool starts = name.front() == '/' || name.front() == '\\';
        if (ends && starts)
        {
            return dir + name.substr(1);
        }
        return ends || starts ? dir + name : dir + SEPARATOR + name;
    }
}

namespace
{
    constexpr std::string_view DISK_FOLDER = "disk:";
    constexpr std::string_view ABOVE       = "above:";
    const std::string          CONFIG_NAME(".luaurc");

    // A name with an extension of its own, as the plugin reads one: a dot
    // past its first letter.
    bool hasExtension(const std::string& name)
    {
        const size_t dot = name.rfind('.');
        return dot != std::string::npos && dot > 0;
    }

    // A file's name as a module: a script's extension taken off.
    std::string stemOf(const std::string& name)
    {
        for (std::string_view extension : { std::string_view(".luau"), std::string_view(".lua") })
        {
            if (name.size() > extension.size() &&
                std::equal(extension.begin(), extension.end(), name.end() - extension.size(),
                           [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); }))
            {
                return name.substr(0, name.size() - extension.size());
            }
        }
        return name;
    }

    // What a folder on disk holds, as a path names it: each file with one of
    // `extensions` -- by its module name, the extension taken off, where
    // `modules` -- and each folder but a hidden one; no more than a few
    // hundred looked at.
    constexpr size_t LISTED_MOST = 500;
    void listFolder(const std::string& dir, const std::vector<std::string_view>& extensions, bool modules, std::vector<ALRequirePlaces::Child>& out)
    {
        std::error_code ec;
        size_t          looked = 0;
        for (std::filesystem::directory_iterator it(fsyspath(dir), ec), end; !ec && it != end && looked < LISTED_MOST; it.increment(ec), ++looked)
        {
            const std::string name = fsyspath(it->path().filename()).string();
            if (name.empty() || name.front() == '.')
            {
                continue;
            }
            std::error_code kind;
            if (it->is_directory(kind))
            {
                out.push_back({ name, true });
                continue;
            }
            for (std::string_view extension : extensions)
            {
                if (name.size() > extension.size() &&
                    std::equal(extension.begin(), extension.end(), name.end() - extension.size(),
                               [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); }))
                {
                    out.push_back({ modules ? name.substr(0, name.size() - extension.size()) : name, false });
                    break;
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const ALRequirePlaces::Child& a, const ALRequirePlaces::Child& b) { return a.name < b.name; });
        out.erase(std::unique(out.begin(), out.end(), [](const ALRequirePlaces::Child& a, const ALRequirePlaces::Child& b) { return a.name == b.name; }),
                  out.end());
    }

    bool isFolder(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_directory(fsyspath(path), ec);
    }
    bool isFile(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(fsyspath(path), ec);
    }
}

// The places a SLua require is walked through, as the search sees them now:
// folders on disk, `disk:` and the path; with world includes on, the
// world's folders, by its own ids (ALIncludeWorld); above a script in the
// world, or one in no place -- the empty id -- `above:` and the number of
// each of the scripter's include folders with a `.luaurc` at its top, in
// their order. What is read on disk is only ever a `.luaurc`; a module's
// file is read once the walk is done, where the folders blessed admit it.
class ALIncludeSearch::Places final : public ALRequirePlaces
{
public:
    Places(ALIncludeSearch& search, const Where& where, wanted_t* wanted, bool retry, std::vector<std::string>& alias_folders)
    :   mSearch(search),
        mWhere(where),
        mWanted(wanted),
        mRetry(retry),
        mAliasFolders(alias_folders)
    {
    }

    bool placeOf(const std::string& path, std::string& folder, std::string& name) override
    {
        std::string file;
        if (ALIncludeIdentity::fileOf(path, file))
        {
            if (!mWhere.disk)
            {
                return false;
            }
            const std::filesystem::path at = fsyspath(file);
            folder = std::string(DISK_FOLDER) + fsyspath(at.parent_path()).string();
            name   = stemOf(fsyspath(at.filename()).string());
            return true;
        }
        std::string item_name;
        if (mWhere.world && ALIncludeIdentity::inWorld(path) && mSearch.mWorld.folderOf(path, folder, item_name))
        {
            name = stemOf(item_name);
            return true;
        }
        return false;
    }

    bool placeOfAbsolute(const std::string& path_in, std::string& folder, std::string& name) override
    {
        if (!mWhere.disk)
        {
            return false;
        }
        std::filesystem::path at = fsyspath(path_in);
        if (!at.has_filename())
        {
            at = at.parent_path();
        }
        folder = std::string(DISK_FOLDER) + fsyspath(at.parent_path()).string();
        name   = fsyspath(at.filename()).string();
        return true;
    }

    Known parentOf(const std::string& folder, std::string& out) override
    {
        if (folder.empty())
        {
            return above(0, out);
        }
        if (folder.compare(0, ABOVE.size(), ABOVE) == 0)
        {
            return above(std::strtoul(folder.c_str() + ABOVE.size(), nullptr, 10) + 1, out);
        }
        std::string dir;
        if (diskDir(folder, dir))
        {
            const std::filesystem::path at = fsyspath(dir);
            if (!at.has_parent_path() || at.parent_path() == at)
            {
                return Known::No;
            }
            out = std::string(DISK_FOLDER) + fsyspath(at.parent_path()).string();
            return Known::Yes;
        }
        if (!mWhere.world)
        {
            return Known::No;
        }
        // The top of the world: the scripter's include folders above it.
        switch (mSearch.mWorld.folderAbove(folder, out))
        {
            case ALPreprocessor::Found::Yes:
                return Known::Yes;
            case ALPreprocessor::Found::Pending:
                return Known::Pending;
            default:
                return above(0, out);
        }
    }

    Known subfolder(const std::string& folder, const std::string& name, std::string& out) override
    {
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)
        {
            return Known::No;
        }
        std::string dir;
        if (diskDir(folder, dir))
        {
            const std::string path = fsyspath(fsyspath(dir) / fsyspath(name)).string();
            if (!isFolder(path))
            {
                return Known::No;
            }
            out = std::string(DISK_FOLDER) + path;
            return Known::Yes;
        }
        if (!inWorld(folder))
        {
            return Known::No;
        }
        std::vector<ALIncludeWorld::Item> items;
        const ALPreprocessor::Found       found = mSearch.mWorld.named(folder, name, items, out);
        if (found == ALPreprocessor::Found::Pending)
        {
            return Known::Pending;
        }
        return out.empty() ? Known::No : Known::Yes;
    }

    Known files(const std::string& folder, const std::string& name, std::vector<File>& out) override
    {
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)
        {
            return Known::No;
        }
        // As written where it has an extension; else with a script's, the
        // modern one first.
        std::vector<std::string> names{ name };
        if (!hasExtension(name))
        {
            names = { name + ".luau", name + ".lua" };
        }
        std::string dir;
        if (diskDir(folder, dir))
        {
            for (const std::string& one : names)
            {
                const std::string path = fsyspath(fsyspath(dir) / fsyspath(one)).string();
                if (isFile(path))
                {
                    out.push_back({ ALIncludeIdentity::ofFile(path), one, LLUUID::null, path });
                }
            }
            return out.empty() ? Known::No : Known::Yes;
        }
        if (!inWorld(folder))
        {
            return Known::No;
        }
        // An item is named as it is, which is usually with no extension at
        // all: that first, then one with a script's.
        if (!hasExtension(name))
        {
            names.insert(names.begin(), name);
        }
        bool pending = false;
        for (const std::string& one : names)
        {
            std::vector<ALIncludeWorld::Item> items;
            std::string                       sub;
            pending = mSearch.mWorld.named(folder, one, items, sub) == ALPreprocessor::Found::Pending || pending;
            for (const ALIncludeWorld::Item& item : items)
            {
                out.push_back({ item.path, item.name, item.assetId, std::string() });
            }
        }
        return pending ? Known::Pending : out.empty() ? Known::No : Known::Yes;
    }

    Known config(const std::string& folder, ALRequirePlaces::Config& out) override
    {
        std::string dir;
        if (diskDir(folder, dir))
        {
            out.onDisk = true;
            out.base   = folder;
            return mSearch.readConfig(mSearch.configIn(dir), out) ? Known::Yes : Known::No;
        }
        if (folder.compare(0, ABOVE.size(), ABOVE) == 0)
        {
            const size_t                                    index = std::strtoul(folder.c_str() + ABOVE.size(), nullptr, 10);
            const std::vector<ALIncludeSearch::DiskConfig>& tops  = atTop();
            if (index >= tops.size())
            {
                return Known::No;
            }
            out.onDisk = true;
            out.base   = std::string(DISK_FOLDER) + tops[index].dir;
            return mSearch.readConfig(tops[index], out) ? Known::Yes : Known::No;
        }
        if (!inWorld(folder))
        {
            return Known::No;
        }
        // In the world a `.luaurc` alone: a `.config.luau` is run only from
        // the disk.
        std::vector<ALIncludeWorld::Item> items;
        std::string                       sub;
        if (mSearch.mWorld.named(folder, CONFIG_NAME, items, sub) == ALPreprocessor::Found::Pending)
        {
            return Known::Pending;
        }
        if (items.empty())
        {
            return Known::No;
        }
        out.onDisk = false;
        out.base   = folder;
        std::string                 asset;
        const ALPreprocessor::Found found =
            mSearch.textOf({ items.front().path, CONFIG_NAME, items.front().assetId, std::string() }, mWanted, mRetry, out.text, asset);
        return found == ALPreprocessor::Found::Yes ? Known::Yes : found == ALPreprocessor::Found::Pending ? Known::Pending : Known::No;
    }

    Known studioAlias(const std::string& alias, std::string& folder) override
    {
        if (!mWhere.disk)
        {
            return Known::No;
        }
        for (const auto& [name, path] : mWhere.aliases)
        {
            if (name == alias && isFolder(path))
            {
                folder = std::string(DISK_FOLDER) + path;
                return Known::Yes;
            }
        }
        return Known::No;
    }

    std::vector<std::string> studioAliasNames() override
    {
        std::vector<std::string> out;
        if (mWhere.disk)
        {
            for (const auto& [name, path] : mWhere.aliases)
            {
                out.push_back(name);
            }
        }
        return out;
    }

    Known children(const std::string& folder, std::vector<Child>& out) override
    {
        std::string dir;
        if (diskDir(folder, dir))
        {
            listFolder(dir, { ".luau", ".lua" }, true, out);
            return Known::Yes;
        }
        if (!inWorld(folder))
        {
            return Known::No;
        }
        std::vector<ALIncludeWorld::Item> items;
        std::vector<std::string>          folders;
        const ALPreprocessor::Found       found = mSearch.mWorld.contents(folder, items, folders);
        for (const ALIncludeWorld::Item& item : items)
        {
            out.push_back({ stemOf(item.name), false });
        }
        for (const std::string& name : folders)
        {
            out.push_back({ name, true });
        }
        return found == ALPreprocessor::Found::Pending ? Known::Pending : Known::Yes;
    }

    void aliasReached(const std::string& config_folder, const std::string& folder) override
    {
        std::string config_dir, dir;
        if (!mWhere.disk || !diskDir(config_folder, config_dir) || !diskDir(folder, dir) ||
            std::find(mAliasFolders.begin(), mAliasFolders.end(), dir) != mAliasFolders.end())
        {
            return;
        }
        ALDiskIncludes own = mSearch.ownFolders(mWhere).includes;
        if (own.blessFromConfig(dir, config_dir))
        {
            mAliasFolders.push_back(dir);
        }
    }

private:
    // A folder on disk's path, where it is one -- and the disk is read.
    bool diskDir(const std::string& folder, std::string& dir) const
    {
        if (!mWhere.disk || folder.compare(0, DISK_FOLDER.size(), DISK_FOLDER) != 0)
        {
            return false;
        }
        dir = folder.substr(DISK_FOLDER.size());
        return !dir.empty();
    }
    // Whether a folder is the world's, and the world is let in.
    bool inWorld(const std::string& folder) const
    {
        return mWhere.world && !folder.empty() && folder.compare(0, ABOVE.size(), ABOVE) != 0 &&
               folder.compare(0, DISK_FOLDER.size(), DISK_FOLDER) != 0;
    }
    Known above(size_t index, std::string& out)
    {
        if (index >= atTop().size())
        {
            return Known::No;
        }
        out = std::string(ABOVE) + std::to_string(index);
        return Known::Yes;
    }
    const std::vector<ALIncludeSearch::DiskConfig>& atTop()
    {
        if (!mTops)
        {
            mTops = mSearch.configsAtTop(mWhere);
        }
        return *mTops;
    }

    ALIncludeSearch&                        mSearch;
    const Where&                            mWhere;
    wanted_t*                               mWanted;
    bool                                    mRetry;
    std::vector<std::string>&               mAliasFolders;
    std::optional<std::vector<ALIncludeSearch::DiskConfig>> mTops;
};

ALIncludeSearch::ALIncludeSearch(ALScriptTextCache& texts, ALIncludeWorld& world) : mTexts(texts), mWorld(world) {}

// static
std::string ALIncludeSearch::itemNameOf(const std::string& name)
{
    std::string out = name;
    if (out.compare(0, 2, "./") == 0)
    {
        out.erase(0, 2);
    }
    const size_t slash = out.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        out.erase(0, slash + 1);
    }
    return out;
}

// static
std::vector<std::string> ALIncludeSearch::foldersOf(const std::string& name)
{
    std::vector<std::string> parts;
    size_t                   from = 0;
    while (true)
    {
        const size_t slash = name.find_first_of("/\\", from);
        if (slash == std::string::npos)
        {
            break;
        }
        if (slash > from)
        {
            parts.push_back(name.substr(from, slash - from));
        }
        from = slash + 1;
    }
    return parts;
}

bool ALIncludeSearch::heldText(const std::string& path, std::string& text) const
{
    if (mTexts.held(path, text))
    {
        return true;
    }
    // A file on disk only where a run admitted it, which is the only way
    // its identity reaches anybody to ask with.
    std::string file;
    return mAdmitted.contains(path) && ALIncludeIdentity::fileOf(path, file) && ALDiskIncludes::readOrdinary(file, text);
}

ALDiskCache::Blessed& ALIncludeSearch::ownFolders(const Where& where)
{
    static const std::vector<std::string> NONE;
    return mDisk.blessed(where.disk ? where.folders : NONE, false, std::string(), NONE, where.generation, where.now);
}

ALDiskCache::Blessed& ALIncludeSearch::blessedFor(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                                  const std::vector<std::string>& alias_folders)
{
    // Nothing on disk while the disk is off: not the scripter's folders,
    // nor what a configuration on disk lists. Otherwise the scripter's
    // own, blessed first; for LSL what each of those folders' own `.lslrc`
    // lists, as far as a configuration may reach, and the nearest `.lslrc`
    // up from a file asking that is itself on disk -- a configuration in
    // the world is anybody's; and the aliases of a `.luaurc` on disk the
    // run has gone through, which resolve kept only where they may be.
    if (!where.disk)
    {
        return ownFolders(where);
    }
    std::string from, from_dir;
    if (!asking.lua && ALIncludeIdentity::fileOf(ask.from, from))
    {
        from_dir = dirOf(from);
    }
    return mDisk.blessed(where.folders, !asking.lua, from_dir, alias_folders, where.generation, where.now);
}

std::vector<ALIncludeSearch::Candidate> ALIncludeSearch::candidatesFor(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                                                       const std::vector<std::string>& alias_folders, bool& unknown)
{
    unknown = false;
    std::vector<Candidate> out;
    const std::string      item_name = itemNameOf(ask.name);
    const auto             from_world = [&out](const std::vector<ALIncludeWorld::Item>& items) {
        for (const ALIncludeWorld::Item& item : items)
        {
            out.push_back({ item.path, item.name, item.assetId, std::string() });
        }
    };
    for (const std::string& source : where.order)
    {
        // The world only where the scripter let it in.
        if (!where.world && (source == "object" || source == "inventory"))
        {
            continue;
        }
        if (source == "object")
        {
            bool not_said = false;
            from_world(mWorld.inObject(asking.self, item_name, not_said));
            unknown = unknown || not_said;
        }
        else if (source == "inventory")
        {
            // The folders the name gives, where it gives any, choose among
            // items of the name -- under the asking file's, for a name that
            // starts from there -- and no other where there is one.
            from_world(mWorld.inInventory(item_name, foldersOf(ask.name), ask.from));
        }
        else if (source == "disk")
        {
            // Only under a folder somebody blessed: the scripter's own
            // include folders, and what a `.lslrc` or a `.luaurc` that is
            // itself on disk lists. Nothing in the world blesses anything,
            // nor does the folder a script is in, nor a path from a root.
            ALDiskCache::Blessed& blessed = blessedFor(ask, asking, where, alias_folders);
            if (!blessed.includes.blessed())
            {
                continue;
            }
            // Where a name is looked for: beside the file asking, where it
            // is on disk, as a require expects; then the blessed folders.
            std::vector<std::string> dirs;
            std::string              from;
            if (ALIncludeIdentity::fileOf(ask.from, from))
            {
                dirs.push_back(dirOf(from));
            }
            for (const std::string& folder : blessed.includes.folders())
            {
                if (std::find(dirs.begin(), dirs.end(), folder) == dirs.end())
                {
                    dirs.push_back(folder);
                }
            }
            const std::vector<std::string> names = ALDiskIncludes::namesFor(ask.name, asking.lua, ask.require);
            if (ALLuauConfig::absolute(ask.name))
            {
                // A path from a root, which an alias may stand for: the
                // file itself, where a blessed folder holds it.
                dirs.assign(1, std::string());
            }
            for (const std::string& dir : dirs)
            {
                for (const std::string& name : names)
                {
                    const std::string                file = dir.empty() ? name : joined(dir, name);
                    const std::optional<std::string> real = mDisk.admits(blessed, file);
                    if (!real)
                    {
                        continue;
                    }
                    out.push_back(admitted(*real));
                }
            }
        }
    }
    return out;
}

ALPreprocessor::Found ALIncludeSearch::textOf(const Candidate& c, wanted_t* wanted, bool retry, std::string& text, std::string& assetId)
{
    if (!c.file.empty())
    {
        assetId.clear();
        return mDisk.read(c.file, text) ? ALPreprocessor::Found::Yes : ALPreprocessor::Found::No;
    }
    if (mTexts.take(c.path, c.assetId, text))
    {
        assetId = c.assetId.isNull() ? std::string() : c.assetId.asString();
        return ALPreprocessor::Found::Yes;
    }
    if (mTexts.hasFailed(c.path))
    {
        if (!retry)
        {
            return ALPreprocessor::Found::No;
        }
        // Asked for again, once: an include that was not there when this
        // script was last expanded may be there now.
        mTexts.forgetFailure(c.path);
    }
    if (wanted)
    {
        wanted->insert(c.path);
    }
    return ALPreprocessor::Found::Pending;
}

ALPreprocessor::Found ALIncludeSearch::configsFor(const std::string& from, const Asking& asking, const Where& where, wanted_t* wanted, bool retry,
                                                  std::vector<Config>& out)
{
    out.clear();
    std::vector<Candidate>  chain;
    std::vector<DiskConfig> disk;
    std::string             file;
    // A configuration in the world only where the world is let in: an
    // object is not asked what it holds otherwise, and would never say. A
    // `.luaurc` alone there: a `.config.luau` is run only from the disk.
    const bool in_world = ALIncludeIdentity::inWorld(from);
    if (in_world && where.world)
    {
        std::vector<ALIncludeWorld::Item> items;
        if (mWorld.configsOver(from, CONFIG_NAME, items) == ALPreprocessor::Found::Pending)
        {
            return ALPreprocessor::Found::Pending;
        }
        for (const ALIncludeWorld::Item& item : items)
        {
            chain.push_back({ item.path, CONFIG_NAME, item.assetId, std::string() });
        }
    }
    else if (!in_world && ALIncludeIdentity::fileOf(from, file))
    {
        // Up the directories from the file's own, every one to the root.
        disk = configsUp(dirOf(file), where);
    }
    if (in_world)
    {
        // Above whatever the world has, the one at the top of each of the
        // scripter's include folders, while the disk is read: a script in
        // the world has no folders on disk to look up through, and its
        // scripter's modules are read from those.
        disk = configsAtTop(where);
    }
    // Every text asked for at once: Pending while any is on its way.
    bool pending = false;
    for (const Candidate& c : chain)
    {
        Config                      config;
        std::string                 asset;
        const ALPreprocessor::Found found = textOf(c, wanted, retry, config.text, asset);
        if (found == ALPreprocessor::Found::Pending)
        {
            pending = true;
        }
        else if (found == ALPreprocessor::Found::Yes)
        {
            config.path = c.path;
            out.push_back(std::move(config));
        }
    }
    if (pending)
    {
        return ALPreprocessor::Found::Pending;
    }
    // On disk, a folder with both is passed over, as Luau's analysis
    // passes it over; and a `.config.luau` that did not run, as a
    // `.luaurc` that does not parse is.
    for (const DiskConfig& one : disk)
    {
        ALRequirePlaces::Config read;
        if (readConfig(one, read) && !read.ambiguous && !read.text.empty())
        {
            out.push_back({ ALIncludeIdentity::ofFile(one.luaurc.empty() ? one.luau : one.luaurc), std::move(read.text) });
        }
    }
    return out.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
}

ALIncludeSearch::DiskConfig ALIncludeSearch::configIn(const std::string& dir)
{
    const std::string luaurc = fsyspath(fsyspath(dir) / fsyspath(CONFIG_NAME)).string();
    const std::string luau   = fsyspath(fsyspath(dir) / fsyspath(ALLuauConfigScript::NAME)).string();
    return { dir, isFile(luaurc) ? luaurc : std::string(), isFile(luau) ? luau : std::string() };
}

std::vector<ALIncludeSearch::DiskConfig> ALIncludeSearch::configsUp(const std::string& dir, const Where& where)
{
    // Each name's from the cache, a folder's two together, the nearest --
    // the longest of the folders above one -- first. A copy of the first:
    // asking the second may move what the cache keeps.
    const std::vector<std::string> luaurcs = mDisk.upwards(dir, CONFIG_NAME, where.generation, where.now);
    const std::vector<std::string>& luaus  = mDisk.upwards(dir, ALLuauConfigScript::NAME, where.generation, where.now);
    std::vector<DiskConfig>         out;
    const auto                      in = [&out](const std::string& folder) -> DiskConfig& {
        for (DiskConfig& one : out)
        {
            if (one.dir == folder)
            {
                return one;
            }
        }
        out.push_back({ folder, std::string(), std::string() });
        return out.back();
    };
    for (const std::string& file : luaurcs)
    {
        in(dirOf(file)).luaurc = file;
    }
    for (const std::string& file : luaus)
    {
        in(dirOf(file)).luau = file;
    }
    std::stable_sort(out.begin(), out.end(), [](const DiskConfig& a, const DiskConfig& b) { return a.dir.size() > b.dir.size(); });
    return out;
}

std::vector<ALIncludeSearch::DiskConfig> ALIncludeSearch::configsAtTop(const Where& where)
{
    ALDiskCache::Blessed&   own = ownFolders(where);
    std::vector<DiskConfig> out;
    for (const std::string& folder : own.includes.folders())
    {
        DiskConfig one;
        one.luaurc = mDisk.admits(own, fsyspath(fsyspath(folder) / fsyspath(CONFIG_NAME)).string()).value_or(std::string());
        one.luau   = mDisk.admits(own, fsyspath(fsyspath(folder) / fsyspath(ALLuauConfigScript::NAME)).string()).value_or(std::string());
        if (!one.luaurc.empty() || !one.luau.empty())
        {
            one.dir = dirOf(one.luaurc.empty() ? one.luau : one.luaurc);
            out.push_back(std::move(one));
        }
    }
    return out;
}

bool ALIncludeSearch::readConfig(const DiskConfig& config, ALRequirePlaces::Config& out)
{
    if (!config.luaurc.empty() && !config.luau.empty())
    {
        out.ambiguous = true;
        return true;
    }
    if (!config.luaurc.empty())
    {
        return mDisk.read(config.luaurc, out.text);
    }
    std::string source;
    if (config.luau.empty() || !mDisk.read(config.luau, source))
    {
        return false;
    }
    if (!ALLuauConfigScript::asLuaurc(source, out.text, out.error))
    {
        out.text.clear();
    }
    return true;
}

ALPreprocessor::Found ALIncludeSearch::resolve(const ALPreprocessor::Ask& ask_in, ALPreprocessor::Include& out, const Asking& asking,
                                               const Where& where, wanted_t* wanted, bool retry, std::vector<std::string>* alias_folders)
{
    ALPreprocessor::Ask ask = ask_in;
    if (ask.from.empty())
    {
        // The script itself asking: a relative name is taken from where
        // it is.
        ask.from = asking.self;
    }
    if (asking.lua && ask.require)
    {
        // A SLua require: walked as the plugin's rules have it (LAD9), with
        // what an alias of a `.luaurc` on disk reaches blessed for the run.
        std::vector<std::string> own_folders;
        return resolveRequire(ask, out, asking, where, wanted, retry, alias_folders ? *alias_folders : own_folders);
    }
    bool                         unknown    = false;
    const std::vector<Candidate> candidates = candidatesFor(ask, asking, where, alias_folders ? *alias_folders : std::vector<std::string>(), unknown);
    for (const Candidate& c : candidates)
    {
        const ALPreprocessor::Found found = textOf(c, wanted, retry, out.text, out.assetId);
        if (found == ALPreprocessor::Found::No)
        {
            continue;
        }
        // Named where it is found, whether its text is in hand or not: a
        // run takes the text, a question the name alone.
        out.name = c.name;
        out.path = c.path;
        return found;
    }
    // Not found anywhere listed; the object may still hold it.
    return unknown ? ALPreprocessor::Found::Pending : ALPreprocessor::Found::No;
}

ALPreprocessor::Found ALIncludeSearch::resolveRequire(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Asking& asking,
                                                      const Where& where, wanted_t* wanted, bool retry, std::vector<std::string>& alias_folders)
{
    Places                            places(*this, where, wanted, retry, alias_folders);
    const ALRequireNavigation::Walked walked = ALRequireNavigation::walk(places, ask.from, ask.name);
    if (walked.found == ALRequirePlaces::Known::Pending)
    {
        return ALPreprocessor::Found::Pending;
    }
    for (const ALRequirePlaces::File& file : walked.files)
    {
        Candidate c{ file.path, file.name, file.assetId, std::string() };
        if (!file.file.empty())
        {
            const std::optional<std::string> real = requireAdmits(ask, asking, where, alias_folders, file.file);
            if (!real)
            {
                continue;
            }
            c = admitted(*real);
        }
        const ALPreprocessor::Found found = textOf(c, wanted, retry, out.text, out.assetId);
        if (found == ALPreprocessor::Found::No)
        {
            continue;
        }
        out.name = c.name;
        out.path = c.path;
        if (walked.passedOver)
        {
            out.passedOver = walked.passedOver->file.empty() ? walked.passedOver->name : walked.passedOver->file;
        }
        return found;
    }
    out.why = walked.error;
    searchedBefore(ask, asking, where, alias_folders, out);
    return ALPreprocessor::Found::No;
}

std::optional<std::string> ALIncludeSearch::requireAdmits(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                                          const std::vector<std::string>& alias_folders, const std::string& file)
{
    std::vector<std::string> blessing = alias_folders;
    for (const auto& [name, folder] : where.aliases)
    {
        blessing.push_back(folder);
    }
    return mDisk.admits(blessedFor(ask, asking, where, blessing), file);
}

void ALIncludeSearch::searchedBefore(const ALPreprocessor::Ask& ask, const Asking& asking, const Where& where,
                                     std::vector<std::string>& alias_folders, ALPreprocessor::Include& out)
{
    // Only a name the search took: not an alias's, nor a path from a root.
    if (ask.name.empty() || ask.name.front() == '@' || ALLuauConfig::absolute(ask.name))
    {
        return;
    }
    bool                         unknown = false;
    const std::vector<Candidate> found   = candidatesFor(ask, asking, where, alias_folders, unknown);
    if (found.empty())
    {
        return;
    }
    const Candidate& was = found.front();
    out.searched         = was.file.empty() ? was.name : was.file;
    if (was.file.empty())
    {
        // In the world: said, and nothing offered to write instead.
        return;
    }
    // A name under a folder, as a require writes it: its path from there,
    // `/` between the parts, a script's extension taken off and a folder's
    // init as the folder -- and as written, extension and all, should that
    // be the one that walks to it.
    const auto under = [&was](const std::string& folder) {
        std::vector<std::string>    names;
        std::error_code             ec;
        const std::filesystem::path real     = std::filesystem::weakly_canonical(fsyspath(folder), ec);
        const std::filesystem::path relative = fsyspath(was.file).lexically_relative(ec ? fsyspath(folder) : real);
        std::string                 path     = relative.generic_string();
        if (path.empty() || path == "." || path.compare(0, 2, "..") == 0)
        {
            return names;
        }
        std::string module = stemOf(path);
        if (module == "init")
        {
            return names;
        }
        if (module.size() > 5 && module.compare(module.size() - 5, 5, "/init") == 0)
        {
            module.erase(module.size() - 5);
        }
        names.push_back(module);
        names.push_back(path);
        return names;
    };
    // Whether a require so written walks to the very file the search found.
    const auto walks = [&](const std::string& name, const Where& in) {
        std::vector<std::string>          blessed = alias_folders;
        Places                            places(*this, in, nullptr, false, blessed);
        const ALRequireNavigation::Walked walked = ALRequireNavigation::walk(places, ask.from, name);
        for (const ALRequirePlaces::File& file : walked.files)
        {
            const std::optional<std::string> real = file.file.empty() ? std::nullopt : requireAdmits(ask, asking, in, blessed, file.file);
            if (real && *real == was.file)
            {
                return true;
            }
        }
        return false;
    };
    const auto offer = [&out](const std::string& require, const std::string& alias = std::string(), const std::string& folder = std::string()) {
        if (std::none_of(out.moves.begin(), out.moves.end(), [&require](const ALPreprocessor::Include::Move& move) { return move.require == require; }))
        {
            out.moves.push_back({ require, alias, folder });
        }
    };
    // From beside the file asking.
    std::string from_file;
    if (ALIncludeIdentity::fileOf(ask.from, from_file))
    {
        for (const std::string& name : under(fsyspath(fsyspath(from_file).parent_path()).string()))
        {
            if (walks("./" + name, where))
            {
                offer("./" + name);
                break;
            }
        }
    }
    // Through an alias that reaches it already: a `.luaurc`'s, or the
    // studio's own.
    std::vector<std::pair<std::string, std::string>> aliases;
    for (const auto& [prefix, folder] : moduleFolders(asking, where))
    {
        if (!prefix.empty())
        {
            aliases.emplace_back(prefix.substr(1, prefix.size() - 2), folder);
        }
    }
    aliases.insert(aliases.end(), where.aliases.begin(), where.aliases.end());
    for (const auto& [alias, folder] : aliases)
    {
        for (const std::string& name : under(folder))
        {
            if (walks("@" + alias + "/" + name, where))
            {
                offer("@" + alias + "/" + name);
                break;
            }
        }
    }
    if (!out.moves.empty())
    {
        return;
    }
    // Otherwise the include folder that holds it, named a studio alias
    // after itself.
    std::vector<std::string> taken;
    for (const auto& [alias, folder] : where.aliases)
    {
        taken.push_back(alias);
    }
    for (const std::string& folder : where.folders)
    {
        const std::vector<std::string> names = under(folder);
        if (names.empty())
        {
            continue;
        }
        std::filesystem::path at = fsyspath(folder);
        if (!at.has_filename())
        {
            at = at.parent_path();
        }
        const std::string alias = ALLuauConfig::studioAliasFor(fsyspath(at.filename()).string(), taken);
        Where             named = where;
        named.aliases.emplace_back(alias, folder);
        for (const std::string& name : names)
        {
            if (walks("@" + alias + "/" + name, named))
            {
                offer("@" + alias + "/" + name, alias, folder);
                return;
            }
        }
    }
}

std::vector<ALRequireNavigation::Suggestion> ALIncludeSearch::suggest(const std::string& from, const std::string& typed, bool require,
                                                                     const Asking& asking, const Where& where)
{
    if (require && asking.lua)
    {
        std::vector<std::string> alias_folders;
        Places                   places(*this, where, nullptr, false, alias_folders);
        return ALRequireNavigation::suggest(places, from.empty() ? asking.self : from, typed);
    }
    // An include: the names under the folders its search looks in, the
    // file's own first, each with its extension, and the folders.
    std::vector<ALRequireNavigation::Suggestion> out;
    if (!where.disk)
    {
        return out;
    }
    const size_t      slash = typed.find_last_of("/\\");
    const std::string head  = slash == std::string::npos ? std::string() : typed.substr(0, slash + 1);
    std::vector<std::string> dirs;
    std::string              file;
    if (ALIncludeIdentity::fileOf(from.empty() ? asking.self : from, file))
    {
        dirs.push_back(fsyspath(fsyspath(file).parent_path()).string());
    }
    for (const std::string& folder : ownFolders(where).includes.folders())
    {
        if (std::find(dirs.begin(), dirs.end(), folder) == dirs.end())
        {
            dirs.push_back(folder);
        }
    }
    std::vector<ALRequirePlaces::Child> found;
    for (const std::string& dir : dirs)
    {
        const std::vector<std::string_view> extensions = asking.lua ? std::vector<std::string_view>{ ".luau", ".lua" } : std::vector<std::string_view>{ ".lsl" };
        listFolder(head.empty() ? dir : fsyspath(fsyspath(dir) / fsyspath(head)).string(), extensions, false, found);
    }
    std::sort(found.begin(), found.end(), [](const ALRequirePlaces::Child& a, const ALRequirePlaces::Child& b) { return a.name < b.name; });
    found.erase(std::unique(found.begin(), found.end(), [](const ALRequirePlaces::Child& a, const ALRequirePlaces::Child& b) { return a.name == b.name; }),
                found.end());
    for (const ALRequirePlaces::Child& child : found)
    {
        out.push_back({ child.name, head + child.name, child.folder });
    }
    return out;
}

ALIncludeSearch::Candidate ALIncludeSearch::admitted(const std::string& real)
{
    Candidate    c;
    const size_t slash = real.find_last_of("/\\");
    c.name             = slash == std::string::npos ? real : real.substr(slash + 1);
    c.path             = ALIncludeIdentity::ofFile(real);
    c.file             = real;
    // Past a few thousand, the oldest admissions let go of all at once:
    // each is made again by the next run over what names it.
    if (mAdmitted.size() >= ADMITTED_KEPT && !mAdmitted.contains(c.path))
    {
        mAdmitted.clear();
    }
    mAdmitted.insert(c.path);
    return c;
}

ALPreprocessor::Found ALIncludeSearch::lookUp(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Asking& asking, const Where& where)
{
    // Where an alias of a `.luaurc` on disk points is blessed for the
    // asking, as it is for a run.
    std::vector<std::string> alias_folders;
    return resolve(ask, out, asking, where, nullptr, false, &alias_folders);
}

std::vector<ALPreprocessor::Include> ALIncludeSearch::includedBy(const std::string& text, const Asking& asking, const Where& where)
{
    // What a file asks for: its include lines -- `#include`, `--#include`
    // in SLua -- and a SLua script's require calls.
    const auto asks_in = [&asking](const std::string& text, const std::string& from) {
        std::vector<ALPreprocessor::Ask> asks;
        for (size_t at = 0; at < text.size();)
        {
            const size_t     nl   = text.find('\n', at);
            std::string_view line = std::string_view(text).substr(at, nl == std::string::npos ? std::string::npos : nl - at);
            at                    = nl == std::string::npos ? text.size() : nl + 1;
            const size_t name     = ALPreprocessor::directiveName(line, asking.lua);
            if (name == std::string_view::npos)
            {
                continue;
            }
            line.remove_prefix(name);
            if (line.substr(0, 7) != "include")
            {
                continue;
            }
            line.remove_prefix(7);
            line.remove_prefix(std::min(line.size(), line.find_first_not_of(" \t")));
            const char   open  = line.empty() ? '\0' : line.front();
            const size_t close = open == '"' ? line.find('"', 1) : open == '<' ? line.find('>', 1) : std::string_view::npos;
            if (close == std::string_view::npos)
            {
                continue;
            }
            ALPreprocessor::Ask ask;
            ask.name   = std::string(line.substr(1, close - 1));
            ask.angled = open == '<';
            ask.from   = from;
            asks.push_back(std::move(ask));
        }
        if (asking.lua)
        {
            for (const ALPreprocessor::Required& required : ALPreprocessor::requiresIn(text))
            {
                ALPreprocessor::Ask ask;
                ask.name    = required.name;
                ask.require = true;
                ask.from    = from;
                asks.push_back(std::move(ask));
            }
        }
        return asks;
    };
    std::vector<ALPreprocessor::Include>             out;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    std::vector<std::pair<std::string, std::string>> todo{ { text, asking.self } };
    for (size_t next = 0; next < todo.size() && out.size() < 256; ++next)
    {
        const std::string file = todo[next].first;
        const std::string from = todo[next].second;
        for (const ALPreprocessor::Ask& ask : asks_in(file, from))
        {
            ALPreprocessor::Include found;
            if (lookUp(ask, found, asking, where) != ALPreprocessor::Found::Yes || found.path.empty() || !seen.insert(found.path).second)
            {
                continue;
            }
            todo.emplace_back(found.text, found.path);
            out.push_back(std::move(found));
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ALIncludeSearch::moduleFolders(const Asking& asking, const Where& where)
{
    std::vector<std::pair<std::string, std::string>> out;
    // Nothing on disk while the disk is off: not the scripter's folders,
    // nor what a configuration on disk lists.
    if (!where.disk)
    {
        return out;
    }
    const ALDiskIncludes own = ownFolders(where).includes;
    for (const std::string& folder : where.folders)
    {
        out.emplace_back(std::string(), folder);
        if (!asking.lua)
        {
            for (const std::string& listed : ALDiskIncludes::lslrcFolders(folder))
            {
                if (own.mayFromConfig(listed, folder))
                {
                    out.emplace_back(std::string(), listed);
                }
            }
        }
    }
    std::string file;
    if (!asking.lua)
    {
        // The nearest `.lslrc` up from a script that is itself on disk.
        if (ALIncludeIdentity::fileOf(asking.self, file))
        {
            std::string config_folder;
            for (const std::string& listed : ALDiskIncludes::nearestLslrcFolders(dirOf(file), &config_folder))
            {
                if (own.mayFromConfig(listed, config_folder))
                {
                    out.emplace_back(std::string(), listed);
                }
            }
        }
        return out;
    }
    // Each alias of the `.luaurc` files that govern the script, the
    // nearest saying first, where that one is a file on disk: the path it
    // stands for, from beside the file. One in the world names nothing on
    // disk, and hides the same alias further up all the same.
    std::vector<Config> configs;
    if (configsFor(asking.self, asking, where, nullptr, false, configs) != ALPreprocessor::Found::Yes)
    {
        return out;
    }
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> said;
    for (const Config& config : configs)
    {
        ALLuauConfig parsed;
        std::string  error, config_file;
        if (!ALLuauConfig::parse(config.text, parsed, error))
        {
            continue;
        }
        const bool on_disk = ALIncludeIdentity::fileOf(config.path, config_file);
        for (const auto& [alias, path] : parsed.aliases)
        {
            if (!said.insert(alias).second || !on_disk)
            {
                continue;
            }
            std::string value = path;
            while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
            {
                value.pop_back();
            }
            const std::string folder = ALLuauConfig::absolute(value) ? value : joined(dirOf(config_file), value);
            if (own.mayFromConfig(folder, dirOf(config_file)))
            {
                out.emplace_back("@" + alias + "/", folder);
            }
        }
    }
    return out;
}

bool ALIncludeSearch::configOf(const Asking& asking, const Where& where, ALLuauConfig& out, const ALLuauConfig* base)
{
    out = base ? *base : ALLuauConfig();
    if (!asking.lua)
    {
        return false;
    }
    std::vector<Config> configs;
    if (configsFor(asking.self, asking, where, nullptr, /*retry*/ false, configs) != ALPreprocessor::Found::Yes)
    {
        return false;
    }
    // As Luau reads a chain: what a nearer one says wins, and globals add
    // up.
    std::vector<std::string_view> texts;
    for (const Config& one : configs)
    {
        texts.push_back(one.text);
    }
    return ALLuauConfig::parseChain(texts, out, base);
}

namespace
{
    // A module found nowhere, however it was said: as it was, why Luau's
    // navigator found nothing, or with a reason of no known shape.
    bool moduleMissing(const ALScriptProblem& problem)
    {
        return !problem.args.empty() &&
               (problem.key == "PreprocModuleNotFound" || problem.key == "PreprocModuleNotFoundWhy" || ALMessageMap::requireReason(problem.key));
    }
}

// static
void ALIncludeSearch::explainMissing(ALScriptProblems& problems, const Missing& facts)
{
    // What could not be found may be in the object that never said what
    // it holds: said, ahead of the names it would have answered, so that
    // a save stopped for them says why.
    if (facts.objectUnanswered)
    {
        const bool missing = std::any_of(problems.begin(), problems.end(), [](const ALScriptProblem& p) {
            return p.key == "PreprocIncludeNotFound" || moduleMissing(p);
        });
        if (missing)
        {
            ALScriptProblem unanswered;
            unanswered.severity = ALScriptProblem::Severity::Error;
            unanswered.key      = "PreprocObjectUnanswered";
            unanswered.message  = "the object this script is in did not say what it holds -- it did not answer in time, or is out of view -- so "
                                  "no include was looked for in it; save again once it answers";
            problems.insert(problems.begin(), unanswered);
        }
    }
    // What could not be found where includes are not taken from the world,
    // with one so named in the object or the inventory: said so, and where
    // it is let in, since that is what a script saved by somebody who
    // took its includes from their inventory runs into.
    if (!facts.world)
    {
        for (ALScriptProblem& problem : problems)
        {
            const bool include = problem.key == "PreprocIncludeNotFound" && problem.args.size() == 1;
            if ((!include && !moduleMissing(problem)) || !facts.inWorld || !facts.inWorld(problem.args[0]))
            {
                continue;
            }
            problem.args.resize(1);
            problem.key     = include ? "PreprocIncludeInWorld" : "PreprocModuleInWorld";
            problem.message = ALScriptProblem::fill(include ? "could not find include file '[1]': one so named is in the object or the inventory, but "
                                                              "includes from there are off. Includes come from folders on disk: turn on Build > "
                                                              "Include from Disk, and add the folder with Build > Add Include Folder..."
                                                            : "could not find module '[1]': one so named is in the object or the inventory, but "
                                                              "modules from there are off. Modules come from folders on disk: turn on Build > "
                                                              "Include from Disk, and add the folder with Build > Add Include Folder...",
                                                    problem.args);
        }
    }
    // What could not be found where the disk was not looked in -- disk
    // includes off, or on with no folder of the scripter's to look in --
    // said so, and how it is: a module kept in a folder on disk, as the VS
    // Code plugin keeps them, is what a scripter new to the studio runs
    // into. Said from the settings alone: nothing on the disk is touched
    // to say it. A file on disk asking may have folders a `.luaurc` beside
    // it let in, and is not second-guessed.
    std::string file;
    for (ALScriptProblem& problem : problems)
    {
        const bool include = problem.key == "PreprocIncludeNotFound" && problem.args.size() == 1;
        if (!include && !moduleMissing(problem))
        {
            continue;
        }
        const bool from_disk = facts.fromDisk || ALIncludeIdentity::fileOf(problem.file, file);
        if (facts.disk && (!facts.noFolders || from_disk))
        {
            continue;
        }
        problem.args.resize(1);
        problem.key     = include ? "PreprocIncludeNotOnDisk" : "PreprocModuleNotOnDisk";
        problem.message = ALScriptProblem::fill(include ? "could not find include file '[1]': the disk was not looked in. Includes come from "
                                                          "folders on disk: turn on Build > Include from Disk, and add the folder with Build > "
                                                          "Add Include Folder..."
                                                        : "could not find module '[1]': the disk was not looked in. Modules come from folders on "
                                                          "disk: turn on Build > Include from Disk, and add the folder with Build > Add Include "
                                                          "Folder...",
                                                problem.args);
    }
}

// static
ALIncludeSearch::LeftOut ALIncludeSearch::leftOut(const ALScriptProblems& problems)
{
    LeftOut out;
    for (const ALScriptProblem& problem : problems)
    {
        const bool in_world = problem.key == "PreprocIncludeInWorld" || problem.key == "PreprocModuleInWorld";
        const bool off_disk = problem.key == "PreprocIncludeNotOnDisk" || problem.key == "PreprocModuleNotOnDisk";
        const bool missing  = (problem.key == "PreprocIncludeNotFound" && problem.args.size() == 1) || moduleMissing(problem) ||
                             problem.key == "PreprocModuleSearched";
        if ((!in_world && !off_disk && !missing) || problem.args.empty())
        {
            continue;
        }
        ++out.problems;
        out.diskRoute = out.diskRoute || in_world || off_disk;
        if (std::find(out.names.begin(), out.names.end(), problem.args[0]) == out.names.end())
        {
            out.names.push_back(problem.args[0]);
        }
    }
    return out;
}

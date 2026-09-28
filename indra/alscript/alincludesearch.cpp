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

#include <algorithm>

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
                    Candidate c;
                    const size_t slash = real->find_last_of("/\\");
                    c.name             = slash == std::string::npos ? *real : real->substr(slash + 1);
                    c.path             = ALIncludeIdentity::ofFile(*real);
                    c.file             = *real;
                    // Past a few thousand, the oldest admissions let go of
                    // all at once: each is made again by the next run over
                    // what names it.
                    if (mAdmitted.size() >= ADMITTED_KEPT && !mAdmitted.contains(c.path))
                    {
                        mAdmitted.clear();
                    }
                    mAdmitted.insert(c.path);
                    out.push_back(std::move(c));
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
    static const std::string CONFIG_NAME(".luaurc");
    out.clear();
    std::vector<Candidate> chain;
    std::string            file;
    // A configuration in the world only where the world is let in: an
    // object is not asked what it holds otherwise, and would never say.
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
        for (const std::string& config : mDisk.upwards(dirOf(file), CONFIG_NAME, where.generation, where.now))
        {
            chain.push_back({ ALIncludeIdentity::ofFile(config), CONFIG_NAME, LLUUID::null, config });
        }
    }
    if (in_world)
    {
        // Above whatever the world has, the one at the top of each of the
        // scripter's include folders, while the disk is read: a script in
        // the world has no folders on disk to look up through, and its
        // scripter's modules are read from those.
        for (const std::string& top : mDisk.atTop(ownFolders(where), CONFIG_NAME))
        {
            chain.push_back({ ALIncludeIdentity::ofFile(top), CONFIG_NAME, LLUUID::null, top });
        }
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
    return out.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
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
    std::string alias, rest;
    if (asking.lua && ask.require && ALLuauConfig::aliasOf(ask.name, alias, rest))
    {
        // Through the nearest `.luaurc` over the asking file that says
        // what the alias is, as Luau reads a chain of them: the alias's
        // path from beside that configuration, so the name is asked for
        // from there.
        std::vector<Config>         configs;
        const ALPreprocessor::Found config = configsFor(ask.from, asking, where, wanted, retry, configs);
        if (config != ALPreprocessor::Found::Yes)
        {
            return config;
        }
        std::vector<std::string_view> texts;
        for (const Config& one : configs)
        {
            texts.push_back(one.text);
        }
        std::string                 value;
        const std::optional<size_t> saying = ALLuauConfig::aliasIn(texts, alias, value);
        if (!saying)
        {
            return ALPreprocessor::Found::No;
        }
        const std::string config_path = configs[*saying].path;
        if (!ALLuauConfig::absolute(value) && value.compare(0, 2, "./") != 0 && value.compare(0, 3, "../") != 0)
        {
            value = "./" + value;
        }
        while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
        {
            value.pop_back();
        }
        ask.name = rest.empty() ? value : value + "/" + rest;
        ask.from = config_path;
        // A `.luaurc` on disk blesses where its aliases point, for this
        // run; one in the world blesses nothing, and its alias is only
        // ever a name to look for in the world. Only while the disk is
        // read, and only where a configuration may reach
        // (ALDiskIncludes::mayFromConfig).
        std::string config_file;
        if (alias_folders && where.disk && ALIncludeIdentity::fileOf(config_path, config_file))
        {
            const std::string folder = ALLuauConfig::absolute(value) ? value : joined(dirOf(config_file), value);
            if (std::find(alias_folders->begin(), alias_folders->end(), folder) == alias_folders->end())
            {
                ALDiskIncludes own = ownFolders(where).includes;
                if (own.blessFromConfig(folder, dirOf(config_file)))
                {
                    alias_folders->push_back(folder);
                }
            }
        }
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
    boost::unordered_flat_set<std::string>           seen;
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

// static
void ALIncludeSearch::explainMissing(ALScriptProblems& problems, const Missing& facts)
{
    // What could not be found may be in the object that never said what
    // it holds: said, ahead of the names it would have answered, so that
    // a save stopped for them says why.
    if (facts.objectUnanswered)
    {
        const bool missing = std::any_of(problems.begin(), problems.end(), [](const ALScriptProblem& p) {
            return p.key == "PreprocIncludeNotFound" || p.key == "PreprocModuleNotFound";
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
            const bool include = problem.key == "PreprocIncludeNotFound";
            if ((!include && problem.key != "PreprocModuleNotFound") || problem.args.size() != 1 || !facts.inWorld || !facts.inWorld(problem.args[0]))
            {
                continue;
            }
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
        const bool include = problem.key == "PreprocIncludeNotFound";
        if ((!include && problem.key != "PreprocModuleNotFound") || problem.args.size() != 1)
        {
            continue;
        }
        const bool from_disk = facts.fromDisk || ALIncludeIdentity::fileOf(problem.file, file);
        if (facts.disk && (!facts.noFolders || from_disk))
        {
            continue;
        }
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
        if ((!in_world && !off_disk && problem.key != "PreprocIncludeNotFound" && problem.key != "PreprocModuleNotFound") || problem.args.size() != 1)
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

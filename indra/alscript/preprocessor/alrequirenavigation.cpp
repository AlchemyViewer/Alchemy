/**
 * @file alrequirenavigation.cpp
 * @brief A SLua require walked by Luau's own navigator, over the places a script's modules may be.
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

#include "alrequirenavigation.h"

#include "aldiskincludes.h"
#include "alluauconfig.h"

#include "Luau/FileResolver.h"
#include "Luau/RequireNavigator.h"

#include <algorithm>

namespace
{
    typedef ALRequirePlaces::Known Known;
    typedef ALRequirePlaces::File  File;

    // What an alias stands for in the configurations the navigator is
    // given: one of ours, by its number, which it hands back to
    // jumpToAlias as it is -- no `./`, `../` or `@` in front.
    constexpr std::string_view TARGET = "alias-target:";

    bool endsWithScript(const std::string& name)
    {
        return ALDiskIncludes::extensionOf(name, ALDiskIncludes::scriptExtensions(true)) != 0;
    }

    std::string jsonString(const std::string& text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }

    std::vector<std::string> componentsOf(const std::string& path)
    {
        std::vector<std::string> out;
        size_t                   from = 0;
        while (from <= path.size())
        {
            const size_t slash = path.find('/', from);
            const size_t end   = slash == std::string::npos ? path.size() : slash;
            out.push_back(path.substr(from, end - from));
            if (slash == std::string::npos)
            {
                break;
            }
            from = slash + 1;
        }
        return out;
    }

    // Where a walk stands: a folder, or a name in one -- a module, or a
    // folder walked on through -- as Luau's own navigator stands at a
    // module's path.
    struct Position
    {
        std::string                folder;
        std::optional<std::string> name;
    };

    // Luau's navigator's view of our places. The file asking stands at its
    // own name in its folder, a folder's init too, so that `./` is beside
    // it and the `.luaurc` beside it is the first read (LAD2). An alias
    // goes through the nearest configuration that names it, as the
    // navigator walks up; what one stands for, where it is a path, comes
    // back here to be walked from that configuration's folder -- and on
    // disk, blessed there for the run.
    class Context final : public Luau::Require::NavigationContext
    {
    public:
        Context(ALRequirePlaces& places, Position requirer)
        :   mPlaces(places),
            mRequirer(std::move(requirer)),
            mAt(mRequirer)
        {
        }

        NavigateResult resetToRequirer() override
        {
            mAt = mRequirer;
            return NavigateResult::Success;
        }

        NavigateResult toParent() override
        {
            if (mAt.name)
            {
                mAt.name.reset();
                return NavigateResult::Success;
            }
            std::string up;
            if (!known(mPlaces.parentOf(mAt.folder, up)))
            {
                return NavigateResult::NotFound;
            }
            mAt = Position{ up, std::nullopt };
            return NavigateResult::Success;
        }

        NavigateResult toChild(const std::string& component) override
        {
            std::string folder;
            if (!folderOf(mAt, folder))
            {
                return NavigateResult::NotFound;
            }
            // There, as a file or as a folder.
            std::vector<File> files;
            std::string       sub;
            const bool        file   = known(mPlaces.files(folder, component, files));
            const bool        within = known(mPlaces.subfolder(folder, component, sub));
            if (!file && !within)
            {
                return NavigateResult::NotFound;
            }
            mAt = Position{ folder, component };
            return NavigateResult::Success;
        }

        // An alias no configuration names: Script Studio's own, a folder
        // on disk the scripter named.
        NavigateResult toAliasFallback(const std::string& alias_in) override
        {
            std::string alias = alias_in;
            std::transform(alias.begin(), alias.end(), alias.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string folder;
            if (!known(mPlaces.studioAlias(alias, folder)))
            {
                return NavigateResult::NotFound;
            }
            mAt = Position{ folder, std::nullopt };
            return NavigateResult::Success;
        }

        ConfigStatus getConfigStatus() const override
        {
            if (mAt.name)
            {
                return ConfigStatus::Absent;
            }
            // A `.config.luau` comes as the `.luaurc` it says, which is all
            // the navigator is given: it never runs one itself (LA19).
            mConfig = ALRequirePlaces::Config();
            if (!known(mPlaces.config(mAt.folder, mConfig)))
            {
                return ConfigStatus::Absent;
            }
            return mConfig.ambiguous ? ConfigStatus::Ambiguous : ConfigStatus::PresentJson;
        }

        ConfigBehavior getConfigBehavior() const override { return ConfigBehavior::GetConfig; }

        // The configuration's aliases, as our own parse read it -- which
        // knows the studio's lints, Luau's does not -- each path one of
        // ours to walk, and each that names another alias as it is, for
        // the navigator to follow; none reserved, which no configuration
        // may name. One that does not parse, or did not run, ends the walk
        // there, as it ends Luau's navigator's, and says why.
        std::optional<std::string> getConfig() const override
        {
            if (!mConfig.error.empty())
            {
                mMissed = "the configuration '" + mConfig.file + "' could not be read: " + mConfig.error;
                return std::nullopt;
            }
            std::string json = "{\"aliases\": {";
            bool        first = true;
            for (const auto& [alias, value] : mConfig.aliases)
            {
                if (ALLuauConfig::reservedAlias(alias))
                {
                    continue;
                }
                std::string said = value;
                if (said.empty() || said.front() != '@')
                {
                    said = std::string(TARGET) + std::to_string(mTargets.size());
                    mTargets.push_back({ mConfig.base, value, mConfig.onDisk, mConfig.file });
                }
                json += (first ? "" : ", ") + jsonString(alias) + ": " + jsonString(said);
                first = false;
            }
            return json + "}}";
        }

        NavigateResult jumpToAlias(const std::string& path) override
        {
            if (path.compare(0, TARGET.size(), TARGET) != 0)
            {
                return NavigateResult::NotFound;
            }
            const size_t index = std::strtoul(path.c_str() + TARGET.size(), nullptr, 10);
            if (index >= mTargets.size())
            {
                return NavigateResult::NotFound;
            }
            const Target target = mTargets[index];
            mMissed             = "the alias stands for '" + target.value + "', which is not there";
            std::string  value  = target.value;
            std::replace(value.begin(), value.end(), '\\', '/');
            while (value.size() > 1 && value.back() == '/')
            {
                value.pop_back();
            }
            if (ALLuauConfig::absolute(value))
            {
                // A path from a root: on disk, and only from a configuration
                // on disk; one in the world names only the world.
                if (!target.onDisk)
                {
                    mMissed = "the alias stands for '" + target.value + "', a path on disk, which a configuration in the world may not name";
                    return NavigateResult::NotFound;
                }
                std::string folder, name;
                if (!mPlaces.placeOfAbsolute(value, target.base, folder, name))
                {
                    // Out of every folder it may name, as it is written --
                    // said so, and nothing asked of the disk to say it.
                    const size_t slash = target.file.find_last_of("/\\");
                    mMissed = "the configuration in '" + (slash == std::string::npos ? target.file : target.file.substr(0, slash)) + "' names '" +
                              target.value + "', which is outside it and outside the include folders";
                    return NavigateResult::NotFound;
                }
                mAt = Position{ folder, name.empty() ? std::nullopt : std::optional<std::string>(name) };
            }
            else
            {
                mAt = Position{ target.base, std::nullopt };
                for (const std::string& component : componentsOf(value))
                {
                    if (component.empty() || component == ".")
                    {
                        continue;
                    }
                    if ((component == ".." ? toParent() : toChild(component)) != NavigateResult::Success)
                    {
                        return NavigateResult::NotFound;
                    }
                }
            }
            if (target.onDisk)
            {
                mPlaces.aliasReached(target.base, mAt.folder, mAt.name.value_or(std::string()));
            }
            mMissed.clear();
            return NavigateResult::Success;
        }
        // Why the walk stopped, where Luau would name it by our own word for
        // it: the last alias it went through stood for nothing there, or a
        // configuration on its way could not be read.
        const std::string& missed() const { return mMissed; }

        const Position& at() const { return mAt; }
        bool            pending() const { return mPending; }

        // The folder a position's children are in: the folder itself, or
        // the folder of the name.
        bool folderOf(const Position& at, std::string& out) const
        {
            if (!at.name)
            {
                out = at.folder;
                return true;
            }
            return known(mPlaces.subfolder(at.folder, *at.name, out));
        }
        // Whether a place said yes; one not known yet noted, and no.
        bool known(Known said) const
        {
            mPending = mPending || said == Known::Pending;
            return said == Known::Yes;
        }

    private:
        // What an alias of a configuration stands for, from its folder; and
        // the configuration's file, for what is said of it.
        struct Target
        {
            std::string base;
            std::string value;
            bool        onDisk = false;
            std::string file;
        };

        ALRequirePlaces&                mPlaces;
        Position                        mRequirer;
        Position                        mAt;
        mutable bool                    mPending = false;
        mutable ALRequirePlaces::Config mConfig;
        mutable std::vector<Target>     mTargets;
        mutable std::string             mMissed;
    };

    // Luau's words for what went wrong, the last it said.
    struct Said final : public Luau::Require::ErrorHandler
    {
        std::string what;
        void        reportError(std::string message) override { what = std::move(message); }
    };
}

namespace
{
    // Where a require's path stops, walked as a require is: none where it
    // goes nowhere.
    std::optional<Position> reach(ALRequirePlaces& places, const std::string& from, const std::string& typed)
    {
        std::string folder, stem;
        const bool  placed = places.placeOf(from, folder, stem);
        const ALRequireNavigation::Path said = ALRequireNavigation::navigatorPath(typed, placed ? stem : std::string());
        if (!said.error.empty())
        {
            return std::nullopt;
        }
        Context                  context(places, Position{ placed ? folder : std::string(), placed ? stem : std::string() });
        Said                     errors;
        Luau::Require::Navigator navigator(context, errors);
        if (navigator.navigate(said.path) != Luau::Require::Navigator::Status::Success)
        {
            return std::nullopt;
        }
        return context.at();
    }

    // A place in the tree Luau's suggester walks: the file asking, or
    // where a path from it stops, or what a folder there holds.
    class Node final : public Luau::RequireNode
    {
    public:
        Node(ALRequirePlaces& places, std::string from, Position at, std::string component, bool folder)
        :   mPlaces(places),
            mFrom(std::move(from)),
            mAt(std::move(at)),
            mComponent(std::move(component)),
            mFolder(folder)
        {
        }

        std::string              getPathComponent() const override { return mComponent; }
        std::vector<std::string> getTags() const override { return mFolder ? std::vector<std::string>{ "folder" } : std::vector<std::string>(); }

        std::unique_ptr<Luau::RequireNode> resolvePathToNode(const std::string& path) const override
        {
            const std::optional<Position> at = reach(mPlaces, mFrom, path);
            if (!at)
            {
                return nullptr;
            }
            return std::make_unique<Node>(mPlaces, mFrom, *at, at->name.value_or(std::string()), true);
        }

        std::vector<std::unique_ptr<Luau::RequireNode>> getChildren() const override
        {
            std::vector<std::unique_ptr<Luau::RequireNode>> out;
            std::string                                     folder;
            if (mAt.name)
            {
                if (mPlaces.subfolder(mAt.folder, *mAt.name, folder) != Known::Yes)
                {
                    return out;
                }
            }
            else
            {
                folder = mAt.folder;
            }
            std::vector<ALRequirePlaces::Child> held;
            mPlaces.children(folder, held);
            for (const ALRequirePlaces::Child& child : held)
            {
                out.push_back(std::make_unique<Node>(mPlaces, mFrom, Position{ folder, child.name }, child.name, child.folder));
            }
            return out;
        }

        std::vector<Luau::RequireAlias> getAvailableAliases() const override
        {
            std::vector<Luau::RequireAlias> out;
            for (const std::string& alias : ALRequireNavigation::aliasesFrom(mPlaces, mFrom))
            {
                out.emplace_back(alias, std::vector<std::string>{ "folder" });
            }
            return out;
        }

    private:
        ALRequirePlaces& mPlaces;
        std::string      mFrom;
        Position         mAt;
        std::string      mComponent;
        bool             mFolder = false;
    };

    // Luau's suggester, over our places, for one file asking.
    class Suggester final : public Luau::RequireSuggester
    {
    public:
        Suggester(ALRequirePlaces& places, std::string from)
        :   mPlaces(places),
            mFrom(std::move(from))
        {
        }

    protected:
        std::unique_ptr<Luau::RequireNode> getNode(const Luau::ModuleName&) const override
        {
            std::string folder, stem;
            const bool  placed = mPlaces.placeOf(mFrom, folder, stem);
            return std::make_unique<Node>(mPlaces, mFrom, Position{ placed ? folder : std::string(), placed ? stem : std::string() }, stem,
                                          false);
        }

    private:
        ALRequirePlaces& mPlaces;
        std::string      mFrom;
    };
}

namespace ALRequireNavigation
{
    std::vector<Suggestion> suggest(ALRequirePlaces& places, const std::string& from, const std::string& typed)
    {
        // Asked of the path up to its last slash: what follows it is the
        // name being typed, which the editor narrows to.
        std::vector<Suggestion> out;
        const size_t            slash = typed.find_last_of('/');
        const std::string       asked = slash == std::string::npos ? typed : typed.substr(0, slash + 1);
        const Suggester         suggester(places, from);
        const std::optional<Luau::RequireSuggestions> found = suggester.getRequireSuggestions(from, asked);
        if (!found)
        {
            return out;
        }
        // Up is offered but through an alias, which may not climb out of
        // its folder (navigatorPath): `@self`'s may, as Luau's own.
        std::string alias, rest;
        const bool  climbs = typed.empty() || typed.front() != '@' || (ALLuauConfig::aliasOf(typed, alias, rest) && alias == "self");
        for (const Luau::RequireSuggestion& one : *found)
        {
            if (one.label == ".." && !climbs)
            {
                continue;
            }
            Suggestion said;
            said.label  = one.label;
            said.path   = one.fullPath;
            said.folder = std::find(one.tags.begin(), one.tags.end(), "folder") != one.tags.end() || one.label == "./" || one.label == "../";
            if (one.label == "..")
            {
                // Up from where the path is: Luau's own gives the folder the
                // path is in, which is where it already is.
                said.path   = (slash == std::string::npos ? std::string() : typed.substr(0, slash + 1)) + "..";
                said.folder = true;
            }
            out.push_back(std::move(said));
        }
        return out;
    }

    Path navigatorPath(const std::string& name_in, const std::string& stem)
    {
        Path        out;
        std::string name = name_in;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.front() != '@')
        {
            // Relative to the file asking, however it is written.
            out.path = name.compare(0, 2, "./") == 0 || name.compare(0, 3, "../") == 0 ? name : "./" + name;
            return out;
        }
        const size_t slash = name.find('/');
        std::string  alias = name.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
        std::transform(alias.begin(), alias.end(), alias.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string rest = slash == std::string::npos ? std::string() : name.substr(slash + 1);
        if (alias == "self")
        {
            // The module's own folder: beside a folder's init, else the
            // folder of the file's own name.
            const std::string own = stem == "init" || stem.empty() ? std::string() : stem;
            out.path              = "./" + own + (own.empty() || rest.empty() ? "" : "/") + rest;
            return out;
        }
        if (ALLuauConfig::reservedAlias(alias))
        {
            out.error = "the alias '@" + alias + "' is reserved: aliases starting @sl- are Second Life's";
            return out;
        }
        for (const std::string& component : componentsOf(rest))
        {
            if (component == "..")
            {
                out.error = "a require through an alias may not climb out of its folder: '" + name_in + "'";
                return out;
            }
        }
        out.path = name;
        return out;
    }

    Walked walk(ALRequirePlaces& places, const std::string& from, const std::string& name)
    {
        Walked      out;
        std::string folder, stem;
        const bool  placed = places.placeOf(from, folder, stem);
        Position    start{ placed ? folder : std::string(), placed ? stem : std::string() };
        std::string path;
        if (ALLuauConfig::absolute(name))
        {
            // A path from a root, which the navigator does not walk: where
            // it stands, on disk, where a folder a require reads holds it.
            std::string at_folder, at_name;
            if (!places.placeOfAbsolute(name, std::string(), at_folder, at_name))
            {
                out.error = "could not find '" + name + "'";
                return out;
            }
            start = Position{ at_folder, at_name };
        }
        else
        {
            const Path said = navigatorPath(name, placed ? stem : std::string());
            if (!said.error.empty())
            {
                out.error = said.error;
                return out;
            }
            path = said.path;
        }
        Context context(places, start);
        if (!path.empty())
        {
            Said                     errors;
            Luau::Require::Navigator navigator(context, errors);
            if (navigator.navigate(path) != Luau::Require::Navigator::Status::Success)
            {
                out.found = context.pending() ? Known::Pending : Known::No;
                out.error = context.missed().empty() ? errors.what : context.missed();
                return out;
            }
        }
        // The module where the walk stopped: a file of the name, before the
        // init of a folder of it (LAD3); a folder's own init where it
        // stopped at a folder. One named with a script's extension is that
        // file alone.
        const Position&   at = context.at();
        std::vector<File> files;
        std::vector<File> init;
        if (at.name)
        {
            context.known(places.files(at.folder, *at.name, files));
            std::string sub;
            if (!endsWithScript(*at.name) && context.known(places.subfolder(at.folder, *at.name, sub)))
            {
                context.known(places.files(sub, "init", init));
            }
        }
        else
        {
            context.known(places.files(at.folder, "init", init));
        }
        if (context.pending())
        {
            out.found = Known::Pending;
            return out;
        }
        if (files.empty() && init.empty())
        {
            out.error = "could not find a module at '" + name + "'";
            return out;
        }
        out.found = Known::Yes;
        if (!files.empty())
        {
            out.files = std::move(files);
            if (!init.empty())
            {
                out.passedOver = init.front();
            }
        }
        else
        {
            out.files = std::move(init);
        }
        return out;
    }

    std::vector<std::string> aliasesFrom(ALRequirePlaces& places, const std::string& from)
    {
        std::vector<std::string> out;
        const auto               add = [&out](const std::string& alias) {
            if (!ALLuauConfig::reservedAlias(alias) && std::find(out.begin(), out.end(), alias) == out.end())
            {
                out.push_back(alias);
            }
        };
        // Up from the file's folder as the navigator climbs for an alias;
        // from a file in no place, from where the walk of one starts --
        // nowhere, above which are the scripter's include folders.
        std::string folder, stem;
        if (!places.placeOf(from, folder, stem))
        {
            folder.clear();
        }
        for (size_t up = 0; up < 64; ++up)
        {
            ALRequirePlaces::Config config;
            const Known             said = places.config(folder, config);
            if (said == Known::Yes && (config.ambiguous || !config.error.empty()))
            {
                return out;
            }
            if (said == Known::Yes)
            {
                for (const auto& [alias, value] : config.aliases)
                {
                    add(alias);
                }
            }
            std::string above;
            if (places.parentOf(folder, above) != Known::Yes)
            {
                break;
            }
            folder = above;
        }
        for (const std::string& alias : places.studioAliasNames())
        {
            add(alias);
        }
        return out;
    }
}

/**
 * @file alcvarpreview.cpp
 * @brief What opening a settings file would do to its rows.
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

#include "alcvarpreview.h"

#include "fsyspath.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>

namespace
{
    namespace fs = std::filesystem;

    std::string shown(const ALCVarValue& value)
    {
        return value.empty() ? std::string("nothing") : ALCVarValues::toJson(value);
    }

    S32 numberAt(std::span<const ALCVarStep> steps, size_t index)
    {
        return steps[index].number ? steps[index].number : S32(index + 1);
    }

    const ALCVarMigrate* migrateOf(const ALCVarDesc& desc, S32 step)
    {
        for (const ALCVarMigrate& migrate : desc.migrates)
        {
            if (migrate.step == step)
            {
                return &migrate;
            }
        }
        return nullptr;
    }

    // The steps a value of step `at` is brought through, "1 -> 2 (scale) ->
    // 3", and the first hook of them the store does not register.
    std::string trail(const ALCVars& store, const ALCVarDesc& desc, size_t at, std::string& missing)
    {
        std::string out = "step " + std::to_string(numberAt(desc.steps, at));
        for (size_t i = at + 1; i < desc.steps.size(); ++i)
        {
            const S32            step    = numberAt(desc.steps, i);
            const ALCVarMigrate* migrate = migrateOf(desc, step);
            out += " -> " + std::to_string(step);
            if (!migrate || migrate->kind == ALCVarMigrate::Kind::Join)
            {
                continue;
            }
            switch (migrate->kind)
            {
                case ALCVarMigrate::Kind::Map:    out += " (map)"; break;
                case ALCVarMigrate::Kind::Scale:  out += " (scale)"; break;
                case ALCVarMigrate::Kind::Reset:  out += " (reset)"; break;
                case ALCVarMigrate::Kind::Rename: out += " (rename)"; break;
                case ALCVarMigrate::Kind::Hook:
                    out += " (fn " + std::string(migrate->hook) + ")";
                    if (missing.empty() && !store.migration(migrate->hook))
                    {
                        missing = std::string(migrate->hook);
                    }
                    break;
                default:                          break;
            }
        }
        return out;
    }

    // Where a row of the setting's own name is in its history: its index,
    // and how the line says it; nothing for a step the history does not
    // hold.
    std::optional<size_t> placeOwn(const ALCVarDesc& desc, U64 decl, std::string& said)
    {
        if (const auto at = ALCVarConvert::stepOf(desc, decl))
        {
            said = decl == 0 ? "imported, so at its first step: " : "";
            return at;
        }
        for (const ALCVarMigrate& migrate : desc.migrates)
        {
            for (const ALCVarJoin& join : migrate.join)
            {
                if (join.decl != decl)
                {
                    continue;
                }
                for (size_t i = 0; i < desc.steps.size(); ++i)
                {
                    if (numberAt(desc.steps, i) == migrate.step && desc.steps[i].alias < 0)
                    {
                        said = fmt::format("of a fork's other branch ({:016x}), answered by the join of step {}: ", decl,
                                           migrate.step);
                        return i;
                    }
                }
            }
        }
        return std::nullopt;
    }

    // Where an old name's row is in its setting's history, as fromStored
    // places it: at its step of the old name's chain, the first where it
    // carries none, or through the alias's codec.
    size_t placeAlias(const ALCVarDesc& desc, const ALCVarAlias& alias, U64 decl, std::string& said)
    {
        const S16 index  = S16(&alias - desc.aliases.data());
        bool      chain  = false;
        for (size_t i = 0; i < desc.steps.size(); ++i)
        {
            if (desc.steps[i].alias == index)
            {
                const bool first = !chain;
                chain            = true;
                if (desc.steps[i].decl == decl || (decl == 0 && first))
                {
                    said = decl == 0 ? "imported, so at its first step: " : "";
                    return i;
                }
            }
        }
        said = chain ? "of a step of the old name this build does not know, read through the alias: "
                     : "through the alias, ";
        if (chain)
        {
            return desc.steps.size() - 1;
        }
        for (size_t i = 0; i < desc.steps.size(); ++i)
        {
            if (desc.steps[i].alias < 0)
            {
                return i;
            }
        }
        return 0;
    }

    // What a setting of the other scope's row is for, said after it.
    std::string scopeNote(const ALCVarDesc& desc, ALCVarLayer which)
    {
        const bool mine = which == ALCVarLayer::Global ? desc.scope == ALCVarScope::Global
                                                       : desc.scope == ALCVarScope::Account || desc.has(ALCVAR_ACCOUNT_OVERRIDE);
        if (mine)
        {
            return "";
        }
        return desc.scope == ALCVarScope::Account ? " (an account's setting: read from this file only to move it)"
                                                  : " (the machine's setting: read from this file only to move it)";
    }
}

namespace ALCVarPreview
{
    std::vector<std::string> describe(const ALCVars& store, const std::vector<ALCVarRow>& rows,
                                      const std::vector<ALCVarRow>& prior, ALCVarLayer which)
    {
        boost::unordered_flat_map<std::string_view, const ALCVarRow*> byName;
        for (const ALCVarRow& row : rows)
        {
            byName.emplace(row.name, &row);
        }
        // As the store's load gives a hook the other rows, by any name.
        boost::unordered_node_map<std::string, ALCVarStored> stored;
        const ALCVarStoredLookup lookup = [&](std::string_view name) -> const ALCVarStored* {
            const auto row = byName.find(name);
            if (row == byName.end())
            {
                return nullptr;
            }
            if (const auto cached = stored.find(row->second->name); cached != stored.end())
            {
                return &cached->second;
            }
            const auto value = row->second->stored();
            return value ? &stored.emplace(row->second->name, *value).first->second : nullptr;
        };
        const ALCVarMigrationLookup migrations = [&store](std::string_view name) { return store.migration(name); };

        std::vector<std::pair<std::string, std::string>> lines;
        boost::unordered_flat_set<std::string_view>      told;
        // The settings each name gives a first value.
        boost::unordered_flat_map<std::string_view, std::vector<std::string_view>> sourceOf;
        for (U32 i = 0; i < store.settingCount(); ++i)
        {
            for (const ALCVarFrom& from : store.desc(i).from)
            {
                sourceOf[from.name].push_back(store.desc(i).name);
            }
        }

        for (U32 i = 0; i < store.settingCount(); ++i)
        {
            const ALCVarDesc& d = store.desc(i);
            std::vector<std::pair<const ALCVarRow*, const ALCVarAlias*>> found;
            if (const auto own = byName.find(d.name); own != byName.end())
            {
                found.emplace_back(own->second, nullptr);
            }
            for (const ALCVarAlias& alias : d.aliases)
            {
                if (const auto old = byName.find(alias.name); old != byName.end())
                {
                    found.emplace_back(old->second, &alias);
                }
            }
            auto label = [&](const ALCVarRow& row, const ALCVarAlias* via) {
                return via ? row.name + " (an old name of " + std::string(d.name) + ")" : row.name;
            };
            if (d.scope == ALCVarScope::Runtime)
            {
                for (const auto& [row, via] : found)
                {
                    lines.emplace_back(row->name, label(*row, via) + ": of a setting kept for the session alone: left alone");
                    told.insert(row->name);
                }
                continue;
            }

            // The newest of its own row and its old names', as the load
            // takes it.
            const ALCVarRow*   winner = nullptr;
            const ALCVarAlias* via    = nullptr;
            for (const auto& [row, alias] : found)
            {
                if (!winner || (alias && row->modified > winner->modified))
                {
                    winner = row;
                    via    = alias;
                }
            }
            for (const auto& [row, alias] : found)
            {
                told.insert(row->name);
                if (row != winner)
                {
                    lines.emplace_back(row->name, label(*row, alias) + ": older than " + winner->name +
                                                      "'s row, which " + std::string(d.name) + " takes: left alone");
                }
            }
            const std::string scope = scopeNote(d, which);

            if (!winner)
            {
                // Its sources', where any is stored.
                const bool sourced = std::any_of(d.from.begin(), d.from.end(),
                                                 [&](const ALCVarFrom& from) { return byName.contains(from.name); });
                if (!sourced || !scope.empty())
                {
                    continue;
                }
                std::string missing;
                for (const ALCVarFrom& from : d.from)
                {
                    if (missing.empty() && byName.contains(from.name) && !store.migration(from.hook))
                    {
                        missing = std::string(from.hook);
                    }
                }
                ALCVarConvert::Path path;
                const auto          value = ALCVarConvert::fromStored(nullptr, d, nullptr, migrations, lookup, &path);
                const S32           first = d.steps.empty() ? 1 : d.steps.front().number;
                lines.emplace_back(std::string(d.name),
                                   std::string(d.name) + ": no row of its own; " +
                                       (value ? "its sources give " + shown(*value) + ", written at step " + std::to_string(first)
                                              : "its sources give nothing" +
                                                    (missing.empty() ? std::string() : ", as " + missing + " is not registered here") +
                                                    ": its default"));
                continue;
            }

            std::string head = label(*winner, via) + ": ";
            if (winner->tombstone)
            {
                lines.emplace_back(winner->name, head + "the user's value, removed: " + std::string(d.name) +
                                                     " takes its default" + scope);
                continue;
            }
            const auto row = winner->stored();
            if (!row)
            {
                lines.emplace_back(winner->name, head + "of a type or unit this build does not know: left alone" + scope);
                continue;
            }
            const bool current = !via && winner->type == S32(d.type) && winner->decl == d.decl() &&
                                 winner->unit == ALCVarUnits::nameOf(d.unit);
            if (current)
            {
                const auto value = ALCVarConvert::fit(row->value, d);
                const S32  step  = d.steps.empty() ? 1 : numberAt(d.steps, d.steps.size() - 1);
                head += "at step " + std::to_string(step) + ", its last: " + shown(row->value);
                head += !value                     ? ", which does not fit it: its default, and the row is left as it was"
                        : *value == row->value     ? ", as it is"
                                                   : ", read as " + shown(*value) + "; the row is left as it was";
                lines.emplace_back(winner->name, head + scope);
                continue;
            }
            if (!via && std::find(d.superseded.begin(), d.superseded.end(), winner->decl) != d.superseded.end())
            {
                lines.emplace_back(winner->name, head + "of the setting the name was before it was declared again: left alone; " +
                                                     std::string(d.name) + " takes its default" + scope);
                continue;
            }

            std::string           where;
            std::optional<size_t> at;
            if (via)
            {
                at = placeAlias(d, *via, winner->decl, where);
            }
            else
            {
                at = placeOwn(d, winner->decl, where);
            }
            ALCVarConvert::Path path;
            auto                value = ALCVarConvert::fromStored(&*row, d, via, migrations, lookup, &path);
            std::string         missing;
            std::string         through;
            if (at && !d.steps.empty())
            {
                through = trail(store, d, *at, missing) + ": ";
            }
            else
            {
                // A newer viewer's step, or another branch's: by its type and
                // unit, and the row it kept from before a migrate where this
                // build knows that one's step.
                through = fmt::format("of a step this build does not know ({:016x}), read by its type and unit: ", winner->decl);
                const ALCVarRow* known = nullptr;
                for (const ALCVarRow& kept : prior)
                {
                    if (kept.name == d.name && ALCVarConvert::stepOf(d, kept.decl) && (!known || kept.modified > known->modified))
                    {
                        known = &kept;
                    }
                }
                if (const auto earlier = known ? known->stored() : std::nullopt)
                {
                    ALCVarConvert::Path unused;
                    value   = ALCVarConvert::fromStored(&*earlier, d, nullptr, migrations, lookup, &unused);
                    through += "the row it kept from step " +
                               std::to_string(numberAt(d.steps, *ALCVarConvert::stepOf(d, known->decl))) + " read instead: ";
                }
            }
            std::string fate;
            if (value)
            {
                fate = shown(winner->value) + " becomes " + shown(*value);
                if (path.known && path.moved)
                {
                    fate += "; written forward at step " + std::to_string(d.steps.empty() ? 1 : numberAt(d.steps, d.steps.size() - 1));
                    fate += via ? " under " + std::string(d.name) + ", the old name's row kept"
                                : path.migrated ? ", the row before it kept for the viewers of its step" : "";
                }
                else
                {
                    fate += "; the row is left as it was, for the viewer that wrote it";
                }
            }
            else
            {
                fate = shown(winner->value) + " does not convert" +
                       (missing.empty() ? std::string() : ", as " + missing + " is not registered here") + ": " +
                       std::string(d.name) + " takes its default, and the row is left as it was";
            }
            lines.emplace_back(winner->name, head + where + through + fate + scope);
        }

        // Rows no setting of this build has, which an open keeps as they are.
        for (const ALCVarRow& row : rows)
        {
            if (told.contains(row.name))
            {
                continue;
            }
            std::string line = row.name + ": no setting of this build is named so: left alone, for the viewer that wrote it";
            if (const auto takers = sourceOf.find(row.name); takers != sourceOf.end())
            {
                std::vector<std::string> names(takers->second.begin(), takers->second.end());
                std::string              list;
                for (size_t n = 0; n < names.size(); ++n)
                {
                    list += (n ? n + 1 == names.size() ? " and " : ", " : "") + names[n];
                }
                line += "; " + list + (names.size() == 1 ? " takes its first value" : " take their first values") + " from it";
            }
            lines.emplace_back(row.name, line);
        }
        std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::string> out;
        for (auto& [name, line] : lines)
        {
            out.push_back(std::move(line));
        }
        return out;
    }

    std::optional<std::vector<std::string>> file(const ALCVars& store, const std::string& path, ALCVarLayer which,
                                                 std::string& error)
    {
        if (path.ends_with(".albackup"))
        {
            error = "a backup (.albackup) has no reader yet: preview the settings.db it was taken from";
            return std::nullopt;
        }
        const fs::path  source = fsyspath(path);
        std::error_code ec;
        if (!fs::is_regular_file(source, ec))
        {
            error = "cannot be read";
            return std::nullopt;
        }
        // The file and its journal copied, as SQLite reads them together.
        const fs::path dir = fs::temp_directory_path(ec) / ("alcvar_preview_" + LLUUID::generateNewID().asString());
        fs::create_directories(dir, ec);
        const fs::path copy = dir / "settings.db";
        fs::copy_file(source, copy, ec);
        for (const char* journal : { "-wal", "-journal" })
        {
            fs::path from = source;
            from += journal;
            fs::path to = copy;
            to += journal;
            if (!ec && fs::exists(from))
            {
                fs::copy_file(from, to, ec);
            }
        }
        if (ec)
        {
            error = "cannot be copied: " + ec.message();
            fs::remove_all(dir, ec);
            return std::nullopt;
        }

        std::optional<std::vector<std::string>> lines;
        {
            ALCVarFileOptions options;
            options.mayQuarantine = false;
            ALCVarFile            file(fsyspath(copy).string(), options);
            const ALDbOpenResult& opened = file.open();
            if (!opened.usable() || opened.outcome == ALDbOpenResult::Outcome::Transient)
            {
                error = opened.message.empty() ? std::string("is not a settings file") : opened.message;
            }
            else
            {
                const auto rows  = file.readRows();
                const auto prior = rows ? file.readPrior() : std::nullopt;
                if (!rows || !prior)
                {
                    error = "could not be read whole";
                }
                else
                {
                    lines = describe(store, *rows, *prior, which);
                    if (opened.outcome == ALDbOpenResult::Outcome::Recovered)
                    {
                        lines->insert(lines->begin(), "the file is damaged: the rows said are what could be read of it");
                    }
                }
            }
            file.close();
        }
        fs::remove_all(dir, ec);
        return lines;
    }
}

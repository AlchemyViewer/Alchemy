/**
 * @file alcvarxmlimport.cpp
 * @brief The settings XML an LLControlGroup saved, imported once into a new
 *        settings file.
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

#include "alcvarxmlimport.h"

#include "alcvars.h"

#include "llfile.h"
#include "llsdserialize.h"

#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <array>

std::optional<ALCVarType> ALCVarXmlImport::typeNamed(std::string_view name)
{
    // LLControlGroup's eControlType, by the names its files carry.
    static constexpr std::array<std::pair<std::string_view, ALCVarType>, 12> TYPES = { {
        { "U32", ALCVarType::U32 },
        { "S32", ALCVarType::S32 },
        { "F32", ALCVarType::F32 },
        { "Boolean", ALCVarType::Bool },
        { "String", ALCVarType::String },
        { "Vector3", ALCVarType::Vec3 },
        { "Vector3D", ALCVarType::Vec3d },
        { "Quaternion", ALCVarType::Quat },
        { "Rect", ALCVarType::Rect },
        { "Color4", ALCVarType::Color4 },
        { "Color3", ALCVarType::Color3 },
        { "LLSD", ALCVarType::LLSD },
    } };
    for (const auto& [text, type] : TYPES)
    {
        if (text == name)
        {
            return type;
        }
    }
    return std::nullopt;
}

bool ALCVarXmlImport::read(const std::string& path, const ALCVars& store, ALCVarLayer which, S64 stamp,
                           std::vector<ALCVarRow>& rows, Counts& counts)
{
    llifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open())
    {
        LL_WARNS("ALCVars") << path << " cannot be read to import" << LL_ENDL;
        return false;
    }
    LLSD settings;
    if (LLSDSerialize::fromXMLDocument(settings, in) == LLSDParser::PARSE_FAILURE || !settings.isMap())
    {
        LL_WARNS("ALCVars") << path << " is not a settings file" << LL_ENDL;
        return false;
    }

    // Every value the file saved, in the type it gives it, by name: what a
    // hook reads, as it reads the rows of settings.db by name. LLControlGroup
    // keeps a U32 as LLSD's signed integer, so one past 2^31 is saved
    // negative, and read back as the U32 it was.
    boost::unordered_node_map<std::string, ALCVarStored> saved;
    for (auto it = settings.beginMap(); it != settings.endMap(); ++it)
    {
        const LLSD& entry = it->second;
        if (!entry.isMap() || !entry.has("Value") || (entry.has("Persist") && entry["Persist"].asInteger() == 0))
        {
            continue;
        }
        const auto  type = typeNamed(entry["Type"].asString());
        ALCVarValue value(entry["Value"]);
        if (type == ALCVarType::U32 && entry["Value"].isInteger())
        {
            value = ALCVarValue(U32(entry["Value"].asInteger()));
        }
        else if (type)
        {
            if (auto typed = ALCVarConvert::toType(value, *type))
            {
                value = std::move(*typed);
            }
        }
        saved.emplace(it->first, ALCVarStored{ std::move(value), ALCVarUnit::None, 0 });
    }
    const ALCVarMigrationLookup migrations = [&store](std::string_view name) { return store.migration(name); };
    const ALCVarStoredLookup    lookup     = [&saved](std::string_view name) -> const ALCVarStored* {
        const auto found = saved.find(std::string(name));
        return found != saved.end() ? &found->second : nullptr;
    };
    boost::unordered_flat_set<std::string> written;

    for (auto it = settings.beginMap(); it != settings.endMap(); ++it)
    {
        const std::string& name  = it->first;
        const LLSD&        entry = it->second;
        if (!entry.isMap() || !entry.has("Value"))
        {
            continue;
        }
        if (entry.has("Persist") && entry["Persist"].asInteger() == 0)
        {
            ++counts.unsaved;
            continue;
        }
        const ALCVarValue& value = saved.find(name)->second.value;

        const ALCVarRef ref = store.find(name);
        if (!ref)
        {
            // A setting of another viewer's, or of a newer one: kept under
            // its own name, for whoever declares it.
            ALCVarRow row;
            row.name     = name;
            row.type     = S32(value.type());
            row.value    = ALCVarFile::persisted(value);
            row.modified = stamp;
            rows.push_back(std::move(row));
            ++counts.unknown;
            continue;
        }

        const ALCVarDesc& desc = ref.desc();
        const bool        mine = which == ALCVarLayer::Global
                                     ? desc.scope == ALCVarScope::Global
                                     : desc.scope == ALCVarScope::Account || desc.has(ALCVAR_ACCOUNT_OVERRIDE);
        if (desc.scope == ALCVarScope::Runtime)
        {
            continue;
        }

        // Written at the setting's first step, in the unit the alias says.
        const ALCVarStored stored{ value, ALCVarUnit::None, 0 };
        const auto         converted = ALCVarConvert::fromStored(&stored, desc, ref.alias(), migrations, lookup);
        if (!converted)
        {
            // A sensitive value is never logged.
            LL_WARNS("ALCVars") << name << " = " << (desc.has(ALCVAR_SENSITIVE) ? "(sensitive)" : value.describe())
                                << " does not convert into " << desc.name << ", which keeps its default" << LL_ENDL;
            ++counts.refused;
            continue;
        }

        ALCVarRow row;
        row.name     = std::string(desc.name);
        row.type     = S32(desc.type);
        row.unit     = std::string(ALCVarUnits::nameOf(desc.unit));
        row.decl     = desc.decl();
        row.value    = ALCVarFile::persisted(*converted);
        row.modified = stamp;
        written.insert(row.name);
        rows.push_back(std::move(row));
        // Of the other scope, for the scope moves to take from this file.
        ++(mine ? counts.imported : counts.others);
    }

    // A setting split from others, or merged, the file saved none of: what
    // its sources' hooks make of the file's values of them.
    for (const ALCVarGroupDesc* group : store.schema().groups)
    {
        for (const ALCVarDesc& desc : group->settings)
        {
            if (desc.from.empty() || desc.scope == ALCVarScope::Runtime || written.contains(std::string(desc.name)))
            {
                continue;
            }
            const auto converted = ALCVarConvert::fromStored(nullptr, desc, nullptr, migrations, lookup);
            if (!converted)
            {
                continue;
            }
            const bool mine = which == ALCVarLayer::Global
                                  ? desc.scope == ALCVarScope::Global
                                  : desc.scope == ALCVarScope::Account || desc.has(ALCVAR_ACCOUNT_OVERRIDE);
            ALCVarRow  row;
            row.name     = std::string(desc.name);
            row.type     = S32(desc.type);
            row.unit     = std::string(ALCVarUnits::nameOf(desc.unit));
            row.decl     = desc.decl();
            row.value    = ALCVarFile::persisted(*converted);
            row.modified = stamp;
            rows.push_back(std::move(row));
            ++(mine ? counts.imported : counts.others);
        }
    }
    return true;
}

/**
 * @file alcvarconvert.h
 * @brief The one conversion every stored value takes into the setting it is
 *        now: a settings file's rows, presets, backups and imported XML alike.
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

#include "alcvarconvertible.h"
#include "alcvardesc.h"
#include "alcvarvalue.h"

#include <functional>
#include <optional>
#include <string_view>

// A value as something stored it: in its own type and unit, at a step of
// its setting's history.
struct ALCVarStored
{
    ALCVarValue value;
    // None where the writer said nothing of a unit.
    ALCVarUnit  unit = ALCVarUnit::None;
    // The fingerprint of the step it was written at. One the setting's
    // history does not hold was written by a newer viewer, or by a build of
    // another branch.
    U64         decl = 0;
};

// The other values stored beside the one converted, by the name they are
// stored under, old names included: what a migration that splits or merges
// settings reads. Null where there is none.
using ALCVarStoredLookup = std::function<const ALCVarStored*(std::string_view name)>;

// A migration a declaration names by its migrate's fn: the setting's value
// from what was stored -- its own stored value, if any, and the rest by
// name -- or nothing, to take the default.
using ALCVarMigration =
    std::function<std::optional<ALCVarValue>(const ALCVarDesc& desc, const ALCVarStored* stored,
                                              const ALCVarStoredLookup& lookup)>;

// Finds a registered migration by its name; null where none is.
using ALCVarMigrationLookup = std::function<const ALCVarMigration*(std::string_view name)>;

namespace ALCVarConvert
{
    // A value of one type as another, as the conversion table says
    // (ALCVarConvert::convertible): integers clamped to the narrower type,
    // floats rounded to the nearest integer, any nonzero number true,
    // strings formatted and parsed, a number in every component of a vector
    // and alpha 1, a scalar a one-element list, LLSD through its form.
    // Nothing where the table has no conversion or the value does not fit:
    // a string that does not parse, a NaN as an integer.
    std::optional<ALCVarValue> toType(const ALCVarValue& value, ALCVarType type);

    // A number, or each component of a vector, scaled from one unit into
    // another of the same quantity; nothing across quantities. A unit of
    // None on either side is taken to be the other's.
    std::optional<ALCVarValue> toUnit(const ALCVarValue& value, ALCVarUnit from, ALCVarUnit to);

    // The form a value read or written under an alias has: a string for a
    // UUID or a list kept in one, a boolean's opposite, the alias's unit --
    // a real for an integer setting, whose value in another unit need not
    // be whole.
    ALCVarType aliasType(const ALCVarAlias& alias, const ALCVarDesc& desc);
    // The setting's value in that form, and back from it: nothing where it
    // does not convert.
    std::optional<ALCVarValue> toAlias(const ALCVarValue& value, const ALCVarAlias& alias, const ALCVarDesc& desc);
    std::optional<ALCVarValue> fromAlias(const ALCVarValue& value, const ALCVarAlias& alias, const ALCVarDesc& desc);

    // Clamped into the range, a value no longer among the choices mapped to
    // the one whose `was` lists it, every float finite; nothing where it does
    // not fit, which takes the default.
    std::optional<ALCVarValue> fit(const ALCVarValue& value, const ALCVarDesc& desc);

    // Where a fingerprint is in the setting's own history: the index of its
    // step, or nothing for one the history does not hold. No fingerprint at
    // all is the setting's own first step: what the XML import keeps of a
    // name the schema did not declare then, taken as every imported value
    // is.
    std::optional<size_t> stepOf(const ALCVarDesc& desc, U64 decl);

    // How a stored value was taken.
    struct Path
    {
        // Its step is one the setting's history holds, or it is an old
        // name's: a row this build may write forward.
        bool known    = false;
        // It came through a step after its own, or from an old name.
        bool moved    = false;
        // A migrate took it on the way: what it meant changed, which a
        // viewer of its step cannot read from the row it is written as.
        bool migrated = false;
    };

    // A stored value into the setting it is now. One of a step of its
    // history is brought forward through each step after it: a step with a
    // migrate takes it as the step before held it and gives it in its own
    // type and unit -- a rename's through the old name's codec -- and
    // between them the table converts it and scales its unit, an integer's
    // value a real until the last step rounds it once. One found under
    // `via`, an old name, is at its step of the old name's history, the
    // first where it has no fingerprint, and comes through the steps after
    // it and the rename; where the old name had no history, through the
    // alias's codec at the setting's first step. One of a fork's other
    // branch takes the join's answer, at the join's step. One of a step the
    // history does not hold is read by the type and unit it carries, through
    // the table alone; one of the setting a reused name was before, never.
    // Then fit() against the declaration as it is. Nothing where it does not
    // convert, or a hook throws or gives nothing: the setting takes its
    // default and the stored value is left as it was. With no stored value
    // at all, the setting's sources (`from`), each found by `lookup` and
    // brought to its own history's last step -- through its own declaration,
    // migrates and all, where it is declared, and the table alone where it
    // was retired -- give one through their hooks, at the setting's first
    // step. `path`, if given, says how it was taken.
    std::optional<ALCVarValue> fromStored(const ALCVarStored* stored, const ALCVarDesc& desc,
                                          const ALCVarAlias* via, const ALCVarMigrationLookup& migrations,
                                          const ALCVarStoredLookup& lookup, Path* path = nullptr);
}

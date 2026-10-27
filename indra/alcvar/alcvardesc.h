/**
 * @file alcvardesc.h
 * @brief What the schema says of each setting and group, as the generator
 *        writes it into read-only tables, and the slots the values live in.
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

#include "alcvartype.h"

#include <boost/smart_ptr/atomic_shared_ptr.hpp>

#include <span>
#include <string_view>

// One of a setting's enumerated values: a number for an integer setting, a
// text for a string one, with the old values that became it.
struct ALCVarChoice
{
    S64                               number = 0;
    std::string_view                  text;
    std::string_view                  label;
    std::span<const S64>              wasNumbers;
    std::span<const std::string_view> wasTexts;
};

// An old name a setting is found under, and how a value stored under it was
// written where it differs from the setting's own.
struct ALCVarAlias
{
    std::string_view name;
    // A boolean that said the opposite.
    bool             inverted = false;
    // A UUID or a list kept in a string.
    bool             stringEncoded = false;
    // A list kept in a string, its elements joined by this.
    std::string_view joined;
    // The unit the old name's values were in; None for the setting's own.
    ALCVarUnit       unit = ALCVarUnit::None;
};

// A step of a setting's history, as the lock records it: what its stored
// values meant from the step on, until the next. A value carries its step's
// fingerprint.
struct ALCVarStep
{
    U64        decl = 0;
    ALCVarType type = ALCVarType::Bool;
    ALCVarUnit unit = ALCVarUnit::None;
    // The old name it was written under, by its place among the setting's
    // aliases; -1 for the setting's own.
    S16        alias = -1;
    // Its number, which its migrate names; 0 for a history made by hand,
    // numbered from 1 in order.
    S32        number = 0;
};

// A map's entry: a value of the step before and what it becomes, each a
// number or a string as its step holds it -- the text as the declaration
// wrote it.
struct ALCVarMapEntry
{
    F64              from = 0.0;
    F64              to   = 0.0;
    std::string_view fromText;
    std::string_view toText;
};

// A step of a fork's other branch, and how the join that ends the fork
// takes a value of it: as that step held it, into the join's own step.
struct ALCVarJoin
{
    U64                                     decl      = 0;
    ALCVarType                              type      = ALCVarType::Bool;
    ALCVarUnit                              unit      = ALCVarUnit::None;
    ALCVarMigrateKind                       kind      = ALCVarMigrateKind::None;
    std::span<const ALCVarMapEntry>         map;
    ALCVarMapElse                           otherwise = ALCVarMapElse::Keep;
    F64                                     elseValue = 0.0;
    std::string_view                        elseText;
    F64                                     scale     = 1.0;
    F64                                     offset    = 0.0;
    std::string_view                        hook;
};

// How the meaning of stored values changed at a step: the change the
// conversions cannot see for themselves. It takes a value as the step
// before held it, and gives it in its own step's type and unit.
struct ALCVarMigrate
{
    using Kind = ALCVarMigrateKind;

    // The step it begins: 2 or later.
    S32                                     step   = 0;
    Kind                                    kind   = Kind::None;
    // Map: an old value to a new one, and what becomes of one it does not
    // list: a number, or a string's text.
    std::span<const ALCVarMapEntry>         map;
    ALCVarMapElse                           otherwise = ALCVarMapElse::Keep;
    F64                                     elseValue = 0.0;
    std::string_view                        elseText;
    // Scale: value * scale + offset.
    F64                                     scale  = 1.0;
    F64                                     offset = 0.0;
    // Hook: the registered migration of this name.
    std::string_view                        hook;
    // Rename: the alias whose codec takes the old name's value.
    S16                                     alias = -1;
    // Join: each of the other branch's steps, answered.
    std::span<const ALCVarJoin>             join;
};

struct ALCVarDesc;

// A setting another takes its first value from, with the hook that works
// it out and the steps its rows are brought through: its declaration's,
// migrates and all, where it is declared, and the lock's alone where it was
// retired.
struct ALCVarFrom
{
    std::string_view            name;
    std::string_view            hook;
    std::span<const ALCVarStep> steps;
    const ALCVarDesc*           desc = nullptr;
};

// A setting's compiled default. The fixed-size types are kept as their slot
// holds them; a string as itself, and a list or LLSD as JSON text.
struct ALCVarDefault
{
    U64              words[3] = { 0, 0, 0 };
    std::string_view text;
};

struct ALCVarDesc
{
    // The setting's whole name: "render.shadows.resolution_scale".
    std::string_view   name;
    std::string_view   label;
    std::string_view   help;
    ALCVarType         type    = ALCVarType::Bool;
    ALCVarScope        scope   = ALCVarScope::Global;
    ALCVarUnit         unit    = ALCVarUnit::None;
    ALCVarBackup       backup  = ALCVarBackup::Yes;
    ALCVarPreset       preset  = ALCVarPreset::None;
    ALCVarRefers       refers  = ALCVarRefers::None;
    ALCVarOrigin       origin  = ALCVarOrigin::Alchemy;
    U16                flags   = 0;
    // Its place in its group, and in its group's array of its storage.
    U16                key     = 0;
    U16                slot    = 0;
    ALCVarDefault      def;
    // The values that are valid, and what a slider or spinner offers. For a
    // vector, each component's.
    bool               ranged   = false;
    bool               uiRanged = false;
    F64                rangeMin = 0.0;
    F64                rangeMax = 0.0;
    F64                uiMin    = 0.0;
    F64                uiMax    = 0.0;
    F64                uiStep   = 0.0;
    std::span<const ALCVarChoice>     choices;
    std::span<const ALCVarAlias>      aliases;
    std::span<const std::string_view> tags;
    std::string_view   validate;
    std::string_view   asset;
    // Its history, step 1 first, the last the declaration as it is: what
    // its stored values have meant.
    std::span<const ALCVarStep>       steps;
    // Its steps' migrates, by step.
    std::span<const ALCVarMigrate>    migrates;
    // The steps of the setting a reused name was before: their values take
    // the default, and are left as they are.
    std::span<const U64>              superseded;
    // What it takes its first value from, where it has none of its own.
    std::span<const ALCVarFrom>       from;
    // Where it is declared.
    std::string_view   file;
    U32                line = 0;

    bool has(ALCVarFlag flag) const { return (flags & flag) != 0; }
    // What a value written now carries: its last step's fingerprint.
    U64  decl() const { return steps.empty() ? 0 : steps.back().decl; }
};

// A value of two or three words, with the sequence word a reader checks
// before and after it: odd while a write is under way.
template <size_t WORDS>
struct ALCVarSeqSlot
{
    U64 sequence = 0;
    U64 words[WORDS] = {};
};

// A string or a list: null until the setting is first published, which reads
// the descriptor's default. `current` is the same object as `value` holds,
// for the main thread's views.
struct ALCVarSharedSlot
{
    boost::atomic_shared_ptr<const void> value;
    const void*                          current = nullptr;
};

// An LLSD value, made at registration.
struct ALCVarBoxedSlot
{
    LLSD* value = nullptr;
};

// What a group's own code keeps at run time: its sequence word, which a
// group's read() checks and every write to the group flips, on a cache line
// of its own, and the indices registration gives it.
struct alignas(64) ALCVarGroupState
{
    U32 sequence   = 0;
    // The index of the group's first setting among all of the schema's.
    U32 first      = 0;
    U32 index      = 0;
    U32 registered = 0;
};

struct ALCVarGroupDesc
{
    // "render.shadows"; "render" for an area.
    std::string_view          name;
    std::string_view          label;
    std::string_view          help;
    // Its parent group's name, "" for an area.
    std::string_view          parent;
    // The header its handles are in: "cvars/render/shadows.h".
    std::string_view          header;
    ALCVarBackup              backup = ALCVarBackup::Yes;
    ALCVarPreset              preset = ALCVarPreset::None;
    ALCVarGroupState*         state  = nullptr;
    std::span<const ALCVarDesc> settings;
    // Its values, an array a storage class.
    std::span<U8>               bytes;
    std::span<U32>              words;
    std::span<U64>              dwords;
    std::span<ALCVarSeqSlot<2>> pairs;
    std::span<ALCVarSeqSlot<3>> triples;
    std::span<ALCVarSharedSlot> shared;
    std::span<ALCVarBoxedSlot>  boxed;
    std::string_view          file;
    U32                       line = 0;
};

// A whole schema, as the generator writes it: every group, in the order of
// the files and of the entries in them, parents before their children.
struct ALCVarSchema
{
    // The namespace its handles are under: "cvar".
    std::string_view                         root;
    std::span<const ALCVarGroupDesc* const>  groups;
    // Every tag a setting names, sorted.
    std::span<const std::string_view>        tags;
};

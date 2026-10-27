/**
 * @file alcvarref.h
 * @brief A setting found by name, at the boundaries that cannot hold a
 *        handle: XUI, menus, LEAP, the command line, files.
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

#include "alcvar.h"

#include <optional>

// A setting as ALCVars::find gives it: its index, and the alias it was found
// under, if any. Through an alias, values are in the alias's form, converted
// both ways by its codec: a boolean's opposite, a UUID or a list kept in a
// string, the old unit. Resolved once, where the name is met, and kept.
class ALCVarRef
{
public:
    static constexpr U32 NONE = U32(-1);

    ALCVarRef() = default;
    ALCVarRef(U32 index, const ALCVarAlias* alias) : mIndex(index), mAlias(alias) {}

    explicit operator bool() const { return mIndex != NONE; }
    bool operator==(const ALCVarRef& other) const = default;

    U32                index() const { return mIndex; }
    const ALCVarDesc&  desc() const;
    // The alias it was found under; null for its own name.
    const ALCVarAlias* alias() const { return mAlias; }
    // The name it was found by.
    std::string_view   name() const;
    // The type of what is read and written through it.
    ALCVarType         type() const;

    // The value, in the alias's form.
    ALCVarValue                get() const;
    // As another type, by the conversion table: what a by-name read of
    // another type, as getF32 of an integer setting, is given; nothing where
    // it does not convert.
    std::optional<ALCVarValue> getAs(ALCVarType type) const;
    // For the UI's boundary.
    LLSD                       getLLSD() const;

    // A user write of a value of any type, converted into the alias's form
    // and through its codec: false where it does not convert or is refused.
    bool set(const ALCVarValue& value) const;
    bool setLLSD(const LLSD& value) const;
    // A Session write, as the command line's --set makes.
    bool setSession(const ALCVarValue& value) const;
    void reset() const;
    bool isUserSet() const;

    // Told on the main thread of each change, in the alias's form.
    [[nodiscard]] ALCVarConnection onChange(ALCVarListener listener) const;

private:
    // A value of any type into the setting's own.
    std::optional<ALCVarValue> toSetting(const ALCVarValue& value) const;

    U32                mIndex = NONE;
    const ALCVarAlias* mAlias = nullptr;
};

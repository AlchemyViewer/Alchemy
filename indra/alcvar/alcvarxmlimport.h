/**
 * @file alcvarxmlimport.h
 * @brief The settings XML an LLControlGroup saved, imported once into a new
 *        settings file through the one conversion.
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

#include "alcvarfile.h"

#include <optional>
#include <string>
#include <vector>

class ALCVars;
enum class ALCVarLayer : U8;

// A user's settings.xml or settings_per_account.xml, as LLControlGroup
// wrote it: a map of names to their Type, Value and Persist. Each value is
// found by its name or an old name, typed by the file and in the unit the
// alias says, and converted into the setting it is now as a stored row is.
// Every value is written; which of them the feature table wrote, and which
// defaults the user chose that the file could not hold, is for the store to
// settle against the quality level (ALCVars::setQuality). A setting of the
// other scope is kept as a row too, for the scope moves to take. Names the
// schema does not know are kept as rows of their own. The XML file is left
// as it was, so an older viewer still starts with what it last saved.
class ALCVarXmlImport
{
public:
    struct Counts
    {
        U32 imported = 0;
        // Of settings of the other scope, kept for the scope moves.
        U32 others   = 0;
        // Kept as rows of their own names.
        U32 unknown  = 0;
        // That do not convert: the setting takes its default.
        U32 refused  = 0;
        // Not persisted, as the file said.
        U32 unsaved  = 0;
    };

    // The rows the file gives, for a file of one layer -- Global or Account
    // -- each written at `stamp`. False when the file cannot be read or is
    // not settings XML.
    static bool read(const std::string& path, const ALCVars& store, ALCVarLayer which, S64 stamp,
                     std::vector<ALCVarRow>& rows, Counts& counts);

    // LLControlGroup's name for a type, as its files carry it.
    static std::optional<ALCVarType> typeNamed(std::string_view name);

    // The meta key that records the import.
    static constexpr const char* META_KEY = "imported_xml";
    // The meta key that holds the import's stamp until a quality level has
    // settled it, and "" after.
    static constexpr const char* PENDING_KEY = "import_pending";
};

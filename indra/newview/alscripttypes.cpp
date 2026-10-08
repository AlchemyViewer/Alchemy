/**
 * @file alscripttypes.cpp
 * @brief What the viewer knows a script by, and what the workspace answers about one.
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

#include "llviewerprecompiledheaders.h"

#include "alscripttypes.h"

#include "alsourcemap.h"
#include "llmd5.h"
#include "llsd.h"

#include <array>

std::string ALScriptRef::id() const
{
    const std::string                 joined = object.asString() + "_" + item.asString();
    std::array<char, MD5HEX_STR_SIZE> hex    = {};
    LLMD5                             hash(reinterpret_cast<const U8*>(joined.c_str()));
    hash.hex_digest(hex.data());
    return std::string(hex.data());
}

LLSD ALScriptRef::key() const
{
    LLSD key;
    key["taskid"] = object;
    key["itemid"] = item;
    return key;
}

ALScriptRef ALScriptRef::fromKey(const LLSD& key)
{
    if (key.isMap())
    {
        return ALScriptRef(key["taskid"].asUUID(), key["itemid"].asUUID());
    }
    return ALScriptRef(LLUUID::null, key.asUUID());
}

std::vector<ALScriptDiagnostic> ALScriptCompileResult::inSource(S32 source_line) const
{
    std::vector<ALScriptDiagnostic> out = diagnostics;
    for (ALScriptDiagnostic& said : out)
    {
        if (!said.hasLine || !sourceMap)
        {
            continue;
        }
        if (const ALSourceMap::Loc loc = sourceMap->toSource(said.line - codeLine, said.hasColumn ? said.column : 0); loc.file == 0)
        {
            said.line   = loc.line;
            said.column = loc.column;
        }
        else
        {
            if (loc.found())
            {
                const ALSourceMap::File& file = sourceMap->files()[loc.file];
                said.message = (file.path.empty() ? file.name : file.path) + ":" + std::to_string(loc.line + 1) + ": " + said.message;
            }
            said.hasLine   = false;
            said.hasColumn = false;
            continue;
        }
        said.line += source_line;
        said.hasColumn = said.hasColumn && source_line == 0;
    }
    return out;
}

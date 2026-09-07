/**
 * @file alxuidiagnostics.cpp
 * @brief Collects what the XUI parser and the widget factory would otherwise only log.
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

#include "alxuidiagnostics.h"

#include "llerror.h"

#include <algorithm>

ALXUIDiagnostics* ALXUIDiagnostics::sActive = nullptr;

ALXUIDiagnostics::ALXUIDiagnostics()
{
    llassert(sActive == nullptr);
    sActive = this;
}

ALXUIDiagnostics::~ALXUIDiagnostics()
{
    if (sActive == this)
    {
        sActive = nullptr;
    }
}

void ALXUIDiagnostics::report(Kind kind, S32 depth, std::string_view file, S32 line, std::string_view path, std::string_view message)
{
    mEntries.push_back(Entry{ kind, depth, line, std::string(file), std::string(path), std::string(message) });
}

size_t ALXUIDiagnostics::count(Kind kind) const
{
    return std::count_if(mEntries.begin(), mEntries.end(),
                         [kind](const Entry& e) { return e.kind == kind; });
}

const char* ALXUIDiagnostics::kindName(Kind kind)
{
    switch (kind)
    {
    case Kind::ParseError:       return "parse error";
    case Kind::ParseWarning:     return "parse warning";
    case Kind::UnknownAttribute: return "unknown attribute";
    case Kind::MisScopedElement: return "mis-scoped element";
    case Kind::InvalidChild:     return "invalid child";
    case Kind::CreateFailed:     return "create failed";
    }
    return "?";
}

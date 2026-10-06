/**
 * @file alluautypes.h
 * @brief How the SLua analyzer reads Luau's positions and types, wherever it reads them.
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

#include "alscriptspan.h"
#include "stdtypes.h"

#include "Luau/Location.h"
#include "Luau/TypeFwd.h"

#include <string>

// What the service and the classes over its front end (ALLuauFrontend)
// all read Luau's answers with, said one way wherever they are said.
// Luau's headers come with it, as with the front end's.
namespace ALLuauTypes
{
    // A zero-based line and byte column as Luau has a position; and a
    // stretch Luau found as the studio has one.
    Luau::Position positionOf(S32 line, S32 column);
    ALScriptSpan   spanOf(const Luau::Location& where);

    // How a type prints beside a name: a table's first few fields and
    // how many more, since `ll` has hundreds and a tip is one glance.
    std::string typeText(Luau::TypeId type);

    // The function type a call's callee has, or the first of an
    // overloaded one's.
    const Luau::FunctionType* functionOf(Luau::TypeId type);
}

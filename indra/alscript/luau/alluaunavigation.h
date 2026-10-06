/**
 * @file alluaunavigation.h
 * @brief Where a name in an SLua script is bound and used, across the modules it requires.
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

#include "alscriptsymbol.h"

#include "Luau/Location.h"

#include <optional>
#include <string>
#include <string_view>

namespace Luau
{
    struct Module;
    struct SourceModule;
    struct FrontendModuleResolver;
}

struct ALLuauFrontend;

// Where a name is bound and every place it stands: a local, a global, a
// field of a table, a type -- in the script the front end is asked about,
// and in the modules it requires, which Luau checks apart
// (ALLuauService::setModules).
//
// A field or a type a module declares is the module's wherever it is
// used. A table is known by where it was made, the module and the place
// in it, which the copy a requiring script sees keeps; a field of the same
// name on another table is another field. A type is known by where it was
// declared, through the local a script required its module as where it is
// one of a module's. A local and a global are the script's alone.
//
// Over the service's front end, which it asks and does not keep; one
// thread's, as the front end is.
class ALLuauNavigation
{
public:
    explicit ALLuauNavigation(ALLuauFrontend& front);

    // The name at `at` of the script, where it is declared and every place
    // it stands: the script's, and each module's by its key.
    ALScriptReferences references(std::string_view source, Luau::Position at);

    // Where the field or the type at `at` of the script is declared, in a
    // module checked already for a question about it: in the script itself,
    // or in a module it requires, by the module's key. None for a local or
    // a global, which Luau's own binding says, nor for what the definitions
    // declare.
    struct Declared
    {
        // Empty for the script itself.
        std::string    file;
        Luau::Location where;
    };
    std::optional<Declared> declaredAt(const Luau::Module& module, const Luau::SourceModule& source, Luau::Position at) const;

private:
    // Which of the front end's resolvers holds `module` and what was
    // checked with it: autocomplete's, under the old solver where the
    // script is not strict, else the check's.
    const Luau::FrontendModuleResolver& resolverOf(const Luau::Module& module) const;

    ALLuauFrontend& mFront;
};

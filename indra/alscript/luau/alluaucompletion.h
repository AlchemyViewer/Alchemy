/**
 * @file alluaucompletion.h
 * @brief What could go at a position of an SLua script, as Luau's autocomplete says.
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

#include <string_view>
#include <vector>

namespace Luau
{
    struct AutocompleteResult;
    struct Module;
}

class ALLuauFragment;
struct ALLuauFrontend;

// What could go at a position of the script the front end is asked about,
// as Luau's autocomplete answers over the module checked for it: each
// entry with what Luau knows of it -- whether it is of the type wanted
// there, where a call's brackets go, what the position is -- and none Luau
// marks as indexed the wrong way, a method after a dot or a field after a
// colon. In no order: the editor ranks them by what was typed.
//
// As the script is typed, the statement being typed is checked alone
// against the script's last check (ALLuauFragment), where that answers;
// else the script is checked whole first, where it changed.
//
// Over the service's front end and fragment, which it asks and does not
// keep; one thread's, as the front end is.
class ALLuauCompletion
{
public:
    ALLuauCompletion(ALLuauFrontend& front, ALLuauFragment& fragment);

    std::vector<ALScriptCompletion> complete(std::string_view source, S32 line, S32 column);

private:
    // What Luau found at `at`, in the studio's terms: `module` the one
    // whose types the entries are, the fragment's or the script's.
    std::vector<ALScriptCompletion> answer(const Luau::AutocompleteResult& found, const Luau::Module& module, Luau::Position at) const;

    ALLuauFrontend& mFront;
    ALLuauFragment& mFragment;
};

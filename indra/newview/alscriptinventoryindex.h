/**
 * @file alscriptinventoryindex.h
 * @brief The agent's scripts and notecards by name, kept from the inventory's changes rather than walked again.
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

#include "alscriptnameindex.h"
#include "llinventoryobserver.h"
#include "llsingleton.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The agent's scripts and notecards, out of the trash, by name: what an
// include or a require names in the inventory.
// One walk of the whole inventory the first time it is asked; from then,
// what the inventory says changed is looked at -- only the items it names
// -- rather than the whole tree walked again whenever anything anywhere
// moves, which a background fetch does constantly. A folder moved, or
// taken away, is walked again, since what is under it is not named.
class ALScriptInventoryIndex final : public LLSingleton<ALScriptInventoryIndex>, public LLInventoryObserver
{
    LLSINGLETON(ALScriptInventoryIndex);
    ~ALScriptInventoryIndex() override;

public:
    const std::vector<LLUUID>& named(std::string_view name);
    U32                        generation();
    // Every script and notecard listed, with its name, in no order: what
    // Quick Open offers. A link is listed as itself.
    template<typename Visit>
    void each(Visit&& visit)
    {
        if (!mIndex.built())
        {
            build();
        }
        mIndex.forEach(std::forward<Visit>(visit));
    }

    void changed(U32 mask) override;

private:
    void build();
    // Where an item is to be listed, its name; nothing where it is not.
    static std::optional<std::string> listedAs(const LLUUID& id);

    ALScriptNameIndex mIndex;
};

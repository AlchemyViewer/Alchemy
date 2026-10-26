/**
 * @file alcompact.h
 * @brief Dropping items from a vector in one pass, while it grows.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include <cstddef>
#include <utility>

/// Walks `items` in order and keeps those `keep` returns true for, in their order, dropping the rest: in one pass, where
/// a loop that erases each dropped item from the middle of the vector takes time growing with the square of its length.
///
/// `keep` is handed a copy of each item, and may append to `items` while it runs, as a walk that moves drawables marks
/// others moved: the walk reads the size again at every step, so what is appended is walked too, in its turn. It must
/// not change `items` otherwise. Returns how many items were dropped.
template <typename Vector, typename Keep>
size_t al_compact(Vector& items, Keep&& keep)
{
    size_t kept = 0;
    for (size_t next = 0; next < items.size(); ++next)
    {
        // A copy, not a reference: an append in keep() can move the vector's store.
        typename Vector::value_type item = items[next];
        if (keep(item))
        {
            items[kept++] = std::move(item);
        }
    }
    const size_t dropped = items.size() - kept;
    items.erase(items.begin() + kept, items.end());
    return dropped;
}

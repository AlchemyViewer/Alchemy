/**
 * @file alxuidocuments.cpp
 * @brief The XUI files a tool has open at once, each with its own edits.
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

#include "alxuidocuments.h"

#include <algorithm>

ALXUIDocuments::ALXUIDocuments() = default;
ALXUIDocuments::~ALXUIDocuments() = default;

ALXUIEdit* ALXUIDocuments::open(const std::string& path)
{
    mError.clear();
    if (path.empty())
    {
        return nullptr;
    }
    if (ALXUIEdit* already = find(path))
    {
        makeActive(path);
        return already;
    }

    auto held = std::make_unique<ALXUIEdit>();
    if (!held->loadFile(path))
    {
        mError = held->error();
        return nullptr;
    }
    ALXUIEdit* opened = held.get();
    mOpen.emplace(path, std::move(held));
    mPaths.push_back(path);
    makeActive(path);
    return opened;
}

ALXUIEdit* ALXUIDocuments::find(std::string_view path)
{
    const auto it = mOpen.find(path);
    return it == mOpen.end() ? nullptr : it->second.get();
}

const ALXUIEdit* ALXUIDocuments::find(std::string_view path) const
{
    const auto it = mOpen.find(path);
    return it == mOpen.end() ? nullptr : it->second.get();
}

const std::string* ALXUIDocuments::textFor(std::string_view path) const
{
    const ALXUIEdit* held = find(path);
    return held ? &held->text() : nullptr;
}

void ALXUIDocuments::makeActive(std::string_view path)
{
    if (ALXUIEdit* held = find(path))
    {
        mActivePath.assign(path);
        mActivePtr = held;
    }
}

bool ALXUIDocuments::close(std::string_view path)
{
    const auto it = mOpen.find(path);
    if (it == mOpen.end())
    {
        return false;
    }
    const bool was_active = mActivePtr == it->second.get();
    mOpen.erase(it);
    mPaths.erase(std::remove(mPaths.begin(), mPaths.end(), path), mPaths.end());
    if (was_active)
    {
        // Whatever is left, so that a tool with something open is never
        // holding nothing: the one opened before this is the one a person
        // was looking at before this.
        mActivePath.clear();
        mActivePtr = nullptr;
        if (!mPaths.empty())
        {
            makeActive(mPaths.back());
        }
    }
    return true;
}

void ALXUIDocuments::closeAll()
{
    mOpen.clear();
    mPaths.clear();
    mActivePath.clear();
    mActivePtr = nullptr;
}

S32 ALXUIDocuments::dirtyCount() const
{
    S32 count = 0;
    for (const auto& [path, held] : mOpen)
    {
        count += held->dirty() ? 1 : 0;
    }
    return count;
}

S32 ALXUIDocuments::saveAll()
{
    mError.clear();
    S32 written = 0;
    // In the order they were opened, so that the one named in an error is
    // the one a person would expect to hear about first.
    for (const std::string& path : mPaths)
    {
        ALXUIEdit* held = find(path);
        if (!held || !held->dirty())
        {
            continue;
        }
        if (!held->save())
        {
            mError = held->error();
            break;
        }
        ++written;
    }
    return written;
}

/**
 * @file altextureboost.cpp
 * @brief A holder of a texture's boost
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
#include "altextureboost.h"

#include "llviewertexture.h"

#include <utility>

ALTextureBoost::ALTextureBoost(LLViewerTexture* texture, S32 level)
{
    hold(texture, level);
}

ALTextureBoost::~ALTextureBoost()
{
    release();
}

ALTextureBoost::ALTextureBoost(ALTextureBoost&& other) noexcept
    : mTexture(std::move(other.mTexture))
    , mLevel(other.mLevel)
{
}

ALTextureBoost& ALTextureBoost::operator=(ALTextureBoost&& other) noexcept
{
    if (this != &other)
    {
        release();
        mTexture = std::move(other.mTexture);
        mLevel   = other.mLevel;
    }
    return *this;
}

void ALTextureBoost::hold(LLViewerTexture* texture, S32 level)
{
    if (texture == mTexture.get() && level == mLevel)
    {
        return;
    }
    // Referenced across the release, which lets go of mTexture: where only the level changes, the same texture, whose
    // last reference that may be.
    LLPointer<LLViewerTexture> held = texture;
    if (texture)
    {
        texture->addBoostHold(level);
    }
    release();
    mTexture = std::move(held);
    mLevel   = level;
}

void ALTextureBoost::release()
{
    if (mTexture.notNull())
    {
        mTexture->removeBoostHold(mLevel);
        mTexture = nullptr;
    }
}

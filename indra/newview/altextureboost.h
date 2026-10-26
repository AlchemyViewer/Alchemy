/**
 * @file altextureboost.h
 * @brief What a texture's boost is made of, and a holder of one
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

#include "llpointer.h"
#include "stdtypes.h"

#include <algorithm>
#include <vector>

class LLViewerTexture;

// What a texture's boost (LLGLTexture::EBoostLevel) is made of, the boost being the highest of them:
// - its own level: what the texture is for (a UI image, a bump or sculpt map, a baked texture), as whoever made it
//   for that says;
// - the levels holders keep on it while they hold it (ALTextureBoost), counted by holder: kept as a count a level,
//   so a level held by many (a texture on every face of a HUD) costs no more to hold, let go of or read than one;
// - a level asked for frame by frame, which lasts until a frame has passed without it.
class ALTextureBoostLevels
{
public:
    S32 level() const
    {
        S32 level = std::max({ mOwn, mFrame, mFrameBefore });
        for (const Held& held : mHeld)
        {
            level = std::max(level, held.mLevel);
        }
        return level;
    }

    S32  own() const { return mOwn; }
    void setOwn(S32 level) { mOwn = level; }

    void addHold(S32 level)
    {
        for (Held& held : mHeld)
        {
            if (held.mLevel == level)
            {
                ++held.mHolders;
                return;
            }
        }
        mHeld.push_back({ level, 1 });
    }
    // Lets go of one hold of `level`; returns false where none is held.
    bool removeHold(S32 level)
    {
        for (Held& held : mHeld)
        {
            if (held.mLevel == level)
            {
                if (--held.mHolders == 0)
                {
                    held = mHeld.back();
                    mHeld.pop_back();
                }
                return true;
            }
        }
        return false;
    }

    // Asks for `level` in `frame`. Asked for more than once in a frame, the highest stands; a level asked for in the
    // frame before stands beside it through this one, so an asker later in a frame than another is not lowered by it.
    void askForFrame(S32 level, U32 frame)
    {
        if (frame != mFrameAsked)
        {
            mFrameBefore = frame - mFrameAsked == 1 ? mFrame : 0;
            mFrame       = level;
            mFrameAsked  = frame;
        }
        else
        {
            mFrame = std::max(mFrame, level);
        }
    }

    // In `frame`, lets go of what was asked for before the frame before: a level asked for every frame lasts from its
    // asking through the next frame's readers, whatever comes first in a frame. Returns whether the frame's level
    // changed.
    bool lapse(U32 frame)
    {
        const S32 before = std::max(mFrame, mFrameBefore);
        if (frame - mFrameAsked >= 1)
        {
            mFrameBefore = 0;
        }
        if (frame - mFrameAsked >= 2)
        {
            mFrame = 0;
        }
        return std::max(mFrame, mFrameBefore) != before;
    }

private:
    struct Held
    {
        S32 mLevel;
        U32 mHolders;
    };

    S32               mOwn         = 0;
    S32               mFrame       = 0; // the highest asked for in mFrameAsked
    S32               mFrameBefore = 0; // the highest asked for in the frame before it
    U32               mFrameAsked  = 0;
    std::vector<Held> mHeld;        // one a level held
};

// A boost kept on a texture for as long as this holds it (LLViewerTexture::addBoostHold): by a window showing the
// texture while it is open, a HUD while it is worn.
class ALTextureBoost
{
public:
    ALTextureBoost() = default;
    ALTextureBoost(LLViewerTexture* texture, S32 level);
    ~ALTextureBoost();

    ALTextureBoost(ALTextureBoost&& other) noexcept;
    ALTextureBoost& operator=(ALTextureBoost&& other) noexcept;
    ALTextureBoost(const ALTextureBoost&)            = delete;
    ALTextureBoost& operator=(const ALTextureBoost&) = delete;

    // Holds `level` on `texture`, then lets go of what was held, so a texture held at the same level throughout keeps
    // its boost unchanged. A null texture holds nothing.
    void hold(LLViewerTexture* texture, S32 level);
    void release();

    LLViewerTexture* get() const { return mTexture.get(); }

private:
    LLPointer<LLViewerTexture> mTexture;
    S32                        mLevel = 0;
};

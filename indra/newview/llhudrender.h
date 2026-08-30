/**
 * @file llhudrender.h
 * @brief LLHUDRender class definition
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLHUDRENDER_H
#define LL_LLHUDRENDER_H

#include "llfontgl.h"
#include "llfonttextcache.h"

class LLVector3;
class LLFontGL;

#include "llrect.h"
#include "v3math.h"

// What a HUD-text draw needs is mostly per world position, not per line: the
// camera's pixel basis at that point, the viewport, and the 2D projection the
// font draws under. A nametag draws several lines at one position, and paid
// for all of that once per line.
//
// Construct one, ask whether the position is visible, then draw as many lines
// against it as that position carries. The matrices it pushes are held for its
// lifetime, so it is a scope: keep it no longer than the lines it draws.
class LLHUDTextScope
{
public:
    LLHUDTextScope(const LLVector3& pos_agent, bool orthographic);
    ~LLHUDTextScope();

    LLHUDTextScope(const LLHUDTextScope&) = delete;
    LLHUDTextScope& operator=(const LLHUDTextScope&) = delete;

    // False when the position is behind the camera. draw() is then a no-op,
    // so a caller with one line need not ask.
    bool visible() const { return mVisible; }

    void draw(std::string_view utf8text,
              const LLFontGL& font,
              const U8 style,
              const LLFontGL::ShadowType shadow,
              const F32 x_offset,
              const F32 y_offset,
              const LLColor4& color);

private:
    LLVector3 mPosAgent;
    LLVector3 mRightAxis;
    LLVector3 mUpAxis;
    LLRect    mWorldViewRect;
    bool      mVisible = false;
};

// One line at one position, for the callers that only ever draw one.
void hud_render_text(std::string_view utf8text,
                     const LLVector3 &pos_agent,
                     const LLFontGL &font,
                     const U8 style,
                     const LLFontGL::ShadowType,
                     const F32 x_offset,
                     const F32 y_offset,
                     const LLColor4& color,
                     const bool orthographic);


#endif //LL_LLHUDRENDER_H


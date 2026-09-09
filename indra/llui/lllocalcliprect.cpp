/**
* @file lllocalcliprect.cpp
*
* $LicenseInfo:firstyear=2009&license=viewerlgpl$
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
#include "linden_common.h"

#include "lllocalcliprect.h"

#include "llfontgl.h"
#include "llrender2dutils.h"
#include "llui.h"

/*static*/ std::stack<LLRect> LLScreenClipRect::sClipRectStack;


LLScreenClipRect::LLScreenClipRect(const LLRect& rect, bool enabled)
:   mScissorState(GL_SCISSOR_TEST),
    mEnabled(enabled)
{
    if (mEnabled)
    {
        pushClipRect(rect);
        mScissorState.setEnabled(true);
        updateScissorRegion();
    }
}

LLScreenClipRect::~LLScreenClipRect()
{
    if (mEnabled)
    {
        popClipRect();
        updateScissorRegion();
    }
}

//static
void LLScreenClipRect::pushClipRect(const LLRect& rect)
{
    LLRect combined_clip_rect = rect;
    if (!sClipRectStack.empty())
    {
        LLRect top = sClipRectStack.top();
        combined_clip_rect.intersectWith(top);

        if(combined_clip_rect.isEmpty())
        {
            // avoid artifacts where zero area rects show up as lines
            combined_clip_rect = LLRect::null;
        }
    }
    sClipRectStack.push(combined_clip_rect);
}

//static
void LLScreenClipRect::popClipRect()
{
    sClipRectStack.pop();
}

//static
void LLScreenClipRect::updateScissorRegion()
{
    if (sClipRectStack.empty()) return;

    // finish any deferred calls in the old clipping region
    gGL.flush();

    LLRect rect = sClipRectStack.top();
    stop_glerror();
    S32 x,y,w,h;
    x = llfloor(rect.mLeft * LLUI::getScaleFactor().mV[VX]);
    y = llfloor(rect.mBottom * LLUI::getScaleFactor().mV[VY]);
    // A rect with no area admits nothing. The pixel added to the others is the
    // one the rect's far edge names and does not contain; added to a rect that
    // names no pixels at all, it let a column of them through at the origin.
    w = rect.getWidth() > 0 ? llceil(rect.getWidth() * LLUI::getScaleFactor().mV[VX]) + 1 : 0;
    h = rect.getHeight() > 0 ? llceil(rect.getHeight() * LLUI::getScaleFactor().mV[VY]) + 1 : 0;
    glScissor( x,y,w,h );
    stop_glerror();
}

//---------------------------------------------------------------------------
// LLLocalClipRect
//---------------------------------------------------------------------------
// Without the scale a clip under one moved with the drawing's offset and kept
// the drawing's old size, so the scissor and the thing it was meant to cut
// ended up in different places.
LLLocalClipRect::LLLocalClipRect(const LLRect& rect, bool enabled /* = true */)
:   LLScreenClipRect(LLRender2D::toScreen(rect), enabled)
{}

LLLocalClipRect::~LLLocalClipRect()
{}

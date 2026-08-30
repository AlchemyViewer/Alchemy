/**
 * @file alcachedlabel.h
 * @brief A text vertex buffer that invalidates itself when its text changes.
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

#ifndef LL_ALCACHEDLABEL_H
#define LL_ALCACHEDLABEL_H

#include "llfontvertexbuffer.h"

#include <string>
#include <string_view>

// LLFontVertexBuffer notices every rendering parameter that changes except the
// one the caller owns: the text. A widget drawing a label it never edits can
// take the buffer directly. A widget whose label is rewritten from several
// places -- a menu item's, rebuilt by enable signals, control variables, and
// branch and accelerator suffixes -- cannot, because one mutation site that
// forgets to invalidate leaves stale text on screen and nothing catches it.
//
// This keys the buffer on the text it last drew. The copy is paid once per
// change; per frame the cost is a comparison of two short strings against the
// shaping and geometry pass it avoids.
class ALCachedLabel
{
public:
    // The buffer to draw `text` with, invalidated first if the text moved.
    LLFontVertexBuffer& forText(std::string_view text)
    {
        if (text != mDrawn)
        {
            mDrawn.assign(text);
            mBuffer.reset();
        }
        return mBuffer;
    }

    // For the changes the buffer cannot see and the text does not describe:
    // going invisible, and losing the GL resources behind it.
    void reset()
    {
        mDrawn.clear();
        mBuffer.reset();
    }

private:
    LLFontVertexBuffer mBuffer;
    std::string        mDrawn;
};

#endif // LL_ALCACHEDLABEL_H

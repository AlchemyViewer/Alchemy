/**
 * @file alplace.cpp
 * @brief Where a box goes beside something: under or over it, or beside it.
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

#include "alplace.h"

#include "llmath.h"

namespace
{
    LLRect underOrOver(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap, bool under_first)
    {
        const S32    x = llclamp(anchor.mLeft, bounds.mLeft, llmax(bounds.mLeft, bounds.mRight - width));
        const LLRect under(x, anchor.mBottom - gap, x + width, anchor.mBottom - gap - height);
        const LLRect over(x, anchor.mTop + gap + height, x + width, anchor.mTop + gap);
        const bool   under_fits = under.mBottom >= bounds.mBottom;
        const bool   over_fits  = over.mTop <= bounds.mTop;
        if (under_first)
        {
            return (under_fits || !over_fits) ? under : over;
        }
        return (over_fits || !under_fits) ? over : under;
    }
}

// static
LLRect ALPlace::under(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap)
{
    return underOrOver(anchor, width, height, bounds, gap, true);
}

// static
LLRect ALPlace::over(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap)
{
    return underOrOver(anchor, width, height, bounds, gap, false);
}

// static
LLRect ALPlace::beside(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap)
{
    LLRect rect;
    if (anchor.mRight + gap + width <= bounds.mRight)
    {
        rect = LLRect(anchor.mRight + gap, anchor.mTop, anchor.mRight + gap + width, anchor.mTop - height);
    }
    else if (anchor.mLeft - gap - width >= bounds.mLeft)
    {
        rect = LLRect(anchor.mLeft - gap - width, anchor.mTop, anchor.mLeft - gap, anchor.mTop - height);
    }
    else
    {
        rect = under(anchor, width, height, bounds, gap);
    }
    if (rect.mBottom < bounds.mBottom)
    {
        rect.translate(0, bounds.mBottom - rect.mBottom);
    }
    if (rect.mTop > bounds.mTop)
    {
        rect.translate(0, bounds.mTop - rect.mTop);
    }
    return rect;
}

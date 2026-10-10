/**
 * @file alhoverglow.cpp
 * @brief The glow of what the pointer is over, fading in and out.
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

#include "alhoverglow.h"

#include "alselectionoutline.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "pipeline.h"

void ALHoverGlow::addTo(ALSelectionOutline& outline) const
{
    for (const Glow& glow : mGlows)
    {
        LLViewerObject* object = gObjectList.findObject(glow.mID);
        if (!object || object->isDead() || object->isHUDAttachment())
        {
            continue;
        }
        LLColor4 colour = LLPipeline::RenderHighlightColor;
        colour.mV[VALPHA] *= glow.mFade;
        // Every face: the TE mask holds more than a volume has.
        outline.add(object, 0xFFFFFFFF, colour, false, ALSelectionOutline::PRIORITY_HOVER, 0);
    }
}

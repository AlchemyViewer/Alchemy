/**
 * @file alsurface.cpp
 * @brief The one look every small thing floating over the editor's text wears.
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

#include "alsurface.h"

#include "llrender2dutils.h"
#include "lluicolortable.h"

namespace ALSurface
{
    LLColor4 shade(const LLColor4& paper, const LLColor4& ink, F32 amount)
    {
        LLColor4 mixed;
        for (S32 i = 0; i < 3; ++i)
        {
            mixed.mV[i] = paper.mV[i] + (ink.mV[i] - paper.mV[i]) * amount;
        }
        // Opaque whatever the two were: a surface is held over text, and
        // the text behind it is exactly what it must not show.
        mixed.mV[VALPHA] = 1.f;
        return mixed;
    }

    LLColor4 frame(const LLColor4& ink, F32 alpha)
    {
        return ink % (FRAME * alpha);
    }

    void draw(const LLRect& rect, const LLColor4& paper, const LLColor4& ink, F32 alpha)
    {
        gl_rect_2d(rect, ground(paper, ink) % alpha, true);
        gl_rect_2d(rect, frame(ink, alpha), false);
    }

    const LLUIColor& well()
    {
        static const LLUIColor color = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
        return color;
    }

    const LLUIColor& rim()
    {
        static const LLUIColor color = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
        return color;
    }

    const LLUIColor& handle()
    {
        static const LLUIColor color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
        return color;
    }

    const LLUIColor& text()
    {
        static const LLUIColor color = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
        return color;
    }

    const LLUIColor& quiet()
    {
        static const LLUIColor color = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
        return color;
    }
}

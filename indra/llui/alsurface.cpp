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

    namespace
    {
        // A channel of an sRGB colour as light, and a colour's luminance
        // from those, as WCAG 2 weighs them.
        F32 linear(F32 channel)
        {
            return channel <= 0.03928f ? channel / 12.92f : powf((channel + 0.055f) / 1.055f, 2.4f);
        }
        F32 luminance(const LLColor4& color)
        {
            return 0.2126f * linear(color.mV[VRED]) + 0.7152f * linear(color.mV[VGREEN]) + 0.0722f * linear(color.mV[VBLUE]);
        }
    }

    F32 contrast(const LLColor4& ink, const LLColor4& paper)
    {
        const F32 alpha = llclamp(ink.mV[VALPHA], 0.f, 1.f);
        LLColor4  seen;
        for (S32 i = 0; i < 3; ++i)
        {
            seen.mV[i] = ink.mV[i] * alpha + paper.mV[i] * (1.f - alpha);
        }
        const F32 a = luminance(seen);
        const F32 b = luminance(paper);
        return (llmax(a, b) + 0.05f) / (llmin(a, b) + 0.05f);
    }

    LLColor4 legible(const LLColor4& ink, const LLColor4& wanted, const LLColor4& paper, F32 least)
    {
        if (contrast(wanted, paper) >= least)
        {
            return wanted;
        }
        // How far towards `wanted` it may go, found by halving: the
        // contrast falls along the way, since `wanted` reads less.
        F32 lo = 0.f, hi = 1.f;
        for (S32 i = 0; i < 16; ++i)
        {
            const F32 mid = (lo + hi) / 2.f;
            (contrast(lerp(ink, wanted, mid), paper) >= least ? lo : hi) = mid;
        }
        return lerp(ink, wanted, lo);
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

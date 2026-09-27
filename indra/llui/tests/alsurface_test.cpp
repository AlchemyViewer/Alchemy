/**
 * @file alsurface_test.cpp
 * @brief The surfaces our controls are drawn with: the recipe from paper and ink, and the skin's colours by part.
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

#include "../alsurface.h"

#include "../lluicolortable.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <string>

class LLAvatarName;
const std::string gSurfaceTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSurfaceTestAnonName;
}

namespace tut
{
    struct alsurface_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static bool nearest(const LLColor4& a, const LLColor4& b)
        {
            for (S32 i = 0; i < 4; ++i)
            {
                if (std::fabs(a.mV[i] - b.mV[i]) > 0.0001f)
                {
                    return false;
                }
            }
            return true;
        }
    };
    typedef test_group<alsurface_data> alsurface_group;
    typedef alsurface_group::object    alsurface_object;
    alsurface_group                    alsurface_test_group("alsurface");

    template<> template<>
    void alsurface_object::test<1>()
    {
        set_test_name("a surface is the paper carried towards the ink and opaque; the chosen band the same at its own weight");
        const LLColor4 paper(0.1f, 0.2f, 0.3f, 0.5f);
        const LLColor4 ink(0.9f, 0.8f, 0.7f, 1.f);
        ensure("none of the way is the paper, opaque", nearest(ALSurface::shade(paper, ink, 0.f), LLColor4(0.1f, 0.2f, 0.3f, 1.f)));
        ensure("all of it the ink", nearest(ALSurface::shade(paper, ink, 1.f), LLColor4(0.9f, 0.8f, 0.7f, 1.f)));
        ensure("the ground its own way", nearest(ALSurface::ground(paper, ink), ALSurface::shade(paper, ink, ALSurface::GROUND)));
        ensure("the chosen band its own", nearest(ALSurface::chosen(paper, ink), ALSurface::shade(paper, ink, ALSurface::CHOSEN)));
        ensure("the frame the ink thinned", nearest(ALSurface::frame(ink, 0.5f), LLColor4(0.9f, 0.8f, 0.7f, ALSurface::FRAME * 0.5f)));

        // The ink at the chosen weight, laid over the paper as a draw lays
        // it, comes to the band a list with paper of its own is given.
        const LLColor4 over = ALSurface::chosenOver(ink);
        LLColor4       laid;
        for (S32 i = 0; i < 3; ++i)
        {
            laid.mV[i] = paper.mV[i] * (1.f - over.mV[VALPHA]) + over.mV[i] * over.mV[VALPHA];
        }
        laid.mV[VALPHA] = 1.f;
        ensure("the same band over any paper", nearest(laid, ALSurface::chosen(paper, ink)));
    }

    template<> template<>
    void alsurface_object::test<2>()
    {
        set_test_name("each part a control plays is drawn in the skin's colour for it");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // The skin's, told here before any part is first asked for: nothing
        // loads a skin's colours in a test.
        LLUIColorTable& table = LLUIColorTable::instance();
        table.setColor("DefaultShadowLight", LLColor4(0.1f, 0.1f, 0.1f, 1.f));
        table.setColor("LabelDisabledColor", LLColor4(0.2f, 0.3f, 0.4f, 1.f));
        table.setColor("EmphasisColor", LLColor4(0.9f, 0.6f, 0.1f, 1.f));
        table.setColor("LabelTextColor", LLColor4(0.8f, 0.8f, 0.9f, 1.f));
        ensure("the well", nearest(ALSurface::well().get(), LLColor4(0.1f, 0.1f, 0.1f, 1.f)));
        ensure("the rim", nearest(ALSurface::rim().get(), LLColor4(0.2f, 0.3f, 0.4f, 1.f)));
        ensure("the handle", nearest(ALSurface::handle().get(), LLColor4(0.9f, 0.6f, 0.1f, 1.f)));
        ensure("the text", nearest(ALSurface::text().get(), LLColor4(0.8f, 0.8f, 0.9f, 1.f)));
        ensure("the quiet words", nearest(ALSurface::quiet().get(), LLColor4(0.2f, 0.3f, 0.4f, 1.f)));
        // And they follow a theme that changes after.
        table.setColor("EmphasisColor", LLColor4(0.1f, 0.6f, 0.9f, 1.f));
        ensure("followed", nearest(ALSurface::handle().get(), LLColor4(0.1f, 0.6f, 0.9f, 1.f)));
    }
}

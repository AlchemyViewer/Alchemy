/**
 * @file llfloaterwebprofile.cpp
 * @brief Web profile floater.
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

#include "llviewerprecompiledheaders.h"

#include "llfloaterwebprofile.h"

#include "llviewercontrol.h"

LLFloaterWebProfile::LLFloaterWebProfile(const Params& key) :
    LLFloaterWebContent(key)
{
}

void LLFloaterWebProfile::onOpen(const LLSD& key)
{
    Params p(key);
    p.show_chrome(true);
    p.window_class("web_content");
    p.allow_address_entry(false);
    p.trusted_content(true);
    LLFloaterWebContent::onOpen(p);
    applyPreferredRect();
}

// virtual
void LLFloaterWebProfile::handleReshape(const LLRect& new_rect, bool by_user)
{
    LL_DEBUGS() << "handleReshape: " << new_rect << LL_ENDL;

    if (by_user && !isMinimized())
    {
        LL_DEBUGS() << "Storing new rect" << LL_ENDL;
        gSavedSettings.setRect("WebProfileFloaterRect", new_rect);
    }

    LLFloaterWebContent::handleReshape(new_rect, by_user);
}

LLFloater* LLFloaterWebProfile::create(const LLSD& key)
{
    LLFloaterWebContent::Params p(key);
    preCreate(p);
    return new LLFloaterWebProfile(p);
}

void LLFloaterWebProfile::applyPreferredRect()
{
    const LLRect preferred_rect = gSavedSettings.getRect("WebProfileFloaterRect");
    LL_DEBUGS() << "Applying preferred rect: " << preferred_rect << LL_ENDL;

    // Don't override position that may have been set by floater stacking code.
    LLRect new_rect = getRect();
    new_rect.setLeftTopAndSize(
        new_rect.mLeft, new_rect.mTop,
        preferred_rect.getWidth(), preferred_rect.getHeight());
    setShape(new_rect);
}

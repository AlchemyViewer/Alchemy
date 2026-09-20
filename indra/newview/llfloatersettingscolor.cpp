/**
* @file llfloatersettingscolor.cpp
* @brief Implementation of LLFloaterSettingsColor
* @author Rye Cogtail<rye@alchemyviewer.org>
*
* $LicenseInfo:firstyear=2024&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2024, Linden Research, Inc.
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

#include "llfloatersettingscolor.h"

#include "alcolortablepanel.h"
#include "lluicolortable.h"

LLFloaterSettingsColor::LLFloaterSettingsColor(const LLSD& key)
:   LLFloater(key)
{
}

LLFloaterSettingsColor::~LLFloaterSettingsColor()
{
}

void LLFloaterSettingsColor::onClose(bool app_quitting)
{
    // The viewer writes them itself on the way out.
    if (!app_quitting)
    {
        LLUIColorTable::instance().saveUserSettings();
    }
}

bool LLFloaterSettingsColor::postBuild()
{
    enableResizeCtrls(true, false, true);
    mColors = getChild<ALColorTablePanel>("colors");

    return LLFloater::postBuild();
}

void LLFloaterSettingsColor::onOpen(const LLSD& key)
{
    if (key.isMap() && key.has("name") && mColors)
    {
        mColors->showColor(key["name"].asString());
    }
}

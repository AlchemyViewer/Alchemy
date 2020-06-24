/**
 * @file alpanelquicksettingspulldown.cpp
 * @brief Quick Settings popdown panel
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2013-2014, Alchemy Viewer Project.
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alpanelquicksettingspulldown.h"

///----------------------------------------------------------------------------
/// Class ALPanelQuickSettingsPulldown
///----------------------------------------------------------------------------

// Default constructor
ALPanelQuickSettingsPulldown::ALPanelQuickSettingsPulldown() : LLPanelPulldown()
{
	buildFromFile("panel_quick_settings_pulldown.xml");
}

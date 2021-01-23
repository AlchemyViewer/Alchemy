/**
* @file alcinematicmode.cpp
* @brief Cinematic UI Mode
*
* $LicenseInfo:firstyear=2017&license=viewerlgpl$
* Copyright (C) 2017-2020 XenHat <me@xenh.at>
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
* $/LicenseInfo$
**/

#include "llviewerprecompiledheaders.h"

#include "alcinematicmode.h"
#include "llviewerwindow.h"
#include "llmoveview.h"
#include "llnavigationbar.h"
#include "llchicletbar.h"
#include "llhudtext.h"
#include <llagentcamera.h>

bool ALCinematicMode::_enabled = false;

// static
void ALCinematicMode::toggle()
{
	if (gAgentCamera.getCameraMode() != CAMERA_MODE_MOUSELOOK)
	{
		LL_INFOS() << "Toggling Cinematic Mode" << LL_ENDL;
		// Ordered to have a nice effect
		if (_enabled)
		{
			// Showing elements again
			LLChicletBar::getInstance()->setVisible(TRUE);
			LLPanelStandStopFlying::getInstance()->setVisible(TRUE); // FIXME: that doesn't always work
			LLNavigationBar::getInstance()->setVisible(TRUE);
			gViewerWindow->setUIVisibility(TRUE);
		}
		else
		{
			// Hiding Elements
			gViewerWindow->setUIVisibility(FALSE);
			LLNavigationBar::getInstance()->setVisible(FALSE);
			LLPanelStandStopFlying::getInstance()->setVisible(FALSE); // FIXME: that doesn't always work
			LLChicletBar::getInstance()->setVisible(FALSE);
		}
		_enabled = !_enabled;
		LLHUDText::refreshAllObjectText();
	}
}

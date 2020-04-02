/** 
 * @file lltoolgun.cpp
 * @brief LLToolGun class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "lltoolgun.h"

#include "llviewerwindow.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewercontrol.h"
#include "llsky.h"
#include "llappviewer.h"
#include "llresmgr.h"
#include "llfontgl.h"
#include "llui.h"
#include "llviewertexturelist.h"
#include "llviewercamera.h"
// [RLVa:KB] - Checked: 2014-02-24 (RLVa-1.4.10)
#include "llfocusmgr.h"
// [/RLVa:KB]
#include "llhudmanager.h"
#include "lltoolmgr.h"
#include "lltoolgrab.h"
#include "lluiimage.h"
// Linden library includes
#include "llwindow.h"			// setMouseClipping()

//#include "alavatarcolormgr.h"
#include "llavatarnamecache.h"
#include "llnetmap.h"
#include "llworld.h"

LLToolGun::LLToolGun( LLToolComposite* composite )
:	LLTool( std::string("gun"), composite ),
		mIsSelected(FALSE)
{
	mCrosshairp = LLUI::getUIImage("crosshairs.tga");
}

void LLToolGun::handleSelect()
{
// [RLVa:KB] - Checked: 2014-02-24 (RLVa-1.4.10)
	if (gFocusMgr.getAppHasFocus())
	{
// [/RLVa:KB]
		gViewerWindow->hideCursor();
		gViewerWindow->moveCursorToCenter();
		gViewerWindow->getWindow()->setMouseClipping(TRUE);
		mIsSelected = TRUE;
// [RLVa:KB] - Checked: 2014-02-24 (RLVa-1.4.10)
	}
// [/RLVa:KB]
}

void LLToolGun::handleDeselect()
{
	gViewerWindow->moveCursorToCenter();
	gViewerWindow->showCursor();
	gViewerWindow->getWindow()->setMouseClipping(FALSE);
	mIsSelected = FALSE;
}

BOOL LLToolGun::handleMouseDown(S32 x, S32 y, MASK mask)
{
	gGrabTransientTool = this;
	LLToolMgr::getInstance()->getCurrentToolset()->selectTool( LLToolGrab::getInstance() );

	return LLToolGrab::getInstance()->handleMouseDown(x, y, mask);
}

BOOL LLToolGun::handleHover(S32 x, S32 y, MASK mask) 
{
	if( gAgentCamera.cameraMouselook() && mIsSelected )
	{
		const F32 NOMINAL_MOUSE_SENSITIVITY = 0.0025f;

		static LLCachedControl<F32> mouse_sensitivity_setting(gSavedSettings, "MouseSensitivity");
		F32 mouse_sensitivity = clamp_rescale(mouse_sensitivity_setting, 0.f, 15.f, 0.5f, 2.75f) * NOMINAL_MOUSE_SENSITIVITY;

		// ...move the view with the mouse

		// get mouse movement delta
		S32 dx = -gViewerWindow->getCurrentMouseDX();
		S32 dy = -gViewerWindow->getCurrentMouseDY();
		
		if (dx != 0 || dy != 0)
		{
			// ...actually moved off center
			const F32 fov = LLViewerCamera::getInstance()->getView() / DEFAULT_FIELD_OF_VIEW;
			static LLCachedControl<bool> invert_mouse(gSavedSettings, "InvertMouse");
			if (invert_mouse)
			{
				gAgent.pitch(mouse_sensitivity * fov * -dy);
			}
			else
			{
				gAgent.pitch(mouse_sensitivity * fov * dy);
			}
			LLVector3 skyward = gAgent.getReferenceUpVector();
			gAgent.rotate(mouse_sensitivity * fov * dx, skyward.mV[VX], skyward.mV[VY], skyward.mV[VZ]);

			static LLCachedControl<bool> mouse_sun(gSavedSettings, "MouseSun");
			if (mouse_sun)
			{
                LLVector3 sunpos = LLViewerCamera::getInstance()->getAtAxis();
				gSky.setSunDirectionCFR(sunpos);
				gSavedSettings.setVector3("SkySunDefaultPosition", LLViewerCamera::getInstance()->getAtAxis());
			}

			static LLCachedControl<bool> mouse_moon(gSavedSettings, "MouseMoon");
            if (mouse_moon)
			{
                LLVector3 moonpos = LLViewerCamera::getInstance()->getAtAxis();
				gSky.setMoonDirectionCFR(moonpos);
				gSavedSettings.setVector3("SkyMoonDefaultPosition", LLViewerCamera::getInstance()->getAtAxis());
			}

			gViewerWindow->moveCursorToCenter();
			gViewerWindow->hideCursor();
		}

		LL_DEBUGS("UserInput") << "hover handled by LLToolGun (mouselook)" << LL_ENDL;
	}
	else
	{
		LL_DEBUGS("UserInput") << "hover handled by LLToolGun (not mouselook)" << LL_ENDL;
	}

	// HACK to avoid assert: error checking system makes sure that the cursor is set during every handleHover.  This is actually a no-op since the cursor is hidden.
	gViewerWindow->setCursor(UI_CURSOR_ARROW);  

	return TRUE;
}

void LLToolGun::draw()
{
	static LLCachedControl<bool> show_crosshairs(gSavedSettings, "ShowCrosshairs");
	static LLCachedControl<bool> show_iff(gSavedSettings, "AlchemyMouselookIFF", true);
	static LLCachedControl<F32> iff_range(gSavedSettings, "AlchemyMouselookIFFRange", 380.f);
	if (show_crosshairs)
	{
		const S32 windowWidth = gViewerWindow->getWorldViewRectScaled().getWidth();
		const S32 windowHeight = gViewerWindow->getWorldViewRectScaled().getHeight();
		LLColor4 targetColor = LLColor4::white;
		targetColor.mV[VALPHA] = 0.5f;
		if (show_iff)
		{
			LLVector3d myPosition = gAgentCamera.getCameraPositionGlobal();
			LLQuaternion myRotation = LLViewerCamera::getInstance()->getQuaternion();
			myRotation.set(-myRotation.mQ[VX], -myRotation.mQ[VY], -myRotation.mQ[VZ], myRotation.mQ[VW]);

			LLWorld::pos_map_t positions;
			LLWorld::getInstance()->getAvatars(&positions, gAgent.getPositionGlobal(), iff_range);
			for (const auto& position : positions)
            {
				const auto& id = position.first;
				const auto& targetPosition = position.second;
				if (id == gAgentID || targetPosition.isNull())
				{
					continue;
				}

				LLVector3d magicVector = (targetPosition - myPosition) * myRotation;
				magicVector.setVec(-magicVector.mdV[VY], magicVector.mdV[VZ], magicVector.mdV[VX]);
				if (magicVector.mdV[VX] > -0.75 && magicVector.mdV[VX] < 0.75 && magicVector.mdV[VZ] > 0.0 && magicVector.mdV[VY] > -1.5 && magicVector.mdV[VY] < 1.5) // Do not fuck with these, cheater. :(
				{
					LLAvatarName avatarName;
					LLAvatarNameCache::get(id, &avatarName);
					//targetColor = ALAvatarColorMgr::instance().getColor(id);
					targetColor.mV[VALPHA] = 0.5f;
					LLFontGL::getFontSansSerifBold()->renderUTF8(
						llformat("%s : %.2fm", avatarName.getCompleteName().c_str(), (targetPosition - myPosition).magVec()),
						0, (windowWidth / 2.f), (windowHeight / 2.f) - 25.f, targetColor,
						LLFontGL::HCENTER, LLFontGL::TOP, LLFontGL::BOLD, LLFontGL::NO_SHADOW
						);

					break;
				}
			}
		}

		mCrosshairp->draw(
			(windowWidth - mCrosshairp->getWidth()) / 2,
			(windowHeight - mCrosshairp->getHeight()) / 2, targetColor);
	}
}

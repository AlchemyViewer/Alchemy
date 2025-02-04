/**
 * @file llfloaterprofiletexture.cpp
 * @brief LLFloaterProfileTexture class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#include "llfloaterprofiletexture.h"

#include "llbutton.h"
#include "llfloaterreg.h"
#include "llpreview.h" // for constants
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewertexture.h"


//////////////////////////////////////////////////////////////////////////
// LLFloaterProfileTexture
 //////////////////////////////////////////////////////////////////////////

LLFloaterProfileTexture::LLFloaterProfileTexture(LLView* owner)
    : LLFloater(LLSD())
    , mLastHeight(0)
    , mLastWidth(0)
    , mOwnerHandle(owner->getHandle())
    , mContextConeOpacity(0.f)
    , mCloseButton(NULL)
    , mProfileIcon(NULL)
{
    buildFromFile("floater_profile_texture.xml");
}

LLFloaterProfileTexture::~LLFloaterProfileTexture()
{
}

// virtual
BOOL LLFloaterProfileTexture::postBuild()
{
    mProfileIcon = getChild<LLProfileImageCtrl>("profile_pic");
    mProfileIcon->setImageLoadedCallback([this](BOOL success, LLViewerFetchedTexture* imagep) {onImageLoaded(success, imagep); });

    mCloseButton = getChild<LLButton>("close_btn");
    mCloseButton->setCommitCallback([this](LLUICtrl*, void*) { closeFloater(); }, nullptr);

    return TRUE;
}

// virtual
void LLFloaterProfileTexture::reshape(S32 width, S32 height, BOOL called_from_parent)
{
    LLFloater::reshape(width, height, called_from_parent);
}

// It takes a while until we get height and width information.
// When we receive it, reshape the window accordingly.
void LLFloaterProfileTexture::updateDimensions()
{
    LLPointer<LLViewerFetchedTexture> image = mProfileIcon->getImage();
    if (image.isNull())
    {
        return;
    }
    if ((image->getFullWidth() * image->getFullHeight()) == 0)
    {
        return;
    }

    S32 img_width = image->getFullWidth();
    S32 img_height = image->getFullHeight();

    mLastHeight = img_height;
    mLastWidth = img_width;

    LLRect old_floater_rect = getRect();
    LLRect old_image_rect = mProfileIcon->getRect();
    S32 width = old_floater_rect.getWidth() - old_image_rect.getWidth() + mLastWidth;
    S32 height = old_floater_rect.getHeight() - old_image_rect.getHeight() + mLastHeight;

    const F32 MAX_DIMENTIONS = 1024; // most profiles are supposed to be 256x256

    S32 biggest_dim = llmax(width, height);
    if (biggest_dim > MAX_DIMENTIONS)
    {
        F32 scale_down = MAX_DIMENTIONS / (F32)biggest_dim;
        width *= scale_down;
        height *= scale_down;
    }

    //reshape floater
    reshape(width, height);

    gFloaterView->adjustToFitScreen(this, FALSE);
}

void LLFloaterProfileTexture::draw()
{
    // drawFrustum
    LLView *owner = mOwnerHandle.get();
    static LLCachedControl<F32> max_opacity(gSavedSettings, "PickerContextOpacity", 0.4f);
    drawConeToOwner(mContextConeOpacity, max_opacity, owner);

    LLFloater::draw();
}

void LLFloaterProfileTexture::onOpen(const LLSD& key)
{
    mCloseButton->setFocus(true);
}

void LLFloaterProfileTexture::resetAsset()
{
    mProfileIcon->setValue(LLUUID::null);
}
void LLFloaterProfileTexture::loadAsset(const LLUUID &image_id)
{
    mProfileIcon->setValue(image_id);
    updateDimensions();
}

void LLFloaterProfileTexture::onImageLoaded(BOOL success, LLViewerFetchedTexture* imagep)
{
    if (success)
    {
        updateDimensions();
    }
}

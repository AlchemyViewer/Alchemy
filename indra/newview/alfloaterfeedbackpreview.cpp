/**
 * @file alfloaterfeedbackpreview.cpp
 * @brief One feedback attachment as it will be sent, to read before sending
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

#include "alfloaterfeedbackpreview.h"

#include "altextview.h"
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "llrender2dutils.h"
#include "llviewertexture.h"

ALFloaterFeedbackPreview::ALFloaterFeedbackPreview(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterFeedbackPreview::postBuild()
{
    mText = getChild<ALTextView>("text");
    mFixedFont = mText->getFont();
    return true;
}

void ALFloaterFeedbackPreview::draw()
{
    LLFloater::draw();

    if (mImage && !isMinimized())
    {
        // The picture, whole, as large as the floater lets it be.
        constexpr S32 MARGIN = 4;
        const LLRect rect = getLocalRect();
        const S32 room_width = rect.getWidth() - 2 * MARGIN;
        const S32 room_height = rect.getHeight() - getHeaderHeight() - 2 * MARGIN;
        if (room_width <= 0 || room_height <= 0)
        {
            return;
        }
        const F32 scale = std::min(static_cast<F32>(room_width) / mImageWidth,
                                   static_cast<F32>(room_height) / mImageHeight);
        const S32 width = ll_round(mImageWidth * scale);
        const S32 height = ll_round(mImageHeight * scale);
        const S32 x = MARGIN + (room_width - width) / 2;
        const S32 y = MARGIN + (room_height - height) / 2;
        gl_draw_scaled_image(x, y, width, height, mImage);
    }
}

// static
ALFloaterFeedbackPreview* ALFloaterFeedbackPreview::showWithText(const std::string& title, std::string_view text)
{
    ALFloaterFeedbackPreview* floater = LLFloaterReg::showTypedInstance<ALFloaterFeedbackPreview>("feedback_preview",
                                                                                               LLSD(), true);
    if (!floater)
    {
        return nullptr;
    }
    floater->setTitle(title);
    floater->mImage = nullptr;
    floater->mText->setVisible(true);
    floater->mText->setText(text);
    return floater;
}

// static
void ALFloaterFeedbackPreview::showText(const std::string& title, std::string_view text)
{
    if (ALFloaterFeedbackPreview* floater = showWithText(title, text))
    {
        floater->mText->setFont(floater->mFixedFont);
    }
}

// static
void ALFloaterFeedbackPreview::showProse(const std::string& title, std::string_view text)
{
    if (ALFloaterFeedbackPreview* floater = showWithText(title, text))
    {
        floater->mText->setFont(LLFontGL::getFontSansSerif());
    }
}

// static
void ALFloaterFeedbackPreview::showImage(const std::string& title, const LLPointer<LLImageRaw>& image)
{
    ALFloaterFeedbackPreview* floater = LLFloaterReg::showTypedInstance<ALFloaterFeedbackPreview>("feedback_preview",
                                                                                               LLSD(), true);
    if (!floater || image.isNull())
    {
        return;
    }
    floater->setTitle(title);
    floater->mText->setVisible(false);
    floater->mImage = LLViewerTextureManager::getLocalTexture(image.get(), false);
    floater->mImageWidth = image->getWidth();
    floater->mImageHeight = image->getHeight();
}

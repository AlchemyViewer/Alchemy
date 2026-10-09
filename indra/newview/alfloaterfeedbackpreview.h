/**
 * @file alfloaterfeedbackpreview.h
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

#ifndef AL_ALFLOATERFEEDBACKPREVIEW_H
#define AL_ALFLOATERFEEDBACKPREVIEW_H

#include "llfloater.h"
#include "llimage.h"

#include <string_view>

class ALTextView;
class LLViewerTexture;

class ALFloaterFeedbackPreview final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterFeedbackPreview, LLFloater);

    explicit ALFloaterFeedbackPreview(const LLSD& key);

    bool postBuild() override;
    void draw() override;

    // An attachment's text, in the fixed-width face a log reads in.
    static void showText(const std::string& title, std::string_view text);
    // Words to read, in the interface's own face.
    static void showProse(const std::string& title, std::string_view text);
    static void showImage(const std::string& title, const LLPointer<LLImageRaw>& image);

private:
    ~ALFloaterFeedbackPreview() override = default;

    static ALFloaterFeedbackPreview* showWithText(const std::string& title, std::string_view text);

    ALTextView* mText = nullptr;
    const LLFontGL* mFixedFont = nullptr;
    LLPointer<LLViewerTexture> mImage;
    S32 mImageWidth = 0;
    S32 mImageHeight = 0;
};

#endif // AL_ALFLOATERFEEDBACKPREVIEW_H

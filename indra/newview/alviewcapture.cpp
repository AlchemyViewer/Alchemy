/**
 * @file alviewcapture.cpp
 * @brief A rect of the screen as a picture, and that picture written to a file
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

#include "alviewcapture.h"

#include "lldir.h"
#include "llimagebmp.h"
#include "llimagej2c.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerwindow.h"

ALViewCapture::Outcome ALViewCapture::captureScreenRect(const LLRect& screen, LLPointer<LLImageRaw>& out)
{
    const S32 window_width = gViewerWindow->getWindowWidthRaw();
    const S32 window_height = gViewerWindow->getWindowHeightRaw();
    LLPointer<LLImageRaw> shot = new LLImageRaw;

    if (!gViewerWindow->rawSnapshot(shot, window_width, window_height, /*keep_window_aspect=*/true,
                                    /*is_texture=*/false, /*show_ui=*/true, /*show_hud=*/false))
    {
        return Outcome::NoSnapshot;
    }

    // The rect in the window, in the snapshot's own scale: a snapshot may
    // come back at a different size than the window.
    const F32 scale_x = (F32)shot->getWidth() / (F32)llmax(1, gViewerWindow->getWindowWidthScaled());
    const F32 scale_y = (F32)shot->getHeight() / (F32)llmax(1, gViewerWindow->getWindowHeightScaled());
    const S32 left = llclamp((S32)(screen.mLeft * scale_x), 0, shot->getWidth());
    const S32 right = llclamp((S32)(screen.mRight * scale_x), left, shot->getWidth());
    const S32 bottom = llclamp((S32)(screen.mBottom * scale_y), 0, shot->getHeight());
    const S32 top = llclamp((S32)(screen.mTop * scale_y), bottom, shot->getHeight());
    const S32 width = right - left;
    const S32 height = top - bottom;

    if (width <= 0 || height <= 0)
    {
        return Outcome::OffScreen;
    }

    // Row zero of the snapshot is the bottom of the window, which is
    // where the rect's bottom is too.
    const U8 components = shot->getComponents();
    LLPointer<LLImageRaw> cropped = new LLImageRaw(width, height, components);

    for (S32 row = 0; row < height; ++row)
    {
        memcpy(cropped->getData() + (size_t)row * width * components,
               shot->getData() + ((size_t)(bottom + row) * shot->getWidth() + left) * components,
               (size_t)width * components);
    }

    out = cropped;

    return Outcome::Captured;
}

// The file and the format are one question: the extension the person
// types is what the picture is written as.
void ALViewCapture::saveThroughPicker(LLPointer<LLImageRaw> image, const std::string& suggested_name, said_t said)
{
    LLFilePickerReplyThread::startPicker([image, said = std::move(said)](const std::vector<std::string>& filenames,
                                                                          LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter)
        {
            if (filenames.empty() || image.isNull())
            {
                return;
            }

            std::string path = filenames.front();
            std::string extension = gDirUtilp->getExtension(path);

            LLStringUtil::toLower(extension);

            if (extension.empty())
            {
                extension = "png";
                path += ".png";
            }

            LLPointer<LLImageFormatted> formatted;

            if (extension == "jpg" || extension == "jpeg")
            {
                formatted = new LLImageJPEG(gSavedSettings.getS32("SnapshotQuality"));
            }
            else if (extension == "bmp")
            {
                formatted = new LLImageBMP;
            }
            else if (extension == "tga")
            {
                formatted = new LLImageTGA;
            }
            else if (extension == "j2c" || extension == "jp2")
            {
                formatted = new LLImageJ2C;
            }
            else
            {
                formatted = new LLImagePNG;
            }

            const bool written = formatted->encode(image, 0.f) && formatted->save(path);

            said(path, image->getWidth(), image->getHeight(), written);
        }, LLFilePicker::FFSAVE_ALL, suggested_name);
}

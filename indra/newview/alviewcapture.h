/**
 * @file alviewcapture.h
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

#pragma once

#include "llimage.h"
#include "llpointer.h"
#include "llrect.h"

#include <functional>
#include <string>

// What a tool showing a picture of the UI does when asked for the picture
// as a file: the window as drawn, cropped to a rect of the screen, then
// written to whatever file the picker names, in the format the file's
// extension says. XUI Studio captures a preview this way.
namespace ALViewCapture
{
    enum class Outcome
    {
        Captured,
        // The window would not give a snapshot.
        NoSnapshot,
        // The rect is off the screen.
        OffScreen
    };

    // The screen rect as a picture: row zero is the bottom, as a snapshot
    // has it.
    Outcome captureScreenRect(const LLRect& screen, LLPointer<LLImageRaw>& out);

    // What became of the picture once the picker has spoken: the file it
    // went to and its size, or the file it would not go to. Nothing is
    // said for a picker cancelled, since nothing happened.
    typedef std::function<void(const std::string& path, S32 width, S32 height, bool written)> said_t;

    // The picker, opened on a suggested name, and the picture written to
    // the file it names. A name without an extension is written as a PNG.
    void saveThroughPicker(LLPointer<LLImageRaw> image, const std::string& suggested_name, said_t said);
}

/**
 * @file lllivelslfile.h
 * @brief A copy of a text an external editor has open, watched for its saves.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#pragma once

#include "alscripttempfiles.h"
#include "lllivefile.h"

#include <functional>
#include <memory>
#include <string>

class LLLiveLSLFile : public LLLiveFile
{
public:
    typedef std::function<bool(const std::string& filename)> change_callback_t;

    LLLiveLSLFile(std::string file_path, change_callback_t change_cb);
    ~LLLiveLSLFile() override;

    void ignoreNextUpdate() { mIgnoreNextUpdate = true; }

protected:
    bool loadFile() override;

    change_callback_t   mOnChangeCallback;
    bool                mIgnoreNextUpdate;
    // The copy held while it is watched: Script Studio may hold the same
    // one, and it goes with whichever lets go last.
    std::shared_ptr<ALScriptTempFiles::Claim> mHeld;
};

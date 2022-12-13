/**
* @file llprofileimagepicker.h
* @brief Specialized profile image filepicker
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

#ifndef LL_PROFILEIMAGEPICKER_H
#define LL_PROFILEIMAGEPICKER_H

#include "llviewermenufile.h"

enum EProfileImageType
{
    PROFILE_IMAGE_SL,
    PROFILE_IMAGE_FL,
};

class LLProfileImagePicker final : public LLFilePickerThread
{
public:
    typedef std::function<void(LLUUID const&)> ugly_picker_cb_t;

    LLProfileImagePicker(EProfileImageType type, LLHandle<LLPanel>* handle, ugly_picker_cb_t const& cb);
    ~LLProfileImagePicker() override;
    void notify(const std::vector<std::string>& filenames) override;

private:
    LLHandle<LLPanel>* mHandle;
    EProfileImageType mType;
    ugly_picker_cb_t mCallback;
};

#endif //  LL_PROFILEIMAGEPICKER_H

/**
* @file llprofileimagepicker.cpp
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

#include "llviewerprecompiledheaders.h"
#include "llprofileimagepicker.h"

#include "llavatariconctrl.h"
#include "llagent.h"
#include "llloadingindicator.h"
#include "llnotificationsutil.h"
#include "llpanel.h"
#include "llviewertexturelist.h"

static constexpr std::string_view PROFILE_IMAGE_UPLOAD_CAP("UploadAgentProfileImage");

static void post_profile_image_coro(std::string cap_url, EProfileImageType type, std::string path_to_image,
    LLProfileImagePicker::ugly_picker_cb_t cb);
static LLUUID post_profile_image(std::string cap_url, const LLSD& first_data, std::string path_to_image);
static void setImageUploading(LLPanel* panel, bool loading);


LLProfileImagePicker::LLProfileImagePicker(EProfileImageType type, LLHandle<LLPanel>* handle, ugly_picker_cb_t const& cb)
:   LLFilePickerThread(LLFilePicker::FFLOAD_IMAGE)
,   mHandle(handle)
,   mType(type),
    mCallback(cb)
{ }

LLProfileImagePicker::~LLProfileImagePicker()
{
    delete mHandle;
}

void LLProfileImagePicker::notify(const std::vector<std::string>& filenames)
{
    if (mHandle->isDead())
    {
        return;
    }
    if (filenames.empty())
    {
        return;
    }
    const std::string& file_path = filenames.at(0);
    if (file_path.empty())
    {
        return;
    }

    // generate a temp texture file for coroutine
    std::string const& temp_file = gDirUtilp->getTempFilename();
    U32 codec = LLImageBase::getCodecFromExtension(gDirUtilp->getExtension(file_path));
    const S32 MAX_DIM = 256;
    if (!LLViewerTextureList::createUploadFile(file_path, temp_file, codec, MAX_DIM))
    {
        LLSD notif_args;
        notif_args["REASON"] = LLImage::getLastThreadError().c_str();
        LLNotificationsUtil::add("CannotUploadTexture", notif_args);
        LL_WARNS("AvatarProperties") << "Failed to upload profile image of type " << (S32)PROFILE_IMAGE_SL << ", failed to open image" << LL_ENDL;
        return;
    }

    std::string cap_url = gAgent.getRegionCapability(PROFILE_IMAGE_UPLOAD_CAP);
    if (cap_url.empty())
    {
        LLSD args;
        args["CAPABILITY"] = std::string(PROFILE_IMAGE_UPLOAD_CAP);
        LLNotificationsUtil::add("RegionCapabilityRequestError", args);
        LL_WARNS("AvatarProperties") << "Failed to upload profile image of type " << (S32)PROFILE_IMAGE_SL << ", no cap found" << LL_ENDL;
        return;
    }

    setImageUploading(mHandle->get(), true);
    LLCoros::instance().launch("postAgentUserImageCoro",
                               [cap_url, this, temp_file]
                               {
                                   return post_profile_image_coro(cap_url, mType, temp_file, mCallback);
                               });
}

LLUUID post_profile_image(std::string cap_url, const LLSD& first_data, std::string path_to_image)
{
    LLCore::HttpRequest::policy_t               httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter(
        new LLCoreHttpUtil::HttpCoroutineAdapter("post_profile_image_coro", httpPolicy));
    LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);
    LLCore::HttpHeaders::ptr_t httpHeaders;

    LLCore::HttpOptions::ptr_t httpOpts(new LLCore::HttpOptions);
    httpOpts->setFollowRedirects(true);

    LLSD result = httpAdapter->postAndSuspend(httpRequest, cap_url, first_data, httpOpts, httpHeaders);

    LLSD               httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status      = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status)
    {
    // todo: notification?
    LL_WARNS("AvatarProperties") << "Failed to get uploader cap " << status.toString() << LL_ENDL;
    return LLUUID::null;
    }
    if (!result.has("uploader"))
    {
    // todo: notification?
    LL_WARNS("AvatarProperties") << "Failed to get uploader cap, response contains no data." << LL_ENDL;
    return LLUUID::null;
    }
    std::string uploader_cap = result["uploader"].asString();
    if (uploader_cap.empty())
    {
    LL_WARNS("AvatarProperties") << "Failed to get uploader cap, cap invalid." << LL_ENDL;
    return LLUUID::null;
    }

    // Upload the image

    LLCore::HttpRequest::ptr_t uploaderhttpRequest(new LLCore::HttpRequest);
    LLCore::HttpHeaders::ptr_t uploaderhttpHeaders(new LLCore::HttpHeaders);
    LLCore::HttpOptions::ptr_t uploaderhttpOpts(new LLCore::HttpOptions);
    S64                        length;

    {
    llifstream instream(path_to_image.c_str(), std::iostream::binary | std::iostream::ate);
    if (!instream.is_open())
    {
        LL_WARNS("AvatarProperties") << "Failed to open file " << path_to_image << LL_ENDL;
        return LLUUID::null;
    }
    length = instream.tellg();
    }

    uploaderhttpHeaders->append(HTTP_OUT_HEADER_CONTENT_TYPE, "application/jp2");         // optional
    uploaderhttpHeaders->append(HTTP_OUT_HEADER_CONTENT_LENGTH, llformat("%d", length));  // required!
    uploaderhttpOpts->setFollowRedirects(true);

    result = httpAdapter->postFileAndSuspend(uploaderhttpRequest, uploader_cap, path_to_image, uploaderhttpOpts, uploaderhttpHeaders);

    httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    status      = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    LL_WARNS("AvatarProperties") << result << LL_ENDL;

    if (!status)
    {
    LL_WARNS("AvatarProperties") << "Failed to upload image " << status.toString() << LL_ENDL;
    return LLUUID::null;
    }

    if (result["state"].asString() != "complete")
    {
    if (result.has("message"))
    {
        LL_WARNS("AvatarProperties") << "Failed to upload image, state " << result["state"] << " message: " << result["message"] << LL_ENDL;
    }
    else
    {
        LL_WARNS("AvatarProperties") << "Failed to upload image " << result << LL_ENDL;
    }
    return LLUUID::null;
    }

    return result["new_asset"].asUUID();
}

void post_profile_image_coro(std::string cap_url, EProfileImageType type, std::string path_to_image,
                             LLProfileImagePicker::ugly_picker_cb_t cb)
{
    LLSD data;
    switch (type)
    {
    case PROFILE_IMAGE_SL:
        data["profile-image-asset"] = "sl_image_id";
        break;
    case PROFILE_IMAGE_FL:
        data["profile-image-asset"] = "fl_image_id";
        break;
    }

    LLUUID result = post_profile_image(cap_url, data, path_to_image);

    cb(result);

    if (type == PROFILE_IMAGE_SL && result.notNull())
    {
        LLAvatarIconIDCache::getInstance()->add(gAgentID, result);
        // Should trigger callbacks in icon controls
        LLAvatarPropertiesProcessor::getInstance()->sendAvatarPropertiesRequest(gAgentID);
    }

    // Cleanup
    LLFile::remove(path_to_image);
}

void setImageUploading(LLPanel* panel, bool loading)
{
    LLLoadingIndicator* indicator = panel->findChild<LLLoadingIndicator>("image_upload_indicator");
    if (indicator)
    {
        indicator->setVisible(loading);
        if (loading)
        {
            indicator->start();
        }
        else
        {
            indicator->stop();
        }
    }
}

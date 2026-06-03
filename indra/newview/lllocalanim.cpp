/**
 * @file lllocalanim.cpp
 * @brief Local animation preview implementation
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "lllocalanim.h"

#include "llbvhloader.h"
#include "lldatapacker.h"
#include "lldir.h"
#include "llfile.h"
#include "llkeyframemotion.h"
#include "llstring.h"
#include "llvoavatar.h"
#include "llvoavatarself.h" // gAgentAvatarp, isAgentAvatarValid (BVH joint aliases)

LLLocalAnimMgr::LLLocalAnimMgr()
{
}

LLLocalAnimMgr::~LLLocalAnimMgr()
{
    // Drop the keyframe data we cached globally for our local motions.
    for (const auto& entry : mAnims)
    {
        LLKeyframeDataCache::removeKeyframeData(entry.first);
    }
    mAnims.clear();
}

LLUUID LLLocalAnimMgr::loadAnim(const std::string& filename)
{
    std::error_code ec;
    LLFile infile;
    infile.open(filename, LLFile::in | LLFile::binary, ec);
    if (!infile || ec)
    {
        LL_WARNS("LocalAnim") << "Can't open animation file: " << filename << LL_ENDL;
        return LLUUID::null;
    }

    const S64 file_size = infile.size(ec);
    if (file_size <= 0 || ec)
    {
        LL_WARNS("LocalAnim") << "Empty or unreadable animation file: " << filename << LL_ENDL;
        return LLUUID::null;
    }

    std::vector<U8> data((size_t)file_size);
    if ((S64)infile.read(data.data(), file_size, ec) != file_size || ec)
    {
        LL_WARNS("LocalAnim") << "Short read on animation file: " << filename << LL_ENDL;
        return LLUUID::null;
    }
    infile.close();

    // Decode to LLKeyframeMotion serialized form. A .anim file already IS that form;
    // a .bvh is parsed and serialized the same way the upload path does.
    std::string ext = gDirUtilp->getExtension(filename);
    LLStringUtil::toLower(ext);

    std::vector<U8> keyframe;
    if (ext == "anim")
    {
        keyframe = std::move(data);
    }
    else if (ext == "bvh")
    {
        data.push_back(0); // LLBVHLoader wants a null-terminated text buffer
        ELoadStatus load_status = E_ST_OK;
        S32 line_number = 0;
        std::map<std::string, std::string, std::less<>> joint_alias_map;
        if (isAgentAvatarValid())
        {
            joint_alias_map = gAgentAvatarp->getJointAliases();
        }
        LLBVHLoader loader((const char*)data.data(), load_status, line_number, joint_alias_map);
        if (!loader.isInitialized())
        {
            LL_WARNS("LocalAnim") << "BVH parse failed (status " << load_status << ", line "
                                  << line_number << "): " << filename << LL_ENDL;
            return LLUUID::null;
        }
        const U32 out_size = loader.getOutputSize();
        keyframe.resize(out_size);
        LLDataPackerBinaryBuffer dp(keyframe.data(), (S32)out_size);
        loader.serialize(dp); // BVH -> keyframe (.anim) bytes
    }
    else
    {
        LL_WARNS("LocalAnim") << "Unsupported animation file type '." << ext << "': " << filename << LL_ENDL;
        return LLUUID::null;
    }

    if (keyframe.empty())
    {
        LL_WARNS("LocalAnim") << "No animation data decoded from " << filename << LL_ENDL;
        return LLUUID::null;
    }

    LLUUID id;
    id.generate();

    LocalAnim anim;
    anim.mFilename  = filename;
    anim.mShortName = gDirUtilp->getBaseFileName(filename, true /* strip extension */);
    anim.mData      = std::move(keyframe);

    const size_t bytes = anim.mData.size();
    mAnims[id] = std::move(anim);

    LL_INFOS("LocalAnim") << "Loaded local anim '" << mAnims[id].mShortName << "' ("
                          << bytes << " bytes) as " << id << LL_ENDL;
    return id;
}

bool LLLocalAnimMgr::playOnAvatar(LLVOAvatar* av, const LLUUID& anim_id)
{
    auto iter = mAnims.find(anim_id);
    if (!av || iter == mAnims.end())
    {
        return false;
    }

    // createMotion() returns a load-pending LLKeyframeMotion for an unknown id; we
    // then hand it the keyframe data locally (deserialize() also caches it globally
    // via LLKeyframeDataCache, so replays -- and a freshly recreated control avatar
    // -- can resolve the id without an asset fetch that would never arrive).
    LLKeyframeMotion* motionp = dynamic_cast<LLKeyframeMotion*>(av->createMotion(anim_id));
    if (!motionp)
    {
        LL_WARNS("LocalAnim") << "createMotion failed for " << anim_id << LL_ENDL;
        return false;
    }

    if (!LLKeyframeDataCache::getKeyframeData(anim_id))
    {
        LLDataPackerBinaryBuffer dp(iter->second.mData.data(), (S32)iter->second.mData.size());
        if (!motionp->deserialize(dp, anim_id, false))
        {
            LL_WARNS("LocalAnim") << "Failed to deserialize local anim '"
                                  << iter->second.mShortName << "'" << LL_ENDL;
            return false;
        }
    }

    // Replace any local anim already playing on this control avatar.
    const LLUUID av_id = av->getID();
    auto prev = mPlaying.find(av_id);
    if (prev != mPlaying.end() && prev->second != anim_id)
    {
        av->stopMotion(prev->second, false);
    }

    av->startMotion(anim_id);
    mPlaying[av_id] = anim_id;
    LL_INFOS("LocalAnim") << "Playing local anim '" << iter->second.mShortName << "'" << LL_ENDL;
    return true;
}

void LLLocalAnimMgr::stopOnAvatar(LLVOAvatar* av)
{
    if (!av)
    {
        return;
    }
    auto iter = mPlaying.find(av->getID());
    if (iter != mPlaying.end())
    {
        av->stopMotion(iter->second, false);
        mPlaying.erase(iter);
    }
}

bool LLLocalAnimMgr::isLocalAnim(const LLUUID& anim_id) const
{
    return mAnims.find(anim_id) != mAnims.end();
}

std::string LLLocalAnimMgr::getShortName(const LLUUID& anim_id) const
{
    auto iter = mAnims.find(anim_id);
    return (iter != mAnims.end()) ? iter->second.mShortName : std::string();
}

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
#include "llcharacter.h"        // LLCharacter::sInstances (resolve avatar by id)
#include "lldatapacker.h"
#include "lldir.h"
#include "llfile.h"
#include "llinventoryicon.h"
#include "llkeyframemotion.h"
#include "llscrolllistctrl.h"
#include "llstring.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"     // gAgentAvatarp, isAgentAvatarValid (BVH joint aliases)

namespace
{
    constexpr F32 LOCAL_ANIM_TIMER_HEARTBEAT = 3.0f; // seconds between file-change polls

    // Resolve an avatar id to a live avatar, including animesh control avatars
    // (both are LLCharacters, registered in LLCharacter::sInstances).
    LLVOAvatar* resolve_avatar(const LLUUID& av_id)
    {
        for (LLCharacter* character : LLCharacter::sInstances)
        {
            if (character && character->getID() == av_id)
            {
                return dynamic_cast<LLVOAvatar*>(character);
            }
        }
        return nullptr;
    }
}

LLLocalAnimMgr::LLLocalAnimMgr()
{
    mTimer.stopTimer(); // started on demand once the first unit is added
}

LLLocalAnimMgr::~LLLocalAnimMgr()
{
    mTimer.stopTimer();
    // Drop the keyframe data we cached globally for our local motions.
    for (const auto& entry : mAnims)
    {
        LLKeyframeDataCache::removeKeyframeData(entry.first);
    }
    mAnims.clear();
}

bool LLLocalAnimMgr::decodeFile(const std::string& filename, std::vector<U8>& out_keyframe) const
{
    std::error_code ec;
    LLFile infile;
    infile.open(filename, LLFile::in | LLFile::binary, ec);
    if (!infile || ec)
    {
        LL_WARNS("LocalAnim") << "Can't open animation file: " << filename << LL_ENDL;
        return false;
    }

    // Reject unsupported types and absurd sizes BEFORE buffering the whole file,
    // so a wrong or huge pick can't allocate unbounded memory / stall the viewer.
    std::string ext = gDirUtilp->getExtension(filename);
    LLStringUtil::toLower(ext);
    if (ext != "anim" && ext != "bvh")
    {
        LL_WARNS("LocalAnim") << "Unsupported animation file type '." << ext << "': " << filename << LL_ENDL;
        return false;
    }

    constexpr S64 MAX_LOCAL_ANIM_BYTES = 64ll * 1024 * 1024;
    const S64 file_size = infile.size(ec);
    if (file_size <= 0 || file_size > MAX_LOCAL_ANIM_BYTES || ec)
    {
        LL_WARNS("LocalAnim") << "Empty, oversized, or unreadable animation file: " << filename << LL_ENDL;
        return false;
    }

    std::vector<U8> data((size_t)file_size);
    if ((S64)infile.read(data.data(), file_size, ec) != file_size || ec)
    {
        LL_WARNS("LocalAnim") << "Short read on animation file: " << filename << LL_ENDL;
        return false;
    }
    infile.close();

    // Decode to LLKeyframeMotion serialized form. A .anim file already IS that form;
    // a .bvh is parsed and serialized the same way the upload path does.
    if (ext == "anim")
    {
        // Validate that the bytes actually deserialize before accepting them, so a
        // truncated/corrupt save isn't committed -- and, via doUpdates()'s "keep last
        // good on failure", doesn't blank a live preview on hot reload. Use a throwaway
        // motion id on the agent avatar so playback and the global keyframe cache are
        // untouched. (No avatar yet -> can't validate here; the size/ext guards apply.)
        if (isAgentAvatarValid())
        {
            LLUUID probe_id;
            probe_id.generate();
            if (LLKeyframeMotion* probe = dynamic_cast<LLKeyframeMotion*>(gAgentAvatarp->createMotion(probe_id)))
            {
                LLDataPackerBinaryBuffer probe_dp(data.data(), (S32)data.size());
                const bool ok = probe->deserialize(probe_dp, probe_id, false);
                gAgentAvatarp->removeMotion(probe_id);
                LLKeyframeDataCache::removeKeyframeData(probe_id);
                if (!ok)
                {
                    LL_WARNS("LocalAnim") << "Corrupt .anim (failed to deserialize): " << filename << LL_ENDL;
                    return false;
                }
            }
        }
        out_keyframe = std::move(data);
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
            return false;
        }
        const U32 out_size = loader.getOutputSize();
        out_keyframe.resize(out_size);
        LLDataPackerBinaryBuffer dp(out_keyframe.data(), (S32)out_size);
        loader.serialize(dp); // BVH -> keyframe (.anim) bytes
    }
    else
    {
        LL_WARNS("LocalAnim") << "Unsupported animation file type '." << ext << "': " << filename << LL_ENDL;
        return false;
    }

    if (out_keyframe.empty())
    {
        LL_WARNS("LocalAnim") << "No animation data decoded from " << filename << LL_ENDL;
        return false;
    }
    return true;
}

LLUUID LLLocalAnimMgr::loadAnim(const std::string& filename)
{
    // No double-add: a file that's already loaded just returns its existing unit.
    // (Live-reload re-decodes in doUpdates(), not here, so this doesn't block it.)
    if (LLUUID existing = getUnitID(filename); existing.notNull())
    {
        return existing;
    }

    std::vector<U8> keyframe;
    if (!decodeFile(filename, keyframe))
    {
        return LLUUID::null;
    }

    LLUUID id;
    id.generate();

    LocalAnim anim;
    anim.mFilename  = filename;
    anim.mShortName = gDirUtilp->getBaseFileName(filename, true /* strip extension */);
    anim.mData      = std::move(keyframe);
    std::error_code ec;
    anim.mLastModified = std::filesystem::last_write_time(filename, ec);

    const size_t bytes = anim.mData.size();
    mAnims[id] = std::move(anim);

    if (!mTimer.isRunning())
    {
        mTimer.startTimer(); // begin watching source files for live reload
    }

    mUnitsChangedSignal();
    LL_INFOS("LocalAnim") << "Loaded local anim '" << mAnims[id].mShortName << "' ("
                          << bytes << " bytes) as " << id << LL_ENDL;
    return id;
}

LLUUID LLLocalAnimMgr::addUnit(const std::string& filename)
{
    return loadAnim(filename);
}

bool LLLocalAnimMgr::addUnit(const std::vector<std::string>& filenames)
{
    bool any = false;
    for (const std::string& filename : filenames)
    {
        if (!filename.empty() && loadAnim(filename).notNull())
        {
            any = true;
        }
    }
    return any;
}

void LLLocalAnimMgr::delUnit(LLUUID tracking_id)
{
    auto iter = mAnims.find(tracking_id);
    if (iter == mAnims.end())
    {
        return;
    }

    // Stop it wherever it's playing, purge the cached motion, and drop the play map.
    for (auto pit = mPlaying.begin(); pit != mPlaying.end(); )
    {
        if (pit->second == tracking_id)
        {
            if (LLVOAvatar* av = resolve_avatar(pit->first))
            {
                av->stopMotion(tracking_id, true);
                av->removeMotion(tracking_id);
            }
            pit = mPlaying.erase(pit);
        }
        else
        {
            ++pit;
        }
    }

    LLKeyframeDataCache::removeKeyframeData(tracking_id);
    mAnims.erase(iter);

    if (mAnims.empty())
    {
        mTimer.stopTimer(); // nothing left to watch
    }
    mUnitsChangedSignal();
    LL_INFOS("LocalAnim") << "Removed local anim " << tracking_id << LL_ENDL;
}

boost::signals2::connection LLLocalAnimMgr::setUnitsChangedCallback(const std::function<void()>& cb)
{
    return mUnitsChangedSignal.connect(cb);
}

LLUUID LLLocalAnimMgr::getUnitID(const std::string& filename) const
{
    for (const auto& entry : mAnims)
    {
        if (entry.second.mFilename == filename)
        {
            return entry.first;
        }
    }
    return LLUUID::null;
}

std::string LLLocalAnimMgr::getFilename(const LLUUID& tracking_id) const
{
    auto iter = mAnims.find(tracking_id);
    return (iter != mAnims.end()) ? iter->second.mFilename : std::string();
}

std::vector<std::string> LLLocalAnimMgr::getFilenames() const
{
    std::vector<std::string> out;
    out.reserve(mAnims.size());
    for (const auto& entry : mAnims)
    {
        out.push_back(entry.second.mFilename);
    }
    return out;
}

void LLLocalAnimMgr::feedScrollList(LLScrollListCtrl* ctrl)
{
    if (!ctrl)
    {
        return;
    }

    const std::string icon_name = LLInventoryIcon::getIconName(LLAssetType::AT_ANIMATION, LLInventoryType::IT_ANIMATION);

    for (const auto& entry : mAnims)
    {
        LLSD element;
        element["columns"][0]["column"] = "icon";
        element["columns"][0]["type"]   = "icon";
        element["columns"][0]["value"]  = icon_name;

        element["columns"][1]["column"] = "unit_name";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = entry.second.mShortName;

        LLSD data;
        data["id"]   = entry.first;
        data["type"] = (S32)LLAssetType::AT_ANIMATION;
        element["value"] = data;

        ctrl->addElement(element);
    }
}

void LLLocalAnimMgr::reapplyToAvatar(const LLUUID& av_id, const LLUUID& anim_id)
{
    LLVOAvatar* av = resolve_avatar(av_id);
    auto iter = mAnims.find(anim_id);
    if (!av || iter == mAnims.end())
    {
        return;
    }

    // Purge the stale parsed motion instance so createMotion() yields a fresh one
    // that deserializes the new bytes.
    av->stopMotion(anim_id, true);
    av->removeMotion(anim_id);

    LLKeyframeMotion* motionp = dynamic_cast<LLKeyframeMotion*>(av->createMotion(anim_id));
    if (!motionp)
    {
        return;
    }
    LLDataPackerBinaryBuffer dp(iter->second.mData.data(), (S32)iter->second.mData.size());
    if (motionp->deserialize(dp, anim_id, false))
    {
        av->startMotion(anim_id);
    }
}

void LLLocalAnimMgr::doUpdates()
{
    // Stop/restart around the sweep so a long poll can't re-enter via the timer.
    mTimer.stopTimer();

    for (auto& entry : mAnims)
    {
        const LLUUID& id   = entry.first;
        LocalAnim&    anim = entry.second;

        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(anim.mFilename, ec);
        if (ec || mtime == anim.mLastModified)
        {
            continue;
        }
        std::vector<U8> fresh;
        if (!decodeFile(anim.mFilename, fresh))
        {
            continue; // keep last good data; don't consume mtime so a mid-save retries
        }
        anim.mLastModified = mtime;
        anim.mData = std::move(fresh);

        // Refresh the cached keyframe data so the next play uses the new bytes,
        // and re-apply live to any avatar currently playing this id.
        LLKeyframeDataCache::removeKeyframeData(id);
        for (const auto& play : mPlaying)
        {
            if (play.second == id)
            {
                reapplyToAvatar(play.first, id);
            }
        }
        LL_INFOS("LocalAnim") << "Live-reloaded local anim '" << anim.mShortName << "'" << LL_ENDL;
    }

    if (!mAnims.empty())
    {
        mTimer.startTimer();
    }
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

/*=======================================*/
/*  LLLocalAnimTimer: live-reload poll   */
/*=======================================*/
LLLocalAnimMgr::LLLocalAnimTimer::LLLocalAnimTimer()
    : LLEventTimer(LOCAL_ANIM_TIMER_HEARTBEAT)
{
}

void LLLocalAnimMgr::LLLocalAnimTimer::startTimer() { mEventTimer.start(); }
void LLLocalAnimMgr::LLLocalAnimTimer::stopTimer()  { mEventTimer.stop(); }
bool LLLocalAnimMgr::LLLocalAnimTimer::isRunning()  { return mEventTimer.getStarted(); }

bool LLLocalAnimMgr::LLLocalAnimTimer::tick()
{
    if (LLLocalAnimMgr::instanceExists())
    {
        LLLocalAnimMgr::getInstance()->doUpdates();
    }
    return false; // keep ticking
}

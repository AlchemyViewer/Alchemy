/**
 * @file lllocalanim.h
 * @brief Local animation preview header
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

// Local Animation is the animation analog of Local Mesh (lllocalmesh.h): it loads
// a Second Life internal animation file (.anim) from disk, assigns it a client-only
// motion UUID, and plays it on an avatar -- typically the control avatar of a local
// mesh that has been made an animated object (animesh) -- without uploading to the
// asset server. A .anim file IS the LLKeyframeMotion serialized format, so loading
// is just reading the bytes and deserializing them into a motion (which also caches
// the keyframe data globally, so any control avatar can play the id).

#ifndef LL_LLLOCALANIM_H
#define LL_LLLOCALANIM_H

#include "llsingleton.h"
#include "lluuid.h"

#include <map>
#include <string>
#include <vector>

class LLVOAvatar;

// Owns loaded local animations and plays them on control avatars.
class LLLocalAnimMgr : public LLSingleton<LLLocalAnimMgr>
{
    LLSINGLETON(LLLocalAnimMgr);
    ~LLLocalAnimMgr();

public:
    // Load a local .anim file into a client-only motion. Returns the motion id, or
    // null on failure. The keyframe bytes are kept so the motion can be
    // (re)deserialized onto any control avatar that plays it.
    LLUUID loadAnim(const std::string& filename);

    // Play a loaded local anim on the given avatar (the animesh control av),
    // replacing whatever local anim was playing on it. stopOnAvatar() stops the
    // local anim currently playing on that avatar.
    bool playOnAvatar(LLVOAvatar* av, const LLUUID& anim_id);
    void stopOnAvatar(LLVOAvatar* av);

    bool        isLocalAnim(const LLUUID& anim_id) const;
    std::string getShortName(const LLUUID& anim_id) const;

private:
    struct LocalAnim
    {
        std::string      mFilename;
        std::string      mShortName;
        std::vector<U8>  mData; // raw .anim bytes == LLKeyframeMotion serialized form
    };
    std::map<LLUUID, LocalAnim> mAnims;

    // Which local anim is currently playing on each control avatar (by avatar id),
    // so a later play can replace it and "Stop" can end it.
    std::map<LLUUID, LLUUID> mPlaying;
};

#endif // LL_LLLOCALANIM_H

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
// a Second Life internal animation file (.anim) or a .bvh from disk, assigns it a
// client-only motion UUID, and plays it on an avatar -- typically the control
// avatar of a local mesh that has been made an animated object (animesh) -- without
// uploading to the asset server. A .anim file IS the LLKeyframeMotion serialized
// format; a .bvh is parsed and serialized the same way the upload path does. The
// manager mirrors the shared local-asset registry API (LLLocalMeshMgr /
// LLLocalBitmapMgr): addUnit/delUnit/getUnitID/getFilename/feedScrollList and a
// doUpdates() live-reload tick driven by a periodic timer.

#ifndef LL_LLLOCALANIM_H
#define LL_LLLOCALANIM_H

#include "lleventtimer.h"
#include "llsingleton.h"
#include "lluuid.h"

#include <boost/signals2/signal.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

class LLScrollListCtrl;
class LLVOAvatar;

// Owns loaded local animations and plays them on control avatars.
class LLLocalAnimMgr : public LLSingleton<LLLocalAnimMgr>
{
    LLSINGLETON(LLLocalAnimMgr);
    ~LLLocalAnimMgr();

public:
    // --- Shared local-asset registry API (mirrors LLLocalMeshMgr) -------------
    LLUUID addUnit(const std::string& filename);
    bool   addUnit(const std::vector<std::string>& filenames);
    void   delUnit(LLUUID tracking_id);

    LLUUID      getUnitID(const std::string& filename) const;
    std::string getFilename(const LLUUID& tracking_id) const;
    // Every currently-loaded source file path (for cross-session persistence).
    std::vector<std::string> getFilenames() const;

    void feedScrollList(LLScrollListCtrl* ctrl);

    // Fired when the unit list changes (add/remove) so the Local Assets floater
    // refreshes reactively.
    boost::signals2::connection setUnitsChangedCallback(const std::function<void()>& cb);

    // Timer tick: poll every loaded anim's source file for changes and live-reload.
    void doUpdates();

    // --- Loading / playback ---------------------------------------------------
    // Load a local .anim/.bvh file into a client-only motion. Returns the motion id
    // (which doubles as the tracking id), or null on failure. Kept public for the
    // in-world right-click "Play Local Animation" path; addUnit() wraps it.
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
        std::string                     mFilename;
        std::string                     mShortName;
        std::vector<U8>                 mData; // raw .anim bytes == LLKeyframeMotion serialized form
        std::filesystem::file_time_type mLastModified {}; // for live reload
    };
    std::map<LLUUID, LocalAnim> mAnims;

    // Which local anim is currently playing on each control avatar (by avatar id),
    // so a later play can replace it, "Stop"/delete can end it, and live-reload can
    // re-apply fresh data to it.
    std::map<LLUUID, LLUUID> mPlaying;

    boost::signals2::signal<void()> mUnitsChangedSignal; // add/remove

    // Decode a .anim/.bvh file into keyframe bytes (the LLKeyframeMotion form).
    bool decodeFile(const std::string& filename, std::vector<U8>& out_keyframe) const;
    // Re-deserialize fresh bytes onto an avatar already playing this id (live reload).
    void reapplyToAvatar(const LLUUID& av_id, const LLUUID& anim_id);

    // Live-reload polling (mirrors LLLocalMeshTimer).
    class LLLocalAnimTimer : public LLEventTimer
    {
    public:
        LLLocalAnimTimer();
        void startTimer();
        void stopTimer();
        bool isRunning();
        bool tick() override;
    };
    LLLocalAnimTimer mTimer;
};

#endif // LL_LLLOCALANIM_H

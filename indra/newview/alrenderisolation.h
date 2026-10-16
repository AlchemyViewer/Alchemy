/**
 * @file alrenderisolation.h
 * @brief Which of the world a snapshot cutout keeps, and the question every
 *        cull choke point asks of it
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

#ifndef AL_RENDERISOLATION_H
#define AL_RENDERISOLATION_H

#include "lluuid.h"
#include "pipeline.h"

#include <vector>

class LLDrawable;
class LLSpatialGroup;
class LLViewerObject;
class LLVOAvatar;

/**
 * A snapshot cutout's subject: one avatar with everything it wears, or one
 * linkset -- optionally with the avatars sitting on it and everything they
 * wear.
 *
 * Two lifetimes. The *armed* target is session state, picked in the snapshot
 * floater and kept after it closes, so File > Take Snapshot to Disk can cut
 * the same subject out. A Scope is the frames of one cutout render: it
 * resolves the target to objects once, sets LLPipeline::sCutoutRender, turns
 * occlusion off, and answers includes() for the cull choke points.
 *
 * Everything outside the subject is dropped from the main camera's pass only
 * (LLPipeline::cullingToCutoutSubject). Shadows, probes and impostor bakes in the
 * same frame still see the whole world, which is what keeps the subject lit
 * the way it is where it stands.
 */
class ALRenderIsolation
{
public:
    /// The subject for a pick of @a picked_id: the wearer for an attachment,
    /// the linkset root for a prim, the avatar for an avatar. Null for what a
    /// cutout cannot isolate -- HUD attachments, land, water, grass, particles
    /// -- and for what is not in the scene.
    static LLUUID targetFromPick(const LLUUID& picked_id);

    /// Keeps @a target_id as the subject until disarm(). A linkset that is
    /// still static is made active here: its geometry is batched with its
    /// neighbours' until then, and only a spatial bridge of its own can be
    /// told apart from them. That is one-way, as it is for any object the
    /// viewer sees move, and it takes a rebuild, so it happens on the pick
    /// rather than on the frame that needs it -- and again for each Scope,
    /// for a subject the viewer has made anew since (a teleport back, a
    /// region reloaded), which comes back static.
    static void arm(const LLUUID& target_id);
    static void disarm();
    static const LLUUID& armedTarget() { return sArmed; }

    /// Whether @a avatar is the armed subject, or sits on it while the cutout
    /// takes seated avatars along, while the Cutout box is on. Such an avatar is never impostored for
    /// being one too many (LLVOAvatar::computeUpdatePeriod): an impostor drops
    /// the attachments and draws a billboard.
    static bool isArmedAvatar(const LLVOAvatar* avatar);

    /// The armed subject if it is in the scene now, or null.
    static LLViewerObject* findArmedObject();

    class Scope
    {
    public:
        Scope(const LLUUID& target_id, bool include_seated);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        /// Whether the target was found. Without it the frame is empty, which
        /// is still a frame: the caller decides what that means.
        bool hasSubject() const { return mHasSubject; }

    private:
        // Stale occlusion results would still hide the subject behind walls
        // this frame no longer draws.
        LLDisableOcclusionCulling mNoOcclusion;
        bool mHasSubject = false;
    };

    /// Whether @a vobj is part of the current scope's subject.
    static bool includes(LLViewerObject* vobj);
    /// The same for a drawable; a spatial bridge answers for its root.
    static bool includes(LLDrawable* drawable);
    /// The same for a group: one inside a subject's bridge is part of it, and
    /// one of a region's shared partitions never is -- everything in those is
    /// batched together, and the subject is never among it (see arm()).
    static bool includes(LLSpatialGroup* group);

private:
    /// Gives a static linkset a spatial bridge of its own (see arm()).
    static void giveOwnSpatialBridge(LLViewerObject* object);
    static bool includesAvatar(const LLVOAvatar* avatar);
    static void collectSeatedAvatars(LLViewerObject* object);

    static LLUUID                   sArmed;
    // Resolved by a Scope for its frames only.
    static LLViewerObject*          sRoot;      // the linkset, for an object target
    static std::vector<LLVOAvatar*> sAvatars;   // the avatar, or the ones seated on the linkset
};

#endif // AL_RENDERISOLATION_H

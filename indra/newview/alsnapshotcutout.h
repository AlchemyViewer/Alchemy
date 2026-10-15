/**
 * @file alsnapshotcutout.h
 * @brief One snapshot cutout, as asked for
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

#ifndef AL_SNAPSHOTCUTOUT_H
#define AL_SNAPSHOTCUTOUT_H

#include "llsnapshotmodel.h"
#include "lluuid.h"
#include "v4color.h"

#include <optional>

/// One snapshot cutout, as asked for: which subject, what fills the rest of
/// the frame, and what the image is going to. Built in one place, fromSettings(),
/// for both of the ways a cutout is taken -- the snapshot floater's preview and
/// File > Take Snapshot to Disk -- so the two cannot disagree about it.
struct ALSnapshotCutout
{
    LLUUID   mTarget;                           ///< the armed subject (ALRenderIsolation)
    bool     mIncludeSeated = false;            ///< an object's seated avatars with it
    LLColor4 mFill{ 0.f, 0.f, 0.f, 0.f };       ///< alpha 0 leaves the rest empty, 1 fills it
    bool     mKeepAlpha = true;                 ///< the destination stores alpha
    U32      mSupersample = 1;                  ///< rendered this much larger each way, then averaged down

    /// What the settings ask for, for an image of @a type in @a format, or
    /// nothing: while the Cutout box is off, while no subject is armed, and
    /// while RLVa restricts what may be seen -- a cutout of one subject would
    /// show it clear of a vision sphere or an overlay.
    static std::optional<ALSnapshotCutout> fromSettings(LLSnapshotModel::ESnapshotType type,
                                                        LLSnapshotModel::ESnapshotFormat format);

    /// Whether RLVa restricts vision now, which rules cutouts out.
    static bool blockedByRlv();

    /// Whether @a other renders the same subject the same way. The fill and
    /// whether alpha is kept are not part of that: they are how the rendered
    /// cutout is laid down (ALCutoutImage::layOverFill), after the render.
    bool drawsSameAs(const ALSnapshotCutout& other) const;

    bool operator==(const ALSnapshotCutout& other) const;
    bool operator!=(const ALSnapshotCutout& other) const { return !(*this == other); }
};

#endif // AL_SNAPSHOTCUTOUT_H

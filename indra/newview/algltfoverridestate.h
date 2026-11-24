/**
 * @file algltfoverridestate.h
 * @brief How a face stands against the glTF overrides its region sent, and what became of those a region sent.
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

#include "stdtypes.h"

#include <string>

// How a face stands against what the region sent for its object (LLGLTFOverrideCacheEntry::faceState).
enum class ALGLTFOverrideFaceState
{
    UNSENT,  // the region has sent nothing for the object, and the face shows what it holds
    HELD,    // the face waits on the region's answer to an edit of the viewer's own
    MATCHES, // the face shows the region's override for it, or none where the region sent none
    DIFFERS  // the face shows another override than the region's, or one where the region sent none
};

// What became of the glTF overrides a region sent, counted over the region's life and logged when its cache is saved.
struct ALGLTFOverrideCounts
{
    U32 mMessages = 0;        // override messages the region sent
    U32 mFacesSet = 0;        // faces a message gave an override or took one from at once
    U32 mFacesDeferred = 0;   // faces a message found with no material or one still fetching
    U32 mFacesReconciled = 0; // faces the region's record changed later (LLViewerRegion::applyCacheMiscExtras)
    U32 mKeptOnUpdate = 0;    // objects whose cache entry went for an update and whose overrides stayed
    U32 mKeptOnKill = 0;      // objects the region killed whose overrides stayed with their cache entry
    U32 mTakenWithObject = 0; // objects whose overrides went with the object
    U32 mDroppedAsOther = 0;  // records another object under the local ID made stale
    U32 mAskedAgain = 0;      // objects asked for again so that their full updates bring their overrides

    bool any() const;
    std::string summary() const;
};

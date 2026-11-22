/**
 * @file almeshuploadassets.h
 * @brief The mesh assets a fee request wrote, for the upload that follows
 *        it to send rather than write again
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

#ifndef AL_MESHUPLOADASSETS_H
#define AL_MESHUPLOADASSETS_H

#include "llmodel.h"

#include <boost/unordered_map.hpp>

#include <string>

// Each base model's asset as a fee request wrote it, and what it was written
// from and with. The model upload floater keeps them while the fee it shows
// stands; an upload whose options match, and whose every base model is
// written from what its asset was, sends them rather than make the hulls and
// write the assets again, so that it sends what the fee was quoted for.
class ALMeshUploadAssets
{
public:
    struct Options
    {
        bool upload_skin = false;
        bool upload_joints = false;
        bool lock_scale_if_joint_position = false;
        LLModel::EBlockPacking packing = LLModel::PACK_PLAIN;
        ALMeshFacePack::Weights weights = ALMeshFacePack::Weights::KEEP;

        bool operator==(const Options&) const = default;
    };

    struct Asset
    {
        size_t source = 0; // LLMeshUploadThread's fingerprint of what it was written from
        std::string bytes;
    };

    Options mOptions;
    boost::unordered_map<const LLModel*, Asset> mAssets; // by base model
};

#endif // AL_MESHUPLOADASSETS_H

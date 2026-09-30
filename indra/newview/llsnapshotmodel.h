/**
* @file llsnapshotmodel.h
* @brief Snapshot model for storing snapshot data etc.
*
* $LicenseInfo:firstyear=2004&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2016, Linden Research, Inc.
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

#ifndef LL_LLSNAPSHOTMODEL_H
#define LL_LLSNAPSHOTMODEL_H

class LLSnapshotModel
{
public:
    enum ESnapshotType
    {
        SNAPSHOT_POSTCARD,
        SNAPSHOT_TEXTURE,
        SNAPSHOT_LOCAL,
        SNAPSHOT_WEB
    };

    typedef enum e_snapshot_format
    {
        SNAPSHOT_FORMAT_PNG,
        SNAPSHOT_FORMAT_JPEG,
        SNAPSHOT_FORMAT_BMP,
        SNAPSHOT_FORMAT_WEBP,
        SNAPSHOT_FORMAT_AVIF,
        SNAPSHOT_FORMAT_AVIF_LOSSLESS
    } ESnapshotFormat;

    typedef enum
    {
        SNAPSHOT_TYPE_COLOR,
        SNAPSHOT_TYPE_DEPTH,
        SNAPSHOT_TYPE_DEPTH24
    } ESnapshotLayerType;

    /// Whether an image of @a type saved as @a format can keep an alpha
    /// channel. An inventory texture is J2C whatever the format says; JPEG and
    /// BMP have nowhere to put alpha, and a postcard is always JPEG.
    static bool keepsAlpha(ESnapshotType type, ESnapshotFormat format)
    {
        if (type == SNAPSHOT_TEXTURE)
        {
            return true;
        }
        if (type == SNAPSHOT_POSTCARD)
        {
            return false;
        }
        switch (format)
        {
        case SNAPSHOT_FORMAT_PNG:
        case SNAPSHOT_FORMAT_WEBP:
        case SNAPSHOT_FORMAT_AVIF:
        case SNAPSHOT_FORMAT_AVIF_LOSSLESS:
            return true;
        default:
            return false;
        }
    }
};

#endif // LL_LLSNAPSHOTMODEL_H

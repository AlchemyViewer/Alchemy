/** 
 * @file llbvhconsts.h
 * @brief Consts and types useful to BVH files and LindenLabAnimation format.
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLBVHCONSTS_H
#define LL_LLBVHCONSTS_H

const F32 MAX_ANIM_DURATION = 60.f;

typedef enum e_constraint_type
	{
		CONSTRAINT_TYPE_POINT,
		CONSTRAINT_TYPE_PLANE,
		NUM_CONSTRAINT_TYPES
	} EConstraintType;

typedef enum e_constraint_target_type
	{
		CONSTRAINT_TARGET_TYPE_BODY,
		CONSTRAINT_TARGET_TYPE_GROUND,
		NUM_CONSTRAINT_TARGET_TYPES
	} EConstraintTargetType;

const std::string BVHSTATUS[] = {
    "E_ST_OK",
    "E_ST_EOF",
    "E_ST_NO_CONSTRAINT",
    "E_ST_NO_FILE",
    "E_ST_NO_HIER",
    "E_ST_NO_JOINT",
    "E_ST_NO_NAME",
    "E_ST_NO_OFFSET",
    "E_ST_NO_CHANNELS",
    "E_ST_NO_ROTATION",
    "E_ST_NO_AXIS",
    "E_ST_NO_MOTION",
    "E_ST_NO_FRAMES",
    "E_ST_NO_FRAME_TIME",
    "E_ST_NO_POS",
    "E_ST_NO_ROT",
    "E_ST_NO_XLT_FILE",
    "E_ST_NO_XLT_HEADER",
    "E_ST_NO_XLT_NAME",
    "E_ST_NO_XLT_IGNORE",
    "E_ST_NO_XLT_RELATIVE",
    "E_ST_NO_XLT_OUTNAME",
    "E_ST_NO_XLT_MATRIX",
    "E_ST_NO_XLT_MERGECHILD",
    "E_ST_NO_XLT_MERGEPARENT",
    "E_ST_NO_XLT_PRIORITY",
    "E_ST_NO_XLT_LOOP",
    "E_ST_NO_XLT_EASEIN",
    "E_ST_NO_XLT_EASEOUT",
    "E_ST_NO_XLT_HAND",
    "E_ST_NO_XLT_EMOTE",
    "E_ST_BAD_ROOT"
};

#endif // LL_LLBVHCONSTS_H

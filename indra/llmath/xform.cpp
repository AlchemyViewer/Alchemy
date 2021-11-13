/** 
 * @file xform.cpp
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "linden_common.h"

#include "xform.h"

LLXform::LLXform()
{
	init();
}

// Link optimization - don't inline these LL_WARNS()
void LLXform::warn(const char* const msg)
{
	LL_WARNS() << msg << LL_ENDL;
}

LLXform* LLXform::getRoot() const
{
	const LLXform* root = this;
	while(root->mParent)
	{
		root = root->mParent;
	}
	return (LLXform*)root;
}

BOOL LLXform::isRoot() const
{
	return (!mParent);
}

BOOL LLXform::isRootEdit() const
{
	return (!mParent);
}

void LLXformMatrix::update()
{
	if (mParent) 
	{
		mWorldPosition = mPosition;
		if (mParent->getScaleChildOffset())
		{
			mWorldPosition.scaleVec(mParent->getScale());
		}
		mWorldPosition *= mParent->getWorldRotation();
		mWorldPosition += mParent->getWorldPosition();
		mWorldRotation = mRotation * mParent->getWorldRotation();
	}
	else
	{
		mWorldPosition = mPosition;
		mWorldRotation = mRotation;
	}
}

void LLXformMatrix::updateMatrix(BOOL update_bounds)
{
	update();

	LLMatrix4 world_matrix;
	world_matrix.initAll(mScale, mWorldRotation, mWorldPosition);
	mWorldMatrix.loadu(world_matrix);

	if (update_bounds && (mChanged & MOVED))
	{
		mMax = mMin = mWorldMatrix.getRow<3>();

		LLVector4a total_sum,sum1,sum2;
		total_sum.setAbs(mWorldMatrix.getRow<0>());
		sum1.setAbs(mWorldMatrix.getRow<1>());
		sum2.setAbs(mWorldMatrix.getRow<2>());
		sum1.add(sum2);
		total_sum.add(sum1);
		total_sum.mul(.5f);

		mMax.add(total_sum);
		mMin.sub(total_sum);
	}
}

void LLXformMatrix::getMinMax(LLVector3& min, LLVector3& max) const
{
	min.set(mMin.getF32ptr());
	max.set(mMax.getF32ptr());
}

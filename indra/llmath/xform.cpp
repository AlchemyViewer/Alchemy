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

// LLXform default ctor and destructor are now inline constexpr defaulted in
// the header (using in-class member initialisers). isRoot/isRootEdit below
// serve as the vtable key function.

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

bool LLXform::isRoot() const
{
    return (!mParent);
}

bool LLXform::isRootEdit() const
{
    return (!mParent);
}

LLXformMatrix::~LLXformMatrix()
{
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

void LLXformMatrix::updateMatrix(bool update_bounds)
{
    update();

    mWorldMatrix.initAll(mScale, mWorldRotation, mWorldPosition);

    if (update_bounds && (mChanged & MOVED))
    {
        // Half the sum of the absolute basis rows is the extent the box has to
        // grow by on each axis; the same three sums the scalar version built a
        // component at a time.
        LLVector4a extent, row1, row2;
        extent.setAbs(mWorldMatrix.getRow<0>());
        row1.setAbs(mWorldMatrix.getRow<1>());
        row2.setAbs(mWorldMatrix.getRow<2>());
        row1.add(row2);
        extent.add(row1);
        extent.mul(0.5f);

        mMin = mMax = mWorldMatrix.getTranslation();
        mMin.sub(extent);
        mMax.add(extent);
    }
}

void LLXformMatrix::getMinMax(LLVector3& min, LLVector3& max) const
{
    min.set(mMin.getF32ptr());
    max.set(mMax.getF32ptr());
}

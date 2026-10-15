/**
 * @file xform_test.cpp
 * @author Adroit
 * @date March 2007
 * @brief Test cases for LLXform
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
#include <catch2/catch_test_macros.hpp>

#include "../xform.h"

namespace
{
    //test case for init(), getParent(), getRotation(), getPositionW(), getWorldRotation() fns.
    TEST_CASE("LLXForm: a default LLXform is an unparented identity transform", "[LLXForm]")
    {
        LLXform xform_obj;
        LLVector3 emptyVec(0.f,0.f,0.f);
        LLVector3 initialScaleVec(1.f,1.f,1.f);

        CHECK_FALSE(xform_obj.getParent());
        CHECK_FALSE(xform_obj.isChanged());
        CHECK(xform_obj.getPosition() == emptyVec);
        CHECK((xform_obj.getRotation()).isIdentity());
        CHECK(xform_obj.getScale() == initialScaleVec);
        CHECK(xform_obj.getPositionW() == emptyVec);
        CHECK((xform_obj.getWorldRotation()).isIdentity());
        CHECK_FALSE(xform_obj.getScaleChildOffset());
    }

    // test cases for
    // setScale(const LLVector3& scale)
    // setScale(const F32 x, const F32 y, const F32 z)
    // setRotation(const F32 x, const F32 y, const F32 z)
    // setPosition(const F32 x, const F32 y, const F32 z)
    // getLocalMat4(LLMatrix4 &mat)
    TEST_CASE("LLXForm: scale/position/rotation setters store what they are given", "[LLXForm]")
    {
        LLMatrix4 llmat4;
        LLXform xform_obj;

        F32 x = 3.6f;
        F32 y = 5.5f;
        F32 z = 4.2f;
        F32 w = 0.f;
        F32 posz = z + 2.122f;
        LLVector3 vec(x, y, z);
        xform_obj.setScale(x, y, z);
        xform_obj.setPosition(x, y, posz);
        CHECK(xform_obj.getScale() == vec);

        vec.setVec(x, y, posz);
        CHECK(xform_obj.getPosition() == vec);

        x = x * 2.f;
        y = y + 2.3f;
        z = posz * 4.f;
        vec.setVec(x, y, z);
        xform_obj.setPositionX(x);
        xform_obj.setPositionY(y);
        xform_obj.setPositionZ(z);
        CHECK(xform_obj.getPosition() == vec);

        xform_obj.setScaleChildOffset(true);
        CHECK(xform_obj.getScaleChildOffset());

        vec.setVec(x, y, z);

        xform_obj.addPosition(vec);
        vec += vec;
        CHECK(xform_obj.getPosition() == vec);

        xform_obj.setScale(vec);
        CHECK(xform_obj.getScale() == vec);

        LLQuaternion quat(x, y, z, w);
        xform_obj.setRotation(quat);
        CHECK(xform_obj.getRotation() == quat);

        xform_obj.setRotation(x, y, z, w);
        CHECK(xform_obj.getRotation() == quat);

        xform_obj.setRotation(x, y, z);
        quat.setQuat(x,y,z);
        CHECK(xform_obj.getRotation() == quat);

        // LLXform::setRotation(const F32 x, const F32 y, const F32 z)
        //      Does normalization
        // LLXform::setRotation(const F32 x, const F32 y, const F32 z, const F32 s)
        //      Simply copies the individual values - does not do any normalization.
        // Is that the expected behavior?
    }

    // test cases for inline bool setParent(LLXform *parent) and getParent() fn.
    TEST_CASE("LLXForm: setParent builds a chain that getRoot and isRoot follow", "[LLXForm]")
    {
        LLXform xform_obj;
        LLXform par;
        LLXform grandpar;
        xform_obj.setParent(&par);
        par.setParent(&grandpar);
        CHECK(&par == xform_obj.getParent());
        CHECK(&grandpar == xform_obj.getRoot());
        CHECK(grandpar.isRoot());
        CHECK_FALSE(par.isRoot());
        CHECK_FALSE(xform_obj.isRoot());
        CHECK(grandpar.isRootEdit());
        CHECK_FALSE(par.isRootEdit());
        CHECK_FALSE(xform_obj.isRootEdit());
    }

    TEST_CASE("LLXForm: setters raise their changed flags and clearChanged clears them", "[LLXForm]")
    {
        LLXform xform_obj;
        xform_obj.setChanged(LLXform::TRANSLATED | LLXform::ROTATED | LLXform::SCALED);
        CHECK(xform_obj.isChanged());

        xform_obj.clearChanged(LLXform::TRANSLATED | LLXform::ROTATED | LLXform::SCALED);
        CHECK_FALSE(xform_obj.isChanged());

        // isChanged(bits) is true when any of the bits is set, so each flag
        // is asked about on its own.
        LLVector3 llvect3(12.4f, -5.6f, 0.34f);
        xform_obj.setScale(llvect3);
        CHECK(xform_obj.isChanged(LLXform::SCALED));
        CHECK_FALSE(xform_obj.isChanged(LLXform::TRANSLATED));
        CHECK_FALSE(xform_obj.isChanged(LLXform::ROTATED));

        xform_obj.setPosition(1.2f, 2.3f, 3.4f);
        CHECK(xform_obj.isChanged(LLXform::TRANSLATED));
        UNSCOPED_INFO("setPosition must not clear SCALED");
        CHECK(xform_obj.isChanged(LLXform::SCALED));
        CHECK_FALSE(xform_obj.isChanged(LLXform::ROTATED));

        xform_obj.clearChanged(LLXform::SCALED);
        CHECK_FALSE(xform_obj.isChanged(LLXform::SCALED));
        UNSCOPED_INFO("clearing SCALED must leave TRANSLATED");
        CHECK(xform_obj.isChanged(LLXform::TRANSLATED));

        xform_obj.setRotation(1, 2, 3, 4);
        CHECK(xform_obj.isChanged(LLXform::ROTATED));
        CHECK_FALSE(xform_obj.isChanged(LLXform::SCALED));

        // MOVED is the three together.
        xform_obj.setScale(llvect3);
        CHECK(xform_obj.isChanged(LLXform::SCALED));
        xform_obj.clearChanged(LLXform::MOVED);
        CHECK_FALSE(xform_obj.isChanged());
    }

    //to test init() and getWorldMatrix() fns.
    TEST_CASE("LLXForm: LLXformMatrix init leaves an identity world matrix", "[LLXForm]")
    {
        LLXformMatrix formMatrix_obj;
        formMatrix_obj.init();
        LLMatrix4 mat4_obj;

        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[0][0] == 1.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[0][1] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[0][2] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[0][3] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[1][0] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[1][1] == 1.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[1][2] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[1][3] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[2][0] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[2][1] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[2][2] == 1.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[2][3] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[3][0] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[3][1] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[3][2] == 0.f);
        CHECK(formMatrix_obj.getWorldMatrix().mMatrix[3][3] == 1.f);
    }

    //to test mMin.clearVec() and mMax.clearVec() fns
    TEST_CASE("LLXForm: getMinMax after init puts both corners at the origin", "[LLXForm]")
    {
        LLXformMatrix formMatrix_obj;
        formMatrix_obj.init();
        // Not zero to begin with, so the zeros below are getMinMax's.
        LLVector3 llmin_vec3(1.f, 1.f, 1.f);
        LLVector3 llmax_vec3(1.f, 1.f, 1.f);
        formMatrix_obj.getMinMax(llmin_vec3, llmax_vec3);
        CHECK(llmin_vec3.mV[0] == 0.f);
        CHECK(llmin_vec3.mV[1] == 0.f);
        CHECK(llmin_vec3.mV[2] == 0.f);
        CHECK(llmax_vec3.mV[0] == 0.f);
        CHECK(llmax_vec3.mV[1] == 0.f);
        CHECK(llmax_vec3.mV[2] == 0.f);
    }

    //test case of update() fn.
    TEST_CASE("LLXForm: update composes world position and rotation with the parent", "[LLXForm]")
    {
        LLXformMatrix formMatrix_obj;

        LLXformMatrix parent;
        LLVector3 llvecpos(1.0, 2.0, 3.0);
        LLVector3 llvecpospar(10.0, 20.0, 30.0);
        formMatrix_obj.setPosition(llvecpos);
        parent.setPosition(llvecpospar);

        LLVector3 llvecparentscale(1.0, 2.0, 0);
        parent.setScaleChildOffset(true);
        parent.setScale(llvecparentscale);

        LLQuaternion quat(1, 2, 3, 4);
        LLQuaternion quatparent(5, 6, 7, 8);
        formMatrix_obj.setRotation(quat);
        parent.setRotation(quatparent);
        formMatrix_obj.setParent(&parent);

        parent.update();
        formMatrix_obj.update();

        LLVector3 worldPos = llvecpos;
        worldPos.scaleVec(llvecparentscale);
        worldPos *= quatparent;
        worldPos += llvecpospar;

        LLQuaternion worldRot = quat * quatparent;

        CHECK(formMatrix_obj.getWorldPosition() == worldPos);
        CHECK(formMatrix_obj.getWorldRotation() == worldRot);

        CHECK(parent.getWorldPosition() == llvecpospar);
        CHECK(parent.getWorldRotation() == quatparent);
    }
}


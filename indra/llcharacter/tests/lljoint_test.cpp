/**
 * @file lljoint_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief lljoint test cases.
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
#include "m4math.h"
#include "v3math.h"

#include "../lljoint.h"

#include "../test/lltut.h"


namespace tut
{
    struct lljoint_data
    {
    };
    typedef test_group<lljoint_data> lljoint_test;
    typedef lljoint_test::object lljoint_object;
    tut::lljoint_test lljoint_testcase("LLJoint");

    template<> template<>
    void lljoint_object::test<1>()
    {
        LLJoint lljoint;
        LLJoint* jnt = lljoint.getParent();
        ensure("getParent() failed ", (NULL == jnt));
        ensure("getRoot() failed ", (&lljoint == lljoint.getRoot()));
    }

    template<> template<>
    void lljoint_object::test<2>()
    {
        std::string str = "LLJoint";
        LLJoint parent(str), child;
        child.setup(str, &parent);
        LLJoint* jnt = child.getParent();
        ensure("setup() failed ", (&parent == jnt));
    }

    template<> template<>
    void lljoint_object::test<3>()
    {
        LLJoint parent, child;
        std::string str = "LLJoint";
        child.setup(str, &parent);
        LLJoint* jnt = parent.findJoint(str);
        ensure("findJoint() failed ", (&child == jnt));
    }

    template<> template<>
    void lljoint_object::test<4>()
    {
        LLJoint parent;
        std::string str1 = "LLJoint", str2;
        parent.setName(str1);
        str2 = parent.getName();
        ensure("setName() failed ", (str1 == str2));
    }

    template<> template<>
    void lljoint_object::test<5>()
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        // SL-315
        lljoint.setPosition(vec3);
        LLVector3 pos = lljoint.getPosition();
        ensure("setPosition()/getPosition() failed ", (vec3 == pos));
    }

    template<> template<>
    void lljoint_object::test<6>()
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        // SL-315
        lljoint.setWorldPosition(vec3);
        LLVector3 pos = lljoint.getWorldPosition();
        ensure("1:setWorldPosition()/getWorldPosition() failed ", (vec3 == pos));
        LLVector3 lastPos = lljoint.getLastWorldPosition();
        ensure("2:getLastWorldPosition failed ", (vec3 == lastPos));
    }

    template<> template<>
    void lljoint_object::test<7>()
    {
        LLJoint lljoint("LLJoint");
        LLQuaternion q(2.3f,30.f,10.f,1.f);
        lljoint.setRotation(q);
        LLQuaternion rot = lljoint.getRotation();
        ensure("setRotation()/getRotation() failed ", (q == rot));
    }
    template<> template<>
    void lljoint_object::test<8>()
    {
        LLJoint lljoint("LLJoint");
        LLQuaternion q(2.3f,30.f,10.f,1.f);
        lljoint.setWorldRotation(q);
        LLQuaternion rot = lljoint.getWorldRotation();
        ensure("1:setWorldRotation()/getWorldRotation() failed ", (q == rot));
        LLQuaternion lastRot = lljoint.getLastWorldRotation();
        ensure("2:getLastWorldRotation failed ", (q == lastRot));
    }

    template<> template<>
    void lljoint_object::test<9>()
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        lljoint.setScale(vec3);
        LLVector3 scale = lljoint.getScale();
        ensure("setScale()/getScale failed ", (vec3 == scale));
    }

    template<> template<>
    void lljoint_object::test<10>()
    {
        LLJoint lljoint("LLJoint");
        LLMatrix4 mat;
        mat.setIdentity();
        lljoint.setWorldMatrix(mat);//giving warning setWorldMatrix not correctly implemented;
        LLMatrix4 mat4 = lljoint.getWorldMatrix().toMatrix4();
        ensure("setWorldMatrix()/getWorldMatrix failed ", (mat4 == mat));
    }

    template<> template<>
    void lljoint_object::test<11>()
    {
        S32 joint_num = 12;
        LLJoint lljoint(joint_num);
        lljoint.setName("parent");
        S32 jointNum =  lljoint.getJointNum();
        ensure("getJointNum failed ", (jointNum == joint_num));
    }

    template<> template<>
    void lljoint_object::test<12>()
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        lljoint.setSkinOffset(vec3);
        LLVector3 offset = lljoint.getSkinOffset();
        ensure("1:setSkinOffset()/getSkinOffset() failed ", (vec3 == offset));
    }

    template<> template<>
    void lljoint_object::test<13>()
    {
        LLJoint lljointgp("gparent");
        LLJoint lljoint("parent");
        LLJoint lljoint1("child1");
        lljoint.addChild(&lljoint1);
        LLJoint lljoint2("child2");
        lljoint.addChild(&lljoint2);
        LLJoint lljoint3("child3");
        lljoint.addChild(&lljoint3);

        LLJoint* jnt = NULL;
        jnt = lljoint2.getParent();
        ensure("addChild() failed ", (&lljoint == jnt));
        LLJoint* jnt1 = lljoint.findJoint("child3");
        ensure("findJoint() failed ", (&lljoint3 == jnt1));
        lljoint.removeChild(&lljoint3);
        LLJoint* jnt2 = lljoint.findJoint("child3");
        ensure("removeChild() failed ", (NULL == jnt2));

        lljointgp.addChild(&lljoint);
        ensure("GetParent() failed ", (&lljoint== lljoint2.getParent()));
        ensure("getRoot() failed ", (&lljointgp == lljoint2.getRoot()));

        ensure("getRoot() failed ", &lljoint1 == lljoint.findJoint("child1"));

        lljointgp.removeAllChildren();
        // parent removed from grandparent - so should not be able to locate child
        ensure("removeAllChildren() failed ", (NULL == lljointgp.findJoint("child1")));
        // it should still exist in parent though
        ensure("removeAllChildren() failed ", (&lljoint1 == lljoint.findJoint("child1")));
    }

    template<> template<>
    void lljoint_object::test<14>()
    {
        LLJoint lljointgp("gparent");

        LLJoint llparent1("parent1");
        LLJoint llparent2("parent2");

        LLJoint llchild("child1");
        LLJoint lladoptedchild("child2");
        llparent1.addChild(&llchild);
        llparent1.addChild(&lladoptedchild);

        llparent2.addChild(&lladoptedchild);
        ensure("1. addChild failed to remove prior parent", lladoptedchild.getParent() == &llparent2);
        ensure("2. addChild failed to remove prior parent", llparent1.findJoint("child2") == NULL);
    }

    template<> template<>
    void lljoint_object::test<15>()
    {
        // updateWorldMatrixChildren() reports how many world matrices it
        // recomputed. A joint is recomputed only while MATRIX_DIRTY is set,
        // and only below joints that still have mUpdateXform.
        LLJoint root, a, b, c, d;
        root.setup("root");
        a.setup("a", &root);
        b.setup("b", &a);
        c.setup("c", &a);
        d.setup("d", &root);

        ensure_equals("fresh tree recomputes every joint", root.updateWorldMatrixChildren(), 5);
        ensure_equals("clean tree recomputes nothing", root.updateWorldMatrixChildren(), 0);

        root.setRotation(LLQuaternion(0.5f, LLVector3::x_axis));
        ensure_equals("root rotation dirties the whole tree", root.updateWorldMatrixChildren(), 5);

        a.setRotation(LLQuaternion(0.25f, LLVector3::y_axis));
        ensure_equals("mid-level rotation dirties its subtree", root.updateWorldMatrixChildren(), 3);

        root.setPosition(root.getPosition());
        ensure_equals("unchanged position dirties nothing", root.updateWorldMatrixChildren(), 0);

        root.setPosition(LLVector3(1.f, 2.f, 3.f));
        ensure_equals("root position dirties the whole tree", root.updateWorldMatrixChildren(), 5);

        a.mUpdateXform = false;
        root.setRotation(LLQuaternion(0.75f, LLVector3::z_axis));
        ensure_equals("subtree without mUpdateXform is skipped", root.updateWorldMatrixChildren(), 2);
    }


    template<> template<>
    void lljoint_object::test<16>()
    {
        // Writing an unchanged rotation must not dirty the joint, since
        // touch() would carry that down the entire subtree.
        LLJoint root, child;
        root.setup("root");
        child.setup("child", &root);

        root.updateWorldMatrixChildren();
        ensure_equals("tree starts clean", root.updateWorldMatrixChildren(), 0);

        const LLQuaternion rot(0.4f, LLVector3::y_axis);
        root.setRotation(rot);
        ensure_equals("a new rotation dirties the subtree", root.updateWorldMatrixChildren(), 2);

        root.setRotation(rot);
        ensure_equals("rewriting the same rotation dirties nothing", root.updateWorldMatrixChildren(), 0);

        root.setRotation(LLQuaternion(0.9f, LLVector3::z_axis));
        ensure_equals("a different rotation dirties the subtree again", root.updateWorldMatrixChildren(), 2);
    }


    template<> template<>
    void lljoint_object::test<17>()
    {
        // A sitting avatar hangs its root joint off the seat's transform,
        // which is no joint, and then writes seat relative values into the
        // root. Those hold still while the seat moves, and touch() only
        // travels down from a joint that was written to, so the skeleton
        // never hears that the seat carried it somewhere.
        LLXformMatrix seat;
        seat.setPosition(LLVector3(1.f, 0.f, 0.f));
        seat.updateMatrix();

        LLJoint root, child;
        root.setup("root");
        child.setup("child", &root);
        root.getXform()->setParent(&seat);

        root.updateWorldMatrixChildren();
        ensure_equals("tree starts clean", root.updateWorldMatrixChildren(), 0);

        seat.setPosition(LLVector3(5.f, 0.f, 0.f));
        seat.updateMatrix();
        ensure_equals("a moved seat dirties nothing on its own", root.updateWorldMatrixChildren(), 0);

        root.touchIfXformParentMoved();
        ensure_equals("the seat's move dirties the whole tree", root.updateWorldMatrixChildren(), 2);
        ensure("the root followed the seat", root.getWorldPosition() == LLVector3(5.f, 0.f, 0.f));

        root.touchIfXformParentMoved();
        ensure_equals("a still seat dirties nothing", root.updateWorldMatrixChildren(), 0);

        seat.setRotation(LLQuaternion(0.5f, LLVector3::z_axis));
        seat.updateMatrix();
        root.touchIfXformParentMoved();
        ensure_equals("a rotated seat dirties the whole tree", root.updateWorldMatrixChildren(), 2);
    }

    template<> template<>
    void lljoint_object::test<18>()
    {
        // The avatar root is written every frame from a slerp toward a target
        // that has stopped moving, with an interpolant taken from the frame
        // time. slerp blends its two arguments instead of returning either,
        // so the root lands a rounding short of where it already was, and a
        // different frame time rounds a different way: it never arrives, and
        // the equality compare in setRotation never fires.
        LLJoint root, child;
        root.setup("root");
        child.setup("child", &root);

        const LLQuaternion target(0.4f, LLVector3::y_axis);
        root.setWorldRotationIfMoved(LLQuaternion(0.42f, LLVector3::y_axis));
        root.updateWorldMatrixChildren();

        // Long enough to catch up: the gap closes by a factor of u each frame.
        for (S32 frame = 0; frame < 600; ++frame)
        {
            const F32 u = (0.010f + 0.006f * (frame % 5)) / 0.4f;
            root.setWorldRotationIfMoved(slerp(u, root.getWorldRotation(), target));
        }
        root.updateWorldMatrixChildren();

        for (S32 frame = 0; frame < 8; ++frame)
        {
            const F32 u = (0.010f + 0.006f * (frame % 5)) / 0.4f;
            root.setWorldRotationIfMoved(slerp(u, root.getWorldRotation(), target));
        }
        ensure_equals("a root that has caught up with its target stops dirtying the tree",
                      root.updateWorldMatrixChildren(), 0);

        // What it stopped short by is the whole cost of the tolerance.
        ensure("the root stopped within a hundredth of a degree of its target",
               root.getWorldRotation().isEqualEps(target, 1.e-4f));

        // A real turn still has to reach the skeleton.
        root.setWorldRotationIfMoved(LLQuaternion(0.4f + 1.f * DEG_TO_RAD, LLVector3::y_axis));
        ensure_equals("a real turn dirties the whole tree", root.updateWorldMatrixChildren(), 2);
    }

    /*
        Test cases for the following not added. They perform operations
        on underlying LLXformMatrix and LLVector3 elements which have
        been unit tested separately.
        Unit Testing these functions will basically require re-implementing
        logic of these function in the test case itself

        1) void updateWorldMatrixParent();
        2) void updateWorldPRSParent();
        3) void updateWorldMatrix();
        4) LLXformMatrix *getXform() { return &mXform; }
        5) void setConstraintSilhouette(LLDynamicArray<LLVector3>& silhouette);
        6) void clampRotation(LLQuaternion old_rot, LLQuaternion new_rot);

    */
}


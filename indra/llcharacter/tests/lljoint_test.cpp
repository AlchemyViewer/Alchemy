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

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"


namespace
{
    TEST_CASE("LLJoint: a lone joint has no parent and is its own root", "[LLJoint]")
    {
        LLJoint lljoint;
        LLJoint* jnt = lljoint.getParent();
        CHECK(jnt == NULL);
        CHECK(&lljoint == lljoint.getRoot());
    }

    TEST_CASE("LLJoint: setup attaches a joint to its parent", "[LLJoint]")
    {
        std::string str = "LLJoint";
        LLJoint parent(str), child;
        child.setup(str, &parent);
        LLJoint* jnt = child.getParent();
        CHECK(&parent == jnt);
    }

    TEST_CASE("LLJoint: findJoint finds a child by name", "[LLJoint]")
    {
        LLJoint parent, child;
        std::string str = "LLJoint";
        child.setup(str, &parent);
        LLJoint* jnt = parent.findJoint(str);
        CHECK(&child == jnt);
    }

    TEST_CASE("LLJoint: setName and getName round-trip", "[LLJoint]")
    {
        LLJoint parent;
        std::string str1 = "LLJoint", str2;
        parent.setName(str1);
        str2 = parent.getName();
        CHECK(str1 == str2);
    }

    TEST_CASE("LLJoint: setPosition and getPosition round-trip", "[LLJoint]")
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        // SL-315
        lljoint.setPosition(vec3);
        LLVector3 pos = lljoint.getPosition();
        CHECK(vec3 == pos);
    }

    TEST_CASE("LLJoint: setWorldPosition sets the world and last world positions", "[LLJoint]")
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        // SL-315
        lljoint.setWorldPosition(vec3);
        LLVector3 pos = lljoint.getWorldPosition();
        CHECK(vec3 == pos);
        LLVector3 lastPos = lljoint.getLastWorldPosition();
        CHECK(vec3 == lastPos);
    }

    TEST_CASE("LLJoint: setRotation and getRotation round-trip", "[LLJoint]")
    {
        LLJoint lljoint("LLJoint");
        LLQuaternion q(2.3f,30.f,10.f,1.f);
        lljoint.setRotation(q);
        LLQuaternion rot = lljoint.getRotation();
        CHECK(q == rot);
    }
    TEST_CASE("LLJoint: setWorldRotation sets the world and last world rotations", "[LLJoint]")
    {
        LLJoint lljoint("LLJoint");
        LLQuaternion q(2.3f,30.f,10.f,1.f);
        lljoint.setWorldRotation(q);
        LLQuaternion rot = lljoint.getWorldRotation();
        CHECK(q == rot);
        LLQuaternion lastRot = lljoint.getLastWorldRotation();
        CHECK(q == lastRot);
    }

    TEST_CASE("LLJoint: setScale and getScale round-trip", "[LLJoint]")
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        lljoint.setScale(vec3);
        LLVector3 scale = lljoint.getScale();
        CHECK(vec3 == scale);
    }

    TEST_CASE("LLJoint: setWorldMatrix and getWorldMatrix round-trip the identity", "[LLJoint]")
    {
        LLJoint lljoint("LLJoint");
        LLMatrix4 mat;
        mat.setIdentity();
        lljoint.setWorldMatrix(mat);//giving warning setWorldMatrix not correctly implemented;
        LLMatrix4 mat4 = lljoint.getWorldMatrix().toMatrix4();
        CHECK(mat4 == mat);
    }

    TEST_CASE("LLJoint: the constructor stores the joint number", "[LLJoint]")
    {
        S32 joint_num = 12;
        LLJoint lljoint(joint_num);
        lljoint.setName("parent");
        S32 jointNum =  lljoint.getJointNum();
        CHECK(jointNum == joint_num);
    }

    TEST_CASE("LLJoint: setSkinOffset and getSkinOffset round-trip", "[LLJoint]")
    {
        LLJoint lljoint;
        LLVector3 vec3(2.3f,30.f,10.f);
        lljoint.setSkinOffset(vec3);
        LLVector3 offset = lljoint.getSkinOffset();
        CHECK(vec3 == offset);
    }

    TEST_CASE("LLJoint: addChild and removeChild keep the tree's links and lookups", "[LLJoint]")
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
        CHECK(&lljoint == jnt);
        LLJoint* jnt1 = lljoint.findJoint("child3");
        CHECK(&lljoint3 == jnt1);
        lljoint.removeChild(&lljoint3);
        LLJoint* jnt2 = lljoint.findJoint("child3");
        CHECK(jnt2 == NULL);

        lljointgp.addChild(&lljoint);
        CHECK(&lljointgp == lljoint.getParent());
        CHECK(&lljointgp == lljoint2.getRoot());

        CHECK(&lljoint1 == lljoint.findJoint("child1"));

        lljointgp.removeAllChildren();
        // parent removed from grandparent - so should not be able to locate child
        CHECK(lljointgp.findJoint("child1") == NULL);
        // it should still exist in parent though
        CHECK(&lljoint1 == lljoint.findJoint("child1"));
    }

    TEST_CASE("LLJoint: addChild takes a child away from its previous parent", "[LLJoint]")
    {
        LLJoint lljointgp("gparent");

        LLJoint llparent1("parent1");
        LLJoint llparent2("parent2");

        LLJoint llchild("child1");
        LLJoint lladoptedchild("child2");
        llparent1.addChild(&llchild);
        llparent1.addChild(&lladoptedchild);

        llparent2.addChild(&lladoptedchild);
        CHECK(lladoptedchild.getParent() == &llparent2);
        CHECK(llparent1.findJoint("child2") == NULL);
    }

    TEST_CASE("LLJoint: updateWorldMatrixChildren recomputes only dirty updating joints", "[LLJoint]")
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

        UNSCOPED_INFO("fresh tree recomputes every joint");
        CHECK(root.updateWorldMatrixChildren() == 5);
        UNSCOPED_INFO("clean tree recomputes nothing");
        CHECK(root.updateWorldMatrixChildren() == 0);

        root.setRotation(LLQuaternion(0.5f, LLVector3::x_axis));
        UNSCOPED_INFO("root rotation dirties the whole tree");
        CHECK(root.updateWorldMatrixChildren() == 5);

        a.setRotation(LLQuaternion(0.25f, LLVector3::y_axis));
        UNSCOPED_INFO("mid-level rotation dirties its subtree");
        CHECK(root.updateWorldMatrixChildren() == 3);

        root.setPosition(root.getPosition());
        UNSCOPED_INFO("unchanged position dirties nothing");
        CHECK(root.updateWorldMatrixChildren() == 0);

        root.setPosition(LLVector3(1.f, 2.f, 3.f));
        UNSCOPED_INFO("root position dirties the whole tree");
        CHECK(root.updateWorldMatrixChildren() == 5);

        a.setUpdateXform(false);
        root.setRotation(LLQuaternion(0.75f, LLVector3::z_axis));
        UNSCOPED_INFO("subtree without mUpdateXform is skipped");
        CHECK(root.updateWorldMatrixChildren() == 2);

        // Coming back into the sweep rebuilds what was missed while out of it.
        a.setUpdateXform(true);
        UNSCOPED_INFO("rejoining the sweep recomputes the subtree");
        CHECK(root.updateWorldMatrixChildren() == 3);
    }


    TEST_CASE("LLJoint: rewriting the same rotation does not dirty the joint", "[LLJoint]")
    {
        // Writing an unchanged rotation must not dirty the joint, since
        // touch() would carry that down the entire subtree.
        LLJoint root, child;
        root.setup("root");
        child.setup("child", &root);

        root.updateWorldMatrixChildren();
        UNSCOPED_INFO("tree starts clean");
        CHECK(root.updateWorldMatrixChildren() == 0);

        const LLQuaternion rot(0.4f, LLVector3::y_axis);
        root.setRotation(rot);
        UNSCOPED_INFO("a new rotation dirties the subtree");
        CHECK(root.updateWorldMatrixChildren() == 2);

        root.setRotation(rot);
        CHECK(root.updateWorldMatrixChildren() == 0);

        root.setRotation(LLQuaternion(0.9f, LLVector3::z_axis));
        UNSCOPED_INFO("a different rotation dirties the subtree again");
        CHECK(root.updateWorldMatrixChildren() == 2);
    }


    TEST_CASE("LLJoint: touchIfXformParentMoved dirties the tree when the seat moves", "[LLJoint]")
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
        UNSCOPED_INFO("tree starts clean");
        CHECK(root.updateWorldMatrixChildren() == 0);

        seat.setPosition(LLVector3(5.f, 0.f, 0.f));
        seat.updateMatrix();
        UNSCOPED_INFO("a moved seat dirties nothing on its own");
        CHECK(root.updateWorldMatrixChildren() == 0);

        root.touchIfXformParentMoved();
        UNSCOPED_INFO("the seat's move dirties the whole tree");
        CHECK(root.updateWorldMatrixChildren() == 2);
        UNSCOPED_INFO("the root followed the seat");
        CHECK(root.getWorldPosition() == LLVector3(5.f, 0.f, 0.f));

        root.touchIfXformParentMoved();
        UNSCOPED_INFO("a still seat dirties nothing");
        CHECK(root.updateWorldMatrixChildren() == 0);

        seat.setRotation(LLQuaternion(0.5f, LLVector3::z_axis));
        seat.updateMatrix();
        root.touchIfXformParentMoved();
        UNSCOPED_INFO("a rotated seat dirties the whole tree");
        CHECK(root.updateWorldMatrixChildren() == 2);
    }

    TEST_CASE("LLJoint: a root slerping onto a still target stops dirtying the tree", "[LLJoint]")
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
        CHECK(root.updateWorldMatrixChildren() == 0);

        // What it stopped short by is the whole cost of the tolerance.
        UNSCOPED_INFO("the root stopped within a hundredth of a degree of its target");
        CHECK(root.getWorldRotation().isEqualEps(target, 1.e-4f));

        // A real turn still has to reach the skeleton.
        root.setWorldRotationIfMoved(LLQuaternion(0.4f + 1.f * DEG_TO_RAD, LLVector3::y_axis));
        UNSCOPED_INFO("a real turn dirties the whole tree");
        CHECK(root.updateWorldMatrixChildren() == 2);
    }

    TEST_CASE("LLJoint: the sweep descends only into marked subtrees", "[LLJoint]")
    {
        // The sweep walks only where something told it to. A joint carrying
        // neither a dirty matrix of its own nor the mark left by a write below
        // it is not descended into at all, which is what makes an idle
        // skeleton cost one visit instead of a hundred and thirty four.
        LLJoint root, a, b, c, d, e;
        root.setup("root");
        a.setup("a", &root);
        b.setup("b", &a);
        c.setup("c", &b);
        d.setup("d", &root);
        e.setup("e", &d);

        root.updateWorldMatrixChildren();
        UNSCOPED_INFO("tree starts clean");
        CHECK(root.updateWorldMatrixChildren() == 0);

        c.setRotation(LLQuaternion(0.3f, LLVector3::x_axis));
        UNSCOPED_INFO("the write is announced all the way up");
        CHECK((root.mDirtyFlags & LLJoint::SUBTREE_DIRTY) != 0);
        UNSCOPED_INFO("the sibling branch is left alone");
        CHECK(d.mDirtyFlags == 0);

        // Dirty a joint behind the sweep's back, below a branch nothing
        // marked. The sweep must stop at that branch's root and never look at
        // it -- the old walk visited every joint and would have rebuilt it.
        e.mDirtyFlags |= LLJoint::MATRIX_DIRTY;
        UNSCOPED_INFO("only the marked path is walked");
        CHECK(root.updateWorldMatrixChildren() == 1);
        UNSCOPED_INFO("the unmarked branch was never descended into");
        CHECK((e.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);

        UNSCOPED_INFO("the mark is cleared once the subtree is clean");
        CHECK((root.mDirtyFlags & LLJoint::SUBTREE_DIRTY) == 0);
    }

    TEST_CASE("LLJoint: getWorldMatrix leaves the subtree mark for the sweep", "[LLJoint]")
    {
        // getWorldMatrix rebuilds this joint and the ones above it on the
        // spot, without ever walking down. It must not carry the subtree mark
        // off with it: the joints below have still not been rebuilt, and the
        // sweep is the only thing that will do it.
        LLJoint root, a, b;
        root.setup("root");
        a.setup("a", &root);
        b.setup("b", &a);

        root.updateWorldMatrixChildren();
        UNSCOPED_INFO("tree starts clean");
        CHECK(root.updateWorldMatrixChildren() == 0);

        root.setRotation(LLQuaternion(0.3f, LLVector3::z_axis));
        b.setRotation(LLQuaternion(0.2f, LLVector3::x_axis));
        {
            INFO("the root carries both its own dirt and the mark");
            CHECK((root.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);
            CHECK((root.mDirtyFlags & LLJoint::SUBTREE_DIRTY) != 0);
        }

        root.getWorldMatrix();
        UNSCOPED_INFO("the joints below still get their turn");
        CHECK(root.updateWorldMatrixChildren() == 2);
    }

    TEST_CASE("LLJoint: world transforms are current without a sweep in between", "[LLJoint]")
    {
        // A culled avatar does not get swept at all, which is only safe
        // because skipping the sweep costs nothing but time: the dirty flags
        // stay where they are, and anything that does ask for a world
        // transform in the meantime rebuilds what it needs by itself.
        LLJoint root, a, b;
        root.setup("root");
        a.setup("a", &root);
        b.setup("b", &a);
        root.updateWorldMatrixChildren();

        root.setPosition(LLVector3(1.f, 0.f, 0.f));
        UNSCOPED_INFO("a world position is current with no sweep in between");
        CHECK(b.getWorldPosition() == LLVector3(1.f, 0.f, 0.f));
        UNSCOPED_INFO("and the sweep still owes the matrices");
        CHECK(root.updateWorldMatrixChildren() == 3);

        root.setPosition(LLVector3(2.f, 0.f, 0.f));
        const LLVector3 world_translation(b.getWorldMatrix().getTranslation().getF32ptr());
        UNSCOPED_INFO("a world matrix is current with no sweep in between");
        CHECK(world_translation == LLVector3(2.f, 0.f, 0.f));
        UNSCOPED_INFO("and asking for it was the sweep's work");
        CHECK(root.updateWorldMatrixChildren() == 0);
    }

    TEST_CASE("LLJoint: a joint healed before the sweep still sends it to its children", "[LLJoint]")
    {
        // The joint that was written is healed before the sweep -- the avatar
        // asks its root for a world matrix right after moving it -- and the
        // heal clears that joint's own matrix flag. Its children are still
        // waiting, so it has to keep saying so, or the sweep turns back at it
        // and leaves the skeleton below stale for as long as the pose holds.
        LLJoint root, a, b;
        root.setup("root");
        a.setup("a", &root);
        b.setup("b", &a);
        root.updateWorldMatrixChildren();

        root.touch();
        root.setWorldPosition(LLVector3(1.f, 0.f, 0.f));
        root.getWorldMatrix();
        UNSCOPED_INFO("a healed root still sends the sweep down to its children");
        CHECK(root.updateWorldMatrixChildren() == 2);

        a.setRotation(LLQuaternion(0.3f, LLVector3::x_axis));
        a.getWorldMatrix();
        UNSCOPED_INFO("and so does a healed joint in the middle of the tree");
        CHECK(root.updateWorldMatrixChildren() == 1);
        UNSCOPED_INFO("after which the tree is clean");
        CHECK(root.updateWorldMatrixChildren() == 0);
    }

    TEST_CASE("LLJoint: an unchanged scale does not dirty the joint", "[LLJoint]")
    {
        // A scale written back unchanged is not a change. The pose blender
        // writes every channel of a joint each interpolated frame, so a
        // rotation-only blend must not dirty the skeleton through the scale.
        LLJoint root, a;
        root.setup("root");
        a.setup("a", &root);
        root.setScale(LLVector3(2.f, 2.f, 2.f));
        root.updateWorldMatrixChildren();
        {
            INFO("tree starts clean");
            CHECK(root.mDirtyFlags == 0);
            CHECK(a.mDirtyFlags == 0);
        }

        root.setScale(LLVector3(2.f, 2.f, 2.f));
        CHECK(root.mDirtyFlags == 0);
        CHECK(a.mDirtyFlags == 0);

        root.setScale(LLVector3(3.f, 2.f, 2.f));
        UNSCOPED_INFO("a different scale dirties the joint");
        CHECK((root.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);
        UNSCOPED_INFO("and its children");
        CHECK((a.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);
    }

    TEST_CASE("LLJoint: setWorldRotation round-trips whatever the parent's scale", "[LLJoint]")
    {
        // A world rotation asked for is the world rotation that comes back.
        // This is the route the foot solver takes six times a frame for a
        // standing avatar.
        LLJoint parent("parent", nullptr);
        LLJoint child("child", &parent);

        const LLQuaternion parent_rot(0.8f, LLVector3(0.f, 0.f, 1.f));
        const LLQuaternion wanted(-1.3f, LLVector3(0.577f, 0.577f, 0.577f));

        parent.setRotation(parent_rot);
        parent.setPosition(LLVector3(3.f, -1.f, 2.f));

        // Whatever scale the parent carries. The world rotation is built from
        // local rotations alone, so a scale has no business in this answer.
        const LLVector3 scales[] = {
            LLVector3(1.f, 1.f, 1.f),
            LLVector3(1.5f, 1.5f, 1.5f),
            LLVector3(0.25f, 0.25f, 0.25f),
            LLVector3(2.f, 0.5f, 1.3f),
        };

        for (const LLVector3& scale : scales)
        {
            parent.setScale(scale);
            child.setRotation(LLQuaternion::DEFAULT);
            child.setWorldRotation(wanted);

            const LLQuaternion got = child.getWorldRotation();
            REQUIRE_THAT(fabsf(dot(got, wanted)), ll_test::WithinBits(1.f, 16));
        }
    }

    TEST_CASE("LLJoint: setWorldRotation matches the matrix route under an unscaled parent", "[LLJoint]")
    {
        // With an unscaled parent it is the same local rotation the matrix
        // route arrived at, so the joints that were already getting the right
        // answer keep it. Written out here rather than called, so the two are
        // separate implementations.
        LLJoint parent("parent", nullptr);
        LLJoint child("child", &parent);

        parent.setRotation(LLQuaternion(2.1f, LLVector3(0.f, 1.f, 0.f)));
        parent.setPosition(LLVector3(-4.f, 5.f, 6.f));
        parent.setScale(LLVector3(1.f, 1.f, 1.f));

        const LLQuaternion wanted(0.55f, LLVector3(1.f, 0.f, 0.f));

        LLMatrix4 temp_mat(wanted);
        LLMatrix4 parent_world = parent.getWorldMatrix().toMatrix4();
        parent_world.mMatrix[VW][VX] = 0.f;
        parent_world.mMatrix[VW][VY] = 0.f;
        parent_world.mMatrix[VW][VZ] = 0.f;
        temp_mat *= parent_world.invert();
        const LLQuaternion through_the_matrix(temp_mat);

        child.setWorldRotation(wanted);

        CHECK_THAT(fabsf(dot(child.getRotation(), through_the_matrix)), ll_test::WithinBits(1.f, 16));
    }

    TEST_CASE("LLJoint: a scaled parent defeats the matrix route but not setWorldRotation", "[LLJoint]")
    {
        // The route this replaced could not do that. It built the answer out
        // of matrices, and LLMatrix4::invert transposes -- which inverts a
        // rotation but leaves a scale where it was -- so the product it took a
        // quaternion out of carried the parent's scale, and the extraction
        // adds one to the trace before taking a root, which the later
        // normalize cannot undo. Written out here so the difference is on the
        // record rather than in a commit message.
        LLJoint parent("parent", nullptr);
        LLJoint child("child", &parent);

        const LLQuaternion wanted(-1.3f, LLVector3(0.577f, 0.577f, 0.577f));
        parent.setRotation(LLQuaternion(0.8f, LLVector3(0.f, 0.f, 1.f)));
        parent.setScale(LLVector3(1.5f, 1.5f, 1.5f));

        LLMatrix4 temp_mat(wanted);
        LLMatrix4 parent_world = parent.getWorldMatrix().toMatrix4();
        parent_world.mMatrix[VW][VX] = 0.f;
        parent_world.mMatrix[VW][VY] = 0.f;
        parent_world.mMatrix[VW][VZ] = 0.f;
        temp_mat *= parent_world.invert();

        child.setRotation(LLQuaternion(temp_mat));
        const LLQuaternion through_the_matrix = child.getWorldRotation();

        child.setRotation(LLQuaternion::DEFAULT);
        child.setWorldRotation(wanted);
        const LLQuaternion now = child.getWorldRotation();

        UNSCOPED_INFO("a scaled parent gives back what was asked for");
        CHECK_THAT(fabsf(dot(now, wanted)), ll_test::WithinBits(1.f, 16));
        UNSCOPED_INFO("which the matrix route it replaced did not");
        CHECK(fabsf(dot(through_the_matrix, wanted)) < 0.999f);
    }

    TEST_CASE("LLJoint: setWorldRotation undoes a parent rotation that is not unit length", "[LLJoint]")
    {
        // Nothing normalizes a rotation on its way into a joint, so a parent
        // can be carrying one that has drifted off unit length. The world
        // rotation asked for is still the one that comes back, length and all:
        // the parent is undone with its inverse, not its conjugate, and those
        // differ by exactly that length.
        LLJoint parent("parent", nullptr);
        LLJoint child("child", &parent);

        LLQuaternion drifted(0.3f, 0.4f, 0.5f, 0.6f);
        UNSCOPED_INFO("the parent's rotation is not unit length");
        CHECK(fabsf(drifted.mQ[0]*drifted.mQ[0] + drifted.mQ[1]*drifted.mQ[1]
                   + drifted.mQ[2]*drifted.mQ[2] + drifted.mQ[3]*drifted.mQ[3] - 1.f) > 0.05f);
        parent.setRotation(drifted);

        const LLQuaternion wanted(-1.3f, LLVector3(0.577f, 0.577f, 0.577f));
        child.setWorldRotation(wanted);

        const LLQuaternion got = child.getWorldRotation();
        for (S32 i = 0; i < 4; ++i)
        {
            REQUIRE_THAT(got.mQ[i], ll_test::WithinBits(wanted.mQ[i], 14));
        }
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


/**
 * @file llrendermatrix_test.cpp
 * @brief The matrix stack, the projection helpers and the clip-plane
 *        classes, against the scalar matrix and the formulas.
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

#include "linden_common.h"

#include "../llrender.h"
#include "../llgl.h"
#include "alprojection.h"
#include "llplane.h"
#include "m4math.h"
#include "llquaternion.h"

#include "../test/lltut.h"

#include <cstring>
#include <sstream>

namespace tut
{
    struct llrendermatrix_data
    {
        // Every case starts from identity stacks and the forward depth
        // convention, and leaves them so.
        llrendermatrix_data()
        {
            reset();
        }

        ~llrendermatrix_data()
        {
            reset();
        }

        static void reset()
        {
            LLRender::sReverseZ = false;
            for (LLRender::eMatrixMode mode : { LLRender::MM_MODELVIEW, LLRender::MM_PROJECTION, LLRender::MM_TEXTURE0 })
            {
                gGL.matrixMode(mode);
                gGL.loadIdentity();
            }
            gGL.matrixMode(LLRender::MM_MODELVIEW);
        }
    };

    typedef test_group<llrendermatrix_data> llrendermatrix_test;
    typedef llrendermatrix_test::object     llrendermatrix_object;
    tut::llrendermatrix_test llrendermatrix_testgroup("llrendermatrix");

    // Every element within tolerance of the larger magnitude, or of one.
    void ensure_matrix_close(const std::string& what, const LLMatrix4a& actual, const LLMatrix4a& expected, F32 tolerance)
    {
        const F32* a = actual.getF32ptr();
        const F32* e = expected.getF32ptr();
        for (S32 i = 0; i < 16; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(a[i]), fabsf(e[i]));
            std::ostringstream msg;
            msg << what << " at element " << i << ": " << a[i] << " vs " << e[i];
            ensure_approximately_equals_range(msg.str().c_str(), a[i], e[i], tolerance * scale);
        }
    }

    void ensure_vector_close(const std::string& what, const LLVector4a& actual, const LLVector4a& expected, F32 tolerance)
    {
        for (S32 i = 0; i < 3; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(actual[i]), fabsf(expected[i]));
            std::ostringstream msg;
            msg << what << " at lane " << i << ": " << actual[i] << " vs " << expected[i];
            ensure_approximately_equals_range(msg.str().c_str(), actual[i], expected[i], tolerance * scale);
        }
    }

    bool same_bytes(const LLMatrix4a& a, const LLMatrix4a& b)
    {
        return std::memcmp(a.getF32ptr(), b.getF32ptr(), sizeof(F32) * 16) == 0;
    }

    // The reverse-Z rewrite on the floats: the depth lane of every row
    // becomes half of the w lane less the depth lane.
    LLMatrix4a reversed_z(const LLMatrix4a& p)
    {
        LLMatrix4a r = p;
        for (S32 row = 0; row < 4; ++row)
        {
            r.mMatrix[row].getF32ptr()[2] = 0.5f * (p.mMatrix[row][3] - p.mMatrix[row][2]);
        }
        return r;
    }

    const F32 M_ROWS[16] = { 0.6f, 0.8f, 0.f, 0.f,
                             -0.8f, 0.6f, 0.f, 0.f,
                             0.f, 0.f, 1.f, 0.f,
                             5.f, -7.f, 9.f, 1.f };

    // The stack ops apply the new transform ahead of what the stack held,
    // which on the scalar matrix is the product written the other way
    // round.
    template<> template<>
    void llrendermatrix_object::test<1>()
    {
        gGL.loadMatrix(M_ROWS);
        gGL.translatef(1.f, 2.f, 3.f);
        gGL.scalef(2.f, 3.f, 4.f);
        gGL.rotatef(30.f, 0.f, 0.f, 1.f);
        gGL.multMatrix(M_ROWS);

        // the scalar matrix: mult first, then the rotation, the scale, the
        // translation, then the matrix loaded first
        LLMatrix4 t;
        t.setTranslation(1.f, 2.f, 3.f);
        LLMatrix4 s;
        s.mMatrix[0][0] = 2.f;
        s.mMatrix[1][1] = 3.f;
        s.mMatrix[2][2] = 4.f;
        LLMatrix4 r;
        r.initRotation(LLQuaternion(30.f * DEG_TO_RAD, LLVector3(0.f, 0.f, 1.f)));
        LLMatrix4 expected(M_ROWS);
        expected *= r;
        expected *= s;
        expected *= t;
        expected *= LLMatrix4(M_ROWS);
        ensure_matrix_close("the stack after translate, scale, rotate, mult", gGL.getModelviewMatrix(), LLMatrix4a(expected), 1e-5f);

        // push copies, pop restores
        gGL.pushMatrix();
        gGL.loadIdentity();
        ensure("loadIdentity", gGL.getModelviewMatrix() == LLMatrix4a::identity());
        gGL.popMatrix();
        ensure_matrix_close("popMatrix restores", gGL.getModelviewMatrix(), LLMatrix4a(expected), 1e-5f);

        // the modes are separate stacks
        gGL.matrixMode(LLRender::MM_PROJECTION);
        ensure("the projection stack is untouched", gGL.getProjectionMatrix() == LLMatrix4a::identity());
    }

    // ortho on the stack and the projection helpers, in both depth conventions.
    template<> template<>
    void llrendermatrix_object::test<2>()
    {
        for (bool reverse : { false, true })
        {
            LLRender::sReverseZ = reverse;
            const std::string when = reverse ? " under reverse-Z" : " forward";

            const LLMatrix4a ortho = LLMatrix4a::ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f);
            const LLMatrix4a persp = LLMatrix4a::perspective(1.1f, 1.777f, 0.25f, 1024.f);
            const LLMatrix4a expected_ortho = reverse ? reversed_z(ortho) : ortho;
            const LLMatrix4a expected_persp = reverse ? reversed_z(persp) : persp;

            ensure("al_ortho" + when, same_bytes(al_ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f), expected_ortho));
            ensure("al_perspective" + when, same_bytes(al_perspective(1.1f, 1.777f, 0.25f, 1024.f), expected_persp));
            ensure("al_reverse_z_transform" + when, same_bytes(al_reverse_z_transform(persp), reversed_z(persp)));

            // a reversed projection puts the near plane at window 1 and the
            // far at 0
            LLVector4a near_clip, far_clip;
            expected_persp.transform4(LLVector4a(0.f, 0.f, -0.25f, 1.f), near_clip);
            expected_persp.transform4(LLVector4a(0.f, 0.f, -1024.f, 1.f), far_clip);
            ensure_approximately_equals_range(("near plane depth" + when).c_str(), near_clip[2] / near_clip[3], reverse ? 1.f : -1.f, 1e-5f);
            ensure_approximately_equals_range(("far plane depth" + when).c_str(), far_clip[2] / far_clip[3], reverse ? 0.f : 1.f, 1e-5f);

            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(M_ROWS);
            gGL.ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f);
            LLMatrix4a expected_stack;
            expected_stack.setMul(expected_ortho, LLMatrix4a(M_ROWS));
            ensure_matrix_close("gGL.ortho applies ahead of the stack" + when, gGL.getProjectionMatrix(), expected_stack, 1e-5f);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);

            // project and unproject choose the form by the convention
            const S32 viewport[4] = { 10, 20, 1280, 720 };
            const LLMatrix4a mv = LLMatrix4a::translation(3.f, -2.f, -20.f);
            const LLVector4a obj(1.f, 2.f, 3.f, 1.f);

            const LLVector4a win = al_project(obj, mv, expected_persp, viewport);
            const LLVector4a expected_win = reverse ? alprojection::project_zo(obj, mv, expected_persp, viewport) : alprojection::project(obj, mv, expected_persp, viewport);
            ensure_vector_close("al_project" + when, win, expected_win, 1e-6f);
            ensure("window depth is in [0, 1]" + when, win[2] >= 0.f && win[2] <= 1.f);

            const LLVector4a back = al_unproject(win, mv, expected_persp, viewport);
            ensure_vector_close("al_unproject of al_project" + when, back, obj, 5e-4f);

            LLMatrix4a inverse;
            inverse.setMul(mv, expected_persp);
            inverse.invert();
            ensure_vector_close("al_unproject through the inverse" + when, al_unproject(win, inverse, viewport), back, 1e-6f);
        }
    }

    // The far-clip squash replaces the depth column with the w column
    // scaled, then restores the projection.
    template<> template<>
    void llrendermatrix_object::test<3>()
    {
        const LLMatrix4a persp = LLMatrix4a::perspective(1.1f, 1.777f, 0.25f, 1024.f);
        for (bool reverse : { false, true })
        {
            LLRender::sReverseZ = reverse;
            const std::string when = reverse ? " under reverse-Z" : " forward";
            for (U32 layer : { 0u, 1u, 3u })
            {
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.loadMatrix(persp);
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                {
                    LLGLSquashToFarClip squash(persp, layer);
                    const F32 depth = reverse ? (0.000005f + 0.00005f * layer) : (0.99999f - 0.0001f * layer);
                    LLMatrix4a expected = persp;
                    for (S32 row = 0; row < 4; ++row)
                    {
                        expected.mMatrix[row].getF32ptr()[2] = persp.mMatrix[row][3] * depth;
                    }
                    ensure("squashed projection" + when, same_bytes(gGL.getProjectionMatrix(), expected));
                    ensure("the mode is left where it was", gGL.getMatrixMode() == LLRender::MM_MODELVIEW);

                    // every point lands at that depth
                    LLVector4a clip;
                    gGL.getProjectionMatrix().transform4(LLVector4a(0.3f, -0.2f, -50.f, 1.f), clip);
                    ensure_approximately_equals_range(("squashed depth" + when).c_str(), clip[2] / clip[3], depth, 1e-6f);
                }
                ensure("projection restored" + when, same_bytes(gGL.getProjectionMatrix(), persp));
            }
        }
    }

    // The oblique clip plane: the plane through the inverse transpose of
    // the modelview then projection, normalized on depth, applied after
    // the projection as its depth.
    template<> template<>
    void llrendermatrix_object::test<4>()
    {
        const LLMatrix4a persp = LLMatrix4a::perspective(1.1f, 1.777f, 0.25f, 1024.f);
        LLMatrix4a mv;
        mv.setMul(LLMatrix4a::rotation(0.4f, LLVector4a(0.f, 1.f, 0.f)), LLMatrix4a::translation(3.f, -2.f, -20.f));
        const LLPlane plane(LLVector3(0.f, 0.f, 1.f), -12.5f);

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.loadMatrix(persp);
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        {
            LLGLUserClipPlane clip(plane, mv, persp);

            LLMatrix4a invtrans;
            invtrans.setMul(mv, persp);
            invtrans.invert();
            invtrans.transpose();
            LLVector4a cplane;
            invtrans.transform4(LLVector4a(-plane[0], -plane[1], -plane[2], -plane[3]), cplane);
            cplane.mul(1.f / fabsf(cplane[2]));
            cplane.getF32ptr()[3] -= 1.f;
            if (cplane[2] < 0.f)
            {
                cplane.negate();
            }
            // the projection, then the plane written over its depth
            LLMatrix4a suffix;
            suffix.setIdentity();
            suffix.setColumn<2>(cplane);
            LLMatrix4a expected;
            expected.setMul(persp, suffix);
            ensure_matrix_close("oblique projection", gGL.getProjectionMatrix(), expected, 1e-5f);

            // only the depth column moved
            for (S32 column : { 0, 1, 3 })
            {
                for (S32 row = 0; row < 4; ++row)
                {
                    ensure_equals("the other columns are the projection's", gGL.getProjectionMatrix().mMatrix[row][column], persp.mMatrix[row][column]);
                }
            }
        }
        ensure("projection restored", same_bytes(gGL.getProjectionMatrix(), persp));
    }
}

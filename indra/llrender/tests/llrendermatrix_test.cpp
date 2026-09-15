/**
 * @file llrendermatrix_test.cpp
 * @brief The matrix stack, the projection helpers and the clip-plane
 *        classes on the native matrix, against what glm computed for the
 *        same calls.
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
#include "llplane.h"

#include "../test/lltut.h"

#include "glm/mat4x4.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_projection.hpp"

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
    void ensure_matrix_close(const std::string& what, const LLMatrix4a& actual, const glm::mat4& expected, F32 tolerance)
    {
        const F32* a = actual.getF32ptr();
        const F32* e = glm::value_ptr(expected);
        for (S32 i = 0; i < 16; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(a[i]), fabsf(e[i]));
            std::ostringstream msg;
            msg << what << " at element " << i << ": " << a[i] << " vs " << e[i];
            ensure_approximately_equals_range(msg.str().c_str(), a[i], e[i], tolerance * scale);
        }
    }

    void ensure_vector_close(const std::string& what, const LLVector4a& actual, const glm::vec3& expected, F32 tolerance)
    {
        for (S32 i = 0; i < 3; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(actual[i]), fabsf(expected[i]));
            std::ostringstream msg;
            msg << what << " at lane " << i << ": " << actual[i] << " vs " << expected[i];
            ensure_approximately_equals_range(msg.str().c_str(), actual[i], expected[i], tolerance * scale);
        }
    }

    // What the reverse-Z rewrite did on glm: row 2 becomes half of row 3 less row 2.
    glm::mat4 glm_reverse_z(const glm::mat4& p)
    {
        glm::mat4 r = p;
        for (int c = 0; c < 4; ++c)
        {
            r[c][2] = 0.5f * (p[c][3] - p[c][2]);
        }
        return r;
    }

    const F32 M_ROWS[16] = { 0.6f, 0.8f, 0.f, 0.f,
                             -0.8f, 0.6f, 0.f, 0.f,
                             0.f, 0.f, 1.f, 0.f,
                             5.f, -7.f, 9.f, 1.f };

    // The stack ops post-multiply as glm's translate, scale, rotate and
    // operator*= did.
    template<> template<>
    void llrendermatrix_object::test<1>()
    {
        gGL.loadMatrix(M_ROWS);
        gGL.translatef(1.f, 2.f, 3.f);
        gGL.scalef(2.f, 3.f, 4.f);
        gGL.rotatef(30.f, 0.f, 0.f, 1.f);
        gGL.multMatrix(M_ROWS);

        glm::mat4 expected = glm::make_mat4(M_ROWS);
        expected = glm::translate(expected, glm::vec3(1.f, 2.f, 3.f));
        expected = glm::scale(expected, glm::vec3(2.f, 3.f, 4.f));
        expected = glm::rotate(expected, glm::radians(30.f), glm::vec3(0.f, 0.f, 1.f));
        expected *= glm::make_mat4(M_ROWS);
        ensure_matrix_close("the stack after translate, scale, rotate, mult", gGL.getModelviewMatrix(), expected, 1e-5f);

        // push copies, pop restores
        gGL.pushMatrix();
        gGL.loadIdentity();
        ensure("loadIdentity", gGL.getModelviewMatrix() == LLMatrix4a::identity());
        gGL.popMatrix();
        ensure_matrix_close("popMatrix restores", gGL.getModelviewMatrix(), expected, 1e-5f);

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

            const glm::mat4 gortho = glm::ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f);
            const glm::mat4 gpersp = glm::perspective(1.1f, 1.777f, 0.25f, 1024.f);
            const glm::mat4 expected_ortho = reverse ? glm_reverse_z(gortho) : gortho;
            const glm::mat4 expected_persp = reverse ? glm_reverse_z(gpersp) : gpersp;

            ensure_matrix_close("al_ortho" + when, al_ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f), expected_ortho, 2e-6f);
            ensure_matrix_close("al_perspective" + when, al_perspective(1.1f, 1.777f, 0.25f, 1024.f), expected_persp, 2e-6f);
            ensure_matrix_close("al_reverse_z_transform" + when, al_reverse_z_transform(LLMatrix4a(gpersp)), glm_reverse_z(gpersp), 0.f);

            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(M_ROWS);
            gGL.ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f);
            ensure_matrix_close("gGL.ortho post-multiplies" + when, gGL.getProjectionMatrix(), glm::make_mat4(M_ROWS) * expected_ortho, 1e-5f);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);

            // project and unproject choose the form by the convention
            const S32 viewport[4] = { 10, 20, 1280, 720 };
            const glm::ivec4 gviewport(10, 20, 1280, 720);
            const glm::mat4 gmv = glm::translate(glm::identity<glm::mat4>(), glm::vec3(3.f, -2.f, -20.f));
            const LLMatrix4a mv(gmv);
            const LLMatrix4a persp(expected_persp);
            const LLVector4a obj(1.f, 2.f, 3.f, 1.f);
            const glm::vec3 gobj(1.f, 2.f, 3.f);

            const LLVector4a win = al_project(obj, mv, persp, viewport);
            const glm::vec3 gwin = reverse ? glm::projectZO(gobj, gmv, expected_persp, gviewport) : glm::project(gobj, gmv, expected_persp, gviewport);
            ensure_vector_close("al_project" + when, win, gwin, 1e-4f);

            const LLVector4a back = al_unproject(win, mv, persp, viewport);
            const glm::vec3 gback = reverse ? glm::unProjectZO(gwin, gmv, expected_persp, gviewport) : glm::unProject(gwin, gmv, expected_persp, gviewport);
            ensure_vector_close("al_unproject" + when, back, gback, 5e-4f);
            ensure_vector_close("al_unproject of al_project" + when, back, gobj, 5e-4f);

            // and the glm forms give the same numbers
            const glm::vec3 bwin = al_project(gobj, gmv, expected_persp, gviewport);
            ensure_vector_close("al_project on glm" + when, win, bwin, 1e-6f);
            const glm::vec3 bback = al_unproject(bwin, gmv, expected_persp, gviewport);
            ensure_vector_close("al_unproject on glm" + when, back, bback, 1e-6f);
        }
    }

    // The far-clip squash replaces the depth column with the w column
    // scaled, then restores the projection.
    template<> template<>
    void llrendermatrix_object::test<3>()
    {
        const glm::mat4 gpersp = glm::perspective(1.1f, 1.777f, 0.25f, 1024.f);
        for (bool reverse : { false, true })
        {
            LLRender::sReverseZ = reverse;
            const std::string when = reverse ? " under reverse-Z" : " forward";
            for (U32 layer : { 0u, 1u, 3u })
            {
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.loadMatrix(LLMatrix4a(gpersp));
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                {
                    LLGLSquashToFarClip squash(LLMatrix4a(gpersp), layer);
                    const F32 depth = reverse ? (0.000005f + 0.00005f * layer) : (0.99999f - 0.0001f * layer);
                    glm::mat4 expected = gpersp;
                    expected = glm::row(expected, 2, glm::row(gpersp, 3) * depth);
                    ensure_matrix_close("squashed projection" + when, gGL.getProjectionMatrix(), expected, 0.f);
                    ensure("the mode is left where it was", gGL.getMatrixMode() == LLRender::MM_MODELVIEW);
                }
                ensure_matrix_close("projection restored" + when, gGL.getProjectionMatrix(), gpersp, 0.f);
            }
        }
    }

    // The oblique clip plane: the plane through the inverse transpose of
    // the modelview then projection, normalized on depth, written into the
    // projection's depth column.
    template<> template<>
    void llrendermatrix_object::test<4>()
    {
        const glm::mat4 gpersp = glm::perspective(1.1f, 1.777f, 0.25f, 1024.f);
        const glm::mat4 gmv = glm::rotate(glm::translate(glm::identity<glm::mat4>(), glm::vec3(3.f, -2.f, -20.f)), 0.4f, glm::vec3(0.f, 1.f, 0.f));
        const LLPlane plane(LLVector3(0.f, 0.f, 1.f), -12.5f);

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.loadMatrix(LLMatrix4a(gpersp));
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        {
            LLGLUserClipPlane clip(plane, LLMatrix4a(gmv), LLMatrix4a(gpersp));

            const glm::mat4 invtrans = glm::transpose(glm::inverse(gpersp * gmv));
            glm::vec4 cplane = invtrans * glm::vec4(-plane[0], -plane[1], -plane[2], -plane[3]);
            cplane /= fabsf(cplane[2]);
            cplane[3] -= 1.f;
            if (cplane[2] < 0.f)
            {
                cplane *= -1.f;
            }
            glm::mat4 suffix = glm::identity<glm::mat4>();
            suffix = glm::row(suffix, 2, cplane);
            const glm::mat4 expected = suffix * gpersp;
            ensure_matrix_close("oblique projection", gGL.getProjectionMatrix(), expected, 1e-4f);
        }
        ensure_matrix_close("projection restored", gGL.getProjectionMatrix(), gpersp, 0.f);
    }
}

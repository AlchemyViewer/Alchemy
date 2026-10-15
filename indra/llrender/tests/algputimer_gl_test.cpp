/**
 * @file algputimer_gl_test.cpp
 * @brief ALGPUTimer on the hidden window: a span's time read back, one span at a time, and the query deleted with the
 *        timer.
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

#include "../algputimer.h"
#include "../llgl.h"
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // The GL_TIME_ELAPSED query open on the context, 0 for none.
    GLint openTimer()
    {
        GLint name = -1;
        glGetQueryiv(GL_TIME_ELAPSED, GL_CURRENT_QUERY, &name);
        return name;
    }

    // A timer query left open goes with the case, so that the next case can open one.
    struct algputimer_data
    {
        algputimer_data()
        {
            // An error the context held before the case is not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        ~algputimer_data()
        {
            if (openTimer() != 0)
            {
                glEndQuery(GL_TIME_ELAPSED);
            }
        }

        ll_test::SharedGLScope mGL;
    };


    // A span's time is read once, and the timer then takes another span.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: a span's time is read once, and the next span begins after it", "[algputimer_gl]")
    {
        ALGPUTimer timer;
        {
            INFO("nothing is pending before the first span");
            CHECK_FALSE(timer.isPending());
            U64 ns = 0;
            CHECK_FALSE(timer.poll(ns, true));
        }

        REQUIRE(timer.begin());
        {
            INFO("the span holds the context's timer query");
            CHECK(timer.getQuery() != 0);
            CHECK(openTimer() == (GLint)timer.getQuery());
        }
        timer.end();
        {
            INFO("the ended span's time is pending, and its query closed");
            CHECK(timer.isPending());
            CHECK(openTimer() == 0);
        }

        U64 ns = 0;
        {
            INFO("the time is read");
            CHECK(timer.poll(ns, true));
            CHECK_FALSE(timer.isPending());
        }
        {
            INFO("and only once");
            CHECK_FALSE(timer.poll(ns, true));
        }

        {
            INFO("the next span begins and is read in its turn");
            REQUIRE(timer.begin());
            timer.end();
            CHECK(timer.poll(ns, true));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // The autotune profiles an avatar again before the last profile's time is in. The span that would begin then is
    // refused, its end ends nothing, and another timer query can still begin.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: a span cannot begin while the last one's time is pending", "[algputimer_gl]")
    {
        ALGPUTimer timer;
        REQUIRE(timer.begin());
        timer.end();
        REQUIRE(timer.isPending());

        {
            INFO("the second span is refused");
            CHECK_FALSE(timer.begin());
        }
        timer.end();
        {
            INFO("no timer query is left open");
            CHECK(openTimer() == 0);
        }

        // Another avatar's, or the shader profiler's.
        GLuint other = 0;
        glGenQueries(1, &other);
        glBeginQuery(GL_TIME_ELAPSED, other);
        {
            INFO("another timer query begins");
            CHECK(glGetError() == GL_NO_ERROR);
        }
        glEndQuery(GL_TIME_ELAPSED);
        glDeleteQueries(1, &other);

        {
            INFO("the first span's time is still there to read");
            U64 ns = 0;
            CHECK(timer.poll(ns, true));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // release() deletes the query, ending a span left open; the timer's destruction does the same.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: the query goes with release and with the timer", "[algputimer_gl]")
    {
        GLuint name = 0;
        {
            ALGPUTimer timer;
            REQUIRE(timer.begin());
            timer.end();
            name = timer.getQuery();
            REQUIRE(glIsQuery(name) == GL_TRUE);

            timer.release();
            {
                INFO("release deletes the query and drops the pending time");
                CHECK(glIsQuery(name) == GL_FALSE);
                CHECK(timer.getQuery() == 0);
                CHECK_FALSE(timer.isPending());
            }

            {
                INFO("a released timer times again");
                REQUIRE(timer.begin());
            }
            name = timer.getQuery();
        }
        {
            INFO("the timer's destruction ends its open span and deletes its query");
            CHECK(openTimer() == 0);
            CHECK(glIsQuery(name) == GL_FALSE);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
}

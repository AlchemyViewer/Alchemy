/**
 * @file altestfailure.h
 * @brief What a failed assertion throws, for a test that catches it to add to
 *        the report and rethrow, as tests caught tut::failure.
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

#ifndef AL_ALTESTFAILURE_H
#define AL_ALTESTFAILURE_H

#include <catch2/internal/catch_test_failure_exception.hpp>

namespace ll_test
{
    // Thrown by a failed REQUIRE or a FAIL, after Catch2 has recorded the
    // failure: catching it can add to what the case reports, or tidy up,
    // and must rethrow, since the failure stands either way. It carries no
    // message; Catch2 has reported that already. One alias, so the tests
    // that catch it do not each name Catch2's internal header.
    using Failure = Catch::TestFailureException;
}

#endif // AL_ALTESTFAILURE_H

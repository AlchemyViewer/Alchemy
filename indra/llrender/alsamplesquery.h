/**
 * @file alsamplesquery.h
 * @brief Whether any sample of a set of draws passed, and draws that run only if one did.
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

#pragma once

#include "stdtypes.h"

/// A GL_ANY_SAMPLES_PASSED query over the draws between begin() and end(), and the draws a Conditional scope then
/// holds, which the GPU runs only if any of those samples passed. The CPU reads nothing back and waits for nothing: the
/// GPU waits for the query where the scope begins. Clears are draws for this purpose; binding targets and setting state
/// are not. From GL 4.3 the query is conservative: the GPU may answer from its coarse depth, erring only toward a
/// sample passed, which runs the scope's draws for nothing and never skips one that was wanted.
class ALSamplesQuery
{
public:
    ALSamplesQuery() = default;
    ALSamplesQuery(const ALSamplesQuery&) = delete;
    ALSamplesQuery& operator=(const ALSamplesQuery&) = delete;

    /// Counts the draws that follow, until end().
    void begin();
    void end();

    /// Deletes the query object, while the context it was made in is current.
    void release();

    /// Draws issued in the scope run only if the query's last begin() and end() let a sample through; with no query
    /// made yet, they run.
    class Conditional
    {
    public:
        explicit Conditional(const ALSamplesQuery& query);
        ~Conditional();

        Conditional(const Conditional&) = delete;
        Conditional& operator=(const Conditional&) = delete;

    private:
        bool mConditional = false;
    };

private:
    U32  mQuery = 0;
    U32  mTarget = 0;
    bool mActive = false;
};

/**
 * @file alscriptstudio_stubs.h
 * @brief What a test chooses of the stubs Script Studio's units are tested over.
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

#ifndef AL_ALSCRIPTSTUDIO_STUBS_H
#define AL_ALSCRIPTSTUDIO_STUBS_H

#include "alscriptproblem.h"

#include <functional>
#include <utility>

namespace al_studio_test
{
    // What the stub of ALScriptLints::apply does with a check's problems:
    // the lints as a scripter chose them, which are the viewer's settings.
    // Nothing, until a test chooses.
    std::function<void(ALScriptProblems&)>& chosenLints();

    // A test's choice of the lints while this lives, and the choice before
    // it put back after, however the test ends.
    class ChosenLints
    {
    public:
        explicit ChosenLints(std::function<void(ALScriptProblems&)> apply) : mWas(std::exchange(chosenLints(), std::move(apply))) {}
        ~ChosenLints() { chosenLints() = std::move(mWas); }
        ChosenLints(const ChosenLints&)            = delete;
        ChosenLints& operator=(const ChosenLints&) = delete;

    private:
        std::function<void(ALScriptProblems&)> mWas;
    };
}

#endif // AL_ALSCRIPTSTUDIO_STUBS_H

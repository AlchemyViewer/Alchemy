/**
 * @file alscriptstudioanalysis.h
 * @brief What the units split out of the Script Studio's window ask of its analysis.
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

#include "alscriptanalysis.h"
#include "alscriptstudioplaces.h"

#include <functional>
#include <string>

struct ALScriptStudioDoc;
struct ALTextPos;

// What the units split out of the Script Studio's window ask of its
// analysis, whichever unit asks: the analyzers asked, a tab checked again,
// what the analyzers read of a tab, and the Problems told. The window
// implements it, over its checking (ALScriptStudioChecking) and its
// Problems tab; a unit's test fakes it.
class ALScriptStudioAnalysis
{
public:
    typedef ALScriptStudioDoc Doc;

    // --- asking --------------------------------------------------------------------

    // A question put to the analyzers, answered later.
    virtual void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) = 0;
    // A place in a tab's source asked about, as its check asks: through
    // the expansion where the preprocessor makes one, once it is made.
    virtual void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at) = 0;
    // A tab checked again, now or after a pause.
    virtual void scheduleAnalysis(Doc& doc, bool now) = 0;

    // --- what the analyzers read ---------------------------------------------------

    // Whether the preprocessor runs over a tab, so that the analyzers read
    // its expansion; whether a tab is a fragment of LSL -- an include's
    // functions -- with no default state; the name an include is shown
    // by; and an include's lines as it reads, none where it is not had.
    virtual bool                  preprocessed(const Doc& doc) const                         = 0;
    virtual bool                  lslFragment(const Doc& doc) const                          = 0;
    virtual std::string           includeName(const Doc& doc, const std::string& path) const = 0;
    virtual ALScriptPlaces::Lines sourceLines(const std::string& path) const                 = 0;

    // --- what they said ------------------------------------------------------------

    // The Problems tab said again for a tab, with the next frame.
    virtual void refreshProblems(Doc& doc) = 0;

protected:
    ~ALScriptStudioAnalysis() = default;
};

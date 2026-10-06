/**
 * @file alscriptstudiocomparepairs.h
 * @brief A comparison's texts lined up by their functions, paired by what each is.
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
#include "alscriptstudiodoc.h"
#include "alscriptsymbol.h"

#include <memory>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A tab's comparison lined up by its texts' functions (ALDiffView::setPairs):
// each text's outline worked out from a parse of it, as it is set beside
// the other -- the two set, the tab typed in, a version stepped to -- and
// the two paired by what each function, event and state is
// (ALScriptOutlinePairs) once both are known for the texts the comparison
// shows. Until then the pairs it has are carried with their lines. Not
// where the comparison has ranges, which line a conversion or an
// expansion up more finely, nor for a notecard.
class ALScriptStudioComparePairs
{
public:
    typedef ALScriptStudioDoc Doc;

    // What it asks of the window: a text's outline worked out, answered on
    // the main thread -- by the analysis (ALScriptAnalysis, Shape), or a
    // test's.
    class Window
    {
    public:
        virtual void askShape(ALScriptAnalysis::Request request, ALScriptAnalysis::callback_t answered) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioComparePairs(ALScriptStudioServices& services, Window& window);

    // A tab's comparison's texts as they now are (ALDiffView::setOnTexts):
    // the outline of each that is another text than the last asked of
    // asked for, and the two paired where both are known.
    void follow(Doc& doc);

private:
    void answered(const std::string& id, size_t side, const std::shared_ptr<const std::string>& text, std::vector<ALScriptOutlineEntry> outline);
    void pairUp(Doc& doc);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Whether this is still here, for what the analysis calls back.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};

// What a tab keeps of its comparison's texts' outlines: of each side, the
// text last asked of, the text answered for and what it declares, and how
// many times it was asked, which numbers the questions.
struct ALScriptStudioDoc::ComparePairs
{
    struct Side
    {
        std::shared_ptr<const std::string> asked;
        std::shared_ptr<const std::string> answered;
        std::vector<ALScriptOutlineEntry>  outline;
        U32                                version = 0;
    };
    Side sides[2];
};

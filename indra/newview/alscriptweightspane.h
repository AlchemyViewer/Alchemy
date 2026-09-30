/**
 * @file alscriptweightspane.h
 * @brief The Script Studio's Weights tab: what a script's code weighs, for each target and by part.
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

#include "alscriptweight.h"
#include "llpanel.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>
#include <string>
#include <vector>

class ALPaneList;
class LLScrollListItem;
class LLTextBox;

// The Script Studio's Weights tab: what a script's code weighs for each
// target it could be compiled for, side by side -- which is how to choose
// one -- and what each part of it weighs for the target chosen there: its
// bytes, its share of what the target runs a script in, and how that moved
// since the text was last saved, sorted by whichever of them is asked. It
// fills its lists from what it is given and says what is chosen; going
// there is the window's. What a script allocates as it runs is in none of
// it.
class ALScriptWeightsPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptWeightsPane, LLPanel);
    // What the pane shows of one script.
    struct Shown
    {
        std::string                 id;
        std::string                 name;
        // Each target it was weighed for, in the source's places, its own
        // first.
        std::vector<ALScriptWeight> weights;
        // What each weighed as the text was last saved, where it was
        // weighed then.
        std::vector<ALScriptWeight> saved;
        // Whether they were weighed before the preprocessor's optimizer,
        // which a save runs; and what a save sends weighs for its own
        // target, where that has been weighed of the text as it stands.
        bool                        beforeOptimizer = false;
        std::optional<size_t>       sent;
        // An include's name, by the identity its parts carry.
        boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> fileNames;
        // What a function with no name of its own is called by where it
        // stands: the event a handler is put on, as the outline lists it,
        // by the line the function starts on.
        boost::unordered_flat_map<S32, std::string> handlers;
    };
    // Where a part is: in the script shown where `file` is empty, else in
    // that include.
    struct Place
    {
        std::string file;
        std::string fileName;
        S32         line   = 0;
        S32         column = 0;
    };

    // Built by the skin, as the tab (class="script_studio_weights"),
    // speaking in the words of the window it is a tab of.
    explicit ALScriptWeightsPane(const LLPanel::Params& params = getDefaultParams());
    bool     postBuild() override;

    // A script's weights, the target chosen kept where it is the same
    // script, and the part chosen and the scroll kept through a refill.
    void show(Shown shown);
    // Nothing to show, and why.
    void showNothing(const std::string& why);
    const std::string& shownId() const { return mShown.id; }

    // The part chosen in the list, where it has a place.
    std::optional<Place> chosenPlace() const;
    ALPaneList*          partsList() const { return mParts; }

private:
    // A part as listed: the part, and how its bytes moved since the text
    // was last saved -- nothing where it was not weighed then, and `fresh`
    // where it was and this part was not in it.
    struct Row
    {
        ALScriptWeight::Part part;
        // Which of the parts of its kind, name and file it is, in order:
        // every anonymous function has the same empty name.
        size_t               nth = 0;
        std::optional<S64>   change;
        bool                 fresh = false;
        // Its name as shown, made once for the row and for sorting by it.
        std::string          name;
    };

    const ALScriptWeight* chosen() const;
    const ALScriptWeight* savedFor(ALScriptWeight::Target target) const;
    void                  fillTargets();
    void                  fillParts();
    std::string           partName(const ALScriptWeight::Part& part) const;
    std::string           kindName(ALScriptWeight::Part::Kind kind) const;
    std::string           where(const ALScriptWeight::Part& part) const;
    std::string           changeText(const std::optional<S64>& change, bool fresh) const;
    // A share of what the target runs a script in, as a percentage.
    std::string share(size_t bytes, size_t limit) const;
    std::string           kilobytes(size_t bytes, bool estimate) const;
    S32                   compare(S32 column, const LLScrollListItem* a, const LLScrollListItem* b) const;

    const LLPanel*         mStrings = this;
    LLTextBox*             mHead    = nullptr;
    ALPaneList*            mTargets = nullptr;
    ALPaneList*            mParts   = nullptr;
    Shown                  mShown;
    ALScriptWeight::Target mChosen = ALScriptWeight::Target::SLua;
    std::vector<Row>       mRows;
};

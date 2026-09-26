/**
 * @file alscriptreferencespane.h
 * @brief Script Studio's References tab: the places a name was found, listed, slid with edits, and chosen.
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

#include "alscriptstudiodoc.h"
#include "llpanel.h"

#include <string>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLTextBox;

// The References tab of a Script Studio window: the places a name was last
// found at -- in the script it was looked up from, its includes, and the
// object's other scripts -- whichever tab is in front, the declaration
// marked; slid with the edits made to what they are in, and gone where the
// edit took the name away; one chosen gone to, which is the window's.
class ALScriptReferencesPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptReferencesPane, LLPanel);
    typedef ALScriptStudioDoc Doc;

    // What was found: the script it was looked up from, by id and name;
    // the name; the places, the script's own with no file; and where the
    // name is declared, where it is known.
    struct Found
    {
        std::string             from;
        std::string             fromName;
        std::string             name;
        std::vector<Doc::Place> places;
        bool                    hasDefinition = false;
        std::string             home;
        ALScriptSpan            definition;
    };

    // What the tab asks of the window beyond its services.
    class Window
    {
    public:
        // A place chosen: gone to, the keyboard left in the list to walk
        // on, or, `to_editor`, taken to the script.
        virtual void referenceChosen(const Found& found, const Doc::Place& place, bool to_editor) = 0;
        // How many there are changed, which the tab's title says.
        virtual void referencesCounted() = 0;

    protected:
        ~Window() = default;
    };

    explicit ALScriptReferencesPane(const LLPanel::Params& params = getDefaultParams());
    bool     postBuild() override;

    // What was found, listed; nothing, with the question it answers said.
    void         show(Found found);
    void         forget() { show(Found()); }
    const Found& found() const { return mFound; }
    ALPaneList*  list() const { return mList; }
    // The places in a tab slid with an edit to its text: the tab's own
    // where it is what the name was looked up from, else by `path`, what the
    // preprocessor's map calls it. Listed again once the frame is drawn.
    void slide(Doc& doc, const std::string& path, const ALTextDocument::Edit& edit);
    void pump();
    // The tab it was looked up from known by another id.
    void rekey(const std::string& was, const std::string& id);
    // The row chosen gone to.
    void choose(bool to_editor);

private:
    void fill();
    // Where in the places the one with this number is; the end where it is
    // gone, or with `or_after` the first after it, else the last.
    size_t placeWith(U32 id, bool or_after) const;

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALPaneList*             mList     = nullptr;
    LLTextBox*              mHead     = nullptr;
    Found                   mFound;
    // Whether an edit has moved the places since the list was filled.
    bool                    mStale = false;
};

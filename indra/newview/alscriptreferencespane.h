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

#include <functional>
#include <set>
#include <string>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLButton;
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

    // A rename previewed over what was found: a box by each place, checked,
    // to leave it out by, and its line as it would read with `new_name`;
    // `said` over them. Rename makes it at the places still checked, by
    // their order in what was found; Cancel lists them as found. Anything
    // shown after lets it go.
    void preview(Found found, const std::string& new_name, const std::string& said, std::function<void(const std::vector<size_t>& kept)> apply);
    bool previewing() const { return static_cast<bool>(mApply); }
    std::vector<size_t> kept() const;
    // A place's box turned, by its order in what was found.
    void setKept(size_t index, bool kept);
    void renamePreviewed();
    void cancelPreview();

private:
    // The places listed: each a row of its own by its number, made again
    // only where it says something else, and no more than PLACES_MOST of
    // them -- a name used everywhere in a big script, or across many --
    // the rest counted in a row of their own.
    static constexpr size_t PLACES_MOST = 1000;
    void                    fill();
    // The places numbered, the preview let go of.
    void take(Found found);
    // The boxes as the rows have them now, after a click.
    void readBoxes();
    void showButtons();
    // Where in the places the one with this number is; the end where it is
    // gone, or with `or_after` the first after it, else the last.
    size_t placeWith(U32 id, bool or_after) const;

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALPaneList*             mList     = nullptr;
    LLTextBox*              mHead     = nullptr;
    LLButton*               mRename   = nullptr;
    LLButton*               mCancel   = nullptr;
    Found                   mFound;
    // The rename previewed: the name, what is said of it, the places left
    // out by their numbers, and what makes it.
    std::string             mNewName;
    std::string             mSaid;
    std::set<U32>           mLeftOut;
    std::function<void(const std::vector<size_t>& kept)> mApply;
    // The head's width with no buttons beside it.
    S32                     mHeadRight = 0;
    // Whether an edit has moved the places since the list was filled.
    bool                    mStale = false;
};

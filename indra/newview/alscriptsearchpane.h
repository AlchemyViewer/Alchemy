/**
 * @file alscriptsearchpane.h
 * @brief Script Studio's Search tab: the scripts open, or an object's, searched and replaced across.
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

#include "alscriptsearch.h"
#include "llpanel.h"
#include "llsd.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

class ALPaneList;
class ALScopeBar;
class ALScriptStudioServices;
class LLButton;
class LLLineEditor;
class LLTextBox;

// The Script Studio's Search tab: words looked for across the scripts
// open, one object's contents or every object the explorer lists, and
// where asked what each includes, said as a sentence -- find `timer` as
// [text] [ignoring case] in [open scripts] [and what they include] --
// each place found a row, with the count of them; and Replace All over
// them, asked about first. A script open is searched as it stands in its
// tab, and again a moment after it is typed in; any other as the region
// has it, fetched, and what an earlier search is answered is dropped.
// Going to a place, and fetching, are the window's.
class ALScriptSearchPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptSearchPane, LLPanel);
    typedef ALScriptStudioDoc Doc;

    // What the pane asks of the window beyond its services.
    class Window
    {
    public:
        // An object the explorer lists and is in sight: its root, its name,
        // and what its prims hold.
        struct Object
        {
            LLUUID                   root;
            std::string              name;
            std::vector<ALScriptRef> items;
        };
        virtual std::vector<Object> objectsListed() const               = 0;
        virtual std::string         objectName(const LLUUID& root) const = 0;
        // What a row says an open script is in: its object, by the name it
        // has now; nothing for one in the inventory or on disk.
        virtual std::string whereIs(const Doc& doc) const = 0;
        // The object "this object" is: the script in front's, else the one
        // chosen in the explorer; null for neither.
        virtual LLUUID objectInHand() const = 0;
        // The root of the object a script is in: null for one in the
        // inventory, or out of sight.
        virtual LLUUID rootOf(const ALScriptRef& ref) const = 0;
        // A script open in another of the studio's windows: its tab there.
        virtual Doc* openElsewhere(const ALScriptRef& ref) = 0;
        // A script's text fetched for the search at `generation`, which
        // answers through fetched().
        virtual void fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where) = 0;
        // The edits a tab was opened with (Doc::pendingEdits), made once
        // its text is in, or now.
        virtual void applyPendingEdits(Doc& doc) = 0;
        // Replace All asked about, in the words `args` fill in; `yes`
        // where the answer is yes.
        virtual void confirmReplaceAll(const LLSD& args, std::function<void()> yes) = 0;
        // A place found gone to: shown in its script, the keyboard left in
        // the list to walk on, or taken to the script (`to_editor`).
        virtual void searchResultChosen(const ALScriptSearch::Found& one, const ALTextRange& place, bool to_editor) = 0;
        // What a script includes and requires, and those in turn, as a
        // run would find them now -- nothing fetched: each file's
        // identity, name and text. `file` is a script on disk's path.
        struct Included
        {
            std::string path;
            std::string name;
            std::string text;
        };
        virtual std::vector<Included> includesOf(const ALScriptRef& ref, const std::string& file, const std::string& name, const std::string& text,
                                                 bool lua) = 0;

    protected:
        ~Window() = default;
    };

    // Built by the skin, as the tab (class="script_studio_search"), in a
    // Script Studio window it finds through the view tree, whose services
    // it uses and which it asks what it does not do itself.
    explicit ALScriptSearchPane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // What the sentence says, searched for; and the words to look for
    // given the keyboard, what is chosen on a line of a tab in front put
    // there first.
    void run();
    void focusQuery(const Doc* front);
    // A script's text fetched for a search: searched where it is this
    // search's; `text` none where it could not be had.
    void fetched(U32 generation, const std::string& where, const ALScriptRef& ref, const std::string& name, const std::optional<std::string>& text,
                 bool notecard);
    // A tab typed in: searched again a moment later, where the search is
    // over it. Each frame, the moment come.
    void typedIn(const Doc& doc);
    void pump();
    // A tab called something else from here on.
    void rekey(const std::string& from, const std::string& to);

    // The row chosen, gone to.
    void choose(bool to_editor);
    // Replace All: asked about, then every place replaced -- script by
    // script, each one step to undo, a script not open opened with the
    // change unsaved -- and a script typed in since the search left alone,
    // said, and searched again.
    void askReplaceAll();
    void replaceAll();

    const ALScriptSearch& search() const { return mSearch; }
    ALPaneList*           list() const { return mResults; }
    ALScopeBar*           bar() const { return mBar; }
    LLTextBox*            count() const { return mCount; }

    // How it matches, and where, kept between sessions.
    void saveState(LLSD& state) const;
    void readState(const LLSD& state);

private:
    void buildSentence();
    void onChanged();
    void searchOpen(const Doc& doc);
    void searched(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text, U32 version,
                  const std::string& doc_id, bool keep_text = false, bool notecard = false);
    // What a script searched includes, each file once a search, where the
    // sentence asks for it.
    void searchIncludes(const ALScriptRef& ref, const std::string& file, const std::string& name, const std::string& text, bool lua);
    // A script's places as rows, after the rest, up to what a list holds;
    // and its rows told what it holds now, in place, where it holds as
    // many places as its rows are -- false where it does not.
    void addRows(size_t index);
    bool refreshRows(size_t index);
    // The rows from what was found, the row chosen and the scroll kept.
    void refill();
    void settled();
    // The tab a script found is open in here, or none; and whether
    // Replace All would change it, which its question counts by.
    Doc* tabOf(const ALScriptSearch::Found& one);
    bool replaceable(const ALScriptSearch::Found& one);

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALScopeBar*             mBar         = nullptr;
    ALPaneList*             mResults     = nullptr;
    LLTextBox*              mCount       = nullptr;
    LLLineEditor*           mReplacement = nullptr;
    LLButton*               mReplace     = nullptr;
    ALScriptSearch          mSearch;
    // The files included that this search has searched.
    std::set<std::string>   mIncludesSearched;
};

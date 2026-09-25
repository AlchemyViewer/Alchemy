/**
 * @file alscriptproblemspane.h
 * @brief Script Studio's Problems tab: a tab's problems gathered, and listed through the pane's filters.
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

#include "alfindings.h"
#include "alscriptanalysis.h"
#include "alscriptstudiodoc.h"
#include "llhandle.h"
#include "llpanel.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLCheckBoxCtrl;
class LLComboBox;
class LLContextMenu;
class LLFilterEditor;

// The Script Studio's Problems tab: what is wrong with a script, as each
// thing that looks at it last said -- the analyzers, the preprocessor and
// its optimizer, the compiler, the script as it ran, the weight, the
// definitions -- listed under whose it is, through filters by level, by
// origin and by words, for the tab in front or every one open; with the
// counts the tab's title says, and a menu of what can be done about one.
// Going to a problem's place, and making a fix, are the window's.
class ALScriptProblemsPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptProblemsPane, LLPanel);
    typedef ALScriptStudioDoc Doc;

    // --- the list of a tab's problems ----------------------------------------------

    // A tab's problems as the list and its editor show them: the rows, in
    // order -- the script's own, then each include's, by place -- the
    // squiggles, the worst mark each line takes, and the lines a fix is
    // offered on, with whether one changes the script rather than only
    // saying the problem is wanted.
    struct Made
    {
        std::vector<Doc::Shown>                         rows;
        std::vector<ALCodeEditor::Decoration>           decorations;
        std::vector<std::pair<S32, ALCodeEditor::Mark>> marks;
        std::vector<std::pair<S32, bool>>               fixable;
    };
    // What they are made with beyond the tab and the window's words: the
    // target the optimizer's savings are weighed for, where the tab is
    // weighed; and an include's name, by what it is.
    struct Making
    {
        std::optional<ALScriptWeight::Target>               target;
        std::function<std::string(const std::string& path)> includeName;
    };
    static Made make(const Doc& doc, const ALScriptStudioServices& services, const Making& making);

    // --- the pane ------------------------------------------------------------------

    // Where a row's problem is: whose, in which file, and its stretch.
    struct Place
    {
        std::string doc;
        std::string file;
        std::string fileName;
        S32         line      = 0;
        S32         column    = 0;
        bool        hasColumn = false;
        S32         endLine   = -1;
        S32         endColumn = -1;
    };

    // What the pane asks of the window beyond its services.
    class Window
    {
    public:
        // How many problems there are to list has changed, which the tab's
        // title says.
        virtual void problemCountsChanged() = 0;
        // A filter changed, which the window keeps between sessions.
        virtual void problemFiltersChanged() = 0;
        // A row's place shown in its script, the keyboard left in the list
        // to walk on, or taken to the script (`to_editor`).
        virtual void problemChosen(const Place& place, bool to_editor) = 0;
        // The mark a script's rows are listed under; one of its includes'.
        virtual std::string problemIcon(const Doc& doc, const std::string& include) const = 0;
        // A fix made over the text at `version`, whose places it is in.
        virtual bool applyFix(Doc& doc, const ALScriptFix& fix, U32 version) = 0;
        // The preferred fix of every problem of a kind made -- of every
        // kind, for none -- once asked.
        virtual void fixAllOfKind(Doc& doc, const std::string& key) = 0;
        // The tab's problems gathered again.
        virtual void refreshProblems(Doc& doc) = 0;
        // A lint, by what names it in a language, where it is one there is
        // a choice about; how it is set, and set; the settings of all.
        virtual bool                 isLint(bool lua, const std::string& id) const                    = 0;
        virtual ALScriptLints::Level lintLevel(bool lua, const std::string& id) const                 = 0;
        virtual void                 setLintLevel(bool lua, const std::string& id, ALScriptLints::Level) = 0;
        virtual void                 showLintSettings()                                              = 0;

    protected:
        ~Window() = default;
    };

    // Built by the skin, as the tab (class="script_studio_problems");
    // given the window's services, and what it asks of the window, once
    // the window is built, before anything is listed.
    explicit ALScriptProblemsPane(const LLPanel::Params& params = getDefaultParams());
    ~ALScriptProblemsPane() override;
    bool postBuild() override;
    void attach(ALScriptStudioServices& services, Window& window);

    // A tab's problems made anew: kept, and listed again where the list
    // holds that tab's, or every one's.
    void changed(const Doc& doc);
    // A tab's problems let go of: it closed, or is another's now. Closed,
    // the list no longer holds it either.
    void forget(const std::string& id);
    void closed(const std::string& id);
    // A tab called something else from here on.
    void rekey(const std::string& from, const std::string& to);

    // The list filled with a tab's problems, and every open script's
    // where the scope says, the row chosen and the scroll kept through a
    // refill of the same; with none, only the counts.
    void               fill(const Doc* doc);
    // Whose problems the list holds; the tab in front where it holds none
    // that is open.
    Doc*               listed();
    const std::string& listedId() const { return mShownFor; }
    // How many problems there are to list, before the filters.
    S32                held() const { return mHeld; }
    ALPaneList*        list() const { return mList; }

    // The row chosen, gone to.
    void choose(bool to_editor);
    // The first error in the list, chosen and gone to; the checkers' own,
    // passing over the compiler's, where asked.
    void selectFirstError(bool checkers_only);

    // A row's menu, over the row at a place of the list, and what it does.
    void showMenu(S32 x, S32 y);
    void act(const std::string& action);
    // Whether an item of it is offered: a copy, the run-time errors let
    // go of, a lint turned off or made an error, the settings; whether the
    // lint is an error; whether a fix of the kind, or Fix All, is shown.
    bool enabled(const std::string& what) const;
    bool lintIsError() const;
    bool fixShown(const std::string& which) const;

    // The filters, kept between sessions: the levels, the scope, the
    // origin.
    void saveState(LLSD& state) const;
    void readState(const LLSD& state);

private:
    typedef ALFindings<Doc::Shown, Doc::ProblemTraits> store_t;

    store_t::Query          query(const Doc& doc) const;
    std::vector<const Doc*> docsFor(const Doc* doc);
    bool                    everyScript() const;
    void                    layoutFilters();
    // The chosen row's script, its problem, and its lint where there is a
    // choice about it.
    Doc*              chosenDoc() const;
    const Doc::Shown* chosenShown() const;
    std::string       chosenLint(bool& lua) const;

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALPaneList*             mList     = nullptr;
    LLCheckBoxCtrl*         mErrors   = nullptr;
    LLCheckBoxCtrl*         mWarnings = nullptr;
    LLCheckBoxCtrl*         mNotes    = nullptr;
    LLCheckBoxCtrl*         mFixable  = nullptr;
    LLComboBox*             mScope    = nullptr;
    LLComboBox*             mOrigin   = nullptr;
    LLFilterEditor*         mFilter   = nullptr;
    store_t                 mStore;
    // Whose problems the list holds, so that a refill of the same
    // script's keeps the row chosen and the scroll.
    std::string             mShownFor;
    S32                     mHeld = 0;
    LLHandle<LLContextMenu> mMenu;
};

/**
 * @file alscriptcrumbsbar.h
 * @brief Script Studio's bar under the editor: the path of symbols the caret is in, and the words past it.
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
#include "alpreprocessor.h"
#include "alscriptweight.h"
#include "aljumpbar.h"
#include "almenuslot.h"
#include "llmenugl.h"
#include "llpanel.h"

#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;

// The bar under a Script Studio window's editor: the path of symbols the
// caret is in -- the script, then each symbol holding it, outermost first,
// each offering the others beside it -- and past it the trailer, what is
// true of the tab in front: that it may only be read, vim's word, where the
// caret is, what is selected, how it is indented, how many problems it
// has, what it weighs or its optimizer made it, what a save would send,
// and which of its views is in front. A step chosen, or a word of the
// trailer pressed, is the window's to do -- but for the indentation,
// whose menu is the bar's: it changes nothing but the tab's editor.
class ALScriptCrumbsBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptCrumbsBar, LLPanel);
    typedef ALScriptStudioDoc      Doc;
    typedef ALJumpBar::TrailerPart Part;

    // What the bar asks of the window beyond its services.
    class Window
    {
    public:
        // A step chosen: the caret put where it says in the tab's script,
        // or only the keyboard given it, where the step names nothing now.
        virtual void crumbChosen(Doc& doc, std::optional<ALTextRange> at) = 0;
        // A word of the trailer pressed: line, problems or expanded.
        virtual void trailerChosen(const std::string& value) = 0;
        // What the trailer says beyond the tab: vim's word, the tab's
        // problems, and the target it is weighed for.
        virtual std::string                           vimBanner() const                                        = 0;
        virtual void                                  problemCounts(const Doc& doc, S32& errors, S32& warnings) const = 0;
        virtual std::optional<ALScriptWeight::Target> weightTarget(const Doc& doc) const                        = 0;
        // Why a save preprocesses the tab, or that it does not.
        virtual ALPreprocessor::Wanted                preprocessedWhy(const Doc& doc) const                     = 0;

    protected:
        ~Window() = default;
    };

    // What the trailer's pressable words say they do, with the keys that
    // do the same: go to a line, the problems, and each view.
    struct Tips
    {
        std::string line;
        std::string problems;
        std::string source;
        std::string expanded;
    };

    explicit ALScriptCrumbsBar(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // The path of the tab in front's caret, as the caret's unit found it
    // (doc.caret->crumbPath), and its trailer: the crumbs made again only
    // where the path, the outline, the tab or its name has changed -- it
    // is asked on every key -- and the trailer every time.
    void showPath(Doc& doc);
    void showTrailer(Doc& doc);
    // A step chosen by what the bar calls it: the script's top, or a
    // symbol by where it was and what it is called.
    void choose(const std::string& value);
    // No tab: nothing on the bar.
    void forget();
    // A choice from the indentation's menu, for the tab in front's script:
    // spaces or tabs, width_N, what the script says read again (or the
    // default), or the whole script converted to spaces or tabs; whether
    // it can be made, and whether it is what the script is indented by.
    void indentAct(const std::string& action);
    bool indentEnabled(const std::string& action) const;
    bool indentChecked(const std::string& action) const;
    void setTips(Tips tips)
    {
        mTips = std::move(tips);
        mSteady.reset();
    }

private:
    // The trailer's parts, each added to `parts` where it has anything to
    // say: the lead -- read only, vim's word; the caret's place; the
    // selection; the indentation; the problems; what it weighs, or what
    // the optimizer made it; what a save would send; and the view.
    void lead(Doc& doc, std::vector<Part>& parts) const;
    void place(Doc& doc, std::vector<Part>& parts) const;
    void selection(Doc& doc, std::vector<Part>& parts) const;
    void indentation(Doc& doc, std::vector<Part>& parts) const;
    void problems(Doc& doc, std::vector<Part>& parts) const;
    void weight(Doc& doc, std::vector<Part>& parts) const;
    void sending(Doc& doc, std::vector<Part>& parts) const;
    void views(Doc& doc, std::vector<Part>& parts) const;

    // The indentation pressed: its menu.
    void showIndentMenu();

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALJumpBar*              mBar      = nullptr;
    ALMenuSlot              mIndentMenu;
    Tips                    mTips;
    // Whose path the bar shows, so that a tab come to the front is shown
    // there whatever its own path was when last shown; and the path, the
    // outline it was read from and the name, as the crumbs were last made.
    std::string             mShownFor;
    std::vector<size_t>     mShownPath;
    U32                     mShownOf = 0;
    std::string             mShownName;

    // What the trailer's words that do not move with the caret are made
    // of -- all but the caret's place and the selection -- and those
    // words as last made: a caret moving says the same of them, and they
    // are made again only where something here has changed. Whatever a
    // part of them reads is here.
    struct Steady
    {
        std::string id;
        bool        readOnly = false;
        std::string banner;
        // The indentation's.
        bool        loaded     = false;
        S32         tabWidth   = 0;
        bool        softTabs   = false;
        U8          indentFrom = 0;
        // The problems'.
        bool        checking = false;
        S32         errors    = 0;
        S32         warnings  = 0;
        S32         migration = 0;
        // The weight's, or the optimizer's.
        std::optional<ALScriptWeight::Target> target;
        bool                                  optimized  = false;
        size_t                                codeBefore = 0;
        size_t                                codeAfter  = 0;
        bool                                  weighed    = false;
        ALScriptWeight::Target                weighedFor = ALScriptWeight::Target::Mono;
        size_t                                total      = 0;
        size_t                                limit      = 0;
        bool                                  estimate   = false;
        bool                                  exact      = false;
        bool                                  sent       = false;
        // What a save sends.
        bool                                  notecard   = false;
        size_t                                assetBytes = 0;
        // The views', and whether a save preprocesses, and why.
        bool                                  expandable = false;
        bool                                  expanded   = false;
        ALPreprocessor::Wanted                wanted     = ALPreprocessor::Wanted::No;
        // The colours the weight and the size are said in past a limit.
        LLColor4                              errorColor;
        LLColor4                              warningColor;
        bool operator==(const Steady&) const = default;
    };
    Steady steadyOf(Doc& doc) const;
    std::optional<Steady> mSteady;
    std::vector<Part>     mSteadyLead;
    std::vector<Part>     mSteadyRest;
};

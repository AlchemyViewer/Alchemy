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
#include "alscriptweight.h"
#include "aljumpbar.h"
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
        // The path the caret is in changed: the outline follows it.
        virtual void pathChanged(Doc& doc) = 0;
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
    ~ALScriptCrumbsBar() override;
    bool postBuild() override;

    // The path of the tab in front's caret, and its trailer: the bar told
    // only where the path, the outline, the tab or its name has changed --
    // it is asked on every key -- and the trailer every time.
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
    void setTips(Tips tips) { mTips = std::move(tips); }

private:
    // The trailer's parts, each added to `parts` where it has anything to
    // say: the place -- read only, vim's word, the caret; the selection;
    // the problems; what it weighs, or what the optimizer made it; what a
    // save would send; and the view.
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
    LLHandle<LLContextMenu> mIndentMenu;
    Tips                    mTips;
    // Whose path the bar shows, so that a tab come to the front is shown
    // there whatever its own path was when last shown.
    std::string             mShownFor;
};

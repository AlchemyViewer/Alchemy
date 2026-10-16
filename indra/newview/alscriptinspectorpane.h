/**
 * @file alscriptinspectorpane.h
 * @brief Script Studio's inspector: what is known of the name at the caret, and what is wrong there.
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
#include "alscriptstudioplaces.h"
#include "llpanel.h"

#include <string>
#include <vector>

class ALScriptStudioServices;
class ALTextView;

// The inspector of a Script Studio window: what the analyzers know of the
// name at the tab in front's caret -- its declaration, read as code in the
// colours the script's own text gives it, where it is declared, as a link
// there, what is expected of it, its type, and what it does, from the
// analyzer or else the grid's definitions, with its page -- and whatever is
// squiggled where the caret is. The reference's words about a word of the
// language are shown here too. Following the link is the window's.
class ALScriptInspectorPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptInspectorPane, LLPanel);
    typedef ALScriptStudioDoc        Doc;
    typedef ALScriptPlaces::Declared Declared;

    // What the inspector asks of the window beyond its services.
    class Window
    {
    public:
        // Where a name is declared, gone to: in the script, or in the
        // include it was declared in.
        virtual void goToDeclared(const LLSD& value) = 0;
        // Whether the preprocessor makes what a tab sends, and so what the
        // analyzers read: the places they say are the expansion's.
        virtual bool preprocessed(const Doc& doc) const = 0;

    protected:
        ~Window() = default;
    };

    explicit ALScriptInspectorPane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // Words shown: every URL in them a link, the line after the first a
    // link to where the name is declared where `declared` says, and the
    // lines given code, styled as the tab in front's editor colours the
    // name at its caret.
    void show(const std::string& text, const Declared& declared = Declared(), const std::vector<S32>& code_lines = {});
    // What the analyzers said of the name at a tab's caret, where the tab
    // is in front and the caret still on the name it was asked about.
    void inspected(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at);
    // What is squiggled under a position, from the checkers and the
    // compiler, each with what it says; empty where nothing is.
    std::string problemsAt(const Doc& doc, const ALTextPos& at) const;
    // Nothing shown.
    void        forget();
    ALTextView* view() const { return mSymbol; }

private:
    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALTextView*             mSymbol   = nullptr;
};

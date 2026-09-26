/**
 * @file alscriptstudioweighing.h
 * @brief Script Studio's weighing: what a script comes to for its target, kept as saved, shown in the editor and the Weights tab.
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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;
class ALScriptWeightsPane;

// A Script Studio window's weighing, as the tab's part `doc.weighing` keeps
// it: what a script comes to for the target it is compiled for, weighed with
// each check and again as a save sends it; each target's, while the Weights
// tab is looked at; what it came to when last saved, which the tab's change
// column counts from; what each fix in the list shown would change it by;
// and what a save would send, in bytes. Shown as notes and heat in the
// editor, and in the Weights tab, which is the UI.
class ALScriptStudioWeighing
{
public:
    typedef ALScriptStudioDoc Doc;

    // What weighing asks of the window beyond its services.
    class Window
    {
    public:
        // The analyzers asked to weigh a tab as its check has it; and a
        // question of their own put to them, answered later.
        virtual void askWeights(Doc& doc)                                                                                         = 0;
        virtual void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) = 0;
        // Whether a tab is a fragment of LSL -- an include's functions --
        // which is not weighed; whether the preprocessor makes what it
        // sends; and the name an include is shown by.
        virtual bool        lslFragment(const Doc& doc) const                          = 0;
        virtual bool        preprocessed(const Doc& doc) const                         = 0;
        virtual std::string includeName(const Doc& doc, const std::string& path) const = 0;
        // The settings: whether the preprocessor's optimizer runs after
        // the check's expansion; the viewer's version, which an envelope
        // says; and whether the editors show the weights as notes and heat.
        virtual bool        optimizing() const     = 0;
        virtual std::string programVersion() const = 0;
        virtual bool        weightNotes() const    = 0;
        virtual bool        weightHeat() const     = 0;
        // The Problems told of a tab's weight; and a save told, which warns
        // where it is over its limit.
        virtual void refreshProblems(Doc& doc) = 0;
        virtual void warnOverWeight(Doc& doc)  = 0;
        // The Weights tab: whether it is looked at, and the pane.
        virtual bool                 weightsShown() const = 0;
        virtual ALScriptWeightsPane* weightsPane()        = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioWeighing(ALScriptStudioServices& services, Window& window);

    // The target a tab is weighed for; none for one that is not weighed --
    // not loaded, a notecard, a fragment, a target there is no weigher for.
    std::optional<ALScriptWeight::Target> target(const Doc& doc) const;
    // The targets its check asks to be weighed for: its own, and the rest
    // while it is in front and the Weights tab is looked at.
    std::vector<ALScriptWeight::Target> targets(const Doc& doc) const;
    // Weighed as it stands, with its check; and what came back.
    void weigh(Doc& doc);
    void weighed(Doc& doc, const ALScriptAnalysis::Result& result);
    // Weighed as a run of the preprocessor made it to be sent.
    void weighSent(Doc& doc);
    // What it weighs now taken as what it weighed when saved, where it is
    // saved and weighed as it stands.
    void keepSaved(Doc& doc);
    // Its weight shown in its editor as notes and heat, as the settings say.
    void showInEditor(Doc& doc);
    // What a save would send, in bytes, measured again where the text or
    // its expansion changed since.
    void measureAsset(Doc& doc);
    // The fixes an editor's list shows, to be weighed once nothing more is
    // coming to it.
    void fixesShown(const std::string& id, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes);
    // The Weights tab filled with the tab in front's weights, or why there
    // are none; and asked to be, since what it shows changed.
    void refreshPane();
    void stale() { mStale = true; }
    // Each frame: the fixes shown weighed once they have settled, and the
    // Weights tab kept filled while it is looked at, the tab in front
    // weighed for the targets it asks for.
    void pump();

private:
    void weighedSent(Doc& doc, const ALScriptAnalysis::Result& result, const Doc::Expanded& sent);
    // A copy of the text the analyzers read -- the expansion where the
    // preprocessor makes one -- with a fix's edits made; false where they
    // cannot be made there.
    bool editedCopy(const Doc& doc, const std::vector<std::pair<ALTextRange, std::string>>& edits, std::string& out) const;
    void weighFixes(Doc& doc, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // The fix list last shown, to be weighed once nothing more is coming
    // to it.
    struct FixesToWeigh
    {
        std::string                    id;
        U32                            shown = 0;
        std::vector<ALCodeEditor::Fix> fixes;
    };
    std::optional<FixesToWeigh> mFixesToWeigh;
    // Whether the Weights tab was looked at the frame before, and whether
    // what it shows is out of date.
    bool                        mWasShown = false;
    bool                        mStale    = true;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>       mAlive = std::make_shared<bool>(true);
};

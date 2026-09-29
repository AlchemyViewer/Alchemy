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

#include "alcodeeditor.h"
#include "alscriptanalysis.h"
#include "alscriptstudiodoc.h"
#include "alscriptweight.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioAnalysis;
class ALScriptStudioServices;
class ALScriptWeightsPane;

// What the script weighs (ALScriptStudioWeighing).
struct ALScriptStudioDoc::Weighing
{
    // For its target, as the last weighing said of the text at
    // `version`; whether what was weighed is what a save compiles --
    // not where the optimizer changes it after -- and whether it is
    // what a preprocessor's run made to be sent, which a check's
    // weighing of the same text, before the optimizer, does not
    // replace; and a weighing on its way.
    std::optional<ALScriptWeight> weight;
    U32                           version = 0;
    bool                          exact   = false;
    bool                          sent    = false;
    bool                          asking  = false;
    // The version its weights were last asked for, by a weigh or with
    // a check: a tab come to the front is weighed once for its text.
    U32                           askedFor = 0;
    // What the Weights tab lists: each target the last check's text
    // was weighed for, its own first, in the source's places, of the
    // text at `allVersion`; and each target's as the text was last
    // saved, where it was weighed while it was that text -- what
    // "since the save" counts from.
    std::vector<ALScriptWeight> all;
    U32                         allVersion = 0;
    std::vector<ALScriptWeight> saved;
    // What a save would send, in bytes, as the last check measured
    // it, and the text and the expansion it was measured of: what the
    // trailer says once it is past half of what a script may be.
    size_t                             assetBytes = 0;
    std::optional<std::pair<U32, U32>> assetMeasured;
};

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

    // What weighing asks of the window beyond its services and its analysis.
    class Window
    {
    public:
        // The analyzers asked to weigh a tab as its check has it.
        virtual void askWeights(Doc& doc) = 0;
        // The settings: whether the preprocessor's optimizer runs after
        // the check's expansion; the viewer's version, which an envelope
        // says; and whether the editors show the weights as notes and heat.
        virtual bool        optimizing() const     = 0;
        virtual std::string programVersion() const = 0;
        virtual bool        weightNotes() const    = 0;
        virtual bool        weightHeat() const     = 0;
        // A save told of a tab's weight, which warns where it is over its
        // limit.
        virtual void warnOverWeight(Doc& doc) = 0;
        // The Weights tab: whether it is looked at, and the pane.
        virtual bool                 weightsShown() const = 0;
        virtual ALScriptWeightsPane* weightsPane()        = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioWeighing(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window);

    // The target a tab is weighed for; none for one that is not weighed --
    // not loaded, a notecard, a fragment, a target there is no weigher for.
    std::optional<ALScriptWeight::Target> target(const Doc& doc) const;
    // The targets its check asks to be weighed for: its own, and the rest
    // while it is in front and the Weights tab is looked at.
    std::vector<ALScriptWeight::Target> targets(const Doc& doc) const;
    // Weighed as it stands, with its check; and what came back.
    void weigh(Doc& doc);
    // Whether a weighing of the text as it stands is on its way.
    static bool asking(const Doc& doc);
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
    // What a save would send, as a save measured it: one refused for its
    // size, which the trailer says.
    static void measuredBySave(Doc& doc, size_t bytes) { doc.weighing->assetBytes = bytes; }
    // Its weights asked for with a check of the text at `version`, in the
    // same job, rather than again when it comes to the front.
    static void askedWithCheck(Doc& doc, U32 version) { doc.weighing->askedFor = version; }
    // What a run of the preprocessor makes to be sent is made differently
    // now -- its settings changed -- and the check's weighing of the text
    // is taken again in place of what the last run made.
    static void sendsDifferently(Doc& doc) { doc.weighing->sent = false; }
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
    ALScriptStudioAnalysis& mAnalysis;
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

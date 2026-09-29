/**
 * @file alscriptstudiocaret.h
 * @brief Script Studio's name at the caret: its definition gone to, its references and rename asked for, the inspector told.
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

#include "alkeymap.h"
#include "alscriptanalysis.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudiowords.h"

#include <string>
#include <vector>

class ALScriptStudioAnalysis;
class ALScriptStudioServices;

// The tab's part of what is said of its caret: the name asked about, the
// path the bar shows, and the inspector's question about where the caret
// is.
struct ALScriptStudioDoc::Caret
{
    // The name last asked about -- its definition, its references, a
    // new name -- where, and of which text.
    ALEditorCommand symbolCommand = ALEditorCommand::None;
    U32             symbolVersion = 0;
    ALTextPos       symbolAt;
    // The symbols the caret is in, the outermost first, by the outline
    // it was read from (ALScriptStudioCaret::placePath): what the bar
    // at the bottom and the outline both show.
    std::vector<size_t> crumbPath;
    U32                 crumbsOf = 0;
    // Where the caret was last seen; when the inspector is due to be
    // told what it is on, or zero; and what it was last told about.
    ALTextPos seen{ -1, -1 };
    F64       inspectDue = 0.0;
    ALTextPos inspectAt{ -1, -1 };
    U32       inspectVersion = 0;
};

// A Script Studio window's name at the caret, as the tab's part
// `doc.caret` keeps it: Go to Definition, Find References and Rename asked
// of the analyzers about the name under the caret, and their answer read
// back to the source -- this script's places and an include's, the
// declaration's -- then gone to, or handed to the lookups across scripts;
// a word of the language with no definition of the script's own shown in
// the reference. And the caret watched each frame: the bar told where it
// moved, the lit places put out once it has left them, and the inspector
// told of what it is on once it has settled -- what is wrong there, or the
// name there asked about.
class ALScriptStudioCaret
{
public:
    typedef ALScriptStudioDoc Doc;

    // How long the caret must settle before the inspector is told, as
    // long as the analyzers wait after the last keystroke.
    static constexpr F64 SETTLE = 0.35;

    // What the name at the caret asks of the window itself, beyond what it is given.
    class Window
    {
    public:
        // A jump about to be made, for Back to come back from; and the
        // keyboard in the text in front, which ends a walk down a list.
        virtual void noteJump()       = 0;
        virtual void keyboardInText() = 0;
        // A stretch of a tab's source gone to in the view in front, the
        // keyboard given it; a place of an include opened.
        virtual void goTo(Doc& doc, const ALTextRange& range)                                                       = 0;
        virtual void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length) = 0;
        // A word of the language shown in the reference.
        virtual void showReference(const ALScriptStudioWords::Vocab& word, bool lua) = 0;
        // The lookup across the object's scripts started from what was
        // found in the tab.
        virtual void startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition,
                                 const std::string& home_path, const ALScriptSpan& definition, std::vector<Doc::Place> places,
                                 U32 version) = 0;
        // The symbols the caret is in, found (placePath): shown on the bar,
        // and -- where they changed -- followed by the outline; and what is
        // wrong where the caret is shown in the inspector, false where
        // nothing is.
        virtual void showPath(Doc& doc, bool changed)              = 0;
        virtual bool showProblemsAt(Doc& doc, const ALTextPos& at) = 0;
        // Whether the inspector is out to be read: folded away, what is at
        // the caret is not asked, and is asked as it comes out.
        virtual bool inspectorShown() const                        = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioCaret(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window);

    // A command about the name at the caret asked of the analyzers, and
    // what they answered done, where it answers that question of that text.
    void ask(Doc& doc, ALEditorCommand command, const ALTextRange& word);
    void answered(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at);
    // Each frame, at `now`: the tab in front's caret watched.
    void pump(F64 now);
    // A tab's caret seen afresh on the next frame, wherever it stands: the
    // view in front is another view now, or the tab another tab.
    static void seeAfresh(Doc& doc) { doc.caret->seen = ALTextPos(-1, -1); }
    // The inspector told of the caret again once it is seen, having been
    // told of another tab's since.
    static void inspectAfresh(Doc& doc) { doc.caret->inspectAt = ALTextPos(-1, -1); }
    // The inspector told now of the word of the source at `at`, rather than
    // a moment after the caret settles.
    void inspect(Doc& doc, const ALTextPos& at);
    // The symbols a tab's caret is in, found once for both of the panes
    // that show them, and shown: as the caret moves, as the outline is
    // made again, as the tab comes to the front or is renamed. None while
    // the expansion is in front, whose lines are not the outline's.
    void placePath(Doc& doc);

private:
    ALScriptStudioServices& mServices;
    ALScriptStudioAnalysis& mAnalysis;
    Window&                 mWindow;
};

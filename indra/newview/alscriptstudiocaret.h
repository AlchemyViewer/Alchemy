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

#include "alscriptanalysis.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudiowords.h"

#include <string>
#include <vector>

class ALScriptStudioServices;

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

    // What the name at the caret asks of the window beyond its services.
    class Window
    {
    public:
        // The analyzers asked about a place of a tab's source; whether
        // the preprocessor makes what they read; and a line of an include
        // as it reads, false where it is not had.
        virtual void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at)  = 0;
        virtual bool preprocessed(const Doc& doc) const                                      = 0;
        virtual bool sourceLine(const std::string& path, S32 line, std::string& out) const = 0;
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
        // The caret moved: the bar's path; and what is wrong where it is
        // shown in the inspector, false where nothing is.
        virtual void showPath(Doc& doc)                           = 0;
        virtual bool showProblemsAt(Doc& doc, const ALTextPos& at) = 0;
        // Whether the inspector is out to be read: folded away, what is at
        // the caret is not asked, and is asked as it comes out.
        virtual bool inspectorShown() const                        = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioCaret(ALScriptStudioServices& services, Window& window);

    // A command about the name at the caret asked of the analyzers, and
    // what they answered done, where it answers that question of that text.
    void ask(Doc& doc, ALEditorCommand command, const ALTextRange& word);
    void answered(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at);
    // Each frame, at `now`: the tab in front's caret watched.
    void pump(F64 now);

private:
    ALScriptStudioServices& mServices;
    Window&                 mWindow;
};

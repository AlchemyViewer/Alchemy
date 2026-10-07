/**
 * @file altextfeatures.h
 * @brief What a view built over a text view adds to it: folding, completion, fixes, names, marks and brackets.
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
#include "stdtypes.h"

class LLColor4;
struct ALTextPos;

// What a view built over a text view adds to it, asked by the text view
// where it has a command or a question it cannot answer alone: folding,
// the code's structure, completion, signature help, fixes, the name at the
// caret, the marks beside lines and a bracket's partner. The code editor
// is one (ALTextView::setFeatures); a plain text view has none, and does
// without -- no command of theirs done, no marks, hidden lines shown
// again one at a time, brackets unmatched.
class ALTextFeatures
{
public:
    // The commands that are the features' own -- folding, the code's
    // structure, completion, signature help, a fix, the name at the caret
    // -- done; and, for those a menu asks about, whether they could be.
    // Neither for a command that is none of theirs.
    virtual bool performFeature(ALEditorCommand command) = 0;
    virtual bool canPerformFeature(ALEditorCommand command) const = 0;
    // Whether anyone answers questions about names at all: the right-click
    // menu leaves them out of a view nobody answers them for.
    virtual bool offersSymbols() const = 0;
    // The caret has landed on a hidden line, and it must be seen.
    virtual void revealLine(S32 line) = 0;
    // What the ruler and the map show beside a line: a mark's colour,
    // where there is one for it; and a count that moves on whenever the
    // marks do -- set, cleared, or moved with their lines by an edit -- so
    // that the lines with one are not looked for every frame, nor at every
    // edit.
    virtual bool mapMark(S32 line, LLColor4& color) const = 0;
    virtual U32  marksRevision() const = 0;
    // The bracket a closing one at a place closes, matched past strings
    // and comments.
    virtual bool closerOpenedAt(const ALTextPos& closer, ALTextPos& opener) = 0;

protected:
    ~ALTextFeatures() = default;
};

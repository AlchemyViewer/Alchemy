/**
 * @file alvimhost.h
 * @brief What vim asks of a text view beyond the view: lit layers, folds, functions and brackets.
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

#include "altextdocument.h"
#include "alfoldmodel.h"

#include <optional>
#include <vector>

class ALBracketIndex;

// What vim asks of a text view beyond the text view itself: layers of lit
// places it may light and put out, the text's folds and functions, and a
// bracket index kept with the text. The code editor is one
// (ALTextView::vimHost); a plain text view is none, and vim does without --
// nothing lit, no folds, no functions, the brackets summed afresh.
class ALVimHost
{
public:
    // The layers vim lights: every match of a search, as hlsearch and
    // incsearch have them; a visual block; and the matches an asking :s has
    // still to come. The host's own -- Find References, a name's
    // occurrences -- are none of vim's to touch.
    enum class Layer : U8
    {
        Search,
        Block,
        Confirm
    };
    virtual void                            setLayer(Layer layer, std::vector<ALTextRange> ranges) = 0;
    virtual void                            clearLayer(Layer layer) = 0;
    // Those that begin before a place let go, the rest left lit.
    virtual void                            clearLayerBefore(Layer layer, const ALTextPos& pos) = 0;
    virtual const std::vector<ALTextRange>& layer(Layer layer) const = 0;

    // The brackets of the text, kept with it: %, [( and the bracket objects.
    virtual ALBracketIndex& bracketIndex() = 0;

    // The text's blocks, by start line; one folded or opened at a line, or
    // around it; all of them; and whether one folded starts at a line.
    virtual const std::vector<ALFoldModel::Region>& foldRegions() = 0;
    virtual bool                                    foldAt(S32 line)         = 0;
    virtual bool                                    unfoldAt(S32 line)       = 0;
    virtual void                                    foldAll()                = 0;
    virtual void                                    unfoldAll()              = 0;
    virtual bool                                    isFolded(S32 line) const = 0;

    // The start of the next function after a place, or of the last one
    // before it, or with `ends` the end; and the innermost function holding
    // a stretch and more besides.
    virtual std::optional<ALTextRange> functionFrom(const ALTextPos& at, bool forward, bool ends) const = 0;
    virtual std::optional<ALTextRange> functionAround(const ALTextRange& range) const                  = 0;

protected:
    ~ALVimHost() = default;
};

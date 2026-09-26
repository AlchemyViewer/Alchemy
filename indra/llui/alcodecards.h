/**
 * @file alcodecards.h
 * @brief A code editor's cards: the hover card over the text, and signature help for a call being typed.
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

#include "alfixlistmodel.h"
#include "altextdocument.h"
#include "llrect.h"
#include "llsd.h"
#include "v4color.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// What a code editor's cards say and where they go, and what it keeps for
// them: the hover card over the text -- the problems under the mouse, the
// fixes for their line, and what the word is -- with the analyzer's answer
// kept for the word it was asked about; and signature help for the call
// the caret is in. The editor makes the card's view, styles it, and draws
// the signature; this says what their lines are and where they go, with
// nothing laid out or drawn.
class ALCodeCards
{
public:
    // --- what a card says ---------------------------------------------------------

    // A problem a card says, in its squiggle's colour.
    struct Problem
    {
        std::string message;
        LLColor4    color;
    };
    // A line of what a card says that is a way somewhere -- where the word
    // was declared -- by its words: a link on the card that hands its value
    // to whoever gave it.
    struct Link
    {
        std::string line;
        std::string tooltip;
        LLSD        value;
    };
    // A card's text, and what its lines are: the problems, a line or more
    // each; under them a line for each fix that would put them right, in
    // the order given; a blank line; then what the word is, its first line
    // the head. After the head, a line with the deprecation note in it, and
    // each link on the first line below the head that says its words.
    struct Composition
    {
        std::string                         text;
        // Each line of a problem's, and which problem it is.
        std::vector<std::pair<S32, size_t>> problemLines;
        std::vector<std::pair<S32, LLSD>>   fixLines;
        S32                                 headLine = -1;
        std::vector<S32>                    deprecatedLines;
        // Each link's line, and which link it is.
        std::vector<std::pair<S32, size_t>> linkLines;
    };
    static Composition compose(const std::vector<Problem>& problems, const std::vector<ALCodeFix>& fixes, const std::string& says,
                               const std::vector<Link>& links);
    // What a card says of something deprecated, on a line of its own:
    // whoever writes the words puts this, and the card colours it.
    static const std::string& deprecatedNote();

    // --- where it goes ------------------------------------------------------------

    // The widest a card is, and the room inside its edge.
    static constexpr S32 MAX_WIDTH = 560;
    static constexpr S32 PAD       = 6;
    // How wide a card may be over text so wide; how wide one is whose
    // widest line is so wide, within that; and how tall one is whose lines
    // come to so much.
    static S32 widthLimit(S32 text_width);
    static S32 width(S32 content_width, S32 limit);
    static S32 height(S32 content_height);
    // Where it goes: under what it is about, at its start and two pixels
    // clear; above it where under would run off the text's bottom and
    // above would not; within the text's width.
    static LLRect place(const LLRect& anchor, const LLRect& text, S32 width, S32 height);

    // --- what the analyzer says of a word ---------------------------------------

    // Whether a word has been asked about as the text stands -- its
    // version -- and the asking of one, which lets go of what came for the
    // last.
    bool askedAbout(const ALTextRange& word, U32 version) const { return word == mAsked && version == mAskedVersion; }
    void asking(const ALTextRange& word, U32 version);
    // An answer: kept where it is for the word last asked about, as the
    // text stands, and false where it is not -- come too late.
    bool                     heard(const ALTextPos& at, U32 version, const std::string& text, std::vector<Link> links);
    const ALTextRange&       asked() const { return mAsked; }
    const std::string&       answer() const { return mAnswer; }
    const std::vector<Link>& links() const { return mLinks; }

    // --- signature help -----------------------------------------------------------

    // The call the caret is in: the label with each parameter's span in
    // it, in bytes, the one the caret is at, and a line of documentation.
    struct Signature
    {
        std::string                      label;
        std::vector<std::pair<S32, S32>> parameters;
        S32                              active = 0;
        std::string                      documentation;
    };
    void             showSignature(const ALTextPos& at, Signature signature);
    void             hideSignature() { mSignature.reset(); }
    const Signature* signature() const { return mSignature ? &*mSignature : nullptr; }
    const ALTextPos& signatureAt() const { return mSignatureAt; }
    // Shown, and still about the call a caret is in: on its line, and not
    // before where it began.
    bool signatureFor(const ALTextPos& caret) const;

    static constexpr S32 SIGNATURE_PAD = 6;
    // A signature's box, so wide and tall, for a call whose column is at
    // the anchor's left on its row: above the row, kept inside the view --
    // no wider than it -- and under the row where above would run off the
    // view's top and under would not.
    static LLRect signatureBox(S32 wanted_width, S32 height, const LLRect& anchor, const LLRect& view);
    // How far a label longer than the room is scrolled: so that it reads
    // through the parameter being filled in, whose end is `through` from
    // its start -- the head, already typed, being the part to give up.
    static F32 labelShift(F32 label_width, F32 through, F32 room);

private:
    ALTextRange              mAsked;
    U32                      mAskedVersion = 0;
    std::string              mAnswer;
    std::vector<Link>        mLinks;
    std::optional<Signature> mSignature;
    ALTextPos                mSignatureAt;
};

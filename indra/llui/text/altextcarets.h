/**
 * @file altextcarets.h
 * @brief The selections a text view has besides its main one.
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

#ifndef AL_ALTEXTCARETS_H
#define AL_ALTEXTCARETS_H

#include "alanchoredranges.h"

#include <utility>
#include <vector>

// The selections a view has besides its main one, which stays the view's
// own so that everything that knows of one selection goes on working. Each
// is a range from its anchor to its caret, as the view's selection is --
// the caret before the anchor where it was made backwards -- and an empty
// one is a caret alone. They are kept in the order they begin and in step
// with the text's edits, as ALAnchoredRanges keeps what is laid over it:
// a caret is pushed along by text put in right at it, as typing pushes the
// main one, and a selection keeps what it held, what is put in at either
// edge staying outside it.
//
// None is over another. Two selections that overlap become one, and so do
// a caret and a selection or a caret it touches; two selections that only
// touch -- two matches side by side -- stay two. One that meets the main
// selection becomes part of it, the main one keeping its place as main.
class ALTextCarets
{
public:
    // Where one is in the text, in order: its range, normalised.
    struct Normalised
    {
        ALTextRange operator()(const ALTextRange& selection) const { return selection.normalised(); }
    };
    typedef ALAnchoredRanges<ALTextRange, Normalised> ranges_t;
    typedef ranges_t::const_iterator                  const_iterator;

    const std::vector<ALTextRange>& selections() const { return mSelections.items(); }
    size_t                          size() const { return mSelections.size(); }
    bool                            empty() const { return mSelections.empty(); }
    const ALTextRange&              operator[](size_t i) const { return mSelections[i]; }
    const_iterator                  begin() const { return mSelections.begin(); }
    const_iterator                  end() const { return mSelections.end(); }
    // Those that may lie on a line, in order; each still asked whether it
    // does, one that began above it having perhaps ended.
    std::pair<const_iterator, const_iterator> onLine(S32 line) const { return mSelections.onLine(line); }

    // All of them at once, or one more, merged where they meet each other.
    // The main selection is merged with them by merge(), which whoever
    // holds it asks once it has put it.
    void assign(std::vector<ALTextRange> selections);
    void add(const ALTextRange& selection);
    void clear() { mSelections.clear(); }

    // Those that meet merged, the main selection with them, which grows by
    // any it meets: what the main one put somewhere asks. Whether it changed.
    bool merge(ALTextRange& main);

    // An edit made: each slid along with the text, and those it brought
    // together merged -- among themselves, since the main selection is
    // placed by whoever made the edit.
    void apply(const ALTextDocument::Edit& edit);

    // A selection moved along by an edit as these are: a caret pushed past
    // what was put in at it, a selection's ends kept outside what was put
    // in at them, and either way back to where the edit began where the
    // edit took what it stood on.
    static ALTextRange slid(const ALTextRange& selection, const ALTextDocument::Edit& edit);
    // Whether two selections, the first beginning at or before the second,
    // are one: overlapping, or touching where either is a caret.
    static bool meet(const ALTextRange& first, const ALTextRange& second);
    // Two that meet as one, over both, running the way `winner` runs -- its
    // caret at the end or at the start -- or the way `loser` does where the
    // winner is a caret.
    static ALTextRange joined(const ALTextRange& winner, const ALTextRange& loser);

private:
    // Each that meets the one before it joined to it, the main selection
    // among them where one is given.
    bool sweep(ALTextRange* main);

    ranges_t mSelections;
};

#endif // AL_ALTEXTCARETS_H

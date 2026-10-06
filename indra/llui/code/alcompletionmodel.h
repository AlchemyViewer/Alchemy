/**
 * @file alcompletionmodel.h
 * @brief What a code editor offers to complete a word with, and in what order.
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

#include "alfuzzymatch.h"
#include "alsyntaxgrammar.h"
#include "altextdocument.h"
#include "lluiimage.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A word a code editor offers to complete what is typed with.
struct ALCompletion
{
    std::string  text;
    std::string  detail;
    ALSyntaxKind kind = ALSyntaxKind::Text;
    // A snippet rather than a word: accepting it puts this body in place
    // of the prefix, with its placeholders to tab through.
    std::string  snippet;
    // What it does, shown beside the list while it is the one chosen: a
    // handle to it, so that a list copied at every key copies no text of
    // it, and a word's text outlives the vocabulary it came from while a
    // list shows it. shared() makes one of a text, or none of nothing.
    std::shared_ptr<const std::string> documentation;
    static std::shared_ptr<const std::string> shared(std::string text)
    {
        return text.empty() ? nullptr : std::make_shared<const std::string>(std::move(text));
    }
    // Struck from the language: marked so on the list and last in it, but
    // completed as what it is -- a function with its brackets.
    bool         deprecated = false;
    // Of the type wanted where it goes, as whoever answered knows: first
    // among those that match what was typed as well.
    bool         fits = false;
    // Where a call's brackets go once it is taken, where whoever answered
    // knows: else as the editor guesses from its kind, a function called;
    // none, for a function passed rather than called; after an empty pair,
    // for one that takes nothing; or between them. Never where they are
    // there already.
    enum class Brackets : U8
    {
        Guess,
        None,
        After,
        Inside
    };
    Brackets     brackets = Brackets::Guess;
    // The mark before it on the list, where the provider has one; else
    // the icon of its kind, or a badge where the icons are not to be had.
    LLUIImagePtr icon;
};

// The list of completions for the identifier being typed, as it narrows:
// what the editor's provider answered, what an analyzer answered later for
// the same identifier merged in by name, and the document's own words after
// them, the best match first. It says what is listed; the list drawn and
// the choice taken are the editor's.
class ALCompletionModel
{
public:
    // The most a list holds.
    static constexpr size_t CAP = 200;

    // How well what was typed matches a word, best first, or -1 for not
    // at all: 0 its start as typed, 1 its start in either case, 2 a run
    // of it from where one of its parts begins -- `Say` in `llSay`,
    // `listen` in `llListen` -- and 3 a letter at the start of each of
    // several parts, runs of each after -- `sp` or `spos` in
    // `llSetPos`. The shared matcher's first four tiers (ALFuzzyMatch),
    // whose parts begin after an underscore or a dot, at a capital after
    // a small letter, and at a digit.
    static S32 matchTier(std::string_view word, std::string_view typed);
    static S32 tierOf(const ALFuzzyMatch::Match& match);
    // The icon a kind wears on the list -- Symbol_Function and the rest --
    // and the badge, a letter, where there is no image provider to look
    // them up in.
    static const char* iconNameOf(const ALCompletion& completion);
    static const char* badgeOf(const ALCompletion& completion);
    // The document's own words that match what was typed -- but the one
    // being typed, which ends at `at`, one that starts with a digit, one no
    // longer than what was typed, and one `out` has already -- after what
    // `out` holds, up to `most` in all.
    static void documentWords(const ALTextDocument& text, const ALTextPos& at, std::string_view prefix, std::vector<ALCompletion>& out,
                              size_t most = CAP);
    // In the order offered: the start of the word as typed, then in either
    // case, then a part of it, then letters of its parts. Among equals what
    // fits where it goes; then the script's own names -- a parameter, a
    // local, a field -- then the language's words, then its constants, then
    // what is deprecated, then the document's bare words; then the
    // alphabet. No more than CAP.
    static void rank(std::vector<ALCompletion>& list, std::string_view prefix);

    // What a list draws from while it narrows one identifier: what the
    // provider answered as it was first asked, and the document's words,
    // gathered once and filtered again as the prefix grows, rather than
    // asked for and scanned at every key. Whether the pool is for the
    // identifier starting at `start`, after `head`, with what was typed
    // so far still starting with what it was gathered for; and the pool
    // gathered: `answered` what the provider said of `head` and `prefix`,
    // the head and its separator still on each name.
    bool pooled(const ALTextPos& start, const std::string& head, std::string_view prefix) const;
    void pool(const ALTextPos& start, const ALTextPos& at, std::string_view prefix, const std::string& head, char separator,
              std::vector<ALCompletion> answered, const ALTextDocument& text);
    // The list for an identifier starting at `start`, typed up to `at`,
    // `prefix` so far: the pool narrowed to it. True where the identifier
    // is one the list was not narrowing before, of which whoever answers
    // later is to be asked; what an earlier one answered is let go of.
    bool narrow(const ALTextPos& start, const ALTextPos& at, std::string_view prefix);
    // Both at once, after `head` and a dot.
    bool narrow(const ALTextPos& start, const ALTextPos& at, std::string_view prefix, const std::string& head, std::vector<ALCompletion> answered,
                const ALTextDocument& text);
    // What was answered later about the identifier starting at `start`:
    // kept, and true, where it is still the one the list narrows. `words`
    // false where the answer says the document's own words are no use
    // there -- a type, a string -- which then leave the list until it
    // narrows another identifier.
    bool supply(const ALTextPos& start, std::vector<ALCompletion> more, bool words = true);
    // The list let go of, and what the identifier was asked as with it;
    // and all of it, what was answered too, as the list closes.
    void hide();
    void close();
    // Whether a list made now for `asked` is the one last shown, an answer
    // joined to it, which keeps what was chosen in it; `asked` remembered.
    bool relisted(const std::string& asked);

    const std::vector<ALCompletion>& list() const { return mList; }
    // The identifier the list narrows, which the one chosen replaces.
    const ALTextRange& range() const { return mRange; }
    // Where the identifier asked about starts, or -1s for none.
    const ALTextPos&   asked() const { return mAsked; }

private:
    std::vector<ALCompletion> mList;
    ALTextRange               mRange;
    std::string               mListedFor;
    ALTextPos                 mAsked{ -1, -1 };
    // What was answered later, kept through every narrowing of the same
    // identifier until the list closes; and whether it wants the
    // document's words beside it.
    std::vector<ALCompletion> mSupplied;
    bool                      mWords = true;
    // The pool (pooled): what the provider answered, and the document's
    // words apart, which are offered only where longer than what is typed.
    std::vector<ALCompletion>         mPool;
    // Each made ready to be matched (ALFuzzyMatch), beside it.
    std::vector<ALFuzzyMatch::Target> mPoolTargets;
    std::vector<ALFuzzyMatch::Target> mPoolWords;
    ALTextPos                         mPoolStart{ -1, -1 };
    std::string                       mPoolHead;
    std::string                       mPoolPrefix;
};

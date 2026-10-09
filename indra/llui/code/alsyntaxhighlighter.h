/**
 * @file alsyntaxhighlighter.h
 * @brief A document's lines lexed by a grammar, kept up to date as it changes.
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

#include "allinetable.h"
#include "alsyntaxgrammar.h"
#include "altextdocument.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <vector>

// The tokens of every line of a document, by a grammar, lexed when asked
// for and kept as the document changes. An edit marks its lines dirty; the
// next request lexes from the first dirty line, and stops early at the
// first line after the edit that starts in the state it started in
// before, since everything from there on lexes as it did. A line's
// revision moves when its tokens change, which is what a layout cache
// compares.
class ALSyntaxHighlighter
{
public:
    ALSyntaxHighlighter();
    ~ALSyntaxHighlighter();
    ALSyntaxHighlighter(const ALSyntaxHighlighter&) = delete;
    ALSyntaxHighlighter& operator=(const ALSyntaxHighlighter&) = delete;

    // The grammar, and the words its tables hold. Either change lexes
    // everything again.
    void                                   setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar);
    std::shared_ptr<const ALSyntaxGrammar> grammar() const { return mGrammar; }
    // The words its tables hold: a language's, shared by every view of it
    // and taught once rather than copied into each, set again when they
    // change; none, set to nothing. ownWords() is this view's own to
    // change, copied from what it shared where it has none of its own --
    // for a test, or a view taught by hand -- and wordsChanged() says so.
    void                                   setWords(std::shared_ptr<const ALSyntaxWords> words);
    const ALSyntaxWords&                   words() const { return *mWords; }
    ALSyntaxWords&                         ownWords();
    void                                   wordsChanged();

    // The document to follow; null to follow none.
    void attach(ALTextDocument* document);

    // A line's tokens, lexed if need be, along with every line before it
    // that was not. Empty without a grammar or a document.
    const std::vector<ALSyntaxToken>& tokens(S32 line);
    U32                               revision(S32 line);
    // The state a line starts in, lexed to it if need be: where a line that
    // stands for it elsewhere -- what a fix would make of it, previewed --
    // is lexed from. The grammar's first state past the text, or with
    // nothing lexed.
    ALSyntaxState                     startState(S32 line);

    // For whoever wants every line's tokens and asks every frame -- the
    // blocks a gutter draws: the lines that want lexing lexed on from the
    // first of them, no more than `most` of them, and whether every line
    // is lexed now. An edit that changes how each line after it starts --
    // a quote or a comment left open -- is lexed a slice a frame so, what
    // was found of the text before it kept until this says it is done,
    // rather than the whole text lexed in the frame after the edit.
    static constexpr S32 SLICE = 256;
    bool                 lexSome(S32 most = SLICE);

    // Told, as lines are lexed, of the lines whose tokens changed -- whose
    // revisions moved -- first through last, after each request that lexed
    // any: so that whoever keeps what it read of lines' tokens reads those
    // again, and no other line need be asked.
    typedef boost::signals2::signal<void(S32 first, S32 last)> relexed_signal_t;
    boost::signals2::connection onRelexed(const relexed_signal_t::slot_type& slot) { return mRelexed.connect(slot); }

    // How many lines the last request had to lex, for a test that says an
    // edit re-lexes only what it must, and how many it looked at, lexed or
    // found to lex as they did, for one that says the rest are not looked
    // at; and how many states are kept, for a test that says they do not
    // pile up.
    S32    lastLexed() const { return mLastLexed; }
    S32    lastLooked() const { return mLastLooked; }
    size_t statesKept() const { return mStates.size(); }

private:
    struct Line
    {
        // The states the line starts and ends in, by their numbers in the
        // states kept.
        U32                        start    = 0;
        U32                        end      = 0;
        std::vector<ALSyntaxToken> tokens;
        U32                        revision = 0;
        // Whether the tokens are as the line lexes now: from the state it
        // starts in, by the grammar and the words there are.
        bool                       valid    = false;
        // Whether the tokens are of the line's text, as it lexes now or
        // not: what it lexes to next is held against them, and the
        // revision moves only where the two differ.
        bool                       lexed    = false;
    };
    struct StateHash
    {
        size_t operator()(const ALSyntaxState& state) const;
    };

    void reset();
    void onEdit(const ALTextDocument::Edit& edit);
    void ensure(S32 line);
    // The lines through `line` that want lexing lexed, from the first of
    // them, no more than `most` of them; the rest from there next time.
    void lex(S32 line, S32 most);
    // A state's number, kept once whoever starts or ends in it; and the
    // states kept cut back to those lines are in, past a number of them.
    U32  intern(const ALSyntaxState& state);
    void compactStates();

    ALTextDocument*                        mDocument = nullptr;
    boost::signals2::scoped_connection     mConnection;
    std::shared_ptr<const ALSyntaxGrammar> mGrammar;
    std::shared_ptr<const ALSyntaxWords>   mWords;
    std::shared_ptr<ALSyntaxWords>         mOwnWords;
    ALLineTable<Line>                      mLines;
    // Every state a line starts or ends in, each once, and the number of
    // the one the first line starts in: a line keeps two numbers rather
    // than two copies of a stack of states, and where lexing again can stop
    // is where two numbers are the same.
    std::vector<ALSyntaxState>                                 mStates;
    boost::unordered_flat_map<ALSyntaxState, U32, StateHash>   mStateIds;
    U32                                                        mInitialState = 0;
    // The state a line is lexed through, from the one it starts in to the
    // one it ends in: one kept, whose room each line lexed uses again,
    // rather than a copy made and freed for every line.
    ALSyntaxState                                              mLexing;
    // Where each run of lines that may not lex as they did begins, in
    // order: an edit's first line, or where lexing last stopped short. A
    // run goes on from there through each line lexed anew, to the first
    // that starts in the state the line before it ends in; past that,
    // every line lexes as it did until the next run, which lexing goes on
    // to without looking at the lines between.
    std::vector<S32>                                           mDirty;
    S32                                    mLastLexed  = 0;
    S32                                    mLastLooked = 0;
    // The first line that may not lex as it did, past them all for none.
    S32 firstDirty() const { return mDirty.empty() ? static_cast<S32>(mLines.size()) : mDirty.front(); }
    relexed_signal_t                       mRelexed;
};

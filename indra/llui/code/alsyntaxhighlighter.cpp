/**
 * @file alsyntaxhighlighter.cpp
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

#include "linden_common.h"

#include "alsyntaxhighlighter.h"

#include <algorithm>

namespace
{
    const std::vector<ALSyntaxToken> NO_TOKENS;
}

namespace
{
    const std::shared_ptr<const ALSyntaxWords>& noWords()
    {
        static const std::shared_ptr<const ALSyntaxWords> none = std::make_shared<const ALSyntaxWords>();
        return none;
    }
}

ALSyntaxHighlighter::ALSyntaxHighlighter()
:   mWords(noWords())
{
}

ALSyntaxHighlighter::~ALSyntaxHighlighter() = default;

void ALSyntaxHighlighter::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    mGrammar = std::move(grammar);
    reset();
}

void ALSyntaxHighlighter::setWords(std::shared_ptr<const ALSyntaxWords> words)
{
    if (!words)
    {
        words = noWords();
    }
    if (words == mWords)
    {
        return;
    }
    mWords = std::move(words);
    mOwnWords.reset();
    reset();
}

ALSyntaxWords& ALSyntaxHighlighter::ownWords()
{
    if (!mOwnWords || mOwnWords.get() != mWords.get())
    {
        mOwnWords = std::make_shared<ALSyntaxWords>(*mWords);
        mWords    = mOwnWords;
    }
    return *mOwnWords;
}

void ALSyntaxHighlighter::wordsChanged()
{
    reset();
}

void ALSyntaxHighlighter::attach(ALTextDocument* document)
{
    // Off the document it followed: release() would only forget the
    // connection, and the old text's edits would still come here.
    mConnection.disconnect();
    mDocument = document;
    if (mDocument)
    {
        mConnection = mDocument->onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    }
    // Another text: no line's tokens are of its lines, whatever they come
    // to, so every revision moves on.
    for (Line& line : mLines)
    {
        line.lexed = false;
    }
    reset();
}

void ALSyntaxHighlighter::reset()
{
    // Every line lexed again, its tokens and its revision kept until then:
    // a line that lexes to the same tokens afterwards keeps its number,
    // and one that does not moves on.
    mLines.resize(mDocument ? static_cast<size_t>(mDocument->lineCount()) : 0);
    for (Line& line : mLines)
    {
        line.valid = false;
    }
    mFirstDirty = 0;
    mStates.clear();
    mStateIds.clear();
    mInitialState = mGrammar ? intern(mGrammar->initialState()) : 0;
}

size_t ALSyntaxHighlighter::StateHash::operator()(const ALSyntaxState& state) const
{
    size_t hash = state.frames.size();
    for (const ALSyntaxState::Frame& frame : state.frames)
    {
        boost::hash_combine(hash, frame.state);
        boost::hash_combine(hash, std::hash<std::string>()(frame.payload));
    }
    return hash;
}

U32 ALSyntaxHighlighter::intern(const ALSyntaxState& state)
{
    const auto found = mStateIds.find(state);
    if (found != mStateIds.end())
    {
        return found->second;
    }
    // A state no line was in before, which is rare: copied only then.
    const U32 id = static_cast<U32>(mStates.size());
    mStateIds.emplace(state, id);
    mStates.push_back(state);
    return id;
}

void ALSyntaxHighlighter::compactStates()
{
    // A text whose spans open with ever new captures -- long brackets of
    // every level -- makes states no line is in any longer. Past this many,
    // those the lines are in are kept, and renumbered.
    constexpr size_t MOST = 4096;
    if (mStates.size() <= MOST)
    {
        return;
    }
    std::vector<U32>           renumbered(mStates.size(), U32_MAX);
    std::vector<ALSyntaxState> kept;
    const auto                 keep = [&](U32 id) {
        if (renumbered[id] == U32_MAX)
        {
            renumbered[id] = static_cast<U32>(kept.size());
            kept.push_back(std::move(mStates[id]));
        }
        return renumbered[id];
    };
    mInitialState = keep(mInitialState);
    for (Line& line : mLines)
    {
        if (line.valid)
        {
            line.start = keep(line.start);
            line.end   = keep(line.end);
        }
    }
    mStates.swap(kept);
    mStateIds.clear();
    for (size_t i = 0; i < mStates.size(); ++i)
    {
        mStateIds.emplace(mStates[i], static_cast<U32>(i));
    }
}

void ALSyntaxHighlighter::onEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // The lines the edit replaced go, and the lines it made come in, dirty;
    // what follows keeps its tokens until the state it starts in is seen
    // to have changed.
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    mLines.applySpans(spans, mDocument ? mDocument->lineCount() : -1, Line());
    mFirstDirty = llmin(mFirstDirty, llclamp(spans.front().first, 0, static_cast<S32>(mLines.size())));
}

void ALSyntaxHighlighter::ensure(S32 line)
{
    lex(line, S32_MAX);
}

bool ALSyntaxHighlighter::lexSome(S32 most)
{
    lex(static_cast<S32>(mLines.size()) - 1, most);
    return !mDocument || !mGrammar || mFirstDirty >= static_cast<S32>(mLines.size());
}

void ALSyntaxHighlighter::lex(S32 line, S32 most)
{
    mLastLexed = 0;
    if (!mDocument || !mGrammar || mLines.empty())
    {
        return;
    }
    line = llclamp(line, 0, static_cast<S32>(mLines.size()) - 1);
    if (mFirstDirty > line)
    {
        return;
    }
    // A zone only where there are lines to look at: most requests -- a row
    // drawn, the map's revisions, the folds' -- find them lexed already.
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    compactStates();
    std::vector<ALSyntaxToken> fresh;
    // Each line starts in the state the one before ends in, by number,
    // copied into the one kept for lexing only for a line lexed anew. A
    // line that lexes as it did costs nothing against `most`: the stop is
    // at the first that would be lexed past it.
    S32 i = mFirstDirty;
    for (; i <= line; ++i)
    {
        Line&     entry = mLines[i];
        const U32 start = (i == 0) ? mInitialState : mLines[i - 1].end;
        if (entry.valid && entry.start == start)
        {
            // Lexes as it did.
            continue;
        }
        if (mLastLexed >= most)
        {
            break;
        }
        mLexing = mStates[start];
        mGrammar->lexLine(mDocument->line(i), mLexing, fresh, *mWords);
        ++mLastLexed;
        if (!entry.lexed || fresh != entry.tokens)
        {
            entry.tokens.swap(fresh);
            ++entry.revision;
        }
        entry.start = start;
        entry.end   = intern(mLexing);
        entry.valid = true;
        entry.lexed = true;
    }
    mFirstDirty = i;
}

const std::vector<ALSyntaxToken>& ALSyntaxHighlighter::tokens(S32 line)
{
    ensure(line);
    // A line not lexed now -- there is no grammar, or no document -- has
    // none, whatever it was lexed to before.
    if (line < 0 || line >= static_cast<S32>(mLines.size()) || !mLines[line].valid)
    {
        return NO_TOKENS;
    }
    return mLines[line].tokens;
}

U32 ALSyntaxHighlighter::revision(S32 line)
{
    ensure(line);
    if (line < 0 || line >= static_cast<S32>(mLines.size()))
    {
        return 0;
    }
    return mLines[line].revision;
}

ALSyntaxState ALSyntaxHighlighter::startState(S32 line)
{
    ensure(line);
    if (!mGrammar || line < 0 || line >= static_cast<S32>(mLines.size()) || !mLines[line].valid || mLines[line].start >= mStates.size())
    {
        return mGrammar ? mGrammar->initialState() : ALSyntaxState();
    }
    return mStates[mLines[line].start];
}

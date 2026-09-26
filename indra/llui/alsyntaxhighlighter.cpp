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

ALSyntaxHighlighter::ALSyntaxHighlighter() = default;

ALSyntaxHighlighter::~ALSyntaxHighlighter() = default;

void ALSyntaxHighlighter::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    mGrammar = std::move(grammar);
    reset();
}

void ALSyntaxHighlighter::wordsChanged()
{
    reset();
}

void ALSyntaxHighlighter::attach(ALTextDocument* document)
{
    mConnection.release();
    mDocument = document;
    if (mDocument)
    {
        mConnection = mDocument->onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    }
    reset();
}

void ALSyntaxHighlighter::reset()
{
    // Revisions survive a reset: a line that lexes to the same tokens
    // afterwards keeps its number, and one that does not moves on.
    std::vector<U32> revisions;
    revisions.reserve(mLines.size());
    for (const Line& line : mLines)
    {
        revisions.push_back(line.revision);
    }
    mLines.assign(mDocument ? mDocument->lineCount() : 0, Line());
    for (size_t i = 0; i < mLines.size() && i < revisions.size(); ++i)
    {
        mLines[i].revision = revisions[i];
    }
    mFirstDirty = 0;
}

void ALSyntaxHighlighter::onEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // The lines the edit replaced go, and the lines it made come in, dirty;
    // what follows keeps its tokens until the state it starts in is seen
    // to have changed.
    const S32 first = llclamp(edit.range.begin.line, 0, static_cast<S32>(mLines.size()));
    const S32 last  = llclamp(edit.range.end.line, first, static_cast<S32>(mLines.size()) - 1);
    const S32 made  = 1 + edit.breaksInserted();
    if (first < static_cast<S32>(mLines.size()))
    {
        mLines.erase(mLines.begin() + first, mLines.begin() + last + 1);
    }
    mLines.insert(mLines.begin() + first, made, Line());
    if (mDocument)
    {
        mLines.resize(mDocument->lineCount());
    }
    mFirstDirty = llmin(mFirstDirty, first);
}

void ALSyntaxHighlighter::ensure(S32 line)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
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
    std::vector<ALSyntaxToken> fresh;
    for (S32 i = mFirstDirty; i <= line; ++i)
    {
        Line&               entry = mLines[i];
        const ALSyntaxState start = (i == 0) ? mGrammar->initialState() : mLines[i - 1].end;
        if (entry.valid && entry.start == start)
        {
            // Lexes as it did.
            continue;
        }
        ALSyntaxState state = start;
        mGrammar->lexLine(mDocument->line(i), state, fresh, mWords);
        ++mLastLexed;
        if (!entry.valid || fresh != entry.tokens)
        {
            entry.tokens.swap(fresh);
            ++entry.revision;
        }
        entry.start = start;
        entry.end   = std::move(state);
        entry.valid = true;
    }
    mFirstDirty = line + 1;
}

const std::vector<ALSyntaxToken>& ALSyntaxHighlighter::tokens(S32 line)
{
    ensure(line);
    if (line < 0 || line >= static_cast<S32>(mLines.size()))
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

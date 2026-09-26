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
    ALSyntaxWords&                         words() { return mWords; }
    void                                   wordsChanged();

    // The document to follow; null to follow none.
    void attach(ALTextDocument* document);

    // A line's tokens, lexed if need be, along with every line before it
    // that was not. Empty without a grammar or a document.
    const std::vector<ALSyntaxToken>& tokens(S32 line);
    U32                               revision(S32 line);

    // How many lines the last request had to lex, for a test that says an
    // edit re-lexes only what it must.
    S32 lastLexed() const { return mLastLexed; }

private:
    struct Line
    {
        ALSyntaxState              start;
        ALSyntaxState              end;
        std::vector<ALSyntaxToken> tokens;
        U32                        revision = 0;
        bool                       valid    = false;
    };

    void reset();
    void onEdit(const ALTextDocument::Edit& edit);
    void ensure(S32 line);

    ALTextDocument*                        mDocument = nullptr;
    boost::signals2::scoped_connection     mConnection;
    std::shared_ptr<const ALSyntaxGrammar> mGrammar;
    ALSyntaxWords                          mWords;
    ALLineTable<Line>                      mLines;
    S32                                    mFirstDirty = 0;
    S32                                    mLastLexed  = 0;
};

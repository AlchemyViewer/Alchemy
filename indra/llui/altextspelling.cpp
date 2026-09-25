/**
 * @file altextspelling.cpp
 * @brief A text view's spell check: the words a line lacks, and what the one at the caret might have been.
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

#include "altextview.h"

#include "llspellcheck.h"

namespace
{
    // A word the spell check has no business with: code rather than
    // prose -- a digit in it, an underscore, a capital after the first
    // letter -- or too short to be wrong.
    bool proseWord(std::string_view word)
    {
        if (word.size() < 3)
        {
            return false;
        }
        bool lower_seen = false;
        for (size_t i = 0; i < word.size(); ++i)
        {
            const char c = word[i];
            if ((c >= '0' && c <= '9') || c == '_')
            {
                return false;
            }
            if (c >= 'A' && c <= 'Z' && lower_seen)
            {
                return false;
            }
            if (c >= 'a' && c <= 'z')
            {
                lower_seen = true;
            }
        }
        return true;
    }
}

// --- the spell check -------------------------------------------------------------

void ALTextView::setSpellCheck(bool check)
{
    if (check == mSpellCheck)
    {
        return;
    }
    mSpellCheck = check;
    if (check && !mSpellSettingsConnection.connected())
    {
        mSpellSettingsConnection = LLSpellChecker::setSettingsChangeCallback([this]() { recheckSpelling(); });
    }
    recheckSpelling();
}

bool ALTextView::getSpellCheck() const
{
    return mSpellCheck && !mReadOnly && (mSpellChecker || LLSpellChecker::getUseSpellCheck());
}

void ALTextView::setSpellChecker(spell_checker_t checker, spell_suggester_t suggester)
{
    mSpellChecker   = std::move(checker);
    mSpellSuggester = std::move(suggester);
    recheckSpelling();
}

void ALTextView::recheckSpelling()
{
    mSpellLines.clear();
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
}

void ALTextView::checkLine(S32 line)
{
    if (static_cast<size_t>(line) >= mSpellLines.size())
    {
        mSpellLines.resize(static_cast<size_t>(mDocument.lineCount()));
    }
    SpellLine& checked = mSpellLines[static_cast<size_t>(line)];
    checked.valid      = true;
    checked.revision   = mHighlighter.revision(line);
    checked.words.clear();
    if (!getSpellCheck())
    {
        return;
    }
    const std::string& text = mDocument.line(line);
    // What is prose: everything, without a grammar or with one that
    // says so; comments and strings otherwise.
    auto check_stretch = [&](S32 begin, S32 end) {
        size_t at = static_cast<size_t>(begin);
        while (at < static_cast<size_t>(end))
        {
            const auto word = utf8str_next_word_range(text, at);
            if (word.first >= word.second || word.first >= static_cast<size_t>(end))
            {
                break;
            }
            const size_t word_end = llmin(word.second, static_cast<size_t>(end));
            at                    = word.second;
            const std::string_view piece(text.data() + word.first, word_end - word.first);
            if (!proseWord(piece))
            {
                continue;
            }
            const std::string spelled(piece);
            const bool        ok = mSpellChecker ? mSpellChecker(spelled) : LLSpellChecker::instance().checkSpelling(spelled);
            if (!ok)
            {
                checked.words.emplace_back(static_cast<S32>(word.first), static_cast<S32>(word_end));
            }
        }
    };
    const std::shared_ptr<const ALSyntaxGrammar> grammar = mHighlighter.grammar();
    if (!grammar || grammar->prose())
    {
        check_stretch(0, static_cast<S32>(text.size()));
        return;
    }
    // What names a file, a module or an address rather than saying
    // anything is the grammar's to say, as a path, which is never checked.
    for (const ALSyntaxToken& token : mHighlighter.tokens(line))
    {
        const bool prose = token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment || token.kind == ALSyntaxKind::String;
        if (prose)
        {
            check_stretch(token.begin, token.end);
        }
    }
}

const std::vector<std::pair<S32, S32>>& ALTextView::misspellings(S32 line)
{
    static const std::vector<std::pair<S32, S32>> none;
    if (line < 0 || line >= mDocument.lineCount())
    {
        return none;
    }
    // Checked again where the line's tokens changed since -- a comment
    // opened or closed on a line above makes it prose or code -- as well
    // as where the line itself did.
    if (static_cast<size_t>(line) >= mSpellLines.size() || !mSpellLines[static_cast<size_t>(line)].valid ||
        mSpellLines[static_cast<size_t>(line)].revision != mHighlighter.revision(line))
    {
        checkLine(line);
    }
    return mSpellLines[static_cast<size_t>(line)].words;
}

bool ALTextView::misspelledAt(const ALTextPos& pos, ALTextRange* word)
{
    for (const auto& [begin, end] : misspellings(pos.line))
    {
        if (begin <= pos.column && pos.column <= end)
        {
            if (word)
            {
                *word = ALTextRange(ALTextPos(pos.line, begin), ALTextPos(pos.line, end));
            }
            return true;
        }
    }
    return false;
}

void ALTextView::refreshSuggestions()
{
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    ALTextRange word;
    if (!getSpellCheck() || !misspelledAt(mCaret, &word))
    {
        return;
    }
    mSuggestedFor = word;
    if (mSpellSuggester)
    {
        mSpellSuggester(mDocument.text(word), mSuggestions);
    }
    else if (!mSpellChecker && LLSpellChecker::instanceExists())
    {
        LLSpellChecker::instance().getSuggestions(mDocument.text(word), mSuggestions);
    }
}

const std::string& ALTextView::getSuggestion(U32 index) const
{
    return index < mSuggestions.size() ? mSuggestions[index] : LLStringUtil::null;
}

U32 ALTextView::getSuggestionCount() const
{
    return static_cast<U32>(mSuggestions.size());
}

void ALTextView::replaceWithSuggestion(U32 index)
{
    if (index >= mSuggestions.size() || mSuggestedFor.empty() || mReadOnly)
    {
        return;
    }
    const ALTextRange word       = mSuggestedFor;
    const std::string suggestion = mSuggestions[index];
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    if (!edit(word, suggestion).nothing())
    {
        afterEdit();
    }
}

void ALTextView::addToDictionary()
{
    if (canAddToDictionary())
    {
        LLSpellChecker::instance().addToCustomDictionary(mDocument.text(mSuggestedFor));
        recheckSpelling();
    }
}

bool ALTextView::canAddToDictionary() const
{
    return getSpellCheck() && !mSuggestedFor.empty() && !mSpellChecker && LLSpellChecker::instanceExists();
}

void ALTextView::addToIgnore()
{
    if (canAddToIgnore())
    {
        LLSpellChecker::instance().addToIgnoreList(mDocument.text(mSuggestedFor));
        recheckSpelling();
    }
}

bool ALTextView::canAddToIgnore() const
{
    return canAddToDictionary();
}

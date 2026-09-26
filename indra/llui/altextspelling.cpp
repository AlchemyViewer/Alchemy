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

#include "altextspelling.h"

#include "alsyntaxhighlighter.h"
#include "llspellcheck.h"
#include "llstring.h"

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

void ALTextSpelling::setChecker(checker_t checker, suggester_t suggester)
{
    mChecker   = std::move(checker);
    mSuggester = std::move(suggester);
    recheck();
}

bool ALTextSpelling::available() const
{
    return mChecker || LLSpellChecker::getUseSpellCheck();
}

void ALTextSpelling::recheck()
{
    mLines.clear();
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
}

void ALTextSpelling::checkLine(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, S32 line, bool on)
{
    if (static_cast<size_t>(line) >= mLines.size())
    {
        mLines.resize(static_cast<size_t>(doc.lineCount()));
    }
    Line& checked    = mLines[static_cast<size_t>(line)];
    checked.valid    = true;
    checked.revision = highlighter.revision(line);
    checked.words.clear();
    if (!on)
    {
        return;
    }
    const std::string& text = doc.line(line);
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
            const bool        ok = mChecker ? mChecker(spelled) : LLSpellChecker::instance().checkSpelling(spelled);
            if (!ok)
            {
                checked.words.emplace_back(static_cast<S32>(word.first), static_cast<S32>(word_end));
            }
        }
    };
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter.grammar();
    if (!grammar || grammar->prose())
    {
        check_stretch(0, static_cast<S32>(text.size()));
        return;
    }
    // What names a file, a module or an address rather than saying
    // anything is the grammar's to say, as a path, which is never checked.
    for (const ALSyntaxToken& token : highlighter.tokens(line))
    {
        const bool prose = token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment || token.kind == ALSyntaxKind::String;
        if (prose)
        {
            check_stretch(token.begin, token.end);
        }
    }
}

const ALTextSpelling::words_t& ALTextSpelling::misspellings(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, S32 line, bool on)
{
    static const words_t none;
    if (line < 0 || line >= doc.lineCount())
    {
        return none;
    }
    // Checked again where the line's tokens changed since -- a comment
    // opened or closed on a line above makes it prose or code -- as well
    // as where the line itself did.
    if (static_cast<size_t>(line) >= mLines.size() || !mLines[static_cast<size_t>(line)].valid ||
        mLines[static_cast<size_t>(line)].revision != highlighter.revision(line))
    {
        checkLine(doc, highlighter, line, on);
    }
    return mLines[static_cast<size_t>(line)].words;
}

bool ALTextSpelling::misspelledAt(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, const ALTextPos& pos, bool on, ALTextRange* word)
{
    for (const auto& [begin, end] : misspellings(doc, highlighter, pos.line, on))
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

void ALTextSpelling::edited(const ALTextDocument::Edit& edit, S32 line_count)
{
    // The lines the edit touched are checked again when they are next
    // asked about; the ones below slide.
    const S32 count = static_cast<S32>(mLines.size());
    const S32 first = llclamp(edit.range.begin.line, 0, count);
    const S32 last  = llclamp(edit.range.end.line, first, count - 1);
    const S32 made  = 1 + edit.breaksInserted();
    if (first < count)
    {
        mLines.erase(mLines.begin() + first, mLines.begin() + last + 1);
    }
    mLines.insert(mLines.begin() + llmin(first, static_cast<S32>(mLines.size())), made, Line());
    mLines.resize(static_cast<size_t>(line_count));
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
}

void ALTextSpelling::suggestAt(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, const ALTextPos& pos, bool on)
{
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    ALTextRange word;
    if (!on || !misspelledAt(doc, highlighter, pos, on, &word))
    {
        return;
    }
    mSuggestedFor = word;
    if (mSuggester)
    {
        mSuggester(doc.text(word), mSuggestions);
    }
    else if (!mChecker && LLSpellChecker::instanceExists())
    {
        LLSpellChecker::instance().getSuggestions(doc.text(word), mSuggestions);
    }
}

std::optional<std::pair<ALTextRange, std::string>> ALTextSpelling::take(U32 index)
{
    std::optional<std::pair<ALTextRange, std::string>> taken;
    if (index < mSuggestions.size() && !mSuggestedFor.empty())
    {
        taken.emplace(mSuggestedFor, mSuggestions[index]);
    }
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    return taken;
}

bool ALTextSpelling::canTeach(bool on) const
{
    return on && !mSuggestedFor.empty() && !mChecker && LLSpellChecker::instanceExists();
}

void ALTextSpelling::addToDictionary(const ALTextDocument& doc)
{
    LLSpellChecker::instance().addToCustomDictionary(doc.text(mSuggestedFor));
    recheck();
}

void ALTextSpelling::addToIgnore(const ALTextDocument& doc)
{
    LLSpellChecker::instance().addToIgnoreList(doc.text(mSuggestedFor));
    recheck();
}

/**
 * @file alvimsearch.cpp
 * @brief Vim's searching: the last pattern, its matches kept, the search line lit, and offsets.
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

#include "alvimsearch.h"

#include "alvimhost.h"
#include "alvimexcommands.h"
#include "alvimkeymap.h"
#include "alvimtext.h"

#include <algorithm>
#include <cstdlib>

bool ALVimSearch::search(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool no_smartcase, const Offset& offset,
                         std::optional<ALTextPos> search_from)
{
    const ALTextPos                start = mVim.cursor(view);
    const std::optional<ALTextPos> to    = target(view, pattern, forward, count, no_smartcase, offset, search_from);
    if (!to)
    {
        return false;
    }
    if (!mVim.mOperator && *to != start)
    {
        mVim.noteJump(view, start);
    }
    mVim.moveTo(view, *to);
    return true;
}

std::optional<ALTextPos> ALVimSearch::target(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool no_smartcase, const Offset& offset,
                                             std::optional<ALTextPos> search_from)
{
    const ALTextDocument& d = view.document();
    ALTextSearchOptions   options;
    options.regex = true;
    // A pattern in vim's spelling; what * and # look for in its case as
    // ignorecase alone says.
    const Pattern pattern_in = patternOf(pattern, caseWithoutSmartCase(no_smartcase));
    options.caseSensitive    = pattern_in.caseSensitive;
    const Found&                    found_now = found(view, pattern_in, options);
    const std::vector<ALTextRange>& matches   = found_now.matches;
    if (!found_now.error.empty())
    {
        mVim.say(pattern_in.readError.empty() ? ALVimKeymap::said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", found_now.error } }) : found_now.error,
                 true);
        return std::nullopt;
    }
    if (matches.empty())
    {
        mVim.say(ALVimKeymap::said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        return std::nullopt;
    }
    const ALTextPos start = search_from ? *search_from : mVim.cursor(view);
    // From the last match where an offset left the caret by it: n after
    // /x/e goes on from that match, not from past its end.
    ALTextPos   from = offset.kind && !lastMatch.empty() && offsetFrom(d, lastMatch, offset) == start ? lastMatch.begin : start;
    ALTextRange hit;
    for (S32 n = 0; n < count; ++n)
    {
        const S32 index = ALTextSearch::nearest(matches, forward ? d.nextCluster(from) : from, forward);
        if (index < 0)
        {
            break;
        }
        hit  = matches[static_cast<size_t>(index)];
        from = hit.begin;
    }
    if (!hit.empty())
    {
        lastMatch = hit;
        from       = offsetFrom(d, hit, offset);
    }
    // Every match lit, as hlsearch has it, until :noh or the caret
    // leaves them.
    if (ALVimHost* host = view.vimHost(); host && !mVim.isVisual() && mVim.mShared->highlightSearch)
    {
        lightFound(*host);
    }
    return from;
}

ALVimSearch::Pattern ALVimSearch::patternOf(const std::string& vim, std::optional<bool> force_case) const
{
    return ALVimPattern::of(vim, mVim.mEx->lastReplacement, { mVim.mShared->ignoreCase, mVim.mShared->smartCase }, force_case);
}

std::optional<bool> ALVimSearch::caseWithoutSmartCase(bool no_smartcase) const
{
    return no_smartcase ? std::optional<bool>(!mVim.mShared->ignoreCase) : std::nullopt;
}

ALVimPattern::Places ALVimSearch::placesOf(const ALTextView& view) const
{
    // The last visual area, as a range: whole lines for a line-wise one,
    // the character under either end included otherwise; a block is the
    // lines between its ends and the columns between them.
    const ALTextDocument& d = view.document();
    ALVimPattern::Places  places;
    places.caret = view.caret();
    if (mVim.mVisualLast != ALVimKeymap::Mode::Normal)
    {
        const ALTextPos a = mVim.mVisualLastAnchor < mVim.mVisualLastCaret ? mVim.mVisualLastAnchor : mVim.mVisualLastCaret;
        const ALTextPos b = mVim.mVisualLastAnchor < mVim.mVisualLastCaret ? mVim.mVisualLastCaret : mVim.mVisualLastAnchor;
        places.visual     = true;
        places.visualRange = mVim.mVisualLast == ALVimKeymap::Mode::VisualLine ? ALTextRange(d.lineStart(a.line), d.lineEnd(b.line)) : ALTextRange(a, d.nextCluster(b));
        if (mVim.mVisualLast == ALVimKeymap::Mode::VisualBlock)
        {
            // Its columns as the reader counts them, as the block itself
            // has them.
            places.tabWidth   = view.getTabWidth();
            places.blockToEnd = mVim.mVisualLastToEnd;
            ALVimText::blockColumns(d, d.clamp(mVim.mVisualLastAnchor), d.clamp(mVim.mVisualLastCaret), places.tabWidth, places.blockLeft, places.blockRight);
            places.visualRange = ALTextRange(d.lineStart(a.line), d.lineEnd(b.line));
        }
    }
    return places;
}

std::vector<ALTextRange> ALVimSearch::matchesOf(ALTextView& view, const Pattern& pattern, ALTextSearchOptions options, const ALTextRange* scope, std::string& error,
                                                std::vector<ALTextPos>& wholes) const
{
    return pattern.matchesIn(view.document(), options, scope, placesOf(view), error, wholes);
}

const ALVimSearch::Found& ALVimSearch::found(ALTextView& view, const Pattern& pattern, const ALTextSearchOptions& options)
{
    const ALTextDocument&      d    = view.document();
    const ALTextSearchOptions& kept = mFound.options;
    const bool same = mFound.doc == &d && mFound.version == d.version() && mFound.pattern == pattern && kept.caseSensitive == options.caseSensitive &&
                      kept.wholeWord == options.wholeWord && kept.regex == options.regex && kept.preserveCase == options.preserveCase &&
                      kept.acrossLines == options.acrossLines && kept.limit == options.limit &&
                      kept.firstPerLine == options.firstPerLine;
    if (same)
    {
        return mFound;
    }
    mFound.matches = matchesOf(view, pattern, options, nullptr, mFound.error, mFound.wholes);
    // Kept where it is the text's alone to say; asked again otherwise.
    mFound.doc     = pattern.placed() ? nullptr : &d;
    mFound.version = d.version();
    mFound.pattern = pattern;
    mFound.options = options;
    mFound.lit     = nullptr;
    return mFound;
}

void ALVimSearch::lightFound(ALVimHost& host)
{
    // Lit already where the host holds as many as were lit there: what
    // puts them out -- :noh, the caret leaving them -- leaves none.
    if (mFound.lit == &host && host.layer(ALVimHost::Layer::Search).size() == mFound.matches.size())
    {
        return;
    }
    host.setLayer(ALVimHost::Layer::Search, mFound.matches);
    mFound.lit = &host;
}

std::optional<ALTextRange> ALVimSearch::matchNear(ALTextView& view, bool forward)
{
    if (pattern.empty())
    {
        mVim.say(ALVimKeymap::said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        return std::nullopt;
    }
    ALTextSearchOptions options;
    options.regex         = true;
    const Pattern parsed  = patternOf(pattern, caseWithoutSmartCase(noSmartCase));
    options.caseSensitive = parsed.caseSensitive;
    const Found&                    found_now = found(view, parsed, options);
    const std::vector<ALTextRange>& matches   = found_now.matches;
    if (!found_now.error.empty() || matches.empty())
    {
        mVim.say(ALVimKeymap::said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        return std::nullopt;
    }
    // gn: the one the caret is in, or the next; gN the one it is in, or
    // the one before.
    const ALTextPos at = mVim.cursor(view);
    if (forward)
    {
        for (const ALTextRange& match : matches)
        {
            if (at < match.end)
            {
                return match;
            }
        }
        return matches.front();
    }
    for (auto it = matches.rbegin(); it != matches.rend(); ++it)
    {
        if (!(at < it->begin))
        {
            return *it;
        }
    }
    return matches.back();
}

// static
void ALVimSearch::splitOffset(const std::string& line, llwchar kind, std::string& pattern, std::string& offset_text)
{
    // The pattern runs to the first `kind` not escaped; the rest is the
    // offset.
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '\\')
        {
            ++i;
            continue;
        }
        if (static_cast<llwchar>(static_cast<unsigned char>(line[i])) == kind)
        {
            pattern     = line.substr(0, i);
            offset_text = line.substr(i + 1);
            return;
        }
    }
    pattern = line;
    offset_text.clear();
}

// static
std::string ALVimSearch::backwardPattern(const std::string& typed)
{
    // As vim's skip_regexp() reads one: a [] collection -- \[ one under \V
    // -- passed over to its ], as skip_anyof() finds it; elsewhere \? made
    // a ?, and every other backslash kept with what follows it.
    const size_t size         = typed.size();
    const auto   collectionAt = [&typed, size](size_t p) {
        if (p < size && typed[p] == '^')
        {
            ++p;
        }
        if (p < size && (typed[p] == ']' || typed[p] == '-'))
        {
            ++p;
        }
        static constexpr std::string_view ESCAPED("]^-n\\rtebdoxuU");
        while (p < size && typed[p] != ']')
        {
            if (typed[p] == '-')
            {
                ++p;
                if (p < size && typed[p] != ']')
                {
                    ++p;
                }
            }
            else if (typed[p] == '\\' && p + 1 < size && ESCAPED.find(typed[p + 1]) != std::string_view::npos)
            {
                p += 2;
            }
            else if (typed[p] == '[' && p + 1 < size && (typed[p + 1] == ':' || typed[p + 1] == '=' || typed[p + 1] == '.'))
            {
                // [:alpha:] and the like, to their own close.
                const char   kind  = typed[p + 1];
                const size_t close = typed.find(std::string{ kind, ']' }, p + 2);
                p                  = close == std::string::npos ? p + 1 : close + 2;
            }
            else
            {
                ++p;
            }
        }
        return p;
    };
    std::string out;
    out.reserve(size);
    bool nomagic = false;
    for (size_t i = 0; i < size; ++i)
    {
        const bool bracket = typed[i] == '[' && !nomagic;
        if (bracket || (typed[i] == '\\' && i + 1 < size && typed[i + 1] == '[' && nomagic))
        {
            const size_t end = collectionAt(bracket ? i + 1 : i + 2);
            out.append(typed, i, end < size ? end + 1 - i : std::string::npos);
            if (end >= size)
            {
                break;
            }
            i = end;
            continue;
        }
        if (typed[i] == '\\' && i + 1 < size)
        {
            const char next = typed[++i];
            if (next != '?')
            {
                out += '\\';
            }
            out += next;
            if (next == 'v' || next == 'V')
            {
                nomagic = next == 'V';
            }
            continue;
        }
        out += typed[i];
    }
    return out;
}

// static
bool ALVimSearch::parseOffset(const std::string& text, Offset& out)
{
    // [+-]N lines, e[+-N] from the end, s[+-N] or b[+-N] from the start;
    // a sign alone is one.
    out = Offset{};
    if (text.empty())
    {
        return true;
    }
    size_t at   = 0;
    char   kind = 'l';
    if (text[0] == 'e' || text[0] == 's' || text[0] == 'b')
    {
        kind = text[0] == 'e' ? 'e' : 's';
        at   = 1;
    }
    S32 sign = 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    const bool signed_alone = at == text.size() && at > 0 && (text[at - 1] == '+' || text[at - 1] == '-');
    S32        amount       = 0;
    for (; at < text.size(); ++at)
    {
        if (text[at] < '0' || text[at] > '9')
        {
            return false;
        }
        amount = llmin(amount * 10 + (text[at] - '0'), 100000);
    }
    out.kind   = kind;
    out.amount = sign * (signed_alone ? 1 : amount);
    return true;
}

ALTextPos ALVimSearch::offsetFrom(const ALTextDocument& d, const ALTextRange& match, const Offset& offset) const
{
    if (offset.kind == 'l')
    {
        return ALTextPos(llclamp(match.begin.line + offset.amount, 0, d.lineCount() - 1), 0);
    }
    ALTextPos at = offset.kind == 'e' ? d.prevCluster(match.end) : match.begin;
    for (S32 n = 0; n < std::abs(offset.amount); ++n)
    {
        at = offset.amount > 0 ? d.nextCluster(at) : d.prevCluster(at);
    }
    return offset.kind ? at : match.begin;
}

void ALVimSearch::incrementalSearch(ALTextView& view)
{
    ALVimHost* host = view.vimHost();
    if (!mVim.mShared->incrementalSearch || !host)
    {
        return;
    }
    std::string pattern;
    std::string offset_text;
    splitOffset(mVim.mCommandLine.line, mVim.mCommandLine.kind, pattern, offset_text);
    // A key that left the pattern as it was, in the same text: nothing
    // more to light or scroll to.
    if (mIncrementalShown && pattern == mIncrementalPattern && mVim.mCommandLine.kind == mIncrementalKind && view.document().version() == mIncrementalVersion)
    {
        return;
    }
    mIncrementalShown   = true;
    mIncrementalPattern = pattern;
    mIncrementalKind    = mVim.mCommandLine.kind;
    mIncrementalVersion = view.document().version();
    if (pattern.empty())
    {
        host->clearLayer(ALVimHost::Layer::Search);
        view.scrollToCaret();
        return;
    }
    // What is typed so far may not be a pattern yet: nothing lit then. After
    // ?, \? is the ? itself (backwardPattern).
    ALTextSearchOptions options;
    options.regex                            = true;
    const Pattern parsed                     = patternOf(mVim.mCommandLine.kind == '?' ? backwardPattern(pattern) : pattern);
    options.caseSensitive                    = parsed.caseSensitive;
    const Found&                    found_now = found(view, parsed, options);
    const std::vector<ALTextRange>& matches   = found_now.matches;
    if (!found_now.error.empty() || matches.empty())
    {
        host->clearLayer(ALVimHost::Layer::Search);
        view.scrollToCaret();
        return;
    }
    const bool forward = mVim.mCommandLine.kind == '/';
    const S32  index   = ALTextSearch::nearest(matches, forward ? view.document().nextCluster(mVim.cursor(view)) : mVim.cursor(view), forward);
    const ALTextRange next = matches[static_cast<size_t>(index < 0 ? 0 : index)];
    // Every match lit where hlsearch has them, else the next alone.
    if (mVim.mShared->highlightSearch)
    {
        lightFound(*host);
    }
    else
    {
        host->setLayer(ALVimHost::Layer::Search, { next });
        mFound.lit = nullptr;
    }
    view.scrollToShow(next.begin);
}

void ALVimSearch::endIncremental(ALTextView& view)
{
    if (!mIncrementalShown)
    {
        return;
    }
    mIncrementalShown = false;
    if (ALVimHost* host = view.vimHost())
    {
        host->clearLayer(ALVimHost::Layer::Search);
    }
    view.scrollToCaret();
}

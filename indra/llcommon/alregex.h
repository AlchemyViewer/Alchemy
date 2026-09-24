/**
 * @file alregex.h
 * @brief A regular expression compiled once and run by RE2.
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

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The viewer's regular expressions: a pattern compiled once, matched in time
// linear in the text, never throwing, and safe to match from any thread. RE2
// runs them; nothing here names it, so what includes this includes none of
// RE2 or abseil.
//
// A pattern reads as Boost.Regex's Perl syntax read it over a std::string,
// so what was written for that reads the same here: the text is bytes, `.`
// takes a line break, and `^` and `$` are at every line's ends as well as
// the text's. The flags turn each of those off. What RE2 does not have --
// lookahead and lookbehind, backreferences, \K -- does not compile: ok() is
// false, error() says why, and a regex that did not compile matches nothing.
//
// The script editor's find and its vim search stay on Boost.Regex, whose
// Perl syntax, lookaround and all, is theirs to offer.

// Where a regex matched, and each of its groups: offsets into the text it
// was run over, and views of it, which are good for as long as that text is.
class LL_COMMON_API ALRegexMatch
{
public:
    static constexpr size_t npos = std::string_view::npos;

    // The groups asked for, the whole match first, after a match; none
    // before one.
    size_t size() const { return mCount; }
    // Whether group i took part in the match. Group 0 is the whole of it.
    bool matched(size_t i = 0) const { return i < mCount && group(i).data() != nullptr; }
    size_t begin(size_t i = 0) const { return matched(i) ? static_cast<size_t>(group(i).data() - mText.data()) : npos; }
    size_t end(size_t i = 0) const { return matched(i) ? begin(i) + group(i).size() : npos; }
    size_t length(size_t i = 0) const { return matched(i) ? group(i).size() : 0; }
    std::string_view view(size_t i = 0) const { return matched(i) ? group(i) : std::string_view(); }
    std::string str(size_t i = 0) const { return std::string(view(i)); }

private:
    friend class ALRegex;

    // As many groups as the viewer's patterns have, and one over, held in
    // place; a pattern with more asks for the rest of the room.
    static constexpr size_t INLINE_GROUPS = 24;

    const std::string_view& group(size_t i) const { return mCount <= INLINE_GROUPS ? mInline[i] : mHeap[i]; }

    std::string_view              mText;
    std::string_view              mInline[INLINE_GROUPS];
    std::vector<std::string_view> mHeap;
    size_t                        mCount = 0;
};

class LL_COMMON_API ALRegex
{
public:
    enum Flags : U32
    {
        NONE         = 0,
        // Letters match either case.
        ICASE        = 1 << 0,
        // `.` stops at a line break.
        NO_DOT_NL    = 1 << 1,
        // `^` and `$` are the text's ends only.
        NO_MULTILINE = 1 << 2,
        // The text is UTF-8, and `.` and a negated class take a code point
        // rather than a byte. A byte that is no part of one then matches
        // none of them.
        UTF8         = 1 << 3,
    };

    // Compiles nothing: ok() is false, and it matches nothing.
    ALRegex() = default;
    explicit ALRegex(std::string_view pattern, U32 flags = NONE);

    // Whether the pattern compiled, and if not, why.
    bool               ok() const;
    const std::string& error() const;
    // The pattern as it was given.
    const std::string& pattern() const;
    // How many capturing groups it has.
    S32                groups() const;

    // Whether the whole text matches.
    bool match(std::string_view text, ALRegexMatch* out = nullptr) const;
    // The first match that starts at or after `start`, or with anchor_start
    // one that starts there or none. What comes before `start` is still the
    // text's, so that \b and ^ read what is on either side of it. Only the
    // first `wanted` groups are filled in, all where it is negative; with
    // no match to fill in, the text is only looked through for one, which
    // is the quickest of all.
    bool search(std::string_view text, ALRegexMatch* out = nullptr, size_t start = 0, bool anchor_start = false,
                S32 wanted = -1) const;

    // Each match in turn, none overlapping, until the text ends or `fn`
    // returns false. How many there were.
    template <typename F>
    size_t forEach(std::string_view text, F&& fn, S32 wanted = -1) const;
    // Each match put in the place of what `fn` makes of it. How many there were.
    template <typename F>
    size_t replaceEach(std::string& text, F&& fn) const;
    // Each match put in the place of `literal`, taken as it is written.
    // How many there were.
    size_t replaceAll(std::string& text, std::string_view literal) const;

    // The bytes a match can begin with, wherever it is looked for: every
    // match of one or more bytes starts with one from lo to hi. False where
    // that is any byte, or cannot be said.
    bool firstByteRange(U8& lo, U8& hi) const;

    // The text as a pattern that matches it and nothing else.
    static std::string escape(std::string_view text);

private:
    struct Impl;
    std::shared_ptr<const Impl> mImpl;
};

LL_COMMON_API std::ostream& operator<<(std::ostream& out, const ALRegex& regex);

template <typename F>
size_t ALRegex::forEach(std::string_view text, F&& fn, S32 wanted) const
{
    ALRegexMatch found;
    size_t       count = 0;
    size_t       at    = 0;
    while (at <= text.size() && search(text, &found, at, false, wanted))
    {
        ++count;
        if (!fn(std::as_const(found)))
        {
            break;
        }
        // An empty match: on by a byte, so as not to find it again.
        at = found.end() + (found.length() == 0 ? 1 : 0);
    }
    return count;
}

template <typename F>
size_t ALRegex::replaceEach(std::string& text, F&& fn) const
{
    std::string  out;
    ALRegexMatch found;
    size_t       count = 0;
    // Where the text not yet copied over begins.
    size_t       kept  = 0;
    while (kept <= text.size() && search(text, &found, kept))
    {
        if (count++ == 0)
        {
            out.reserve(text.size());
        }
        out.append(text, kept, found.begin() - kept);
        out += fn(std::as_const(found));
        kept = found.end();
        if (found.length() == 0)
        {
            // An empty match: the byte after it is copied, and the search
            // goes on past it.
            if (kept == text.size())
            {
                break;
            }
            out += text[kept++];
        }
    }
    if (count > 0)
    {
        if (kept < text.size())
        {
            out.append(text, kept, std::string::npos);
        }
        text.swap(out);
    }
    return count;
}

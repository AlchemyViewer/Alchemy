/**
 * @file alregex.cpp
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

#include "linden_common.h"

#include "alregex.h"

#include <re2/re2.h>
#include <re2/set.h>

#include <algorithm>
#include <ostream>
#include <type_traits>

struct ALRegex::Impl
{
    Impl(std::string_view source, U32 flags, const std::string& program, const re2::RE2::Options& options)
    :   source(source),
        flags(flags),
        re(program, options)
    {
    }

    std::string source;
    U32         flags;
    re2::RE2    re;
};

namespace
{
    const std::string NO_PATTERN;
    const std::string NOT_COMPILED("no pattern was given");

    // What the patterns are handed to RE2 as. Boost.Regex's Perl syntax is
    // many-lined unless told otherwise, and RE2's is not, so the flag is
    // said at the head of the pattern.
    std::string programOf(std::string_view pattern, U32 flags)
    {
        std::string program;
        program.reserve(pattern.size() + 4);
        if (!(flags & ALRegex::NO_MULTILINE))
        {
            program += "(?m)";
        }
        program += pattern;
        return program;
    }

    re2::RE2::Options optionsOf(U32 flags)
    {
        re2::RE2::Options options;
        options.set_encoding((flags & ALRegex::UTF8) ? re2::RE2::Options::EncodingUTF8 : re2::RE2::Options::EncodingLatin1);
        options.set_dot_nl(!(flags & ALRegex::NO_DOT_NL));
        options.set_case_sensitive(!(flags & ALRegex::ICASE));
        // A pattern that does not compile says so through error(), and
        // whoever gave it says so where it matters; RE2 is not to write to
        // stderr besides.
        options.set_log_errors(false);
        return options;
    }

    // RE2 takes abseil's string_view, which is the standard one wherever
    // abseil is built for C++17 or later, as it is for every target; the
    // groups are filled in where they are kept.
    static_assert(std::is_same_v<absl::string_view, std::string_view>, "abseil's string_view is the standard one");

    bool run(const re2::RE2& re, std::string_view text, size_t start, size_t end, re2::RE2::Anchor anchor, std::string_view* groups,
             int count)
    {
        return re.Match(text, start, end, anchor, groups, count);
    }

    enum class Firsts : U8
    {
        // No match of a byte or more.
        None,
        Some,
        // Any byte, or none can be said.
        Any
    };

    // The first bytes of the matches of `re` past `lead`, which every match
    // of it begins with, from the least and the most a match can be.
    Firsts firstsAfter(const re2::RE2& re, std::string_view lead, U8& lo, U8& hi)
    {
        std::string least, most;
        if (!re.PossibleMatchRange(&least, &most, static_cast<int>(lead.size()) + 1))
        {
            return Firsts::Any;
        }
        if (most.size() <= lead.size())
        {
            // Nothing past the lead matches; or the most a match can be was
            // rounded up past what it begins with, and it could be anything.
            return most == lead ? Firsts::None : Firsts::Any;
        }
        lo = least.size() > lead.size() ? static_cast<U8>(least[lead.size()]) : 0;
        hi = static_cast<U8>(most[lead.size()]);
        return Firsts::Some;
    }
}

ALRegex::ALRegex(std::string_view pattern, U32 flags)
:   mImpl(std::make_shared<const Impl>(pattern, flags, programOf(pattern, flags), optionsOf(flags)))
{
}

bool ALRegex::ok() const
{
    return mImpl && mImpl->re.ok();
}

const std::string& ALRegex::error() const
{
    return mImpl ? mImpl->re.error() : NOT_COMPILED;
}

const std::string& ALRegex::pattern() const
{
    return mImpl ? mImpl->source : NO_PATTERN;
}

U32 ALRegex::flags() const
{
    return mImpl ? mImpl->flags : NONE;
}

S32 ALRegex::groups() const
{
    return ok() ? mImpl->re.NumberOfCapturingGroups() : 0;
}

bool ALRegex::match(std::string_view text, ALRegexMatch* out) const
{
    if (out)
    {
        out->mCount = 0;
    }
    if (!ok())
    {
        return false;
    }
    if (!text.data())
    {
        text = std::string_view("", 0);
    }
    if (!out)
    {
        return run(mImpl->re, text, 0, text.size(), re2::RE2::ANCHOR_BOTH, nullptr, 0);
    }
    const size_t     count  = static_cast<size_t>(mImpl->re.NumberOfCapturingGroups()) + 1;
    std::string_view* groups = count <= ALRegexMatch::INLINE_GROUPS ? out->mInline : (out->mHeap.resize(count), out->mHeap.data());
    if (!run(mImpl->re, text, 0, text.size(), re2::RE2::ANCHOR_BOTH, groups, static_cast<int>(count)))
    {
        return false;
    }
    out->mText  = text;
    out->mCount = count;
    return true;
}

bool ALRegex::search(std::string_view text, ALRegexMatch* out, size_t start, bool anchor_start, S32 wanted) const
{
    if (out)
    {
        out->mCount = 0;
    }
    if (!ok() || start > text.size())
    {
        return false;
    }
    if (!text.data())
    {
        text = std::string_view("", 0);
    }
    const re2::RE2::Anchor anchor = anchor_start ? re2::RE2::ANCHOR_START : re2::RE2::UNANCHORED;
    if (!out)
    {
        return run(mImpl->re, text, start, text.size(), anchor, nullptr, 0);
    }
    const size_t available = static_cast<size_t>(mImpl->re.NumberOfCapturingGroups()) + 1;
    const size_t count     = wanted < 0 ? available : std::min(available, static_cast<size_t>(wanted) + 1);
    std::string_view* groups = count <= ALRegexMatch::INLINE_GROUPS ? out->mInline : (out->mHeap.resize(count), out->mHeap.data());
    if (!run(mImpl->re, text, start, text.size(), anchor, groups, static_cast<int>(count)))
    {
        return false;
    }
    out->mText  = text;
    out->mCount = count;
    return true;
}

size_t ALRegex::replaceAll(std::string& text, std::string_view literal) const
{
    if (!ok())
    {
        return 0;
    }
    // RE2's rewrites read \0 to \9 for groups and \\ for a backslash, and
    // nothing else: the literal's backslashes are doubled and that is all.
    std::string rewrite;
    rewrite.reserve(literal.size());
    for (const char c : literal)
    {
        if (c == '\\')
        {
            rewrite += '\\';
        }
        rewrite += c;
    }
    return static_cast<size_t>(re2::RE2::GlobalReplace(&text, mImpl->re, rewrite));
}

bool ALRegex::firstByteRange(U8& lo, U8& hi) const
{
    if (!ok())
    {
        return false;
    }
    // RE2 answers for a match at the text's start. After a byte, one can
    // begin elsewhere only where \b or \B reads the other way, which is
    // after a word byte; so the answer is that one and the one after `_`
    // together. (^ holds at the start and after no byte in a line, so the
    // start's answer covers everything it allows.)
    U8           start_lo = 0, start_hi = 0, word_lo = 0, word_hi = 0;
    const Firsts at_start = firstsAfter(mImpl->re, std::string_view(), start_lo, start_hi);
    if (at_start == Firsts::Any)
    {
        return false;
    }
    const re2::RE2 after_word("_(?:" + mImpl->re.pattern() + ")", mImpl->re.options());
    const Firsts   past_word = after_word.ok() ? firstsAfter(after_word, "_", word_lo, word_hi) : Firsts::Any;
    if (past_word == Firsts::Any || (at_start == Firsts::None && past_word == Firsts::None))
    {
        return false;
    }
    if (at_start == Firsts::None)
    {
        lo = word_lo;
        hi = word_hi;
    }
    else if (past_word == Firsts::None)
    {
        lo = start_lo;
        hi = start_hi;
    }
    else
    {
        lo = std::min(start_lo, word_lo);
        hi = std::max(start_hi, word_hi);
    }
    return true;
}

// static
std::string ALRegex::escape(std::string_view text)
{
    return re2::RE2::QuoteMeta(absl::string_view(text.data(), text.size()));
}

std::ostream& operator<<(std::ostream& out, const ALRegex& regex)
{
    return out << regex.pattern();
}

// --- ALRegexSet --------------------------------------------------------------

struct ALRegexSet::Impl
{
    explicit Impl(U32 flags)
    :   flags(flags),
        set(optionsOf(flags), re2::RE2::UNANCHORED)
    {
    }

    U32           flags;
    re2::RE2::Set set;
    size_t        count    = 0;
    bool          compiled = false;
};

ALRegexSet::ALRegexSet(U32 flags)
:   mImpl(std::make_unique<Impl>(flags))
{
}

ALRegexSet::~ALRegexSet() = default;
ALRegexSet::ALRegexSet(ALRegexSet&& other) noexcept = default;
ALRegexSet& ALRegexSet::operator=(ALRegexSet&& other) noexcept = default;

S32 ALRegexSet::add(std::string_view pattern, std::string* error)
{
    if (!mImpl || mImpl->compiled)
    {
        if (error)
        {
            *error = "the set is compiled";
        }
        return -1;
    }
    std::string why;
    const int   index = mImpl->set.Add(programOf(pattern, mImpl->flags), &why);
    if (index < 0)
    {
        if (error)
        {
            *error = why;
        }
        return -1;
    }
    ++mImpl->count;
    return index;
}

bool ALRegexSet::compile()
{
    if (!mImpl || mImpl->compiled || mImpl->count == 0)
    {
        return false;
    }
    mImpl->compiled = mImpl->set.Compile();
    return mImpl->compiled;
}

bool ALRegexSet::ok() const
{
    return mImpl && mImpl->compiled;
}

U32 ALRegexSet::flags() const
{
    return mImpl ? mImpl->flags : ALRegex::NONE;
}

size_t ALRegexSet::size() const
{
    return mImpl ? mImpl->count : 0;
}

bool ALRegexSet::match(std::string_view text, std::vector<S32>& hits) const
{
    static_assert(std::is_same_v<S32, int>, "RE2 gives the set's hits as ints");
    hits.clear();
    if (!ok())
    {
        return false;
    }
    // A miss says why: none matched, or the pass could not be made.
    re2::RE2::Set::ErrorInfo info{ re2::RE2::Set::kNoError };
    if (!mImpl->set.Match(text.data() ? text : std::string_view("", 0), &hits, &info) && info.kind != re2::RE2::Set::kNoError)
    {
        hits.clear();
        return false;
    }
    return true;
}

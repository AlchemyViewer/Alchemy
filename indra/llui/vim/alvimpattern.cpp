/**
 * @file alvimpattern.cpp
 * @brief Vim's patterns and replacements as the search engine reads them, and where their matches may stand.
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

#include "alvimpattern.h"
#include "alvimtext.h"

#include "altextchars.h"
#include "llstring.h"

#include <algorithm>

// static
ALVimPattern ALVimPattern::of(const std::string& vim, const std::string& last_replacement, const Case& case_rules, std::optional<bool> force_case)
{
    // Vim's magic spelling to Perl's: with a backslash, ( ) | + ? = { }
    // < > are the engine's own ( ) | + ? ? { } \b \b, and without one
    // they are themselves; \v makes what follows very magic, where they
    // are the engine's own bare and themselves with a backslash; \V very
    // nomagic, where only ^ $ and the backslash items are special. \zs
    // is \K; \ze looks ahead at the rest; \{-} is *?; the classes \a \l
    // \u \x \o \h \i \k are brackets; \c and \C say how case is matched;
    // a bracket expression is copied through as it stands. A magic ^ is a
    // line's start only first in a branch, a $ its end only last in one,
    // and either is itself anywhere else.
    ALVimPattern        out;
    std::optional<bool> case_in_pattern;
    enum class Magic : U8
    {
        Magic,
        Very,
        None
    };
    Magic magic    = Magic::Magic;
    bool  looking  = false;
    bool  zs_seen  = false;
    // Where each \K was put, for the pattern without them.
    std::vector<size_t> k_at;
    auto  literal  = [&](char c) {
        if (alRegexSpecial(c))
        {
            out.regex += '\\';
        }
        out.regex += c;
    };
    // %[abc]: a, ab or abc -- each item optional and the next only with
    // it. `at` is the [; the index of the ] comes back, or npos.
    auto optionalSequence = [&](size_t at) {
        const size_t close = vim.find(']', at + 1);
        if (close == std::string::npos)
        {
            return std::string::npos;
        }
        S32 opened = 0;
        for (size_t k = at + 1; k < close; ++k)
        {
            out.regex += "(?:";
            if (vim[k] == '\\' && k + 1 < close)
            {
                out.regex += '\\';
                out.regex += vim[++k];
            }
            else
            {
                literal(vim[k]);
            }
            ++opened;
        }
        for (S32 k = 0; k < opened; ++k)
        {
            out.regex += ")?";
        }
        return close;
    };
    // What a count's braces hold as the engine's repeat: n,m, n and n, as
    // they are; ,m from none, which the engine would take for the braces
    // themselves; nothing, or a comma alone, any number, as *.
    auto repeatOf = [](const std::string& body) {
        if (body.empty() || body == ",")
        {
            return std::string("*");
        }
        return body[0] == ',' ? "{0" + body + "}" : "{" + body + "}";
    };
    // Whether a magic ^ read now is the line's start, as vim has it: at
    // the pattern's start, and after \( \%( \| \& or \n, a \c, a \v and
    // the like between them putting in nothing; anywhere else it is
    // itself.
    bool at_start = true;
    // Whether a magic $ before `k` is the line's end, as vim has it: at the
    // pattern's end, and before \| \) \& or \n -- or very magic's | ) and &
    // where a \v comes between -- a \c, a \v and the like between them
    // putting in nothing; anywhere else it is itself.
    auto endsAt = [&vim](size_t k) {
        constexpr std::string_view SWITCHES("cCmMvVZ");
        constexpr std::string_view ENDS("|&)");
        bool                       very = false;
        while (k + 1 < vim.size() && vim[k] == '\\' && SWITCHES.find(vim[k + 1]) != std::string_view::npos)
        {
            if (vim[k + 1] == 'v')
            {
                very = true;
            }
            else if (vim[k + 1] == 'm' || vim[k + 1] == 'M' || vim[k + 1] == 'V')
            {
                very = false;
            }
            k += 2;
        }
        if (k >= vim.size())
        {
            return true;
        }
        if (vim[k] == '\\')
        {
            return k + 1 < vim.size() && (ENDS.find(vim[k + 1]) != std::string_view::npos || vim[k + 1] == 'n');
        }
        return very && ENDS.find(vim[k]) != std::string_view::npos;
    };
    // How deep in the engine's brackets the output is, so that a \zs at
    // the top can split the pattern into groups.
    S32   depth    = 0;
    for (size_t i = 0; i < vim.size(); ++i)
    {
        const char c          = vim[i];
        const bool start_here = at_start;
        at_start              = false;
        if (c == '\\' && i + 1 < vim.size())
        {
            const char n = vim[++i];
            switch (n)
            {
                case 'v': magic = Magic::Very; at_start = start_here; continue;
                case 'm': magic = Magic::Magic; at_start = start_here; continue;
                case 'M':
                case 'V': magic = Magic::None; at_start = start_here; continue;
                case 'c': case_in_pattern = false; at_start = start_here; continue;
                case 'C': case_in_pattern = true; at_start = start_here; continue;
                case '(':
                    out.regex += magic == Magic::Very ? "\\(" : "(";
                    depth += magic == Magic::Very ? 0 : 1;
                    at_start = magic != Magic::Very;
                    continue;
                case ')':
                    out.regex += magic == Magic::Very ? "\\)" : ")";
                    depth -= magic == Magic::Very ? 0 : 1;
                    continue;
                case '|':
                    out.regex += magic == Magic::Very ? "\\|" : "|";
                    at_start = magic != Magic::Very;
                    continue;
                case '+': out.regex += magic == Magic::Very ? "\\+" : "+"; continue;
                case '?':
                case '=': out.regex += magic == Magic::Very ? std::string(1, n) : "?"; continue;
                case '<':
                case '>': out.regex += magic == Magic::Very ? std::string(1, n) : "\\b"; continue;
                case '{':
                {
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\{";
                        continue;
                    }
                    // \{n,m}, \{-n,m} lazy, \{} for *, \{-} for *?; the
                    // closing brace may carry a backslash of its own.
                    size_t close = vim.find('}', i + 1);
                    if (close == std::string::npos)
                    {
                        out.regex += "\\{";
                        continue;
                    }
                    std::string body = vim.substr(i + 1, close - i - 1);
                    if (!body.empty() && body.back() == '\\')
                    {
                        body.pop_back();
                    }
                    const bool lazy = !body.empty() && body[0] == '-';
                    if (lazy)
                    {
                        body.erase(0, 1);
                    }
                    out.regex += repeatOf(body);
                    if (lazy)
                    {
                        out.regex += "?";
                    }
                    i = close;
                    continue;
                }
                case 'z':
                    if (i + 1 < vim.size() && vim[i + 1] == 's')
                    {
                        // The engine's \K, which keeps the groups counted
                        // as they were written and & the match alone; and
                        // a note of where it went, so that the pattern
                        // without it can be had. The last one counts.
                        k_at.push_back(out.regex.size());
                        out.regex += "\\K";
                        zs_seen = true;
                        ++i;
                        continue;
                    }
                    if (i + 1 < vim.size() && vim[i + 1] == 'e')
                    {
                        out.regex += "(?=";
                        looking = true;
                        ++i;
                        continue;
                    }
                    literal('z');
                    continue;
                case '%':
                {
                    if (i + 1 < vim.size() && vim[i + 1] == '(')
                    {
                        out.regex += "(?:";
                        ++depth;
                        ++i;
                        at_start = true;
                        continue;
                    }
                    if (i + 1 < vim.size() && vim[i + 1] == '[')
                    {
                        const size_t close = optionalSequence(i + 1);
                        if (close == std::string::npos)
                        {
                            literal('%');
                            continue;
                        }
                        i = close;
                        continue;
                    }
                    // A character by its code: \%d123 \%x7b \%o173 \%u007b.
                    if (i + 1 < vim.size() && (vim[i + 1] == 'd' || vim[i + 1] == 'x' || vim[i + 1] == 'o' || vim[i + 1] == 'u' || vim[i + 1] == 'U'))
                    {
                        const char how   = vim[i + 1];
                        const int  radix = how == 'd' ? 10 : how == 'o' ? 8 : 16;
                        size_t     k     = i + 2;
                        U32        code  = 0;
                        S32        taken = 0;
                        while (k < vim.size() && taken < (how == 'U' ? 8 : how == 'u' ? 4 : how == 'x' ? 2 : 12))
                        {
                            const char h = vim[k];
                            int        v = -1;
                            if (h >= '0' && h <= '9') v = h - '0';
                            else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                            if (v < 0 || v >= radix)
                            {
                                break;
                            }
                            code = code * static_cast<U32>(radix) + static_cast<U32>(v);
                            ++k;
                            ++taken;
                        }
                        if (taken > 0)
                        {
                            // As the bytes it is, each escaped where it is
                            // anything to the engine.
                            for (const char b : utf8str_from_cp(static_cast<llwchar>(code)))
                            {
                                literal(b);
                            }
                            i = k - 1;
                            continue;
                        }
                    }
                    // The file's ends: \%^ and \%$.
                    if (i + 1 < vim.size() && (vim[i + 1] == '^' || vim[i + 1] == '$'))
                    {
                        Where place;
                        place.kind       = vim[i + 1] == '^' ? Where::Kind::FileStart : Where::Kind::FileEnd;
                        place.afterStart = zs_seen;
                        out.where.push_back(place);
                        ++i;
                        continue;
                    }
                    // The places: \%V \%# \%23l \%<23l \%>23l \%23c \%23v.
                    size_t k    = i + 1;
                    S32    side = 0;
                    if (k < vim.size() && (vim[k] == '<' || vim[k] == '>'))
                    {
                        side = vim[k] == '<' ? -1 : 1;
                        ++k;
                    }
                    S32 number = 0;
                    bool digits = false;
                    while (k < vim.size() && vim[k] >= '0' && vim[k] <= '9')
                    {
                        number = number * 10 + (vim[k++] - '0');
                        digits = true;
                    }
                    if (k < vim.size())
                    {
                        Where place;
                        place.side   = side;
                        place.number = number;
                        bool known   = true;
                        switch (vim[k])
                        {
                            case 'V': place.kind = Where::Kind::Visual; known = !digits; break;
                            case '#': place.kind = Where::Kind::Caret; known = !digits; break;
                            case 'l': place.kind = Where::Kind::Line; known = digits; break;
                            case 'c':
                            case 'v': place.kind = Where::Kind::Column; known = digits; break;
                            default: known = false; break;
                        }
                        if (known)
                        {
                            place.afterStart = zs_seen;
                            out.where.push_back(place);
                            i = k;
                            continue;
                        }
                    }
                    literal('%');
                    continue;
                }
                case '_':
                    // \_s and the like: the class with a line break in it,
                    // which the search must then be let cross.
                    if (i + 1 < vim.size())
                    {
                        const char cls  = vim[++i];
                        out.acrossLines = true;
                        if (cls == '.')
                        {
                            out.regex += "[\\s\\S]";
                        }
                        else if (cls == '[')
                        {
                            // \_[abc]: the bracket expression with a line
                            // break in it.
                            const size_t close = vim.find(']', i + 1);
                            if (close == std::string::npos)
                            {
                                out.regex += "\\[";
                            }
                            else
                            {
                                out.regex += "[\\n" + vim.substr(i + 1, close - i - 1) + "]";
                                i = close;
                            }
                        }
                        else
                        {
                            out.regex += "(?:\\" + std::string(1, cls) + "|\\n)";
                        }
                        continue;
                    }
                    literal('_');
                    continue;
                case 'a': out.regex += "[A-Za-z]"; continue;
                case 'A': out.regex += "[^A-Za-z]"; continue;
                case 'l': out.regex += "[a-z]"; continue;
                case 'L': out.regex += "[^a-z]"; continue;
                case 'u': out.regex += "[A-Z]"; continue;
                case 'U': out.regex += "[^A-Z]"; continue;
                case 'x': out.regex += "[0-9A-Fa-f]"; continue;
                case 'X': out.regex += "[^0-9A-Fa-f]"; continue;
                case 'o': out.regex += "[0-7]"; continue;
                case 'O': out.regex += "[^0-7]"; continue;
                case 'h': out.regex += "[A-Za-z_]"; continue;
                case 'H': out.regex += "[^A-Za-z_]"; continue;
                case 'i':
                case 'k': out.regex += "[A-Za-z0-9_]"; continue;
                case 'I':
                case 'K': out.regex += "[A-Za-z_]"; continue;
                case 'e': out.regex += "\\x1b"; continue;
                default:
                    // \s \S \d \D \w \W \n \t \r \b \. \* \[ \] \/ and the
                    // rest: as they are, a backslash before a letter or a
                    // symbol the engine reads the same way; \n is a line's
                    // end, which the search must be let cross.
                    out.acrossLines |= n == 'n';
                    out.regex += '\\';
                    out.regex += n;
                    at_start = n == 'n' || (n == '&' && magic != Magic::Very);
                    continue;
            }
        }
        if (c == '[' && magic != Magic::None)
        {
            // A bracket expression through to its close, as it stands; an
            // unclosed [ is itself.
            size_t j = i + 1;
            if (j < vim.size() && vim[j] == '^')
            {
                ++j;
            }
            if (j < vim.size() && vim[j] == ']')
            {
                ++j;
            }
            while (j < vim.size() && vim[j] != ']')
            {
                if (vim[j] == '\\' && j + 1 < vim.size())
                {
                    ++j;
                }
                ++j;
            }
            if (j < vim.size())
            {
                out.acrossLines |= vim.find("\\n", i) < j;
                out.regex.append(vim, i, j - i + 1);
                i = j;
            }
            else
            {
                out.regex += "\\[";
            }
            continue;
        }
        switch (magic)
        {
            case Magic::Very:
                switch (c)
                {
                    case '<':
                    case '>': out.regex += "\\b"; break;
                    case '=': out.regex += "?"; break;
                    case '%':
                        if (i + 1 < vim.size() && vim[i + 1] == '(')
                        {
                            out.regex += "(?:";
                            ++depth;
                            ++i;
                            at_start = true;
                        }
                        else if (i + 1 < vim.size() && vim[i + 1] == '[')
                        {
                            const size_t close = optionalSequence(i + 1);
                            if (close == std::string::npos)
                            {
                                literal('%');
                            }
                            else
                            {
                                i = close;
                            }
                        }
                        else
                        {
                            literal('%');
                        }
                        break;
                    case '{':
                    {
                        const size_t close = vim.find('}', i + 1);
                        std::string  body  = close == std::string::npos ? std::string() : vim.substr(i + 1, close - i - 1);
                        const bool   lazy  = !body.empty() && body[0] == '-';
                        if (close == std::string::npos)
                        {
                            out.regex += "\\{";
                            break;
                        }
                        if (lazy)
                        {
                            body.erase(0, 1);
                        }
                        out.regex += repeatOf(body);
                        if (lazy)
                        {
                            out.regex += "?";
                        }
                        i = close;
                        break;
                    }
                    case '~':
                        for (const char r : last_replacement)
                        {
                            literal(r);
                        }
                        break;
                    case '(': out.regex += c; ++depth; at_start = true; break;
                    case ')': out.regex += c; --depth; break;
                    case '|':
                    case '&': out.regex += c; at_start = true; break;
                    default: out.regex += c; break;
                }
                break;
            case Magic::Magic:
                switch (c)
                {
                    case '^': out.regex += start_here ? "^" : "\\^"; break;
                    case '$': out.regex += endsAt(i + 1) ? "$" : "\\$"; break;
                    case '.':
                    case '*': out.regex += c; break;
                    case '~':
                        // The last replacement, as the text it is.
                        for (const char r : last_replacement)
                        {
                            literal(r);
                        }
                        break;
                    default: literal(c); break;
                }
                break;
            case Magic::None:
                switch (c)
                {
                    // At the pattern's start -- nothing put in yet, \V and \c
                    // putting in nothing -- the line's start; elsewhere itself.
                    case '^': out.regex += out.regex.empty() ? "^" : "\\^"; break;
                    case '$': out.regex += i + 1 == vim.size() ? "$" : "\\$"; break;
                    default: literal(c); break;
                }
                break;
        }
    }
    if (looking)
    {
        out.regex += ")";
    }
    if (!k_at.empty() && !out.where.empty())
    {
        out.wholeRegex = out.regex;
        for (size_t k = k_at.size(); k-- > 0;)
        {
            out.wholeRegex.erase(k_at[k], 2);
        }
    }
    if (case_in_pattern)
    {
        out.caseSensitive = *case_in_pattern;
    }
    else if (force_case)
    {
        out.caseSensitive = *force_case;
    }
    else if (case_rules.ignore)
    {
        out.caseSensitive = case_rules.smart && std::any_of(vim.begin(), vim.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    }
    else
    {
        out.caseSensitive = true;
    }
    return out;
}

std::vector<ALTextRange> ALVimPattern::matchesIn(const ALTextDocument& d, ALTextSearchOptions options, const ALTextRange* scope, const Places& places,
                                                 std::string& error, std::vector<ALTextPos>& wholes, std::string_view with,
                                                 std::vector<std::string>* replaced) const
{
    options.acrossLines     = acrossLines;
    std::vector<ALTextRange> matches = replaced ? ALTextSearch::matches(d, regex, options, scope, &error, &wholes, with, *replaced)
                                                : ALTextSearch::matches(d, regex, options, scope, &error, &wholes);
    if (error.empty() && !wholeRegex.empty())
    {
        // The pattern without its \K matches the same stretches whole:
        // each match here is the whole one that ends where it does.
        std::string              other;
        std::vector<ALTextRange> full = ALTextSearch::matches(d, wholeRegex, options, scope, &other);
        // Both in the text's order: paired walking the two together.
        size_t f = 0;
        for (size_t i = 0; i < matches.size() && i < wholes.size(); ++i)
        {
            while (f < full.size() && full[f].end < matches[i].end)
            {
                ++f;
            }
            if (f < full.size() && full[f].end == matches[i].end)
            {
                wholes[i] = full[f].begin;
            }
        }
    }
    if (error.empty())
    {
        constrain(d, places, matches, wholes, replaced);
    }
    return matches;
}

bool ALVimPattern::placed() const
{
    return std::any_of(where.begin(), where.end(), [](const Where& place) { return place.kind == Where::Kind::Visual || place.kind == Where::Kind::Caret; });
}

void ALVimPattern::constrain(const ALTextDocument& d, const Places& places, std::vector<ALTextRange>& matches, std::vector<ALTextPos>& wholes,
                             std::vector<std::string>* replaced) const
{
    if (where.empty())
    {
        return;
    }
    const ALTextRange& visual      = places.visualRange;
    const S32          block_left  = places.blockLeft;
    const S32          block_right = places.blockRight;
    const ALTextPos&   caret       = places.caret;
    auto            allowed = [&](size_t index) {
        const ALTextRange match = matches[index].normalised();
        for (const Where& place : where)
        {
            // Before a \zs, the place is the whole match's start; after
            // it, the reported match's.
            const ALTextPos at = place.afterStart || index >= wholes.size() ? match.begin : wholes[index];
            switch (place.kind)
            {
                case Where::Kind::FileStart:
                    if (at != d.start())
                    {
                        return false;
                    }
                    break;
                case Where::Kind::FileEnd:
                {
                    // The end of the text, or of its last line where the
                    // text ends with a line break, which vim does not
                    // count as a line.
                    const ALTextPos end   = d.end();
                    const bool      last  = match.end == end;
                    const bool      above = end.column == 0 && end.line > 0 && match.end == d.lineEnd(end.line - 1);
                    if (!last && !above)
                    {
                        return false;
                    }
                    break;
                }
                case Where::Kind::Visual:
                    if (!places.visual || at < visual.begin || !(at < visual.end))
                    {
                        return false;
                    }
                    if (block_left >= 0)
                    {
                        // In what the block holds of the line, which the
                        // bytes before its columns place.
                        const ALTextRange piece = ALVimText::blockPiece(d, at.line, block_left, block_right, places.blockToEnd, places.tabWidth);
                        if (at < piece.begin || !(at < piece.end))
                        {
                            return false;
                        }
                    }
                    break;
                case Where::Kind::Caret:
                    if (at != caret)
                    {
                        return false;
                    }
                    break;
                case Where::Kind::Line:
                {
                    const S32 line = at.line + 1;
                    if (place.side < 0 ? line >= place.number : place.side > 0 ? line <= place.number : line != place.number)
                    {
                        return false;
                    }
                    break;
                }
                case Where::Kind::Column:
                {
                    const S32 column = at.column + 1;
                    if (place.side < 0 ? column >= place.number : place.side > 0 ? column <= place.number : column != place.number)
                    {
                        return false;
                    }
                    break;
                }
            }
        }
        return true;
    };
    // Each list kept where its match is, so that the n-th of each is
    // still the n-th match's.
    size_t kept = 0;
    for (size_t i = 0; i < matches.size(); ++i)
    {
        if (!allowed(i))
        {
            continue;
        }
        // Moved only once something before it has gone: a string moved
        // onto itself is left empty.
        if (kept != i)
        {
            matches[kept] = matches[i];
            if (i < wholes.size())
            {
                wholes[kept] = wholes[i];
            }
            if (replaced && i < replaced->size())
            {
                (*replaced)[kept] = std::move((*replaced)[i]);
            }
        }
        ++kept;
    }
    matches.resize(kept);
    wholes.resize(std::min(wholes.size(), kept));
    if (replaced)
    {
        replaced->resize(std::min(replaced->size(), kept));
    }
}

// static
std::string ALVimPattern::replacementOf(const std::string& with)
{
    // Vim's spelling to the search engine's: & and \0 are the whole
    // match, \1 to \9 the groups, \r and \n a line break, \t a tab;
    // \& \~ and \\ are themselves; \u \U \l \L \e \E change case as the
    // engine has them; a $ is only a $. The ~ was put in before this.
    std::string out;
    out.reserve(with.size() + 8);
    for (size_t i = 0; i < with.size(); ++i)
    {
        const char c = with[i];
        if (c == '\\' && i + 1 < with.size())
        {
            const char n = with[++i];
            switch (n)
            {
                case '&': out += '&'; break;
                case '~': out += '~'; break;
                case '\\': out += "\\\\"; break;
                case 'r':
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'e': out += "\\E"; break;
                case 'u':
                case 'U':
                case 'l':
                case 'L':
                case 'E': out += '\\'; out += n; break;
                default:
                    if (n >= '0' && n <= '9')
                    {
                        out += '$';
                        out += n;
                    }
                    else
                    {
                        out += n;
                    }
                    break;
            }
        }
        else if (c == '&')
        {
            out += "$&";
        }
        else if (c == '$')
        {
            out += "$$";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

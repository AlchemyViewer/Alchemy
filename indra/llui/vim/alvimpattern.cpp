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

#include <unicode/uchar.h>

#include <algorithm>

namespace
{
    // vim's classes in a bracket expression, [:alpha:] and the rest, each
    // as what the engine reads for it in a bracket of its own: what the
    // class holds where vim keeps to ASCII or the engine has no such
    // class, the engine's class where both go by Unicode.
    struct BracketClass
    {
        std::string_view name;
        std::string_view engine;
    };
    constexpr BracketClass BRACKET_CLASSES[] = {
        { "alnum", "A-Za-z0-9" },
        { "alpha", "A-Za-z" },
        { "blank", " \\t" },
        { "cntrl", "\\x00-\\x1f\\x7f" },
        { "digit", "0-9" },
        { "graph", "!-~" },
        { "lower", "[:lower:]" },
        { "print", "[:print:]" },
        { "punct", "!-/:-@\\[-`{-~" },
        { "space", " \\t\\r\\n\\v\\f" },
        { "upper", "[:upper:]" },
        { "xdigit", "0-9A-Fa-f" },
        { "tab", "\\t" },
        { "return", "\\r" },
        { "backspace", "\\x08" },
        { "escape", "\\x1b" },
        { "ident", "A-Za-z0-9_" },
        { "keyword", "A-Za-z0-9_" },
        { "fname", "A-Za-z0-9/.\\-_+,#$%~=" },
    };

    // A bracket expression of vim's, its [ at `at`, as vim's skip_anyof
    // reads it: a ^ first negates it and a ] or - after that is itself; a
    // backslash takes the character after it with it only where it is
    // one vim means something by there, and is itself before any other;
    // [:alpha:] [=a=] and [.a.] are whole, and any other [ is itself.
    // The index of its ] comes back, or npos where none closes it; the
    // engine's spelling of it is put in `engine` where one is given.
    size_t bracketOf(std::string_view vim, size_t at, std::string* engine = nullptr)
    {
        constexpr std::string_view TAKEN("]^-n\\nrtebdoxuU");
        auto put = [engine](std::string_view s) {
            if (engine)
            {
                *engine += s;
            }
        };
        put("[");
        size_t j = at + 1;
        if (j < vim.size() && vim[j] == '^')
        {
            put("^");
            ++j;
        }
        if (j < vim.size() && (vim[j] == ']' || vim[j] == '-'))
        {
            put(vim[j] == ']' ? "\\]" : "\\-");
            ++j;
        }
        while (j < vim.size() && vim[j] != ']')
        {
            if (vim[j] == '\\' && j + 1 < vim.size() && TAKEN.find(vim[j + 1]) != std::string_view::npos)
            {
                put(vim.substr(j, 2));
                j += 2;
            }
            else if (vim[j] == '\\')
            {
                put("\\\\");
                ++j;
            }
            else if (vim[j] == '[' && j + 1 < vim.size() && vim[j + 1] == ':')
            {
                const size_t close = vim.find(":]", j + 2);
                const std::string_view name = close == std::string_view::npos ? std::string_view() : vim.substr(j + 2, close - j - 2);
                const BracketClass* known =
                    std::find_if(std::begin(BRACKET_CLASSES), std::end(BRACKET_CLASSES), [name](const BracketClass& c) { return c.name == name; });
                if (known == std::end(BRACKET_CLASSES))
                {
                    put("\\[");
                    ++j;
                    continue;
                }
                put(known->engine);
                j = close + 2;
            }
            else if (vim[j] == '[' && j + 1 < vim.size() && (vim[j + 1] == '=' || vim[j + 1] == '.'))
            {
                // One character between the two = or . and the ] after
                // them.
                const char   how  = vim[j + 1];
                const size_t next = j + 2 < vim.size() ? utf8str_decode_at(vim, j + 2).next : j + 2;
                if (next + 1 >= vim.size() || next <= j + 2 || vim[next] != how || vim[next + 1] != ']')
                {
                    put("\\[");
                    ++j;
                    continue;
                }
                const std::string_view one = vim.substr(j + 2, next - j - 2);
                if (how == '=')
                {
                    put("[=");
                    put(one);
                    put("=]");
                }
                else
                {
                    put(alRegexSpecial(one[0]) || one[0] == '-' || one[0] == ']' ? "\\" : "");
                    put(one);
                }
                j = next + 2;
            }
            else if (vim[j] == '[')
            {
                put("\\[");
                ++j;
            }
            else if (vim[j] == '-' && vim.substr(j + 1, 2) == "\\n")
            {
                // No range to a line break: the - itself.
                put("\\-");
                ++j;
            }
            else
            {
                put(vim.substr(j, 1));
                ++j;
            }
        }
        if (j >= vim.size())
        {
            return std::string::npos;
        }
        put("]");
        return j;
    }

    // Each backslash item in a pattern outside its bracket expressions,
    // the character after the backslash handed to `item`, as vim walks a
    // pattern for what it says of itself (skip_regexp): what opens a
    // bracket expression goes by the last \v or \V before it.
    template <typename Item>
    void eachItem(const std::string& vim, Item&& item)
    {
        bool very_nomagic = false;
        for (size_t i = 0; i < vim.size(); ++i)
        {
            if ((vim[i] == '[' && !very_nomagic) || (vim[i] == '\\' && i + 1 < vim.size() && vim[i + 1] == '[' && very_nomagic))
            {
                // Through to the bracket's close; an unclosed one runs on to
                // the end.
                i = std::min(bracketOf(vim, vim[i] == '[' ? i : i + 1), vim.size());
            }
            else if (vim[i] == '\\' && i + 1 < vim.size())
            {
                ++i;
                very_nomagic = vim[i] == 'V' || (vim[i] != 'v' && very_nomagic);
                item(vim[i]);
            }
        }
    }

    // Whether a pattern has a capital in it, as vim's smartcase asks
    // (pat_has_uppercase): one after a backslash is no capital -- \S, \V,
    // and \_S and \%V with the character after their _ or % -- nor in a
    // very magic pattern one after a bare % or _, where a backslash is no
    // more than itself. How magic the pattern is goes by the last \v or \V
    // in it outside a bracket expression, as vim measures it for this.
    bool hasCapital(const std::string& vim)
    {
        enum class Magic : U8
        {
            On,
            Very,
            None
        };
        Magic magic = Magic::On;
        eachItem(vim, [&magic](char item) { magic = item == 'v' ? Magic::Very : item == 'V' ? Magic::None : magic; });
        for (size_t i = 0; i < vim.size();)
        {
            const unsigned char c = static_cast<unsigned char>(vim[i]);
            if (c >= 0x80)
            {
                const LLCodepointAt cp = utf8str_decode_at(vim, i);
                if (cp.next > i + 1 && LLStringOps::isUpper(cp.cp))
                {
                    return true;
                }
                i = llmax(cp.next, i + 1);
            }
            else if (c == '\\' && magic != Magic::Very)
            {
                const bool two = i + 2 < vim.size() && (vim[i + 1] == '_' || vim[i + 1] == '%');
                i += two ? 3u : (i + 1 < vim.size() ? 2u : 1u);
            }
            else if ((c == '%' || c == '_') && magic == Magic::Very)
            {
                i += i + 1 < vim.size() ? 2u : 1u;
            }
            else if (c >= 'A' && c <= 'Z')
            {
                return true;
            }
            else
            {
                ++i;
            }
        }
        return false;
    }

    // Whether a pattern has a \Z in it, which has vim ignore composing
    // characters throughout.
    bool ignoresMarks(const std::string& vim)
    {
        bool found = false;
        eachItem(vim, [&found](char item) { found = found || item == 'Z'; });
        return found;
    }

    // A format of the engine's with vim's groups in it, ${1} to ${9} as
    // ALVimPattern::replacementOf writes them, each by the engine's number
    // for it (`numbers`, in vim's order); one vim does not have is
    // nothing, as vim makes it.
    std::string groupsAsCounted(std::string_view format, const std::vector<S32>& numbers)
    {
        std::string out;
        out.reserve(format.size() + 8);
        for (size_t i = 0; i < format.size(); ++i)
        {
            const char c = format[i];
            if (c == '$' && i + 3 < format.size() && format[i + 1] == '{' && format[i + 2] >= '1' && format[i + 2] <= '9' && format[i + 3] == '}')
            {
                const size_t vim_group = static_cast<size_t>(format[i + 2] - '1');
                if (vim_group < numbers.size())
                {
                    out += "${" + std::to_string(numbers[vim_group]) + "}";
                }
                i += 3;
            }
            else if ((c == '\\' || c == '$') && i + 1 < format.size())
            {
                // An escape, or $$ $& and the like, as it stands.
                out += c;
                out += format[++i];
            }
            else
            {
                out += c;
            }
        }
        return out;
    }
}

// static
ALVimPattern ALVimPattern::of(const std::string& vim, const std::string& last_replacement, const Case& case_rules, std::optional<bool> force_case)
{
    // Vim's magic spelling to Perl's: with a backslash, ( ) | + ? = { }
    // < > are the engine's own ( ) | + ? ? { } \b \b, and without one
    // they are themselves; \v makes what follows very magic, where they
    // are the engine's own bare and themselves with a backslash; \V very
    // nomagic, where only the backslash items are special -- \. \* \~ \[
    // are magic's . * ~ [ -- a bare ^ and $ themselves and \^ \$ a line's
    // start and end anywhere; \M nomagic, the same but for magic's ^ and
    // $. \zs is \K; \ze looks ahead at the rest of its branch, or is a
    // group at which the match is cut; \& looks ahead at the concat
    // before it from where the next begins; \@= \@! \@<= \@<! and \@>
    // look round the atom before them; \{-} is *?; the classes \a \l \u
    // \x \o \h \i \k are brackets; \c and \C say how case is matched, and
    // \Z that composing characters are passed over; a bracket expression
    // is read as vim reads one, its classes as what they hold. A magic ^
    // is a line's start only first in a branch, a $ its end only last in
    // one, and either is itself anywhere else.
    ALVimPattern        out;
    std::optional<bool> case_in_pattern;
    enum class Magic : U8
    {
        Magic,
        Very,
        // \M, nomagic.
        Off,
        // \V, very nomagic.
        None
    };
    Magic magic    = Magic::Magic;
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
    // The last replacement made, as the text it is.
    auto lastReplacement = [&]() {
        for (const char r : last_replacement)
        {
            literal(r);
        }
    };
    // A bracket expression through to its close, as the engine spells it.
    // `at` is the [; the index of the ] comes back, or npos where none
    // closes it.
    auto bracket = [&](size_t at) {
        std::string  engine;
        const size_t j = bracketOf(vim, at, &engine);
        if (j == std::string::npos)
        {
            return std::string::npos;
        }
        out.acrossLines |= vim.find("\\n", at) < j;
        out.regex += engine;
        return j;
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
    // Whether a magic or nomagic ^ read now is the line's start, as vim
    // has it: at the pattern's start, and after \( \%( \| \& or \n, a \c,
    // a \v and the like between them putting in nothing; anywhere else it
    // is itself.
    bool at_start = true;
    // Whether a magic or nomagic $ before `k` is the line's end, as vim
    // has it: at the pattern's end, and before \| \) \& or \n -- or very
    // magic's | ) and & where a \v comes between -- a \c, a \v and the like
    // between them putting in nothing; anywhere else it is itself.
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
    // How deep in the engine's brackets the output is, so that a \ze at
    // the top can look ahead to its branch's end.
    S32   depth    = 0;
    // The look aheads \ze opened, by the depth each was opened at and
    // where in the expression: each runs to the end of its branch -- a \|
    // at that depth, the \) that closes its group, or the pattern's end.
    // One with nothing in it matches anywhere, and the engine takes no
    // empty look ahead, so it goes.
    std::vector<std::pair<S32, size_t>> looking;
    auto                                closeLook = [&]() {
        if (out.regex.size() == looking.back().second + 3)
        {
            out.regex.resize(looking.back().second);
        }
        else
        {
            out.regex += ')';
        }
        looking.pop_back();
    };
    auto closeLooks = [&]() {
        while (!looking.empty() && looking.back().first == depth)
        {
            closeLook();
        }
    };
    // Only the first \ze of a branch at the top looks ahead. Any other --
    // in a group, where what follows the group must still match past it,
    // or after another, where the last crossed is the one that counts --
    // is an empty group of the engine's at which the match is cut, the
    // pattern matched on past it (cutGroups). Each with where it went,
    // since one a look round takes in counts for nothing, as in vim; and
    // whether a \ze has come in the branch at the top so far.
    std::vector<std::pair<size_t, S32>> cuts;
    bool                                ze_in_branch = false;
    // The engine's groups as they open, vim's and the cuts among them;
    // the engine's number for each of vim's.
    S32              groups = 0;
    std::vector<S32> vim_groups;
    // Where each group still open began in the expression, and where the
    // last atom did, which a \@ looks around: none at the start of a
    // branch. Each token read is taken for an atom until it says it is
    // none -- a multi, a group's opening, a \c -- and a group's closing is
    // the group's atom.
    std::vector<size_t> open_at;
    // Where the concat being read began in the expression, at each depth
    // the innermost last: the pattern's start, a group's opening, a \| or
    // a \&.
    std::vector<size_t> concats     = { 0 };
    size_t              atom_at     = std::string::npos;
    size_t              token_at    = std::string::npos;
    bool                token_atom  = false;
    bool                token_group = false;
    // A \Z anywhere has composing characters ignored: each atom that is a
    // character takes the marks after it in the text with it, in a group
    // of its own so that a multi after it repeats both, and a mark in the
    // pattern is none. The last token so taken, where it was such an atom;
    // not a group, whose characters took their own, nor a line's or a
    // word's edge, which is none.
    const bool ignore_marks = ignoresMarks(vim);
    auto       withMarks    = [&]() {
        if (!ignore_marks || !token_atom || token_group || token_at >= out.regex.size())
        {
            return;
        }
        const std::string_view put = std::string_view(out.regex).substr(token_at);
        if (put != "^" && put != "$" && put != "\\b")
        {
            out.regex.insert(token_at, "(?:");
            out.regex += "\\p{Mark}*)";
        }
    };
    // \@= \@! \@<= \@<! \@>, and \@123<= with how far back it may look,
    // which the engine needs no telling: the last atom looked ahead at or
    // behind it, or taken whole and never given back, as the engine's (?=
    // (?! (?<= (?<! (?>. `at` is past the @; the index of the last
    // character taken comes back, or npos where what follows is none of
    // these or no atom comes before.
    auto lookAround = [&](size_t at) {
        size_t k = at;
        while (k < vim.size() && vim[k] >= '0' && vim[k] <= '9')
        {
            ++k;
        }
        std::string_view open;
        if (k + 1 < vim.size() && vim[k] == '<' && (vim[k + 1] == '=' || vim[k + 1] == '!'))
        {
            open = vim[k + 1] == '=' ? "(?<=" : "(?<!";
            ++k;
        }
        else if (k == at && k < vim.size() && (vim[k] == '=' || vim[k] == '!' || vim[k] == '>'))
        {
            open = vim[k] == '=' ? "(?=" : vim[k] == '!' ? "(?!" : "(?>";
        }
        if (open.empty() || atom_at == std::string::npos)
        {
            return std::string::npos;
        }
        out.regex.insert(atom_at, open.data(), open.size());
        for (size_t& put_at : k_at)
        {
            if (put_at >= atom_at)
            {
                put_at += open.size();
            }
        }
        for (std::pair<S32, size_t>& look : looking)
        {
            if (look.second >= atom_at)
            {
                look.second += open.size();
            }
        }
        // A cut in what is looked ahead at or behind is none; one in what
        // is taken whole still is.
        if (open != "(?>")
        {
            std::erase_if(cuts, [&](const std::pair<size_t, S32>& cut) { return cut.first >= atom_at; });
        }
        for (std::pair<size_t, S32>& cut : cuts)
        {
            if (cut.first >= atom_at)
            {
                cut.first += open.size();
            }
        }
        out.regex += ')';
        return k;
    };
    // \& and very magic's &: the concat before it must match where the
    // next does, and is looked ahead at from there, the branch's last
    // concat alone the match. A \zs or a \ze in one looked ahead at counts
    // for nothing, as in vim.
    auto endConcat = [&]() {
        closeLooks();
        const size_t from = concats.back();
        while (!k_at.empty() && k_at.back() >= from)
        {
            out.regex.erase(k_at.back(), 2);
            k_at.pop_back();
        }
        zs_seen = !k_at.empty();
        std::erase_if(cuts, [from](const std::pair<size_t, S32>& cut) { return cut.first >= from; });
        // An empty one matches anywhere, and the engine takes no empty
        // look ahead.
        if (out.regex.size() > from)
        {
            out.regex.insert(from, "(?=");
            out.regex += ')';
        }
        concats.back() = out.regex.size();
        ze_in_branch   = ze_in_branch && depth > 0;
        at_start       = true;
        atom_at        = std::string::npos;
        token_atom     = false;
    };
    for (size_t i = 0; i < vim.size(); ++i)
    {
        const char c          = vim[i];
        const bool start_here = at_start;
        at_start              = false;
        // The token before this one is the last atom where it was one; the
        // later bytes of a character are its first's.
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
        {
            withMarks();
            if (token_atom)
            {
                atom_at = token_at;
            }
            token_at    = out.regex.size();
            token_atom  = true;
            token_group = false;
            if (ignore_marks && static_cast<unsigned char>(c) >= 0xC0)
            {
                const LLCodepointAt mark = utf8str_decode_at(vim, i);
                if (mark.next > i + 1 && (U_GET_GC_MASK(static_cast<UChar32>(mark.cp)) & U_GC_M_MASK) != 0)
                {
                    i          = mark.next - 1;
                    at_start   = start_here;
                    token_atom = false;
                    continue;
                }
            }
        }
        if (c == '\\' && i + 1 < vim.size())
        {
            const char n       = vim[++i];
            const bool nomagic = magic == Magic::Off || magic == Magic::None;
            switch (n)
            {
                case 'v': magic = Magic::Very; at_start = start_here; token_atom = false; continue;
                case 'm': magic = Magic::Magic; at_start = start_here; token_atom = false; continue;
                case 'M': magic = Magic::Off; at_start = start_here; token_atom = false; continue;
                case 'V': magic = Magic::None; at_start = start_here; token_atom = false; continue;
                case 'c': case_in_pattern = false; at_start = start_here; token_atom = false; continue;
                case 'C': case_in_pattern = true; at_start = start_here; token_atom = false; continue;
                case 'Z': at_start = start_here; token_atom = false; continue;
                case '(':
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\(";
                        continue;
                    }
                    open_at.push_back(out.regex.size());
                    out.regex += "(";
                    concats.push_back(out.regex.size());
                    vim_groups.push_back(++groups);
                    ++depth;
                    at_start   = true;
                    atom_at    = std::string::npos;
                    token_atom = false;
                    continue;
                case ')':
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\)";
                        continue;
                    }
                    closeLooks();
                    out.regex += ")";
                    --depth;
                    if (concats.size() > 1)
                    {
                        concats.pop_back();
                    }
                    // The group whole, from where it opened.
                    token_atom  = !open_at.empty();
                    token_group = true;
                    if (token_atom)
                    {
                        token_at = open_at.back();
                        open_at.pop_back();
                    }
                    continue;
                case '|':
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\|";
                        continue;
                    }
                    closeLooks();
                    out.regex += "|";
                    concats.back() = out.regex.size();
                    ze_in_branch   = ze_in_branch && depth > 0;
                    at_start       = true;
                    atom_at        = std::string::npos;
                    token_atom     = false;
                    continue;
                case '&':
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\&";
                        continue;
                    }
                    endConcat();
                    continue;
                case '@':
                {
                    const size_t to = magic == Magic::Very ? std::string::npos : lookAround(i + 1);
                    if (to == std::string::npos)
                    {
                        out.regex += "\\@";
                        continue;
                    }
                    i          = to;
                    token_atom = false;
                    continue;
                }
                case '+': out.regex += magic == Magic::Very ? "\\+" : "+"; token_atom = magic == Magic::Very; continue;
                case '?':
                case '=': out.regex += magic == Magic::Very ? std::string(1, n) : "?"; token_atom = magic == Magic::Very; continue;
                case '<':
                case '>': out.regex += magic == Magic::Very ? std::string(1, n) : "\\b"; continue;
                // Very nomagic's line's start and end, anywhere; elsewhere
                // the characters.
                case '^':
                case '$': out.regex += magic == Magic::None ? std::string(1, n) : "\\" + std::string(1, n); continue;
                // Nomagic's and very nomagic's any character, repeat, last
                // replacement and bracket expression, which are magic's
                // bare ones; elsewhere the characters.
                case '.': out.regex += nomagic ? "." : "\\."; continue;
                case '*': out.regex += nomagic ? "*" : "\\*"; token_atom = !nomagic; continue;
                case '~':
                    if (nomagic)
                    {
                        lastReplacement();
                        continue;
                    }
                    out.regex += "\\~";
                    continue;
                case '[':
                {
                    const size_t close = nomagic ? bracket(i) : std::string::npos;
                    if (close == std::string::npos)
                    {
                        out.regex += "\\[";
                        continue;
                    }
                    i = close;
                    continue;
                }
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
                    token_atom = false;
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
                        token_atom = false;
                        continue;
                    }
                    if (i + 1 < vim.size() && vim[i + 1] == 'e')
                    {
                        if (depth == 0 && !ze_in_branch)
                        {
                            looking.emplace_back(depth, out.regex.size());
                            out.regex += "(?=";
                        }
                        else
                        {
                            cuts.emplace_back(out.regex.size(), ++groups);
                            out.regex += "()";
                        }
                        ze_in_branch = true;
                        ++i;
                        token_atom = false;
                        continue;
                    }
                    literal('z');
                    continue;
                case '%':
                {
                    if (i + 1 < vim.size() && vim[i + 1] == '(')
                    {
                        open_at.push_back(out.regex.size());
                        out.regex += "(?:";
                        concats.push_back(out.regex.size());
                        ++depth;
                        ++i;
                        at_start   = true;
                        atom_at    = std::string::npos;
                        token_atom = false;
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
                        token_atom = false;
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
                            i          = k;
                            token_atom = false;
                            continue;
                        }
                    }
                    literal('%');
                    continue;
                }
                case '_':
                    // \_^ and \_$: a line's start and end wherever they
                    // stand, crossing none.
                    if (i + 1 < vim.size() && (vim[i + 1] == '^' || vim[i + 1] == '$'))
                    {
                        out.regex += vim[++i];
                        continue;
                    }
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
                            // \_[abc]: the bracket expression or a line
                            // break, which a ^ negating it leaves be.
                            std::string  engine;
                            const size_t close = bracketOf(vim, i, &engine);
                            if (close == std::string::npos)
                            {
                                out.regex += "\\[";
                            }
                            else
                            {
                                out.regex += "(?:" + engine + "|\\n)";
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
                    // A back reference to one of vim's groups that a cut
                    // before it has the engine count otherwise, by the
                    // engine's number.
                    if (n >= '1' && n <= '9' && static_cast<size_t>(n - '0') <= vim_groups.size() &&
                        vim_groups[static_cast<size_t>(n - '1')] != n - '0')
                    {
                        out.regex += "\\g{" + std::to_string(vim_groups[static_cast<size_t>(n - '1')]) + "}";
                        continue;
                    }
                    // \s \S \d \D \w \W \n \t \r \b \] \/ and the rest: as
                    // they are, a backslash before a letter or a symbol the
                    // engine reads the same way; \n is a line's end, which
                    // the search must be let cross.
                    out.acrossLines |= n == 'n';
                    out.regex += '\\';
                    out.regex += n;
                    at_start = n == 'n';
                    continue;
            }
        }
        if (c == '[' && (magic == Magic::Magic || magic == Magic::Very))
        {
            // A bracket expression; an unclosed [ is itself.
            const size_t close = bracket(i);
            if (close == std::string::npos)
            {
                out.regex += "\\[";
                continue;
            }
            i = close;
            continue;
        }
        switch (magic)
        {
            case Magic::Very:
                switch (c)
                {
                    case '<':
                    case '>': out.regex += "\\b"; break;
                    case '=': out.regex += "?"; token_atom = false; break;
                    case '+':
                    case '?':
                    case '*': out.regex += c; token_atom = false; break;
                    case '@':
                    {
                        const size_t to = lookAround(i + 1);
                        if (to == std::string::npos)
                        {
                            out.regex += c;
                            break;
                        }
                        i          = to;
                        token_atom = false;
                        break;
                    }
                    case '%':
                        if (i + 1 < vim.size() && vim[i + 1] == '(')
                        {
                            open_at.push_back(out.regex.size());
                            out.regex += "(?:";
                            concats.push_back(out.regex.size());
                            ++depth;
                            ++i;
                            at_start   = true;
                            atom_at    = std::string::npos;
                            token_atom = false;
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
                        i          = close;
                        token_atom = false;
                        break;
                    }
                    case '~': lastReplacement(); break;
                    case '(':
                        open_at.push_back(out.regex.size());
                        out.regex += c;
                        concats.push_back(out.regex.size());
                        vim_groups.push_back(++groups);
                        ++depth;
                        at_start   = true;
                        atom_at    = std::string::npos;
                        token_atom = false;
                        break;
                    case ')':
                        closeLooks();
                        out.regex += c;
                        --depth;
                        if (concats.size() > 1)
                        {
                            concats.pop_back();
                        }
                        token_atom  = !open_at.empty();
                        token_group = true;
                        if (token_atom)
                        {
                            token_at = open_at.back();
                            open_at.pop_back();
                        }
                        break;
                    case '|':
                        closeLooks();
                        out.regex += c;
                        concats.back() = out.regex.size();
                        ze_in_branch   = ze_in_branch && depth > 0;
                        at_start       = true;
                        atom_at        = std::string::npos;
                        token_atom     = false;
                        break;
                    case '&': endConcat(); break;
                    default: out.regex += c; break;
                }
                break;
            case Magic::Magic:
                switch (c)
                {
                    case '^': out.regex += start_here ? "^" : "\\^"; break;
                    case '$': out.regex += endsAt(i + 1) ? "$" : "\\$"; break;
                    case '.': out.regex += c; break;
                    case '*': out.regex += c; token_atom = false; break;
                    case '~': lastReplacement(); break;
                    default: literal(c); break;
                }
                break;
            case Magic::Off:
                // Magic's ^ and $, and every other character itself.
                switch (c)
                {
                    case '^': out.regex += start_here ? "^" : "\\^"; break;
                    case '$': out.regex += endsAt(i + 1) ? "$" : "\\$"; break;
                    default: literal(c); break;
                }
                break;
            case Magic::None:
                // Every character itself, ^ and $ too.
                literal(c);
                break;
        }
    }
    withMarks();
    while (!looking.empty())
    {
        closeLook();
    }
    for (const std::pair<size_t, S32>& cut : cuts)
    {
        if (cut.second < 64)
        {
            out.cutGroups |= U64(1) << cut.second;
        }
    }
    for (size_t k = 0; k < vim_groups.size(); ++k)
    {
        if (vim_groups[k] != static_cast<S32>(k + 1))
        {
            out.groupNumbers = vim_groups;
            break;
        }
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
        out.caseSensitive = case_rules.smart && hasCapital(vim);
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
    options.cutGroups       = cutGroups;
    // The format's groups by vim's numbers, where the engine counts the
    // same groups otherwise.
    std::string renumbered;
    if (replaced && !groupNumbers.empty())
    {
        renumbered = groupsAsCounted(with, groupNumbers);
        with       = renumbered;
    }
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
    // match, \1 to \9 the groups, each of one digit and in braces for the
    // engine, which would read a digit after it as more of the number;
    // \r and \n a line break, \t a tab; \& \~ and \\ are themselves; \u
    // \U \l \L \e \E change case as the engine has them; a $ is only a $.
    // The ~ was put in before this.
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
                        out += "${";
                        out += n;
                        out += '}';
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

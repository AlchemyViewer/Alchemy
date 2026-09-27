/**
 * @file alsyntaxgrammar.cpp
 * @brief A highlighting grammar: states of rules, read from a file.
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

#include "alsyntaxgrammar.h"

#include "alstringmatch.h"

#include "alregex.h"
#include "lldir.h"
#include "llsdserialize.h"
#include "llstring.h"

#include <bitset>

// --- kinds -------------------------------------------------------------------

namespace
{
    const char* const KIND_NAMES[] = {
        "text",     "comment",  "doc_comment", "string",       "escape",       "number",   "keyword",   "control",
        "type",     "constant", "function",    "event",        "label",        "operator", "punctuation", "preprocessor",
        "tag",      "attribute", "attribute_value", "entity",   "variable",     "parameter", "property", "deprecated", "invalid",
        "namespace", "state",   "global_variable", "path",
    };
    static_assert(sizeof(KIND_NAMES) / sizeof(KIND_NAMES[0]) == static_cast<size_t>(ALSyntaxKind::COUNT), "every kind has a name");

    // A byte that is part of a word: a letter, a digit, an underscore, or
    // anything beyond ASCII.
    bool wordByte(unsigned char c)
    {
        return c >= 0x80 || c == '_' || std::isalnum(c);
    }

    // What a character class rule matches: a set of ASCII bytes, and
    // whether every byte beyond ASCII is in it too.
    struct CharClass
    {
        std::bitset<128> ascii;
        bool             unicode = false;

        bool matches(unsigned char c) const { return c >= 0x80 ? unicode : ascii.test(c); }
    };

    // "a-zA-Z0-9_" and the like. A backslash keeps the next character.
    bool parseCharClass(std::string_view spec, CharClass& out, std::string& error)
    {
        out.ascii.reset();
        size_t i = 0;
        auto next = [&](unsigned char& c) -> bool {
            if (i >= spec.size())
            {
                return false;
            }
            c = static_cast<unsigned char>(spec[i++]);
            if (c == '\\')
            {
                if (i >= spec.size())
                {
                    return false;
                }
                c = static_cast<unsigned char>(spec[i++]);
            }
            return true;
        };
        unsigned char c;
        while (next(c))
        {
            if (c >= 0x80)
            {
                error = "a character class holds ASCII; use unicode: true for the rest";
                return false;
            }
            if (i + 1 < spec.size() && spec[i] == '-')
            {
                ++i;
                unsigned char hi;
                if (!next(hi) || hi < c || hi >= 0x80)
                {
                    error = "a range in a character class runs upward within ASCII";
                    return false;
                }
                for (unsigned int b = c; b <= hi; ++b)
                {
                    out.ascii.set(b);
                }
            }
            else
            {
                out.ascii.set(c);
            }
        }
        return true;
    }
}

const char* alSyntaxKindName(ALSyntaxKind kind)
{
    const size_t index = static_cast<size_t>(kind);
    return index < static_cast<size_t>(ALSyntaxKind::COUNT) ? KIND_NAMES[index] : "text";
}

std::optional<ALSyntaxKind> alSyntaxKindFromName(std::string_view name)
{
    for (size_t i = 0; i < static_cast<size_t>(ALSyntaxKind::COUNT); ++i)
    {
        if (name == KIND_NAMES[i])
        {
            return static_cast<ALSyntaxKind>(i);
        }
    }
    return std::nullopt;
}

// --- words -------------------------------------------------------------------

void ALSyntaxWords::set(std::string_view table, std::vector<std::string> words)
{
    set_t& set = mTables[std::string(table)];
    set.clear();
    for (std::string& word : words)
    {
        set.insert(std::move(word));
    }
}

void ALSyntaxWords::clear()
{
    mTables.clear();
}

bool ALSyntaxWords::has(std::string_view table, std::string_view word) const
{
    const auto it = mTables.find(table);
    return it != mTables.end() && it->second.contains(word);
}

void ALSyntaxWords::collect(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const
{
    for (const auto& [table, words] : mTables)
    {
        for (const std::string& word : words)
        {
            if (ALStringMatch::startsWithNoCase(word, prefix))
            {
                out.emplace_back(word, table);
            }
        }
    }
}

// --- the grammar -------------------------------------------------------------

struct ALSyntaxGrammar::Impl
{
    struct Rule
    {
        enum class Match : U8
        {
            Literal,
            Chars,
            Word,
            Span,
            Regex,
            Eol,
            // Inside a span: the end that leaves it, written with the
            // capture the opening made, and the escape that keeps a
            // character from being the end.
            SpanEnd,
            SpanEscape
        };
        enum class Then : U8
        {
            Stay,
            Push,
            Pop,
            Next
        };

        Match        match     = Match::Literal;
        ALSyntaxKind kind      = ALSyntaxKind::Text;
        Then         then      = Then::Stay;
        U16          target    = 0;
        bool         wholeWord = false;
        // Word: a word right after `head.` is looked up as `head.word`
        // first, which is how SLua's tables name `ll.Say`.
        bool         qualified = false;
        // Literal and SpanEscape: the text; Span: the opening; SpanEnd: the
        // end, a regex where endRegex says so, with \1 standing for the
        // capture.
        std::string  text;
        bool         endRegex = false;
        // Chars.
        CharClass    chars;
        S32          minCount = 1;
        S32          maxCount = 0;
        // Word: the tables in the order they are asked, each with the kind
        // a word found there is.
        std::vector<std::pair<std::string, ALSyntaxKind>> tables;
        // Regex, and a span opened by one.
        ALRegex      regex;
        // The bytes a match of the regex can begin with, from firstLo to
        // firstHi, where that could be said: at a place with another byte
        // the regex is not tried.
        bool         firstKnown = false;
        U8           firstLo    = 0;
        U8           firstHi    = 0xFF;
        // Regex: what may not come right before a match, and the group
        // whose text the rule takes; the rest of the match is only read.
        CharClass    notAfter;
        bool         hasNotAfter = false;
        S32          consume     = 0;

        bool mayStartWith(unsigned char c) const { return !firstKnown || (c >= firstLo && c <= firstHi); }
    };

    // What opens a block, and what may not come after it on the line for
    // it to.
    struct Opener
    {
        ALRegex regex;
        ALRegex unless;
    };

    struct State
    {
        std::string       name;
        std::vector<Rule> rules;
        ALSyntaxKind      defaultKind = ALSyntaxKind::Text;
    };

    std::string              name;
    std::vector<std::string> extensions;
    std::vector<std::string> wordTables;
    std::string              lineComment;
    std::string              memberSeparators = ".";
    std::vector<std::pair<char, char>> pairs;
    bool                     prose = false;
    // What opens a block, searched for at the end of the text before the
    // caret, and what closes one, matched at the start of a line's text.
    std::vector<Opener>      indentOpens;
    ALRegex                  indentCloses;
    // A head that sends only the line under it in, and what that line may
    // begin with to stay level with it instead.
    std::vector<Opener>      indentOnce;
    ALRegex                  indentOnceExcept;
    // What the editor closes a block with, and the openers it closes.
    std::string              blockEndWord;
    std::vector<Opener>      blockEnds;
    // How a comment goes on to a new line: what its line begins with,
    // what ends it, and what the new line begins with.
    struct Continuation
    {
        ALRegex     regex;
        ALRegex     unless;
        std::string with;
    };
    std::vector<Continuation> continuations;
    ALSyntaxGrammar::FoldWords foldWords;
    std::vector<State>       states;
    // The words the grammar declares for its tables, ahead of whatever is
    // filled in at runtime.
    ALSyntaxWords            words;
    U16                      initial = 0;
    CharClass                wordStart;
    CharClass                wordContinue;
    // The end regexes of spans, one per capture they were written with,
    // up to a number, then made afresh: a grammar is every view's, for
    // the whole session, and a capture may be any text at all.
    mutable std::map<std::string, ALRegex> endRegexes;

    S32 stateIndex(std::string_view name) const
    {
        for (size_t i = 0; i < states.size(); ++i)
        {
            if (states[i].name == name)
            {
                return static_cast<S32>(i);
            }
        }
        return -1;
    }

    // A rule's push or next, by the indices of the rule and the name of
    // the state, resolved once every state is there.
    struct Target
    {
        S32         state;
        size_t      rule;
        std::string name;
    };

    bool loadRule(const LLSD& in, const std::string& state_name, size_t index, std::vector<Target>& targets, std::string& error);
    bool compileRegex(const std::string& pattern, ALRegex& out, std::string& error) const;
    bool compileOpeners(const LLSD& opens, std::vector<Opener>& out, std::string& error);
    static bool anyOpens(const std::vector<Opener>& openers, std::string_view before);
    size_t tryRule(const Rule& rule, std::string_view line, size_t pos, const std::string& payload, const ALSyntaxWords& words,
                   ALSyntaxKind& kind, std::string& capture) const;
    const ALRegex* endRegexFor(const Rule& rule, const std::string& payload) const;
};

bool ALSyntaxGrammar::Impl::compileRegex(const std::string& pattern, ALRegex& out, std::string& error) const
{
    out = ALRegex(pattern);
    if (!out.ok())
    {
        error = "regex '" + pattern + "': " + out.error();
        return false;
    }
    return true;
}

bool ALSyntaxGrammar::Impl::compileOpeners(const LLSD& opens, std::vector<Opener>& out, std::string& error)
{
    // One pattern, or a list of them, each a pattern or a map of one and
    // what may not follow it on the line.
    const LLSD list = opens.isArray() ? opens : LLSD::emptyArray().with(0, opens);
    for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
    {
        Opener opener;
        if (it->isMap())
        {
            if (!it->has("regex") || !compileRegex((*it)["regex"].asString(), opener.regex, error) ||
                (it->has("unless") && !compileRegex((*it)["unless"].asString(), opener.unless, error)))
            {
                if (error.empty())
                {
                    error = "an opener is a pattern, or a map with a regex and what it is not followed by";
                }
                return false;
            }
        }
        else if (!compileRegex(it->asString(), opener.regex, error))
        {
            return false;
        }
        out.push_back(std::move(opener));
    }
    return !out.empty();
}

// static
bool ALSyntaxGrammar::Impl::anyOpens(const std::vector<Opener>& openers, std::string_view before)
{
    for (const Opener& opener : openers)
    {
        // Only a match with nothing it may not be followed by at or after
        // where it starts: one past the last of those, where there are any.
        size_t from = 0;
        if (opener.unless.ok())
        {
            opener.unless.forEach(before, [&from](const ALRegexMatch& found) {
                from = found.begin() + 1;
                return true;
            }, 0);
        }
        if (opener.regex.search(before, nullptr, from))
        {
            return true;
        }
    }
    return false;
}

bool ALSyntaxGrammar::Impl::loadRule(const LLSD& in, const std::string& state_name, size_t index,
                                     std::vector<Target>& targets, std::string& error)
{
    const std::string where = "state '" + state_name + "' rule " + std::to_string(index) + ": ";
    if (!in.isMap())
    {
        error = where + "not a map";
        return false;
    }
    const S32 state_index = stateIndex(state_name);
    State&    state       = states[state_index];
    state.rules.emplace_back();
    Rule&        rule       = state.rules.back();
    const size_t rule_index = state.rules.size() - 1;

    if (in.has("kind"))
    {
        const std::optional<ALSyntaxKind> kind = alSyntaxKindFromName(in["kind"].asStringRef());
        if (!kind)
        {
            error = where + "unknown kind '" + in["kind"].asString() + "'";
            return false;
        }
        rule.kind = *kind;
    }
    rule.wholeWord = in["whole_word"].asBoolean();
    if ((in.has("not_after_chars") || in.has("consume")) && !in.has("regex"))
    {
        error = where + "not_after_chars and consume are for a regex";
        return false;
    }

    // What it matches.
    S32 matchers = 0;
    if (in.has("match"))
    {
        ++matchers;
        rule.match = Rule::Match::Literal;
        rule.text  = in["match"].asString();
        if (rule.text.empty())
        {
            error = where + "match is empty";
            return false;
        }
    }
    if (in.has("chars"))
    {
        ++matchers;
        rule.match = Rule::Match::Chars;
        std::string bad;
        if (!parseCharClass(in["chars"].asStringRef(), rule.chars, bad))
        {
            error = where + bad;
            return false;
        }
        rule.chars.unicode = in["unicode"].asBoolean();
        rule.minCount      = in.has("min") ? in["min"].asInteger() : 1;
        rule.maxCount      = in.has("max") ? in["max"].asInteger() : 0;
        if (rule.minCount < 1 || (rule.maxCount && rule.maxCount < rule.minCount))
        {
            error = where + "min and max do not make a range";
            return false;
        }
    }
    if (in.has("word"))
    {
        ++matchers;
        rule.match     = Rule::Match::Word;
        rule.qualified = in["qualified"].asBoolean();
        const LLSD& tables = in["tables"];
        for (LLSD::array_const_iterator it = tables.beginArray(); it != tables.endArray(); ++it)
        {
            const std::optional<ALSyntaxKind> kind = alSyntaxKindFromName((*it)["kind"].asStringRef());
            const std::string                 table = (*it)["table"].asString();
            if (!kind || table.empty())
            {
                error = where + "a table names a table and a kind";
                return false;
            }
            rule.tables.emplace_back(table, *kind);
            if (std::find(wordTables.begin(), wordTables.end(), table) == wordTables.end())
            {
                wordTables.push_back(table);
            }
        }
    }
    if (in.has("regex"))
    {
        ++matchers;
        rule.match = Rule::Match::Regex;
        if (!compileRegex(in["regex"].asString(), rule.regex, error))
        {
            error = where + error;
            return false;
        }
        rule.firstKnown = rule.regex.firstByteRange(rule.firstLo, rule.firstHi);
        // What RE2 has no lookaround for: the byte before, which may not be
        // one of these, and what follows the token, which the regex matches
        // and the rule does not take.
        if (in.has("not_after_chars"))
        {
            std::string bad;
            if (!parseCharClass(in["not_after_chars"].asStringRef(), rule.notAfter, bad))
            {
                error = where + bad;
                return false;
            }
            rule.hasNotAfter = true;
        }
        if (in.has("consume"))
        {
            rule.consume = static_cast<S32>(in["consume"].asInteger());
            if (rule.consume < 1 || rule.consume > rule.regex.groups())
            {
                error = where + "consume names a group of the regex";
                return false;
            }
        }
    }
    if (in.has("eol"))
    {
        ++matchers;
        rule.match = Rule::Match::Eol;
        // What, ending a line, carries the state on to the next rather
        // than letting the line's end end it: a C directive's backslash.
        rule.text = in["unless_after"].asString();
    }
    if (in.has("span") || in.has("span_regex"))
    {
        ++matchers;
        rule.match = Rule::Match::Span;
        if (in.has("span"))
        {
            rule.text = in["span"].asString();
        }
        else if (!compileRegex(in["span_regex"].asString(), rule.regex, error))
        {
            error = where + error;
            return false;
        }
        else
        {
            rule.firstKnown = rule.regex.firstByteRange(rule.firstLo, rule.firstHi);
        }
        if (rule.text.empty() && !rule.regex.ok())
        {
            error = where + "span is empty";
            return false;
        }
        if (!in.has("end") && !in.has("end_regex"))
        {
            error = where + "a span has an end";
            return false;
        }
        // The state the span is in while it is open: its escape, its end,
        // and whether a line end closes it.
        State inside;
        inside.name        = state_name + "/" + std::to_string(index);
        inside.defaultKind = rule.kind;
        if (in.has("escape"))
        {
            Rule escape;
            escape.match = Rule::Match::SpanEscape;
            escape.text  = in["escape"].asString();
            escape.kind  = ALSyntaxKind::Escape;
            inside.rules.push_back(std::move(escape));
        }
        Rule end;
        end.match    = Rule::Match::SpanEnd;
        end.kind     = rule.kind;
        end.then     = Rule::Then::Pop;
        end.endRegex = in.has("end_regex");
        end.text     = end.endRegex ? in["end_regex"].asString() : in["end"].asString();
        if (end.text.empty())
        {
            error = where + "the end of a span is empty";
            return false;
        }
        inside.rules.push_back(std::move(end));
        if (in.has("multiline") && !in["multiline"].asBoolean())
        {
            Rule eol;
            eol.match = Rule::Match::Eol;
            eol.then  = Rule::Then::Pop;
            inside.rules.push_back(std::move(eol));
        }
        // The rule pushes the state; the state is added after the rules
        // are read, so the reference is by name for now.
        rule.then = Rule::Then::Push;
        targets.push_back(Target{ state_index, rule_index, inside.name });
        states.push_back(std::move(inside));
        return true;
    }
    if (matchers != 1)
    {
        error = where + "a rule matches one thing: match, chars, word, regex, span or eol";
        return false;
    }

    // Where it goes.
    if (in.has("push"))
    {
        rule.then = Rule::Then::Push;
        targets.push_back(Target{ state_index, rule_index, in["push"].asString() });
    }
    else if (in.has("next"))
    {
        rule.then = Rule::Then::Next;
        targets.push_back(Target{ state_index, rule_index, in["next"].asString() });
    }
    else if (in["pop"].asBoolean())
    {
        rule.then = Rule::Then::Pop;
    }
    if (rule.match == Rule::Match::Eol && rule.then == Rule::Then::Stay)
    {
        error = where + "an eol rule goes somewhere";
        return false;
    }
    return true;
}

const ALRegex* ALSyntaxGrammar::Impl::endRegexFor(const Rule& rule, const std::string& payload) const
{
    std::string pattern = rule.text;
    for (size_t at = pattern.find("\\1"); at != std::string::npos; at = pattern.find("\\1", at))
    {
        const std::string escaped = ALRegex::escape(payload);
        pattern.replace(at, 2, escaped);
        at += escaped.size();
    }
    auto it = endRegexes.find(pattern);
    if (it == endRegexes.end())
    {
        if (endRegexes.size() >= 256)
        {
            endRegexes.clear();
        }
        ALRegex     compiled;
        std::string error;
        if (!compileRegex(pattern, compiled, error))
        {
            LL_WARNS("Syntax") << name << ": " << error << LL_ENDL;
        }
        it = endRegexes.emplace(pattern, std::move(compiled)).first;
    }
    return it->second.ok() ? &it->second : nullptr;
}

size_t ALSyntaxGrammar::Impl::tryRule(const Rule& rule, std::string_view line, size_t pos, const std::string& payload,
                                      const ALSyntaxWords& words, ALSyntaxKind& kind, std::string& capture) const
{
    const size_t npos = std::string_view::npos;
    const size_t len  = line.size();
    kind              = rule.kind;
    switch (rule.match)
    {
        case Rule::Match::Literal:
        case Rule::Match::SpanEscape:
        {
            if (line.compare(pos, rule.text.size(), rule.text) != 0)
            {
                return npos;
            }
            size_t end = pos + rule.text.size();
            if (rule.wholeWord && ((pos > 0 && wordByte(line[pos - 1])) || (end < len && wordByte(line[end]))))
            {
                return npos;
            }
            if (rule.match == Rule::Match::SpanEscape && end < len)
            {
                // The escape and the character it keeps.
                end = utf8str_decode_at(line, end).next;
            }
            return end;
        }
        case Rule::Match::Chars:
        {
            size_t end = pos;
            while (end < len && rule.chars.matches(static_cast<unsigned char>(line[end])) &&
                   (rule.maxCount == 0 || static_cast<S32>(end - pos) < rule.maxCount))
            {
                ++end;
            }
            return static_cast<S32>(end - pos) >= rule.minCount ? end : npos;
        }
        case Rule::Match::Word:
        {
            if (!wordStart.matches(static_cast<unsigned char>(line[pos])) || (pos > 0 && wordContinue.matches(static_cast<unsigned char>(line[pos - 1]))))
            {
                return npos;
            }
            size_t end = pos + 1;
            while (end < len && wordContinue.matches(static_cast<unsigned char>(line[end])))
            {
                ++end;
            }
            const std::string_view word = line.substr(pos, end - pos);
            // `head.word`, where the rule qualifies and there is a head.
            if (rule.qualified && pos >= 2 && line[pos - 1] == '.')
            {
                size_t head = pos - 1;
                while (head > 0 && wordContinue.matches(static_cast<unsigned char>(line[head - 1])))
                {
                    --head;
                }
                if (head < pos - 1 && wordStart.matches(static_cast<unsigned char>(line[head])))
                {
                    const std::string_view dotted = line.substr(head, end - head);
                    for (const auto& [table, table_kind] : rule.tables)
                    {
                        if (this->words.has(table, dotted) || words.has(table, dotted))
                        {
                            kind = table_kind;
                            return end;
                        }
                    }
                }
            }
            for (const auto& [table, table_kind] : rule.tables)
            {
                if (this->words.has(table, word) || words.has(table, word))
                {
                    kind = table_kind;
                    break;
                }
            }
            return end;
        }
        case Rule::Match::Span:
        {
            if (!rule.regex.ok())
            {
                return line.compare(pos, rule.text.size(), rule.text) == 0 ? pos + rule.text.size() : npos;
            }
            // Anchored where the lexer is, with the line before it read.
            ALRegexMatch found;
            if (!rule.mayStartWith(static_cast<unsigned char>(line[pos])) || !rule.regex.search(line, &found, pos, true, 1) ||
                found.length() == 0)
            {
                return npos;
            }
            capture = found.str(1);
            return found.end();
        }
        case Rule::Match::Regex:
        {
            if (!rule.mayStartWith(static_cast<unsigned char>(line[pos])) ||
                (rule.hasNotAfter && pos > 0 && rule.notAfter.matches(static_cast<unsigned char>(line[pos - 1]))))
            {
                return npos;
            }
            // Only the group the rule takes is asked for: none, mostly,
            // which the search answers quickest.
            ALRegexMatch found;
            if (!rule.regex.search(line, &found, pos, true, rule.consume) || found.length() == 0)
            {
                return npos;
            }
            if (rule.consume == 0)
            {
                return found.end();
            }
            if (found.begin(rule.consume) != pos || found.length(rule.consume) == 0)
            {
                return npos;
            }
            return found.end(rule.consume);
        }
        case Rule::Match::SpanEnd:
        {
            if (!rule.endRegex)
            {
                return line.compare(pos, rule.text.size(), rule.text) == 0 ? pos + rule.text.size() : npos;
            }
            const ALRegex* regex = endRegexFor(rule, payload);
            ALRegexMatch   found;
            if (!regex || !regex->search(line, &found, pos, true, 0) || found.length() == 0)
            {
                return npos;
            }
            return found.end();
        }
        case Rule::Match::Eol:
            return npos;
    }
    return npos;
}

ALSyntaxGrammar::ALSyntaxGrammar()
:   mImpl(std::make_unique<Impl>())
{
}

ALSyntaxGrammar::~ALSyntaxGrammar() = default;

bool ALSyntaxGrammar::load(const LLSD& description, std::string& error)
{
    auto impl = std::make_unique<Impl>();
    if (!description.isMap() || !description.has("states") || !description["states"].isMap())
    {
        error = "a grammar is a map with states";
        return false;
    }
    impl->name = description["name"].asString();
    if (impl->name.empty())
    {
        error = "a grammar has a name";
        return false;
    }
    impl->lineComment = description["line_comment"].asString();
    if (description.has("member_separators"))
    {
        impl->memberSeparators = description["member_separators"].asString();
    }
    impl->prose       = description["prose"].asBoolean();
    const LLSD& pairs = description["pairs"];
    for (LLSD::array_const_iterator it = pairs.beginArray(); it != pairs.endArray(); ++it)
    {
        const std::string pair = it->asString();
        if (pair.size() != 2)
        {
            error = "a pair is two characters, the opener and the closer";
            return false;
        }
        impl->pairs.emplace_back(pair[0], pair[1]);
    }
    if (description.has("indent"))
    {
        const LLSD& indent = description["indent"];
        if (!indent.isMap() || !indent.has("opens") || !indent.has("closes") ||
            !impl->compileOpeners(indent["opens"], impl->indentOpens, error) ||
            !impl->compileRegex(indent["closes"].asString(), impl->indentCloses, error))
        {
            if (error.empty())
            {
                error = "indent is a map with opens and closes";
            }
            return false;
        }
        if ((indent.has("once") && !impl->compileOpeners(indent["once"], impl->indentOnce, error)) ||
            (indent.has("once_except") && !impl->compileRegex(indent["once_except"].asString(), impl->indentOnceExcept, error)))
        {
            if (error.empty())
            {
                error = "indent's once is a pattern or a list of them, as opens is";
            }
            return false;
        }
        if (indent.has("ends"))
        {
            const LLSD& ends = indent["ends"];
            impl->blockEndWord = ends["word"].asString();
            if (impl->blockEndWord.empty() || !ends.has("after") || !impl->compileOpeners(ends["after"], impl->blockEnds, error))
            {
                if (error.empty())
                {
                    error = "indent's ends is a map of the word a block ends with and the openers, after, it ends";
                }
                return false;
            }
        }
        const LLSD& continues = indent["continues"];
        for (LLSD::array_const_iterator it = continues.beginArray(); it != continues.endArray(); ++it)
        {
            Impl::Continuation continuation;
            continuation.with = (*it)["with"].asString();
            if (!it->has("regex") || continuation.with.empty() || !impl->compileRegex((*it)["regex"].asString(), continuation.regex, error) ||
                (it->has("unless") && !impl->compileRegex((*it)["unless"].asString(), continuation.unless, error)))
            {
                if (error.empty())
                {
                    error = "a continuation is a map of a regex, what the new line begins with, and what ends the comment";
                }
                return false;
            }
            impl->continuations.push_back(std::move(continuation));
        }
    }
    if (description.has("folds"))
    {
        const LLSD& folds = description["folds"];
        for (const auto& [key, list] : { std::pair{ "opens", &impl->foldWords.opens }, std::pair{ "closes", &impl->foldWords.closes },
                                         std::pair{ "middles", &impl->foldWords.middles }, std::pair{ "joined", &impl->foldWords.joined } })
        {
            for (LLSD::array_const_iterator it = folds[key].beginArray(); it != folds[key].endArray(); ++it)
            {
                list->push_back(it->asString());
            }
        }
    }
    const LLSD& extensions = description["extensions"];
    for (LLSD::array_const_iterator it = extensions.beginArray(); it != extensions.endArray(); ++it)
    {
        impl->extensions.push_back(it->asString());
    }

    std::string bad;
    if (!parseCharClass(description.has("word_start") ? description["word_start"].asStringRef() : std::string_view("A-Za-z_"), impl->wordStart, bad) ||
        !parseCharClass(description.has("word_continue") ? description["word_continue"].asStringRef() : std::string_view("A-Za-z0-9_"), impl->wordContinue, bad))
    {
        error = "word characters: " + bad;
        return false;
    }
    impl->wordStart.unicode = impl->wordContinue.unicode = !description.has("word_unicode") || description["word_unicode"].asBoolean();

    // The words the file declares for its tables.
    const LLSD& declared = description["words"];
    for (LLSD::map_const_iterator it = declared.beginMap(); it != declared.endMap(); ++it)
    {
        std::vector<std::string> list;
        for (LLSD::array_const_iterator word = it->second.beginArray(); word != it->second.endArray(); ++word)
        {
            list.push_back(word->asString());
        }
        impl->words.set(it->first, std::move(list));
        if (std::find(impl->wordTables.begin(), impl->wordTables.end(), it->first) == impl->wordTables.end())
        {
            impl->wordTables.push_back(it->first);
        }
    }

    // Every state by name first, so a rule can name one declared after it.
    const LLSD& states = description["states"];
    for (LLSD::map_const_iterator it = states.beginMap(); it != states.endMap(); ++it)
    {
        Impl::State state;
        state.name = it->first;
        if (it->second.isMap() && it->second.has("default"))
        {
            const std::optional<ALSyntaxKind> kind = alSyntaxKindFromName(it->second["default"].asStringRef());
            if (!kind)
            {
                error = "state '" + state.name + "': unknown default kind '" + it->second["default"].asString() + "'";
                return false;
            }
            state.defaultKind = *kind;
        }
        impl->states.push_back(std::move(state));
    }
    const std::string initial = description.has("initial") ? description["initial"].asString() : "main";
    const S32 initial_index = impl->stateIndex(initial);
    if (initial_index < 0)
    {
        error = "no state '" + initial + "' to start in";
        return false;
    }
    impl->initial = static_cast<U16>(initial_index);

    // Then the rules, with what they push resolved once every state, the
    // spans' own included, is there.
    std::vector<Impl::Target> targets;
    for (LLSD::map_const_iterator it = states.beginMap(); it != states.endMap(); ++it)
    {
        const LLSD& rules = it->second.isMap() ? it->second["rules"] : it->second;
        size_t      index = 0;
        for (LLSD::array_const_iterator rule = rules.beginArray(); rule != rules.endArray(); ++rule, ++index)
        {
            if (!impl->loadRule(*rule, it->first, index, targets, error))
            {
                return false;
            }
        }
    }
    for (const Impl::Target& target : targets)
    {
        const S32 index = impl->stateIndex(target.name);
        if (index < 0)
        {
            error = "state '" + impl->states[target.state].name + "' goes to '" + target.name + "', which is not a state";
            return false;
        }
        impl->states[target.state].rules[target.rule].target = static_cast<U16>(index);
    }
    mImpl = std::move(impl);
    error.clear();
    return true;
}

std::shared_ptr<const ALSyntaxGrammar> ALSyntaxGrammar::fromFile(const std::string& path, std::string& error)
{
    llifstream file(path);
    if (!file.is_open())
    {
        error = "cannot open " + path;
        return nullptr;
    }
    LLSD description;
    if (!LLSDSerialize::deserialize(description, file, -1))
    {
        error = "cannot read " + path + " as LLSD";
        return nullptr;
    }
    auto grammar = std::make_shared<ALSyntaxGrammar>();
    if (!grammar->load(description, error))
    {
        error = path + ": " + error;
        return nullptr;
    }
    return grammar;
}

const std::string& ALSyntaxGrammar::name() const
{
    return mImpl->name;
}

const std::vector<std::string>& ALSyntaxGrammar::extensions() const
{
    return mImpl->extensions;
}

const std::vector<std::string>& ALSyntaxGrammar::wordTables() const
{
    return mImpl->wordTables;
}

void ALSyntaxGrammar::collectWords(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const
{
    mImpl->words.collect(prefix, out);
}

const std::string& ALSyntaxGrammar::lineComment() const
{
    return mImpl->lineComment;
}

const std::string& ALSyntaxGrammar::memberSeparators() const
{
    return mImpl->memberSeparators;
}

const std::vector<std::pair<char, char>>& ALSyntaxGrammar::pairs() const
{
    return mImpl->pairs;
}

bool ALSyntaxGrammar::prose() const
{
    return mImpl->prose;
}

bool ALSyntaxGrammar::indents() const
{
    return !mImpl->indentOpens.empty() && mImpl->indentCloses.ok();
}

bool ALSyntaxGrammar::opensBlock(std::string_view before) const
{
    return Impl::anyOpens(mImpl->indentOpens, before);
}

bool ALSyntaxGrammar::opensOnce(std::string_view before) const
{
    return Impl::anyOpens(mImpl->indentOnce, before);
}

bool ALSyntaxGrammar::keptLevelWithOnce(std::string_view text) const
{
    return mImpl->indentOnceExcept.ok() && mImpl->indentOnceExcept.search(text, nullptr, 0, true, 0);
}

const std::string& ALSyntaxGrammar::blockEnd(std::string_view before) const
{
    static const std::string NONE;
    return Impl::anyOpens(mImpl->blockEnds, before) ? mImpl->blockEndWord : NONE;
}

const ALSyntaxGrammar::FoldWords& ALSyntaxGrammar::foldWords() const
{
    return mImpl->foldWords;
}

bool ALSyntaxGrammar::endsComment(std::string_view text) const
{
    for (const Impl::Continuation& continuation : mImpl->continuations)
    {
        if (continuation.unless.ok() && continuation.unless.search(text, nullptr, 0))
        {
            return true;
        }
    }
    return false;
}

const std::string& ALSyntaxGrammar::commentContinues(std::string_view text) const
{
    static const std::string NONE;
    for (const Impl::Continuation& continuation : mImpl->continuations)
    {
        if (continuation.regex.search(text, nullptr, 0, true, 0) && !(continuation.unless.ok() && continuation.unless.search(text, nullptr, 0)))
        {
            return continuation.with;
        }
    }
    return NONE;
}

size_t ALSyntaxGrammar::closesBlock(std::string_view text) const
{
    ALRegexMatch found;
    if (!mImpl->indentCloses.search(text, &found, 0, true, 0))
    {
        return 0;
    }
    return found.length();
}

size_t ALSyntaxGrammar::cachedEndPatterns() const
{
    return mImpl->endRegexes.size();
}

ALSyntaxState ALSyntaxGrammar::initialState() const
{
    ALSyntaxState state;
    state.frames.push_back(ALSyntaxState::Frame{ mImpl->initial, std::string() });
    return state;
}

void ALSyntaxGrammar::lexLine(std::string_view line, ALSyntaxState& state, std::vector<ALSyntaxToken>& tokens, const ALSyntaxWords& words) const
{
    typedef Impl::Rule Rule;
    tokens.clear();
    if (state.frames.empty() || mImpl->states.empty())
    {
        state = initialState();
    }
    const size_t len = line.size();

    auto emit = [&tokens](size_t begin, size_t end, ALSyntaxKind kind) {
        if (end <= begin)
        {
            return;
        }
        if (!tokens.empty() && tokens.back().kind == kind && tokens.back().end == static_cast<S32>(begin))
        {
            tokens.back().end = static_cast<S32>(end);
        }
        else
        {
            tokens.push_back(ALSyntaxToken{ static_cast<S32>(begin), static_cast<S32>(end), kind });
        }
    };
    auto go = [&state](const Rule& rule, std::string capture) {
        switch (rule.then)
        {
            case Rule::Then::Push:
                // Past the depth, where it is: what nests past reason lexes
                // as the state it is in.
                if (state.frames.size() < MAX_DEPTH)
                {
                    state.frames.push_back(ALSyntaxState::Frame{ rule.target, std::move(capture) });
                }
                break;
            case Rule::Then::Pop:
                if (state.frames.size() > 1)
                {
                    state.frames.pop_back();
                }
                break;
            case Rule::Then::Next:
                state.frames.back() = ALSyntaxState::Frame{ rule.target, std::move(capture) };
                break;
            case Rule::Then::Stay:
                break;
        }
    };

    size_t pos = 0;
    while (pos < len)
    {
        const ALSyntaxState::Frame& frame = state.frames.back();
        const Impl::State&          current = mImpl->states[frame.state];
        const Rule*                 hit     = nullptr;
        size_t                      end     = std::string_view::npos;
        ALSyntaxKind                kind    = current.defaultKind;
        std::string                 capture;
        for (const Rule& rule : current.rules)
        {
            if (rule.match == Rule::Match::Eol)
            {
                continue;
            }
            end = mImpl->tryRule(rule, line, pos, frame.payload, words, kind, capture);
            if (end != std::string_view::npos)
            {
                hit = &rule;
                break;
            }
        }
        if (!hit)
        {
            // One character of what the state is made of.
            const size_t next = utf8str_decode_at(line, pos).next;
            emit(pos, next, current.defaultKind);
            pos = next;
            continue;
        }
        emit(pos, end, kind);
        go(*hit, std::move(capture));
        pos = end;
    }

    // The line's end: whatever the state does with one, a few times over
    // for states that leave one another.
    for (S32 guard = 0; guard < 8; ++guard)
    {
        const Impl::State& current = mImpl->states[state.frames.back().state];
        const Rule*        eol     = nullptr;
        for (const Rule& rule : current.rules)
        {
            if (rule.match == Rule::Match::Eol)
            {
                eol = &rule;
                break;
            }
        }
        if (!eol)
        {
            break;
        }
        if (!eol->text.empty())
        {
            std::string_view kept = line;
            while (!kept.empty() && (kept.back() == ' ' || kept.back() == '\t'))
            {
                kept.remove_suffix(1);
            }
            if (kept.size() >= eol->text.size() && kept.compare(kept.size() - eol->text.size(), eol->text.size(), eol->text) == 0)
            {
                break;
            }
        }
        go(*eol, std::string());
    }
}

// --- the library -------------------------------------------------------------

S32 ALSyntaxLibrary::loadDirectory(const std::string& directory)
{
    S32 loaded = 0;
    for (const std::string& file : gDirUtilp->getFilesInDir(directory))
    {
        if (file.size() < 4 || file.compare(file.size() - 4, 4, ".xml") != 0)
        {
            continue;
        }
        std::string error;
        if (loadFile(gDirUtilp->add(directory, file), error))
        {
            ++loaded;
        }
        else
        {
            LL_WARNS("Syntax") << error << LL_ENDL;
        }
    }
    return loaded;
}

bool ALSyntaxLibrary::loadFile(const std::string& path, std::string& error)
{
    std::shared_ptr<const ALSyntaxGrammar> grammar = ALSyntaxGrammar::fromFile(path, error);
    if (!grammar)
    {
        return false;
    }
    add(std::move(grammar));
    return true;
}

void ALSyntaxLibrary::add(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    mGrammars[grammar->name()] = std::move(grammar);
}

std::shared_ptr<const ALSyntaxGrammar> ALSyntaxLibrary::find(std::string_view name) const
{
    const auto it = mGrammars.find(name);
    return it == mGrammars.end() ? nullptr : it->second;
}

std::shared_ptr<const ALSyntaxGrammar> ALSyntaxLibrary::forExtension(std::string_view extension) const
{
    if (!extension.empty() && extension.front() == '.')
    {
        extension.remove_prefix(1);
    }
    for (const auto& [name, grammar] : mGrammars)
    {
        for (const std::string& known : grammar->extensions())
        {
            if (known == extension)
            {
                return grammar;
            }
        }
    }
    return nullptr;
}

std::vector<std::string> ALSyntaxLibrary::names() const
{
    std::vector<std::string> out;
    for (const auto& [name, grammar] : mGrammars)
    {
        out.push_back(name);
    }
    return out;
}

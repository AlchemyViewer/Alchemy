/**
 * @file alscriptfixes.cpp
 * @brief What would put a problem right, made from what the analyzers say.
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

#include "alscriptfixes.h"

#include "llstring.h"

#include <algorithm>
#include <cctype>

namespace
{
    // A fix's words, keyed as a problem's are: the English is what a skin
    // without the key shows, and what the tests read.
    ALScriptFix titled(const char* key, const char* english, std::vector<std::string> args)
    {
        ALScriptFix fix;
        fix.key   = key;
        fix.title = ALScriptProblem::fill(english, args);
        fix.args  = std::move(args);
        return fix;
    }

    // The text's lines, each without its break, and where each begins.
    class Lines
    {
    public:
        explicit Lines(std::string_view text) : mText(text)
        {
            mStarts.push_back(0);
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\n')
                {
                    mStarts.push_back(i + 1);
                }
            }
        }

        std::string_view line(S32 n) const
        {
            if (n < 0 || static_cast<size_t>(n) >= mStarts.size())
            {
                return {};
            }
            const size_t begin = mStarts[n];
            size_t       end   = static_cast<size_t>(n) + 1 < mStarts.size() ? mStarts[n + 1] - 1 : mText.size();
            if (end > begin && mText[end - 1] == '\r')
            {
                --end;
            }
            return mText.substr(begin, end - begin);
        }

        // Where a place is in the text, or nothing for a place past its
        // line's end or past the last line.
        std::optional<size_t> offsetOf(S32 line, S32 column) const
        {
            if (column < 0 || static_cast<size_t>(column) > this->line(line).size() || line < 0 || static_cast<size_t>(line) >= mStarts.size())
            {
                return std::nullopt;
            }
            return mStarts[line] + static_cast<size_t>(column);
        }

    private:
        std::string_view    mText;
        std::vector<size_t> mStarts;
    };

    bool identifierByte(char c)
    {
        return isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    bool isIdentifier(std::string_view word)
    {
        return !word.empty() && !isdigit(static_cast<unsigned char>(word.front())) && std::all_of(word.begin(), word.end(), identifierByte);
    }

    // A name and its members, `math.abs` or `LLEvents:on`.
    bool isDottedName(std::string_view name)
    {
        size_t from = 0;
        for (;;)
        {
            const size_t cut = name.find_first_of(".:", from);
            if (!isIdentifier(name.substr(from, cut == std::string_view::npos ? std::string_view::npos : cut - from)))
            {
                return false;
            }
            if (cut == std::string_view::npos)
            {
                return true;
            }
            from = cut + 1;
        }
    }

    // `word` as a whole word in line[from, to): the first, or the last;
    // -1 for none.
    S32 findWord(std::string_view line, std::string_view word, S32 from, S32 to, bool last)
    {
        from     = std::max<S32>(0, from);
        to       = std::min<S32>(static_cast<S32>(line.size()), to);
        S32 seen = -1;
        for (size_t at = line.find(word, static_cast<size_t>(from)); at != std::string_view::npos && at + word.size() <= static_cast<size_t>(to);
             at      = line.find(word, at + 1))
        {
            const bool starts = at == 0 || !identifierByte(line[at - 1]);
            const bool ends   = at + word.size() >= line.size() || !identifierByte(line[at + word.size()]);
            if (starts && ends)
            {
                seen = static_cast<S32>(at);
                if (!last)
                {
                    break;
                }
            }
        }
        return seen;
    }

    // `';'` as the syntax error words it, and the `;` it stands for.
    std::string unquoted(const std::string& token)
    {
        return token.size() >= 3 && token.front() == '\'' && token.back() == '\'' ? token.substr(1, token.size() - 2) : std::string();
    }

    // The name at a problem's place put right: the one within the stretch
    // the problem marks -- the last there, for the member after its table
    // -- or failing that the first of it on its line from where the
    // problem starts.
    void changeName(ALScriptProblem& problem, const Lines& lines, const std::string& was, const std::string& now, ALScriptFix fix, bool last)
    {
        const std::string_view line = lines.line(problem.line);
        const S32              end  = problem.endLine == problem.line ? problem.endColumn : static_cast<S32>(line.size());
        S32                    at   = findWord(line, was, problem.column, end, last);
        if (at < 0)
        {
            at = findWord(line, was, problem.column, static_cast<S32>(line.size()), false);
        }
        if (at < 0)
        {
            return;
        }
        fix.edits.push_back({ problem.line, at, problem.line, at + static_cast<S32>(was.size()), now });
        problem.fixes.push_back(std::move(fix));
    }

    // A comment on a line: where its words begin and end, and whether it
    // runs to the line's end, as `//` and `--` do.
    struct Comment
    {
        size_t begin     = 0;
        size_t end       = 0;
        bool   toLineEnd = false;
    };

    // Lua's long bracket at `at`, `[[` or `[==[`: its level, or -1.
    int longBracket(std::string_view line, size_t at)
    {
        if (at >= line.size() || line[at] != '[')
        {
            return -1;
        }
        size_t level = 0;
        while (at + 1 + level < line.size() && line[at + 1 + level] == '=')
        {
            ++level;
        }
        return at + 1 + level < line.size() && line[at + 1 + level] == '[' ? static_cast<int>(level) : -1;
    }

    // The comments of one line, past its strings. A comment or a string
    // begun on a line before is not seen, which is no loss: a NOLINT is
    // said at the end of the line it is about.
    std::vector<Comment> commentsOf(std::string_view line, bool lua)
    {
        std::vector<Comment> out;
        char                 quote = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (quote)
            {
                if (c == '\\')
                {
                    ++i;
                }
                else if (c == quote)
                {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || (lua && (c == '\'' || c == '`')))
            {
                quote = c;
                continue;
            }
            if (lua)
            {
                // A long string is passed over whole: a `--` in one is
                // no comment.
                if (const int level = longBracket(line, i); level >= 0)
                {
                    const std::string closer = "]" + std::string(static_cast<size_t>(level), '=') + "]";
                    const size_t      end    = line.find(closer, i + static_cast<size_t>(level) + 2);
                    if (end == std::string_view::npos)
                    {
                        break;
                    }
                    i = end + closer.size() - 1;
                    continue;
                }
                if (c == '-' && i + 1 < line.size() && line[i + 1] == '-')
                {
                    if (const int level = longBracket(line, i + 2); level >= 0)
                    {
                        const std::string closer = "]" + std::string(static_cast<size_t>(level), '=') + "]";
                        const size_t      begin  = i + 4 + static_cast<size_t>(level);
                        const size_t      end    = line.find(closer, begin);
                        out.push_back({ begin, end == std::string_view::npos ? line.size() : end, false });
                        if (end == std::string_view::npos)
                        {
                            break;
                        }
                        i = end + closer.size() - 1;
                        continue;
                    }
                    out.push_back({ i + 2, line.size(), true });
                    break;
                }
            }
            else if (c == '/' && i + 1 < line.size())
            {
                if (line[i + 1] == '/')
                {
                    out.push_back({ i + 2, line.size(), true });
                    break;
                }
                if (line[i + 1] == '*')
                {
                    const size_t end = line.find("*/", i + 2);
                    out.push_back({ i + 2, end == std::string_view::npos ? line.size() : end, false });
                    if (end == std::string_view::npos)
                    {
                        break;
                    }
                    i = end + 1;
                }
            }
        }
        return out;
    }

    // Where `marker` stands as a word of its own in `words` -- NOLINT, and
    // not the start of NOLINTNEXTLINE -- or npos.
    size_t markerIn(std::string_view words, std::string_view marker)
    {
        for (size_t at = words.find(marker); at != std::string_view::npos; at = words.find(marker, at + 1))
        {
            const bool starts = at == 0 || !identifierByte(words[at - 1]);
            const bool ends   = at + marker.size() >= words.size() || !identifierByte(words[at + marker.size()]);
            if (starts && ends)
            {
                return at;
            }
        }
        return std::string_view::npos;
    }

    // Whether a line's comments say `marker` for the problem: bare, or
    // with its name among those in the brackets after it.
    bool says(std::string_view line, std::string_view marker, const std::string& name, const std::string& code, bool lua)
    {
        for (const Comment& comment : commentsOf(line, lua))
        {
            const std::string_view words = line.substr(comment.begin, comment.end - comment.begin);
            const size_t           at    = markerIn(words, marker);
            if (at == std::string_view::npos)
            {
                continue;
            }
            const size_t open = at + marker.size();
            if (open >= words.size() || words[open] != '(')
            {
                return true;
            }
            const size_t close = words.find(')', open);
            if (close == std::string_view::npos)
            {
                continue;
            }
            std::string_view names = words.substr(open + 1, close - open - 1);
            while (!names.empty())
            {
                const size_t     comma = names.find(',');
                std::string_view one   = names.substr(0, comma);
                while (!one.empty() && isspace(static_cast<unsigned char>(one.front())))
                {
                    one.remove_prefix(1);
                }
                while (!one.empty() && isspace(static_cast<unsigned char>(one.back())))
                {
                    one.remove_suffix(1);
                }
                if (!one.empty() && (one == name || (!code.empty() && one == code)))
                {
                    return true;
                }
                names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
            }
        }
        return false;
    }
}

namespace ALScriptFixes
{
    size_t editDistance(std::string_view a, std::string_view b)
    {
        std::vector<size_t> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j)
        {
            row[j] = j;
        }
        for (size_t i = 1; i <= a.size(); ++i)
        {
            size_t previous = row[0];
            row[0]          = i;
            for (size_t j = 1; j <= b.size(); ++j)
            {
                const size_t was  = row[j];
                const bool   same = LLStringOps::toLower(a[i - 1]) == LLStringOps::toLower(b[j - 1]);
                row[j]            = std::min({ row[j] + 1, row[j - 1] + 1, previous + (same ? 0 : 1) });
                previous          = was;
            }
        }
        return row[b.size()];
    }

    std::string nearest(std::string_view word, const std::vector<std::string>& names)
    {
        std::string best;
        size_t      best_distance = std::max<size_t>(2, word.size() / 2) + 1;
        for (const std::string& name : names)
        {
            if (name == word)
            {
                continue;
            }
            const size_t distance = editDistance(name, word);
            if (distance < best_distance || (distance == best_distance && !best.empty() && name.size() < best.size()))
            {
                best          = name;
                best_distance = distance;
            }
        }
        return best;
    }

    void offerName(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::string& now)
    {
        if (!isIdentifier(was) || !isIdentifier(now))
        {
            return;
        }
        ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { was, now });
        fix.preferred   = true;
        changeName(problem, Lines(text), was, now, std::move(fix), false);
    }

    void attach(ALScriptProblems& problems, std::string_view text, bool lua)
    {
        const Lines lines(text);
        for (ALScriptProblem& problem : problems)
        {
            const std::string&              key  = problem.key;
            const std::vector<std::string>& args = problem.args;
            if (!lua && key == "LSLSyntaxMissing" && args.size() == 1)
            {
                // Put in after the last thing before where the parser
                // stopped, which is the one character the problem marks.
                const std::string token = unquoted(args[0]);
                if (!token.empty() && problem.endLine == problem.line && problem.endColumn == problem.column + 1 &&
                    lines.offsetOf(problem.line, problem.endColumn))
                {
                    ALScriptFix fix = titled("ScriptFixInsert", "Insert '[1]'", { token });
                    fix.preferred   = true;
                    fix.edits.push_back({ problem.line, problem.endColumn, problem.line, problem.endColumn, token });
                    problem.fixes.push_back(std::move(fix));
                }
            }
            else if (!lua && key == "LSLUndeclaredWithSuggestion" && args.size() == 2 && isIdentifier(args[0]) && isIdentifier(args[1]))
            {
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[1] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[1], std::move(fix), false);
            }
            else if (!lua && key == "LSLDeprecatedWithReplacement" && args.size() == 2 && isIdentifier(args[0]) && isIdentifier(args[1]))
            {
                // Only where the replacement is one name: Tailslide says
                // some in prose ("llPlaySound, llLoopSound, or
                // llTriggerSound"), which is the scripter's to choose among.
                // Not safe: a replacement does its own thing, not the
                // deprecated one's.
                ALScriptFix fix = titled("ScriptFixUseInstead", "Use '[2]' instead of '[1]'", { args[0], args[1] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[1], std::move(fix), false);
            }
            else if (lua && (key == "LuauLintLocalUnused" || key == "LuauLintFunctionUnused" || key == "LuauLintImportUnused") && args.size() == 1 &&
                     isIdentifier(args[0]) && args[0].front() != '_')
            {
                // Marked unused as the lint asks: nothing reads the name, so
                // where it is declared is the only place it stands, and the
                // script does as it did.
                const std::string_view line = lines.line(problem.line);
                if (problem.column >= 0 && findWord(line, args[0], problem.column, problem.column + static_cast<S32>(args[0].size()), false) == problem.column)
                {
                    ALScriptFix fix = titled("ScriptFixUnused", "Rename to '_[1]'", { args[0] });
                    fix.preferred   = true;
                    fix.safe        = true;
                    fix.edits.push_back({ problem.line, problem.column, problem.line, problem.column, "_" });
                    problem.fixes.push_back(std::move(fix));
                }
            }
            else if (lua && key == "LuauKeyNotFoundDidYouMean" && args.size() == 3 && isIdentifier(args[0]) && isIdentifier(args[2]))
            {
                // The key after its table: the last of its name where the
                // problem is, `ll.Sya` being the stretch it marks.
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[2] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[2], std::move(fix), true);
            }
            else if (lua && (key == "LuauMissingPropertyDidYouMean" || key == "LuauMissingExternPropertyDidYouMean") && args.size() == 3 &&
                     isIdentifier(args[0]) && isIdentifier(args[2]))
            {
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[2] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[2], std::move(fix), true);
            }
            else if (lua &&
                     (key == "LuauLintDeprecatedGlobal" || key == "LuauLintDeprecatedFunctionUse" || key == "LuauLintDeprecatedFunctionUseReason" ||
                      key == "LuauLintDeprecatedMemberUse" || key == "LuauLintDeprecatedMemberUseReason") &&
                     args.size() >= 2 && isDottedName(args[0]) && isDottedName(args[1]))
            {
                // What the definitions say to use instead, where they name
                // one thing, put in place of the whole of what was written
                // -- and only where that is the name the lint said, as it
                // is for `ll.Abs` and not for an alias of it. Not safe: the
                // reason usually says how the two differ.
                if (problem.endLine == problem.line)
                {
                    const std::string_view line = lines.line(problem.line);
                    if (problem.column >= 0 && problem.endColumn <= static_cast<S32>(line.size()) &&
                        line.substr(problem.column, problem.endColumn - problem.column) == args[0])
                    {
                        ALScriptFix fix = titled("ScriptFixUseInstead", "Use '[2]' instead of '[1]'", { args[0], args[1] });
                        fix.preferred   = true;
                        fix.edits.push_back({ problem.line, problem.column, problem.line, problem.endColumn, args[1] });
                        problem.fixes.push_back(std::move(fix));
                    }
                }
            }
        }
    }

    std::optional<std::string> apply(std::string_view text, const ALScriptFix& fix)
    {
        struct Span
        {
            size_t             begin = 0;
            size_t             end   = 0;
            const std::string* with  = nullptr;
        };
        const Lines       lines(text);
        std::vector<Span> spans;
        for (const ALScriptEdit& edit : fix.edits)
        {
            const std::optional<size_t> begin = lines.offsetOf(edit.line, edit.column);
            const std::optional<size_t> end   = lines.offsetOf(edit.endLine, edit.endColumn);
            if (!begin || !end || *end < *begin)
            {
                return std::nullopt;
            }
            spans.push_back({ *begin, *end, &edit.text });
        }
        // In the text's order; two insertions at one place in the order
        // the fix gives them.
        std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
        std::string out;
        out.reserve(text.size());
        size_t at = 0;
        for (const Span& span : spans)
        {
            if (span.begin < at)
            {
                return std::nullopt;
            }
            out.append(text.substr(at, span.begin - at));
            out.append(*span.with);
            at = span.end;
        }
        out.append(text.substr(at));
        return out;
    }

    std::string lintName(const ALScriptProblem& problem, bool lua)
    {
        // A lint, whatever level the scripter set it at: one made an error
        // is still theirs to say is wanted here. A parse or a type error is
        // not a lint, and no comment makes it right.
        if (problem.source != ALScriptProblem::Source::Lint)
        {
            return std::string();
        }
        if (lua)
        {
            return problem.code;
        }
        return problem.key.size() > 3 && problem.key.compare(0, 3, "LSL") == 0 ? problem.key.substr(3) : problem.code;
    }

    bool suppressed(const ALScriptProblem& problem, std::string_view line, std::string_view before, bool lua)
    {
        const std::string name = lintName(problem, lua);
        if (name.empty())
        {
            return false;
        }
        // An LSL warning answers to its number too, which is what the
        // Lints preferences and a .lslrc call it by.
        const std::string code = lua ? std::string() : problem.code;
        return says(line, "NOLINT", name, code, lua) || says(before, "NOLINTNEXTLINE", name, code, lua);
    }

    std::optional<ALScriptFix> suppression(const ALScriptProblem& problem, std::string_view line, bool lua)
    {
        const std::string name = lintName(problem, lua);
        if (name.empty())
        {
            return std::nullopt;
        }
        ALScriptFix fix = titled("ScriptFixSuppress", "Suppress '[1]' on this line", { name });
        fix.kind        = ALScriptFix::Kind::Suppress;
        // A comment changes nothing the script does; but it is never
        // preferred, the problem being worth putting right first.
        fix.safe = true;
        const std::vector<Comment> comments = commentsOf(line, lua);
        // A NOLINT the line has: the name among those it gives. A bare one
        // gives every name already.
        for (const Comment& comment : comments)
        {
            const std::string_view words = line.substr(comment.begin, comment.end - comment.begin);
            const size_t           at    = markerIn(words, "NOLINT");
            if (at == std::string_view::npos)
            {
                continue;
            }
            const size_t open = at + 6;
            if (open >= words.size() || words[open] != '(')
            {
                return std::nullopt;
            }
            const size_t close = words.find(')', open);
            if (close == std::string_view::npos)
            {
                continue;
            }
            const S32 column = static_cast<S32>(comment.begin + close);
            fix.edits.push_back({ problem.line, column, problem.line, column, close == open + 1 ? name : ", " + name });
            return fix;
        }
        // Said in the comment the line ends in, or in one of its own.
        const S32 end = static_cast<S32>(line.size());
        if (!comments.empty() && comments.back().toLineEnd)
        {
            const bool spaced = !line.empty() && isspace(static_cast<unsigned char>(line.back()));
            fix.edits.push_back({ problem.line, end, problem.line, end, std::string(spaced ? "" : " ") + "NOLINT(" + name + ")" });
        }
        else
        {
            const bool spaced = line.empty() || isspace(static_cast<unsigned char>(line.back()));
            fix.edits.push_back({ problem.line, end, problem.line, end, std::string(spaced ? "" : "  ") + (lua ? "-- " : "// ") + "NOLINT(" + name + ")" });
        }
        return fix;
    }
}

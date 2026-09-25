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
#include <cstring>

// Every problem a fix is offered for, by its key: named once here, and
// compared only through `is`, so that each is a key strings.xml has --
// scripts/content_tools/check_script_strings.py sees to it -- and a key
// renamed where it is made cannot quietly take its fix away.
#define AL_FIXED_KEYS(X) \
    X(LSLArgumentWrongType) \
    X(LSLAssignmentInComparison) \
    X(LSLBadReturnType) \
    X(LSLChangeToCurrentState) \
    X(LSLDeprecatedWithReplacement) \
    X(LSLEqAsStatement) \
    X(LSLIntFloatMulAssign) \
    X(LSLInvalidOperator) \
    X(LSLSyntaxMissing) \
    X(LSLUndeclaredWithSuggestion) \
    X(LSLWrongTypeInAssignment) \
    X(LuauKeyNotFoundDidYouMean) \
    X(LuauLintDeprecatedFunctionUse) \
    X(LuauLintDeprecatedFunctionUseReason) \
    X(LuauLintDeprecatedGlobal) \
    X(LuauLintDeprecatedMemberUse) \
    X(LuauLintDeprecatedMemberUseReason) \
    X(LuauLintDirectiveNolintUnknownDidYouMean) \
    X(LuauLintDirectiveUnknownDidYouMean) \
    X(LuauLintFunctionUnused) \
    X(LuauLintGlobalNeverRead) \
    X(LuauLintGlobalUsedAsLocalFunction) \
    X(LuauLintGlobalUsedAsLocalLine) \
    X(LuauLintImportUnused) \
    X(LuauLintLocalUnused) \
    X(LuauLintUninitializedLocal) \
    X(LuauMissingExternPropertyDidYouMean) \
    X(LuauMissingPropertyDidYouMean) \
    X(LuauNotTakeSelf) \
    X(LuauRequiresSelf) \
    X(LuauTypeMismatch) \
    X(LuauTypeMismatchReason) \
    X(OptimizerEvaluated) \
    X(OptimizerFolded) \
    X(OptimizerRemovedFunction) \
    X(OptimizerRemovedGlobal) \
    X(OptimizerRemovedLocal) \
    X(OptimizerRemovedNoEffect) \
    X(OptimizerRemovedState) \
    X(OptimizerRemovedUnreachable) \
    X(OptimizerSimplified) \
    X(OptimizerWroteAs)

namespace
{
    using ALScriptFixes::titled;

    enum class Fixed : U8
    {
#define AL_FIXED_ENUM(name) name,
        AL_FIXED_KEYS(AL_FIXED_ENUM)
#undef AL_FIXED_ENUM
    };

    constexpr std::string_view FIXED_KEYS[] = {
#define AL_FIXED_NAME(name) #name,
        AL_FIXED_KEYS(AL_FIXED_NAME)
#undef AL_FIXED_NAME
    };

    bool is(std::string_view key, Fixed which)
    {
        return key == FIXED_KEYS[static_cast<size_t>(which)];
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

        S32 count() const { return static_cast<S32>(mStarts.size()); }

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

    // `'string'` as a type error words it, and the type it names.
    std::string typeNamed(const std::string& said)
    {
        return said.size() >= 2 && said.front() == '\'' && said.back() == '\'' ? said.substr(1, said.size() - 2) : said;
    }

    // Whether LSL casts one type to another: to a string or a list from
    // anything, from a string to anything but a list, between integer and
    // float. What it does not, no fix offers.
    bool castable(const std::string& from, const std::string& to)
    {
        static const char* const TYPES[] = { "integer", "float", "string", "key", "vector", "rotation", "list" };
        const auto known = [](const std::string& type) { return std::find(std::begin(TYPES), std::end(TYPES), type) != std::end(TYPES); };
        if (from == to || !known(from) || !known(to))
        {
            return false;
        }
        if (to == "string" || to == "list")
        {
            return true;
        }
        if (from == "string")
        {
            return true;
        }
        return (from == "integer" && to == "float") || (from == "float" && to == "integer");
    }

    // An expression with something wrapped round it: bare where it is one
    // name, number or string, which binds tighter than anything, and in
    // brackets otherwise.
    std::string wrapped(const std::string& before, std::string_view expression, const std::string& after, bool bare_ok)
    {
        const bool simple = !expression.empty() &&
                            (std::all_of(expression.begin(), expression.end(), [](char c) { return identifierByte(c) || c == '.'; }) ||
                             (expression.size() >= 2 && expression.front() == '"' && expression.back() == '"' &&
                              expression.substr(1, expression.size() - 2).find('"') == std::string_view::npos));
        return simple && bare_ok ? before + std::string(expression) + after : before + "(" + std::string(expression) + ")" + after;
    }

    // Blanks off both ends of a stretch of a line, as columns.
    void trimmed(std::string_view line, S32& from, S32& to)
    {
        while (from < to && isspace(static_cast<unsigned char>(line[from])))
        {
            ++from;
        }
        while (to > from && (isspace(static_cast<unsigned char>(line[to - 1])) || line[to - 1] == ';'))
        {
            --to;
        }
    }

    // Where a lone `=` stands in a stretch of a line -- not `==`, `<=`,
    // `>=` or `!=`, nor one in a string -- or -1.
    S32 loneEquals(std::string_view line, S32 from, S32 to)
    {
        char quote = 0;
        for (S32 i = llmax(0, from); i < llmin(to, static_cast<S32>(line.size())); ++i)
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
            if (c == '"' || c == '\'')
            {
                quote = c;
                continue;
            }
            if (c == '=' && (i + 1 >= static_cast<S32>(line.size()) || line[i + 1] != '=') && (i == 0 || !strchr("=<>!~+-*/%&|^", line[i - 1])))
            {
                return i;
            }
        }
        return -1;
    }

    // The arguments of the call a stretch of a line holds, each as its
    // columns, blanks off: split at the commas outside brackets, strings,
    // and LSL's vector and rotation literals -- a `<` where an argument
    // or an element begins.
    std::vector<std::pair<S32, S32>> callArguments(std::string_view line, S32 from, S32 to)
    {
        std::vector<std::pair<S32, S32>> out;
        const size_t                     open = line.find('(', static_cast<size_t>(llmax(0, from)));
        if (open == std::string_view::npos || static_cast<S32>(open) >= to)
        {
            return out;
        }
        std::vector<char> closers;
        char              quote       = 0;
        S32               begin       = static_cast<S32>(open) + 1;
        bool              at_start    = true;
        const S32         end         = llmin(to, static_cast<S32>(line.size()));
        for (S32 i = begin; i < end; ++i)
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
            if (isspace(static_cast<unsigned char>(c)))
            {
                continue;
            }
            const bool starting = at_start;
            at_start            = false;
            if (c == '"')
            {
                quote = c;
            }
            else if (c == '(' || c == '[')
            {
                closers.push_back(c == '(' ? ')' : ']');
                at_start = true;
            }
            else if (c == '<' && starting)
            {
                closers.push_back('>');
                at_start = true;
            }
            else if (!closers.empty() && c == closers.back())
            {
                closers.pop_back();
            }
            else if (closers.empty() && (c == ',' || c == ')'))
            {
                S32 a = begin, b = i;
                trimmed(line, a, b);
                if (b > a)
                {
                    out.emplace_back(a, b);
                }
                if (c == ')')
                {
                    return out;
                }
                begin    = i + 1;
                at_start = true;
            }
            else if (c == ',')
            {
                at_start = true;
            }
        }
        return out;
    }

    // A cast of the expression in [from, to) of a line to `to`, where LSL
    // has one from `from`.
    bool offerCast(ALScriptProblem& problem, std::string_view line, S32 from, S32 to, const std::string& from_type, const std::string& to_type)
    {
        trimmed(line, from, to);
        if (to <= from || !castable(from_type, to_type))
        {
            return false;
        }
        const std::string_view expression = line.substr(from, to - from);
        ALScriptFix            fix        = titled("ScriptFixCast", "Cast to [1]", { to_type });
        fix.preferred                     = true;
        fix.edits.push_back({ problem.line, from, problem.line, to, wrapped("(" + to_type + ")", expression, std::string(), true) });
        problem.fixes.push_back(std::move(fix));
        return true;
    }

    // The fixes read off the stretch of one line a problem marks.
    void attachOnLine(ALScriptProblem& problem, std::string_view line, const Lines& lines, bool lua)
    {
        const std::string&              key  = problem.key;
        const std::vector<std::string>& args = problem.args;
        const S32                       from = llmax(0, problem.column);
        const S32                       to   = llmin(problem.endColumn, static_cast<S32>(line.size()));
        if (to < from)
        {
            return;
        }
        const std::string_view marked = line.substr(from, to - from);
        if (!lua && is(key, Fixed::LSLArgumentWrongType) && args.size() >= 4)
        {
            // The argument the call was given, cast to what it takes.
            const std::vector<std::pair<S32, S32>> call  = callArguments(line, from, to);
            const size_t                           which = static_cast<size_t>(atoi(args[1].c_str()));
            if (which >= 1 && which <= call.size())
            {
                offerCast(problem, line, call[which - 1].first, call[which - 1].second, args[0], args[3]);
            }
        }
        else if (!lua && is(key, Fixed::LSLWrongTypeInAssignment) && args.size() == 3)
        {
            // What a declaration is given, cast to what it declares.
            if (const S32 equals = loneEquals(line, from, to); equals >= 0)
            {
                offerCast(problem, line, equals + 1, to, args[2], args[0]);
            }
        }
        else if (!lua && is(key, Fixed::LSLInvalidOperator) && args.size() == 3 && args[1] == "=")
        {
            // An assignment of the wrong type, which Tailslide words as an
            // operator it has no rule for.
            if (const S32 equals = loneEquals(line, from, to); equals >= 0)
            {
                offerCast(problem, line, equals + 1, to, args[2], args[0]);
            }
        }
        else if (!lua && is(key, Fixed::LSLBadReturnType) && args.size() == 2)
        {
            if (const size_t at = marked.find("return"); at != std::string_view::npos)
            {
                offerCast(problem, line, from + static_cast<S32>(at) + 6, to, args[0], args[1]);
            }
        }
        else if (!lua && is(key, Fixed::LSLAssignmentInComparison))
        {
            // Either a comparison was meant, or the assignment is wanted and
            // says so in brackets. Neither is preferred: which was meant is
            // the scripter's to say.
            if (const S32 equals = loneEquals(line, from, to); equals >= 0)
            {
                ALScriptFix compare = titled("ScriptFixCompare", "Compare with '=='", {});
                compare.edits.push_back({ problem.line, equals, problem.line, equals + 1, "==" });
                problem.fixes.push_back(std::move(compare));
                ALScriptFix brackets = titled("ScriptFixBrackets", "Wrap the assignment in brackets", {});
                brackets.safe        = true;
                brackets.edits.push_back({ problem.line, from, problem.line, from, "(" });
                brackets.edits.push_back({ problem.line, to, problem.line, to, ")" });
                problem.fixes.push_back(std::move(brackets));
            }
        }
        else if (!lua && is(key, Fixed::LSLEqAsStatement))
        {
            // A comparison thrown away is an assignment typed with one `=`
            // too many.
            if (const size_t at = marked.find("=="); at != std::string_view::npos)
            {
                ALScriptFix fix = titled("ScriptFixAssign", "Assign with '='", {});
                fix.preferred   = true;
                fix.edits.push_back({ problem.line, from + static_cast<S32>(at), problem.line, from + static_cast<S32>(at) + 2, "=" });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (!lua && is(key, Fixed::LSLIntFloatMulAssign))
        {
            // What the warning itself says to write: the product as a float,
            // cast back to the integer once.
            const size_t at = marked.find("*=");
            if (at != std::string_view::npos)
            {
                S32 name_from = from, name_to = from + static_cast<S32>(at);
                trimmed(line, name_from, name_to);
                S32 value_from = from + static_cast<S32>(at) + 2, value_to = to;
                trimmed(line, value_from, value_to);
                const std::string_view name = line.substr(name_from, name_to - name_from);
                if (isIdentifier(name) && value_to > value_from)
                {
                    ALScriptFix fix = titled("ScriptFixIntFloat", "Multiply as a float, then cast to integer", {});
                    fix.preferred   = true;
                    fix.edits.push_back({ problem.line, name_from, problem.line, value_to,
                                          std::string(name) + " = (integer)(" + std::string(name) + " * " +
                                              wrapped("", line.substr(value_from, value_to - value_from), "", true) + ")" });
                    problem.fixes.push_back(std::move(fix));
                }
            }
        }
        else if (!lua && is(key, Fixed::LSLChangeToCurrentState))
        {
            // It does what `return` does, as the warning says; so write that.
            if (marked.compare(0, 5, "state") == 0)
            {
                S32 end = to;
                while (end > from && line[end - 1] != ';')
                {
                    --end;
                }
                ALScriptFix fix = titled("ScriptFixReturn", "Write 'return' instead", {});
                fix.preferred   = true;
                fix.safe        = true;
                fix.edits.push_back({ problem.line, from, problem.line, end > from ? end : to, end > from ? "return;" : "return" });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (lua && (is(key, Fixed::LuauTypeMismatch) || is(key, Fixed::LuauTypeMismatchReason)) && args.size() >= 2 && typeNamed(args[0]) == "string" &&
                 typeNamed(args[1]) != "string")
        {
            // A value where a string is wanted, said as one. A declaration
            // marks the whole of itself: what it is given is after its `=`.
            S32 value_from = from, value_to = to;
            if (marked.compare(0, 6, "local ") == 0)
            {
                const S32 equals = loneEquals(line, from, to);
                value_from       = equals < 0 ? to : equals + 1;
            }
            trimmed(line, value_from, value_to);
            const std::string_view value = line.substr(value_from, llmax(0, value_to - value_from));
            if (value_to > value_from && value.find(',') == std::string_view::npos)
            {
                ALScriptFix fix = titled("ScriptFixToString", "Wrap in tostring()", {});
                fix.preferred   = true;
                fix.edits.push_back({ problem.line, value_from, problem.line, value_from, "tostring(" });
                fix.edits.push_back({ problem.line, value_to, problem.line, value_to, ")" });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (lua && (is(key, Fixed::LuauRequiresSelf) || is(key, Fixed::LuauNotTakeSelf)))
        {
            // The last `.` or `:` before the call's bracket, the other way.
            const bool   colon = is(key, Fixed::LuauRequiresSelf);
            const size_t open  = marked.find('(');
            const size_t at    = marked.substr(0, open).find_last_of(colon ? '.' : ':');
            if (open != std::string_view::npos && at != std::string_view::npos)
            {
                ALScriptFix fix = colon ? titled("ScriptFixColon", "Call with ':'", {}) : titled("ScriptFixDot", "Call with '.'", {});
                fix.preferred   = true;
                const S32 col   = from + static_cast<S32>(at);
                fix.edits.push_back({ problem.line, col, problem.line, col + 1, colon ? ":" : "." });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (lua && (is(key, Fixed::LuauLintGlobalUsedAsLocalFunction) || is(key, Fixed::LuauLintGlobalUsedAsLocalLine) || is(key, Fixed::LuauLintGlobalNeverRead)) && !args.empty() && isIdentifier(args[0]))
        {
            // Local where it is first given a value, which is the place the
            // lint marks: `local` put before the assignment, where that is
            // what stands there.
            const S32 equals = loneEquals(line, from, static_cast<S32>(line.size()));
            if (marked == args[0] && equals >= to && line.substr(to, equals - to).find_first_not_of(" \t") == std::string_view::npos)
            {
                ALScriptFix fix = titled("ScriptFixLocal", "Make '[1]' local", { args[0] });
                fix.preferred   = true;
                fix.edits.push_back({ problem.line, from, problem.line, from, "local " });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (lua && is(key, Fixed::LuauLintUninitializedLocal) && args.size() == 2 && isIdentifier(args[0]))
        {
            // Given nil where it is declared, which is what it holds anyway:
            // the declaration's line is the lint's second word.
            const S32              at_line = atoi(args[1].c_str()) - 1;
            const std::string_view decl    = lines.line(at_line);
            const S32              name_at = findWord(decl, args[0], 0, static_cast<S32>(decl.size()), false);
            const size_t           local   = decl.find("local");
            if (name_at >= 0 && local != std::string_view::npos && static_cast<S32>(local) < name_at && loneEquals(decl, 0, static_cast<S32>(decl.size())) < 0 &&
                decl.find(',') == std::string_view::npos)
            {
                // After the name and any type it was given, before a
                // comment.
                size_t end = decl.find("--");
                end        = end == std::string_view::npos ? decl.size() : end;
                while (end > static_cast<size_t>(name_at) && isspace(static_cast<unsigned char>(decl[end - 1])))
                {
                    --end;
                }
                ALScriptFix fix = titled("ScriptFixInitNil", "Initialize '[1]' with nil", { args[0] });
                fix.preferred   = true;
                fix.safe        = true;
                fix.edits.push_back({ at_line, static_cast<S32>(end), at_line, static_cast<S32>(end), " = nil" });
                problem.fixes.push_back(std::move(fix));
            }
        }
        else if (lua && (is(key, Fixed::LuauLintDirectiveUnknownDidYouMean) || is(key, Fixed::LuauLintDirectiveNolintUnknownDidYouMean)) && args.size() == 2 &&
                 isIdentifier(args[0]) && isIdentifier(args[1]))
        {
            // A directive or a lint's name spelt as Luau suggests; a comment
            // only, so nothing the script does changes.
            ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[1] });
            fix.preferred   = true;
            fix.safe        = true;
            changeName(problem, lines, args[0], args[1], std::move(fix), false);
        }
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

namespace
{
    // Whether a line gives a local what a require returns: `local util =
    // require("util")`, `local greet = require("util").greet`.
    bool givesRequire(std::string_view line)
    {
        const size_t lead = line.find_first_not_of(" \t");
        if (lead == std::string_view::npos || line.compare(lead, 6, "local ") != 0)
        {
            return false;
        }
        const size_t equals = line.find('=', lead);
        if (equals == std::string_view::npos)
        {
            return false;
        }
        const size_t call = line.find_first_not_of(" \t", equals + 1);
        return call != std::string_view::npos && line.compare(call, 8, "require(") == 0;
    }

    // Where a require goes at the top of a script: after the last of those
    // it opens with, else after the comments it opens with -- its hot
    // comments, a header -- and whether a blank line should follow it,
    // where code would stand against it otherwise.
    S32 requireLine(const Lines& lines, bool& apart)
    {
        S32         after_comments = 0;
        S32         last_require   = -1;
        bool        in_block       = false;
        std::string closing;
        S32         i = 0;
        for (; i < lines.count(); ++i)
        {
            std::string_view line = lines.line(i);
            const size_t     lead = line.find_first_not_of(" \t");
            line                  = lead == std::string_view::npos ? std::string_view() : line.substr(lead);
            if (in_block)
            {
                in_block = line.find(closing) == std::string_view::npos;
                if (last_require < 0)
                {
                    after_comments = i + 1;
                }
                continue;
            }
            if (line.empty())
            {
                continue;
            }
            if (line.compare(0, 2, "--") == 0)
            {
                // A block comment runs to its closing brackets, with as
                // many equals signs between them as it opened with.
                if (line.size() > 2 && line[2] == '[')
                {
                    size_t equals = 3;
                    while (equals < line.size() && line[equals] == '=')
                    {
                        ++equals;
                    }
                    if (equals < line.size() && line[equals] == '[')
                    {
                        closing  = "]" + std::string(equals - 3, '=') + "]";
                        in_block = line.find(closing, equals + 1) == std::string_view::npos;
                    }
                }
                if (last_require < 0)
                {
                    after_comments = i + 1;
                }
                continue;
            }
            if (givesRequire(line))
            {
                last_require = i;
                continue;
            }
            break;
        }
        if (last_require >= 0)
        {
            apart = false;
            return last_require + 1;
        }
        const std::string_view next = lines.line(after_comments);
        apart                       = after_comments < lines.count() && next.find_first_not_of(" \t") != std::string_view::npos;
        return after_comments;
    }

    // Where an include goes at the top of an LSL script: after the last of
    // the directives it opens with -- its includes, and the defines that
    // may say how an include is read -- else after the comments it opens
    // with, apart from code that would stand against it.
    S32 includeLine(const Lines& lines, bool& apart)
    {
        S32  after_comments = 0;
        S32  last_directive = -1;
        bool in_block       = false;
        for (S32 i = 0; i < lines.count(); ++i)
        {
            std::string_view line = lines.line(i);
            const size_t     lead = line.find_first_not_of(" \t");
            line                  = lead == std::string_view::npos ? std::string_view() : line.substr(lead);
            if (in_block)
            {
                in_block = line.find("*/") == std::string_view::npos;
                if (last_directive < 0)
                {
                    after_comments = i + 1;
                }
                continue;
            }
            if (line.empty())
            {
                continue;
            }
            if (line.compare(0, 2, "//") == 0 || line.compare(0, 2, "/*") == 0)
            {
                in_block = line.compare(0, 2, "/*") == 0 && line.find("*/", 2) == std::string_view::npos;
                if (last_directive < 0)
                {
                    after_comments = i + 1;
                }
                continue;
            }
            if (line.front() == '#')
            {
                // With the lines a backslash carries it on to.
                last_directive = i;
                while (!lines.line(last_directive).empty() && lines.line(last_directive).back() == '\\' && last_directive + 1 < lines.count())
                {
                    ++last_directive;
                }
                i = last_directive;
                continue;
            }
            break;
        }
        const S32 at = last_directive >= 0 ? last_directive + 1 : after_comments;
        apart        = last_directive < 0 && at < lines.count() && lines.line(at).find_first_not_of(" \t") != std::string_view::npos;
        return at;
    }

    // A module's name as a string in the script's source.
    std::string luaString(std::string_view name)
    {
        std::string out = "\"";
        for (char c : name)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }
}

namespace ALScriptFixes
{
    ALScriptFix titled(const char* key, const char* english, std::vector<std::string> args)
    {
        ALScriptFix fix;
        fix.key   = key;
        fix.title = ALScriptProblem::fill(english, args);
        fix.args  = std::move(args);
        return fix;
    }

    const char* eventAnswering(std::string_view function)
    {
        static const std::pair<const char*, const char*> ANSWERS[] = {
            { "llListen", "listen" },
            { "llSetTimerEvent", "timer" },
            { "llSensor", "sensor" },
            { "llSensorRepeat", "sensor" },
            { "llRequestPermissions", "run_time_permissions" },
            { "llRequestExperiencePermissions", "experience_permissions" },
            { "llHTTPRequest", "http_response" },
            { "llRequestURL", "http_request" },
            { "llRequestSecureURL", "http_request" },
            { "llRequestAgentData", "dataserver" },
            { "llRequestInventoryData", "dataserver" },
            { "llGetNotecardLine", "dataserver" },
            { "llGetNumberOfNotecardLines", "dataserver" },
            { "llRequestDisplayName", "dataserver" },
            { "llRequestUsername", "dataserver" },
            { "llRequestSimulatorData", "dataserver" },
            { "llTakeControls", "control" },
            { "llTarget", "at_target" },
            { "llRotTarget", "at_rot_target" },
            { "llGetNextEmail", "email" },
            { "llTransferLindenDollars", "transaction_result" },
            { "llCreateLink", "changed" },
        };
        for (const auto& [call, event] : ANSWERS)
        {
            if (function == call)
            {
                return event;
            }
        }
        return nullptr;
    }

    size_t editDistance(std::string_view a, std::string_view b)
    {
        // Three rows: a swap reaches back two.
        std::vector<size_t> before(b.size() + 1), last(b.size() + 1), row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j)
        {
            last[j] = j;
        }
        const auto lower = [](char c) { return LLStringOps::toLower(c); };
        for (size_t i = 1; i <= a.size(); ++i)
        {
            row[0] = i;
            for (size_t j = 1; j <= b.size(); ++j)
            {
                const bool same = lower(a[i - 1]) == lower(b[j - 1]);
                row[j]          = std::min({ last[j] + 1, row[j - 1] + 1, last[j - 1] + (same ? 0 : 1) });
                if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) && lower(a[i - 2]) == lower(b[j - 1]))
                {
                    row[j] = std::min(row[j], before[j - 2] + 1);
                }
            }
            std::swap(before, last);
            std::swap(last, row);
        }
        return last[b.size()];
    }

    std::vector<std::string> nearestNames(std::string_view word, const std::vector<std::string>& names)
    {
        const size_t n     = word.size();
        const size_t limit = n <= 1 ? 0 : n <= 5 ? 1 : n <= 9 ? 2 : n / 3;
        std::vector<std::string> best;
        size_t                   best_distance = limit + 1;
        for (const std::string& name : names)
        {
            if (name == word)
            {
                continue;
            }
            const size_t distance = editDistance(name, word);
            if (distance < best_distance)
            {
                best.clear();
                best_distance = distance;
            }
            if (distance == best_distance && std::find(best.begin(), best.end(), name) == best.end())
            {
                best.push_back(name);
            }
        }
        std::sort(best.begin(), best.end());
        return best;
    }

    std::string nearest(std::string_view word, const std::vector<std::string>& names)
    {
        const std::vector<std::string> near = nearestNames(word, names);
        return near.size() == 1 ? near.front() : std::string();
    }

    bool surelyMeant(std::string_view word, std::string_view now) { return word.size() > 3 || editDistance(word, now) == 0; }

    void offerName(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::string& now, bool preferred)
    {
        if (!isIdentifier(was) || !isIdentifier(now))
        {
            return;
        }
        ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { was, now });
        fix.preferred   = preferred;
        changeName(problem, Lines(text), was, now, std::move(fix), false);
    }

    void offerNames(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::vector<std::string>& names)
    {
        // A few at most: past that the name was no near miss.
        const std::vector<std::string> near = nearestNames(was, names);
        if (near.size() > 3)
        {
            return;
        }
        for (const std::string& now : near)
        {
            offerName(problem, text, was, now, near.size() == 1 && surelyMeant(was, now));
        }
    }

    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& name)
    {
        ALScriptFix fix = titled("ScriptFixRemove", "Remove '[1]'", { name });
        fix.preferred   = true;
        fix.safe        = true;
        offerRemoval(problem, text, line, column, endLine, endColumn, std::move(fix));
    }

    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, ALScriptFix fix)
    {
        const Lines lines(text);
        std::string_view first = lines.line(line);
        std::string_view last  = lines.line(endLine);
        if (!lines.offsetOf(line, column) || !lines.offsetOf(endLine, endColumn) || endLine < line || (endLine == line && endColumn <= column))
        {
            return;
        }
        // The type it is declared as, before its name.
        static const char* const TYPES[] = { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };
        S32 word_end = column;
        while (word_end > 0 && isspace(static_cast<unsigned char>(first[word_end - 1])))
        {
            --word_end;
        }
        S32 word = word_end;
        while (word > 0 && identifierByte(first[word - 1]))
        {
            --word;
        }
        if (word < word_end && std::find(std::begin(TYPES), std::end(TYPES), first.substr(word, word_end - word)) != std::end(TYPES))
        {
            column = word;
        }
        // The `;` that ends it, where the tree leaves it out.
        S32 after = endColumn;
        while (after < static_cast<S32>(last.size()) && isspace(static_cast<unsigned char>(last[after])))
        {
            ++after;
        }
        if (after < static_cast<S32>(last.size()) && last[after] == ';')
        {
            endColumn = after + 1;
        }
        // Its lines whole, where nothing else stands on them.
        const bool alone_before = first.substr(0, column).find_first_not_of(" \t") == std::string_view::npos;
        const bool alone_after  = last.substr(endColumn).find_first_not_of(" \t") == std::string_view::npos;
        fix.edits.clear();
        fix.removes = true;
        if (alone_before && alone_after)
        {
            if (lines.offsetOf(endLine + 1, 0))
            {
                fix.edits.push_back({ line, 0, endLine + 1, 0, std::string() });
            }
            else
            {
                fix.edits.push_back({ line, 0, endLine, static_cast<S32>(last.size()), std::string() });
            }
        }
        else
        {
            // With the blanks that parted it from what follows, where
            // something does -- else from what stood before it -- so that
            // no space is left where it was.
            S32 from = column;
            S32 to   = endColumn;
            if (!alone_after)
            {
                while (to < static_cast<S32>(last.size()) && (last[to] == ' ' || last[to] == '\t'))
                {
                    ++to;
                }
            }
            else
            {
                while (from > 0 && (first[from - 1] == ' ' || first[from - 1] == '\t'))
                {
                    --from;
                }
            }
            fix.edits.push_back({ line, from, endLine, to, std::string() });
        }
        problem.fixes.push_back(std::move(fix));
    }

    void attachOptimizer(ALScriptProblem& problem, std::string_view text)
    {
        const std::string&              key  = problem.key;
        const std::vector<std::string>& args = problem.args;
        if ((is(key, Fixed::OptimizerFolded) || is(key, Fixed::OptimizerEvaluated) || is(key, Fixed::OptimizerSimplified) || is(key, Fixed::OptimizerWroteAs)) && args.size() == 2 &&
            problem.endLine == problem.line)
        {
            // The expression as the optimizer printed it, and as the source
            // has it, the same but for blanks: then what it became may go in
            // its place. A rewrite of what was written, which the scripter
            // may not want, so never preferred.
            const Lines            lines(text);
            const std::string_view line = lines.line(problem.line);
            if (problem.column < 0 || problem.endColumn > static_cast<S32>(line.size()) || problem.endColumn <= problem.column)
            {
                return;
            }
            const auto bare = [](std::string_view words) {
                std::string out;
                for (char c : words)
                {
                    if (!isspace(static_cast<unsigned char>(c)))
                    {
                        out += c;
                    }
                }
                return out;
            };
            if (bare(line.substr(problem.column, problem.endColumn - problem.column)) != bare(args[0]))
            {
                return;
            }
            ALScriptFix fix = titled("ScriptFixOptimized", "Write '[1]' here", { args[1] });
            fix.edits.push_back({ problem.line, problem.column, problem.line, problem.endColumn, args[1] });
            problem.fixes.push_back(std::move(fix));
        }
        else if (is(key, Fixed::OptimizerRemovedUnreachable) || is(key, Fixed::OptimizerRemovedNoEffect))
        {
            // What can never run, or does nothing, gone from the source as
            // it goes from the upload: safe, and preferred.
            ALScriptFix fix = is(key, Fixed::OptimizerRemovedUnreachable) ? titled("ScriptFixRemoveUnreachable", "Remove what can never run", {})
                                                                   : titled("ScriptFixRemoveNoEffect", "Remove what does nothing", {});
            fix.preferred = true;
            fix.safe      = true;
            offerRemoval(problem, text, problem.line, problem.column, problem.endLine, problem.endColumn, std::move(fix));
        }
        else if ((is(key, Fixed::OptimizerRemovedLocal) || is(key, Fixed::OptimizerRemovedGlobal) || is(key, Fixed::OptimizerRemovedFunction) ||
                  is(key, Fixed::OptimizerRemovedState)) &&
                 args.size() == 1)
        {
            // The analyzer offers these already, as unused; offered here as
            // well, but not preferred, so that one is not made twice -- that
            // one, where offerRemoval made one, and no fix already there.
            const size_t before = problem.fixes.size();
            offerRemoval(problem, text, problem.line, problem.column, problem.endLine, problem.endColumn, args[0]);
            if (problem.fixes.size() > before)
            {
                problem.fixes.back().preferred = false;
            }
        }
    }

    void mapThrough(const ALSourceMap& map, ALScriptProblem& problem)
    {
        std::vector<ALScriptFix> kept;
        for (ALScriptFix& fix : problem.fixes)
        {
            bool whole = !fix.edits.empty();
            for (ALScriptEdit& edit : fix.edits)
            {
                ALSourceMap::Loc begin, end;
                if (edit.line == edit.endLine && map.verbatimSpan(edit.line, edit.column, edit.endColumn, begin, end) && begin.file == 0)
                {
                    edit.line      = begin.line;
                    edit.column    = begin.column;
                    edit.endLine   = end.line;
                    edit.endColumn = end.column;
                    continue;
                }
                // Something put in at a line's start -- a local ahead of
                // the statement it comes out of -- at the start of the
                // source line the line began as.
                if (edit.line == edit.endLine && edit.column == 0 && edit.endColumn == 0)
                {
                    ALSourceMap::Loc at;
                    if (map.lineStart(edit.line, at) && at.file == 0)
                    {
                        edit.line = edit.endLine = at.line;
                        continue;
                    }
                }
                // A whole line or lines taken out: the lines of the source
                // they came from, where the first and the last are the
                // script's own, copied.
                if (edit.text.empty() && edit.column == 0 && edit.endColumn == 0 && edit.endLine > edit.line)
                {
                    const ALSourceMap::Loc first = map.toSource(edit.line, 0);
                    const ALSourceMap::Loc last  = map.toSource(edit.endLine - 1, 0);
                    if (first.found() && last.found() && first.file == 0 && last.file == 0 && last.line - first.line == edit.endLine - 1 - edit.line)
                    {
                        edit.line      = first.line;
                        edit.endLine   = last.line + 1;
                        continue;
                    }
                }
                whole = false;
                break;
            }
            if (whole)
            {
                kept.push_back(std::move(fix));
            }
        }
        problem.fixes = std::move(kept);
    }

    bool intoExpansion(const ALSourceMap& map, ALScriptFix& fix)
    {
        ALScriptFix into = fix;
        for (ALScriptEdit& edit : into.edits)
        {
            const ALSourceMap::Loc from = map.toExpanded(0, edit.line, edit.column);
            if (edit.line == edit.endLine && from.found())
            {
                // Over what the map copied as the text has it, or put in
                // where the text it copied stands.
                const ALSourceMap::Loc to = edit.endColumn == edit.column ? from : map.toExpanded(0, edit.endLine, edit.endColumn);
                ALSourceMap::Loc       begin, end;
                if (to.found() && to.line == from.line && map.verbatimSpan(from.line, from.column, to.column, begin, end) && begin.file == 0 &&
                    begin.line == edit.line && begin.column == edit.column && end.line == edit.endLine && end.column == edit.endColumn)
                {
                    edit.line      = from.line;
                    edit.endLine   = from.line;
                    edit.column    = from.column;
                    edit.endColumn = to.column;
                    continue;
                }
                // Something put in at a line's start, at the start of the
                // line of the output the source line began.
                ALSourceMap::Loc at;
                if (edit.column == 0 && edit.endColumn == 0 && map.lineStart(from.line, at) && at.file == 0 && at.line == edit.line)
                {
                    edit.line = edit.endLine = from.line;
                    continue;
                }
            }
            // Whole lines taken out: the output's lines they became, where
            // they are the script's own, one after another.
            if (edit.text.empty() && edit.column == 0 && edit.endColumn == 0 && edit.endLine > edit.line)
            {
                const ALSourceMap::Loc first = map.toExpanded(0, edit.line, 0);
                const ALSourceMap::Loc last  = map.toExpanded(0, edit.endLine - 1, 0);
                const ALSourceMap::Loc back  = first.found() ? map.toSource(first.line, 0) : ALSourceMap::Loc();
                if (first.found() && last.found() && back.file == 0 && back.line == edit.line && last.line - first.line == edit.endLine - 1 - edit.line)
                {
                    edit.line    = first.line;
                    edit.endLine = last.line + 1;
                    continue;
                }
            }
            return false;
        }
        fix = std::move(into);
        return true;
    }

    void attach(ALScriptProblems& problems, std::string_view text, bool lua)
    {
        const Lines lines(text);
        for (ALScriptProblem& problem : problems)
        {
            const std::string&              key  = problem.key;
            const std::vector<std::string>& args = problem.args;
            if (!lua && is(key, Fixed::LSLSyntaxMissing) && args.size() == 1)
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
            else if (!lua && is(key, Fixed::LSLUndeclaredWithSuggestion) && args.size() == 2 && isIdentifier(args[0]) && isIdentifier(args[1]))
            {
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[1] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[1], std::move(fix), false);
            }
            else if (!lua && is(key, Fixed::LSLDeprecatedWithReplacement) && args.size() == 2 && isIdentifier(args[0]) && isIdentifier(args[1]))
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
            else if (lua && (is(key, Fixed::LuauLintLocalUnused) || is(key, Fixed::LuauLintFunctionUnused) || is(key, Fixed::LuauLintImportUnused)) && args.size() == 1 &&
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
            else if (lua && is(key, Fixed::LuauKeyNotFoundDidYouMean) && args.size() == 3 && isIdentifier(args[0]) && isIdentifier(args[2]))
            {
                // The key after its table: the last of its name where the
                // problem is, `ll.Sya` being the stretch it marks.
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[2] });
                fix.preferred   = surelyMeant(args[0], args[2]);
                changeName(problem, lines, args[0], args[2], std::move(fix), true);
            }
            else if (lua && (is(key, Fixed::LuauMissingPropertyDidYouMean) || is(key, Fixed::LuauMissingExternPropertyDidYouMean)) && args.size() == 3 &&
                     isIdentifier(args[0]) && isIdentifier(args[2]))
            {
                ALScriptFix fix = titled("ScriptFixChange", "Change '[1]' to '[2]'", { args[0], args[2] });
                fix.preferred   = true;
                changeName(problem, lines, args[0], args[2], std::move(fix), true);
            }
            else if (lua &&
                     (is(key, Fixed::LuauLintDeprecatedGlobal) || is(key, Fixed::LuauLintDeprecatedFunctionUse) || is(key, Fixed::LuauLintDeprecatedFunctionUseReason) ||
                      is(key, Fixed::LuauLintDeprecatedMemberUse) || is(key, Fixed::LuauLintDeprecatedMemberUseReason)) &&
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
            else if (problem.endLine == problem.line)
            {
                // What the rest need is the stretch the problem marks, on
                // one line.
                attachOnLine(problem, lines.line(problem.line), lines, lua);
            }
        }
    }

    void offerRequire(ALScriptProblem& problem, std::string_view text, const std::string& module, bool field)
    {
        if (problem.args.size() != 1 || !isIdentifier(problem.args[0]) || module.empty())
        {
            return;
        }
        const std::string& name  = problem.args[0];
        const Lines        lines(text);
        bool               apart = false;
        S32                line  = requireLine(lines, apart);
        std::string        said  = "local " + name + " = require(" + luaString(module) + ")" + (field ? "." + name : std::string());
        S32                column = 0;
        if (line >= lines.count())
        {
            // Past a last line with no break after it: on a line of its own.
            line   = lines.count() - 1;
            column = static_cast<S32>(lines.line(line).size());
            said   = "\n" + said;
        }
        else
        {
            said += apart ? "\n\n" : "\n";
        }
        for (const ALScriptFix& had : problem.fixes)
        {
            if (!had.edits.empty() && had.edits.front().text == said)
            {
                return;
            }
        }
        ALScriptFix fix = field ? titled("ScriptFixRequireField", "Take '[1]' from '[2]'", { name, module })
                                : titled("ScriptFixRequire", "Require '[1]'", { module });
        fix.edits.push_back({ line, column, line, column, said });
        problem.fixes.push_back(std::move(fix));
    }

    void offerInclude(ALScriptProblem& problem, std::string_view text, const std::string& include)
    {
        if (problem.args.size() != 1 || !isIdentifier(problem.args[0]) || include.empty() || include.find_first_of("\"\n\r") != std::string::npos)
        {
            return;
        }
        const Lines lines(text);
        bool        apart  = false;
        S32         line   = includeLine(lines, apart);
        S32         column = 0;
        std::string said   = "#include \"" + include + "\"";
        if (line >= lines.count())
        {
            line   = lines.count() - 1;
            column = static_cast<S32>(lines.line(line).size());
            said   = "\n" + said;
        }
        else
        {
            said += apart ? "\n\n" : "\n";
        }
        for (const ALScriptFix& had : problem.fixes)
        {
            if (!had.edits.empty() && had.edits.front().text == said)
            {
                return;
            }
        }
        ALScriptFix fix = titled("ScriptFixInclude", "Include '[1]'", { include });
        fix.edits.push_back({ line, column, line, column, said });
        problem.fixes.push_back(std::move(fix));
    }

    std::string freshName(std::string_view text, std::string_view base)
    {
        std::string name(base);
        for (int n = 2; findWord(text, name, 0, static_cast<S32>(text.size()), false) >= 0; ++n)
        {
            name = std::string(base) + std::to_string(n);
        }
        return name;
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
        // In the text's order: by where each begins, then where it ends --
        // so that something put in at a place goes before a stretch
        // replaced from there -- and two alike in the order the fix gives
        // them. The editor (ALTextView::replaceAll) and the preview
        // (ALCodeEditor::fixedLines) order them the same way.
        std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin || (a.begin == b.begin && a.end < b.end); });
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

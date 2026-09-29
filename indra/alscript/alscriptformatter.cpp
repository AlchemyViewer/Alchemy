/**
 * @file alscriptformatter.cpp
 * @brief A script's layout put right: indentation from its structure, spaces where the language reads better with them.
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

#include "alscriptformatter.h"

#include "alpreprocessor.h"
#include "alscriptlexicon.h"

#include <algorithm>
#include <set>
#include <vector>

namespace
{
    typedef ALPreprocessor::Token Token;
    typedef Token::Kind           Kind;

    // A line of the text: its tokens as written, without the newline
    // that ended it, and the lines it covers, since a string or a
    // comment may run over several.
    struct Line
    {
        std::vector<Token> tokens;
        S32                first = 0;
        S32                last  = 0;
        // What the formatter decided.
        S32                indent    = 0;
        bool               directive = false;
        bool               blank     = true;
    };

    // Something open at a line's end: a block or a bracket, and how far
    // in the line that opened it was, which is what the line closing it
    // goes back to.
    struct Open
    {
        S32  indent  = 0;
        bool bracket = false;
        // A bracket after `if`, `while` or `for`, whose closing ends the
        // condition of a statement that may hang without braces.
        bool control = false;
        // How many blocks were open when a bracket was: a bracket opened
        // inside a block that is still open is what continues a line;
        // one that a block opened inside does not.
        size_t blocks = 0;
        // Luau: what kind of block, for the `end` that closes it -- an
        // `if`'s, a function's, or another's.
        enum What : U8
        {
            Other,
            If,
            Function
        };
        What what = Other;
    };

    bool significant(const Token& t) { return t.kind != Kind::Space && t.kind != Kind::Comment; }

    // LSL's words that shape the code -- not its types, nor print, which
    // are laid out as names are -- and Luau's keywords.
    bool isKeyword(bool lua, const Token& t)
    {
        return t.kind == Kind::Ident && (lua ? ALScriptLexicon::isLuauKeyword(t.text) : ALScriptLexicon::lslWord(t.text) == ALScriptLexicon::LSL_CONTROL);
    }

    bool isOperator(bool lua, std::string_view op)
    {
        // The unary signs are among them, told apart by what stands
        // before; Luau's `<` and `>` are not, since generics write them
        // without spaces and a comparison with.
        static const std::set<std::string_view> lsl = { "=",  "+=", "-=", "*=", "/=", "%=", "==", "!=", "<", ">", "<=", ">=", "&&",
                                                        "||", "&",  "|",  "^",  "<<", ">>", "+",  "-",  "*", "/", "%",  "!",  "~" };
        static const std::set<std::string_view> luau = { "=", "==", "~=", "<=", ">=", "+",  "-",  "*",  "/",   "//", "%",
                                                         "^", "..", "#",  "+=", "-=", "*=", "/=", "//=", "%=", "^=", "..=" };
        return (lua ? luau : lsl).count(op) > 0;
    }

    bool isUnary(bool lua, std::string_view op) { return op == "-" || (lua ? (op == "#") : (op == "!" || op == "~")); }

    bool opens(const Token& t) { return t.kind == Kind::Punct && (t.text == "(" || t.text == "["); }
    bool closes(const Token& t) { return t.kind == Kind::Punct && (t.text == ")" || t.text == "]"); }

    // Whether the token starts an operand: what a binary operator may
    // follow, and what a unary one may not.
    bool operand(bool lua, const Token& t)
    {
        switch (t.kind)
        {
            case Kind::Ident:  return !isKeyword(lua, t) || t.text == "true" || t.text == "false" || t.text == "nil" || t.text == "end";
            case Kind::Number:
            case Kind::String: return true;
            case Kind::Punct:  return t.text == ")" || t.text == "]" || t.text == "}" || t.text == "..." || t.text == "++" || t.text == "--";
            default:           return false;
        }
    }

    std::string indentText(const ALScriptFormatter::Options& options, S32 levels)
    {
        if (levels <= 0)
        {
            return std::string();
        }
        return options.tabs ? std::string(static_cast<size_t>(levels), '\t') : std::string(static_cast<size_t>(levels * options.indent), ' ');
    }

    std::string asWritten(const Line& line)
    {
        std::string text;
        for (const Token& t : line.tokens)
        {
            text += t.text;
        }
        return text;
    }

    // The line's tokens spaced afresh, after its indentation: one space
    // around a binary operator, none around a unary one or inside
    // brackets, one after a comma and none before; a trailing comment,
    // and anything the rules do not name, spaced as written.
    std::string spaced(const Line& line, bool lua)
    {
        std::string  out;
        const Token* prev      = nullptr;
        // The last token before prev that is neither blank nor a comment:
        // what a sign after prev has to operate on, kept as the line goes
        // rather than looked for from its start at every sign.
        const Token* earlier   = nullptr;
        std::string  gap;       // the whitespace written between prev and the next token
        S32          vector    = 0;
        // The brackets open on the line, and whether each is a
        // condition's: what follows a condition's closing bracket is a
        // statement, spaced from it; what follows a cast's is its operand.
        std::vector<bool> brackets;
        bool              conditionClosed = false;
        for (const Token& t : line.tokens)
        {
            if (t.kind == Kind::Space)
            {
                gap += t.text;
                continue;
            }
            const bool closedCondition = conditionClosed;
            conditionClosed            = false;
            if (t.kind == Kind::Punct && t.text == "(")
            {
                brackets.push_back(prev && prev->kind == Kind::Ident && (prev->text == "if" || prev->text == "while" || prev->text == "for"));
            }
            else if (t.kind == Kind::Punct && t.text == ")" && !brackets.empty())
            {
                conditionClosed = brackets.back();
                brackets.pop_back();
            }
            if (!prev)
            {
                out += t.text;
                prev = &t;
                gap.clear();
                continue;
            }
            const Token& a = *prev;
            std::string  between = gap.empty() ? std::string() : std::string(" ");
            bool         decided = false;
            auto         say     = [&](bool space) {
                between = space ? " " : "";
                decided = true;
            };
            const bool aOperand = operand(lua, a);
            if (t.kind == Kind::Comment || a.kind == Kind::Comment)
            {
                // Spaced as written: aligned by hand, most likely.
                between = gap;
                decided = true;
            }
            else if (t.kind == Kind::Punct)
            {
                const std::string& p = t.text;
                if (p == "," || p == ";")
                {
                    say(false);
                }
                else if (p == ")" || p == "]")
                {
                    say(false);
                }
                else if (p == "(")
                {
                    if (a.kind == Kind::Ident)
                    {
                        say(isKeyword(lua, a) && !(lua && a.text == "function"));
                    }
                    else if (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == ")" || a.text == "]" || a.text == "." || a.text == ":"))
                    {
                        say(false);
                    }
                    else if (a.kind == Kind::Punct && isOperator(lua, a.text))
                    {
                        say(true);
                    }
                }
                else if (p == "[")
                {
                    say(!(aOperand || (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == "{"))));
                }
                else if (p == "{")
                {
                    if (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == "{"))
                    {
                        say(false);
                    }
                    else if (lua && a.kind == Kind::Ident && !isKeyword(lua, a))
                    {
                        say(false);
                    }
                    else
                    {
                        say(true);
                    }
                }
                else if (p == "}")
                {
                    say(!(a.kind == Kind::Punct && a.text == "{"));
                }
                else if (p == "." || p == "++" || p == "--")
                {
                    say(false);
                }
                else if (p == "<" && !lua)
                {
                    // A vector or rotation opens where no operand stands
                    // before it; a comparison follows one.
                    if (!aOperand)
                    {
                        ++vector;
                        say(!(a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == "<" || a.text == "{")));
                    }
                    else
                    {
                        say(true);
                    }
                }
                else if (p == ">" && !lua && vector > 0)
                {
                    --vector;
                    say(false);
                }
                else if (isOperator(lua, p))
                {
                    if (isUnary(lua, p) && !aOperand)
                    {
                        // A sign or a not: spaced before as its neighbour
                        // asks, and nothing after, which the next token's
                        // turn will see to.
                        if (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == "<" || a.text == "{" || a.text == "!" || a.text == "~" || a.text == "#"))
                        {
                            say(false);
                        }
                        else if (a.kind == Kind::Punct && a.text == "-" && p == "-")
                        {
                            say(true);
                        }
                        else if (a.kind == Kind::Punct && isUnary(lua, a.text))
                        {
                            say(false);
                        }
                        else
                        {
                            say(true);
                        }
                    }
                    else
                    {
                        say(true);
                    }
                }
            }
            else if (a.kind == Kind::Punct)
            {
                const std::string& p = a.text;
                if (p == "(" || p == "[" || p == "." || p == "@" || p == "++" || p == "--")
                {
                    say(false);
                }
                else if (p == "<" && !lua && vector > 0 && t.kind != Kind::Punct)
                {
                    say(false);
                }
                else if (p == "{")
                {
                    say(true);
                }
                else if (p == "," || p == ";")
                {
                    say(true);
                }
                else if (isOperator(lua, p))
                {
                    // After a unary sign, nothing; after a binary one, a
                    // space. Unary where nothing to operate on stood
                    // before the sign.
                    say(!(isUnary(lua, p) && (!earlier || !operand(lua, *earlier))));
                }
                else if (p == ")" || p == "]")
                {
                    if ((t.kind == Kind::Ident && isKeyword(lua, t)) || closedCondition)
                    {
                        say(true);
                    }
                }
            }
            else if ((a.kind == Kind::Ident || a.kind == Kind::Number || a.kind == Kind::String) &&
                     (t.kind == Kind::Ident || t.kind == Kind::Number || t.kind == Kind::String))
            {
                say(true);
            }
            if (!decided)
            {
                // A space where the author put one.
                between = gap.empty() ? std::string() : std::string(" ");
            }
            out += between;
            out += t.text;
            if (significant(*prev))
            {
                earlier = prev;
            }
            prev = &t;
            gap.clear();
        }
        return out;
    }

    std::vector<Line> linesOf(std::string_view text, bool lua, bool& endsWithNewline)
    {
        std::vector<Line> lines;
        Line              current;
        S32               at = 0;
        for (Token& t : ALPreprocessor::tokenize(text, lua))
        {
            if (t.kind == Kind::Newline)
            {
                current.first = at;
                current.last  = t.line;
                lines.push_back(std::move(current));
                current = Line();
                at      = t.line + static_cast<S32>(t.text.size());
                continue;
            }
            if (significant(t))
            {
                current.blank = false;
            }
            current.tokens.push_back(std::move(t));
        }
        endsWithNewline = current.tokens.empty();
        if (!endsWithNewline)
        {
            current.first = at;
            current.last  = at;
            for (const Token& t : current.tokens)
            {
                current.last = t.line + static_cast<S32>(std::count(t.text.begin(), t.text.end(), '\n'));
            }
            lines.push_back(std::move(current));
        }
        return lines;
    }

    // Luau: whether an `if` after this token is an expression -- `x = if
    // c then a else b` -- rather than a statement. Nothing before it, or
    // a statement's end, makes it a statement; an assignment, an opening
    // bracket, a comma, an operator or a word that wants a value makes
    // it an expression.
    bool wantsValue(const Token* before)
    {
        if (!before)
        {
            return false;
        }
        if (before->kind == Kind::Punct)
        {
            return before->text != ")" && before->text != "]" && before->text != "}" && before->text != ";" && before->text != ":" &&
                   before->text != "::";
        }
        if (before->kind == Kind::Ident)
        {
            return before->text == "return" || before->text == "and" || before->text == "or" || before->text == "not" || before->text == "in";
        }
        return false;
    }

    // Each line's indentation from what is open at its start.
    void decide(std::vector<Line>& lines, bool lua)
    {
        std::vector<Open> open;
        bool              hang = false;
        // Luau: each `if` met and not yet done with, statement or
        // expression -- an expression's `then` and `else` open no block,
        // and its `else` is its end -- with a function's body marked so
        // that an `if` inside one starts afresh.
        enum IfKind : U8
        {
            IfStatement,
            IfExpression,
            FunctionBody
        };
        std::vector<U8> ifs;
        auto            ifExpression = [&ifs]() { return !ifs.empty() && ifs.back() == IfExpression; };
        // The line before's last token, which is what an `if` at a
        // line's start follows.
        const Token* previousLast = nullptr;
        // Whether the token before was the `else` of an if-expression,
        // after which another `if` is the same expression going on.
        bool afterElseExpression = false;
        auto              blocksOpen = [&open]() {
            size_t n = 0;
            for (const Open& o : open)
            {
                n += o.bracket ? 0 : 1;
            }
            return n;
        };
        auto innermostBlock = [&open]() -> const Open* {
            for (auto it = open.rbegin(); it != open.rend(); ++it)
            {
                if (!it->bracket)
                {
                    return &*it;
                }
            }
            return nullptr;
        };
        auto isBlockCloser = [lua](const Token& t) {
            if (t.kind == Kind::Punct)
            {
                return t.text == "}";
            }
            return lua && t.kind == Kind::Ident && (t.text == "end" || t.text == "until" || t.text == "else" || t.text == "elseif");
        };
        // A directive's line continued by a backslash at its end: the
        // lines it runs on to are the directive's too -- a macro's body,
        // whose braces open and close nothing of the script's.
        bool continued = false;
        for (Line& line : lines)
        {
            const Token* first = nullptr;
            const Token* last  = nullptr;
            for (const Token& t : line.tokens)
            {
                if (significant(t))
                {
                    first = first ? first : &t;
                    last  = &t;
                }
            }
            if (continued || (first && first->kind == Kind::Punct && first->text == (lua ? "--#" : "#")))
            {
                // A preprocessor line: as written, and no part of the
                // structure.
                line.directive = true;
                line.indent    = 0;
                continued      = last && last->text == "\\";
                continue;
            }
            // Where the line starts: one further in than the line that
            // opened the innermost block, or the bracket still open
            // inside it; back to the opener's own line for its closer.
            S32 indent = 0;
            if (first && isBlockCloser(*first))
            {
                if (const Open* block = innermostBlock())
                {
                    indent = block->indent;
                }
            }
            else if (first && closes(*first) && !open.empty() && open.back().bracket)
            {
                indent = open.back().indent;
            }
            else
            {
                if (const Open* block = innermostBlock())
                {
                    indent = block->indent + 1;
                }
                if (!open.empty() && open.back().bracket && open.back().blocks == blocksOpen())
                {
                    indent = open.back().indent + 1;
                }
                if (hang && !(first && first->kind == Kind::Punct && first->text == "{"))
                {
                    ++indent;
                }
            }
            line.indent = indent;
            if (!first)
            {
                continue;
            }
            // What the line opens and closes.
            bool controlClosedLast = false;
            const Token* before    = nullptr;
            for (const Token& t : line.tokens)
            {
                if (!significant(t))
                {
                    continue;
                }
                controlClosedLast          = false;
                const bool wasAfterElse    = afterElseExpression;
                afterElseExpression        = false;
                if (t.kind == Kind::Punct)
                {
                    if (t.text == "{")
                    {
                        open.push_back(Open{ indent, false, false, 0 });
                        hang = false;
                    }
                    else if (t.text == "}")
                    {
                        while (!open.empty() && open.back().bracket)
                        {
                            open.pop_back();
                        }
                        if (!open.empty())
                        {
                            open.pop_back();
                        }
                    }
                    else if (opens(t))
                    {
                        const bool control = !lua && t.text == "(" && before && before->kind == Kind::Ident &&
                                             (before->text == "if" || before->text == "while" || before->text == "for");
                        open.push_back(Open{ indent, true, control, blocksOpen() });
                    }
                    else if (closes(t))
                    {
                        if (!open.empty() && open.back().bracket)
                        {
                            controlClosedLast = open.back().control;
                            open.pop_back();
                        }
                    }
                }
                else if (lua && t.kind == Kind::Ident)
                {
                    if (t.text == "if")
                    {
                        ifs.push_back(wantsValue(before ? before : previousLast) || wasAfterElse ? IfExpression : IfStatement);
                    }
                    else if (t.text == "then")
                    {
                        if (!ifExpression())
                        {
                            open.push_back(Open{ indent, false, false, 0, Open::If });
                        }
                    }
                    else if (t.text == "function")
                    {
                        ifs.push_back(FunctionBody);
                        open.push_back(Open{ indent, false, false, 0, Open::Function });
                    }
                    else if (t.text == "do" || t.text == "repeat")
                    {
                        open.push_back(Open{ indent, false, false, 0 });
                    }
                    else if (t.text == "elseif")
                    {
                        if (!ifExpression())
                        {
                            while (!open.empty() && open.back().bracket)
                            {
                                open.pop_back();
                            }
                            if (!open.empty())
                            {
                                open.pop_back();
                            }
                        }
                    }
                    else if (t.text == "end" || t.text == "until")
                    {
                        while (!open.empty() && open.back().bracket)
                        {
                            open.pop_back();
                        }
                        if (!open.empty())
                        {
                            const Open::What what = open.back().what;
                            open.pop_back();
                            if (what == Open::If && !ifs.empty() && ifs.back() == IfStatement)
                            {
                                ifs.pop_back();
                            }
                            else if (what == Open::Function)
                            {
                                // Whatever the body left unfinished goes with it.
                                while (!ifs.empty() && ifs.back() != FunctionBody)
                                {
                                    ifs.pop_back();
                                }
                                if (!ifs.empty())
                                {
                                    ifs.pop_back();
                                }
                            }
                        }
                    }
                    else if (t.text == "else")
                    {
                        if (ifExpression())
                        {
                            // The expression's last part; an `if` right after
                            // it goes on with the same expression.
                            ifs.pop_back();
                            afterElseExpression = true;
                        }
                        else
                        {
                            while (!open.empty() && open.back().bracket)
                            {
                                open.pop_back();
                            }
                            if (!open.empty())
                            {
                                open.pop_back();
                            }
                            open.push_back(Open{ indent, false, false, 0, Open::If });
                        }
                    }
                }
                before = &t;
            }
            previousLast = last;
            // A statement may hang off a condition or an else on the line
            // before, without braces; the next line goes in one, and any
            // after it does not.
            if (!lua)
            {
                hang = controlClosedLast || (last->kind == Kind::Ident && (last->text == "else" || last->text == "do"));
            }
        }
    }

    std::string emit(const std::vector<Line>& lines, const ALScriptFormatter::Options& options, bool endsWithNewline, S32 first, S32 last)
    {
        std::string out;
        S32         blanks = 0;
        const bool  whole  = first < 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const Line& line = lines[i];
            const bool  ours = whole || (line.first >= first && line.last <= last);
            std::string text;
            if (!ours || line.directive)
            {
                text = asWritten(line);
                if (ours)
                {
                    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                    {
                        text.pop_back();
                    }
                }
            }
            else if (line.blank)
            {
                // Nothing on it: nothing, and beyond a few in a row,
                // not even a line -- when the whole is asked for; a few
                // lines asked for keep their numbers.
                if (whole)
                {
                    ++blanks;
                    if (blanks > options.maxBlankLines || out.empty())
                    {
                        continue;
                    }
                }
                text.clear();
            }
            else
            {
                text = indentText(options, line.indent);
                if (options.spacing)
                {
                    text += spaced(line, options.lua);
                }
                else
                {
                    std::string body = asWritten(line);
                    size_t      at   = body.find_first_not_of(" \t");
                    text += at == std::string::npos ? std::string() : body.substr(at);
                }
                while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                {
                    text.pop_back();
                }
            }
            if (!line.blank)
            {
                blanks = 0;
            }
            out += text;
            if (i + 1 < lines.size() || endsWithNewline)
            {
                out += '\n';
            }
        }
        if (whole)
        {
            // Ending with one newline, and no blank line before it.
            while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
            {
                out.pop_back();
            }
            if (!out.empty() && out.back() != '\n')
            {
                out += '\n';
            }
        }
        return out;
    }
}

// static
std::string ALScriptFormatter::format(std::string_view text, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    bool              endsWithNewline = false;
    std::vector<Line> lines           = linesOf(text, options.lua, endsWithNewline);
    decide(lines, options.lua);
    return emit(lines, options, endsWithNewline, -1, -1);
}

// static
std::string ALScriptFormatter::formatLines(std::string_view text, const Options& options, S32 first, S32 last)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    bool              endsWithNewline = false;
    std::vector<Line> lines           = linesOf(text, options.lua, endsWithNewline);
    decide(lines, options.lua);
    return emit(lines, options, endsWithNewline, first, last);
}

// static
std::vector<bool> ALScriptFormatter::breaksInStrings(std::string_view text, bool lua)
{
    std::vector<bool> out(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1, false);
    const auto        mark = [&out](S32 line) {
        if (line >= 0 && static_cast<size_t>(line) < out.size())
        {
            out[static_cast<size_t>(line)] = true;
        }
    };
    if (lua)
    {
        // Luau's strings are the tokens' own: a quoted one ends at its line
        // but for a backslash before the break, a long one runs on.
        for (const Token& t : ALPreprocessor::tokenize(text, true))
        {
            if (t.kind != Kind::String)
            {
                continue;
            }
            S32 line = t.line;
            for (const char c : t.text)
            {
                if (c == '\n')
                {
                    mark(line++);
                }
            }
        }
        return out;
    }
    // An LSL string may hold a break as written, which the tokens -- read
    // as a preprocessor reads C -- end at; so the text is read as LSL
    // reads it, past comments, a quote to its closing quote.
    S32 line = 0;
    for (size_t i = 0; i < text.size();)
    {
        const ALScriptLexicon::Stretch run = ALScriptLexicon::stretchAt(text, i, false);
        for (size_t k = i; k < run.end; ++k)
        {
            if (text[k] == '\n')
            {
                if (run.kind == ALScriptLexicon::Kind::String)
                {
                    mark(line);
                }
                ++line;
            }
        }
        i = run.end;
    }
    return out;
}

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
#include <optional>
#include <set>
#include <string>
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
    // brackets, one after a comma and none before; inside Luau's braces
    // as Options::braceSpaces asks, whatever stands next to them; a
    // trailing comment, and anything the rules do not name, spaced as
    // written.
    std::string spaced(const Line& line, const ALScriptFormatter::Options& options)
    {
        const bool   lua       = options.lua;
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
        // Whether the token before is a < that opened an LSL vector, not
        // a comparison: a sign after it is the first part's.
        bool              vectorOpened    = false;
        for (const Token& t : line.tokens)
        {
            if (t.kind == Kind::Space)
            {
                gap += t.text;
                continue;
            }
            const bool closedCondition = conditionClosed;
            conditionClosed            = false;
            const bool openedVector    = vectorOpened;
            vectorOpened               = false;
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
                // A vector or rotation opens where nothing stands before
                // it: a line of a list may begin with one.
                if (!lua && t.kind == Kind::Punct && t.text == "<")
                {
                    ++vector;
                    vectorOpened = true;
                }
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
                if (lua && a.kind == Kind::Punct && a.text == "{" && p != "}")
                {
                    // A table's first part, or a table type's, whatever
                    // begins it: { { 1 } }, { [k] = v }, { -1 }.
                    say(options.braceSpaces);
                }
                else if (p == "," || p == ";")
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
                    else if (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || a.text == ")" || a.text == "]" || a.text == "."))
                    {
                        say(false);
                    }
                    else if (a.kind == Kind::Punct && isOperator(lua, a.text))
                    {
                        // After a sign, nothing: -(a), #(t), !(b).
                        say(!(isUnary(lua, a.text) && (!earlier || !operand(lua, *earlier))));
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
                    else if (a.kind == Kind::Punct && isUnary(lua, a.text) && (!earlier || !operand(lua, *earlier)))
                    {
                        // A length of a table written out: #{1, 2}.
                        say(false);
                    }
                    else
                    {
                        say(true);
                    }
                }
                else if (p == "}")
                {
                    say(!(a.kind == Kind::Punct && a.text == "{") && (!lua || options.braceSpaces));
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
                        vectorOpened = true;
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
                        // Not after a < that compares: LSL's where an operand
                        // stood before it, and Luau's everywhere.
                        if (a.kind == Kind::Punct && (a.text == "(" || a.text == "[" || (a.text == "<" && openedVector) || a.text == "{" || a.text == "!" || a.text == "~" || a.text == "#"))
                        {
                            say(false);
                        }
                        else if (a.kind == Kind::Punct && a.text == "-" && p == "-")
                        {
                            say(true);
                        }
                        else if (a.kind == Kind::Punct && isUnary(lua, a.text) && (!earlier || !operand(lua, *earlier)))
                        {
                            // A sign after a sign: -#t. Not after a minus
                            // that subtracts, x - #t.
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
                    say(!lua || options.braceSpaces);
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
            // A comment is something on the line, though nothing of the
            // structure: a line of one is kept, not emptied as a blank.
            if (t.kind != Kind::Space)
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

    // How many columns text takes: a tab a level's width, a character of
    // several bytes one.
    S32 columns(std::string_view text, S32 tab)
    {
        S32 n = 0;
        for (const char c : text)
        {
            n += c == '\t' ? tab : (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
        }
        return n;
    }

    // A bracket of a line that closes on it, by its tokens' indexes: the
    // commas directly in it, and the bracket it is in. An LSL vector or
    // rotation is one too, which nothing breaks.
    struct Group
    {
        size_t              open   = 0;
        size_t              close  = 0;
        size_t              parent = std::string::npos;
        size_t              width  = 0;
        bool                vector = false;
        std::vector<size_t> commas;
    };

    // What the tokens' brackets are; none where they do not match, or one
    // is left open: a line that runs on over the next is not broken.
    std::vector<Group> groupsOf(const std::vector<Token>& tokens, bool lua)
    {
        std::vector<Group>  groups;
        std::vector<size_t> open;
        const Token*        before = nullptr;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const Token& t = tokens[i];
            if (!significant(t))
            {
                continue;
            }
            if (t.kind == Kind::Punct)
            {
                // A vector where no operand stands before its <, as spaced
                // reads one; one that never closes was a comparison.
                const bool vector = !lua && t.text == "<" && !(before && operand(lua, *before));
                if (t.text == "(" || t.text == "[" || t.text == "{" || vector)
                {
                    Group group;
                    group.open   = i;
                    group.close  = std::string::npos;
                    group.parent = open.empty() ? std::string::npos : open.back();
                    group.vector = vector;
                    open.push_back(groups.size());
                    groups.push_back(std::move(group));
                }
                else if (t.text == ")" || t.text == "]" || t.text == "}")
                {
                    while (!open.empty() && groups[open.back()].vector)
                    {
                        open.pop_back();
                    }
                    const std::string_view opener = t.text == ")" ? "(" : t.text == "]" ? "[" : "{";
                    if (open.empty() || tokens[groups[open.back()].open].text != opener)
                    {
                        return {};
                    }
                    groups[open.back()].close = i;
                    open.pop_back();
                }
                else if (t.text == ">" && !lua && !open.empty() && groups[open.back()].vector)
                {
                    groups[open.back()].close = i;
                    open.pop_back();
                }
                else if (t.text == "," && !open.empty() && !groups[open.back()].vector)
                {
                    groups[open.back()].commas.push_back(i);
                }
            }
            before = &t;
        }
        if (!open.empty() && std::any_of(open.begin(), open.end(), [&groups](size_t g) { return !groups[g].vector; }))
        {
            return {};
        }
        for (Group& group : groups)
        {
            for (size_t i = group.open; group.close != std::string::npos && i <= group.close; ++i)
            {
                group.width += tokens[i].text.size();
            }
        }
        return groups;
    }

    // A line's tokens as lines of their own, each with how far in it is.
    struct Piece
    {
        std::vector<Token> tokens;
        S32                indent = 0;
    };

    // The tokens from `from` to `to`, the blanks at either end left off.
    std::vector<Token> slice(const std::vector<Token>& tokens, size_t from, size_t to)
    {
        while (from < to && tokens[from].kind == Kind::Space)
        {
            ++from;
        }
        while (to > from && tokens[to - 1].kind == Kind::Space)
        {
            --to;
        }
        return std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(from), tokens.begin() + static_cast<std::ptrdiff_t>(to));
    }

    std::string written(const std::vector<Token>& tokens, S32 indent, const ALScriptFormatter::Options& options)
    {
        Line line;
        line.tokens = tokens;
        return indentText(options, indent) + spaced(line, options);
    }

    // The tokens as pieces no wider than the width where a bracket's commas
    // let them be: the bracket broken is the widest with commas, looked
    // for in the widest without, down; or, where its last part is a table
    // or a list with commas of its own, that, which stays on the line
    // that opens the bracket where that fits.
    void wrap(const std::vector<Token>& tokens, S32 indent, const ALScriptFormatter::Options& options, std::vector<Piece>& out)
    {
        const S32 tab = options.tabs ? options.indent : 1;
        if (columns(written(tokens, indent, options), tab) <= options.width)
        {
            out.push_back({ tokens, indent });
            return;
        }
        const std::vector<Group> groups = groupsOf(tokens, options.lua);
        const auto               pick   = [&groups](size_t parent, auto&& self) -> std::optional<size_t> {
            std::vector<size_t> in;
            for (size_t g = 0; g < groups.size(); ++g)
            {
                if (groups[g].parent == parent && !groups[g].vector && groups[g].close != std::string::npos)
                {
                    in.push_back(g);
                }
            }
            std::stable_sort(in.begin(), in.end(), [&groups](size_t a, size_t b) { return groups[a].width > groups[b].width; });
            for (size_t g : in)
            {
                if (!groups[g].commas.empty())
                {
                    return g;
                }
                if (const std::optional<size_t> inner = self(g, self))
                {
                    return inner;
                }
            }
            return std::nullopt;
        };
        std::optional<size_t> chosen = pick(std::string::npos, pick);
        if (!chosen)
        {
            out.push_back({ tokens, indent });
            return;
        }
        // Its last part, where that is a table or a list of its own with
        // commas, and the line to its opening fits.
        const Group& group = groups[*chosen];
        size_t       last  = group.commas.back() + 1;
        while (last < group.close && !significant(tokens[last]))
        {
            ++last;
        }
        for (size_t g = 0; g < groups.size(); ++g)
        {
            const Group& inner = groups[g];
            if (inner.parent == *chosen && inner.open == last && !inner.commas.empty() && tokens[inner.open].text != "(")
            {
                size_t end = inner.close + 1;
                while (end < group.close && !significant(tokens[end]))
                {
                    ++end;
                }
                if (end == group.close && columns(written(slice(tokens, 0, inner.open + 1), indent, options), tab) <= options.width)
                {
                    chosen = g;
                }
                break;
            }
        }
        const Group& broken = groups[*chosen];
        wrap(slice(tokens, 0, broken.open + 1), indent, options, out);
        size_t from = broken.open + 1;
        for (size_t comma : broken.commas)
        {
            wrap(slice(tokens, from, comma + 1), indent + 1, options, out);
            from = comma + 1;
        }
        const std::vector<Token> rest = slice(tokens, from, broken.close);
        if (!rest.empty())
        {
            wrap(rest, indent + 1, options, out);
        }
        wrap(slice(tokens, broken.close, tokens.size()), indent, options, out);
    }

    // A line past the width broken where it can be: its comment after its
    // code, where it has one, after the last piece. As it is where it has
    // a comment inside it, or a string or comment runs on past it.
    std::string wrapped(const Line& line, const ALScriptFormatter::Options& options, const std::string& text)
    {
        const S32 tab = options.tabs ? options.indent : 1;
        if (options.width <= 0 || line.first != line.last || columns(text, tab) <= options.width)
        {
            return text;
        }
        std::vector<Token> tokens = slice(line.tokens, 0, line.tokens.size());
        std::vector<Token> after;
        if (!tokens.empty() && tokens.back().kind == Kind::Comment)
        {
            after.push_back(tokens.back());
            tokens = slice(tokens, 0, tokens.size() - 1);
        }
        if (std::any_of(tokens.begin(), tokens.end(), [](const Token& t) { return t.kind == Kind::Comment; }))
        {
            return text;
        }
        std::vector<Piece> pieces;
        wrap(tokens, line.indent, options, pieces);
        if (pieces.size() < 2)
        {
            return text;
        }
        if (!after.empty())
        {
            Token gap;
            gap.kind = Kind::Space;
            gap.text = " ";
            pieces.back().tokens.push_back(gap);
            pieces.back().tokens.push_back(after.front());
        }
        std::string out;
        for (const Piece& piece : pieces)
        {
            std::string one = options.spacing ? written(piece.tokens, piece.indent, options) : indentText(options, piece.indent) + asWritten(Line{ piece.tokens });
            while (!one.empty() && (one.back() == ' ' || one.back() == '\t'))
            {
                one.pop_back();
            }
            out += (out.empty() ? "" : "\n") + one;
        }
        return out;
    }

    // Each line as written out, or none for a blank line past a run's
    // length where the whole text is asked for; past the width broken
    // where `wrap`.
    std::vector<std::optional<std::string>> writtenLines(const std::vector<Line>& lines, const ALScriptFormatter::Options& options, S32 first,
                                                         S32 last, bool wrap)
    {
        std::vector<std::optional<std::string>> out;
        S32                                     blanks = 0;
        size_t                                  kept   = 0;
        const bool                              whole  = first < 0;
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
                    if (blanks > options.maxBlankLines || kept == 0)
                    {
                        out.emplace_back();
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
                    text += spaced(line, options);
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
                if (wrap)
                {
                    text = wrapped(line, options, text);
                }
            }
            if (!line.blank)
            {
                blanks = 0;
            }
            ++kept;
            out.push_back(std::move(text));
        }
        return out;
    }

    std::string emit(const std::vector<Line>& lines, const ALScriptFormatter::Options& options, bool endsWithNewline, S32 first, S32 last,
                     bool wrap)
    {
        const std::vector<std::optional<std::string>> each = writtenLines(lines, options, first, last, wrap);
        const bool                                    whole = first < 0;
        std::string                                   out;
        for (size_t i = 0; i < each.size(); ++i)
        {
            if (!each[i])
            {
                continue;
            }
            out += *each[i];
            if (i + 1 < each.size() || endsWithNewline)
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
    return emit(lines, options, endsWithNewline, -1, -1, true);
}

// static
std::string ALScriptFormatter::formatLines(std::string_view text, const Options& options, S32 first, S32 last)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    bool              endsWithNewline = false;
    std::vector<Line> lines           = linesOf(text, options.lua, endsWithNewline);
    decide(lines, options.lua);
    return emit(lines, options, endsWithNewline, first, last, false);
}

// static
std::vector<std::string> ALScriptFormatter::formatEach(std::string_view text, const Options& options, S32 first, S32 last)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    bool              endsWithNewline = false;
    std::vector<Line> lines           = linesOf(text, options.lua, endsWithNewline);
    decide(lines, options.lua);
    // Every line kept, blank or not: from the first where `first` is
    // below it, which would otherwise ask for the whole text's runs.
    const std::vector<std::optional<std::string>> each = writtenLines(lines, options, llmax(first, 0), last, true);
    // A line a string or a comment runs on over is as many of the text's;
    // a line broken at the width is still one.
    std::vector<std::string> out;
    for (size_t i = 0; i < each.size(); ++i)
    {
        const std::string& one = *each[i];
        if (lines[i].first == lines[i].last)
        {
            out.push_back(one);
            continue;
        }
        for (size_t at = 0;;)
        {
            const size_t nl = one.find('\n', at);
            out.push_back(one.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
            if (nl == std::string::npos)
            {
                break;
            }
            at = nl + 1;
        }
    }
    if (endsWithNewline)
    {
        out.emplace_back();
    }
    return out;
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

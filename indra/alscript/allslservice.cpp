/**
 * @file allslservice.cpp
 * @brief The LSL analyzer over Tailslide.
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

#include "allslservice.h"

#include "alscriptfixes.h"

#include "alscriptengine.h"

#include "almessagemap.h"
#include "allsltraits.h"
#include "alscriptlexicon.h"

#include "llfile.h"

#include "Luau/LSLBuiltins.h"

#include <tailslide/tailslide.hh>

#include <atomic>
#include <tailslide/visitor.hh>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>

namespace
{
    std::atomic<bool> sBuiltinsLoaded{ false };

    // Which of Tailslide's numbers the parser itself reports; the rest are
    // the semantic passes'.
    bool fromParser(Tailslide::ErrorCode code)
    {
        return code == Tailslide::E_SYNTAX_ERROR || code == Tailslide::E_PARSER_STACK_DEPTH;
    }

    // Tailslide counts lines and columns from one.
    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    // Whether a node's span holds a one-based position, ends inclusive.
    bool holds(const Tailslide::YYLTYPE& where, int line, int column)
    {
        if (where.first_line == 0)
        {
            return false;
        }
        const bool after_start = where.first_line < line || (where.first_line == line && where.first_column <= column);
        const bool before_end  = where.last_line > line || (where.last_line == line && where.last_column >= column);
        return after_start && before_end;
    }

    bool endsBefore(const Tailslide::YYLTYPE& where, int line, int column)
    {
        return where.last_line < line || (where.last_line == line && where.last_column <= column);
    }

    bool startsBefore(const Tailslide::YYLTYPE& where, int line, int column)
    {
        return where.first_line < line || (where.first_line == line && where.first_column < column);
    }

    std::string typeName(Tailslide::LSLType* type)
    {
        return type ? type->getNodeName() : std::string("void");
    }

    // The parameters of a function or an event, as they were declared.
    std::string parameterList(Tailslide::LSLParamList* params, std::vector<std::string>* each = nullptr)
    {
        std::string list;
        if (!params)
        {
            return list;
        }
        for (Tailslide::LSLASTNode* node = params->getChild(0); node; node = node->getNext())
        {
            if (node->getNodeType() != Tailslide::NODE_IDENTIFIER)
            {
                continue;
            }
            auto*       identifier = static_cast<Tailslide::LSLIdentifier*>(node);
            std::string one        = typeName(identifier->getType()) + " " + identifier->getName();
            if (!list.empty())
            {
                list += ", ";
            }
            list += one;
            if (each)
            {
                each->push_back(std::move(one));
            }
        }
        return list;
    }

    ALScriptSymbolKind kindOf(Tailslide::LSLSymbol* symbol)
    {
        switch (symbol->getSymbolType())
        {
            case Tailslide::SYM_FUNCTION:
                return ALScriptSymbolKind::Function;
            case Tailslide::SYM_STATE:
                return ALScriptSymbolKind::State;
            case Tailslide::SYM_LABEL:
                return ALScriptSymbolKind::Label;
            case Tailslide::SYM_EVENT:
                return ALScriptSymbolKind::Event;
            default:
                break;
        }
        switch (symbol->getSubType())
        {
            case Tailslide::SYM_FUNCTION_PARAMETER:
            case Tailslide::SYM_EVENT_PARAMETER:
                return ALScriptSymbolKind::Parameter;
            case Tailslide::SYM_BUILTIN:
                return ALScriptSymbolKind::Constant;
            default:
                return ALScriptSymbolKind::Variable;
        }
    }

    // As the declaration reads: `integer count`, `float f(integer a)`,
    // `state ready`, `touch_start(integer n)`, `@loop`.
    std::string declarationOf(Tailslide::LSLSymbol* symbol, std::vector<std::string>* parameters = nullptr)
    {
        const std::string name = symbol->getName();
        switch (symbol->getSymbolType())
        {
            case Tailslide::SYM_FUNCTION:
            {
                const std::string returns = symbol->getType() && symbol->getType()->getIType() != Tailslide::LST_NULL ? typeName(symbol->getType()) + " " : std::string();
                return returns + name + "(" + parameterList(symbol->getFunctionDecl(), parameters) + ")";
            }
            case Tailslide::SYM_EVENT:
                return name + "(" + parameterList(symbol->getFunctionDecl(), parameters) + ")";
            case Tailslide::SYM_STATE:
                return "state " + name;
            case Tailslide::SYM_LABEL:
                return "@" + name;
            default:
                return typeName(symbol->getType()) + " " + name;
        }
    }

    // The script parsed and its symbols resolved, for a query; the
    // messages are not the point here.
    // Every node whose span holds the position, outermost first. Several
    // children of one node can: a call gives its name and its argument
    // list the whole call's span.
    void holding(Tailslide::LSLASTNode* node, int line, int column, std::vector<Tailslide::LSLASTNode*>& found)
    {
        const Tailslide::YYLTYPE& where = *node->getLoc();
        const bool                unset = where.first_line == 0;
        if (!unset && !holds(where, line, column))
        {
            return;
        }
        if (!unset)
        {
            found.push_back(node);
        }
        for (Tailslide::LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            holding(child, line, column, found);
        }
    }

    // Where an identifier's name itself ends: the node's span can be the
    // whole call it names.
    int nameEnd(Tailslide::LSLIdentifier* identifier)
    {
        return identifier->getLoc()->first_column + static_cast<int>(strlen(identifier->getName()));
    }

    bool onName(Tailslide::LSLIdentifier* identifier, int line, int column)
    {
        const Tailslide::YYLTYPE& where = *identifier->getLoc();
        return where.first_line == line && where.first_column <= column && column <= nameEnd(identifier);
    }

    bool pastName(Tailslide::LSLIdentifier* identifier, int line, int column)
    {
        const Tailslide::YYLTYPE& where = *identifier->getLoc();
        return where.first_line < line || (where.first_line == line && column > nameEnd(identifier));
    }

    // The name alone, from where a location starts.
    ALScriptSpan nameSpanOf(const Tailslide::YYLTYPE& where, const char* name)
    {
        ALScriptSpan span;
        span.line      = zeroBased(where.first_line);
        span.column    = zeroBased(where.first_column);
        span.endLine   = span.line;
        span.endColumn = span.column + static_cast<S32>(strlen(name));
        return span;
    }

    // Everything a node spans. Tailslide's last column is the one after
    // the last character, counted from one -- the lexer moves it on by
    // each token's length -- which is the column after it counted from
    // zero less one.
    ALScriptSpan spanOf(const Tailslide::YYLTYPE& where)
    {
        ALScriptSpan span;
        span.line      = zeroBased(where.first_line);
        span.column    = zeroBased(where.first_column);
        span.endLine   = zeroBased(where.last_line);
        span.endColumn = zeroBased(where.last_column);
        return span;
    }

    // The identifier named at a position, resolved, or null.
    Tailslide::LSLSymbol* symbolAt(Tailslide::LSLScript* script, int one_line, int one_column)
    {
        std::vector<Tailslide::LSLASTNode*> path;
        holding(script, one_line, one_column, path);
        for (auto it = path.rbegin(); it != path.rend(); ++it)
        {
            if ((*it)->getNodeType() != Tailslide::NODE_IDENTIFIER || !onName(static_cast<Tailslide::LSLIdentifier*>(*it), one_line, one_column))
            {
                continue;
            }
            if (Tailslide::LSLSymbol* symbol = (*it)->getSymbol())
            {
                return symbol;
            }
        }
        return nullptr;
    }

    // Every identifier bound to one symbol. Each handler of an event is
    // a symbol of its own, so an event's places are every handler of it.
    struct Uses final : public Tailslide::ASTVisitor
    {
        Tailslide::LSLSymbol*     target;
        std::vector<ALScriptSpan> spans;

        explicit Uses(Tailslide::LSLSymbol* target_in)
        :   target(target_in)
        {
        }

        bool same(Tailslide::LSLSymbol* symbol) const
        {
            if (symbol == target)
            {
                return true;
            }
            return symbol && target->getSymbolType() == Tailslide::SYM_EVENT && symbol->getSymbolType() == Tailslide::SYM_EVENT
                   && strcmp(symbol->getName(), target->getName()) == 0;
        }

        bool visit(Tailslide::LSLIdentifier* identifier) override
        {
            if (same(identifier->getSymbol()) && identifier->getLoc()->first_line > 0)
            {
                spans.push_back(nameSpanOf(*identifier->getLoc(), identifier->getName()));
            }
            return true;
        }
    };

    // Whether an identifier is where its symbol is declared: the name of
    // a global, a local, a function, a state, an event handler or a
    // label, or a parameter in a declaration's list.
    bool declares(Tailslide::LSLIdentifier* identifier)
    {
        Tailslide::LSLASTNode* parent = identifier->getParent();
        if (!parent)
        {
            return false;
        }
        switch (parent->getNodeType())
        {
            case Tailslide::NODE_GLOBAL_VARIABLE:
            case Tailslide::NODE_GLOBAL_FUNCTION:
            case Tailslide::NODE_STATE:
            case Tailslide::NODE_EVENT_HANDLER:
                return parent->getChild(0) == identifier;
            case Tailslide::NODE_FUNCTION_DEC:
            case Tailslide::NODE_EVENT_DEC:
                return true;
            case Tailslide::NODE_STATEMENT:
                return (parent->getNodeSubType() == Tailslide::NODE_DECLARATION || parent->getNodeSubType() == Tailslide::NODE_LABEL)
                       && parent->getChild(0) == identifier;
            default:
                return false;
        }
    }

    // Every identifier bound to a symbol, by what the symbol is.
    // A visitor that passes over the globals, functions and states lying
    // wholly within lines nobody reads -- an include's -- rather than
    // going down into them. Tailslide's lines count from one.
    struct PassingOver : public Tailslide::ASTVisitor
    {
        using Tailslide::ASTVisitor::visit;
        const std::vector<std::pair<S32, S32>>* passedOver = nullptr;

        bool read(Tailslide::LSLASTNode* node) const
        {
            if (!passedOver || passedOver->empty())
            {
                return true;
            }
            const Tailslide::YYLTYPE* loc = node->getLoc();
            return !ALSourceMap::within(*passedOver, zeroBased(loc->first_line), zeroBased(loc->last_line));
        }
        bool visit(Tailslide::LSLGlobalVariable* node) override { return read(node); }
        bool visit(Tailslide::LSLGlobalFunction* node) override { return read(node); }
        bool visit(Tailslide::LSLState* node) override { return read(node); }
    };

    struct Semantics final : public PassingOver
    {
        using PassingOver::visit;
        std::vector<ALScriptSemanticToken> out;

        bool visit(Tailslide::LSLIdentifier* identifier) override
        {
            Tailslide::LSLSymbol* symbol = identifier->getSymbol();
            if (!symbol || identifier->getLoc()->first_line == 0)
            {
                return true;
            }
            ALScriptSemanticToken token;
            token.span = nameSpanOf(*identifier->getLoc(), identifier->getName());
            token.kind = kindOf(symbol);
            switch (symbol->getSubType())
            {
                case Tailslide::SYM_BUILTIN:
                    token.modifiers |= ALScriptSemanticToken::Builtin;
                    if (symbol->getSymbolType() == Tailslide::SYM_VARIABLE)
                    {
                        token.modifiers |= ALScriptSemanticToken::ReadOnly;
                    }
                    break;
                case Tailslide::SYM_GLOBAL:
                    token.modifiers |= ALScriptSemanticToken::Global;
                    break;
                default:
                    break;
            }
            if (symbol->getSymbolType() == Tailslide::SYM_EVENT)
            {
                token.modifiers |= ALScriptSemanticToken::Builtin;
            }
            if (declares(identifier))
            {
                token.modifiers |= ALScriptSemanticToken::Declaration;
            }
            out.push_back(std::move(token));
            return true;
        }
    };

    // Each argument of each call, by the parameter it fills.
    struct Hints final : public PassingOver
    {
        using PassingOver::visit;
        std::vector<ALScriptInlayHint> out;

        bool visit(Tailslide::LSLFunctionExpression* call) override
        {
            Tailslide::LSLSymbol* symbol = call->getSymbol();
            Tailslide::LSLASTNode* args  = call->getArguments();
            if (!symbol || !symbol->getFunctionDecl() || !args)
            {
                return true;
            }
            Tailslide::LSLASTNode* param = symbol->getFunctionDecl()->getChild(0);
            for (Tailslide::LSLASTNode* arg = args->getChild(0); arg && param; arg = arg->getNext(), param = param->getNext())
            {
                if (param->getNodeType() != Tailslide::NODE_IDENTIFIER || arg->getLoc()->first_line == 0)
                {
                    continue;
                }
                const char* name = static_cast<Tailslide::LSLIdentifier*>(param)->getName();
                if (!name || !*name)
                {
                    continue;
                }
                // Nothing where the argument is the name already.
                if (arg->getNodeSubType() == Tailslide::NODE_LVALUE_EXPRESSION)
                {
                    Tailslide::LSLIdentifier* given = static_cast<Tailslide::LSLLValueExpression*>(arg)->getIdentifier();
                    if (given && strcmp(given->getName(), name) == 0 && !static_cast<Tailslide::LSLLValueExpression*>(arg)->getMember())
                    {
                        continue;
                    }
                }
                ALScriptInlayHint hint;
                hint.line   = zeroBased(arg->getLoc()->first_line);
                hint.column = zeroBased(arg->getLoc()->first_column);
                hint.kind   = ALScriptInlayHint::Kind::Parameter;
                hint.text   = std::string(name) + ":";
                out.push_back(std::move(hint));
            }
            return true;
        }
    };

    // A declaration's identifier, which is its first child.
    Tailslide::LSLIdentifier* identifierOf(Tailslide::LSLASTNode* node)
    {
        Tailslide::LSLASTNode* first = node->getChild(0);
        return first && first->getNodeType() == Tailslide::NODE_IDENTIFIER ? static_cast<Tailslide::LSLIdentifier*>(first) : nullptr;
    }

    // Where a symbol the script declares is named in its declaration: a
    // symbol's own location is its declaration's start, which for a
    // global is the type before the name, so the name is the first
    // identifier bound to it on that line from there on. False for a
    // builtin, or a symbol with no location.
    bool declarationOf(Tailslide::LSLScript* script, Tailslide::LSLSymbol* symbol, ALScriptSpan& span, std::vector<ALScriptSpan>* every = nullptr)
    {
        if (symbol->getSubType() == Tailslide::SYM_BUILTIN || symbol->getLoc()->first_line == 0)
        {
            return false;
        }
        Uses uses(symbol);
        script->visit(&uses);
        std::sort(uses.spans.begin(), uses.spans.end());
        uses.spans.erase(std::unique(uses.spans.begin(), uses.spans.end()), uses.spans.end());
        const S32 line   = zeroBased(symbol->getLoc()->first_line);
        const S32 column = zeroBased(symbol->getLoc()->first_column);
        bool      found  = false;
        for (const ALScriptSpan& one : uses.spans)
        {
            if (one.line == line && one.column >= column)
            {
                span  = one;
                found = true;
                break;
            }
        }
        if (!found)
        {
            span = nameSpanOf(*symbol->getLoc(), symbol->getName());
        }
        if (every)
        {
            *every = std::move(uses.spans);
        }
        return true;
    }

    // --- a text mended to parse ---------------------------------------------------

    // Which bytes of a text are code, a string's -- its quotes too -- or
    // a comment's, and what the end of it is inside.
    enum : U8
    {
        COMMENT_BYTE = 0,
        CODE_BYTE    = 1,
        STRING_BYTE  = 2
    };
    struct Lexed
    {
        std::vector<U8> code;
        bool            inString  = false;
        bool            inComment = false;
    };

    Lexed lex(std::string_view text)
    {
        Lexed out;
        out.code.assign(text.size(), 0);
        enum class In { Code, String, LineComment, BlockComment } in = In::Code;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c    = text[i];
            const char next = i + 1 < text.size() ? text[i + 1] : '\0';
            switch (in)
            {
                case In::Code:
                    if (c == '"')
                    {
                        in          = In::String;
                        out.code[i] = STRING_BYTE;
                    }
                    else if (c == '/' && next == '/')
                    {
                        in = In::LineComment;
                        ++i;
                    }
                    else if (c == '/' && next == '*')
                    {
                        in = In::BlockComment;
                        ++i;
                    }
                    else
                    {
                        out.code[i] = CODE_BYTE;
                    }
                    break;
                case In::String:
                    out.code[i] = STRING_BYTE;
                    if (c == '\\')
                    {
                        if (i + 1 < text.size())
                        {
                            out.code[++i] = STRING_BYTE;
                        }
                    }
                    else if (c == '"')
                    {
                        in = In::Code;
                    }
                    break;
                case In::LineComment:
                    if (c == '\n')
                    {
                        in          = In::Code;
                        out.code[i] = CODE_BYTE;
                    }
                    break;
                case In::BlockComment:
                    if (c == '*' && next == '/')
                    {
                        in = In::Code;
                        ++i;
                    }
                    break;
            }
        }
        out.inString  = in == In::String;
        out.inComment = in == In::BlockComment;
        return out;
    }

    // A zero-based line and byte column as an offset, clamped to the line.
    size_t offsetOf(std::string_view text, S32 line, S32 column)
    {
        size_t at = 0;
        for (S32 l = 0; l < line; ++l)
        {
            const size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
            {
                return text.size();
            }
            at = end + 1;
        }
        const size_t end = std::min(text.find('\n', at), text.size());
        return std::min(at + static_cast<size_t>(std::max(0, column)), end);
    }

    void placeOf(std::string_view text, size_t offset, S32& line, S32& column)
    {
        line                = 0;
        size_t line_start   = 0;
        for (size_t i = 0; i < offset && i < text.size(); ++i)
        {
            if (text[i] == '\n')
            {
                ++line;
                line_start = i + 1;
            }
        }
        column = static_cast<S32>(offset - line_start);
    }

    // Where the parser first stopped, as an offset, or npos where it did not.
    size_t stoppedAt(Tailslide::ScopedScriptParser& parser, std::string_view text)
    {
        size_t first = std::string_view::npos;
        for (Tailslide::LogMessage* message : parser.logger.getMessages())
        {
            if (message->getError() == Tailslide::E_SYNTAX_ERROR)
            {
                const auto* where = message->getLoc();
                first             = std::min(first, offsetOf(text, zeroBased(where->first_line), zeroBased(where->first_column)));
            }
        }
        return first;
    }

    bool identifierByte(char c)
    {
        return ALScriptLexicon::isNameByte(c);
    }

    // The statement the caret is in, closed where the caret is: a string
    // it is inside ended, an operand put after an operator or a comma
    // left hanging, the brackets it opened closed, and the statement
    // ended -- or its block opened, where the rest of the line opened
    // one. The rest of the caret's line goes; everything before the caret
    // and every later line stays where it was.
    std::string closedAt(std::string_view text, size_t caret)
    {
        const std::string_view prefix = text.substr(0, caret);
        const Lexed            lexed  = lex(prefix);
        std::string            tail;
        if (lexed.inComment)
        {
            tail += "*/";
        }
        std::vector<char> open;
        char              last = '\0';
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (lexed.code[i] != CODE_BYTE)
            {
                continue;
            }
            const char c = prefix[i];
            if (c == '(' || c == '[' || c == '{')
            {
                open.push_back(c);
            }
            else if ((c == ')' || c == ']' || c == '}') && !open.empty())
            {
                open.pop_back();
            }
            if (!isspace(static_cast<unsigned char>(c)))
            {
                last = c;
            }
        }
        if (lexed.inString)
        {
            tail += '"';
            last = '"';
        }
        else if (last == '.')
        {
            tail += "x";
        }
        else if (last != '\0' && strchr(",+-*/%=<>&|^!~", last))
        {
            tail += "0";
        }
        bool statement = last != '\0' && last != ';' && last != '{' && last != '}';
        for (auto it = open.rbegin(); it != open.rend() && *it != '{'; ++it)
        {
            tail += *it == '(' ? ')' : ']';
            statement = true;
        }
        const size_t     line_end = std::min(text.find('\n', caret), text.size());
        const std::string_view rest = text.substr(caret, line_end - caret);
        const Lexed      rest_lexed = lex(rest);
        S32              braces     = 0;
        for (size_t i = 0; i < rest.size(); ++i)
        {
            if (rest_lexed.code[i] == CODE_BYTE)
            {
                braces += rest[i] == '{' ? 1 : rest[i] == '}' ? -1 : 0;
            }
        }
        if (braces > 0)
        {
            tail += " " + std::string(static_cast<size_t>(braces), '{');
        }
        else
        {
            if (statement)
            {
                tail += ";";
            }
            tail += std::string(static_cast<size_t>(-braces), '}');
        }
        return std::string(prefix) + tail + std::string(text.substr(line_end));
    }

    // One step nearer parsing, where the parser stopped at `at`: the
    // statement it stopped in blanked back to where it began, the token
    // it stopped on where there was nothing before it to blank, or the
    // blocks left open at the end closed. Blanking keeps every line and
    // column where it was. False where nothing more can be done.
    bool mendAt(std::string& text, size_t at)
    {
        const Lexed lexed = lex(text);
        size_t      rest  = at;
        while (rest < text.size() && isspace(static_cast<unsigned char>(text[rest])))
        {
            ++rest;
        }
        if (rest >= text.size())
        {
            // At the end: the blocks still open closed after it.
            S32 depth = 0;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (lexed.code[i] == CODE_BYTE)
                {
                    depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
                }
            }
            if (depth > 0)
            {
                text += "\n" + std::string(static_cast<size_t>(depth), '}');
                return true;
            }
        }
        size_t start = std::min(at, text.size());
        while (start > 0 && !(lexed.code[start - 1] == CODE_BYTE && (text[start - 1] == ';' || text[start - 1] == '{' || text[start - 1] == '}')))
        {
            --start;
        }
        bool blanked = false;
        for (size_t i = start; i < at && i < text.size(); ++i)
        {
            if (!isspace(static_cast<unsigned char>(text[i])))
            {
                text[i] = ' ';
                blanked = true;
            }
        }
        if (blanked || rest >= text.size())
        {
            return blanked;
        }
        // Nothing before it: the token itself.
        size_t end = rest + 1;
        if (identifierByte(text[rest]))
        {
            while (end < text.size() && identifierByte(text[end]))
            {
                ++end;
            }
        }
        for (size_t i = rest; i < end; ++i)
        {
            text[i] = ' ';
        }
        return true;
    }

    // --- what the parser says, said plainly ------------------------------------------

    // A token as the parser names it, as a scripter would: a character
    // or a word as it is written, a kind of token by what it is.
    std::string tokenWords(std::string_view name)
    {
        if (name.size() >= 3 && name.front() == '\'' && name.back() == '\'')
        {
            return std::string(name);
        }
        static const std::pair<const char*, const char*> NAMES[] = {
            { "\"end of file\"", "the end of the script" }, { "IDENTIFIER", "a name" }, { "EVENT", "an event's name" },
            { "INTEGER_CONSTANT", "a number" }, { "FP_CONSTANT", "a number" }, { "STRING_CONSTANT", "a string" },
            { "STATE_DEFAULT", "'default'" }, { "STATE", "'state'" }, { "JUMP", "'jump'" }, { "RETURN", "'return'" },
            { "IF", "'if'" }, { "ELSE", "'else'" }, { "FOR", "'for'" }, { "DO", "'do'" }, { "WHILE", "'while'" },
            { "PRINT", "'print'" }, { "INTEGER", "'integer'" }, { "FLOAT_TYPE", "'float'" }, { "STRING", "'string'" },
            { "LLKEY", "'key'" }, { "VECTOR", "'vector'" }, { "QUATERNION", "'rotation'" }, { "LIST", "'list'" },
            { "INC_OP", "'++'" }, { "DEC_OP", "'--'" }, { "ADD_ASSIGN", "'+='" }, { "SUB_ASSIGN", "'-='" },
            { "MUL_ASSIGN", "'*='" }, { "DIV_ASSIGN", "'/='" }, { "MOD_ASSIGN", "'%='" }, { "EQ", "'=='" },
            { "NEQ", "'!='" }, { "GEQ", "'>='" }, { "LEQ", "'<='" }, { "BOOLEAN_AND", "'&&'" }, { "BOOLEAN_OR", "'||'" },
            { "SHIFT_LEFT", "'<<'" }, { "SHIFT_RIGHT", "'>>'" }, { "PERIOD", "'.'" },
        };
        for (const auto& [token, words] : NAMES)
        {
            if (name == token)
            {
                return words;
            }
        }
        return std::string(name);
    }

    // Where the line starting at `from` -- the one the parser stopped on,
    // or the one before it where that begins with the brace a head opens --
    // begins a handler or a state inside a block left open: the offset its
    // line starts at. A handler's head is an event's name and its bracket;
    // a state's, `default` or `state` and its name before a brace.
    std::optional<size_t> unclosedBefore(std::string_view source, const Lexed& lexed, size_t from, Tailslide::LSLSymbolTable* builtins)
    {
        const auto first_code = [&](size_t at) {
            while (at < source.size() && (source[at] == ' ' || source[at] == '\t' || lexed.code[at] == COMMENT_BYTE))
            {
                ++at;
            }
            return at;
        };
        const auto word_at = [&](size_t at) {
            size_t end = at;
            while (end < source.size() && identifierByte(source[end]) && lexed.code[end] == CODE_BYTE)
            {
                ++end;
            }
            return source.substr(at, end - at);
        };
        const auto next_code = [&](size_t at) {
            while (at < source.size() && (isspace(static_cast<unsigned char>(source[at])) || lexed.code[at] == COMMENT_BYTE))
            {
                ++at;
            }
            return at < source.size() ? source[at] : '\0';
        };
        // What the line starting at `line` begins: 2 for a handler, whose
        // head sits in its state's braces; 1 for a state, in none; 0 for
        // anything else.
        const auto begins = [&](size_t line) -> S32 {
            const size_t           at   = first_code(line);
            const std::string_view word = word_at(at);
            if (word.empty())
            {
                return 0;
            }
            if (word == "default")
            {
                return next_code(at + word.size()) == '{' ? 1 : 0;
            }
            if (word == "state")
            {
                const size_t           named = first_code(at + word.size());
                const std::string_view name  = word_at(named);
                return !name.empty() && next_code(named + name.size()) == '{' ? 1 : 0;
            }
            if (builtins && next_code(at + word.size()) == '(' && builtins->lookup(std::string(word).c_str(), Tailslide::SYM_EVENT))
            {
                return 2;
            }
            return 0;
        };
        size_t line  = from;
        S32    level = begins(line);
        if (level == 0 && first_code(line) < source.size() && source[first_code(line)] == '{' && line > 0)
        {
            // A brace on its own line: the head it opens, on the line of
            // code before.
            size_t back = line - 1;
            while (back > 0 && (lexed.code[back - 1] == COMMENT_BYTE || isspace(static_cast<unsigned char>(source[back - 1]))))
            {
                --back;
            }
            const size_t start = source.rfind('\n', back == 0 ? 0 : back - 1);
            line               = start == std::string_view::npos || back == 0 ? 0 : start + 1;
            level              = begins(line);
        }
        if (level == 0)
        {
            return std::nullopt;
        }
        // The braces open where the line starts, counting only code's.
        S32 open = 0;
        for (size_t i = 0; i < line; ++i)
        {
            if (lexed.code[i] == CODE_BYTE)
            {
                open += source[i] == '{' ? 1 : source[i] == '}' ? -1 : 0;
            }
        }
        return open >= level ? std::optional<size_t>(line) : std::nullopt;
    }

    // Bison's "syntax error, unexpected X, expecting Y or Z" said as a
    // scripter would: a single bracket or semicolon it wanted is missing,
    // and is marked on the last thing before the place it stopped --
    // where it is missing, rather than on the next line's brace -- and
    // anything else is unexpected, with what was wanted where it says.
    // Keyed, as the map's messages are, for the studio to translate.
    // `lexed` is the text lexed, once for all its syntax errors, made the
    // first time one wants it; `builtins` names the events.
    void plainSyntaxError(std::string_view source, ALScriptProblem& problem, std::optional<Lexed>& lexed_once,
                          Tailslide::LSLSymbolTable* builtins)
    {
        const std::string& said = problem.message;
        const std::string  UNEXPECTED("syntax error, unexpected ");
        if (said.compare(0, UNEXPECTED.size(), UNEXPECTED) != 0)
        {
            return;
        }
        std::string              found = said.substr(UNEXPECTED.size());
        std::vector<std::string> wanted;
        const size_t             expecting = found.find(", expecting ");
        if (expecting != std::string::npos)
        {
            std::string rest = found.substr(expecting + 12);
            found.resize(expecting);
            for (size_t at = 0; at != std::string::npos;)
            {
                const size_t next = rest.find(" or ", at);
                wanted.push_back(tokenWords(rest.substr(at, next == std::string::npos ? std::string::npos : next - at)));
                at = next == std::string::npos ? next : next + 4;
            }
        }
        // The last character before where the parser stopped, past blanks
        // and comments, and whether nothing but blanks is before the stop
        // on its line.
        if (!lexed_once)
        {
            lexed_once = lex(source);
        }
        const Lexed& lexed   = *lexed_once;
        const size_t stopped = std::min(offsetOf(source, problem.line, problem.column), source.size());
        size_t       before  = stopped;
        while (before > 0 && (lexed.code[before - 1] == COMMENT_BYTE || isspace(static_cast<unsigned char>(source[before - 1]))))
        {
            --before;
        }
        const size_t line_start = source.rfind('\n', stopped == 0 ? 0 : stopped - 1);
        const size_t from       = line_start == std::string_view::npos || stopped == 0 ? 0 : line_start + 1;
        const bool   first      = source.substr(from, stopped - from).find_first_not_of(" \t") == std::string_view::npos;
        // A value ends there: a name, a number, a closing bracket, a string.
        const char   last        = before > 0 ? source[before - 1] : '\0';
        const bool   value_ended = before > 0 && (identifierByte(last) || last == ')' || last == ']' || lexed.code[before - 1] == STRING_BYTE);
        // A handler or a state begun inside a block not closed -- a
        // handler's `}` left off, and the next handler read as a call, or
        // the next state as a statement: the brace is what is missing,
        // after the last code before the line it begins on, whatever the
        // parser wanted of the head it misread. By the braces open where
        // that line starts: a handler's head sits in its state's, and a
        // state's in none.
        if (const std::optional<size_t> head = unclosedBefore(source, lexed, from, builtins))
        {
            size_t end = *head;
            while (end > 0 && (lexed.code[end - 1] == COMMENT_BYTE || isspace(static_cast<unsigned char>(source[end - 1]))))
            {
                --end;
            }
            if (end > 0)
            {
                problem.key     = "LSLSyntaxMissing";
                problem.args    = { "'}'" };
                problem.message = ALScriptProblem::fill("Missing [1].", problem.args);
                placeOf(source, end - 1, problem.line, problem.column);
                problem.endLine   = problem.line;
                problem.endColumn = problem.column + 1;
                return;
            }
        }
        // A name straight after a bracket or a comma, where any expression
        // could have a name, is one where only a declaration's parameters
        // go: a function's or an event's, written without its type.
        if (found == "IDENTIFIER" && before > 0 && (last == '(' || last == ',') && lexed.code[before - 1] == CODE_BYTE && stopped < source.size() &&
            identifierByte(source[stopped]))
        {
            size_t end = stopped;
            while (end < source.size() && identifierByte(source[end]))
            {
                ++end;
            }
            problem.key       = "LSLParameterUntyped";
            problem.args      = { std::string(source.substr(stopped, end - stopped)) };
            problem.message   = ALScriptProblem::fill("The parameter '[1]' needs its type before it: integer, float, string, key, vector, rotation or list.",
                                                      problem.args);
            problem.endLine   = problem.line;
            problem.endColumn = problem.column + static_cast<S32>(end - stopped);
            return;
        }
        static const char* const CLOSERS[] = { "';'", "')'", "']'", "'}'", "','", "'('" };
        std::string              missing;
        if (wanted.size() == 1 && std::find(std::begin(CLOSERS), std::end(CLOSERS), wanted.front()) != std::end(CLOSERS))
        {
            missing = wanted.front();
        }
        else if (first && value_ended && (wanted.empty() || std::find(wanted.begin(), wanted.end(), "';'") != wanted.end()))
        {
            // The parser met the next line's first word with the statement
            // before it still open and more than one way to go on, so it
            // named none: the semicolon it most likely wanted.
            missing = "';'";
        }
        if (!missing.empty())
        {
            problem.key     = "LSLSyntaxMissing";
            problem.args    = { missing };
            problem.message = ALScriptProblem::fill("Missing [1].", problem.args);
            if (before > 0)
            {
                placeOf(source, before - 1, problem.line, problem.column);
                problem.endLine   = problem.line;
                problem.endColumn = problem.column + 1;
            }
            return;
        }
        std::string list;
        for (size_t i = 0; i < wanted.size(); ++i)
        {
            list += (i == 0 ? "" : i + 1 == wanted.size() ? " or " : ", ") + wanted[i];
        }
        problem.key     = wanted.empty() ? "LSLSyntaxUnexpected" : "LSLSyntaxUnexpectedWanted";
        problem.args    = { tokenWords(found), list };
        problem.message = ALScriptProblem::fill(wanted.empty() ? "Did not expect [1] here." : "Did not expect [1] here; expected [2].", problem.args);
        problem.args.resize(wanted.empty() ? 1 : 2);
    }
    // Whether a node declares something a warning could say goes unused: a
    // local, a global, a function, a label or a state -- not a parameter,
    // which cannot go.
    bool declares(Tailslide::LSLASTNode* node)
    {
        const Tailslide::LSLNodeType    type = node->getNodeType();
        const Tailslide::LSLNodeSubType sub  = node->getNodeSubType();
        return type == Tailslide::NODE_GLOBAL_VARIABLE || type == Tailslide::NODE_GLOBAL_FUNCTION || type == Tailslide::NODE_STATE ||
               (type == Tailslide::NODE_STATEMENT && (sub == Tailslide::NODE_DECLARATION || sub == Tailslide::NODE_LABEL));
    }

    // The declaration a warning about something unused is at: the one that
    // starts there, or whose name does -- Tailslide places the warning at
    // the symbol, whose place is the one or the other.
    Tailslide::LSLASTNode* declarationAt(Tailslide::LSLASTNode* node, S32 line, S32 column)
    {
        if (!node)
        {
            return nullptr;
        }
        const auto at = [line, column](Tailslide::LSLASTNode* one) {
            const Tailslide::YYLTYPE* loc = one->getLoc();
            return zeroBased(loc->first_line) == line && zeroBased(loc->first_column) == column;
        };
        if (declares(node) && (at(node) || (node->getChild(0) && at(node->getChild(0)))))
        {
            return node;
        }
        for (Tailslide::LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            if (Tailslide::LSLASTNode* found = declarationAt(child, line, column))
            {
                return found;
            }
        }
        return nullptr;
    }

    // The names in scope at a place, as a name there could be spelt: the
    // script's own -- a local only from where it is declared -- and, with
    // `builtins`, the language's functions and constants.
    // The builtins' names that can be misspelt -- functions and constants
    // -- listed once: the table is the process's, loaded once, and some two
    // thousand names were listed again for every name not known.
    std::string declared(Tailslide::LSLType* type);

    // A handler's parameters written as its event takes them -- the types
    // the builtins say, and the names the handler gave them where it gave
    // them -- where they are wrong, too many, too few, or one has no type.
    // Over the bracket after the event's name, where the list closes on
    // its line.
    void offerEventParameters(ALScriptProblem& problem, const ALScriptFixes::Lines& lines, Tailslide::LSLSymbolTable* builtins)
    {
        const std::string_view line = lines.line(problem.line);
        std::string            event;
        size_t                 open = std::string_view::npos;
        if (problem.key == "LSLArgumentWrongTypeEvent" && problem.args.size() == 5)
        {
            event = problem.args[2];
        }
        else if ((problem.key == "LSLTooManyArgumentsEvent" || problem.key == "LSLTooFewArgumentsEvent") && problem.args.size() == 1)
        {
            event = problem.args[0];
        }
        else if (problem.key == "LSLParameterUntyped" && problem.column > 0 && static_cast<size_t>(problem.column) <= line.size())
        {
            // The name before the bracket the parameter is in.
            open = line.rfind('(', static_cast<size_t>(problem.column) - 1);
            size_t name_end = open;
            while (name_end != std::string_view::npos && name_end > 0 && isspace(static_cast<unsigned char>(line[name_end - 1])))
            {
                --name_end;
            }
            size_t name_from = name_end == std::string_view::npos ? name_end : name_end;
            while (name_from != std::string_view::npos && name_from > 0 && identifierByte(line[name_from - 1]))
            {
                --name_from;
            }
            if (open != std::string_view::npos && name_from < name_end)
            {
                event = std::string(line.substr(name_from, name_end - name_from));
            }
        }
        Tailslide::LSLSymbol* symbol = event.empty() || !builtins ? nullptr : builtins->lookup(event.c_str(), Tailslide::SYM_EVENT);
        if (!symbol || !symbol->getFunctionDecl())
        {
            return;
        }
        if (open == std::string_view::npos)
        {
            const size_t named = line.find(event, static_cast<size_t>(llmax(0, problem.column)));
            open               = named == std::string_view::npos ? named : line.find('(', named + event.size());
        }
        const size_t close = open == std::string_view::npos ? open : line.find(')', open);
        if (close == std::string_view::npos)
        {
            return;
        }
        // The names the handler gave, in order: each parameter's last word.
        std::vector<std::string> given;
        const std::string_view   inside = line.substr(open + 1, close - open - 1);
        for (size_t from = 0; from <= inside.size();)
        {
            const size_t           comma = inside.find(',', from);
            const std::string_view one   = inside.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
            size_t                 end   = one.find_last_not_of(" \t");
            size_t                 start = end;
            while (start != std::string_view::npos && start > 0 && identifierByte(one[start - 1]))
            {
                --start;
            }
            if (end != std::string_view::npos && identifierByte(one[end]))
            {
                given.emplace_back(one.substr(start, end - start + 1));
            }
            if (comma == std::string_view::npos)
            {
                break;
            }
            from = comma + 1;
        }
        std::string parameters;
        size_t      n = 0;
        for (Tailslide::LSLASTNode* node = symbol->getFunctionDecl()->getChild(0); node; node = node->getNext())
        {
            if (node->getNodeType() == Tailslide::NODE_IDENTIFIER)
            {
                auto* identifier = static_cast<Tailslide::LSLIdentifier*>(node);
                parameters += (parameters.empty() ? "" : ", ") + declared(identifier->getType()) + " " + (n < given.size() ? given[n] : identifier->getName());
                ++n;
            }
        }
        if (parameters == inside)
        {
            return;
        }
        ALScriptFix fix = ALScriptFixes::titled("ScriptFixEventParameters", "Write the parameters '[1]' takes", { event });
        fix.preferred   = true;
        fix.edits.push_back({ problem.line, static_cast<S32>(open) + 1, problem.line, static_cast<S32>(close), parameters });
        problem.fixes.push_back(std::move(fix));
    }

    const std::vector<std::string>& builtinNames(Tailslide::LSLSymbolTable* builtins)
    {
        static std::mutex                                 lock;
        static Tailslide::LSLSymbolTable*                 listed = nullptr;
        static size_t                                     count  = 0;
        static std::vector<std::string>                   names;
        const std::lock_guard<std::mutex>                 guard(lock);
        if (builtins && (builtins != listed || builtins->getMap().size() != count))
        {
            names.clear();
            for (auto& [name, symbol] : builtins->getMap())
            {
                if (symbol->getSymbolType() == Tailslide::SYM_FUNCTION || symbol->getSymbolType() == Tailslide::SYM_VARIABLE)
                {
                    names.emplace_back(symbol->getName());
                }
            }
            listed = builtins;
            count  = builtins->getMap().size();
        }
        return names;
    }

    // The script's own names in scope at a place: its globals, and what
    // encloses the place declared before it. The builtins are builtinNames.
    std::vector<std::string> namesAt(Tailslide::LSLScript* script, S32 line, S32 column)
    {
        std::vector<std::string>            out;
        std::vector<Tailslide::LSLASTNode*> path;
        holding(script, line + 1, column + 1, path);
        for (Tailslide::LSLASTNode* node : path)
        {
            Tailslide::LSLSymbolTable* table = node->getSymbolTable();
            if (!table)
            {
                continue;
            }
            const bool lexical = table->getTableType() == Tailslide::SYMTAB_LEXICAL;
            for (auto& [name, symbol] : table->getMap())
            {
                if (!lexical || startsBefore(*symbol->getLoc(), line + 1, column + 1))
                {
                    out.emplace_back(symbol->getName());
                }
            }
        }
        return out;
    }
}

struct ALLSLService::Impl
{
    bool builtins = false;

    // The script last parsed and resolved, kept: one request asks four
    // questions of the same text -- what is wrong with it, what it
    // declares, what every name is, what goes beside it -- and each was
    // parsing the whole script again. The parser owns the tree, so it
    // is kept with it.
    std::unique_ptr<Tailslide::ScopedScriptParser> parser;
    std::string                                    text;
    Tailslide::LSLScript*                          script = nullptr;
    bool                                           parsed = false;
    // Whether a check has run its later passes over the tree: each says
    // what it finds into the parser's log, so a second run over the same
    // tree says everything twice, and one for the other target says it
    // on top of what the first said.
    bool                                           passesRan = false;

    // The tree for a text, parsed and resolved if it is not the one in
    // hand. Null where it does not parse.
    Tailslide::LSLScript* resolve(std::string_view source)
    {
        if (parser && text == source)
        {
            return script;
        }
        parser = std::make_unique<Tailslide::ScopedScriptParser>(nullptr);
        text.assign(source);
        script    = parser->parseLSLBytes(text.data(), static_cast<int>(text.size()));
        parsed    = script != nullptr && !parser->logger.getErrors();
        passesRan = false;
        if (script)
        {
            script->collectSymbols();
            script->determineTypes();
        }
        return script;
    }

    // The tree let go of: what a check that has run the later passes
    // over it leaves behind, since those passes change it and say what
    // they find into its log.
    void forget()
    {
        parser.reset();
        text.clear();
        script    = nullptr;
        parsed    = false;
        passesRan = false;
        mendedWhole   = Mended();
        mendedAtPlace = Mended();
    }

    // A script being typed seldom parses -- a call not yet closed, a
    // statement not yet ended -- and Tailslide answers nothing of a text
    // that does not: no scope, no call, no names. So the questions are
    // asked of a copy mended to parse: the statement at the caret, where
    // there is one, closed where the caret is; then each statement the
    // parser stops in blanked, and the blocks left open at the end
    // closed. Every position before the caret, and every line after its
    // own, is where it was, so the answers need no translating back.
    struct Mended
    {
        std::unique_ptr<Tailslide::ScopedScriptParser> parser;
        std::string                                    source;
        S32                                            line   = -1;
        S32                                            column = -1;
        Tailslide::LSLScript*                          script = nullptr;
    };
    // Two kept apart: the whole text's, which a check reads, and one
    // closed at a place, which a question at the caret reads. Each is made
    // again only when its own text or place moves; one slot for both was
    // made again at every turn between a check and a question while the
    // text did not parse.
    Mended                                         mendedWhole;
    Mended                                         mendedAtPlace;
    // How many copies have been mended, for the test that says so.
    size_t                                         mendings     = 0;
    // The lines nobody reads the names, hints and fixes of (setPassedOver).
    std::vector<std::pair<S32, S32>>               passedOver;
    // Whether the last question had a tree to be answered from.
    bool                                           understood   = false;

    // The tree to answer from: the text's own where it parses, else the
    // mended copy's, closed at the position given where one is; null
    // where no mending made it parse.
    Tailslide::LSLScript* understand(std::string_view source, S32 line = -1, S32 column = -1)
    {
        if (Tailslide::LSLScript* own = resolve(source))
        {
            understood = true;
            return own;
        }
        Mended& slot = line >= 0 ? mendedAtPlace : mendedWhole;
        if (slot.parser && slot.source == source && slot.line == line && slot.column == column)
        {
            understood = slot.script != nullptr;
            return slot.script;
        }
        ++mendings;
        slot.source.assign(source);
        slot.line        = line;
        slot.column      = column;
        slot.script      = nullptr;
        std::string copy = line >= 0 ? closedAt(source, offsetOf(source, line, column)) : std::string(source);
        // Each try blanks a statement or closes the end; a handful mends
        // what one pause in typing leaves, and a text broken in more
        // places than that is left as it is.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            slot.parser = std::make_unique<Tailslide::ScopedScriptParser>(nullptr);
            slot.script = slot.parser->parseLSLBytes(copy.data(), static_cast<int>(copy.size()));
            if (slot.script)
            {
                slot.script->collectSymbols();
                slot.script->determineTypes();
                break;
            }
            const size_t at = stoppedAt(*slot.parser, copy);
            if (at == std::string_view::npos || !mendAt(copy, at))
            {
                break;
            }
        }
        understood = slot.script != nullptr;
        return slot.script;
    }
};

ALLSLService::ALLSLService()
:   mImpl(std::make_unique<Impl>())
{
}

ALLSLService::~ALLSLService() = default;

namespace
{
    // What a builtins line declares: a constant's name after its type, an
    // event's after `event`, a function's after what it returns.
    std::string builtinNameOf(std::string_view line)
    {
        std::vector<std::string_view> words;
        size_t                        at = 0;
        while (words.size() < 3 && at < line.size())
        {
            const size_t begin = line.find_first_not_of(" \t(),=\r\n", at);
            if (begin == std::string_view::npos)
            {
                break;
            }
            const size_t end = line.find_first_of(" \t(),=\r\n", begin);
            words.push_back(line.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
            at = end == std::string_view::npos ? line.size() : end;
        }
        if (words.size() < 2 || line.compare(0, 2, "//") == 0)
        {
            return std::string();
        }
        return std::string(words[0] == "const" ? (words.size() > 2 ? words[2] : std::string_view()) : words[1]);
    }
}

bool ALLSLService::loadBuiltins(const std::string& path, std::string& error)
{
    AL_SCRIPT_ENGINE_HELD;
    // Loaded before -- another region's: Tailslide's table is the
    // process's and is only ever added to, and loading a file again would
    // put every name in it twice. So what this one declares that the table
    // lacks is loaded alone, and what both declare stays as first loaded.
    if (sBuiltinsLoaded)
    {
        llifstream in(path, std::ios::in | std::ios::binary);
        if (!in.is_open())
        {
            error = "cannot open " + path;
            return false;
        }
        Tailslide::ScopedScriptParser probe(nullptr);
        Tailslide::LSLSymbolTable*    table = probe.context.builtins;
        std::string                   added;
        std::set<std::string>         names;
        for (std::string line; std::getline(in, line);)
        {
            const std::string name = builtinNameOf(line);
            if (!name.empty() && table && !table->lookup(name.c_str(), Tailslide::SYM_ANY) && names.insert(name).second)
            {
                added += line;
                added += '\n';
            }
        }
        error.clear();
        if (added.empty())
        {
            return true;
        }
        const std::string more = path + ".added";
        {
            llofstream out(more, std::ios::out | std::ios::binary | std::ios::trunc);
            out << added;
        }
        Tailslide::tailslide_init_builtins(more.c_str());
        LLFile::remove(more);
        // An event new here numbered as the runtime numbers it, or after
        // every event there already was: those numbered before keep theirs.
        int last = 0;
        std::vector<Tailslide::LSLSymbol*> fresh;
        for (auto& [name, symbol] : table->getMap())
        {
            if (symbol->getSymbolType() != Tailslide::SYM_EVENT)
            {
                continue;
            }
            if (names.count(symbol->getName()))
            {
                fresh.push_back(symbol);
            }
            else
            {
                last = std::max(last, symbol->getEventIndex());
            }
        }
        for (Tailslide::LSLSymbol* symbol : fresh)
        {
            const int index = Luau::lslEventIndex(symbol->getName());
            symbol->setEventIndex(index > 0 ? index : ++last);
        }
        mImpl->builtins = true;
        return true;
    }
    // A file that is not there has nothing loaded, which is said here: the
    // file is opened first. A line Tailslide cannot read -- a type it does
    // not know, a constant it cannot parse, blanks -- it says so on stderr
    // and skips, since the tailslide port's builtins-skip-unreadable.patch;
    // before that it ended the process, over a file the grid sends.
    LLFILE* file = LLFile::fopen(path, LLFILE_MODE("rb"));
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    fclose(file);
    Tailslide::tailslide_init_builtins(path.c_str());
    // Every event numbered as the runtime numbers it (Luau::lslEventIndex):
    // the grid's definitions list them in an order of their own, and what an
    // event's number goes into -- LSO's handled-events bits, the dispatch of
    // LSL on Luau, whose compiler refuses a script whose numbers disagree
    // with the runtime's -- is the runtime's. One the runtime does not know
    // goes after all it does, in the file's order.
    Tailslide::ScopedScriptParser probe(nullptr);
    if (Tailslide::LSLSymbolTable* table = probe.context.builtins)
    {
        std::vector<Tailslide::LSLSymbol*> unknown;
        int                                last = 0;
        for (auto& [name, symbol] : table->getMap())
        {
            if (symbol->getSymbolType() != Tailslide::SYM_EVENT)
            {
                continue;
            }
            if (const int index = Luau::lslEventIndex(symbol->getName()); index > 0)
            {
                symbol->setEventIndex(index);
                last = std::max(last, index);
            }
            else
            {
                unknown.push_back(symbol);
            }
        }
        std::stable_sort(unknown.begin(), unknown.end(), [](Tailslide::LSLSymbol* a, Tailslide::LSLSymbol* b) { return a->getEventIndex() < b->getEventIndex(); });
        for (Tailslide::LSLSymbol* symbol : unknown)
        {
            symbol->setEventIndex(++last);
        }
    }
    mImpl->builtins = true;
    sBuiltinsLoaded = true;
    error.clear();
    return true;
}

bool ALLSLService::parsed() const
{
    return mImpl->parsed;
}

bool ALLSLService::understood() const
{
    return mImpl->understood;
}

size_t ALLSLService::mendings() const
{
    return mImpl->mendings;
}

void ALLSLService::setPassedOver(std::vector<std::pair<S32, S32>> lines)
{
    mImpl->passedOver = std::move(lines);
}

bool ALLSLService::hasBuiltins() const
{
    return mImpl->builtins;
}

// static
bool ALLSLService::builtinsLoaded()
{
    return sBuiltinsLoaded;
}

ALScriptProblems ALLSLService::check(std::string_view source, bool mono)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    // The passes in the order Tailslide's own tool runs them. The tree is
    // checked even after errors: the messages are the point here, and the
    // optimizer, which is what a broken tree would upset, is not run.
    // The tree the queries will want, checked in place: the later
    // passes change it, but only in ways the queries read -- the
    // symbols, the types and the reference data they themselves ask
    // for -- so the four questions of one request share one parse.
    // A tree already checked is parsed afresh: the passes have spoken
    // into its log once, and would say it all again.
    if (mImpl->passesRan)
    {
        mImpl->forget();
    }
    Tailslide::LSLScript* script = mImpl->resolve(source);
    if (script)
    {
        script->recalculateReferenceData();
        script->propagateValues();
        script->finalPass();
        script->validateGlobals(mono);
        script->checkSymbols();
        mImpl->passesRan = true;
    }
    Tailslide::ScopedScriptParser& parser = *mImpl->parser;

    ALScriptProblems problems;
    // The text lexed once for every syntax error, where there is one.
    std::optional<Lexed> lexed;
    for (Tailslide::LogMessage* message : parser.logger.getMessages())
    {
        ALScriptProblem problem;
        switch (message->getType())
        {
            case Tailslide::LOG_ERROR:
            case Tailslide::LOG_INTERNAL_ERROR:
                problem.severity = ALScriptProblem::Severity::Error;
                break;
            case Tailslide::LOG_WARN:
                problem.severity = ALScriptProblem::Severity::Warning;
                break;
            default:
                // What it is up to, which is not a problem.
                continue;
        }
        const Tailslide::ErrorCode code = message->getError();
        if (fromParser(code))
        {
            problem.source = ALScriptProblem::Source::Parser;
        }
        else if (problem.severity == ALScriptProblem::Severity::Warning)
        {
            problem.source = ALScriptProblem::Source::Lint;
        }
        else
        {
            problem.source = ALScriptProblem::Source::Types;
        }
        const auto* where  = message->getLoc();
        problem.line       = zeroBased(where->first_line);
        problem.column     = zeroBased(where->first_column);
        problem.endLine    = zeroBased(where->last_line);
        problem.endColumn  = zeroBased(where->last_column);
        problem.code       = std::to_string(static_cast<int>(code));
        problem.message    = message->getMessage();
        // Taken apart by its code, where the message is one the map
        // knows, so that the studio may say it in another language.
        ALMessageMap::Match known;
        if (code == Tailslide::E_SYNTAX_ERROR)
        {
            plainSyntaxError(source, problem, lexed, parser.context.builtins);
        }
        else if (ALMessageMap::lsl(static_cast<int>(code), problem.message, known))
        {
            problem.key  = std::move(known.key);
            problem.args = std::move(known.args);
        }
        problems.push_back(std::move(problem));
    }
    // A name it does not know changed to the nearest it does, where one is
    // near: Tailslide suggested one itself once, and no longer does.
    // The text's lines found once, for every fix offered over it; and a
    // problem wholly in lines nobody reads offered none.
    const ALScriptFixes::Lines lines(source);
    const auto                 passedOver = [this](const ALScriptProblem& problem) {
        return ALSourceMap::within(mImpl->passedOver, problem.line, std::max(problem.line, problem.endLine));
    };
    if (script)
    {
        for (ALScriptProblem& problem : problems)
        {
            if (problem.key == "LSLUndeclared" && problem.args.size() == 1 && !passedOver(problem))
            {
                ALScriptFixes::offerNames(problem, lines, problem.args[0], namesAt(script, problem.line, problem.column),
                                          builtinNames(parser.context.builtins));
            }
        }
    }
    // A declaration nothing uses taken out: a local only where what it is
    // given changes nothing, the rest -- a global's constant, a function,
    // a label, a state -- whole.
    if (script)
    {
        for (ALScriptProblem& problem : problems)
        {
            if (problem.key != "LSLDeclaredButNotUsed" || problem.args.size() != 2 || passedOver(problem))
            {
                continue;
            }
            Tailslide::LSLASTNode* declaration = declarationAt(script, problem.line, problem.column);
            if (!declaration)
            {
                continue;
            }
            if (declaration->getNodeSubType() == Tailslide::NODE_DECLARATION)
            {
                Tailslide::LSLASTNode* given = declaration->getChild(1);
                if (given && given->getNodeType() != Tailslide::NODE_NULL && !ALLSLTraits::changesNothing(given))
                {
                    continue;
                }
            }
            const Tailslide::YYLTYPE* loc = declaration->getLoc();
            ALScriptFixes::offerRemoval(problem, lines, zeroBased(loc->first_line), zeroBased(loc->first_column), zeroBased(loc->last_line),
                                        zeroBased(loc->last_column), problem.args[1]);
        }
    }
    // An event's parameters as it takes them, which the builtins know.
    for (ALScriptProblem& problem : problems)
    {
        if (!passedOver(problem))
        {
            offerEventParameters(problem, lines, parser.context.builtins);
        }
    }
    // What would put each right, where its words and its place say.
    ALScriptFixes::attach(problems, lines, false, mImpl->passedOver);
    return problems;
}

// --- what is in scope here -----------------------------------------------------

std::vector<ALScriptCompletion> ALLSLService::symbols(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source, line, column);
    std::vector<ALScriptCompletion> out;
    if (!script)
    {
        return out;
    }
    const int one_line   = line + 1;
    const int one_column = column + 1;
    std::vector<Tailslide::LSLASTNode*> path;
    holding(script, one_line, one_column, path);
    for (Tailslide::LSLASTNode* node : path)
    {
        Tailslide::LSLSymbolTable* table = node->getSymbolTable();
        if (!table)
        {
            continue;
        }
        const bool lexical = table->getTableType() == Tailslide::SYMTAB_LEXICAL;
        for (auto& [name, symbol] : table->getMap())
        {
            if (symbol->getSubType() == Tailslide::SYM_BUILTIN)
            {
                continue;
            }
            // A local is in scope from where it was declared.
            if (lexical && !startsBefore(*symbol->getLoc(), one_line, one_column))
            {
                continue;
            }
            ALScriptCompletion completion;
            completion.text   = symbol->getName();
            completion.kind   = kindOf(symbol);
            completion.detail = declarationOf(symbol);
            out.push_back(std::move(completion));
        }
    }
    std::sort(out.begin(), out.end(), [](const ALScriptCompletion& a, const ALScriptCompletion& b) { return a.text < b.text; });
    return out;
}

// --- what is here ------------------------------------------------------------------

ALScriptHover ALLSLService::hover(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source);
    ALScriptHover                 answer;
    if (!script)
    {
        return answer;
    }
    std::vector<Tailslide::LSLASTNode*> path;
    holding(script, line + 1, column + 1, path);
    // The innermost identifier whose name the position is on, resolved.
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        if ((*it)->getNodeType() != Tailslide::NODE_IDENTIFIER || !onName(static_cast<Tailslide::LSLIdentifier*>(*it), line + 1, column + 1))
        {
            continue;
        }
        Tailslide::LSLSymbol* symbol = (*it)->getSymbol();
        if (!symbol)
        {
            continue;
        }
        answer.found = true;
        answer.label = declarationOf(symbol);
        if (ALScriptSpan declared; declarationOf(script, symbol, declared))
        {
            answer.hasDefinition    = true;
            answer.definitionLine   = declared.line;
            answer.definitionColumn = declared.column;
        }
        break;
    }
    return answer;
}

// --- what a call here takes --------------------------------------------------------

ALScriptSignature ALLSLService::signature(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source, line, column);
    ALScriptSignature             answer;
    if (!script)
    {
        return answer;
    }
    const int one_line   = line + 1;
    const int one_column = column + 1;
    std::vector<Tailslide::LSLASTNode*> path;
    holding(script, one_line, one_column, path);
    // The innermost call the position is inside, past its name.
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        if ((*it)->getNodeSubType() != Tailslide::NODE_FUNCTION_EXPRESSION)
        {
            continue;
        }
        auto*                     call       = static_cast<Tailslide::LSLFunctionExpression*>(*it);
        Tailslide::LSLSymbol*     symbol     = call->getSymbol();
        Tailslide::LSLIdentifier* identifier = call->getIdentifier();
        if (!symbol || symbol->getSymbolType() != Tailslide::SYM_FUNCTION || !identifier || !pastName(identifier, one_line, one_column))
        {
            continue;
        }
        answer.found = true;
        answer.label = declarationOf(symbol, &answer.parameters);
        S32 active   = 0;
        if (Tailslide::LSLASTNode* arguments = call->getArguments())
        {
            S32 index = 0;
            for (Tailslide::LSLASTNode* argument = arguments->getChild(0); argument; argument = argument->getNext(), ++index)
            {
                const Tailslide::YYLTYPE& where = *argument->getLoc();
                if (holds(where, one_line, one_column))
                {
                    active = index;
                    break;
                }
                if (endsBefore(where, one_line, one_column))
                {
                    active = index + 1;
                }
            }
        }
        answer.active = active;
        break;
    }
    return answer;
}

// --- where a symbol lives -----------------------------------------------------------

ALScriptReferences ALLSLService::references(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source);
    ALScriptReferences            answer;
    if (!script)
    {
        return answer;
    }
    Tailslide::LSLSymbol* symbol = symbolAt(script, line + 1, column + 1);
    if (!symbol)
    {
        return answer;
    }
    answer.found = true;
    answer.name  = symbol->getName();
    answer.kind  = kindOf(symbol);
    if (declarationOf(script, symbol, answer.definition, &answer.references))
    {
        answer.hasDefinition = true;
        // An event's name is the language's, and so is the default state's.
        answer.renamable = symbol->getSymbolType() != Tailslide::SYM_EVENT && answer.name != "default";
    }
    else
    {
        Uses uses(symbol);
        script->visit(&uses);
        std::sort(uses.spans.begin(), uses.spans.end());
        uses.spans.erase(std::unique(uses.spans.begin(), uses.spans.end()), uses.spans.end());
        answer.references = std::move(uses.spans);
    }
    return answer;
}

// --- what the script declares ------------------------------------------------------

std::vector<ALScriptOutlineEntry> ALLSLService::outline(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source);
    std::vector<ALScriptOutlineEntry> out;
    if (!script)
    {
        return out;
    }
    auto entry = [&](Tailslide::LSLASTNode* node, ALScriptSymbolKind kind, S32 depth, bool detailed) {
        Tailslide::LSLIdentifier* identifier = identifierOf(node);
        if (!identifier || identifier->getLoc()->first_line == 0)
        {
            return;
        }
        ALScriptOutlineEntry one;
        one.name     = identifier->getName();
        one.kind     = kind;
        one.depth    = depth;
        one.nameSpan = nameSpanOf(*identifier->getLoc(), identifier->getName());
        one.span     = node->getLoc()->first_line > 0 ? spanOf(*node->getLoc()) : one.nameSpan;
        if (detailed && identifier->getSymbol())
        {
            one.detail = declarationOf(identifier->getSymbol());
        }
        out.push_back(std::move(one));
    };
    if (Tailslide::LSLASTNode* globals = script->getGlobals())
    {
        for (Tailslide::LSLASTNode* global = globals->getChild(0); global; global = global->getNext())
        {
            if (global->getNodeType() == Tailslide::NODE_GLOBAL_VARIABLE)
            {
                entry(global, ALScriptSymbolKind::Variable, 0, true);
            }
            else if (global->getNodeType() == Tailslide::NODE_GLOBAL_FUNCTION)
            {
                entry(global, ALScriptSymbolKind::Function, 0, true);
            }
        }
    }
    if (Tailslide::LSLASTNode* states = script->getStates())
    {
        for (Tailslide::LSLASTNode* state = states->getChild(0); state; state = state->getNext())
        {
            if (state->getNodeType() != Tailslide::NODE_STATE)
            {
                continue;
            }
            entry(state, ALScriptSymbolKind::State, 0, false);
            Tailslide::LSLASTNode* handlers = static_cast<Tailslide::LSLState*>(state)->getEventHandlers();
            for (Tailslide::LSLASTNode* handler = handlers ? handlers->getChild(0) : nullptr; handler; handler = handler->getNext())
            {
                if (handler->getNodeType() == Tailslide::NODE_EVENT_HANDLER)
                {
                    entry(handler, ALScriptSymbolKind::Event, 1, true);
                }
            }
        }
    }
    return out;
}

// --- what every name is ------------------------------------------------------------

std::vector<ALScriptSemanticToken> ALLSLService::semanticTokens(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source);
    if (!script)
    {
        return {};
    }
    Semantics semantics;
    semantics.passedOver = &mImpl->passedOver;
    script->visit(&semantics);
    std::vector<ALScriptSemanticToken>& out = semantics.out;
    std::stable_sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](const ALScriptSemanticToken& a, const ALScriptSemanticToken& b) { return a.span == b.span; }),
              out.end());
    return std::move(out);
}

// --- what goes beside the text ----------------------------------------------------

std::vector<ALScriptInlayHint> ALLSLService::inlayHints(std::string_view source, bool parameters)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    if (!parameters)
    {
        return {};
    }
    Tailslide::LSLScript* script = mImpl->understand(source);
    if (!script)
    {
        return {};
    }
    Hints hints;
    hints.passedOver = &mImpl->passedOver;
    script->visit(&hints);
    std::stable_sort(hints.out.begin(), hints.out.end());
    return std::move(hints.out);
}

// --- what could be done here ----------------------------------------------------------

namespace
{
    // What a node spans in the text, or nothing where its place is not the
    // text's.
    std::string_view textOf(std::string_view source, Tailslide::LSLASTNode* node)
    {
        if (!node || node->getLoc()->first_line == 0)
        {
            return {};
        }
        const ALScriptSpan span = spanOf(*node->getLoc());
        const size_t       from = offsetOf(source, span.line, span.column);
        const size_t       to   = offsetOf(source, span.endLine, span.endColumn);
        return to > from ? source.substr(from, to - from) : std::string_view();
    }

    bool isStatement(Tailslide::LSLASTNode* node, Tailslide::LSLNodeSubType sub)
    {
        return node && node->getNodeType() == Tailslide::NODE_STATEMENT && node->getNodeSubType() == sub;
    }

    bool isExpression(Tailslide::LSLASTNode* node, Tailslide::LSLNodeSubType sub)
    {
        return node && node->getNodeType() == Tailslide::NODE_EXPRESSION && node->getNodeSubType() == sub;
    }

    bool present(Tailslide::LSLASTNode* node)
    {
        return node && node->getNodeType() != Tailslide::NODE_NULL;
    }

    // Whether a statement ends in an `if` with no `else` of its own --
    // itself, or the last of an else-if chain, a loop's body -- which an
    // `else` written after it would be taken by.
    bool endsInBareIf(Tailslide::LSLASTNode* statement)
    {
        Tailslide::LSLASTNode* s = statement;
        while (s && s->getNodeType() == Tailslide::NODE_STATEMENT)
        {
            switch (s->getNodeSubType())
            {
                case Tailslide::NODE_IF_STATEMENT:
                    if (!present(s->getChild(2)))
                    {
                        return true;
                    }
                    s = s->getChild(2);
                    break;
                case Tailslide::NODE_WHILE_STATEMENT:
                    s = s->getChild(1);
                    break;
                case Tailslide::NODE_FOR_STATEMENT:
                    s = s->getChild(3);
                    break;
                default:
                    return false;
            }
        }
        return false;
    }

    ALScriptFix refactor(ALScriptFix fix)
    {
        fix.kind = ALScriptFix::Kind::Refactor;
        return fix;
    }

    void replace(ALScriptFix& fix, const ALScriptSpan& span, std::string text)
    {
        fix.edits.push_back({ span.line, span.column, span.endLine, span.endColumn, std::move(text) });
    }

    // A type as a declaration writes it: `rotation`, where Tailslide says
    // `quaternion`, which LSL takes but nobody writes.
    std::string declared(Tailslide::LSLType* type)
    {
        return type && type->getIType() == Tailslide::LST_QUATERNION ? std::string("rotation") : typeName(type);
    }

    // Whether an operation writes to its operand.
    bool assigns(Tailslide::LSLOperator op)
    {
        switch (op)
        {
            case Tailslide::OP_ASSIGN:
            case Tailslide::OP_ADD_ASSIGN:
            case Tailslide::OP_SUB_ASSIGN:
            case Tailslide::OP_MUL_ASSIGN:
            case Tailslide::OP_DIV_ASSIGN:
            case Tailslide::OP_MOD_ASSIGN:
            case Tailslide::OP_PRE_INCR:
            case Tailslide::OP_PRE_DECR:
            case Tailslide::OP_POST_INCR:
            case Tailslide::OP_POST_DECR:
                return true;
            default:
                return false;
        }
    }

    // A condition the other way round: `!x` as `x`, `a == b` as `a != b`,
    // anything else under a `!`. An order is not turned round, `a < b` as
    // `a >= b`: a float may be NaN, which is neither.
    std::string negated(std::string_view source, Tailslide::LSLASTNode* condition)
    {
        const std::string text(textOf(source, condition));
        if (isExpression(condition, Tailslide::NODE_UNARY_EXPRESSION) &&
            static_cast<Tailslide::LSLExpression*>(condition)->getOperation() == Tailslide::OP_BOOLEAN_NOT)
        {
            Tailslide::LSLASTNode* inner = condition->getChild(0);
            if (isExpression(inner, Tailslide::NODE_PARENTHESIS_EXPRESSION))
            {
                inner = inner->getChild(0);
            }
            return std::string(textOf(source, inner));
        }
        if (isExpression(condition, Tailslide::NODE_BINARY_EXPRESSION))
        {
            const Tailslide::LSLOperator op = static_cast<Tailslide::LSLExpression*>(condition)->getOperation();
            if (op == Tailslide::OP_EQ || op == Tailslide::OP_NEQ)
            {
                const ALScriptSpan lhs = spanOf(*condition->getChild(0)->getLoc());
                const ALScriptSpan rhs = spanOf(*condition->getChild(1)->getLoc());
                const size_t       from = offsetOf(source, lhs.endLine, lhs.endColumn);
                const size_t       to   = offsetOf(source, rhs.line, rhs.column);
                std::string        between(to > from ? source.substr(from, to - from) : std::string_view());
                const size_t       at = between.find(op == Tailslide::OP_EQ ? "==" : "!=");
                if (at != std::string::npos)
                {
                    between.replace(at, 2, op == Tailslide::OP_EQ ? "!=" : "==");
                    return std::string(textOf(source, condition->getChild(0))) + between + std::string(textOf(source, condition->getChild(1)));
                }
            }
        }
        const bool simple = isExpression(condition, Tailslide::NODE_LVALUE_EXPRESSION) || isExpression(condition, Tailslide::NODE_FUNCTION_EXPRESSION) ||
                            isExpression(condition, Tailslide::NODE_PARENTHESIS_EXPRESSION);
        return simple ? "!" + text : "!(" + text + ")";
    }

    // The functions called in a stretch of the tree, by name.
    void callsIn(Tailslide::LSLASTNode* node, std::set<std::string>& calls)
    {
        for (; node; node = node->getNext())
        {
            if (isExpression(node, Tailslide::NODE_FUNCTION_EXPRESSION) && node->getChild(0) && node->getChild(0)->getNodeType() == Tailslide::NODE_IDENTIFIER)
            {
                calls.insert(static_cast<Tailslide::LSLIdentifier*>(node->getChild(0))->getName());
            }
            callsIn(node->getChild(0), calls);
        }
    }

    // Whether a stretch of text is one string literal as written, quote to
    // quote with nothing but what is escaped between.
    bool stringLiteral(std::string_view text)
    {
        if (text.size() < 2 || text.front() != '"' || text.back() != '"')
        {
            return false;
        }
        for (size_t i = 1; i + 1 < text.size(); ++i)
        {
            if (text[i] == '\\')
            {
                ++i;
            }
            else if (text[i] == '"')
            {
                return false;
            }
        }
        return true;
    }

    // Whether a node is in a function's or a handler's body, rather than a
    // global's value.
    bool inBody(Tailslide::LSLASTNode* node)
    {
        for (; node; node = node->getParent())
        {
            const Tailslide::LSLNodeType type = node->getNodeType();
            if (type == Tailslide::NODE_GLOBAL_VARIABLE)
            {
                return false;
            }
            if (type == Tailslide::NODE_GLOBAL_FUNCTION || type == Tailslide::NODE_EVENT_HANDLER)
            {
                return true;
            }
        }
        return false;
    }

    // Every node under one, in the order written.
    template <typename Each> void eachNode(Tailslide::LSLASTNode* node, Each&& each)
    {
        for (; node; node = node->getNext())
        {
            each(node);
            eachNode(node->getChild(0), each);
        }
    }

    // A global's name for a string: `g` and the first few words of it,
    // each begun in capitals -- "touched by" as gTouchedBy -- or gText where
    // it has none.
    std::string globalNameFor(std::string_view literal)
    {
        std::string name  = "g";
        S32         words = 0;
        bool        start = true;
        for (size_t i = 1; i + 1 < literal.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(literal[i]);
            if (c == '\\')
            {
                // An escape, `\n` and the like, is no letter of a word.
                ++i;
                start = true;
                continue;
            }
            if (c >= 0x80 || !isalnum(c))
            {
                start = true;
                continue;
            }
            if (start)
            {
                if (words == 3)
                {
                    break;
                }
                ++words;
                start = false;
                name += static_cast<char>(toupper(c));
            }
            else if (name.size() < 24)
            {
                name += static_cast<char>(c);
            }
        }
        return name.size() > 1 ? name : std::string("gText");
    }

    // Whether an expression may stand as it is on either side of a `+`, or
    // after a cast, and mean what it meant alone: a name, a call, a
    // literal not begun by a sign, what is in brackets already.
    bool bareOperand(std::string_view source, Tailslide::LSLASTNode* node)
    {
        if (isExpression(node, Tailslide::NODE_LVALUE_EXPRESSION) || isExpression(node, Tailslide::NODE_FUNCTION_EXPRESSION) ||
            isExpression(node, Tailslide::NODE_PARENTHESIS_EXPRESSION) || isExpression(node, Tailslide::NODE_VECTOR_EXPRESSION) ||
            isExpression(node, Tailslide::NODE_QUATERNION_EXPRESSION))
        {
            return true;
        }
        // A constant written as one word or number, or one string: not what
        // the tree may have folded from more, `2 + 3`, nor a sign before it.
        const std::string_view text = textOf(source, node);
        if (!isExpression(node, Tailslide::NODE_CONSTANT_EXPRESSION) || text.empty())
        {
            return false;
        }
        return stringLiteral(text) ||
               std::all_of(text.begin(), text.end(), [](char c) { return ALScriptLexicon::isNameByte(c) || c == '.'; });
    }

    // Whether an expression is the whole of what it is given to -- a
    // declaration's value, what is assigned or returned, an argument -- so
    // that what is written in its place needs no brackets of its own.
    bool standsWhole(Tailslide::LSLASTNode* node)
    {
        Tailslide::LSLASTNode* parent = node->getParent();
        if (!parent)
        {
            return false;
        }
        if (isStatement(parent, Tailslide::NODE_DECLARATION) || isStatement(parent, Tailslide::NODE_RETURN_STATEMENT) ||
            isStatement(parent, Tailslide::NODE_EXPRESSION_STATEMENT))
        {
            return true;
        }
        if (isExpression(parent, Tailslide::NODE_BINARY_EXPRESSION) && static_cast<Tailslide::LSLExpression*>(parent)->getOperation() == Tailslide::OP_ASSIGN &&
            parent->getChild(1) == node)
        {
            return true;
        }
        return parent->getNodeType() == Tailslide::NODE_AST_NODE_LIST && isExpression(parent->getParent(), Tailslide::NODE_FUNCTION_EXPRESSION);
    }

    // The leading blanks of the line a zero-based place is on.
    std::string indentOf(std::string_view source, S32 line)
    {
        const size_t     start = offsetOf(source, line, 0);
        const size_t     end   = std::min(source.find('\n', start), source.size());
        std::string_view text  = source.substr(start, end - start);
        return std::string(text.substr(0, std::min(text.find_first_not_of(" \t"), text.size())));
    }
}

std::vector<ALScriptFix> ALLSLService::actions(std::string_view source, S32 line, S32 column, S32 endLine, S32 endColumn)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    AL_SCRIPT_ENGINE_HELD;
    std::vector<ALScriptFix> out;
    Tailslide::LSLScript*    script = mImpl->resolve(source);
    if (!script || !mImpl->parsed)
    {
        return out;
    }

    // A stretch chosen that is one expression, into a local declared just
    // before the statement it is in, named as nothing in the script is.
    // Only where that runs it as often as it ran: not out of a loop's
    // condition, nor a statement an `if` or a loop holds without braces.
    if (endLine == line && endColumn > column)
    {
        S32 from = column, to = endColumn;
        const size_t           start = offsetOf(source, line, 0);
        const std::string_view text  = source.substr(start, std::min(offsetOf(source, line, endColumn), source.size()) - start);
        to                           = std::min<S32>(to, static_cast<S32>(text.size()));
        while (from < to && isspace(static_cast<unsigned char>(text[from])))
        {
            ++from;
        }
        while (to > from && isspace(static_cast<unsigned char>(text[to - 1])))
        {
            --to;
        }
        std::vector<Tailslide::LSLASTNode*> path;
        holding(script, line + 1, from + 1, path);
        Tailslide::LSLASTNode* chosen = nullptr;
        for (auto it = path.rbegin(); it != path.rend() && !chosen; ++it)
        {
            const ALScriptSpan span = spanOf(*(*it)->getLoc());
            if ((*it)->getNodeType() == Tailslide::NODE_EXPRESSION && span.line == line && span.column == from && span.endLine == line && span.endColumn == to)
            {
                chosen = *it;
            }
        }
        Tailslide::LSLASTNode* statement = chosen ? chosen->getParent() : nullptr;
        bool                   certain   = chosen != nullptr;
        if (chosen && chosen->getParent() && chosen->getParent()->getNodeType() == Tailslide::NODE_EXPRESSION &&
            assigns(static_cast<Tailslide::LSLExpression*>(chosen->getParent())->getOperation()) && chosen->getParent()->getChild(0) == chosen)
        {
            // What is assigned to, which a local would take in its place.
            certain = false;
        }
        if (chosen && isStatement(chosen->getParent(), Tailslide::NODE_EXPRESSION_STATEMENT))
        {
            // The whole statement, which would leave the local's name
            // standing alone as one.
            certain = false;
        }
        while (statement && statement->getNodeType() != Tailslide::NODE_STATEMENT)
        {
            statement = statement->getParent();
        }
        Tailslide::LSLType* type = chosen ? chosen->getType() : nullptr;
        if (certain && statement && isStatement(statement->getParent(), Tailslide::NODE_COMPOUND_STATEMENT) && type &&
            type->getIType() != Tailslide::LST_NULL && type->getIType() != Tailslide::LST_ERROR && !isStatement(statement, Tailslide::NODE_WHILE_STATEMENT) &&
            !isStatement(statement, Tailslide::NODE_DO_STATEMENT) && !isStatement(statement, Tailslide::NODE_FOR_STATEMENT))
        {
            const std::string  name  = ALScriptFixes::freshName(source, "value");
            const std::string  value(textOf(source, chosen));
            const ALScriptSpan where = spanOf(*statement->getLoc());
            const std::string  indent = indentOf(source, where.line);
            const bool         alone  = static_cast<S32>(indent.size()) == where.column;
            ALScriptFix        fix = refactor(ALScriptFixes::titled("ScriptActionExtract", "Put it in a local, '[1]'", { name }));
            // On a line of its own, where the statement starts its line;
            // else just before it, where another stands ahead of it.
            if (alone)
            {
                replace(fix, { where.line, 0, where.line, 0 }, indent + declared(type) + " " + name + " = " + value + ";\n");
            }
            else
            {
                replace(fix, { where.line, where.column, where.line, where.column }, declared(type) + " " + name + " = " + value + "; ");
            }
            replace(fix, { line, from, line, to }, name);
            out.push_back(std::move(fix));
        }
    }

    std::vector<Tailslide::LSLASTNode*> path;
    holding(script, line + 1, column + 1, path);

    // An `if` with an `else`, the caret on its first line: the condition
    // turned round, the branches swapped. Not where the `else` is another
    // `if`, which would come to stand as the first branch and take the
    // `else` for its own.
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        Tailslide::LSLASTNode* branch = *it;
        if (!isStatement(branch, Tailslide::NODE_IF_STATEMENT) || zeroBased(branch->getLoc()->first_line) != line)
        {
            continue;
        }
        Tailslide::LSLASTNode* condition = branch->getChild(0);
        Tailslide::LSLASTNode* then      = branch->getChild(1);
        Tailslide::LSLASTNode* otherwise = branch->getChild(2);
        while (isExpression(condition, Tailslide::NODE_BOOL_CONVERSION_EXPRESSION))
        {
            condition = condition->getChild(0);
        }
        if (present(condition) && present(then) && present(otherwise) && !isStatement(otherwise, Tailslide::NODE_IF_STATEMENT) &&
            !textOf(source, condition).empty() && !textOf(source, then).empty() && !textOf(source, otherwise).empty())
        {
            ALScriptFix fix = refactor(ALScriptFixes::titled("ScriptActionInvertIf", "Invert the if", {}));
            replace(fix, spanOf(*condition->getLoc()), negated(source, condition));
            // The else branch, first now, in braces where it ends in an if
            // of its own with no else, which would take the else that now
            // follows it.
            const std::string first(textOf(source, otherwise));
            replace(fix, spanOf(*then->getLoc()), endsInBareIf(otherwise) ? "{ " + first + " }" : first);
            replace(fix, spanOf(*otherwise->getLoc()), std::string(textOf(source, then)));
            out.push_back(std::move(fix));
        }
        break;
    }

    // A handler for each event the state the caret is in asks for and does
    // not hear, before the state's closing brace, with the parameters the
    // builtins give the event, laid out as the state's first handler is.
    Tailslide::LSLASTNode* state = nullptr;
    for (Tailslide::LSLASTNode* node : path)
    {
        if (node->getNodeType() == Tailslide::NODE_STATE)
        {
            state = node;
        }
    }
    Tailslide::LSLSymbolTable* builtins = mImpl->parser->context.builtins;
    if (state && builtins)
    {
        Tailslide::LSLASTNode* handlers = static_cast<Tailslide::LSLState*>(state)->getEventHandlers();
        std::set<std::string>  heard;
        std::set<std::string>  calls;
        Tailslide::LSLASTNode* first = nullptr;
        for (Tailslide::LSLASTNode* handler = handlers ? handlers->getChild(0) : nullptr; handler; handler = handler->getNext())
        {
            if (handler->getNodeType() != Tailslide::NODE_EVENT_HANDLER || !handler->getChild(0))
            {
                continue;
            }
            first = first ? first : handler;
            heard.insert(static_cast<Tailslide::LSLIdentifier*>(handler->getChild(0))->getName());
            callsIn(handler->getChild(2), calls);
        }
        // And what the script's own functions the handlers call ask for,
        // and the functions those call: an event asked for from a helper
        // is asked for all the same.
        std::map<std::string, Tailslide::LSLASTNode*> functions;
        if (Tailslide::LSLASTNode* globals = script->getGlobals())
        {
            for (Tailslide::LSLASTNode* global = globals->getChild(0); global; global = global->getNext())
            {
                if (global->getNodeType() == Tailslide::NODE_GLOBAL_FUNCTION && global->getChild(0) &&
                    global->getChild(0)->getNodeType() == Tailslide::NODE_IDENTIFIER)
                {
                    functions[static_cast<Tailslide::LSLIdentifier*>(global->getChild(0))->getName()] = global->getChild(2);
                }
            }
        }
        std::vector<std::string> waiting(calls.begin(), calls.end());
        std::set<std::string>    walked;
        while (!waiting.empty())
        {
            const std::string name = std::move(waiting.back());
            waiting.pop_back();
            const auto function = functions.find(name);
            if (function == functions.end() || !walked.insert(name).second)
            {
                continue;
            }
            std::set<std::string> more;
            callsIn(function->second, more);
            for (const std::string& call : more)
            {
                if (calls.insert(call).second)
                {
                    waiting.push_back(call);
                }
            }
        }
        const ALScriptSpan whole  = spanOf(*state->getLoc());
        const S32          close  = whole.endColumn - 1;
        const size_t       brace  = offsetOf(source, whole.endLine, close);
        std::string        indent = first ? indentOf(source, zeroBased(first->getLoc()->first_line)) : std::string();
        if (indent.empty())
        {
            indent = "    ";
        }
        // `name() {` where the first handler opens its body on its own line.
        bool same_line = false;
        if (first)
        {
            const std::string_view opened = textOf(source, first);
            same_line                     = opened.substr(0, opened.find('\n')).find('{') != std::string_view::npos;
        }
        std::set<std::string> offered;
        for (const std::string& call : calls)
        {
            const char* event = ALScriptFixes::eventAnswering(call);
            if (!event || heard.count(event) || !offered.insert(event).second || brace >= source.size() || source[brace] != '}')
            {
                continue;
            }
            Tailslide::LSLSymbol* symbol = builtins->lookup(event, Tailslide::SYM_EVENT);
            if (!symbol)
            {
                continue;
            }
            std::string parameters;
            if (Tailslide::LSLParamList* params = symbol->getFunctionDecl())
            {
                for (Tailslide::LSLASTNode* node = params->getChild(0); node; node = node->getNext())
                {
                    if (node->getNodeType() == Tailslide::NODE_IDENTIFIER)
                    {
                        auto* identifier = static_cast<Tailslide::LSLIdentifier*>(node);
                        parameters += (parameters.empty() ? "" : ", ") + declared(identifier->getType()) + " " + identifier->getName();
                    }
                }
            }
            const std::string handler = indent + event + "(" + parameters + ")" + (same_line ? " {\n" : "\n" + indent + "{\n") + indent + "}\n";
            ALScriptFix       fix     = refactor(ALScriptFixes::titled("ScriptActionHandler", "Add a handler for '[1]'", { event }));
            // After a blank line, where the brace stands on a line of its
            // own; else on lines of their own ahead of it.
            if (static_cast<S32>(indentOf(source, whole.endLine).size()) == close)
            {
                replace(fix, { whole.endLine, 0, whole.endLine, 0 }, (first ? "\n" : "") + handler);
            }
            else
            {
                replace(fix, { whole.endLine, close, whole.endLine, close }, "\n" + handler);
            }
            out.push_back(std::move(fix));
        }
    }

    // A string written the same several times over, put in a global of its
    // own and each place it stands given the global's name: one copy of it
    // in the script where a target keeps one at each use. Only the places
    // in a function or a handler -- a global's value may not be another
    // global -- and only where there are two of them or more. The global
    // after the globals the script opens with, else ahead of what it opens
    // with.
    Tailslide::LSLASTNode* literal = nullptr;
    for (auto it = path.rbegin(); it != path.rend() && !literal; ++it)
    {
        if (isExpression(*it, Tailslide::NODE_CONSTANT_EXPRESSION) && stringLiteral(textOf(source, *it)))
        {
            literal = *it;
        }
    }
    Tailslide::LSLASTNode* globals = script->getGlobals();
    if (literal && inBody(literal) && globals)
    {
        const std::string         written(textOf(source, literal));
        std::vector<ALScriptSpan> uses;
        eachNode(script->getChild(0), [&](Tailslide::LSLASTNode* node) {
            if (isExpression(node, Tailslide::NODE_CONSTANT_EXPRESSION) && inBody(node) && textOf(source, node) == written)
            {
                uses.push_back(spanOf(*node->getLoc()));
            }
        });
        Tailslide::LSLASTNode* last_global = nullptr;
        Tailslide::LSLASTNode* first_other = nullptr;
        for (Tailslide::LSLASTNode* global = globals->getChild(0); global && !first_other; global = global->getNext())
        {
            (global->getNodeType() == Tailslide::NODE_GLOBAL_VARIABLE ? last_global : first_other) = global;
        }
        if (!first_other && script->getStates())
        {
            first_other = script->getStates()->getChild(0);
        }
        if (uses.size() >= 2 && (last_global || first_other))
        {
            const std::string name = ALScriptFixes::freshName(source, globalNameFor(written));
            ALScriptFix       fix  = refactor(ALScriptFixes::titled("ScriptActionStringGlobal", "Put the string in a global, '[1]', for its [2] uses",
                                                                    { name, std::to_string(uses.size()) }));
            const std::string declaration = "string " + name + " = " + written + ";\n";
            if (last_global)
            {
                const S32 after = zeroBased(last_global->getLoc()->last_line) + 1;
                replace(fix, { after, 0, after, 0 }, declaration);
            }
            else
            {
                const S32 before = zeroBased(first_other->getLoc()->first_line);
                replace(fix, { before, 0, before, 0 }, declaration + "\n");
            }
            for (const ALScriptSpan& use : uses)
            {
                replace(fix, use, name);
            }
            out.push_back(std::move(fix));
        }
    }

    // A list written out, written as a sum: the first element cast, the
    // rest added to it, which Mono makes without boxing each. Each element
    // in brackets where it would not mean alone what it meant, and the sum
    // where it is part of something more. Not in a global's value, which
    // must be written out.
    Tailslide::LSLASTNode* list = nullptr;
    for (auto it = path.rbegin(); it != path.rend() && !list; ++it)
    {
        if (isExpression(*it, Tailslide::NODE_LIST_EXPRESSION))
        {
            list = *it;
        }
    }
    if (list && inBody(list) && present(list->getChild(0)))
    {
        std::string sum;
        bool        whole = true;
        for (Tailslide::LSLASTNode* element = list->getChild(0); element && whole; element = element->getNext())
        {
            const std::string text(textOf(source, element));
            whole = present(element) && !text.empty();
            const std::string operand = bareOperand(source, element) ? text : "(" + text + ")";
            sum += sum.empty() ? "(list)" + operand : " + " + operand;
        }
        if (whole)
        {
            ALScriptFix fix = refactor(ALScriptFixes::titled("ScriptActionListSum", "Write the list as a sum", {}));
            replace(fix, spanOf(*list->getLoc()), standsWhole(list) ? sum : "(" + sum + ")");
            out.push_back(std::move(fix));
        }
    }
    return out;
}

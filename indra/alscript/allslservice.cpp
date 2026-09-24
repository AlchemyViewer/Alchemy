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

#include "alscriptengine.h"

#include "almessagemap.h"

#include "llfile.h"

#include <tailslide/tailslide.hh>

#include <atomic>
#include <tailslide/visitor.hh>

#include <algorithm>
#include <cstring>

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

    // Everything a node spans. Tailslide's last column is the last
    // character's, counted from one, which is the column after it
    // counted from zero.
    ALScriptSpan spanOf(const Tailslide::YYLTYPE& where)
    {
        ALScriptSpan span;
        span.line      = zeroBased(where.first_line);
        span.column    = zeroBased(where.first_column);
        span.endLine   = zeroBased(where.last_line);
        span.endColumn = std::max(0, where.last_column);
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
    struct Semantics final : public Tailslide::ASTVisitor
    {
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
    struct Hints final : public Tailslide::ASTVisitor
    {
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
        return isalnum(static_cast<unsigned char>(c)) || c == '_';
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

    // Bison's "syntax error, unexpected X, expecting Y or Z" said as a
    // scripter would: a single bracket or semicolon it wanted is missing,
    // and is marked on the last thing before the place it stopped --
    // where it is missing, rather than on the next line's brace -- and
    // anything else is unexpected, with what was wanted where it says.
    // Keyed, as the map's messages are, for the studio to translate.
    void plainSyntaxError(std::string_view source, ALScriptProblem& problem)
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
        const Lexed  lexed   = lex(source);
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
        problem.message = ALScriptProblem::fill(wanted.empty() ? "Unexpected [1]." : "Unexpected [1]; expected [2].", problem.args);
        problem.args.resize(wanted.empty() ? 1 : 2);
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
        mendedParser.reset();
        mendedFor.clear();
        mended = nullptr;
    }

    // A script being typed seldom parses -- a call not yet closed, a
    // statement not yet ended -- and Tailslide answers nothing of a text
    // that does not: no scope, no call, no names. So the questions are
    // asked of a copy mended to parse: the statement at the caret, where
    // there is one, closed where the caret is; then each statement the
    // parser stops in blanked, and the blocks left open at the end
    // closed. Every position before the caret, and every line after its
    // own, is where it was, so the answers need no translating back.
    std::unique_ptr<Tailslide::ScopedScriptParser> mendedParser;
    std::string                                    mendedFor;
    S32                                            mendedLine   = -1;
    S32                                            mendedColumn = -1;
    Tailslide::LSLScript*                          mended       = nullptr;
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
        if (mendedParser && mendedFor == source && mendedLine == line && mendedColumn == column)
        {
            understood = mended != nullptr;
            return mended;
        }
        mendedFor.assign(source);
        mendedLine   = line;
        mendedColumn = column;
        mended       = nullptr;
        std::string copy = line >= 0 ? closedAt(source, offsetOf(source, line, column)) : std::string(source);
        // Each try blanks a statement or closes the end; a handful mends
        // what one pause in typing leaves, and a text broken in more
        // places than that is left as it is.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            mendedParser = std::make_unique<Tailslide::ScopedScriptParser>(nullptr);
            mended       = mendedParser->parseLSLBytes(copy.data(), static_cast<int>(copy.size()));
            if (mended)
            {
                mended->collectSymbols();
                mended->determineTypes();
                break;
            }
            const size_t at = stoppedAt(*mendedParser, copy);
            if (at == std::string_view::npos || !mendAt(copy, at))
            {
                break;
            }
        }
        understood = mended != nullptr;
        return mended;
    }
};

ALLSLService::ALLSLService()
:   mImpl(std::make_unique<Impl>())
{
}

ALLSLService::~ALLSLService() = default;

bool ALLSLService::loadBuiltins(const std::string& path, std::string& error)
{
    AL_SCRIPT_ENGINE_HELD;
    // Tailslide exits the process over a file it cannot open, so the file
    // is opened here first. A line it cannot read it reports on stderr and
    // skips, which the process survives.
    LLFILE* file = LLFile::fopen(path, LLFILE_MODE("rb"));
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    fclose(file);
    Tailslide::tailslide_init_builtins(path.c_str());
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
            plainSyntaxError(source, problem);
        }
        else if (ALMessageMap::lsl(static_cast<int>(code), problem.message, known))
        {
            problem.key  = std::move(known.key);
            problem.args = std::move(known.args);
        }
        problems.push_back(std::move(problem));
    }
    return problems;
}

// --- what is in scope here -----------------------------------------------------

std::vector<ALScriptCompletion> ALLSLService::symbols(std::string_view source, S32 line, S32 column)
{
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
    AL_SCRIPT_ENGINE_HELD;
    Tailslide::LSLScript* script = mImpl->understand(source);
    if (!script)
    {
        return {};
    }
    Semantics semantics;
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
    script->visit(&hints);
    std::stable_sort(hints.out.begin(), hints.out.end());
    return std::move(hints.out);
}

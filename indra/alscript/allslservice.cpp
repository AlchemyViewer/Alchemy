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
    Tailslide::LSLScript* resolved(Tailslide::ScopedScriptParser& parser, std::string_view source)
    {
        Tailslide::LSLScript* script = parser.parseLSLBytes(source.data(), static_cast<int>(source.size()));
        if (script)
        {
            script->collectSymbols();
            script->determineTypes();
        }
        return script;
    }

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
}

struct ALLSLService::Impl
{
    bool builtins = false;
};

ALLSLService::ALLSLService()
:   mImpl(std::make_unique<Impl>())
{
}

ALLSLService::~ALLSLService() = default;

bool ALLSLService::loadBuiltins(const std::string& path, std::string& error)
{
    // Tailslide exits the process over a file it cannot open, so the file
    // is opened here first. A line it cannot read it reports on stderr and
    // skips, which the process survives.
    LLFILE* file = LLFile::fopen(path, "rb");
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
    // The passes in the order Tailslide's own tool runs them. The tree is
    // checked even after errors: the messages are the point here, and the
    // optimizer, which is what a broken tree would upset, is not run.
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript* script = parser.parseLSLBytes(source.data(), static_cast<int>(source.size()));
    if (script)
    {
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        script->propagateValues();
        script->finalPass();
        script->validateGlobals(mono);
        script->checkSymbols();
    }

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
        problems.push_back(std::move(problem));
    }
    return problems;
}

// --- what is in scope here -----------------------------------------------------

std::vector<ALScriptCompletion> ALLSLService::symbols(std::string_view source, S32 line, S32 column)
{
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
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
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
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
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
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
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
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
    Tailslide::ScopedScriptParser     parser(nullptr);
    Tailslide::LSLScript*             script = resolved(parser, source);
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
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
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
    if (!parameters)
    {
        return {};
    }
    Tailslide::ScopedScriptParser parser(nullptr);
    Tailslide::LSLScript*         script = resolved(parser, source);
    if (!script)
    {
        return {};
    }
    Hints hints;
    script->visit(&hints);
    std::stable_sort(hints.out.begin(), hints.out.end());
    return std::move(hints.out);
}

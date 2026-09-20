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

#include <algorithm>
#include <cstring>

namespace
{
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
    error.clear();
    return true;
}

bool ALLSLService::hasBuiltins() const
{
    return mImpl->builtins;
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
        if (symbol->getSubType() != Tailslide::SYM_BUILTIN && symbol->getLoc()->first_line > 0)
        {
            answer.hasDefinition    = true;
            answer.definitionLine   = zeroBased(symbol->getLoc()->first_line);
            answer.definitionColumn = zeroBased(symbol->getLoc()->first_column);
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

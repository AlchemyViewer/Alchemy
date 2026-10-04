/**
 * @file allsloptimizer.cpp
 * @brief The LSL optimizer over Tailslide's tree.
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

/**
 * @file allsloptimizer.cpp
 * @brief The LSL optimizer over Tailslide's tree.
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

#include "allsloptimizer.h"

#include "alscriptengine.h"

#include "allslbranches.h"
#include "allslcosts.h"
#include "allsldeadcode.h"
#include "allsleffects.h"
#include "allslflowvalues.h"
#include "allslfolder.h"
#include "allslinliner.h"
#include "allslnames.h"
#include "allsloptimizerpass.h"
#include "allslprinter.h"
#include "allslrepeatedcalls.h"
#include "allslservice.h"
#include "allslshapes.h"
#include "allslsimplifier.h"

#include <tailslide/passes/values.hh>

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <optional>

namespace
{
    using namespace ALLSLPasses;

    // How many nodes a script is, for the budget: what one walk of it
    // visits.
    struct Gather : public ASTVisitor
    {
        size_t& count;
        explicit Gather(size_t& into) : count(into) {}
        bool visit(LSLASTNode* node) override
        {
            ++count;
            return true;
        }
    };

    void collectMessages(Logger& logger, ALScriptProblems& problems)
    {
        for (LogMessage* message : logger.getMessages())
        {
            if (message->getType() != LOG_ERROR && message->getType() != LOG_INTERNAL_ERROR)
            {
                continue;
            }
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Error;
            p.source   = message->getError() == E_SYNTAX_ERROR || message->getError() == E_PARSER_STACK_DEPTH ? ALScriptProblem::Source::Parser
                                                                                                                   : ALScriptProblem::Source::Types;
            p.line      = zeroBased(message->getLoc()->first_line);
            p.column    = zeroBased(message->getLoc()->first_column);
            p.endLine   = zeroBased(message->getLoc()->last_line);
            p.endColumn = std::max(0, message->getLoc()->last_column);
            p.code      = std::to_string(static_cast<int>(message->getError()));
            p.message   = message->getMessage();
            problems.push_back(std::move(p));
        }
    }

    // Whether a statement standing as an if's true branch, with an else
    // after it, would take that else for its own once printed: an if with
    // no else, or a loop or an else that ends in one.
    bool endsInBareIf(LSLASTNode* statement)
    {
        LSLASTNode* s = statement;
        while (s && s->getNodeType() == NODE_STATEMENT)
        {
            switch (s->getNodeSubType())
            {
                case NODE_IF_STATEMENT:
                {
                    LSLASTNode* otherwise = s->getChild(2);
                    if (!otherwise || otherwise->getNodeType() == NODE_NULL)
                    {
                        return true;
                    }
                    s = otherwise;
                    break;
                }
                case NODE_WHILE_STATEMENT:
                    s = s->getChild(1);
                    break;
                case NODE_FOR_STATEMENT:
                    s = s->getChild(3);
                    break;
                default:
                    return false;
            }
        }
        return false;
    }

    // Braces round each true branch that would otherwise take the else
    // after it. The passes move branches about -- `if (!c) A else B` swapped
    // round, an empty branch turned into a negated condition -- and the
    // printer, as the grammar reads it back, gives an else to the nearest
    // if before it.
    void braceDanglingElses(LSLASTNode* node, ScriptAllocator* allocator)
    {
        for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            braceDanglingElses(child, allocator);
        }
        if (node->getNodeType() != NODE_STATEMENT || node->getNodeSubType() != NODE_IF_STATEMENT)
        {
            return;
        }
        LSLASTNode* otherwise = node->getChild(2);
        if (!otherwise || otherwise->getNodeType() == NODE_NULL || !endsInBareIf(node->getChild(1)))
        {
            return;
        }
        LSLASTNode* yes   = node->takeChild(1);
        auto*       block = allocator->newTracked<LSLCompoundStatement>(nullptr);
        block->setLoc(yes->getLoc());
        block->pushChild(yes);
        node->setChild(1, block);
    }
    // A user function a run left standing, with what putting it in place
    // would take but for its bytes (ALLSLCosts), and where it is in the
    // source.
    struct Standing
    {
        std::string          name;
        ALLSLCosts::Inlining shape;
        S32                  line      = 0;
        S32                  column    = 0;
        S32                  endLine   = 0;
        S32                  endColumn = 0;
    };

    // Each user function left in the script, as the inliner would put it
    // in place: one whose body is `return e;` as its expression, with its
    // arguments where its parameters were; any other as a block, each
    // parameter a local and its value another, each return but a last one
    // a jump.
    void standingFunctions(LSLScript* script, const ALSourceMap& inlinedMap, std::vector<Standing>& out)
    {
        if (!script->getGlobals())
        {
            return;
        }
        for (LSLASTNode* global : *script->getGlobals())
        {
            if (global->getNodeType() != NODE_GLOBAL_FUNCTION)
            {
                continue;
            }
            auto*         function = static_cast<LSLGlobalFunction*>(global);
            LSLSymbol*    sym      = function->getSymbol();
            LSLStatement* body     = function->getStatements();
            if (!sym || !body)
            {
                continue;
            }
            Standing f;
            f.name        = sym->getName();
            f.shape.name  = f.name.size();
            f.shape.calls = sym->getReferences() - 1;
            if (LSLFunctionDec* dec = function->getArguments())
            {
                f.shape.params = static_cast<S32>(dec->getNumChildren());
            }
            const bool   value = sym->getType() && sym->getType()->getIType() != LST_NULL;
            LSLASTNode*  last  = nullptr;
            for (LSLASTNode* statement = body->getChild(0); statement; statement = statement->getNext())
            {
                last = statement;
            }
            const bool returnsLast = last && last->getNodeType() == NODE_STATEMENT && last->getNodeSubType() == NODE_RETURN_STATEMENT;
            const bool expression  = value && returnsLast && body->getNumChildren() == 1;
            f.shape.locals         = expression ? 0 : f.shape.params + (value ? 1 : 0);
            boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> strings;
            std::vector<LSLASTNode*>                                                 stack{ body };
            while (!stack.empty())
            {
                LSLASTNode* node = stack.back();
                stack.pop_back();
                if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_RETURN_STATEMENT && node != last)
                {
                    ++f.shape.jumps;
                }
                else if (node->getNodeType() == NODE_CONSTANT && node->getNodeSubType() == NODE_STRING_CONSTANT)
                {
                    strings.insert(static_cast<LSLStringConstant*>(node)->getValue());
                }
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    stack.push_back(child);
                }
            }
            f.shape.strings = static_cast<S32>(strings.size());
            for (const std::string& text : strings)
            {
                f.shape.chars += static_cast<S32>(text.size());
            }
            if (const Tailslide::YYLTYPE* loc = function->getLoc())
            {
                f.line      = zeroBased(loc->first_line);
                f.column    = zeroBased(loc->first_column);
                f.endLine   = zeroBased(loc->last_line);
                f.endColumn = zeroBased(loc->last_column);
                if (!inlinedMap.empty())
                {
                    const ALSourceMap::Loc from = inlinedMap.toSource(f.line, f.column);
                    const ALSourceMap::Loc to   = inlinedMap.toSource(f.endLine, f.endColumn);
                    if (from.found() && to.found())
                    {
                        f.line      = from.line;
                        f.column    = from.column;
                        f.endLine   = to.line;
                        f.endColumn = to.column;
                    }
                }
            }
            out.push_back(std::move(f));
        }
    }
    // One run over a script: put in place what is to be, then the rounds,
    // then the printing, and what was printed checked. The functions it
    // leaves standing into `standing`, where that is asked for; a list's
    // shapes made where `lists` says so.
    ALLSLOptimizer::Result once(std::string_view source, const ALLSLOptimizer::Options& options, std::vector<Standing>* standing, bool lists = true)
    {
        using Result = ALLSLOptimizer::Result;
        Result result;
        result.text       = std::string(source);
        result.sizeBefore = source.size();
        result.sizeAfter  = source.size();
        result.map = ALSourceMap::identity(result.text);
        if (!ALLSLService::builtinsLoaded())
        {
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Error;
            p.source   = ALScriptProblem::Source::Optimizer;
            p.message  = "the LSL definitions are not loaded, so nothing was optimized";
            p.key      = "OptimizerNoDefinitions";
            result.problems.push_back(std::move(p));
            return result;
        }

        // The functions called once put in place first, in the text, so that
        // what is parsed below is an ordinary script; its map is under the
        // printer's. Before the engine is taken for the rest, so that the
        // inliner's hold is its only one and it can let go between rounds.
        std::string inlined;
        ALSourceMap inlinedMap;
        size_t      spent = 0;
        if (options.inlining || !options.constFunctions.empty())
        {
            ALLSLInliner::Asked asked;
            asked.marked             = options.inlineNames;
            asked.constant           = options.constFunctions;
            asked.others             = options.inlining;
            ALLSLInliner::Result put = ALLSLInliner::run(source, asked, options.visitBudget);
            // One budget for the two: what the inliner visited is spent.
            spent = put.visited;
            if (put.stoppedEarly)
            {
                ALScriptProblem p;
                p.severity = ALScriptProblem::Severity::Note;
                p.source   = ALScriptProblem::Source::Optimizer;
                p.key      = "InlinerStoppedEarly";
                p.message  = "stopped putting functions in place part way: there may be more to put in place";
                result.problems.push_back(std::move(p));
            }
            if (put.inlined > 0)
            {
                inlined    = std::move(put.text);
                inlinedMap = std::move(put.map);
                source     = inlined;
                for (ALScriptProblem& note : put.notes)
                {
                    result.problems.push_back(std::move(note));
                }
            }
        }
        AL_SCRIPT_ENGINE_HELD;
        // What is said from here on is said of the inlined text, and brought
        // back to the source on the way out.
        const size_t saidOfInlined = result.problems.size();
        const auto   bringBack     = [&]() {
            if (inlinedMap.empty())
            {
                return;
            }
            for (size_t i = saidOfInlined; i < result.problems.size(); ++i)
            {
                ALScriptProblem&       p    = result.problems[i];
                const ALSourceMap::Loc from = inlinedMap.toSource(p.line, p.column);
                const ALSourceMap::Loc to   = inlinedMap.toSource(p.endLine, p.endColumn);
                if (from.found())
                {
                    p.line   = from.line;
                    p.column = from.column;
                }
                if (to.found())
                {
                    p.endLine   = to.line;
                    p.endColumn = to.column;
                }
            }
        };

        ScopedScriptParser parser(nullptr);
        LSLScript*         script = parser.parseLSLBytes(source.data(), static_cast<int>(source.size()));
        if (!script || parser.logger.getErrors())
        {
            collectMessages(parser.logger, result.problems);
            bringBack();
            result.uncompiled = inlinedMap.empty();
            return result;
        }
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        ALLSLArithmetic behavior(&parser.allocator, options.addstrings, options.target);
        // Tailslide's values until what the script writes is known, and
        // with it, where folding is asked for, what the locals hold as the
        // code runs (FlowValues).
        const ALLSLEffects* flowing   = nullptr;
        const auto          propagate = [&]() {
            if (flowing)
            {
                flowValues(script, &behavior, &parser.allocator, *flowing, options.target != ALLSLOptimizer::Target::Luau);
                return;
            }
            ConstantDeterminingVisitor values(&behavior, &parser.allocator);
            script->visit(&values);
        };
        propagate();
        script->finalPass();
        if (parser.logger.getErrors())
        {
            collectMessages(parser.logger, result.problems);
            bringBack();
            result.uncompiled = inlinedMap.empty();
            return result;
        }

        const ALLSLEffects effects(script);
        if (options.constfold)
        {
            flowing = &effects;
            propagate();
        }
        Ctx                ctx;
        ctx.allocator = &parser.allocator;
        ctx.context   = &parser.context;
        ctx.target    = options.target;
        ctx.foldtabs  = options.foldtabs;
        ctx.effects   = &effects;
        Report report(result.problems, options.notes);
        // Each pass opens the way for the others; round and round until a
        // round changes nothing -- or until the run has visited as much as
        // its budget allows, since a large script whose passes keep finding
        // work would otherwise hold whoever asked for as long as it liked.
        // Every walk of the script counted as it is made -- a pass, and the
        // references and values found again after a pass that changed
        // something -- from what the inliner left of the budget.
        size_t     visited = spent;
        const auto nodes   = [&]() {
            size_t count = 0;
            Gather gather(count);
            script->visit(&gather);
            return count;
        };
        const size_t perWalk = std::max<size_t>(1, nodes());
        const auto   walks   = [&](size_t count) { visited += perWalk * count; };
        const auto   refresh = [&]() {
            script->recalculateReferenceData();
            propagate();
            walks(2);
        };
        const auto settle = [&]() {
            for (int round = 0; round < 64; ++round)
            {
                // No round begun that the budget cannot see through its passes.
                if (visited + perWalk * 3 > options.visitBudget)
                {
                    result.stoppedEarly = true;
                    report.note(nullptr, "OptimizerStoppedEarly", "stopped after [1] rounds: there may be more to do", { std::to_string(round) });
                    break;
                }
                // The references and values found again after the folder and the
                // simplifier together, not after each: what the folder makes are
                // constants, whose values the simplifier reads off them, and what
                // counts it leaves behind can only be too high -- which a pass
                // takes as a reason to do less, never as leave to do wrong. Dead
                // code is looked for with everything found again, since what
                // runs is told from the values.
                int changes = 0;
                if (options.constfold)
                {
                    const int folded = fold(ctx, report, options, script);
                    walks(1);
                    const int simplified = simplify(ctx, report, options, script);
                    walks(1);
                    if (folded + simplified)
                    {
                        changes += folded + simplified;
                        refresh();
                    }
                }
                if (options.dcr)
                {
                    const int removed = removeDead(ctx, report, options, script);
                    walks(1);
                    if (removed)
                    {
                        changes += removed;
                        refresh();
                    }
                }
                // Branches and loops in fewer jumps, once what can never run
                // is gone.
                if (options.constfold)
                {
                    const int restructured = restructure(ctx, report, options, script);
                    walks(1);
                    if (restructured)
                    {
                        changes += restructured;
                        refresh();
                    }
                }
                if (!changes)
                {
                    break;
                }
            }
        };
        settle();
        // A call made again and again kept in a local, once nothing more
        // will fold; and the rounds again where one was, for what the
        // places it leaves open.
        if (options.constfold && !result.stoppedEarly)
        {
            const int kept = keepRepeatedCalls(ctx, report, options, script);
            walks(1);
            if (kept)
            {
                refresh();
                settle();
            }
        }
        // What is made for size alone, once nothing more will fold.
        bool weighLists = false;
        if (options.constfold)
        {
            shape(ctx, report, options, script, ShapeStage::Values);
            walks(1);
            // A list's shapes on Mono, which references a list's helpers
            // once for the whole script -- one to add a string, another to
            // add an integer, one for a literal. At each place they are
            // smaller (ALLSLCosts), which is all they could cost but for a
            // helper nothing in the script called before: where they bring
            // one in, and the places they save at are too few to pay for it
            // at the most a helper costs, the script is weighed with them
            // and without (below), and the smaller kept.
            const bool  mono = lists && options.target == ALLSLOptimizer::Target::Mono;
            const ListHelpers had = mono ? listHelpers(script) : ListHelpers{};
            if (lists)
            {
                const int shaped = shape(ctx, report, options, script, ShapeStage::Lists, mono ? &had : nullptr);
                walks(1);
                if (mono && shaped)
                {
                    const ListHelpers has   = listHelpers(script);
                    const ALLSLCosts& costs = ALLSLCosts::of(options.target);
                    weighLists              = shaped * costs.listShapeLeast <= had.freshIn(has) * costs.listHelperMost;
                }
            }
        }
        if (standing)
        {
            standingFunctions(script, inlinedMap, *standing);
        }
        if (options.shrinknames)
        {
            shrinkNames(script, parser.context, parser.allocator, report, result, options.target);
        }
        braceDanglingElses(script, &parser.allocator);

        Printed     printed = print(script, options);
        std::string written = std::move(printed.text);
        // What was written must check as what it was made from did: a script
        // the optimizer made unable to compile is the optimizer's fault, and
        // the source goes as it was, said so, rather than that.
        if (std::optional<ALScriptProblem> refused = ALLSLOptimizer::checkWritten(written))
        {
            result.problems.clear();
            result.problems.push_back(std::move(*refused));
            return result;
        }
        result.text = std::move(written);
        result.map  = std::move(printed.map);
        if (!inlinedMap.empty())
        {
            result.map = result.map.composed(inlinedMap);
        }
        bringBack();
        result.sizeAfter = result.text.size();
        result.optimized = true;
        if (weighLists)
        {
            // Made again without a list's shapes, both weighed, the smaller
            // kept with its weight.
            std::vector<Standing>  alone;
            ALLSLOptimizer::Result plain = once(source, options, standing ? &alone : nullptr, false);
            result.weight                = weigh(options.target, result.text);
            plain.weight                 = weigh(options.target, plain.text);
            if (plain.optimized && plain.weight.compiled && result.weight.compiled && plain.weight.total <= result.weight.total)
            {
                if (standing)
                {
                    *standing = std::move(alone);
                }
                return plain;
            }
        }
        return result;
    }
} // namespace

ALLSLOptimizer::Result ALLSLOptimizer::run(std::string_view source, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const bool            byCost = options.inlining && options.inlineByCost;
    std::vector<Standing> standing;
    Result                kept = once(source, options, byCost ? &standing : nullptr);
    // Of the functions left standing, those called from more than one
    // place and not marked -- a marked one went wherever it could -- are
    // what the cost may put in place.
    std::erase_if(standing, [&options](const Standing& f) {
        return f.shape.calls < 2 || std::find(options.inlineNames.begin(), options.inlineNames.end(), f.name) != options.inlineNames.end();
    });
    if (!byCost || !kept.optimized || standing.empty())
    {
        return kept;
    }
    if (!kept.weight.compiled)
    {
        kept.weight = weigh(options.target, kept.text);
    }
    if (!kept.weight.compiled)
    {
        return kept;
    }
    // Each estimated from what it weighs as the run left it.
    const ALLSLCosts&                            costs = ALLSLCosts::of(options.target);
    std::vector<std::pair<const Standing*, S32>> estimated;
    for (const Standing& f : standing)
    {
        const auto        renamed = kept.renamed.find(f.name);
        const std::string shown   = renamed == kept.renamed.end() ? f.name : renamed->second;
        for (const ALScriptWeight::Part& part : kept.weight.parts)
        {
            if (part.kind != ALScriptWeight::Part::Kind::Function || part.name != shown)
            {
                continue;
            }
            ALLSLCosts::Inlining shape = f.shape;
            shape.bytes                = static_cast<S32>(part.bytes);
            shape.name                 = shown.size();
            estimated.emplace_back(&f, costs.inlined(shape));
            break;
        }
    }
    // Tried, and kept only where it is smaller as the compiler counts it:
    // those estimated within what an estimate is good to -- a function near
    // nothing either way is often worth it beside the others -- and those
    // estimated to save alone, where they are fewer; the smallest kept.
    constexpr S32         MARGIN = 16;
    size_t                last   = 0;
    std::optional<Result> best;
    for (const S32 under : { MARGIN, 0 })
    {
        Options                  more = options;
        std::vector<std::string> chosen;
        for (const auto& [f, estimate] : estimated)
        {
            if (estimate < under)
            {
                more.inlineNames.push_back(f->name);
                chosen.push_back(f->name);
            }
        }
        if (chosen.empty() || chosen.size() == last)
        {
            continue;
        }
        last = chosen.size();
        std::vector<Standing> left;
        Result                tried = once(source, more, &left);
        // What the inliner could not take is the same text.
        if (!tried.optimized || tried.text == kept.text)
        {
            continue;
        }
        if (!tried.weight.compiled)
        {
            tried.weight = weigh(options.target, tried.text);
        }
        if (!tried.weight.compiled || tried.weight.total >= (best ? best->weight.total : kept.weight.total))
        {
            continue;
        }
        const size_t saved = kept.weight.total - tried.weight.total;
        for (const auto& [f, estimate] : estimated)
        {
            if (std::find(chosen.begin(), chosen.end(), f->name) == chosen.end() || !options.notes ||
                std::any_of(left.begin(), left.end(), [f](const Standing& l) { return l.name == f->name; }))
            {
                continue;
            }
            ALScriptProblem p;
            p.severity  = ALScriptProblem::Severity::Note;
            p.source    = ALScriptProblem::Source::Optimizer;
            p.key       = "InlinerChoseFunction";
            p.line      = f->line;
            p.column    = f->column;
            p.endLine   = f->endLine;
            p.endColumn = f->endColumn;
            p.args      = { f->name, std::to_string(f->shape.calls), std::to_string(saved), ALScriptWeight::nameOf(weightTarget(options.target)) };
            p.message   = ALScriptProblem::fill("put the function [1] in place at its [2] calls, of what put in place so made the code [3] bytes smaller on [4]",
                                                p.args);
            tried.problems.push_back(std::move(p));
        }
        best = std::move(tried);
    }
    return best ? std::move(*best) : std::move(kept);
}

// static
void ALLSLOptimizer::foldGlobals(LSLScript* script, ScriptAllocator* allocator, ScriptContext* context, Target target)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!script || !script->getGlobals())
    {
        return;
    }
    Options options;
    options.target     = target;
    options.addstrings = true;
    options.notes      = false;
    ALLSLArithmetic behavior(allocator, options.addstrings, target);
    Ctx             ctx;
    ctx.allocator = allocator;
    ctx.context   = context;
    ctx.target    = target;
    ALScriptProblems unsaid;
    Report           report(unsaid, false);
    script->recalculateReferenceData();
    // A value folded may be what another is folded from: round and round,
    // as a run does, until a round folds nothing.
    for (int round = 0; round < 16; ++round)
    {
        ConstantDeterminingVisitor values(&behavior, allocator);
        script->visit(&values);
        if (!foldGlobalValues(ctx, report, options, script))
        {
            break;
        }
        script->recalculateReferenceData();
    }
}

// static
std::optional<ALScriptProblem> ALLSLOptimizer::checkWritten(std::string_view written)
{
    AL_SCRIPT_ENGINE_HELD;
    ScopedScriptParser check(nullptr);
    const std::string  text(written);
    LSLScript*         again = check.parseLSLBytes(text.data(), static_cast<int>(text.size()));
    if (again && !check.logger.getErrors())
    {
        again->collectSymbols();
        again->determineTypes();
    }
    if (again && !check.logger.getErrors())
    {
        return std::nullopt;
    }
    ALScriptProblems said;
    collectMessages(check.logger, said);
    ALScriptProblem p;
    p.severity = ALScriptProblem::Severity::Warning;
    p.source   = ALScriptProblem::Source::Optimizer;
    p.key      = "OptimizerWroteUncompilable";
    p.args     = { said.empty() ? std::string() : said.front().message };
    p.message  = ALScriptProblem::fill("not optimized: what the optimizer made of this script did not compile ([1]), so it goes as it was; "
                                       "please report it",
                                       p.args);
    return p;
}

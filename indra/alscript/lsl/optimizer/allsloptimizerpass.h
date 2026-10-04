/**
 * @file allsloptimizerpass.h
 * @brief What the LSL optimizer's passes share.
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

#pragma once

#include "allsloptimizer.h"

#include "allslcosts.h"
#include "allsltraits.h"
#include "allslvalues.h"
#include "llstl.h"

#include <tailslide/tailslide.hh>
#include <tailslide/visitor.hh>

#include <boost/unordered/unordered_flat_map.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALLSLEffects;

// What the LSL optimizer's passes share: each pass is a file of its own
// (allslfolder, allslsimplifier, allsldeadcode, ...), run by
// ALLSLOptimizer::run (allsloptimizer.cpp) over one parsed script, and
// none is known outside the optimizer. Here: the notes a pass makes, the
// run's context, a pass's base, and what each needs of Tailslide's tree.
namespace ALLSLPasses
{
    using namespace Tailslide;

    // ---- what the definitions say of the library -------------------------------------

    inline bool isPure(const char* name) { return ALLSLTraits::pure(name); }

    inline S32 zeroBased(int one) { return std::max(0, one - 1); }

    // ---- numbers as the printer writes them ----------------------------------------------

    using ALLSLValues::floatText;
    using ALLSLValues::integral;

    inline bool ascii(const char* s)
    {
        for (; *s; ++s)
        {
            if (static_cast<unsigned char>(*s) >= 0x80 || (*s < 0x20 && *s != '\n' && *s != '\t'))
            {
                return false;
            }
        }
        return true;
    }

    // What a number can be, the least and the most, where something
    // says: a constant; a library function's answer (ALLSLTraits::bounds),
    // llFrand's of a magnitude whose sign is known; a truth; an & with a
    // number not below nought; a list's length as l != []; a local set
    // to one of these where it is declared and never after.
    struct Range
    {
        double least = 0.0;
        double most  = 0.0;
    };
    std::optional<Range> rangeOf(LSLExpression* e, int depth = 0);

    // ---- notes ---------------------------------------------------------------------------

    class Report
    {
    public:
        Report(ALScriptProblems& problems, bool wanted) : mProblems(problems), mWanted(wanted) {}

        // Whether anybody reads the notes: a pass that would have to
        // print a subtree to say what it did asks first.
        bool wanted() const { return mWanted; }

        // A note with a key a translation may be found under, and the
        // words -- [1], [2] ... in the text -- it is built from.
        void note(const Tailslide::YYLTYPE* loc, const char* key, std::string_view text, std::vector<std::string> args = {})
        {
            if (!mWanted)
            {
                return;
            }
            ALScriptProblem p;
            p.severity = ALScriptProblem::Severity::Note;
            p.source   = ALScriptProblem::Source::Optimizer;
            if (loc)
            {
                p.line      = zeroBased(loc->first_line);
                p.column    = zeroBased(loc->first_column);
                p.endLine   = zeroBased(loc->last_line);
                // Tailslide's last column is one past the end, counted from
                // one: the end as the studio counts it, as the LSL service
                // reads it.
                p.endColumn = zeroBased(loc->last_column);
            }
            p.message = ALScriptProblem::fill(text, args);
            p.key     = key;
            p.args    = std::move(args);
            mProblems.push_back(std::move(p));
        }

    private:
        ALScriptProblems& mProblems;
        bool              mWanted = true;
    };

    // A node as Tailslide prints it, from its first character to its
    // last: what a note says was, and is.
    std::string render(LSLASTNode* node);

    inline bool sideEffectFree(LSLASTNode* node) { return ALLSLTraits::sideEffectFree(node); }
    // What may be dropped, though not moved: a read of the world or the
    // clock changes nothing.
    inline bool changesNothing(LSLASTNode* node) { return ALLSLTraits::changesNothing(node); }

    // Skips the parentheses around an expression.
    inline LSLExpression* bare(LSLExpression* expr)
    {
        while (expr && expr->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            expr = static_cast<LSLParenthesisExpression*>(expr)->getChildExpr();
        }
        return expr;
    }

    inline bool isInteger(LSLASTNode* node, int value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(cv)->getValue() == value;
    }

    inline bool isFloat(LSLASTNode* node, double value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_FLOAT_CONSTANT && static_cast<LSLFloatConstant*>(cv)->getValue() == value;
    }

    inline bool isEmptyString(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_STRING_CONSTANT && !*static_cast<LSLStringConstant*>(cv)->getValue();
    }

    inline bool isEmptyList(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_LIST_CONSTANT && static_cast<LSLListConstant*>(cv)->getLength() == 0;
    }

    // Whether the parent of an expression is another expression that could
    // bind a replacement differently: parentheses go around it then. The
    // right side of an assignment binds nothing.
    bool wantsParens(LSLASTNode* parent, LSLASTNode* old);

    // Puts `replacement` where `old` stands, in parentheses if the place
    // could bind it differently.
    void putInPlace(LSLASTNode* old, LSLExpression* replacement, ScriptAllocator* allocator);

    // Nodes put together off the script -- a sum built a term at a time
    // -- with Tailslide's counting of the references under each held off
    // while they are: every node made counts its children's whole
    // subtrees again, so a sum of n terms, each the left side of the
    // next, is n squared. The whole is counted once as it is put in
    // place. Tailslide holds it off the same way while it parses; a node
    // made meanwhile is marked made by us, as it would have been.
    class Uncounted
    {
    public:
        explicit Uncounted(ScriptContext& context) : mContext(context), mWas(context.parsing) { mContext.parsing = true; }
        ~Uncounted() { mContext.parsing = mWas; }
        Uncounted(const Uncounted&)            = delete;
        Uncounted& operator=(const Uncounted&) = delete;

        template <class T> T* made(T* node) const
        {
            node->setSynthesized(true);
            return node;
        }

    private:
        ScriptContext& mContext;
        const bool     mWas;
    };

    // Every node of a subtree, the root first.
    template <class F> void eachNode(LSLASTNode* root, const F& f)
    {
        std::vector<LSLASTNode*> stack{ root };
        while (!stack.empty())
        {
            LSLASTNode* node = stack.back();
            stack.pop_back();
            if (!node)
            {
                continue;
            }
            f(node);
            for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
            {
                stack.push_back(child);
            }
        }
    }

    // How many labels of a name the function or event a node is in has.
    // A jump goes to the last of them; the compiler finds it among those
    // in scope.
    int labelsNamed(LSLASTNode* node, const char* name);

    // Whether what follows a statement never runs by running on from it:
    // it returns, changes state or jumps; resets the script; is an if
    // every way of which does so, or a loop that never ends.
    bool stops(LSLASTNode* stmt);

    // Whether a label stands anywhere in a subtree.
    bool holdsLabel(LSLASTNode* root);

    // A block's statements made `statements`, in that order: those it had
    // and those moved in from elsewhere, none counted again.
    void setStatements(LSLASTNode* block, const std::vector<LSLASTNode*>& statements, ScriptContext& context);

    // What a constant costs held in a global against written where it is
    // read, on a target (ALLSLCosts::Held): a float's, a vector's and a
    // rotation's as whole or not, a string's and a key's by their length;
    // nothing for a list.
    std::optional<ALLSLCosts::Held> heldFor(ALLSLOptimizer::Target target, LSLConstant* cv);

    // What a text weighs on the target's compiler, and the weigher's name
    // for the target.
    ALScriptWeight::Target weightTarget(ALLSLOptimizer::Target target);
    ALScriptWeight         weigh(ALLSLOptimizer::Target target, std::string_view text);

    struct Ctx
    {
        ScriptAllocator*       allocator = nullptr;
        ScriptContext*         context   = nullptr;
        ALLSLOptimizer::Target target    = ALLSLOptimizer::Target::Mono;
        bool                   foldtabs  = false;
        // Whether a call is larger than the string that answers it, by the
        // call as written and whether the code holds the answer already:
        // weighed once for the run (Folder::smallerAnswered).
        boost::unordered_flat_map<std::string, bool, ll::string_hash, std::equal_to<>> answers;
        // What each function may write, found once for the run: the passes
        // only take code away, so what it says stays true, if more than
        // is left.
        const ALLSLEffects*    effects   = nullptr;

        // A builtin constant as an answer: the JSON_* names, whose values
        // are characters no literal may carry, are folded to the name.
        // The value handed back is the builtin's own, which the call
        // recognises and puts the name in for.
        std::map<LSLConstant*, std::string> namedValues;
        LSLConstant*                        builtin(const char* name)
        {
            if (!context || !context->builtins)
            {
                return nullptr;
            }
            LSLSymbol* sym = context->builtins->lookup(name, SYM_VARIABLE);
            if (!sym || !sym->getConstantValue())
            {
                return nullptr;
            }
            namedValues.emplace(sym->getConstantValue(), name);
            return sym->getConstantValue();
        }
        const std::string* nameOf(LSLConstant* value) const
        {
            const auto found = namedValues.find(value);
            return found == namedValues.end() ? nullptr : &found->second;
        }

        LSLConstant* integer(int v) { return allocator->newTracked<LSLIntegerConstant>(v); }
        LSLConstant* number(double v)
        {
            const bool single = target != ALLSLOptimizer::Target::Luau;
            if (single)
            {
                const float f = static_cast<float>(v);
                if (!std::isfinite(f))
                {
                    return nullptr;
                }
                return allocator->newTracked<LSLFloatConstant>(static_cast<double>(f));
            }
            return std::isfinite(v) ? allocator->newTracked<LSLFloatConstant>(v) : nullptr;
        }
        LSLConstant* string(const std::string& s)
        {
            if (!ascii(s.c_str()) || (!foldtabs && s.find('\t') != std::string::npos))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLStringConstant>(allocator->copyStr(s.c_str()));
        }
        LSLConstant* key(const std::string& s)
        {
            if (!ascii(s.c_str()))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLKeyConstant>(allocator->copyStr(s.c_str()));
        }
        LSLConstant* vector(double x, double y, double z)
        {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLVectorConstant>(fx, fy, fz);
        }
        LSLConstant* rotation(float x, float y, float z, float s)
        {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(s))
            {
                return nullptr;
            }
            return allocator->newTracked<LSLQuaternionConstant>(x, y, z, s);
        }
    };
    using Counted = boost::unordered_flat_map<std::string, int, ll::string_hash, std::equal_to<>>;

    // The string literals in what it visits, keys' too.
    struct Strings : public ASTVisitor
    {
        Counted found;

        bool visit(LSLStringConstant* c) override
        {
            ++found[c->getValue()];
            return false;
        }
        bool visit(LSLKeyConstant* c) override
        {
            ++found[c->getValue()];
            return false;
        }
    };

    struct Pass
    {
        Ctx&                          ctx;
        Report&                       report;
        const ALLSLOptimizer::Options& options;
        int                           changes = 0;

        Pass(Ctx& c, Report& r, const ALLSLOptimizer::Options& o) : ctx(c), report(r), options(o) {}

        LSLConstantExpression* constant(LSLConstant* cv, LSLASTNode* at)
        {
            auto* expr = ctx.allocator->newTracked<LSLConstantExpression>(cv);
            expr->setLoc(at->getLoc());
            return expr;
        }

        // Whether a value may be written into the script as a literal. A
        // list literal is no smaller than the list expression it came
        // from, but it is smaller than a call.
        bool inlineable(LSLConstant* cv, bool listsToo = false)
        {
            if (!cv || cv->containsNaN())
            {
                return false;
            }
            switch (cv->getIType())
            {
                case LST_LIST:
                    if (!listsToo)
                    {
                        return false;
                    }
                    for (LSLASTNode* item : *cv)
                    {
                        if (!inlineable(static_cast<LSLConstant*>(item)) && item->getIType() != LST_KEY)
                        {
                            return false;
                        }
                    }
                    return true;
                case LST_KEY:
                    // A key literal is a string.
                    return false;
                case LST_STRING:
                {
                    const char* s = static_cast<LSLStringConstant*>(cv)->getValue();
                    return options.foldtabs || !strchr(s, '\t');
                }
                case LST_FLOATINGPOINT:
                    return std::isfinite(static_cast<LSLFloatConstant*>(cv)->getValue());
                default:
                    return true;
            }
        }

        void fold(LSLASTNode* node, LSLConstant* cv, const char* key, const char* what)
        {
            // What it was and what it became, printed only where the
            // notes are read: printing is a visit of the whole subtree,
            // and a fold is what a pass does most.
            LSLConstantExpression* expr = nullptr;
            if (report.wanted())
            {
                const std::string was = render(node);
                expr                  = constant(cv, node);
                const std::string now = render(expr);
                if (was != now)
                {
                    report.note(node->getLoc(), key, std::string(what) + " [1] to [2]", { was, now });
                }
            }
            else
            {
                expr = constant(cv, node);
            }
            LSLASTNode::replaceNode(node, expr);
            ++changes;
        }
    };
}

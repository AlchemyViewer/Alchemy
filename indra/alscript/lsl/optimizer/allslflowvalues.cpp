/**
 * @file allslflowvalues.cpp
 * @brief What a local holds as the code runs, for the LSL optimizer.
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

#include "allslflowvalues.h"

#include "allsleffects.h"

#include <tailslide/passes/values.hh>

#include <boost/unordered/unordered_flat_map.hpp>

#include <cmath>
#include <cstring>

namespace ALLSLPasses
{
namespace
{
    // Tailslide's values, and with them a local's or a parameter's through
    // the code that runs on from where it was set to a constant: read
    // before it is set again, on every way there, it is that constant --
    // where Tailslide gives one only to a variable never set after it is
    // declared. What is known goes with the code as it runs: a statement's
    // reads see it as the statement begins and its writes change it as it
    // ends, and a read that something in the same statement may write
    // first -- LSL runs a binary operator's right side before its left --
    // sees nothing. Both ways of an if are followed, and what they agree on
    // goes on; a loop forgets what it writes before it begins, since it may
    // come round again; and a label forgets everything, since a jump may
    // come in from anywhere -- a loop holding one, everything before it
    // begins. Lists are left alone, as Tailslide leaves them; and a local
    // declared with no value is known only once it is set.
    class FlowValues : public ConstantDeterminingVisitor
    {
    public:
        // `vectors` where a vector's or a rotation's literal is no larger
        // than what it is worked out from: not on Luau, which builds one
        // where LSO and Mono load it.
        FlowValues(AOperationBehavior* behavior, ScriptAllocator* allocator, const ALLSLEffects& effects, bool vectors)
            : ConstantDeterminingVisitor(behavior, allocator), mEffects(effects), mVectors(vectors)
        {
        }

        bool beforeDescend(LSLASTNode* node) override
        {
            if (!ConstantDeterminingVisitor::beforeDescend(node))
            {
                if (node->getNodeType() == NODE_STATEMENT || node->getNodeType() == NODE_EXPRESSION)
                {
                    forget(mEffects.of(node));
                }
                return false;
            }
            switch (node->getNodeType())
            {
                case NODE_GLOBAL_FUNCTION:
                case NODE_EVENT_HANDLER:
                    mState = State{};
                    return true;
                case NODE_STATEMENT:
                    statement(node);
                    return false;
                default:
                    return true;
            }
        }

        bool visit(LSLLValueExpression* lvalue) override
        {
            ConstantDeterminingVisitor::visit(lvalue);
            LSLSymbol* sym = lvalue->getSymbol();
            if (!sym || sym->getAssignments() == 0 || !tracked(sym) || !mState.reachable || written(lvalue))
            {
                return true;
            }
            const auto found = mState.known.find(sym);
            if (found == mState.known.end() || (mHot && mHot->writes(sym) && !readFirst(lvalue, sym)))
            {
                return true;
            }
            LSLConstant* cv = found->second;
            if (LSLIdentifier* member = lvalue->getMember())
            {
                cv = part(cv, member->getName());
            }
            lvalue->setConstantValue(cv);
            return true;
        }

        bool visit(LSLDeclaration* decl) override
        {
            ConstantDeterminingVisitor::visit(decl);
            LSLSymbol*  sym  = decl->getSymbol();
            LSLASTNode* init = decl->getChild(1);
            if (!sym || !tracked(sym))
            {
                return false;
            }
            // The value Tailslide gave the symbol from what it was declared
            // with, made the symbol's type.
            LSLConstant* cv = init && init->getNodeType() != NODE_NULL ? sym->getConstantValue() : nullptr;
            if (cv && mState.reachable)
            {
                mState.known[sym] = cv;
            }
            else
            {
                mState.known.erase(sym);
            }
            return false;
        }

    private:
        struct State
        {
            boost::unordered_flat_map<LSLSymbol*, LSLConstant*> known;
            // False after a return, a jump or a change of state, until a label.
            bool                                                reachable = true;
        };

        bool tracked(LSLSymbol* sym) const
        {
            if (sym->getSymbolType() != SYM_VARIABLE ||
                (sym->getSubType() != SYM_LOCAL && sym->getSubType() != SYM_FUNCTION_PARAMETER && sym->getSubType() != SYM_EVENT_PARAMETER))
            {
                return false;
            }
            switch (sym->getIType())
            {
                case LST_INTEGER:
                case LST_FLOATINGPOINT:
                case LST_STRING:
                case LST_KEY:
                    return true;
                case LST_VECTOR:
                case LST_QUATERNION:
                    return mVectors;
                default:
                    return false;
            }
        }

        // What an assignment or an increment writes, not a read.
        static bool written(LSLLValueExpression* lvalue)
        {
            LSLASTNode* parent = lvalue->getParent();
            return parent && parent->getNodeType() == NODE_EXPRESSION && operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) &&
                   parent->getChild(0) == lvalue;
        }

        // Whether nothing of the expression a read is in that runs before it
        // writes what it reads.
        bool readFirst(LSLLValueExpression* lvalue, LSLSymbol* sym) const
        {
            for (LSLASTNode* earlier : ALLSLEffects::before(mRoot, lvalue))
            {
                if (mEffects.of(earlier).writes(sym))
                {
                    return false;
                }
            }
            return true;
        }

        // A vector's or a rotation's part, as Tailslide reads one.
        LSLConstant* part(LSLConstant* cv, const char* member) const
        {
            if (!member)
            {
                return nullptr;
            }
            if (cv->getIType() == LST_VECTOR)
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                switch (member[0])
                {
                    case 'x': return _mAllocator->newTracked<LSLFloatConstant>(v->x);
                    case 'y': return _mAllocator->newTracked<LSLFloatConstant>(v->y);
                    case 'z': return _mAllocator->newTracked<LSLFloatConstant>(v->z);
                    default: return nullptr;
                }
            }
            if (cv->getIType() == LST_QUATERNION)
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                switch (member[0])
                {
                    case 'x': return _mAllocator->newTracked<LSLFloatConstant>(q->x);
                    case 'y': return _mAllocator->newTracked<LSLFloatConstant>(q->y);
                    case 'z': return _mAllocator->newTracked<LSLFloatConstant>(q->z);
                    case 's': return _mAllocator->newTracked<LSLFloatConstant>(q->s);
                    default: return nullptr;
                }
            }
            return nullptr;
        }

        static bool same(LSLConstant* a, LSLConstant* b)
        {
            if (a == b)
            {
                return true;
            }
            if (a->getNodeSubType() != b->getNodeSubType())
            {
                return false;
            }
            // Bit for bit: -0.0 is not 0.0, and NaN is not itself.
            const auto bits = [](double x, double y) { return x == y && std::signbit(x) == std::signbit(y); };
            switch (a->getNodeSubType())
            {
                case NODE_INTEGER_CONSTANT:
                    return static_cast<LSLIntegerConstant*>(a)->getValue() == static_cast<LSLIntegerConstant*>(b)->getValue();
                case NODE_FLOAT_CONSTANT:
                    return bits(static_cast<LSLFloatConstant*>(a)->getValue(), static_cast<LSLFloatConstant*>(b)->getValue());
                case NODE_STRING_CONSTANT:
                    return !strcmp(static_cast<LSLStringConstant*>(a)->getValue(), static_cast<LSLStringConstant*>(b)->getValue());
                case NODE_KEY_CONSTANT:
                    return !strcmp(static_cast<LSLKeyConstant*>(a)->getValue(), static_cast<LSLKeyConstant*>(b)->getValue());
                case NODE_VECTOR_CONSTANT:
                {
                    const Vector3* u = static_cast<LSLVectorConstant*>(a)->getValue();
                    const Vector3* v = static_cast<LSLVectorConstant*>(b)->getValue();
                    return bits(u->x, v->x) && bits(u->y, v->y) && bits(u->z, v->z);
                }
                case NODE_QUATERNION_CONSTANT:
                {
                    const Quaternion* p = static_cast<LSLQuaternionConstant*>(a)->getValue();
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(b)->getValue();
                    return bits(p->x, q->x) && bits(p->y, q->y) && bits(p->z, q->z) && bits(p->s, q->s);
                }
                default:
                    return false;
            }
        }

        // What two ways that meet both know.
        static State merged(const State& a, const State& b)
        {
            if (!a.reachable)
            {
                return b;
            }
            if (!b.reachable)
            {
                return a;
            }
            State out;
            for (const auto& [sym, cv] : a.known)
            {
                const auto there = b.known.find(sym);
                if (there != b.known.end() && same(cv, there->second))
                {
                    out.known.emplace(sym, cv);
                }
            }
            return out;
        }

        void forget(const ALLSLEffects::Writes& writes)
        {
            for (LSLSymbol* sym : writes.variables)
            {
                mState.known.erase(sym);
            }
        }

        static bool holdsLabel(LSLASTNode* root)
        {
            std::vector<LSLASTNode*> stack{ root };
            while (!stack.empty())
            {
                LSLASTNode* n = stack.back();
                stack.pop_back();
                if (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL)
                {
                    return true;
                }
                for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                {
                    stack.push_back(child);
                }
            }
            return false;
        }

        // A loop about to begin: what it writes forgotten, or everything
        // where a jump could come into it.
        void entering(LSLASTNode* loop, const ALLSLEffects::Writes& writes)
        {
            if (holdsLabel(loop))
            {
                mState.known.clear();
                return;
            }
            forget(writes);
        }

        // An expression run as one: its reads see what is known as it
        // begins, and what it writes is known as it ends -- a variable set
        // by the whole of it to a constant, that constant.
        void expression(LSLASTNode* expr)
        {
            if (!expr || expr->getNodeType() == NODE_NULL)
            {
                return;
            }
            const ALLSLEffects::Writes writes = mEffects.of(expr);
            LSLASTNode* const          root   = mRoot;
            const ALLSLEffects::Writes* hot   = mHot;
            mRoot                             = expr;
            mHot                              = &writes;
            expr->visit(this);
            mRoot = root;
            mHot  = hot;
            LSLSymbol*   set = nullptr;
            LSLConstant* to  = nullptr;
            if (expr->getNodeType() == NODE_EXPRESSION && static_cast<LSLExpression*>(expr)->getOperation() == OP_ASSIGN &&
                expr->getChild(0)->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(expr->getChild(0))->getMember())
            {
                set                = expr->getChild(0)->getSymbol();
                LSLASTNode* value  = expr->getChild(1);
                to                 = set && !mEffects.of(value).writes(set) ? value->getConstantValue() : nullptr;
            }
            forget(writes);
            if (set && to && tracked(set) && mState.reachable)
            {
                if (to->getType() != set->getType())
                {
                    to = to->getType()->canCoerce(set->getType()) ? _mOperationBehavior->cast(set->getType(), to, to->getLoc()) : nullptr;
                }
                if (to)
                {
                    mState.known[set] = to;
                }
            }
        }

        void statement(LSLASTNode* stmt)
        {
            switch (stmt->getNodeSubType())
            {
                case NODE_COMPOUND_STATEMENT:
                    visitChildren(stmt);
                    return;
                case NODE_EXPRESSION_STATEMENT:
                    expression(stmt->getChild(0));
                    return;
                case NODE_RETURN_STATEMENT:
                    expression(stmt->getChild(0));
                    mState.reachable = false;
                    return;
                case NODE_DECLARATION:
                    // Known as visit(LSLDeclaration*) says, once its value has run.
                    stmt->getChild(0)->visit(this);
                    expression(stmt->getChild(1));
                    return;
                case NODE_STATE_STATEMENT:
                case NODE_JUMP_STATEMENT:
                    visitChildren(stmt);
                    mState.reachable = false;
                    return;
                case NODE_LABEL:
                    visitChildren(stmt);
                    mState = State{};
                    return;
                case NODE_IF_STATEMENT:
                {
                    expression(stmt->getChild(0));
                    const State before = mState;
                    stmt->getChild(1)->visit(this);
                    const State yes = std::move(mState);
                    mState          = before;
                    stmt->getChild(2)->visit(this);
                    mState = merged(yes, mState);
                    return;
                }
                case NODE_WHILE_STATEMENT:
                {
                    entering(stmt, mEffects.of(stmt));
                    expression(stmt->getChild(0));
                    const State checked = mState;
                    stmt->getChild(1)->visit(this);
                    mState = merged(checked, mState);
                    return;
                }
                case NODE_DO_STATEMENT:
                {
                    entering(stmt, mEffects.of(stmt));
                    const State begun = mState;
                    stmt->getChild(0)->visit(this);
                    expression(stmt->getChild(1));
                    mState = merged(begun, mState);
                    return;
                }
                case NODE_FOR_STATEMENT:
                {
                    for (LSLASTNode* init = stmt->getChild(0)->getChild(0); init; init = init->getNext())
                    {
                        expression(init);
                    }
                    ALLSLEffects::Writes loops = mEffects.of(stmt->getChild(1));
                    loops.add(mEffects.of(stmt->getChild(2)));
                    loops.add(mEffects.of(stmt->getChild(3)));
                    entering(stmt, loops);
                    expression(stmt->getChild(1));
                    const State checked = mState;
                    stmt->getChild(3)->visit(this);
                    for (LSLASTNode* step = stmt->getChild(2)->getChild(0); step; step = step->getNext())
                    {
                        expression(step);
                    }
                    mState = merged(checked, mState);
                    return;
                }
                default:
                    visitChildren(stmt);
                    forget(mEffects.of(stmt));
                    return;
            }
        }

        const ALLSLEffects&         mEffects;
        const bool                  mVectors;
        State                       mState;
        // The expression being run, and what it writes.
        LSLASTNode*                 mRoot = nullptr;
        const ALLSLEffects::Writes* mHot  = nullptr;
    };
}

    void flowValues(LSLScript* script, AOperationBehavior* behavior, ScriptAllocator* allocator, const ALLSLEffects& effects, bool vectors)
    {
        FlowValues values(behavior, allocator, effects, vectors);
        script->visit(&values);
    }
}

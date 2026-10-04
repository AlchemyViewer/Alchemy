/**
 * @file allslprinter.cpp
 * @brief The LSL optimizer's printer.
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

#include "allslprinter.h"

#include "allslcosts.h"

#include <tailslide/passes/pretty_print.hh>

#include <algorithm>
#include <cmath>

namespace ALLSLPasses
{
namespace
{
    // Tailslide's printer with floats that read back exactly, integers
    // where a float may be one, and a note of where every name, number
    // and statement came from.
    class Printer : public PrettyPrintVisitor
    {
    public:
        Printer(const PrettyPrintOpts& opts, const ALLSLOptimizer::Options& options) : PrettyPrintVisitor(opts), mOptions(options) {}

        bool visit(LSLIdentifier* id) override { return marked(id, [&] { return PrettyPrintVisitor::visit(id); }); }
        bool visit(LSLIntegerConstant* c) override
        {
            return marked(c, [&] {
                if (negativeCast(c, c->getValue()))
                {
                    mStream << "((integer)" << c->getValue() << ")";
                    return false;
                }
                return PrettyPrintVisitor::visit(c);
            });
        }
        bool visit(LSLStringConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
        bool visit(LSLKeyConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
        bool visit(LSLFloatConstant* c) override
        {
            return marked(c, [&] {
                const double v       = c->getValue();
                const bool   integer = integerAllowed(c);
                // A whole float where nothing converts an integer for it,
                // as the cast of one where that is smaller; not in a
                // global's value, which takes no cast, nor a negative
                // zero, which the cast would lose the sign of.
                if (!integer && mOptions.optfloats && ALLSLCosts::of(mOptions.target).castForWholeFloat && std::isfinite(v) && std::floor(v) == v &&
                    std::fabs(v) < 2147483648.0 && !(v == 0.0 && std::signbit(v)) && !inGlobal(c))
                {
                    mStream << "((float)" << static_cast<long long>(v) << ")";
                    return false;
                }
                mStream << signedNumber(c, v, integer);
                return false;
            });
        }
        bool visit(LSLVectorConstant* c) override
        {
            return marked(c, [&] {
                const Vector3* v = c->getValue();
                mStream << '<' << signedNumber(c, v->x, true) << ", " << signedNumber(c, v->y, true) << ", " << signedNumber(c, v->z, true) << '>';
                return false;
            });
        }
        bool visit(LSLQuaternionConstant* c) override
        {
            return marked(c, [&] {
                const Quaternion* q = c->getValue();
                mStream << '<' << signedNumber(c, q->x, true) << ", " << signedNumber(c, q->y, true) << ", " << signedNumber(c, q->z, true) << ", "
                        << signedNumber(c, q->s, true) << '>';
                return false;
            });
        }
        // Tailslide calls the type quaternion, which LSL takes, but the
        // word everyone writes is rotation.
        bool visit(LSLType* type) override
        {
            if (type->getIType() == LST_QUATERNION)
            {
                mStream << "rotation";
                return false;
            }
            return PrettyPrintVisitor::visit(type);
        }
        bool visit(LSLGlobalVariable* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLGlobalFunction* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLState* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLEventHandler* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLExpressionStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLDeclaration* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLIfStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLWhileStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLDoStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLForStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLReturnStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLJumpStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLLabel* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }
        bool visit(LSLStateStatement* n) override { return at(n, [&] { return PrettyPrintVisitor::visit(n); }); }

        // The map, once everything is printed.
        ALSourceMap map(const std::string& name)
        {
            ALSourceMap       out;
            out.addFile(name, std::string());
            const std::string text = mStream.str();
            // Marks that start together -- a statement and the name it
            // begins with -- kept in the order they were printed, so that
            // the map, which answers with the last at a place, answers the
            // same for the same script every time.
            std::stable_sort(mMarks.begin(), mMarks.end(), [](const Mark& a, const Mark& b) { return a.offset < b.offset; });
            S32    line = 0, column = 0;
            size_t pos  = 0;
            for (const Mark& mark : mMarks)
            {
                for (; pos < mark.offset && pos < text.size(); ++pos)
                {
                    if (text[pos] == '\n')
                    {
                        ++line;
                        column = 0;
                    }
                    else
                    {
                        ++column;
                    }
                }
                ALSourceMap::Segment s;
                s.outLine   = line;
                s.outColumn = column;
                s.length    = static_cast<S32>(mark.length);
                s.line      = zeroBased(mark.loc.first_line);
                s.column    = zeroBased(mark.loc.first_column);
                s.verbatim  = false;
                out.add(s);
            }
            out.finish();
            return out;
        }

    private:
        struct Mark
        {
            size_t  offset = 0;
            size_t  length = 0;
            Tailslide::YYLTYPE loc{};
        };

        template <class F> bool marked(LSLASTNode* node, F print)
        {
            const size_t start = static_cast<size_t>(mStream.tellp());
            const bool   r     = print();
            const size_t end   = static_cast<size_t>(mStream.tellp());
            if (node->getLoc()->first_line > 0)
            {
                mMarks.push_back(Mark{ start, end - start, *node->getLoc() });
            }
            return r;
        }

        template <class F> bool at(LSLASTNode* node, F print)
        {
            if (node->getLoc()->first_line > 0)
            {
                mMarks.push_back(Mark{ static_cast<size_t>(mStream.tellp()), 0, *node->getLoc() });
            }
            return print();
        }

        std::string number(double v, bool asInteger)
        {
            if (mOptions.optfloats && asInteger && integral(v) && ALLSLCosts::of(mOptions.target).integerForFloat)
            {
                return std::to_string(static_cast<long long>(v));
            }
            return floatText(v, mOptions.target == ALLSLOptimizer::Target::Luau);
        }

        // A negative number as the cast of one, where that is smaller
        // (ALLSLCosts::castForNegative): one constant, where -5 is 5 negated.
        // Not in a global's value, which takes no cast and reads -5 as one
        // constant already.
        bool negativeCast(LSLASTNode* c, double v) const
        {
            return mOptions.constfold && v < 0.0 && std::isfinite(v) && ALLSLCosts::of(mOptions.target).castForNegative && !inGlobal(c);
        }

        // A float as number() writes it, the cast of it where it is negative:
        // an integer's where it is written as one.
        std::string signedNumber(LSLASTNode* c, double v, bool asInteger)
        {
            const std::string text = number(v, asInteger);
            if (!negativeCast(c, v))
            {
                return text;
            }
            const bool whole = text.find_first_not_of("-0123456789") == std::string::npos;
            return std::string(whole ? "((integer)" : "((float)") + text + ")";
        }

        // Whether an integer literal may stand where this float does: where
        // LSL converts one on its own, and the operation does not change
        // with it.
        static bool inGlobal(LSLASTNode* c)
        {
            for (LSLASTNode* up = c->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return true;
                }
            }
            return false;
        }

        static bool integerAllowed(LSLASTNode* c)
        {
            LSLASTNode* node   = c->getParent();
            LSLASTNode* parent = node ? node->getParent() : nullptr;
            // Up through parentheses and a leading minus.
            while (parent && parent->getNodeType() == NODE_EXPRESSION &&
                   (parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION ||
                    (parent->getNodeSubType() == NODE_UNARY_EXPRESSION && static_cast<LSLExpression*>(parent)->getOperation() == OP_MINUS)))
            {
                node   = parent;
                parent = parent->getParent();
            }
            if (!parent || !node)
            {
                return false;
            }
            const int slot = node->getParentSlot();
            switch (parent->getNodeType())
            {
                case NODE_GLOBAL_VARIABLE:
                    return slot == 1 && parent->getChild(0)->getIType() == LST_FLOATINGPOINT;
                case NODE_STATEMENT:
                    if (parent->getNodeSubType() == NODE_DECLARATION)
                    {
                        return slot == 1 && parent->getChild(0)->getIType() == LST_FLOATINGPOINT;
                    }
                    if (parent->getNodeSubType() == NODE_RETURN_STATEMENT)
                    {
                        for (LSLASTNode* up = parent; up; up = up->getParent())
                        {
                            if (up->getNodeType() == NODE_GLOBAL_FUNCTION)
                            {
                                return up->getChild(0)->getIType() == LST_FLOATINGPOINT;
                            }
                        }
                    }
                    return false;
                case NODE_AST_NODE_LIST:
                {
                    // An argument: the parameter's type decides.
                    LSLASTNode* call = parent->getParent();
                    if (!call || call->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                    {
                        return false;
                    }
                    LSLSymbol* sym = call->getSymbol();
                    if (!sym || !sym->getFunctionDecl())
                    {
                        return false;
                    }
                    LSLASTNode* param = sym->getFunctionDecl()->getChild(slot);
                    return param && param->getIType() == LST_FLOATINGPOINT;
                }
                case NODE_EXPRESSION:
                    break;
                default:
                    return false;
            }
            auto* expr = static_cast<LSLExpression*>(parent);
            switch (expr->getNodeSubType())
            {
                case NODE_VECTOR_EXPRESSION:
                case NODE_QUATERNION_EXPRESSION:
                    return true;
                case NODE_BINARY_EXPRESSION:
                {
                    const LSLOperator op = expr->getOperation();
                    LSLASTNode*       other = expr->getChild(slot == 0 ? 1 : 0);
                    if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN)
                    {
                        return slot == 1 && expr->getChild(0)->getIType() == LST_FLOATINGPOINT;
                    }
                    if (op == OP_MOD || op == OP_MOD_ASSIGN)
                    {
                        return false;
                    }
                    // The other operand keeps the operation in floats.
                    return other && (other->getIType() == LST_FLOATINGPOINT || other->getIType() == LST_VECTOR || other->getIType() == LST_QUATERNION);
                }
                default:
                    return false;
            }
        }

        const ALLSLOptimizer::Options& mOptions;
        std::vector<Mark>              mMarks;
    };
}

    Printed print(LSLScript* script, const ALLSLOptimizer::Options& options)
    {
        PrettyPrintOpts opts{};
        opts.mangle_local_names  = options.shrinknames;
        opts.mangle_func_names   = options.shrinknames;
        opts.mangle_global_names = options.shrinknames;
        opts.show_unmangled      = false;
        Printer printer(opts, options);
        script->visit(&printer);
        Printed printed;
        printed.text = printer.mStream.str();
        printed.map  = printer.map(std::string());
        return printed;
    }
}

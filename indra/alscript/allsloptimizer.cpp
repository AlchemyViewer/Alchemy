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

#include "allslcosts.h"
#include "allsleffects.h"
#include "allslinliner.h"
#include "allslservice.h"
#include "allsltraits.h"
#include "allslvalues.h"
#include "llmath.h"
#include "llmd5.h"
#include "llstl.h"
#include "llquaternion.h"
#include "v3math.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>
#include <tailslide/passes/pretty_print.hh>
#include <tailslide/passes/values.hh>
#include <tailslide/visitor.hh>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>

namespace
{
    using namespace Tailslide;

    // ---- what the definitions say of the library -------------------------------------

    bool isPure(const char* name) { return ALLSLTraits::pure(name); }

    S32 zeroBased(int one) { return std::max(0, one - 1); }

    // ---- numbers as the printer writes them ----------------------------------------------

    using ALLSLValues::floatText;
    using ALLSLValues::integral;

    bool ascii(const char* s)
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

    std::string render(LSLASTNode* node)
    {
        PrettyPrintOpts    opts{};
        PrettyPrintVisitor printer(opts);
        node->visit(&printer);
        std::string text = printer.mStream.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        {
            text.pop_back();
        }
        // A statement's rendering starts with its indentation.
        const size_t start = text.find_first_not_of(" \t");
        return start == std::string::npos ? text : text.substr(start);
    }

    bool sideEffectFree(LSLASTNode* node) { return ALLSLTraits::sideEffectFree(node); }
    // What may be dropped, though not moved: a read of the world or the
    // clock changes nothing.
    bool changesNothing(LSLASTNode* node) { return ALLSLTraits::changesNothing(node); }

    // Skips the parentheses around an expression.
    LSLExpression* bare(LSLExpression* expr)
    {
        while (expr && expr->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            expr = static_cast<LSLParenthesisExpression*>(expr)->getChildExpr();
        }
        return expr;
    }

    bool isInteger(LSLASTNode* node, int value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(cv)->getValue() == value;
    }

    bool isFloat(LSLASTNode* node, double value)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_FLOAT_CONSTANT && static_cast<LSLFloatConstant*>(cv)->getValue() == value;
    }

    bool isEmptyString(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_STRING_CONSTANT && !*static_cast<LSLStringConstant*>(cv)->getValue();
    }

    bool isEmptyList(LSLASTNode* node)
    {
        LSLConstant* cv = node ? node->getConstantValue() : nullptr;
        return cv && cv->getNodeSubType() == NODE_LIST_CONSTANT && static_cast<LSLListConstant*>(cv)->getLength() == 0;
    }

    // Whether the parent of an expression is another expression that could
    // bind a replacement differently: parentheses go around it then. The
    // right side of an assignment binds nothing.
    bool wantsParens(LSLASTNode* parent, LSLASTNode* old)
    {
        if (!parent || parent->getNodeType() != NODE_EXPRESSION)
        {
            return false;
        }
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                if (operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(1) == old)
                {
                    return false;
                }
                return true;
            // A cast's own printing puts its operand in parentheses where
            // it needs them, and around parentheses too.
            case NODE_TYPECAST_EXPRESSION:
                return false;
            case NODE_UNARY_EXPRESSION:
            case NODE_PRINT_EXPRESSION:
            case NODE_BOOL_CONVERSION_EXPRESSION:
                return true;
            default:
                return false;
        }
    }

    // Puts `replacement` where `old` stands, in parentheses if the place
    // could bind it differently.
    void putInPlace(LSLASTNode* old, LSLExpression* replacement, ScriptAllocator* allocator)
    {
        LSLASTNode* parent = old->getParent();
        replacement->setLoc(old->getLoc());
        if (wantsParens(parent, old) && replacement->getNodeSubType() != NODE_CONSTANT_EXPRESSION &&
            replacement->getNodeSubType() != NODE_LVALUE_EXPRESSION && replacement->getNodeSubType() != NODE_PARENTHESIS_EXPRESSION &&
            replacement->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            auto* parens = allocator->newTracked<LSLParenthesisExpression>(replacement);
            parens->setType(replacement->getType());
            parens->setLoc(old->getLoc());
            LSLASTNode::replaceNode(old, parens);
            return;
        }
        LSLASTNode::replaceNode(old, replacement);
    }

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

    // ---- the library, evaluated -----------------------------------------------------------------

    struct Ctx
    {
        ScriptAllocator*       allocator = nullptr;
        ScriptContext*         context   = nullptr;
        ALLSLOptimizer::Target target    = ALLSLOptimizer::Target::Mono;
        bool                   foldtabs  = false;
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

    typedef std::vector<LSLConstant*> Args;

    bool argInt(const Args& a, size_t i, int& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_INTEGER_CONSTANT)
        {
            return false;
        }
        out = static_cast<LSLIntegerConstant*>(a[i])->getValue();
        return true;
    }

    // A float argument, or an integer where LSL would promote one.
    bool argFloat(const Args& a, size_t i, double& out)
    {
        if (i >= a.size())
        {
            return false;
        }
        if (a[i]->getNodeSubType() == NODE_FLOAT_CONSTANT)
        {
            out = static_cast<double>(static_cast<float>(static_cast<LSLFloatConstant*>(a[i])->getValue()));
            return true;
        }
        if (a[i]->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            out = static_cast<double>(static_cast<float>(static_cast<LSLIntegerConstant*>(a[i])->getValue()));
            return true;
        }
        return false;
    }

    // A string argument, plain ASCII, so that no VM's idea of a character
    // differs from ours.
    bool argString(const Args& a, size_t i, std::string& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_STRING_CONSTANT)
        {
            return false;
        }
        const char* s = static_cast<LSLStringConstant*>(a[i])->getValue();
        if (!ascii(s))
        {
            return false;
        }
        out = s;
        return true;
    }

    bool argList(const Args& a, size_t i, LSLListConstant*& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_LIST_CONSTANT)
        {
            return false;
        }
        out = static_cast<LSLListConstant*>(a[i]);
        return true;
    }

    bool argVector(const Args& a, size_t i, Vector3& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_VECTOR_CONSTANT)
        {
            return false;
        }
        out = *static_cast<LSLVectorConstant*>(a[i])->getValue();
        return true;
    }

    // A rotation argument that is a unit quaternion, near enough that the
    // VM's normalising of it changes nothing: what the rotation functions
    // are folded over, since what a VM does with the rest is its own.
    bool argUnitRotation(const Args& a, size_t i, LLQuaternion& out)
    {
        if (i >= a.size() || a[i]->getNodeSubType() != NODE_QUATERNION_CONSTANT)
        {
            return false;
        }
        const Quaternion* q   = static_cast<LSLQuaternionConstant*>(a[i])->getValue();
        const float       mag = std::sqrt(q->x * q->x + q->y * q->y + q->z * q->z + q->s * q->s);
        if (!std::isfinite(mag) || std::fabs(mag - 1.0f) >= 1e-6f)
        {
            return false;
        }
        // set() normalises, which changes nothing of a unit rotation but
        // the last bit; the components are taken as they are.
        out.mQ[VX] = q->x;
        out.mQ[VY] = q->y;
        out.mQ[VZ] = q->z;
        out.mQ[VW] = q->s;
        return true;
    }

    std::vector<LSLConstant*> elements(LSLListConstant* list)
    {
        std::vector<LSLConstant*> out;
        for (LSLASTNode* child : *list)
        {
            out.push_back(static_cast<LSLConstant*>(child));
        }
        return out;
    }

    LSLConstant* listOf(Ctx& ctx, const std::vector<LSLConstant*>& items)
    {
        auto* list = ctx.allocator->newTracked<LSLListConstant>(nullptr);
        for (LSLConstant* item : items)
        {
            list->pushChild(item->copy(ctx.allocator));
        }
        return list;
    }

    // LSL's indices: negative from the end. `start > end` names the
    // outside of the range, for a get, and the inside for a delete.
    void normalise(int length, int& start, int& end)
    {
        if (start < 0)
        {
            start += length;
        }
        if (end < 0)
        {
            end += length;
        }
    }

    template <class T> std::vector<T> subRange(const std::vector<T>& v, int start, int end)
    {
        const int length = static_cast<int>(v.size());
        normalise(length, start, end);
        std::vector<T> out;
        if (start <= end)
        {
            for (int i = std::max(0, start); i <= std::min(length - 1, end); ++i)
            {
                out.push_back(v[i]);
            }
            return out;
        }
        for (int i = 0; i <= std::min(length - 1, end); ++i)
        {
            out.push_back(v[i]);
        }
        for (int i = std::max(0, start); i < length; ++i)
        {
            out.push_back(v[i]);
        }
        return out;
    }

    template <class T> std::vector<T> deleteRange(const std::vector<T>& v, int start, int end)
    {
        const int length = static_cast<int>(v.size());
        normalise(length, start, end);
        std::vector<T> out;
        for (int i = 0; i < length; ++i)
        {
            const bool inside = start <= end ? (i >= start && i <= end) : (i <= end || i >= start);
            if (!inside)
            {
                out.push_back(v[i]);
            }
        }
        return out;
    }

    std::vector<char> chars(const std::string& s) { return std::vector<char>(s.begin(), s.end()); }
    std::string       fromChars(const std::vector<char>& v) { return std::string(v.begin(), v.end()); }

    // What (string) makes of a float: six places, as both VMs write it.
    std::string floatString(double v)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(static_cast<float>(v)));
        return buffer;
    }

    // An element as llList2String and llList2CSV write it, or nothing for
    // what neither should fold.
    bool elementText(LSLConstant* c, std::string& out)
    {
        switch (c->getNodeSubType())
        {
            case NODE_INTEGER_CONSTANT:
                out = std::to_string(static_cast<LSLIntegerConstant*>(c)->getValue());
                return true;
            case NODE_FLOAT_CONSTANT:
                out = floatString(static_cast<LSLFloatConstant*>(c)->getValue());
                return true;
            case NODE_STRING_CONSTANT:
            case NODE_KEY_CONSTANT:
                out = static_cast<LSLStringConstant*>(c)->getValue();
                return ascii(out.c_str());
            case NODE_VECTOR_CONSTANT:
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(c)->getValue();
                out              = "<" + floatString(v->x) + ", " + floatString(v->y) + ", " + floatString(v->z) + ">";
                return true;
            }
            case NODE_QUATERNION_CONSTANT:
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(c)->getValue();
                out = "<" + floatString(q->x) + ", " + floatString(q->y) + ", " + floatString(q->z) + ", " + floatString(q->s) + ">";
                return true;
            }
            default:
                return false;
        }
    }

    const char* const BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string toBase64(const std::string& in)
    {
        std::string out;
        size_t      i = 0;
        while (i + 2 < in.size())
        {
            const uint32_t n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += BASE64[(n >> 6) & 63];
            out += BASE64[n & 63];
            i += 3;
        }
        if (i + 1 == in.size())
        {
            const uint32_t n = static_cast<unsigned char>(in[i]) << 16;
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += "==";
        }
        else if (i + 2 == in.size())
        {
            const uint32_t n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
            out += BASE64[(n >> 18) & 63];
            out += BASE64[(n >> 12) & 63];
            out += BASE64[(n >> 6) & 63];
            out += '=';
        }
        return out;
    }

    // Strictly formed input only; anything else is the VM's to answer.
    bool fromBase64(const std::string& in, std::string& out)
    {
        if (in.size() % 4 != 0)
        {
            return false;
        }
        out.clear();
        for (size_t i = 0; i < in.size(); i += 4)
        {
            uint32_t n   = 0;
            int      pad = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char c = in[i + k];
                if (c == '=')
                {
                    if (i + 4 != in.size() || (k < 2))
                    {
                        return false;
                    }
                    ++pad;
                    n <<= 6;
                    continue;
                }
                if (pad)
                {
                    return false;
                }
                const char* p = strchr(BASE64, c);
                if (!p || !c)
                {
                    return false;
                }
                n = (n << 6) | static_cast<uint32_t>(p - BASE64);
            }
            out += static_cast<char>((n >> 16) & 255);
            if (pad < 2)
            {
                out += static_cast<char>((n >> 8) & 255);
            }
            if (pad < 1)
            {
                out += static_cast<char>(n & 255);
            }
        }
        return true;
    }

    // A strict reader of JSON, for llJsonGetValue: only what RFC 8259
    // allows, in ASCII, so that what is folded is what every reader
    // agrees on; anything the simulator's own reader might take another
    // way -- duplicate keys, numbers with a fraction or an exponent,
    // escapes outside ASCII, and the JSON_* answers, which are
    // characters no string literal here may hold -- is left to it.
    struct JsonValue
    {
        enum class Kind : U8
        {
            Null,
            True,
            False,
            Number,
            String,
            Array,
            Object
        };
        Kind                                           kind = Kind::Null;
        std::string                                    text;  // a number as written, a string unescaped
        std::vector<JsonValue>                         items;
        std::vector<std::pair<std::string, JsonValue>> fields;
    };

    struct JsonReader
    {
        const std::string& in;
        size_t             at = 0;
        bool               ok = true;

        explicit JsonReader(const std::string& text) : in(text) {}

        void space()
        {
            while (at < in.size() && (in[at] == ' ' || in[at] == '\t' || in[at] == '\n' || in[at] == '\r'))
            {
                ++at;
            }
        }
        bool take(char c)
        {
            if (at < in.size() && in[at] == c)
            {
                ++at;
                return true;
            }
            return false;
        }
        bool word(const char* w)
        {
            const size_t n = strlen(w);
            if (in.compare(at, n, w) == 0)
            {
                at += n;
                return true;
            }
            return false;
        }
        bool string(std::string& out)
        {
            if (!take('"'))
            {
                return false;
            }
            out.clear();
            while (at < in.size())
            {
                const char c = in[at++];
                if (c == '"')
                {
                    return true;
                }
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    return false;
                }
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (at >= in.size())
                {
                    return false;
                }
                const char e = in[at++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        if (at + 4 > in.size())
                        {
                            return false;
                        }
                        unsigned code = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            const char h = in[at++];
                            if (!isxdigit(static_cast<unsigned char>(h)))
                            {
                                return false;
                            }
                            code = code * 16 + static_cast<unsigned>(isdigit(static_cast<unsigned char>(h)) ? h - '0' : tolower(h) - 'a' + 10);
                        }
                        if (code == 0 || code > 0x7F)
                        {
                            return false;
                        }
                        out += static_cast<char>(code);
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }
        bool value(JsonValue& out)
        {
            space();
            if (at >= in.size())
            {
                return false;
            }
            const char c = in[at];
            if (c == '{')
            {
                ++at;
                out.kind = JsonValue::Kind::Object;
                space();
                if (take('}'))
                {
                    return true;
                }
                while (true)
                {
                    space();
                    std::string key;
                    if (!string(key))
                    {
                        return false;
                    }
                    for (const auto& field : out.fields)
                    {
                        if (field.first == key)
                        {
                            return false;
                        }
                    }
                    space();
                    if (!take(':'))
                    {
                        return false;
                    }
                    JsonValue v;
                    if (!value(v))
                    {
                        return false;
                    }
                    out.fields.emplace_back(std::move(key), std::move(v));
                    space();
                    if (take(','))
                    {
                        continue;
                    }
                    return take('}');
                }
            }
            if (c == '[')
            {
                ++at;
                out.kind = JsonValue::Kind::Array;
                space();
                if (take(']'))
                {
                    return true;
                }
                while (true)
                {
                    JsonValue v;
                    if (!value(v))
                    {
                        return false;
                    }
                    out.items.push_back(std::move(v));
                    space();
                    if (take(','))
                    {
                        continue;
                    }
                    return take(']');
                }
            }
            if (c == '"')
            {
                out.kind = JsonValue::Kind::String;
                return string(out.text);
            }
            if (word("true"))
            {
                out.kind = JsonValue::Kind::True;
                return true;
            }
            if (word("false"))
            {
                out.kind = JsonValue::Kind::False;
                return true;
            }
            if (word("null"))
            {
                out.kind = JsonValue::Kind::Null;
                return true;
            }
            // A number: only a plain integer is taken; a fraction or an
            // exponent is left to the simulator's own formatting.
            const size_t start = at;
            take('-');
            if (at < in.size() && in[at] == '0')
            {
                ++at;
            }
            else if (at < in.size() && in[at] >= '1' && in[at] <= '9')
            {
                while (at < in.size() && isdigit(static_cast<unsigned char>(in[at])))
                {
                    ++at;
                }
            }
            else
            {
                return false;
            }
            if (at < in.size() && (in[at] == '.' || in[at] == 'e' || in[at] == 'E'))
            {
                ok = false;
                return false;
            }
            out.kind = JsonValue::Kind::Number;
            out.text = in.substr(start, at - start);
            return at > start + (in[start] == '-' ? 1 : 0);
        }
        bool whole(JsonValue& out)
        {
            if (!value(out))
            {
                return false;
            }
            space();
            return at == in.size();
        }
    };

    // The value a path of keys and indexes reaches, or null.
    const JsonValue* jsonAt(const JsonValue& root, const std::vector<LSLConstant*>& path)
    {
        const JsonValue* at = &root;
        for (LSLConstant* step : path)
        {
            if (step->getNodeSubType() == NODE_STRING_CONSTANT && at->kind == JsonValue::Kind::Object)
            {
                const char*      key   = static_cast<LSLStringConstant*>(step)->getValue();
                const JsonValue* found = nullptr;
                for (const auto& field : at->fields)
                {
                    if (field.first == key)
                    {
                        found = &field.second;
                    }
                }
                if (!found)
                {
                    return nullptr;
                }
                at = found;
            }
            else if (step->getNodeSubType() == NODE_INTEGER_CONSTANT && at->kind == JsonValue::Kind::Array)
            {
                const int index = static_cast<LSLIntegerConstant*>(step)->getValue();
                if (index < 0 || static_cast<size_t>(index) >= at->items.size())
                {
                    return nullptr;
                }
                at = &at->items[static_cast<size_t>(index)];
            }
            else
            {
                return nullptr;
            }
        }
        return at;
    }

    // A string that reads as a string to every JSON writer and reader:
    // starts with a letter, holds letters, digits, spaces and plain
    // punctuation, and is not a word JSON has a meaning for.
    bool plainJsonString(const std::string& s)
    {
        if (s.empty() || !isalpha(static_cast<unsigned char>(s[0])))
        {
            return false;
        }
        for (const char ch : s)
        {
            if (!(isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '_' || ch == '-' || ch == '.' || ch == ',' || ch == ':' || ch == ';' ||
                  ch == '!' || ch == '?' || ch == '\'' || ch == '(' || ch == ')' || ch == '#' || ch == '@' || ch == '%' || ch == '&' || ch == '*' ||
                  ch == '+' || ch == '=' || ch == '/' || ch == '|' || ch == '~' || ch == '^' || ch == '$'))
            {
                return false;
            }
        }
        return s != "true" && s != "false" && s != "null";
    }

    // Whether a value is one every writer spells alike: integers, plain
    // strings, true, false and null, and arrays and objects of those.
    bool jsonPlain(const JsonValue& v)
    {
        switch (v.kind)
        {
            case JsonValue::Kind::Number: return v.text != "-0" && v.text.size() <= 10;
            case JsonValue::Kind::String: return plainJsonString(v.text);
            case JsonValue::Kind::Array:
                for (const JsonValue& item : v.items)
                {
                    if (!jsonPlain(item)) return false;
                }
                return true;
            case JsonValue::Kind::Object:
                for (const auto& field : v.fields)
                {
                    if (!plainJsonString(field.first) || !jsonPlain(field.second)) return false;
                }
                return true;
            default: return true;
        }
    }

    // A plain value written the compact way, which is the simulator's.
    std::string jsonWrite(const JsonValue& v)
    {
        switch (v.kind)
        {
            case JsonValue::Kind::Null:   return "null";
            case JsonValue::Kind::True:   return "true";
            case JsonValue::Kind::False:  return "false";
            case JsonValue::Kind::Number: return v.text;
            case JsonValue::Kind::String: return "\"" + v.text + "\"";
            case JsonValue::Kind::Array:
            {
                std::string out = "[";
                for (size_t i = 0; i < v.items.size(); ++i)
                {
                    out += (i ? "," : "") + jsonWrite(v.items[i]);
                }
                return out + "]";
            }
            case JsonValue::Kind::Object:
            {
                std::string out = "{";
                for (size_t i = 0; i < v.fields.size(); ++i)
                {
                    out += (i ? "," : "") + ("\"" + v.fields[i].first + "\":") + jsonWrite(v.fields[i].second);
                }
                return out + "}";
            }
        }
        return std::string();
    }

    typedef std::function<LSLConstant*(Ctx&, const Args&)> Evaluator;

    const boost::unordered_flat_map<std::string, Evaluator, ll::string_hash, std::equal_to<>>& evaluators()
    {
        static const boost::unordered_flat_map<std::string, Evaluator, ll::string_hash, std::equal_to<>> table = {
            { "llAbs",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  return c.integer(v < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(v)) : v);
              } },
            { "llFabs",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::fabs(v)) : nullptr;
              } },
            { "llFloor",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v)) return nullptr;
                  const double r = std::floor(v);
                  return (r >= -2147483648.0 && r < 2147483648.0) ? c.integer(static_cast<int>(r)) : nullptr;
              } },
            { "llCeil",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v)) return nullptr;
                  const double r = std::ceil(v);
                  return (r >= -2147483648.0 && r < 2147483648.0) ? c.integer(static_cast<int>(r)) : nullptr;
              } },
            { "llSqrt",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v >= 0.0 ? c.number(std::sqrt(v)) : nullptr;
              } },
            { "llPow",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double b, e;
                  return argFloat(a, 0, b) && argFloat(a, 1, e) ? c.number(std::pow(b, e)) : nullptr;
              } },
            { "llSin",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::sin(v)) : nullptr;
              } },
            { "llCos",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::cos(v)) : nullptr;
              } },
            { "llTan",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) ? c.number(std::tan(v)) : nullptr;
              } },
            { "llAtan2",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double y, x;
                  return argFloat(a, 0, y) && argFloat(a, 1, x) ? c.number(std::atan2(y, x)) : nullptr;
              } },
            { "llLog",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v > 0.0 ? c.number(std::log(v)) : nullptr;
              } },
            { "llLog10",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && v > 0.0 ? c.number(std::log10(v)) : nullptr;
              } },
            { "llModPow",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int base, exponent, modulus;
                  if (!argInt(a, 0, base) || !argInt(a, 1, exponent) || !argInt(a, 2, modulus) || base < 0 || exponent < 0 || modulus <= 0)
                  {
                      return nullptr;
                  }
                  uint64_t result = 1 % static_cast<uint64_t>(modulus);
                  uint64_t b      = static_cast<uint64_t>(base) % static_cast<uint64_t>(modulus);
                  for (uint32_t e = static_cast<uint32_t>(exponent); e; e >>= 1)
                  {
                      if (e & 1)
                      {
                          result = result * b % static_cast<uint64_t>(modulus);
                      }
                      b = b * b % static_cast<uint64_t>(modulus);
                  }
                  return c.integer(static_cast<int>(result));
              } },
            { "llVecMag",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  return argVector(a, 0, v) ? c.number(std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z)) : nullptr;
              } },
            { "llVecDist",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 p, q;
                  if (!argVector(a, 0, p) || !argVector(a, 1, q)) return nullptr;
                  const float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
                  return c.number(std::sqrt(double(dx) * dx + double(dy) * dy + double(dz) * dz));
              } },
            { "llVecNorm",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  if (!argVector(a, 0, v)) return nullptr;
                  const float mag = static_cast<float>(std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
                  if (mag == 0.0f) return c.vector(0, 0, 0);
                  return c.vector(v.x / mag, v.y / mag, v.z / mag);
              } },
            // The rotations, by the viewer's own quaternion, which is the
            // lineage of the simulator's: Euler angles through the
            // matrix, axis and angle, and the axes of a rotation as the
            // unit vectors turned by it. Only unit rotations are folded.
            { "llEuler2Rot",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  if (!argVector(a, 0, v)) return nullptr;
                  LLQuaternion q;
                  q.setEulerAngles(v.x, v.y, v.z);
                  return c.rotation(q.mQ[VX], q.mQ[VY], q.mQ[VZ], q.mQ[VW]);
              } },
            { "llRot2Euler",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  F32 roll, pitch, yaw;
                  q.getEulerAngles(&roll, &pitch, &yaw);
                  return c.vector(roll, pitch, yaw);
              } },
            { "llAxisAngle2Rot",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 axis;
                  double  angle;
                  if (!argVector(a, 0, axis) || !argFloat(a, 1, angle)) return nullptr;
                  const LLQuaternion q(static_cast<F32>(angle), LLVector3(axis.x, axis.y, axis.z));
                  return c.rotation(q.mQ[VX], q.mQ[VY], q.mQ[VZ], q.mQ[VW]);
              } },
            { "llRot2Axis",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  F32       angle;
                  LLVector3 axis;
                  q.getAngleAxis(&angle, axis);
                  // No rotation has no axis to speak of: the VM's to say.
                  if (angle == 0.0f) return nullptr;
                  return c.vector(axis.mV[VX], axis.mV[VY], axis.mV[VZ]);
              } },
            { "llRot2Angle",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  F32       angle;
                  LLVector3 axis;
                  q.getAngleAxis(&angle, axis);
                  return c.number(angle);
              } },
            { "llRot2Fwd",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  const LLVector3 v = LLVector3(1.f, 0.f, 0.f) * q;
                  return c.vector(v.mV[VX], v.mV[VY], v.mV[VZ]);
              } },
            { "llRot2Left",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  const LLVector3 v = LLVector3(0.f, 1.f, 0.f) * q;
                  return c.vector(v.mV[VX], v.mV[VY], v.mV[VZ]);
              } },
            { "llRot2Up",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LLQuaternion q;
                  if (!argUnitRotation(a, 0, q)) return nullptr;
                  const LLVector3 v = LLVector3(0.f, 0.f, 1.f) * q;
                  return c.vector(v.mV[VX], v.mV[VY], v.mV[VZ]);
              } },
            { "llStringLength",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  return argString(a, 0, s) ? c.integer(static_cast<int>(s.size())) : nullptr;
              } },
            { "llGetSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         start, end;
                  if (!argString(a, 0, s) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return c.string(fromChars(subRange(chars(s), start, end)));
              } },
            { "llDeleteSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         start, end;
                  if (!argString(a, 0, s) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return c.string(fromChars(deleteRange(chars(s), start, end)));
              } },
            { "llInsertString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, ins;
                  int         at;
                  if (!argString(a, 0, s) || !argInt(a, 1, at) || !argString(a, 2, ins) || at < 0) return nullptr;
                  const size_t pos = std::min(s.size(), static_cast<size_t>(at));
                  return c.string(s.substr(0, pos) + ins + s.substr(pos));
              } },
            { "llSubStringIndex",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, p;
                  if (!argString(a, 0, s) || !argString(a, 1, p)) return nullptr;
                  const size_t at = s.find(p);
                  return c.integer(at == std::string::npos ? -1 : static_cast<int>(at));
              } },
            { "llToUpper",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  for (char& ch : s) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                  return c.string(s);
              } },
            { "llToLower",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  for (char& ch : s) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
                  return c.string(s);
              } },
            { "llStringTrim",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         how;
                  if (!argString(a, 0, s) || !argInt(a, 1, how)) return nullptr;
                  const auto blank = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
                  size_t     from  = 0;
                  size_t     to    = s.size();
                  if (how & 1)
                  {
                      while (from < to && blank(s[from])) ++from;
                  }
                  if (how & 2)
                  {
                      while (to > from && blank(s[to - 1])) --to;
                  }
                  return c.string(s.substr(from, to - from));
              } },
            { "llEscapeURL",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  std::string out;
                  for (char ch : s)
                  {
                      if (isalnum(static_cast<unsigned char>(ch)))
                      {
                          out += ch;
                      }
                      else
                      {
                          char buffer[8];
                          snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(ch));
                          out += buffer;
                      }
                  }
                  return c.string(out);
              } },
            { "llUnescapeURL",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  if (!argString(a, 0, s)) return nullptr;
                  std::string out;
                  for (size_t i = 0; i < s.size(); ++i)
                  {
                      if (s[i] != '%')
                      {
                          out += s[i];
                          continue;
                      }
                      if (i + 2 >= s.size() || !isxdigit(static_cast<unsigned char>(s[i + 1])) || !isxdigit(static_cast<unsigned char>(s[i + 2])))
                      {
                          return nullptr;
                      }
                      const int byte = static_cast<int>(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                      if (byte == 0 || byte >= 0x80)
                      {
                          return nullptr;
                      }
                      out += static_cast<char>(byte);
                      i += 2;
                  }
                  return c.string(out);
              } },
            { "llStringToBase64",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  return argString(a, 0, s) ? c.string(toBase64(s)) : nullptr;
              } },
            { "llBase64ToString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, out;
                  if (!argString(a, 0, s) || !fromBase64(s, out) || out.find('\0') != std::string::npos) return nullptr;
                  return c.string(out);
              } },
            { "llIntegerToBase64",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  const uint32_t u = static_cast<uint32_t>(v);
                  std::string    bytes{ static_cast<char>(u >> 24), static_cast<char>(u >> 16), static_cast<char>(u >> 8), static_cast<char>(u) };
                  return c.string(toBase64(bytes));
              } },
            { "llBase64ToInteger",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, out;
                  if (!argString(a, 0, s) || s.size() != 8 || !fromBase64(s, out) || out.size() != 4) return nullptr;
                  const uint32_t u = (static_cast<unsigned char>(out[0]) << 24) | (static_cast<unsigned char>(out[1]) << 16) |
                                     (static_cast<unsigned char>(out[2]) << 8) | static_cast<unsigned char>(out[3]);
                  return c.integer(static_cast<int>(u));
              } },
            { "llChar",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  int v;
                  if (!argInt(a, 0, v)) return nullptr;
                  if (v == 0) return c.string(std::string());
                  return (v > 0 && v < 128) ? c.string(std::string(1, static_cast<char>(v))) : nullptr;
              } },
            { "llOrd",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s;
                  int         at;
                  if (!argString(a, 0, s) || !argInt(a, 1, at)) return nullptr;
                  const int length = static_cast<int>(s.size());
                  if (at < 0) at += length;
                  return (at >= 0 && at < length) ? c.integer(static_cast<unsigned char>(s[at])) : c.integer(0);
              } },
            { "llReplaceSubString",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string s, pattern, with;
                  int         count;
                  if (!argString(a, 0, s) || !argString(a, 1, pattern) || !argString(a, 2, with) || !argInt(a, 3, count) || pattern.empty() || count < 0)
                  {
                      return nullptr;
                  }
                  std::string out;
                  size_t      from = 0;
                  int         done = 0;
                  while (true)
                  {
                      const size_t at = s.find(pattern, from);
                      if (at == std::string::npos || (count > 0 && done == count))
                      {
                          out += s.substr(from);
                          break;
                      }
                      out += s.substr(from, at - from) + with;
                      from = at + pattern.size();
                      ++done;
                  }
                  return c.string(out);
              } },
            { "llGetListLength",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  return argList(a, 0, l) ? c.integer(static_cast<int>(l->getLength())) : nullptr;
              } },
            { "llList2List",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              start, end;
                  if (!argList(a, 0, l) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return listOf(c, subRange(elements(l), start, end));
              } },
            { "llDeleteSubList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              start, end;
                  if (!argList(a, 0, l) || !argInt(a, 1, start) || !argInt(a, 2, end)) return nullptr;
                  return listOf(c, deleteRange(elements(l), start, end));
              } },
            { "llListInsertList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant *l, *ins;
                  int              at;
                  if (!argList(a, 0, l) || !argList(a, 1, ins) || !argInt(a, 2, at)) return nullptr;
                  std::vector<LSLConstant*> items = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  if (at < 0) at += length;
                  if (at < 0) return nullptr;
                  const size_t              pos = std::min(items.size(), static_cast<size_t>(at));
                  std::vector<LSLConstant*> more = elements(ins);
                  items.insert(items.begin() + pos, more.begin(), more.end());
                  return listOf(c, items);
              } },
            { "llListReplaceList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant *l, *with;
                  int              start, end;
                  if (!argList(a, 0, l) || !argList(a, 1, with) || !argInt(a, 2, start) || !argInt(a, 3, end)) return nullptr;
                  std::vector<LSLConstant*> items  = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  normalise(length, start, end);
                  if (start > end || start < 0 || start > length) return nullptr;
                  std::vector<LSLConstant*> out(items.begin(), items.begin() + std::min(length, start));
                  std::vector<LSLConstant*> more = elements(with);
                  out.insert(out.end(), more.begin(), more.end());
                  if (end + 1 < length)
                  {
                      out.insert(out.end(), items.begin() + end + 1, items.end());
                  }
                  return listOf(c, out);
              } },
            { "llGetListEntryType",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              at;
                  if (!argList(a, 0, l) || !argInt(a, 1, at)) return nullptr;
                  std::vector<LSLConstant*> items  = elements(l);
                  const int                 length = static_cast<int>(items.size());
                  if (at < 0) at += length;
                  if (at < 0 || at >= length) return c.integer(0);
                  switch (items[at]->getNodeSubType())
                  {
                      case NODE_INTEGER_CONSTANT: return c.integer(1);
                      case NODE_FLOAT_CONSTANT: return c.integer(2);
                      case NODE_STRING_CONSTANT: return c.integer(3);
                      case NODE_KEY_CONSTANT: return c.integer(4);
                      case NODE_VECTOR_CONSTANT: return c.integer(5);
                      case NODE_QUATERNION_CONSTANT: return c.integer(6);
                      default: return nullptr;
                  }
              } },
            { "llJsonGetValue",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string      json;
                  LSLListConstant* path;
                  if (!argString(a, 0, json) || !argList(a, 1, path)) return nullptr;
                  JsonValue  root;
                  JsonReader reader(json);
                  if (!reader.whole(root)) return nullptr;
                  const JsonValue* at = jsonAt(root, elements(path));
                  if (!at) return c.builtin("JSON_INVALID");
                  // A string or a plain number: what is written is what
                  // is answered; true, false and null are their constants.
                  // A nested object or array the simulator spells its own
                  // way.
                  switch (at->kind)
                  {
                      case JsonValue::Kind::String:
                      case JsonValue::Kind::Number: return c.string(at->text);
                      case JsonValue::Kind::True:   return c.builtin("JSON_TRUE");
                      case JsonValue::Kind::False:  return c.builtin("JSON_FALSE");
                      case JsonValue::Kind::Null:   return c.builtin("JSON_NULL");
                      default:
                          // A nested value as the simulator writes it, where
                          // every writer would write it alike.
                          return jsonPlain(*at) ? c.string(jsonWrite(*at)) : nullptr;
                  }
              } },
            { "llJsonSetValue",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  // Over a plain document, down a path that is there but
                  // for its last step, which may be a new key or the next
                  // index; the value a plain string, an integer, or true,
                  // false or null by its constant. Anything the simulator
                  // might make more of -- a path to create, a deletion, a
                  // value that reads as JSON -- is left to it.
                  std::string      json;
                  LSLListConstant* path;
                  if (!argString(a, 0, json) || !argList(a, 1, path) || a.size() < 3 || a[2]->getNodeSubType() != NODE_STRING_CONSTANT) return nullptr;
                  JsonValue  root;
                  JsonReader reader(json);
                  if (!reader.whole(root) || !jsonPlain(root)) return nullptr;
                  const std::string value = static_cast<LSLStringConstant*>(a[2])->getValue();
                  JsonValue         put;
                  auto              same = [&](const char* name) {
                      LSLConstant* builtin = c.builtin(name);
                      return builtin && value == static_cast<LSLStringConstant*>(builtin)->getValue();
                  };
                  // JSON_DELETE takes out what is there: a key from an
                  // object, an index from an array; what is not there the
                  // simulator answers for.
                  const bool deleting = same("JSON_DELETE");
                  if (same("JSON_TRUE")) put.kind = JsonValue::Kind::True;
                  else if (same("JSON_FALSE")) put.kind = JsonValue::Kind::False;
                  else if (same("JSON_NULL")) put.kind = JsonValue::Kind::Null;
                  else if (deleting)
                  {
                  }
                  else if (plainJsonString(value))
                  {
                      put.kind = JsonValue::Kind::String;
                      put.text = value;
                  }
                  else
                  {
                      // An integer, as JSON writes one.
                      size_t i = value.size() > 1 && value[0] == '-' ? 1 : 0;
                      if (i >= value.size() || value.size() > 10 || (value[i] == '0' && value.size() > i + 1)) return nullptr;
                      for (; i < value.size(); ++i)
                      {
                          if (!isdigit(static_cast<unsigned char>(value[i]))) return nullptr;
                      }
                      put.kind = JsonValue::Kind::Number;
                      put.text = value;
                  }
                  const std::vector<LSLConstant*> steps = elements(path);
                  if (steps.empty()) return nullptr;
                  JsonValue* at = &root;
                  for (size_t i = 0; i + 1 < steps.size(); ++i)
                  {
                      const std::vector<LSLConstant*> one(steps.begin() + static_cast<std::ptrdiff_t>(i), steps.begin() + static_cast<std::ptrdiff_t>(i) + 1);
                      const JsonValue*                next = jsonAt(*at, one);
                      if (!next) return nullptr;
                      at = const_cast<JsonValue*>(next);
                  }
                  LSLConstant* last = steps.back();
                  if (last->getNodeSubType() == NODE_STRING_CONSTANT && at->kind == JsonValue::Kind::Object)
                  {
                      std::string key;
                      if (!argString({ last }, 0, key) || !plainJsonString(key)) return nullptr;
                      bool found = false;
                      for (size_t i = 0; i < at->fields.size(); ++i)
                      {
                          if (at->fields[i].first == key)
                          {
                              found = true;
                              if (deleting) at->fields.erase(at->fields.begin() + static_cast<std::ptrdiff_t>(i));
                              else at->fields[i].second = put;
                              break;
                          }
                      }
                      if (!found && deleting) return nullptr;
                      if (!found) at->fields.emplace_back(key, put);
                  }
                  else if (last->getNodeSubType() == NODE_INTEGER_CONSTANT && at->kind == JsonValue::Kind::Array)
                  {
                      const int index = static_cast<LSLIntegerConstant*>(last)->getValue();
                      if (deleting)
                      {
                          if (index < 0 || static_cast<size_t>(index) >= at->items.size()) return nullptr;
                          at->items.erase(at->items.begin() + index);
                      }
                      else if (index == -1 || static_cast<size_t>(index) == at->items.size()) at->items.push_back(put);
                      else if (index >= 0 && static_cast<size_t>(index) < at->items.size()) at->items[static_cast<size_t>(index)] = put;
                      else if (index > 0) return c.builtin("JSON_INVALID");
                      else return nullptr;
                  }
                  else
                  {
                      return nullptr;
                  }
                  return c.string(jsonWrite(root));
              } },
            { "llJsonValueType",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string      json;
                  LSLListConstant* path;
                  if (!argString(a, 0, json) || !argList(a, 1, path)) return nullptr;
                  JsonValue  root;
                  JsonReader reader(json);
                  if (!reader.whole(root)) return nullptr;
                  const JsonValue* at = jsonAt(root, elements(path));
                  if (!at) return c.builtin("JSON_INVALID");
                  switch (at->kind)
                  {
                      case JsonValue::Kind::Object: return c.builtin("JSON_OBJECT");
                      case JsonValue::Kind::Array:  return c.builtin("JSON_ARRAY");
                      case JsonValue::Kind::String: return c.builtin("JSON_STRING");
                      case JsonValue::Kind::Number: return c.builtin("JSON_NUMBER");
                      case JsonValue::Kind::True:   return c.builtin("JSON_TRUE");
                      case JsonValue::Kind::False:  return c.builtin("JSON_FALSE");
                      case JsonValue::Kind::Null:   return c.builtin("JSON_NULL");
                  }
                  return nullptr;
              } },
            { "llList2Json",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  // Only what every writer spells alike: integers and
                  // plain strings -- letters, digits after the first,
                  // spaces and the punctuation that needs no escape --
                  // that read as nothing else. Floats, JSON values as
                  // strings, and anything wanting an escape are left.
                  LSLListConstant* items;
                  if (a.empty() || a[0]->getNodeSubType() != NODE_STRING_CONSTANT || !argList(a, 1, items)) return nullptr;
                  // The kind is a JSON_* value, which is no ASCII string.
                  const std::string kind   = static_cast<LSLStringConstant*>(a[0])->getValue();
                  LSLConstant*      array  = c.builtin("JSON_ARRAY");
                  LSLConstant*      object = c.builtin("JSON_OBJECT");
                  const bool        is_array  = array && kind == static_cast<LSLStringConstant*>(array)->getValue();
                  const bool        is_object = object && kind == static_cast<LSLStringConstant*>(object)->getValue();
                  if (!is_array && !is_object) return nullptr;
                  const std::vector<LSLConstant*> list = elements(items);
                  if (is_object && list.size() % 2 != 0) return nullptr;
                  std::string out = is_array ? "[" : "{";
                  for (size_t i = 0; i < list.size(); ++i)
                  {
                      // A comma before each item, or before each pair.
                      if (i > 0 && (is_array || i % 2 == 0)) out += ",";
                      LSLConstant* item = list[i];
                      if (is_object && i % 2 == 0)
                      {
                          std::string key;
                          if (!argString({ item }, 0, key) || !plainJsonString(key)) return nullptr;
                          out += "\"" + key + "\":";
                          continue;
                      }
                      if (item->getNodeSubType() == NODE_INTEGER_CONSTANT)
                      {
                          out += std::to_string(static_cast<LSLIntegerConstant*>(item)->getValue());
                          continue;
                      }
                      std::string text;
                      if (!argString({ item }, 0, text) || !plainJsonString(text)) return nullptr;
                      out += "\"" + text + "\"";
                  }
                  out += is_array ? "]" : "}";
                  return c.string(out);
              } },
            { "llJson2List",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  // An array's items or an object's keys and values, where
                  // every one is an integer or a plain string; anything
                  // else the simulator types its own way.
                  std::string json;
                  if (!argString(a, 0, json)) return nullptr;
                  JsonValue  root;
                  JsonReader reader(json);
                  if (!reader.whole(root)) return nullptr;
                  std::vector<const JsonValue*> values;
                  std::vector<LSLConstant*>     out;
                  auto                          push = [&](const JsonValue& v) {
                      if (v.kind == JsonValue::Kind::Number)
                      {
                          if (v.text.size() > 10) return false;
                          out.push_back(c.integer(static_cast<int>(std::strtol(v.text.c_str(), nullptr, 10))));
                          return true;
                      }
                      if (v.kind == JsonValue::Kind::String && plainJsonString(v.text))
                      {
                          out.push_back(c.string(v.text));
                          return true;
                      }
                      return false;
                  };
                  if (root.kind == JsonValue::Kind::Array)
                  {
                      for (const JsonValue& v : root.items)
                      {
                          if (!push(v)) return nullptr;
                      }
                  }
                  else if (root.kind == JsonValue::Kind::Object)
                  {
                      for (const auto& field : root.fields)
                      {
                          if (!plainJsonString(field.first)) return nullptr;
                          out.push_back(c.string(field.first));
                          if (!push(field.second)) return nullptr;
                      }
                  }
                  else
                  {
                      return nullptr;
                  }
                  return listOf(c, out);
              } },
            { "llAcos",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && std::fabs(v) <= 1.0 ? c.number(std::acos(v)) : nullptr;
              } },
            { "llAsin",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  return argFloat(a, 0, v) && std::fabs(v) <= 1.0 ? c.number(std::asin(v)) : nullptr;
              } },
            { "llRound",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v)) return nullptr;
                  // LSO adds the half in single precision, Mono in double:
                  // folded only where the two come to the same.
                  double rounded = std::floor(v + 0.5);
                  if (c.target != ALLSLOptimizer::Target::Luau)
                  {
                      const float f = static_cast<float>(v);
                      rounded       = std::floor(static_cast<double>(f) + 0.5);
                      if (static_cast<double>(std::floor(f + 0.5f)) != rounded) return nullptr;
                  }
                  return std::fabs(rounded) < 2147483647.0 ? c.integer(static_cast<int>(rounded)) : nullptr;
              } },
            { "llDumpList2String",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  std::string      between;
                  if (!argList(a, 0, l) || !argString(a, 1, between) || !ascii(between.c_str())) return nullptr;
                  std::string out;
                  bool        first = true;
                  for (LSLConstant* item : elements(l))
                  {
                      std::string text;
                      if (!elementText(item, text)) return nullptr;
                      out += (first ? "" : between) + text;
                      first = false;
                  }
                  return c.string(out);
              } },
            { "llListFindList",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* in;
                  LSLListConstant* sought;
                  // An empty list to find is found where LSO and Mono
                  // disagree: not folded.
                  if (!argList(a, 0, in) || !argList(a, 1, sought)) return nullptr;
                  const std::vector<LSLConstant*> hay  = elements(in);
                  const std::vector<LSLConstant*> find = elements(sought);
                  if (find.empty()) return nullptr;
                  // The same type and the same value.
                  const auto same = [](LSLConstant* x, LSLConstant* y) {
                      if (x->getNodeSubType() != y->getNodeSubType()) return false;
                      switch (x->getNodeSubType())
                      {
                          case NODE_INTEGER_CONSTANT:
                              return static_cast<LSLIntegerConstant*>(x)->getValue() == static_cast<LSLIntegerConstant*>(y)->getValue();
                          case NODE_FLOAT_CONSTANT:
                              return static_cast<LSLFloatConstant*>(x)->getValue() == static_cast<LSLFloatConstant*>(y)->getValue();
                          case NODE_STRING_CONSTANT:
                          case NODE_KEY_CONSTANT:
                              return !strcmp(static_cast<LSLStringConstant*>(x)->getValue(), static_cast<LSLStringConstant*>(y)->getValue());
                          case NODE_VECTOR_CONSTANT:
                          {
                              const Vector3* p = static_cast<LSLVectorConstant*>(x)->getValue();
                              const Vector3* q = static_cast<LSLVectorConstant*>(y)->getValue();
                              return p->x == q->x && p->y == q->y && p->z == q->z;
                          }
                          case NODE_QUATERNION_CONSTANT:
                          {
                              const Quaternion* p = static_cast<LSLQuaternionConstant*>(x)->getValue();
                              const Quaternion* q = static_cast<LSLQuaternionConstant*>(y)->getValue();
                              return p->x == q->x && p->y == q->y && p->z == q->z && p->s == q->s;
                          }
                          default:
                              return false;
                      }
                  };
                  for (size_t i = 0; i + find.size() <= hay.size(); ++i)
                  {
                      bool all = true;
                      for (size_t j = 0; j < find.size() && all; ++j)
                      {
                          all = same(hay[i + j], find[j]);
                      }
                      if (all) return c.integer(static_cast<int>(i));
                  }
                  return c.integer(-1);
              } },
            { "llMD5String",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string text;
                  int         nonce;
                  if (!argString(a, 0, text) || !argInt(a, 1, nonce)) return nullptr;
                  LLMD5 md5;
                  md5.update(text + ":" + std::to_string(nonce));
                  md5.finalize();
                  char hex[33];
                  md5.hex_digest(hex);
                  return c.string(hex);
              } },
            { "llList2CSV",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  if (!argList(a, 0, l)) return nullptr;
                  std::string out;
                  bool        first = true;
                  for (LSLConstant* item : elements(l))
                  {
                      std::string text;
                      if (!elementText(item, text)) return nullptr;
                      out += (first ? "" : ", ") + text;
                      first = false;
                  }
                  return c.string(out);
              } },
        };
        return table;
    }

    // llList2Integer and its kin: the element where it is the type asked
    // for, the type's nothing where the index is off the end, and the
    // VM's own answer otherwise.
    LSLConstant* listGet(Ctx& c, const Args& a, LSLNodeSubType wanted)
    {
        LSLListConstant* l;
        int              at;
        if (!argList(a, 0, l) || !argInt(a, 1, at))
        {
            return nullptr;
        }
        std::vector<LSLConstant*> items  = elements(l);
        const int                 length = static_cast<int>(items.size());
        if (at < 0)
        {
            at += length;
        }
        if (at < 0 || at >= length)
        {
            switch (wanted)
            {
                case NODE_INTEGER_CONSTANT: return c.integer(0);
                case NODE_FLOAT_CONSTANT: return c.number(0.0);
                case NODE_STRING_CONSTANT: return c.string(std::string());
                // What LSO gives for a key out of range is NULL_KEY, which
                // the call is folded to by name; Mono gives an empty key.
                case NODE_KEY_CONSTANT: return c.target == ALLSLOptimizer::Target::LSO ? c.builtin("NULL_KEY") : c.key(std::string());
                case NODE_VECTOR_CONSTANT: return c.vector(0, 0, 0);
                case NODE_QUATERNION_CONSTANT: return c.allocator->newTracked<LSLQuaternionConstant>(0.f, 0.f, 0.f, 1.f);
                default: return nullptr;
            }
        }
        LSLConstant* item = items[at];
        if (wanted == NODE_STRING_CONSTANT)
        {
            std::string text;
            return elementText(item, text) ? c.string(text) : nullptr;
        }
        if (wanted == NODE_KEY_CONSTANT && item->getNodeSubType() == NODE_STRING_CONSTANT)
        {
            return c.key(static_cast<LSLStringConstant*>(item)->getValue());
        }
        return item->getNodeSubType() == wanted ? item->copy(c.allocator) : nullptr;
    }

    LSLConstant* evaluate(Ctx& ctx, const char* name, const Args& args)
    {
        // The list getters are in the table with the rest; nothing is
        // compared before it is looked up.
        static const boost::unordered_flat_map<std::string, LSLNodeSubType, ll::string_hash, std::equal_to<>> GETTERS = {
            { "llList2Integer", NODE_INTEGER_CONSTANT },   { "llList2Float", NODE_FLOAT_CONSTANT },
            { "llList2String", NODE_STRING_CONSTANT },     { "llList2Key", NODE_KEY_CONSTANT },
            { "llList2Vector", NODE_VECTOR_CONSTANT },     { "llList2Rot", NODE_QUATERNION_CONSTANT },
        };
        if (const auto getter = GETTERS.find(name); getter != GETTERS.end())
        {
            return listGet(ctx, args, getter->second);
        }
        auto it = evaluators().find(name);
        return it == evaluators().end() ? nullptr : it->second(ctx, args);
    }

    // ---- the passes -----------------------------------------------------------------------------

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

    // Constant expressions and never-assigned variables become their
    // values; a call to a pure library function with its arguments in
    // hand becomes its answer.
    class Folder : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLExpression* expr) override
        {
            switch (expr->getNodeSubType())
            {
                case NODE_CONSTANT_EXPRESSION:
                    return false;
                case NODE_FUNCTION_EXPRESSION:
                    return !call(static_cast<LSLFunctionExpression*>(expr));
                default:
                    break;
            }
            LSLConstant* cv = expr->getConstantValue();
            if (cv && inlineable(cv))
            {
                fold(expr, cv, "OptimizerFolded", "folded");
                return false;
            }
            return true;
        }

        bool visit(LSLLValueExpression* lvalue) override
        {
            LSLSymbol* sym = lvalue->getSymbol();
            if (!sym || sym->getSubType() == SYM_BUILTIN || sym->getIType() == LST_LIST)
            {
                // A builtin constant is a token the compiler already has.
                return false;
            }
            if (sym->getIType() == LST_KEY && !keyMayInline(lvalue))
            {
                return false;
            }
            LSLConstant* cv = lvalue->getConstantValue();
            if (cv && inlineable(cv) && writeOut(sym, cv))
            {
                fold(lvalue, cv, "OptimizerInlinedConstant", "inlined");
            }
            return false;
        }

    private:
        // A global's value goes where it is read only where writing it at
        // every place it is read costs less than the global does: a vector
        // read ten times is ten vectors. What goes where is the target's
        // (ALLSLCosts). A value a larger expression folds into is not a
        // read of it, and folds whatever this says.
        bool writeOut(LSLSymbol* sym, LSLConstant* cv) const
        {
            if (sym->getSubType() != SYM_GLOBAL)
            {
                return true;
            }
            const ALLSLCosts& costs = ALLSLCosts::of(ctx.target);
            const auto        whole = [](std::initializer_list<double> parts) {
                return std::all_of(parts.begin(), parts.end(), [](double v) { return integral(v); });
            };
            ALLSLCosts::Held held;
            switch (cv->getIType())
            {
                case LST_INTEGER:
                    held = costs.integer;
                    break;
                case LST_FLOATINGPOINT:
                    held = whole({ static_cast<LSLFloatConstant*>(cv)->getValue() }) ? costs.wholeFloating : costs.floating;
                    break;
                case LST_VECTOR:
                {
                    const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                    held             = whole({ v->x, v->y, v->z }) ? costs.wholeVector : costs.vector;
                    break;
                }
                case LST_QUATERNION:
                {
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                    held                = whole({ q->x, q->y, q->z, q->s }) ? costs.wholeRotation : costs.rotation;
                    break;
                }
                case LST_STRING:
                    held = costs.stringOf(static_cast<S32>(strlen(static_cast<LSLStringConstant*>(cv)->getValue())));
                    break;
                default:
                    return true;
            }
            return held.writeOut(sym->getReferences() - 1 - sym->getAssignments());
        }

        // Tailslide's rules: a key's key-ness must not be lost to a list,
        // a print, a condition or a boolean.
        static bool keyMayInline(LSLLValueExpression* lvalue)
        {
            LSLASTNode* ancestor = lvalue->getParent();
            LSLASTNode* top      = lvalue;
            while (ancestor && ancestor->getNodeType() == NODE_EXPRESSION)
            {
                switch (ancestor->getNodeSubType())
                {
                    case NODE_LIST_EXPRESSION:
                    case NODE_PRINT_EXPRESSION:
                    case NODE_BOOL_CONVERSION_EXPRESSION:
                        return false;
                    case NODE_TYPECAST_EXPRESSION:
                        if (ancestor->getIType() == LST_LIST)
                        {
                            return false;
                        }
                        break;
                    default:
                        break;
                }
                top      = ancestor;
                ancestor = ancestor->getParent();
            }
            if (top->getIType() == LST_KEY)
            {
                if (ancestor && ancestor->getNodeType() == NODE_AST_NODE_LIST)
                {
                    ancestor = ancestor->getParent();
                }
                if (ancestor && ancestor->getNodeType() == NODE_STATEMENT)
                {
                    switch (ancestor->getNodeSubType())
                    {
                        case NODE_WHILE_STATEMENT:
                        case NODE_IF_STATEMENT:
                        case NODE_DO_STATEMENT:
                        case NODE_FOR_STATEMENT:
                            return false;
                        default:
                            break;
                    }
                }
            }
            return true;
        }

        bool call(LSLFunctionExpression* expr)
        {
            LSLSymbol* sym = expr->getSymbol();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !isPure(sym->getName()))
            {
                return false;
            }
            Args args;
            for (LSLASTNode* arg : *expr->getArguments())
            {
                LSLConstant* cv = arg->getConstantValue();
                if (!cv && arg->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    // A list is never written into the script in place of
                    // its name, but a call may still be answered from it.
                    auto*      lvalue = static_cast<LSLLValueExpression*>(arg);
                    LSLSymbol* sym    = lvalue->getSymbol();
                    if (sym && !lvalue->getMember() && sym->getAssignments() == 0 && sym->getSubType() != SYM_BUILTIN)
                    {
                        cv = sym->getConstantValue();
                    }
                }
                if (!cv)
                {
                    return false;
                }
                args.push_back(cv);
            }
            LSLConstant* value = evaluate(ctx, sym->getName(), args);
            if (!value)
            {
                return false;
            }
            if (const std::string* name = ctx.nameOf(value))
            {
                // The builtin's name in place of the call: a token the
                // compiler already has.
                LSLSymbol* builtin = ctx.context->builtins->lookup(name->c_str(), SYM_VARIABLE);
                if (!builtin)
                {
                    return false;
                }
                // newTracked passes the context itself.
                auto* id = ctx.allocator->newTracked<LSLIdentifier>(ctx.allocator->copyStr(name->c_str()));
                id->setSymbol(builtin);
                auto* lvalue = ctx.allocator->newTracked<LSLLValueExpression>(id, static_cast<LSLIdentifier*>(nullptr));
                lvalue->setLoc(expr->getLoc());
                id->setLoc(expr->getLoc());
                report.note(expr->getLoc(), "OptimizerEvaluated", "evaluated [1] to [2]", { render(expr), *name });
                LSLASTNode::replaceNode(expr, lvalue);
                ++changes;
                return true;
            }
            if (!inlineable(value, true))
            {
                return false;
            }
            fold(expr, value, "OptimizerEvaluated", "evaluated");
            return true;
        }
    };

    // The identities, the shorter spellings, and the reshaped conditions.
    class Simplifier : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLBinaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (!left || !right || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            const LSLOperator op   = expr->getOperation();
            const LSLIType    type = expr->getIType();
            const bool        keepLeft  = left->getIType() == type;
            const bool        keepRight = right->getIType() == type;
            const bool        numeric   = type == LST_INTEGER || type == LST_FLOATINGPOINT;
            const auto        zero      = [](LSLASTNode* n) { return isInteger(n, 0) || isFloat(n, 0.0); };
            const auto        one       = [](LSLASTNode* n) { return isInteger(n, 1) || isFloat(n, 1.0); };
            switch (op)
            {
                case OP_PLUS:
                    // What adds nothing, by what the sum is: an empty
                    // string to a string, an empty list to a list, and a
                    // zero to an integer. A list plus anything else is that
                    // list with one more element -- `l + 0` appends a zero,
                    // and so does `(list)a + 0`, which is what a list's
                    // literal is written as -- and a float plus zero is not
                    // the float where the float is -0.0.
                    if (keepLeft && ((type == LST_INTEGER && zero(right)) || (type == LST_STRING && isEmptyString(right)) ||
                                     (type == LST_LIST && isEmptyList(right))))
                    {
                        return keep(expr, 0);
                    }
                    if (keepRight && ((type == LST_INTEGER && zero(left)) || (type == LST_STRING && isEmptyString(left)) ||
                                      (type == LST_LIST && isEmptyList(left))))
                    {
                        return keep(expr, 1);
                    }
                    if (numeric && keepLeft && negative(right)) return resign(expr, OP_MINUS);
                    break;
                case OP_MINUS:
                    // Taking zero away leaves a number as it was, -0.0 too.
                    if (numeric && keepLeft && zero(right)) return keep(expr, 0);
                    if (numeric && keepLeft && negative(right)) return resign(expr, OP_PLUS);
                    break;
                case OP_MUL:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    if (keepRight && one(left)) return keep(expr, 1);
                    if (type == LST_INTEGER && isInteger(right, 0) && changesNothing(left)) return become(expr, ctx.integer(0));
                    if (type == LST_INTEGER && isInteger(left, 0) && changesNothing(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_DIV:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    break;
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_SHIFT_LEFT:
                case OP_SHIFT_RIGHT:
                    if (isInteger(right, 0)) return keep(expr, 0);
                    if ((op == OP_BIT_OR || op == OP_BIT_XOR) && isInteger(left, 0)) return keep(expr, 1);
                    break;
                case OP_BIT_AND:
                    if (isInteger(right, -1)) return keep(expr, 0);
                    if (isInteger(left, -1)) return keep(expr, 1);
                    if (isInteger(right, 0) && changesNothing(left)) return become(expr, ctx.integer(0));
                    if (isInteger(left, 0) && changesNothing(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_EQ:
                    // `x == 0` is `!x`, and one token shorter.
                    if (left->getIType() == LST_INTEGER && isInteger(right, 0)) return negate(expr, 0);
                    if (right->getIType() == LST_INTEGER && isInteger(left, 0)) return negate(expr, 1);
                    break;
                case OP_BOOLEAN_AND:
                case OP_BOOLEAN_OR:
                    condition(expr, 0);
                    condition(expr, 1);
                    break;
                default:
                    break;
            }
            return false;
        }

        bool visit(LSLUnaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (!child || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            LSLExpression* inner = bare(child);
            if (expr->getOperation() == OP_MINUS && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_MINUS)
            {
                // -(-x)
                LSLExpression* x = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (x && x->getIType() == expr->getIType())
                {
                    report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { render(expr), render(x) });
                    inner->setChild(0, nullptr);
                    putInPlace(expr, x, ctx.allocator);
                    ++changes;
                }
                return false;
            }
            if (expr->getOperation() == OP_BOOLEAN_NOT)
            {
                condition(expr, 0);
                child = expr->getChildExpr();
                inner = bare(child);
                if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getIType() == LST_INTEGER)
                {
                    // !(a != b) is a == b, whatever they are: both are
                    // true or false. !(a == b) is a != b only for numbers --
                    // a list's != is the difference of the lengths, and
                    // LSO's for a string is not only one or zero -- and the
                    // orderings only for integers, which have no NaN.
                    auto*       bin      = static_cast<LSLBinaryExpression*>(inner);
                    LSLOperator opposite = OP_NONE;
                    const auto  number   = [](LSLExpression* e) { return e->getIType() == LST_INTEGER || e->getIType() == LST_FLOATINGPOINT; };
                    const bool  ints     = bin->getLHS()->getIType() == LST_INTEGER && bin->getRHS()->getIType() == LST_INTEGER;
                    const bool  numbers  = number(bin->getLHS()) && number(bin->getRHS());
                    switch (bin->getOperation())
                    {
                        case OP_EQ: opposite = numbers ? OP_NEQ : OP_NONE; break;
                        case OP_NEQ: opposite = OP_EQ; break;
                        case OP_LESS: opposite = ints ? OP_GEQ : OP_NONE; break;
                        case OP_GREATER: opposite = ints ? OP_LEQ : OP_NONE; break;
                        case OP_LEQ: opposite = ints ? OP_GREATER : OP_NONE; break;
                        case OP_GEQ: opposite = ints ? OP_LESS : OP_NONE; break;
                        default: break;
                    }
                    if (opposite != OP_NONE)
                    {
                        const std::string was = render(expr);
                        bin->setOperation(opposite);
                        inner->getParent()->takeChild(inner->getParentSlot());
                        report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(bin) });
                        putInPlace(expr, bin, ctx.allocator);
                        ++changes;
                    }
                }
            }
            return false;
        }

        bool visit(LSLTypecastExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (child && child->getIType() == expr->getIType() && expr->getIType() != LST_ERROR)
            {
                report.note(expr->getLoc(), "OptimizerDroppedCast", "dropped the cast in [1]", { render(expr) });
                expr->setChild(0, nullptr);
                putInPlace(expr, child, ctx.allocator);
                ++changes;
                return false;
            }
            return false;
        }

        bool visit(LSLFunctionExpression* expr) override
        {
            visitChildren(expr);
            LSLSymbol* sym = expr->getSymbol();
            if (options.listlength && ALLSLCosts::of(options.target).lengthAsNotEqual && sym && sym->getSubType() == SYM_BUILTIN &&
                !strcmp(sym->getName(), "llGetListLength") &&
                expr->getArguments()->getNumChildren() == 1)
            {
                // llGetListLength(x) is x != [], which is the length.
                auto* arg = static_cast<LSLExpression*>(expr->getArguments()->takeChild(0));
                auto* empty = ctx.allocator->newTracked<LSLConstantExpression>(ctx.allocator->newTracked<LSLListConstant>(nullptr));
                auto* test  = ctx.allocator->newTracked<LSLBinaryExpression>(arg, OP_NEQ, static_cast<LSLExpression*>(empty));
                test->setType(TYPE(LST_INTEGER));
                report.note(expr->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { render(expr), render(test) });
                putInPlace(expr, test, ctx.allocator);
                ++changes;
            }
            return false;
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            if (!options.ifelseswap)
            {
                return false;
            }
            LSLExpression* cond  = stmt->getCheckExpr();
            LSLStatement*  yes   = stmt->getTrueBranch();
            LSLStatement*  no    = stmt->getFalseBranch();
            LSLExpression* inner = bare(cond);
            if (no && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                // if (!c) A else B is if (c) B else A.
                LSLExpression* c = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (c)
                {
                    inner->setChild(0, nullptr);
                    stmt->setCheckExpr(c);
                    swapBranches(stmt);
                    report.note(stmt->getLoc(), "OptimizerSwappedBranches", "swapped the branches of if ([1]) and dropped the !", { render(c) });
                    ++changes;
                }
                return false;
            }
            if (no && empty(yes) && cond->getIType() == LST_INTEGER)
            {
                // if (c) ; else B is if (!c) B: !(c) where c is an
                // operation, which the ! would take only the first operand
                // of otherwise.
                stmt->setCheckExpr(nullptr);
                LSLExpression* operand = cond;
                if (cond->getNodeSubType() == NODE_BINARY_EXPRESSION)
                {
                    auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(cond);
                    parens->setType(cond->getType());
                    parens->setLoc(cond->getLoc());
                    operand = parens;
                }
                auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
                negated->setType(TYPE(LST_INTEGER));
                negated->setLoc(cond->getLoc());
                stmt->setCheckExpr(negated);
                stmt->setTrueBranch(nullptr);
                stmt->setFalseBranch(nullptr);
                stmt->setTrueBranch(no);
                report.note(stmt->getLoc(), "OptimizerTurnedEmptyBranch", "turned an empty if branch around");
                ++changes;
            }
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

    private:
        static bool empty(LSLStatement* s)
        {
            if (!s)
            {
                return true;
            }
            if (s->getNodeSubType() == NODE_NOP_STATEMENT)
            {
                return true;
            }
            return s->getNodeSubType() == NODE_COMPOUND_STATEMENT && !s->hasChildren();
        }

        static void swapBranches(LSLIfStatement* stmt)
        {
            LSLStatement* yes = stmt->getTrueBranch();
            LSLStatement* no  = stmt->getFalseBranch();
            stmt->setTrueBranch(nullptr);
            stmt->setFalseBranch(nullptr);
            stmt->setTrueBranch(no);
            stmt->setFalseBranch(yes);
        }

        // A constant below zero, whose sign can move onto the operator.
        static bool negative(LSLASTNode* n)
        {
            LSLConstant* cv = n->getConstantValue();
            if (!cv)
            {
                return false;
            }
            if (cv->getNodeSubType() == NODE_INTEGER_CONSTANT)
            {
                const int v = static_cast<LSLIntegerConstant*>(cv)->getValue();
                return v < 0 && v != INT32_MIN;
            }
            if (cv->getNodeSubType() == NODE_FLOAT_CONSTANT)
            {
                return static_cast<LSLFloatConstant*>(cv)->getValue() < 0.0;
            }
            return false;
        }

        // The expression becomes one of its operands.
        bool keep(LSLBinaryExpression* expr, int slot)
        {
            const std::string was  = render(expr);
            auto*             kept = static_cast<LSLExpression*>(expr->takeChild(slot));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(kept) });
            putInPlace(expr, kept, ctx.allocator);
            ++changes;
            return false;
        }

        bool become(LSLBinaryExpression* expr, LSLConstant* cv)
        {
            fold(expr, cv, "OptimizerSimplified", "simplified");
            return false;
        }

        // x + -c as x - c, x - -c as x + c.
        bool resign(LSLBinaryExpression* expr, LSLOperator op)
        {
            const std::string was = render(expr);
            LSLConstant*      cv  = expr->getRHS()->getConstantValue();
            LSLConstant*      pos = cv->getNodeSubType() == NODE_INTEGER_CONSTANT
                                        ? ctx.integer(-static_cast<LSLIntegerConstant*>(cv)->getValue())
                                        : ctx.allocator->newTracked<LSLFloatConstant>(-static_cast<LSLFloatConstant*>(cv)->getValue());
            expr->setOperation(op);
            expr->setRHS(constant(pos, expr->getRHS()));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(expr) });
            ++changes;
            return false;
        }

        // x == 0 as !x.
        bool negate(LSLBinaryExpression* expr, int slot)
        {
            const std::string was = render(expr);
            auto*             x   = static_cast<LSLExpression*>(expr->takeChild(slot));
            LSLExpression*    operand = x;
            if (x->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                operand = ctx.allocator->newTracked<LSLParenthesisExpression>(x);
                operand->setType(x->getType());
            }
            auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
            negated->setType(TYPE(LST_INTEGER));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(negated) });
            putInPlace(expr, negated, ctx.allocator);
            ++changes;
            return false;
        }

        // A child that is only ever true or false: `x != 0` is `x`, and
        // `!!x` is `x`.
        void condition(LSLASTNode* parent, int slot)
        {
            LSLASTNode* child = parent->getChild(slot);
            if (!child || child->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            auto*          expr  = static_cast<LSLExpression*>(child);
            LSLExpression* inner = bare(expr);
            if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getOperation() == OP_NEQ)
            {
                auto* bin = static_cast<LSLBinaryExpression*>(inner);
                int   keepSlot = -1;
                if (bin->getLHS()->getIType() == LST_INTEGER && isInteger(bin->getRHS(), 0)) keepSlot = 0;
                else if (bin->getRHS()->getIType() == LST_INTEGER && isInteger(bin->getLHS(), 0)) keepSlot = 1;
                if (keepSlot >= 0)
                {
                    const std::string was  = render(expr);
                    auto*             kept = static_cast<LSLExpression*>(bin->takeChild(keepSlot));
                    report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(kept) });
                    LSLASTNode::replaceNode(expr, kept);
                    ++changes;
                    return;
                }
            }
            if (inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                LSLExpression* once = bare(static_cast<LSLUnaryExpression*>(inner)->getChildExpr());
                if (once && once->getNodeSubType() == NODE_UNARY_EXPRESSION && once->getOperation() == OP_BOOLEAN_NOT)
                {
                    LSLExpression* x = static_cast<LSLUnaryExpression*>(once)->getChildExpr();
                    if (x && x->getIType() == LST_INTEGER)
                    {
                        const std::string was = render(expr);
                        once->setChild(0, nullptr);
                        report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(x) });
                        LSLASTNode::replaceNode(expr, x);
                        ++changes;
                    }
                }
            }
        }
    };

    // Which of Mono's list helpers a script calls, each by what Tailslide's
    // Mono compiler calls one for: a literal, an empty list, a cast to a
    // list and one of a list to a string, a list's literal among the
    // globals, something added to a list -- a helper for each type added
    // -- and something added before one.
    // The assembly references each once, which is what a list's shapes can
    // cost that they do not save at each place: a helper nothing in the
    // script called before.
    class ListHelpers : public ASTVisitor
    {
    public:
        boost::unordered_flat_set<U32> used;
        // Lists dumped with nothing between their elements, each a cast to
        // a string it may be.
        S32                            dumps = 0;

        bool castsToString() const { return used.contains(TO_STRING); }

        bool visit(LSLFunctionExpression* expr) override
        {
            LSLSymbol*                     sym  = expr->getSymbol();
            LSLASTNodeList<LSLExpression>* args = expr->getArguments();
            if (sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llDumpList2String") && args && args->getNumChildren() == 2)
            {
                LSLConstant* cv = args->getChild(1)->getConstantValue();
                dumps += cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue() ? 1 : 0;
            }
            return true;
        }

        // How many of `after`'s helpers are not among these.
        S32 freshIn(const ListHelpers& after) const
        {
            return static_cast<S32>(std::count_if(after.used.begin(), after.used.end(), [this](U32 helper) { return !used.contains(helper); }));
        }

        bool visit(LSLGlobalVariable* global) override
        {
            mGlobal = true;
            visitChildren(global);
            mGlobal = false;
            return false;
        }
        bool visit(LSLListExpression* expr) override
        {
            used.insert(mGlobal ? GLOBAL_LITERAL : expr->hasChildren() ? LITERAL : EMPTY);
            return true;
        }
        bool visit(LSLTypecastExpression* expr) override
        {
            if (expr->getIType() == LST_LIST)
            {
                used.insert(CAST);
            }
            else if (expr->getIType() == LST_STRING && expr->getChildExpr() && expr->getChildExpr()->getIType() == LST_LIST)
            {
                used.insert(TO_STRING);
            }
            return true;
        }
        bool visit(LSLBinaryExpression* expr) override
        {
            const LSLOperator op    = expr->getOperation();
            LSLExpression*    left  = expr->getLHS();
            LSLExpression*    right = expr->getRHS();
            if ((op == OP_PLUS || op == OP_ADD_ASSIGN) && expr->getIType() == LST_LIST && left && right)
            {
                used.insert(left->getIType() == LST_LIST ? APPEND | static_cast<U32>(right->getIType()) : PREPEND);
            }
            return true;
        }

    private:
        enum : U32
        {
            LITERAL        = 1u << 8,
            EMPTY          = 2u << 8,
            GLOBAL_LITERAL = 3u << 8,
            CAST           = 4u << 8,
            PREPEND        = 5u << 8,
            APPEND         = 6u << 8,
            TO_STRING      = 7u << 8
        };
        bool mGlobal = false;
    };

    // Rewrites made for size alone, once the rounds are done: each hides a
    // value the folder could have used -- a list's literal made a sum is
    // no longer a constant -- so they wait until nothing more will fold.
    class Shapes : public ASTVisitor, public Pass
    {
    public:
        // Integers' and conditions' shapes, and increments', which the
        // code makes of plain instructions; and lists', which Mono makes
        // of helpers its assembly references once each, so that what they
        // save at each place a helper new to the script can cost more than
        // -- weighed, where it can (once()).
        enum class Stage : U8
        {
            Values,
            Lists
        };
        Shapes(Ctx& c, Report& r, const ALLSLOptimizer::Options& o, Stage stage) : Pass(c, r, o), mCosts(ALLSLCosts::of(o.target)), mStage(stage) {}

        // An increment whose value nothing reads, before rather than after.
        bool visit(LSLExpressionStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                pre(stmt->getExpr());
            }
            return false;
        }

        // Where only whether it is true counts; and an if whose condition
        // is a == with an else, turned around over a ^.
        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage != Stage::Values)
            {
                return false;
            }
            truth(stmt, 0);
            LSLExpression* cond = bare(stmt->getCheckExpr());
            LSLStatement*  yes  = stmt->getTrueBranch();
            LSLStatement*  no   = stmt->getFalseBranch();
            if (mCosts.xorForNotEqual && no && yes && cond && cond->getNodeSubType() == NODE_BINARY_EXPRESSION && cond->getOperation() == OP_EQ &&
                integerOperands(static_cast<LSLBinaryExpression*>(cond)))
            {
                const std::string before = report.wanted() ? render(cond) : std::string();
                cond->setOperation(OP_BIT_XOR);
                stmt->setTrueBranch(nullptr);
                stmt->setFalseBranch(nullptr);
                stmt->setTrueBranch(no);
                stmt->setFalseBranch(yes);
                if (report.wanted())
                {
                    report.note(stmt->getLoc(), "OptimizerSwappedOnXor", "wrote if ([1]) as if ([2]), its branches swapped", { before, render(cond) });
                }
                ++changes;
            }
            return false;
        }

        // The ! of a comparison as the comparison the other way.
        bool visit(LSLUnaryExpression* expr) override
        {
            visitChildren(expr);
            if (mStage == Stage::Values && !inGlobal(expr))
            {
                notComparison(expr);
            }
            return false;
        }
        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                truth(stmt, 0);
            }
            return false;
        }
        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                truth(stmt, 1);
            }
            return false;
        }
        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage != Stage::Values)
            {
                return false;
            }
            truth(stmt, 1);
            if (LSLASTNode* steps = stmt->getIncrExprs())
            {
                for (LSLASTNode* step = steps->getChild(0); step; step = step->getNext())
                {
                    pre(step);
                }
            }
            return false;
        }

        bool visit(LSLFunctionExpression* expr) override
        {
            visitChildren(expr);
            if (!inGlobal(expr))
            {
                if (mStage == Stage::Values)
                {
                    nullKeys(expr);
                }
                else
                {
                    // A list cast to a string, a list's helper on Mono.
                    libraryCast(expr);
                }
            }
            return false;
        }

        bool visit(LSLBinaryExpression* expr) override
        {
            if (inGlobal(expr))
            {
                return false;
            }
            // A list's elements bare, seen as the author wrote them, before
            // what is left of a literal is made a sum below.
            if (mStage == Stage::Lists)
            {
                if (LSLExpression* made = elements(expr))
                {
                    made->visit(this);
                    return false;
                }
                visitChildren(expr);
                return false;
            }
            visitChildren(expr);
            integers(expr);
            return false;
        }

        bool visit(LSLListExpression* expr) override
        {
            visitChildren(expr);
            if (mStage != Stage::Lists || !options.listadd || !mCosts.listAsSum || expr->getNumChildren() == 0)
            {
                return false;
            }
            // Not in a global's initializer, which must stay simple.
            if (inGlobal(expr))
            {
                return false;
            }
            // A sum takes its right side first, so its elements are taken
            // last to first, where a literal's are taken first to last:
            // where one of them changes something, and another is anything
            // but a constant, that one could see the change the other way
            // round.
            size_t changing = 0;
            size_t varying  = 0;
            for (LSLASTNode* child : *expr)
            {
                if (child->getIType() == LST_LIST || child->getIType() == LST_ERROR)
                {
                    return false;
                }
                // Read at another time than it was: a read of the clock
                // counts as a change here.
                changing += sideEffectFree(child) ? 0 : 1;
                varying += child->getConstantValue() ? 0 : 1;
            }
            if (changing > 0 && varying > 1)
            {
                return false;
            }
            // [a, b, c] as (list)a + b + c.
            const std::string           was = render(expr);
            std::vector<LSLExpression*> terms;
            terms.reserve(expr->getNumChildren());
            while (expr->hasChildren())
            {
                // takeChild leaves a null in the slot, dropped each time.
                terms.push_back(static_cast<LSLExpression*>(expr->takeChild(0)));
                expr->removeChild(expr->getChild(0));
            }
            LSLExpression* sum = nullptr;
            {
                const Uncounted uncounted(*ctx.context);
                sum = uncounted.made(ctx.allocator->newTracked<LSLTypecastExpression>(TYPE(LST_LIST), terms.front()));
                sum->setLoc(terms.front()->getLoc());
                for (size_t i = 1; i < terms.size(); ++i)
                {
                    sum = uncounted.made(ctx.allocator->newTracked<LSLBinaryExpression>(sum, OP_PLUS, uncounted.made(bracketed(terms[i]))));
                    sum->setType(TYPE(LST_LIST));
                    sum->setLoc(terms[i]->getLoc());
                }
            }
            report.note(expr->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { was, render(sum) });
            putInPlace(expr, sum, ctx.allocator);
            ++changes;
            return false;
        }

    public:
        // What list helpers the script called before a list's shapes, where
        // they are held once for the script (Mono): a list dumped with
        // nothing between is cast to a string only where the script casts
        // one already, or the dumps are enough to pay for the helper.
        void setHelpers(const ListHelpers* had) { mHad = had; }

    private:
        const ALLSLCosts&  mCosts;
        const Stage        mStage;
        const ListHelpers* mHad = nullptr;

        static bool inGlobal(LSLASTNode* node)
        {
            for (LSLASTNode* up = node->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return true;
                }
            }
            return false;
        }

        void wrote(LSLASTNode* was, LSLExpression* now, const std::string& before)
        {
            if (report.wanted())
            {
                report.note(was->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { before, render(now) });
            }
            ++changes;
        }

        // `op` over `x`, which is bracketed where what it is could read
        // otherwise after the operator: a sum, or -- after a minus -- a
        // minus of its own, which would read as a decrement.
        LSLExpression* unary(const Uncounted& uncounted, LSLOperator op, LSLExpression* x)
        {
            bool tight = false;
            switch (x->getNodeSubType())
            {
                case NODE_LVALUE_EXPRESSION:
                case NODE_FUNCTION_EXPRESSION:
                case NODE_PARENTHESIS_EXPRESSION:
                case NODE_TYPECAST_EXPRESSION:
                    tight = true;
                    break;
                case NODE_UNARY_EXPRESSION:
                    tight = op != OP_MINUS || (x->getOperation() != OP_MINUS && x->getOperation() != OP_PRE_DECR);
                    break;
                default:
                    break;
            }
            if (!tight)
            {
                auto* parens = uncounted.made(ctx.allocator->newTracked<LSLParenthesisExpression>(x));
                parens->setType(x->getType());
                parens->setLoc(x->getLoc());
                x = parens;
            }
            auto* made = uncounted.made(ctx.allocator->newTracked<LSLUnaryExpression>(x, op));
            made->setType(TYPE(LST_INTEGER));
            made->setLoc(x->getLoc());
            return made;
        }

        // `expr`, a binary expression, made `ops` over its operand in
        // `slot`, the last of them outermost: `-~x` is { OP_BIT_NOT,
        // OP_MINUS }.
        void over(LSLBinaryExpression* expr, int slot, std::initializer_list<LSLOperator> ops)
        {
            const std::string before = report.wanted() ? render(expr) : std::string();
            auto*             x      = static_cast<LSLExpression*>(expr->takeChild(slot));
            LSLExpression*    made   = x;
            {
                const Uncounted uncounted(*ctx.context);
                for (LSLOperator op : ops)
                {
                    made = unary(uncounted, op, made);
                }
            }
            made->setLoc(expr->getLoc());
            // Unary binds tighter than anything around it.
            LSLASTNode::replaceNode(expr, made);
            wrote(expr, made, before);
        }

        // Whether a value is never below -1: a find's, or a local's that
        // is set to one where it is declared and never after.
        static bool atLeastMinusOne(LSLExpression* x)
        {
            x = bare(x);
            if (x && x->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(x)->getMember())
            {
                LSLSymbol*  sym  = x->getSymbol();
                LSLASTNode* decl = sym && sym->getSubType() == SYM_LOCAL && sym->getAssignments() == 0 ? sym->getVarDecl() : nullptr;
                LSLASTNode* init = decl ? decl->getChild(1) : nullptr;
                return init && init->getNodeType() == NODE_EXPRESSION && atLeastMinusOne(static_cast<LSLExpression*>(init));
            }
            if (!x || x->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym = static_cast<LSLFunctionExpression*>(x)->getSymbol();
            return sym && sym->getSubType() == SYM_BUILTIN && ALLSLTraits::atLeastMinusOne(sym->getName());
        }

        static bool integerOperands(LSLBinaryExpression* expr)
        {
            return expr->getLHS() && expr->getRHS() && expr->getLHS()->getIType() == LST_INTEGER && expr->getRHS()->getIType() == LST_INTEGER;
        }

        // !(a < b) as a >= b, and each comparison's so: of integers, where
        // the order of any two is known; and of anything but lists for ==
        // and !=, which are each other's opposite whatever the values --
        // a list's != is how much longer one is.
        void notComparison(LSLUnaryExpression* expr)
        {
            LSLExpression* inner = bare(expr->getChildExpr());
            if (!mCosts.comparisonForNot || expr->getOperation() != OP_BOOLEAN_NOT || !inner || inner->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return;
            }
            auto*             cmp = static_cast<LSLBinaryExpression*>(inner);
            LSLOperator       to  = OP_NONE;
            const LSLOperator op  = cmp->getOperation();
            switch (op)
            {
                case OP_LESS: to = OP_GEQ; break;
                case OP_GEQ: to = OP_LESS; break;
                case OP_GREATER: to = OP_LEQ; break;
                case OP_LEQ: to = OP_GREATER; break;
                case OP_EQ: to = OP_NEQ; break;
                case OP_NEQ: to = OP_EQ; break;
                default: return;
            }
            LSLExpression* left  = cmp->getLHS();
            LSLExpression* right = cmp->getRHS();
            if (!left || !right)
            {
                return;
            }
            const bool equality = op == OP_EQ || op == OP_NEQ;
            const bool fits     = integerOperands(cmp) || (equality && left->getIType() != LST_LIST && right->getIType() != LST_LIST &&
                                                       left->getIType() != LST_ERROR && right->getIType() != LST_ERROR);
            if (!fits)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            cmp->getParent()->takeChild(cmp->getParentSlot());
            cmp->setOperation(to);
            putInPlace(expr, cmp, ctx.allocator);
            wrote(expr, cmp, before);
        }

        // a >= 5 as a > 4, and a <= 5 as a < 6, either way round, where the
        // constant one either side of it is an integer too.
        void strict(LSLBinaryExpression* expr)
        {
            const LSLOperator op = expr->getOperation();
            if (!mCosts.strictForInclusive || (op != OP_GEQ && op != OP_LEQ) || !integerOperands(expr))
            {
                return;
            }
            for (int slot = 0; slot < 2; ++slot)
            {
                LSLASTNode*  side = expr->getChild(slot);
                LSLConstant* cv   = side->getConstantValue();
                if (!cv || cv->getNodeSubType() != NODE_INTEGER_CONSTANT || expr->getChild(1 - slot)->getConstantValue())
                {
                    continue;
                }
                // Whether the constant is on the greater side: a >= C, C <= a.
                const bool    below = (op == OP_GEQ) == (slot == 1);
                const int32_t c     = static_cast<LSLIntegerConstant*>(cv)->getValue();
                if ((below && c == INT32_MIN) || (!below && c == INT32_MAX))
                {
                    return;
                }
                const std::string before = report.wanted() ? render(expr) : std::string();
                LSLASTNode::replaceNode(side, constant(ctx.integer(below ? c - 1 : c + 1), side));
                expr->setOperation(op == OP_GEQ ? OP_GREATER : OP_LESS);
                wrote(expr, expr, before);
                return;
            }
        }

        // What an integer comparison or sum comes to in fewer bytes, as a
        // value: x == -1 as !~x, x < 0 as !~x of a find, x + 1 as -~x,
        // x - 1 as ~-x, and two either way.
        void integers(LSLBinaryExpression* expr)
        {
            strict(expr);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (!left || !right || expr->getIType() != LST_INTEGER || left->getIType() != LST_INTEGER || right->getIType() != LST_INTEGER)
            {
                return;
            }
            switch (expr->getOperation())
            {
                case OP_EQ:
                    if (mCosts.notComplementForMinusOne && (isInteger(right, -1) || isInteger(left, -1)))
                    {
                        over(expr, isInteger(right, -1) ? 0 : 1, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_LESS:
                    if (mCosts.notComplementForMinusOne && isInteger(right, 0) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_GREATER:
                    if (mCosts.notComplementForMinusOne && isInteger(left, 0) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_PLUS:
                    for (int slot = 0; slot < 2 && mCosts.negateComplementForIncrement; ++slot)
                    {
                        LSLExpression* other = slot == 0 ? right : left;
                        if (isInteger(other, 1))
                        {
                            over(expr, slot, { OP_BIT_NOT, OP_MINUS });
                            return;
                        }
                        if (isInteger(other, 2))
                        {
                            over(expr, slot, { OP_BIT_NOT, OP_MINUS, OP_BIT_NOT, OP_MINUS });
                            return;
                        }
                    }
                    return;
                case OP_MINUS:
                    if (mCosts.complementNegateForDecrement && isInteger(right, 1))
                    {
                        over(expr, 0, { OP_MINUS, OP_BIT_NOT });
                    }
                    else if (mCosts.complementNegateForDecrement && isInteger(right, 2))
                    {
                        over(expr, 0, { OP_MINUS, OP_BIT_NOT, OP_MINUS, OP_BIT_NOT });
                    }
                    return;
                default:
                    return;
            }
        }

        // What only counts as true or false -- a condition, an operand of
        // !, && or || in one -- in fewer bytes: x != -1 as ~x, and x > -1
        // and x >= 0 as ~x of a find.
        void truth(LSLASTNode* parent, int slot)
        {
            LSLASTNode* child = parent->getChild(slot);
            if (!child || child->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            LSLExpression* inner = bare(static_cast<LSLExpression*>(child));
            if (!inner || inGlobal(inner))
            {
                return;
            }
            if (inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                truth(inner, 0);
                return;
            }
            // A string's length is nothing where the string is.
            if (inner->getNodeSubType() == NODE_FUNCTION_EXPRESSION && mCosts.emptyForLength)
            {
                auto*                          call = static_cast<LSLFunctionExpression*>(inner);
                LSLSymbol*                     sym  = call->getSymbol();
                LSLASTNodeList<LSLExpression>* args = call->getArguments();
                if (sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llStringLength") && args && args->getNumChildren() == 1)
                {
                    const std::string before = report.wanted() ? render(call) : std::string();
                    auto*             text   = static_cast<LSLExpression*>(args->takeChild(0));
                    auto*             made   = ctx.allocator->newTracked<LSLBinaryExpression>(text, OP_NEQ, constant(ctx.string(std::string()), call));
                    made->setType(TYPE(LST_INTEGER));
                    made->setLoc(call->getLoc());
                    putInPlace(call, made, ctx.allocator);
                    wrote(call, made, before);
                }
                return;
            }
            if (inner->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return;
            }
            auto*          expr  = static_cast<LSLBinaryExpression*>(inner);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (expr->getOperation() == OP_BOOLEAN_AND || expr->getOperation() == OP_BOOLEAN_OR)
            {
                truth(expr, 0);
                truth(expr, 1);
                // Either true is the two bits or'd true; LSL runs both
                // sides of || either way.
                if (expr->getOperation() == OP_BOOLEAN_OR && mCosts.bitOrForOr && integerOperands(expr))
                {
                    const std::string before = report.wanted() ? render(expr) : std::string();
                    expr->setOperation(OP_BIT_OR);
                    wrote(expr, expr, before);
                }
                return;
            }
            if (!left || !right || left->getIType() != LST_INTEGER || right->getIType() != LST_INTEGER)
            {
                return;
            }
            if (expr->getOperation() == OP_NEQ && !isInteger(right, -1) && !isInteger(left, -1))
            {
                // Different is the bits differing.
                if (mCosts.xorForNotEqual)
                {
                    const std::string before = report.wanted() ? render(expr) : std::string();
                    expr->setOperation(OP_BIT_XOR);
                    wrote(expr, expr, before);
                }
                return;
            }
            if (!mCosts.complementForNotMinusOne)
            {
                return;
            }
            switch (expr->getOperation())
            {
                case OP_NEQ:
                    if (isInteger(right, -1) || isInteger(left, -1))
                    {
                        over(expr, isInteger(right, -1) ? 0 : 1, { OP_BIT_NOT });
                    }
                    return;
                case OP_GREATER:
                    if (isInteger(right, -1) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT });
                    }
                    return;
                case OP_GEQ:
                    if (isInteger(right, 0) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT });
                    }
                    return;
                case OP_LESS:
                    if (isInteger(left, -1) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT });
                    }
                    return;
                case OP_LEQ:
                    if (isInteger(left, 0) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT });
                    }
                    return;
                default:
                    return;
            }
        }

        // A library call that a cast to a string says the same as:
        // llDumpList2String(l, "") as (string)l, and the one detail
        // llGetObjectDetails gives as a string -- or "", as both say where
        // there is no such object.
        void libraryCast(LSLFunctionExpression* expr)
        {
            LSLSymbol*                     sym  = expr->getSymbol();
            LSLASTNodeList<LSLExpression>* args = expr->getArguments();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !args || args->getNumChildren() != 2)
            {
                return;
            }
            auto*       first  = static_cast<LSLExpression*>(args->getChild(0));
            LSLASTNode* second = args->getChild(1);
            bool        cast   = false;
            if (mCosts.castForDump && !strcmp(sym->getName(), "llDumpList2String"))
            {
                LSLConstant* cv   = second->getConstantValue();
                const bool   pays = !mHad || mHad->castsToString() || mHad->dumps * mCosts.listShapeLeast > mCosts.listHelperMost;
                cast              = pays && cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue();
            }
            else if (mCosts.castForDetail && !strcmp(sym->getName(), "llList2String") && isInteger(second, 0))
            {
                LSLExpression* details = bare(first);
                LSLSymbol*     inner   = details && details->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? details->getSymbol() : nullptr;
                LSLASTNodeList<LSLExpression>* asked = inner ? static_cast<LSLFunctionExpression*>(details)->getArguments() : nullptr;
                LSLASTNode*                    which = asked && asked->getNumChildren() == 2 ? asked->getChild(1) : nullptr;
                LSLExpression*                 named = which && which->getNodeType() == NODE_EXPRESSION ? bare(static_cast<LSLExpression*>(which)) : nullptr;
                // One detail asked for: a literal of one, or that literal as
                // the cast listadd makes of it.
                const bool one = named && ((named->getNodeSubType() == NODE_LIST_EXPRESSION && named->getNumChildren() == 1) ||
                                           (named->getNodeSubType() == NODE_TYPECAST_EXPRESSION && named->getIType() == LST_LIST &&
                                            static_cast<LSLTypecastExpression*>(named)->getChildExpr() &&
                                            static_cast<LSLTypecastExpression*>(named)->getChildExpr()->getIType() != LST_LIST));
                cast = inner && inner->getSubType() == SYM_BUILTIN && !strcmp(inner->getName(), "llGetObjectDetails") && one;
            }
            if (!cast)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            args->takeChild(0);
            auto* made = ctx.allocator->newTracked<LSLTypecastExpression>(TYPE(LST_STRING), first);
            made->setType(TYPE(LST_STRING));
            made->setLoc(expr->getLoc());
            putInPlace(expr, made, ctx.allocator);
            wrote(expr, made, before);
        }

        // Whether a constant is no key a library function would find
        // anything by: NULL_KEY, or anything that is not a key's form.
        static bool nullKey(LSLConstant* cv)
        {
            if (!cv || (cv->getIType() != LST_STRING && cv->getIType() != LST_KEY))
            {
                return false;
            }
            const std::string_view text = cv->getIType() == LST_STRING ? static_cast<LSLStringConstant*>(cv)->getValue()
                                                                        : static_cast<LSLKeyConstant*>(cv)->getValue();
            if (text.size() != 36)
            {
                return true;
            }
            bool zero = true;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
                if (dash ? text[i] != '-' : !isxdigit(static_cast<unsigned char>(text[i])))
                {
                    return true;
                }
                zero = zero && (dash || text[i] == '0');
            }
            return zero;
        }

        // A library function given NULL_KEY, or no key at all, for a key
        // it looks something up by, given "" instead, which it takes the
        // same way; not the two that pass a key on as it came.
        void nullKeys(LSLFunctionExpression* expr)
        {
            LSLSymbol* sym = expr->getSymbol();
            if (!mCosts.emptyForNullKey || !sym || sym->getSubType() != SYM_BUILTIN || !sym->getFunctionDecl() || !expr->getArguments() ||
                !strcmp(sym->getName(), "llMessageLinked") || !strcmp(sym->getName(), "llRemoteDataReply"))
            {
                return;
            }
            std::vector<LSLASTNode*> nulls;
            LSLASTNode*              param = sym->getFunctionDecl()->getChild(0);
            for (LSLASTNode* arg = expr->getArguments()->getChild(0); arg && param; arg = arg->getNext(), param = param->getNext())
            {
                LSLConstant* cv = arg->getConstantValue();
                const bool   empty = cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue();
                if (param->getIType() == LST_KEY && !empty && nullKey(cv))
                {
                    nulls.push_back(arg);
                }
            }
            for (LSLASTNode* arg : nulls)
            {
                const std::string before = report.wanted() ? render(arg) : std::string();
                auto*             made   = constant(ctx.string(std::string()), arg);
                LSLASTNode::replaceNode(arg, made);
                wrote(arg, made, before);
            }
        }

        // x++ and x-- whose value nothing reads, as ++x and --x.
        void pre(LSLASTNode* node)
        {
            if (!mCosts.preForPost || !node || node->getNodeType() != NODE_EXPRESSION || node->getNodeSubType() != NODE_UNARY_EXPRESSION)
            {
                return;
            }
            auto*             expr = static_cast<LSLExpression*>(node);
            const LSLOperator op   = expr->getOperation();
            if (op != OP_POST_INCR && op != OP_POST_DECR)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            expr->setOperation(op == OP_POST_INCR ? OP_PRE_INCR : OP_PRE_DECR);
            wrote(expr, expr, before);
        }

        // A list's element added bare: l + [a, b] as l + a + b, [a] + l as
        // a + l, l + (list)x as l + x, and l += [a] as l += a. What was
        // made, where anything was.
        LSLExpression* elements(LSLBinaryExpression* expr)
        {
            if (!mCosts.elementForList || expr->getIType() != LST_LIST)
            {
                return nullptr;
            }
            LSLExpression*    left  = expr->getLHS();
            LSLExpression*    right = expr->getRHS();
            const LSLOperator op    = expr->getOperation();
            if (!left || !right || (op != OP_PLUS && op != OP_ADD_ASSIGN) || left->getIType() != LST_LIST)
            {
                return nullptr;
            }
            if (LSLExpression* made = appended(expr, left, right, op))
            {
                return made;
            }
            // [a] + l as a + l.
            LSLExpression* literal = bare(left);
            if (op == OP_PLUS && right->getIType() == LST_LIST && literal->getNodeSubType() == NODE_LIST_EXPRESSION &&
                literal->getNumChildren() == 1 && literal->getChild(0)->getIType() != LST_LIST)
            {
                const std::string before = report.wanted() ? render(expr) : std::string();
                LSLASTNode*       a      = literal->takeChild(0);
                literal->removeChild(literal->getChild(0));
                LSLASTNode::replaceNode(left, a);
                wrote(expr, expr, before);
                return expr;
            }
            return nullptr;
        }

        // A list with elements added to it: those elements bare.
        LSLExpression* appended(LSLBinaryExpression* expr, LSLExpression* left, LSLExpression* right, LSLOperator op)
        {
            LSLExpression* added = bare(right);
            // l + (list)x as l + x.
            if (op == OP_PLUS && added->getNodeSubType() == NODE_TYPECAST_EXPRESSION && added->getIType() == LST_LIST)
            {
                LSLExpression* x = static_cast<LSLTypecastExpression*>(added)->getChildExpr();
                if (!x || x->getIType() == LST_LIST)
                {
                    return nullptr;
                }
                const std::string before = report.wanted() ? render(expr) : std::string();
                added->takeChild(0);
                LSLASTNode::replaceNode(right, x);
                wrote(expr, expr, before);
                return expr;
            }
            if (added->getNodeSubType() != NODE_LIST_EXPRESSION || added->getNumChildren() == 0)
            {
                return nullptr;
            }
            const size_t count = added->getNumChildren();
            if ((op == OP_ADD_ASSIGN && count != 1) || (mCosts.elementsForListMost > 0 && count > size_t(mCosts.elementsForListMost)))
            {
                return nullptr;
            }
            // Each element's order as a sum's -- last first -- against a
            // literal's, which is known to nobody: as for listadd.
            size_t changing = 0;
            size_t varying  = 0;
            for (LSLASTNode* child : *added)
            {
                if (child->getIType() == LST_LIST || child->getIType() == LST_ERROR)
                {
                    return nullptr;
                }
                changing += sideEffectFree(child) ? 0 : 1;
                varying += child->getConstantValue() ? 0 : 1;
            }
            if (changing > 0 && varying > 1)
            {
                return nullptr;
            }
            const std::string           before = report.wanted() ? render(expr) : std::string();
            std::vector<LSLExpression*> terms;
            while (added->hasChildren())
            {
                terms.push_back(static_cast<LSLExpression*>(added->takeChild(0)));
                added->removeChild(added->getChild(0));
            }
            if (op == OP_ADD_ASSIGN)
            {
                LSLASTNode::replaceNode(right, terms.front());
                wrote(expr, expr, before);
                return expr;
            }
            // l + a + b, each on the right of its own +.
            auto*          l   = static_cast<LSLExpression*>(expr->takeChild(0));
            LSLExpression* sum = l;
            {
                const Uncounted uncounted(*ctx.context);
                for (LSLExpression* term : terms)
                {
                    sum = uncounted.made(ctx.allocator->newTracked<LSLBinaryExpression>(sum, OP_PLUS, uncounted.made(bracketed(term))));
                    sum->setType(TYPE(LST_LIST));
                    sum->setLoc(term->getLoc());
                }
            }
            sum->setLoc(expr->getLoc());
            putInPlace(expr, sum, ctx.allocator);
            wrote(expr, sum, before);
            return sum;
        }

        // An element of a list as a sum has it, after the first: on the
        // right of a +, which binds tighter than every operator but a
        // product and what is unary, and so bracketed where it is any
        // other. (The first goes under the cast, which the printer
        // brackets for itself.)
        LSLExpression* bracketed(LSLExpression* element)
        {
            bool bare = false;
            switch (element->getNodeSubType())
            {
                case NODE_CONSTANT_EXPRESSION:
                case NODE_LVALUE_EXPRESSION:
                case NODE_PARENTHESIS_EXPRESSION:
                case NODE_FUNCTION_EXPRESSION:
                case NODE_PRINT_EXPRESSION:
                case NODE_VECTOR_EXPRESSION:
                case NODE_QUATERNION_EXPRESSION:
                case NODE_LIST_EXPRESSION:
                    bare = true;
                    break;
                case NODE_UNARY_EXPRESSION:
                case NODE_TYPECAST_EXPRESSION:
                    bare = true;
                    break;
                case NODE_BINARY_EXPRESSION:
                    bare = element->getOperation() == OP_MUL || element->getOperation() == OP_DIV || element->getOperation() == OP_MOD;
                    break;
                default:
                    break;
            }
            if (bare)
            {
                return element;
            }
            auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(element);
            parens->setType(element->getType());
            parens->setLoc(element->getLoc());
            return parens;
        }
    };

    // What can never run, and what is never used.
    class DeadCode : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION && global->getSymbol())
                {
                    mFunctions[global->getSymbol()] = static_cast<LSLGlobalFunction*>(global);
                }
            }
            script->visit(this);
            globals(script);
            states(script);
            unreached(script);
            writeOnly(script);
            emptyHandlers(script);
            return changes;
        }

        bool visit(LSLCompoundStatement* block) override
        {
            visitChildren(block);
            bool                     dead = false;
            std::vector<LSLASTNode*> going;
            // Removed for a reason, said; or silently, with nothing.
            const auto               go = [&](LSLASTNode* stmt, const char* key, const char* why) {
                if (key)
                {
                    // What went, as far as its first line and a few words
                    // of it: a note is read beside the line, not in place
                    // of the code.
                    std::string said = render(stmt);
                    const size_t cut = std::min(said.find('\n'), size_t(60));
                    if (cut < said.size())
                    {
                        said = said.substr(0, cut) + "\xE2\x80\xA6";
                    }
                    report.note(stmt->getLoc(), key, std::string("removed [1]") + why, { said });
                }
                going.push_back(stmt);
            };
            for (LSLASTNode* stmt : *block)
            {
                if (stmt->getNodeSubType() == NODE_LABEL)
                {
                    dead = false;
                    if (unusedLabel(stmt))
                    {
                        go(stmt, nullptr, "");
                    }
                    continue;
                }
                if (dead)
                {
                    // A label in it that a jump goes to makes it reachable,
                    // and what follows it: a jump in SL goes to the last
                    // label of its name in the function, whatever block
                    // that is in.
                    if (!holdsLiveLabel(stmt))
                    {
                        go(stmt, "OptimizerRemovedUnreachable", ", which can never run");
                        continue;
                    }
                    dead = false;
                }
                switch (stmt->getNodeSubType())
                {
                    case NODE_JUMP_STATEMENT:
                        if (jumpsToNext(stmt))
                        {
                            go(stmt, "OptimizerRemovedJump", ", which goes where running on goes");
                            break;
                        }
                        dead = true;
                        break;
                    case NODE_RETURN_STATEMENT:
                    case NODE_STATE_STATEMENT:
                        dead = true;
                        break;
                    case NODE_IF_STATEMENT:
                    case NODE_WHILE_STATEMENT:
                    case NODE_DO_STATEMENT:
                    case NODE_FOR_STATEMENT:
                        // An if none of whose ways goes on, a loop that
                        // never ends -- but for a jump out, whose label
                        // keeps what follows it (above).
                        dead = stops(stmt);
                        break;
                    case NODE_NOP_STATEMENT:
                        go(stmt, nullptr, "");
                        break;
                    case NODE_COMPOUND_STATEMENT:
                        if (!stmt->hasChildren())
                        {
                            go(stmt, nullptr, "");
                        }
                        break;
                    case NODE_EXPRESSION_STATEMENT:
                    {
                        LSLExpression* expr = static_cast<LSLExpressionStatement*>(stmt)->getExpr();
                        if (changesNothing(expr) || callsNothing(expr))
                        {
                            go(stmt, "OptimizerRemovedNoEffect", ", which does nothing");
                        }
                        dead = stops(stmt);
                        break;
                    }
                    case NODE_DECLARATION:
                        if (unusedLocal(static_cast<LSLDeclaration*>(stmt)))
                        {
                            go(stmt, nullptr, "");
                        }
                        break;
                    default:
                        break;
                }
            }
            for (LSLASTNode* stmt : going)
            {
                block->removeChild(stmt);
                ++changes;
            }
            flattenBlocks(block);
            sinkDeclarations(block);
            substituteLocals(block);
            return false;
        }

        // How many identifiers of each name a function or event has,
        // counted once a run: what the pass takes away only makes a count
        // more than there are, never fewer.
        const boost::unordered_flat_map<std::string_view, int>& namesIn(LSLASTNode* node)
        {
            LSLASTNode* callable = node;
            while (callable && callable->getNodeType() != NODE_GLOBAL_FUNCTION && callable->getNodeType() != NODE_EVENT_HANDLER)
            {
                callable = callable->getParent();
            }
            auto& counted = mNames[callable];
            if (counted.empty() && callable)
            {
                std::vector<LSLASTNode*> stack{ callable };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                    {
                        ++counted[static_cast<LSLIdentifier*>(n)->getName()];
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
            }
            return counted;
        }

        // A block standing as a statement of a block, its statements put
        // in its place: where it declares nothing and holds no label, or
        // only names no identifier outside it has -- LSL has no shadowing,
        // and a name it declared would otherwise be one a later declaration
        // shadows, or a global a later read meant; and two labels of a name
        // may not stand in one block.
        void flattenBlocks(LSLCompoundStatement* block)
        {
            const auto flat = [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() != NODE_COMPOUND_STATEMENT)
                {
                    return false;
                }
                boost::unordered_flat_map<std::string_view, int> own;
                std::vector<const char*>                        declared;
                std::vector<LSLASTNode*>                        stack{ inner };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                    {
                        ++own[static_cast<LSLIdentifier*>(n)->getName()];
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                // What it declares, and the labels it holds: two labels of a
                // name may not stand in one block.
                for (LSLASTNode* stmt = inner->getChild(0); stmt; stmt = stmt->getNext())
                {
                    if (stmt->getNodeSubType() == NODE_DECLARATION)
                    {
                        declared.push_back(static_cast<LSLDeclaration*>(stmt)->getIdentifier()->getName());
                    }
                    else if (stmt->getNodeSubType() == NODE_LABEL)
                    {
                        declared.push_back(static_cast<LSLLabel*>(stmt)->getIdentifier()->getName());
                    }
                }
                const auto& all = namesIn(block);
                for (const char* name : declared)
                {
                    const auto total = all.find(std::string_view(name));
                    if (!name || total == all.end() || total->second != own[std::string_view(name)])
                    {
                        return false;
                    }
                }
                return true;
            };
            std::vector<LSLASTNode*> statements;
            bool                     any = false;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
                any = any || flat(stmt);
            }
            if (!any)
            {
                return;
            }
            std::vector<bool> flatten(statements.size());
            for (size_t i = 0; i < statements.size(); ++i)
            {
                flatten[i] = flat(statements[i]);
            }
            // The statements are the ones they were, every reference in
            // them still in the script: counted neither out nor in again.
            const Uncounted uncounted(*ctx.context);
            for (LSLASTNode* stmt : statements)
            {
                block->removeChild(stmt);
            }
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (!flatten[i])
                {
                    block->pushChild(statements[i]);
                    continue;
                }
                std::vector<LSLASTNode*> inner;
                for (LSLASTNode* stmt = statements[i]->getChild(0); stmt; stmt = stmt->getNext())
                {
                    inner.push_back(stmt);
                }
                for (LSLASTNode* stmt : inner)
                {
                    statements[i]->removeChild(stmt);
                    block->pushChild(stmt);
                }
                ++changes;
            }
        }

        // `T x;` and, later in the block, `x = e;` with nothing between that
        // names x, jumps or is jumped to: the declaration where the
        // assignment was, `T x = e;` -- which the local's value may then
        // go further from.
        void sinkDeclarations(LSLCompoundStatement* block)
        {
            std::vector<LSLASTNode*> statements;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
            }
            const auto mentions = [](LSLASTNode* root, LSLSymbol* sym) {
                std::vector<LSLASTNode*> stack{ root };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym)
                    {
                        return true;
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                return false;
            };
            const auto jumpy = [](LSLASTNode* root) {
                std::vector<LSLASTNode*> stack{ root };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_STATEMENT && (n->getNodeSubType() == NODE_LABEL || n->getNodeSubType() == NODE_JUMP_STATEMENT))
                    {
                        return true;
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                return false;
            };
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (statements[i]->getNodeSubType() != NODE_DECLARATION)
                {
                    continue;
                }
                auto*      decl = static_cast<LSLDeclaration*>(statements[i]);
                LSLSymbol* sym  = decl->getSymbol();
                LSLASTNode* init = decl->getChild(1);
                if (!sym || sym->getSubType() != SYM_LOCAL || (init && init->getNodeType() != NODE_NULL))
                {
                    continue;
                }
                for (size_t j = i + 1; j < statements.size(); ++j)
                {
                    LSLASTNode* stmt = statements[j];
                    if (!mentions(stmt, sym))
                    {
                        if (jumpy(stmt))
                        {
                            break;
                        }
                        continue;
                    }
                    auto* expr = stmt->getNodeSubType() == NODE_EXPRESSION_STATEMENT ? static_cast<LSLExpressionStatement*>(stmt)->getExpr() : nullptr;
                    if (!expr || expr->getNodeSubType() != NODE_BINARY_EXPRESSION || expr->getOperation() != '=' ||
                        expr->getChild(0)->getNodeSubType() != NODE_LVALUE_EXPRESSION || static_cast<LSLLValueExpression*>(expr->getChild(0))->getMember() ||
                        static_cast<LSLLValueExpression*>(expr->getChild(0))->getSymbol() != sym || mentions(expr->getChild(1), sym))
                    {
                        break;
                    }
                    decl->setChild(1, static_cast<LSLExpression*>(expr->takeChild(1)));
                    block->removeChild(decl);
                    LSLASTNode::replaceNode(stmt, decl);
                    statements[j] = decl;
                    ++changes;
                    break;
                }
            }
        }

        // How many labels of a name the function or event a node is in has.
        // A jump goes to the last of them; the compiler finds it among
        // those in scope.
        static int labelsNamed(LSLASTNode* node, const char* name)
        {
            LSLASTNode* callable = node;
            while (callable && callable->getNodeType() != NODE_GLOBAL_FUNCTION && callable->getNodeType() != NODE_EVENT_HANDLER)
            {
                callable = callable->getParent();
            }
            int                                    named = 0;
            const std::function<void(LSLASTNode*)> count = [&](LSLASTNode* n) {
                if (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL && !strcmp(static_cast<LSLLabel*>(n)->getIdentifier()->getName(), name))
                {
                    ++named;
                }
                for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                {
                    count(child);
                }
            };
            if (callable)
            {
                count(callable);
            }
            return named;
        }

        // Whether a jump goes where running on would go anyway: its label
        // is the next statement to run -- after the jump, or after the
        // blocks and the if branches it is the end of -- and the only
        // label of its name in the function, a jump going to the last of
        // a name.
        static bool jumpsToNext(LSLASTNode* jump)
        {
            LSLIdentifier* id     = static_cast<LSLJumpStatement*>(jump)->getIdentifier();
            const char*    target = id ? id->getName() : nullptr;
            if (!target)
            {
                return false;
            }
            const auto empty = [](LSLASTNode* n) {
                return n->getNodeSubType() == NODE_NOP_STATEMENT || (n->getNodeSubType() == NODE_COMPOUND_STATEMENT && !n->hasChildren());
            };
            LSLASTNode* at = jump;
            while (true)
            {
                LSLASTNode* parent = at->getParent();
                if (!parent || parent->getNodeType() != NODE_STATEMENT)
                {
                    return false;
                }
                if (parent->getNodeSubType() == NODE_IF_STATEMENT && at != static_cast<LSLIfStatement*>(parent)->getCheckExpr())
                {
                    at = parent;
                    continue;
                }
                if (parent->getNodeSubType() != NODE_COMPOUND_STATEMENT)
                {
                    return false;
                }
                LSLASTNode* next = at->getNext();
                while (next && empty(next))
                {
                    next = next->getNext();
                }
                if (!next)
                {
                    at = parent;
                    continue;
                }
                if (next->getNodeSubType() != NODE_LABEL || strcmp(static_cast<LSLLabel*>(next)->getIdentifier()->getName(), target))
                {
                    return false;
                }
                // The only label of the name in the function or event.
                return labelsNamed(next, target) == 1;
            }
        }

        // A local set once from what changes nothing and is read the same
        // at any time, then read once in the expression of a later
        // statement of the same block: its value put where it is read, and
        // the local gone -- where no loop reads it again and no label
        // could bring the run in between, and nothing that runs between
        // writes what the value reads. A parameter a function was put in
        // place with, or the variable its value was set in, most often.
        void substituteLocals(LSLCompoundStatement* block)
        {
            // The block's locals that may go -- declared here from what
            // changes nothing, never written, read once -- and, in one walk
            // of what follows, where each is read.
            std::vector<LSLASTNode*>                                                          statements;
            boost::unordered_flat_map<LSLSymbol*, std::pair<size_t, LSLLValueExpression*>> reads;
            for (LSLASTNode* stmt : *block)
            {
                if (stmt->getNodeSubType() == NODE_DECLARATION)
                {
                    auto*      decl = static_cast<LSLDeclaration*>(stmt);
                    LSLSymbol* sym  = decl->getSymbol();
                    if (sym && sym->getSubType() == SYM_LOCAL && sym->getAssignments() == 0 && sym->getReferences() == 2 && decl->getInitializer() &&
                        decl->getInitializer()->getNodeType() == NODE_EXPRESSION)
                    {
                        reads.emplace(sym, std::make_pair(size_t(-1), nullptr));
                    }
                }
                statements.push_back(stmt);
            }
            if (reads.empty())
            {
                return;
            }
            std::vector<LSLASTNode*> stack;
            for (size_t k = 0; k < statements.size(); ++k)
            {
                stack.assign(1, statements[k]);
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        const auto found = reads.find(static_cast<LSLLValueExpression*>(n)->getSymbol());
                        if (found != reads.end() && !found->second.second)
                        {
                            found->second = { k, static_cast<LSLLValueExpression*>(n) };
                        }
                    }
                    // A statement's own expressions only: a read in one it
                    // holds is not one a value may be put in, and that
                    // statement's own block walks it.
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        if (child->getNodeType() != NODE_STATEMENT)
                        {
                            stack.push_back(child);
                        }
                    }
                }
            }
            const auto holdsLabel = [](LSLASTNode* root) {
                bool                                   found = false;
                const std::function<void(LSLASTNode*)> look  = [&](LSLASTNode* n) {
                    found = found || (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL);
                    for (LSLASTNode* child = n->getChild(0); child && !found; child = child->getNext())
                    {
                        look(child);
                    }
                };
                look(root);
                return found;
            };
            std::vector<LSLDeclaration*> gone;
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (statements[i]->getNodeSubType() != NODE_DECLARATION)
                {
                    continue;
                }
                auto*          decl  = static_cast<LSLDeclaration*>(statements[i]);
                LSLSymbol*     sym   = decl->getSymbol();
                LSLExpression* value = decl->getInitializer();
                const auto     read  = sym ? reads.find(sym) : reads.end();
                if (read == reads.end() || !value || !sideEffectFree(value) || value->getIType() != sym->getIType())
                {
                    continue;
                }
                // The read, and the statement it is in: one after this.
                const size_t         at  = read->second.first;
                LSLLValueExpression* use = read->second.second;
                if (!use || at <= i || use->getMember())
                {
                    continue;
                }
                // In the statement's own expression: no statement, and so
                // no loop or branch, between it and the read.
                bool direct = true;
                for (LSLASTNode* up = use->getParent(); up && up != statements[at]; up = up->getParent())
                {
                    direct = direct && up->getNodeType() != NODE_STATEMENT;
                }
                const LSLNodeSubType shape = statements[at]->getNodeSubType();
                if (!direct || shape == NODE_WHILE_STATEMENT || shape == NODE_DO_STATEMENT || shape == NODE_FOR_STATEMENT)
                {
                    continue;
                }
                bool                  labelled = false;
                ALLSLEffects::Writes  between;
                for (size_t k = i + 1; k < at; ++k)
                {
                    labelled = labelled || holdsLabel(statements[k]);
                    between.add(ctx.effects->of(statements[k]));
                }
                for (LSLASTNode* earlier : ALLSLEffects::before(statements[at], use))
                {
                    between.add(ctx.effects->of(earlier));
                }
                bool written = false;
                const std::function<void(LSLASTNode*)> reads = [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        written = written || between.writes(static_cast<LSLLValueExpression*>(n)->getSymbol());
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        reads(child);
                    }
                };
                reads(value);
                if (labelled || written)
                {
                    continue;
                }
                report.note(decl->getLoc(), "OptimizerSubstitutedLocal", "put the value of [1] where it is read, and removed it", { sym->getName() });
                putInPlace(use, static_cast<LSLExpression*>(decl->takeChild(1)), ctx.allocator);
                gone.push_back(decl);
                ++changes;
            }
            for (LSLDeclaration* decl : gone)
            {
                block->removeChild(decl);
            }
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            LSLConstant* cv = stmt->getCheckExpr() ? stmt->getCheckExpr()->getConstantValue() : nullptr;
            if (!cv || cv->getNodeSubType() != NODE_INTEGER_CONSTANT)
            {
                return false;
            }
            const bool    taken  = static_cast<LSLIntegerConstant*>(cv)->getValue() != 0;
            LSLStatement* branch = taken ? stmt->getTrueBranch() : stmt->getFalseBranch();
            // A branch never taken is reached all the same by a jump to a
            // label in it.
            if (LSLStatement* other = taken ? stmt->getFalseBranch() : stmt->getTrueBranch(); other && holdsLiveLabel(other))
            {
                return false;
            }
            if (branch && lastIfOfStateChange(stmt, branch))
            {
                return false;
            }
            report.note(stmt->getLoc(), taken ? "OptimizerIfAlwaysTrue" : "OptimizerIfAlwaysFalse",
                        taken ? "the condition of this if is always true; kept only what runs" : "the condition of this if is always false; kept only what runs");
            replaceStatement(stmt, branch ? static_cast<LSLStatement*>(stmt->takeChild(taken ? 1 : 2)) : nullptr);
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0) && !holdsLiveLabel(stmt))
            {
                report.note(stmt->getLoc(), "OptimizerRemovedWhile", "removed a while loop whose condition is always false");
                replaceStatement(stmt, nullptr);
            }
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0))
            {
                report.note(stmt->getLoc(), "OptimizerDoRunsOnce", "a do loop whose condition is always false runs once; kept its body");
                replaceStatement(stmt, static_cast<LSLStatement*>(stmt->takeChild(0)));
            }
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0) && !holdsLiveLabel(stmt))
            {
                // The initialisers still run, once.
                auto* block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
                block->setLoc(stmt->getLoc());
                LSLASTNode* init = stmt->getInitExprs();
                while (init && init->hasChildren())
                {
                    auto* expr = static_cast<LSLExpression*>(init->takeChild(0));
                    init->removeChild(init->getChild(0));
                    auto* es = ctx.allocator->newTracked<LSLExpressionStatement>(expr);
                    es->setLoc(expr->getLoc());
                    block->pushChild(es);
                }
                report.note(stmt->getLoc(), "OptimizerRemovedFor", "removed a for loop whose condition is always false; its initialisers stay");
                replaceStatement(stmt, block->hasChildren() ? block : nullptr);
            }
            return false;
        }

    private:
        // A statement becomes another, or nothing: nothing is an empty
        // statement where a branch or a body needs one.
        void replaceStatement(LSLStatement* old, LSLStatement* now)
        {
            LSLASTNode* parent = old->getParent();
            if (!now)
            {
                if (parent && parent->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                {
                    parent->removeChild(old);
                    ++changes;
                    return;
                }
                now = ctx.allocator->newTracked<LSLNopStatement>();
                now->setLoc(old->getLoc());
            }
            LSLASTNode::replaceNode(old, now);
            ++changes;
        }

        // A call to a function of the script's own whose body is empty,
        // with arguments that do nothing on their own.
        bool callsNothing(LSLExpression* expr) const
        {
            if (!expr || expr->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym      = expr->getSymbol();
            auto       function = sym ? mFunctions.find(sym) : mFunctions.end();
            if (function == mFunctions.end())
            {
                return false;
            }
            LSLASTNode* body = function->second->getStatements();
            if (!body || body->getNodeSubType() != NODE_COMPOUND_STATEMENT || body->hasChildren())
            {
                return false;
            }
            for (LSLASTNode* arg : *static_cast<LSLFunctionExpression*>(expr)->getArguments())
            {
                if (!changesNothing(arg))
                {
                    return false;
                }
            }
            return true;
        }

        // Whether a label a jump from outside a statement goes to is in
        // it, however far down: the one way in to a statement nothing
        // before it reaches. A jump from inside to a label inside -- a
        // loop made of labels -- is no way in.
        static bool holdsLiveLabel(LSLASTNode* statement)
        {
            // Each label's references are its own name and every jump to
            // it; the jumps inside the statement are taken off.
            boost::unordered_flat_map<LSLSymbol*, int> inside;
            std::vector<LSLSymbol*>                    labels;
            const std::function<void(LSLASTNode*)> gather = [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_LABEL)
                {
                    if (LSLSymbol* sym = node->getSymbol())
                    {
                        labels.push_back(sym);
                    }
                }
                else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_JUMP_STATEMENT)
                {
                    if (LSLSymbol* sym = static_cast<LSLJumpStatement*>(node)->getIdentifier()->getSymbol())
                    {
                        ++inside[sym];
                    }
                }
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    gather(child);
                }
            };
            gather(statement);
            for (LSLSymbol* label : labels)
            {
                if (label->getReferences() - 1 > inside[label])
                {
                    return true;
                }
            }
            return false;
        }

        // Whether taking `stmt` away for its branch would leave a state
        // change in a function with no `if` around it: a function may
        // change state only under an `if`, which the compiler checks
        // (Tailslide's E_CHANGE_STATE_IN_FUNCTION), whatever the `if`
        // tests. An event may change state anywhere.
        static bool lastIfOfStateChange(LSLIfStatement* stmt, LSLStatement* branch)
        {
            for (LSLASTNode* up = stmt->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_STATE || (up->getNodeType() == NODE_STATEMENT && up->getNodeSubType() == NODE_IF_STATEMENT))
                {
                    return false;
                }
                if (up->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    break;
                }
            }
            bool orphaned = false;
            const std::function<void(LSLASTNode*, bool)> look = [&](LSLASTNode* node, bool underIf) {
                if (orphaned || !node)
                {
                    return;
                }
                if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT && !underIf)
                {
                    orphaned = true;
                    return;
                }
                const bool isIf = node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_IF_STATEMENT;
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    look(child, underIf || isIf);
                }
            };
            look(branch, false);
            return orphaned;
        }

        bool unusedLabel(LSLASTNode* label)
        {
            LSLSymbol* sym = label->getSymbol();
            if (!sym || sym->getReferences() > 1)
            {
                return false;
            }
            // Another label of its name, and a jump the compiler finds this
            // one for, runs to the last: the count of this one's own jumps
            // does not say it is unused.
            if (labelsNamed(label, static_cast<LSLLabel*>(label)->getIdentifier()->getName()) > 1)
            {
                return false;
            }
            report.note(label->getLoc(), "OptimizerRemovedLabel", "removed the label [1], which nothing jumps to", { render(label) });
            return true;
        }

        bool unusedLocal(LSLDeclaration* decl)
        {
            LSLSymbol* sym = decl->getSymbol();
            if (!sym || sym->getReferences() != 1 || sym->getAssignments() != 0)
            {
                return false;
            }
            LSLASTNode* init = decl->getInitializer();
            if (init && !changesNothing(init))
            {
                return false;
            }
            forget(decl, sym);
            report.note(decl->getLoc(), "OptimizerRemovedLocal", "removed the unused local [1]", { sym->getName() });
            return true;
        }

        static void forget(LSLASTNode* node, LSLSymbol* sym)
        {
            for (LSLASTNode* up = node; up; up = up->getParent())
            {
                if (up->getSymbolTable() && up->getSymbolTable()->remove(sym))
                {
                    return;
                }
            }
        }

        void globals(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* global : *script->getGlobals())
            {
                LSLSymbol* sym = global->getSymbol();
                if (!sym || sym->getReferences() != 1)
                {
                    continue;
                }
                if (global->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    report.note(global->getLoc(), "OptimizerRemovedGlobal", "removed the unused global [1]", { sym->getName() });
                    going.push_back(global);
                }
                else if (global->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    report.note(global->getLoc(), "OptimizerRemovedFunction", "removed the unused function [1]", { sym->getName() });
                    going.push_back(global);
                }
            }
            for (LSLASTNode* global : going)
            {
                script->getSymbolTable()->remove(global->getSymbol());
                script->getGlobals()->removeChild(global);
                ++changes;
            }
        }

        boost::unordered_flat_map<LSLSymbol*, LSLGlobalFunction*> mFunctions;
        boost::unordered_flat_map<LSLASTNode*, boost::unordered_flat_map<std::string_view, int>> mNames;

        // Whether what follows a statement never runs by running on from it:
        // it returns, changes state or jumps; resets the script; is an if
        // every way of which does so, or a loop that never ends. A label
        // jumped to after it is the dead code's own business (above).
        static bool stops(LSLASTNode* stmt)
        {
            if (!stmt || stmt->getNodeType() != NODE_STATEMENT)
            {
                return false;
            }
            const auto forever = [](LSLExpression* check) {
                LSLConstant* cv = check ? check->getConstantValue() : nullptr;
                return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(cv)->getValue() != 0;
            };
            switch (stmt->getNodeSubType())
            {
                case NODE_RETURN_STATEMENT:
                case NODE_STATE_STATEMENT:
                case NODE_JUMP_STATEMENT:
                    return true;
                case NODE_EXPRESSION_STATEMENT:
                {
                    LSLExpression* expr = bare(static_cast<LSLExpressionStatement*>(stmt)->getExpr());
                    LSLSymbol*     sym  = expr && expr->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? expr->getSymbol() : nullptr;
                    return sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llResetScript");
                }
                case NODE_COMPOUND_STATEMENT:
                {
                    LSLASTNode* last = nullptr;
                    for (LSLASTNode* child = stmt->getChild(0); child; child = child->getNext())
                    {
                        last = child;
                    }
                    return stops(last);
                }
                case NODE_IF_STATEMENT:
                {
                    auto* branch = static_cast<LSLIfStatement*>(stmt);
                    return branch->getFalseBranch() && stops(branch->getTrueBranch()) && stops(branch->getFalseBranch());
                }
                case NODE_WHILE_STATEMENT:
                    return forever(static_cast<LSLWhileStatement*>(stmt)->getCheckExpr());
                case NODE_DO_STATEMENT:
                    return forever(static_cast<LSLDoStatement*>(stmt)->getCheckExpr());
                case NODE_FOR_STATEMENT:
                    return forever(static_cast<LSLForStatement*>(stmt)->getCheckExpr());
                default:
                    return false;
            }
        }

        // Every node of a subtree, the root first.
        template <class F> static void each(LSLASTNode* root, const F& f)
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

        // What can run: the default state's handlers, each function they
        // call and each state they enter, and on through those. A function
        // or a state nothing that runs reaches goes, though others that do
        // not run reach it -- functions that call only each other.
        void unreached(LSLScript* script)
        {
            boost::unordered_flat_map<LSLSymbol*, LSLASTNode*> states;
            LSLASTNode*                                        start = nullptr;
            for (LSLASTNode* state : *script->getStates())
            {
                if (LSLSymbol* sym = state->getSymbol())
                {
                    states[sym] = state;
                    start       = !strcmp(sym->getName(), "default") ? state : start;
                }
            }
            if (!start)
            {
                return;
            }
            boost::unordered_flat_set<LSLASTNode*> reached{ start };
            std::vector<LSLASTNode*>               todo{ start };
            const auto                             reach = [&](LSLASTNode* node) {
                if (node && reached.insert(node).second)
                {
                    todo.push_back(node);
                }
            };
            while (!todo.empty())
            {
                LSLASTNode* from = todo.back();
                todo.pop_back();
                each(from, [&](LSLASTNode* node) {
                    if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
                    {
                        LSLSymbol* sym   = static_cast<LSLFunctionExpression*>(node)->getSymbol();
                        const auto found = sym ? mFunctions.find(sym) : mFunctions.end();
                        reach(found == mFunctions.end() ? nullptr : found->second);
                    }
                    else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT)
                    {
                        LSLIdentifier* id    = static_cast<LSLStateStatement*>(node)->getIdentifier();
                        const auto     found = id && id->getSymbol() ? states.find(id->getSymbol()) : states.end();
                        reach(found == states.end() ? nullptr : found->second);
                    }
                });
            }
            for (LSLASTNode* list : { static_cast<LSLASTNode*>(script->getGlobals()), static_cast<LSLASTNode*>(script->getStates()) })
            {
                std::vector<LSLASTNode*> going;
                for (LSLASTNode* node = list->getChild(0); node; node = node->getNext())
                {
                    const bool function = node->getNodeType() == NODE_GLOBAL_FUNCTION;
                    if ((function || node->getNodeType() == NODE_STATE) && node->getSymbol() && !reached.contains(node))
                    {
                        report.note(node->getLoc(), function ? "OptimizerRemovedUncalled" : "OptimizerRemovedUnentered",
                                    function ? "removed the function [1], which nothing that runs calls" : "removed the state [1], which nothing that runs enters",
                                    { node->getSymbol()->getName() });
                        going.push_back(node);
                    }
                }
                for (LSLASTNode* node : going)
                {
                    script->getSymbolTable()->remove(node->getSymbol());
                    list->removeChild(node);
                    ++changes;
                }
            }
        }

        // A handler with nothing in it goes where having it changes
        // nothing: not a touch's, which makes the object touchable, nor a
        // collision's, money's or control's, whose handler changes what the
        // object does. A state keeps one handler, which it must have.
        void emptyHandlers(LSLScript* script)
        {
            static constexpr std::string_view QUIET[] = { "state_entry", "state_exit", "on_rez", "attach", "changed", "timer", "listen", "link_message",
                                                          "dataserver", "http_response", "sensor", "no_sensor", "at_target", "not_at_target",
                                                          "at_rot_target", "not_at_rot_target", "moving_start", "moving_end", "object_rez",
                                                          "email", "remote_data", "run_time_permissions", "linkset_data", "transaction_result",
                                                          "path_update", "experience_permissions", "experience_permissions_denied" };
            for (LSLASTNode* state = script->getStates()->getChild(0); state; state = state->getNext())
            {
                LSLASTNode* handlers = state->getChild(1);
                if (!handlers)
                {
                    continue;
                }
                std::vector<LSLASTNode*> going;
                size_t                   count = 0;
                for (LSLASTNode* handler = handlers->getChild(0); handler; handler = handler->getNext())
                {
                    ++count;
                    auto*         event = static_cast<LSLEventHandler*>(handler);
                    LSLStatement* body  = event->getStatements();
                    LSLSymbol*    sym   = event->getSymbol();
                    if (sym && body && body->getNodeSubType() == NODE_COMPOUND_STATEMENT && !body->hasChildren() &&
                        std::find(std::begin(QUIET), std::end(QUIET), std::string_view(sym->getName())) != std::end(QUIET))
                    {
                        going.push_back(handler);
                    }
                }
                if (going.size() == count)
                {
                    going.pop_back();
                }
                for (LSLASTNode* handler : going)
                {
                    report.note(handler->getLoc(), "OptimizerRemovedEmptyHandler", "removed the empty handler of [1], which having changes nothing",
                                { handler->getSymbol()->getName() });
                    handlers->removeChild(handler);
                    ++changes;
                }
            }
        }

        // Whether an assignment's or an increment's value goes unread: it is
        // a statement of its own, or a for's first part or step.
        static bool unread(LSLASTNode* expr)
        {
            LSLASTNode* up = expr->getParent();
            if (up && up->getNodeType() == NODE_STATEMENT && up->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
            {
                return true;
            }
            LSLASTNode* loop = up ? up->getParent() : nullptr;
            return up && up->getNodeType() == NODE_AST_NODE_LIST && loop && loop->getNodeType() == NODE_STATEMENT &&
                   loop->getNodeSubType() == NODE_FOR_STATEMENT &&
                   (up == static_cast<LSLForStatement*>(loop)->getInitExprs() || up == static_cast<LSLForStatement*>(loop)->getIncrExprs());
        }

        // A statement taken out: from its block or its for, or made nothing
        // where it is the whole of an if's or a loop's body.
        void takeOut(LSLASTNode* node)
        {
            LSLASTNode* up = node->getParent();
            if (up && (up->getNodeSubType() == NODE_COMPOUND_STATEMENT || up->getNodeType() == NODE_AST_NODE_LIST))
            {
                up->removeChild(node);
                return;
            }
            LSLASTNode::replaceNode(node, ctx.allocator->newTracked<LSLNopStatement>());
        }

        // A variable -- a local, a global -- set and never read goes, and
        // each place it is set keeps what setting it ran: `x = f();` is
        // `f();`, `x++;` nothing, and `integer h = llListen(...);` the
        // call. Only where every place that names it but its declaration
        // sets it, and nothing reads what was set.
        void writeOnly(LSLScript* script)
        {
            struct Uses
            {
                LSLASTNode*              declaration = nullptr;
                std::vector<LSLASTNode*> writes;
                bool                     read = false;
            };
            boost::unordered_flat_map<LSLSymbol*, Uses> uses;
            std::vector<LSLSymbol*>                     order;
            const auto                                  of = [&](LSLSymbol* sym) -> Uses& {
                auto [it, fresh] = uses.try_emplace(sym);
                if (fresh)
                {
                    order.push_back(sym);
                }
                return it->second;
            };
            each(script, [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_GLOBAL_VARIABLE && node->getSymbol())
                {
                    of(node->getSymbol()).declaration = node;
                }
                else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_DECLARATION && node->getSymbol())
                {
                    of(node->getSymbol()).declaration = node;
                }
                else if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    LSLSymbol* sym = node->getSymbol();
                    if (!sym || (sym->getSubType() != SYM_LOCAL && sym->getSubType() != SYM_GLOBAL))
                    {
                        return;
                    }
                    LSLASTNode* parent = node->getParent();
                    const bool  write  = parent && parent->getNodeType() == NODE_EXPRESSION &&
                                        operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(0) == node;
                    if (write && unread(parent))
                    {
                        of(sym).writes.push_back(parent);
                    }
                    else
                    {
                        of(sym).read = true;
                    }
                }
            });
            for (LSLSymbol* sym : order)
            {
                // A global never used at all goes with the unused (globals()),
                // and a local set to nothing that changes anything goes with
                // those (unusedLocal); one set to what does is this pass's.
                Uses&      use    = uses[sym];
                const bool global = use.declaration && use.declaration->getNodeType() == NODE_GLOBAL_VARIABLE;
                if (use.read || !use.declaration || (global && use.writes.empty()))
                {
                    continue;
                }
                report.note(use.declaration->getLoc(), "OptimizerRemovedWriteOnly", "removed [1], which is set and never read", { sym->getName() });
                for (LSLASTNode* write : use.writes)
                {
                    LSLASTNode* value  = write->getNodeSubType() == NODE_BINARY_EXPRESSION ? write->getChild(1) : nullptr;
                    LSLASTNode* holder = write->getParent()->getNodeType() == NODE_AST_NODE_LIST ? write : write->getParent();
                    if (value && !changesNothing(value))
                    {
                        // What it was set to, still worked out.
                        LSLASTNode::replaceNode(write, write->takeChild(1));
                    }
                    else
                    {
                        takeOut(holder);
                    }
                }
                if (global)
                {
                    script->getSymbolTable()->remove(sym);
                    script->getGlobals()->removeChild(use.declaration);
                }
                else
                {
                    auto*       decl = static_cast<LSLDeclaration*>(use.declaration);
                    LSLASTNode* init = decl->getInitializer();
                    forget(decl, sym);
                    if (init && !changesNothing(init))
                    {
                        LSLASTNode::replaceNode(decl, ctx.allocator->newTracked<LSLExpressionStatement>(static_cast<LSLExpression*>(decl->takeChild(1))));
                    }
                    else
                    {
                        takeOut(decl);
                    }
                }
                ++changes;
            }
        }

        void states(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* state : *script->getStates())
            {
                LSLSymbol* sym = state->getSymbol();
                if (!sym || sym->getReferences() != 1 || !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                report.note(state->getLoc(), "OptimizerRemovedState", "removed the state [1], which nothing enters", { sym->getName() });
                going.push_back(state);
            }
            for (LSLASTNode* state : going)
            {
                script->getSymbolTable()->remove(state->getSymbol());
                script->getStates()->removeChild(state);
                ++changes;
            }
        }
    };

    // ---- names -------------------------------------------------------------------------------

    class TableCollector : public ASTVisitor
    {
    public:
        bool visit(LSLASTNode* node) override
        {
            if (LSLSymbolTable* table = node->getSymbolTable())
            {
                if (mSeen.insert(table).second)
                {
                    tables.push_back(table);
                }
            }
            return true;
        }
        std::vector<LSLSymbolTable*> tables;

    private:
        std::set<LSLSymbolTable*> mSeen;
    };

    const char* const KEYWORDS[] = { "default", "state",  "event",   "jump",     "return", "if",    "else",       "for",  "do",
                                     "while",   "print",  "integer", "float",    "string", "key",   "vector",     "rotation",
                                     "quaternion", "list", "TRUE",   "FALSE",    "inline", "const", "break", "continue",   "switch", "case", nullptr };

    // Every name the script owns, shortest first for the most used.
    void shrink(LSLScript* script, ScriptContext& context, ScriptAllocator& allocator, Report& report, ALLSLOptimizer::Result& result)
    {
        TableCollector collector;
        script->visit(&collector);
        std::vector<LSLSymbol*> symbols;
        for (LSLSymbolTable* table : collector.tables)
        {
            if (table->getTableType() == SYMTAB_BUILTINS)
            {
                continue;
            }
            for (auto& entry : table->getMap())
            {
                LSLSymbol* sym = entry.second;
                if (sym->getSymbolType() == SYM_EVENT || sym->getSubType() == SYM_BUILTIN)
                {
                    continue;
                }
                if (sym->getSymbolType() == SYM_STATE && !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                symbols.push_back(sym);
            }
        }
        // Equally used, the first declared first, then by name: the tables
        // are hash maps, whose order is the standard library's own.
        std::stable_sort(symbols.begin(), symbols.end(), [](LSLSymbol* a, LSLSymbol* b) {
            if (a->getReferences() != b->getReferences())
            {
                return a->getReferences() > b->getReferences();
            }
            const Tailslide::YYLTYPE& at = *a->getLoc();
            const Tailslide::YYLTYPE& bt = *b->getLoc();
            if (at < bt || bt < at)
            {
                return at < bt;
            }
            return strcmp(a->getName(), b->getName()) < 0;
        });
        const std::string first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
        const std::string rest  = first + "0123456789";
        std::vector<int>  digits;
        const auto        next = [&]() {
            // Counts up in a mixed radix: the first digit over `first`,
            // the rest over `rest`.
            std::string name;
            while (true)
            {
                if (digits.empty())
                {
                    digits.push_back(0);
                }
                else
                {
                    size_t i = digits.size();
                    while (i > 0)
                    {
                        --i;
                        const int radix = i == 0 ? static_cast<int>(first.size()) : static_cast<int>(rest.size());
                        if (++digits[i] < radix)
                        {
                            break;
                        }
                        digits[i] = 0;
                        if (i == 0)
                        {
                            digits.insert(digits.begin(), 0);
                            break;
                        }
                    }
                }
                name.clear();
                for (size_t i = 0; i < digits.size(); ++i)
                {
                    name += (i == 0 ? first : rest)[digits[i]];
                }
                bool keyword = false;
                for (const char* const* k = KEYWORDS; *k; ++k)
                {
                    keyword = keyword || name == *k;
                }
                if (keyword || (context.builtins && context.builtins->lookup(name.c_str(), SYM_ANY)))
                {
                    continue;
                }
                return name;
            }
        };
        for (LSLSymbol* sym : symbols)
        {
            const std::string name = next();
            sym->setMangledName(allocator.copyStr(name.c_str()));
            result.renamed.emplace(sym->getName(), name);
            report.note(sym->getLoc(), "OptimizerRenamed", "renamed the [1] [2] to [3]", { LSLSymbol::getTypeName(sym->getSymbolType()), sym->getName(), name });
        }
    }

    // ---- the printer ----------------------------------------------------------------------------

    // Tailslide's printer with floats that read back exactly, integers
    // where a float may be one, and a note of where every name, number
    // and statement came from.
    class Printer : public PrettyPrintVisitor
    {
    public:
        Printer(const PrettyPrintOpts& opts, const ALLSLOptimizer::Options& options) : PrettyPrintVisitor(opts), mOptions(options) {}

        bool visit(LSLIdentifier* id) override { return marked(id, [&] { return PrettyPrintVisitor::visit(id); }); }
        bool visit(LSLIntegerConstant* c) override { return marked(c, [&] { return PrettyPrintVisitor::visit(c); }); }
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
                mStream << number(v, integer);
                return false;
            });
        }
        bool visit(LSLVectorConstant* c) override
        {
            return marked(c, [&] {
                const Vector3* v = c->getValue();
                mStream << '<' << number(v->x, true) << ", " << number(v->y, true) << ", " << number(v->z, true) << '>';
                return false;
            });
        }
        bool visit(LSLQuaternionConstant* c) override
        {
            return marked(c, [&] {
                const Quaternion* q = c->getValue();
                mStream << '<' << number(q->x, true) << ", " << number(q->y, true) << ", " << number(q->z, true) << ", " << number(q->s, true) << '>';
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

    ALScriptWeight::Target weightTarget(ALLSLOptimizer::Target target)
    {
        return target == ALLSLOptimizer::Target::LSO    ? ALScriptWeight::Target::LSO
               : target == ALLSLOptimizer::Target::Luau ? ALScriptWeight::Target::LSLLuau
                                                        : ALScriptWeight::Target::Mono;
    }

    ALScriptWeight weigh(ALLSLOptimizer::Target target, std::string_view text)
    {
        switch (weightTarget(target))
        {
            case ALScriptWeight::Target::LSO:
                return ALScriptWeigh::lso(text);
            case ALScriptWeight::Target::LSLLuau:
                return ALScriptWeigh::lslLuau(text);
            default:
                return ALScriptWeigh::mono(text);
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
        const auto propagate = [&]() {
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
                Folder folder(ctx, report, options);
                script->visit(&folder);
                walks(1);
                Simplifier simplifier(ctx, report, options);
                script->visit(&simplifier);
                walks(1);
                if (folder.changes + simplifier.changes)
                {
                    changes += folder.changes + simplifier.changes;
                    refresh();
                }
            }
            if (options.dcr)
            {
                DeadCode dead(ctx, report, options);
                const int removed = dead.run(script);
                walks(1);
                if (removed)
                {
                    changes += removed;
                    refresh();
                }
            }
            if (!changes)
            {
                break;
            }
        }
        // What is made for size alone, once nothing more will fold.
        bool weighLists = false;
        if (options.constfold)
        {
            Shapes values(ctx, report, options, Shapes::Stage::Values);
            script->visit(&values);
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
            ListHelpers had;
            if (mono)
            {
                script->visit(&had);
            }
            if (lists)
            {
                Shapes shaped(ctx, report, options, Shapes::Stage::Lists);
                if (mono)
                {
                    shaped.setHelpers(&had);
                }
                script->visit(&shaped);
                walks(1);
                if (mono && shaped.changes)
                {
                    ListHelpers has;
                    script->visit(&has);
                    const ALLSLCosts& costs = ALLSLCosts::of(options.target);
                    weighLists              = shaped.changes * costs.listShapeLeast <= had.freshIn(has) * costs.listHelperMost;
                }
            }
        }
        if (standing)
        {
            standingFunctions(script, inlinedMap, *standing);
        }
        if (options.shrinknames)
        {
            shrink(script, parser.context, parser.allocator, report, result);
        }
        braceDanglingElses(script, &parser.allocator);

        PrettyPrintOpts opts{};
        opts.mangle_local_names  = options.shrinknames;
        opts.mangle_func_names   = options.shrinknames;
        opts.mangle_global_names = options.shrinknames;
        opts.show_unmangled      = false;
        Printer printer(opts, options);
        script->visit(&printer);
        std::string written = printer.mStream.str();
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
        result.map  = printer.map(std::string());
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
        Folder folder(ctx, report, options);
        for (LSLASTNode* global : *script->getGlobals())
        {
            if (global->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                global->visit(&folder);
            }
        }
        if (!folder.changes)
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

/**
 * @file alluausharedstart.cpp
 * @brief Strings that start alike in an SLua script, their start kept once.
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

#include "alluausharedstart.h"

#include "alscriptlexicon.h"

#include "Luau/Ast.h"
#include "Luau/Parser.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace
{
    // Each literal of the strings, and whether it stands where `..` would
    // bind otherwise than the literal did: an operand of what binds tighter,
    // what is indexed or called, what is asserted a type, a call's string
    // given without brackets.
    class Literals final : public Luau::AstVisitor
    {
    public:
        struct Found
        {
            Luau::AstExprConstantString* node     = nullptr;
            bool                         bracketed = false;
        };

        Literals(std::string_view source, const boost::unordered_flat_set<std::string>& wanted, size_t start)
        :   mSource(source),
            mWanted(wanted),
            mStart(start)
        {
        }

        std::vector<Found> found;

        bool visit(Luau::AstExprConstantString* node) override
        {
            if (mLeft.contains(node))
            {
                return false;
            }
            const std::string value(node->value.data, node->value.size);
            if (mWanted.contains(value))
            {
                found.push_back({ node, mBracket.contains(node) });
            }
            return false;
        }

        bool visit(Luau::AstExprBinary* node) override
        {
            using Op = Luau::AstExprBinary::Op;
            // Constant strings joined, which Luau folds into the one string
            // the table keeps: its first piece keeps the start, and Luau
            // folds the rest of the chain onto what follows it.
            std::vector<Luau::AstExprConstantString*> pieces;
            if (node->op == Op::Concat && constantChain(node, pieces))
            {
                std::string whole;
                for (Luau::AstExprConstantString* piece : pieces)
                {
                    whole.append(piece->value.data, piece->value.size);
                    mLeft.insert(piece);
                }
                if (whole.size() <= FOLDED_MOST)
                {
                    if (mWanted.contains(whole) && pieces.front()->value.size >= mStart)
                    {
                        found.push_back({ pieces.front(), false });
                    }
                    return false;
                }
                // Too long for Luau to fold: each piece its own string.
                for (Luau::AstExprConstantString* piece : pieces)
                {
                    mLeft.erase(piece);
                }
            }
            const bool looser = node->op == Op::Concat || node->op == Op::CompareNe || node->op == Op::CompareEq || node->op == Op::CompareLt ||
                                node->op == Op::CompareLe || node->op == Op::CompareGt || node->op == Op::CompareGe || node->op == Op::And ||
                                node->op == Op::Or;
            if (!looser)
            {
                mBracket.insert(node->left);
                mBracket.insert(node->right);
            }
            return true;
        }
        bool visit(Luau::AstExprUnary* node) override
        {
            mBracket.insert(node->expr);
            return true;
        }
        bool visit(Luau::AstExprIndexName* node) override
        {
            mBracket.insert(node->expr);
            return true;
        }
        bool visit(Luau::AstExprIndexExpr* node) override
        {
            // A key, which no load makes.
            mBracket.insert(node->expr);
            mLeft.insert(node->index);
            return true;
        }
        bool visit(Luau::AstExprTypeAssertion* node) override
        {
            mBracket.insert(node->expr);
            return true;
        }
        bool visit(Luau::AstExprTable* node) override
        {
            for (const Luau::AstExprTable::Item& item : node->items)
            {
                if (item.key)
                {
                    mLeft.insert(item.key);
                }
            }
            return true;
        }
        bool visit(Luau::AstExprCall* node) override
        {
            mBracket.insert(node->func);
            // A require's path is where a module is, not a string to share.
            if (const Luau::AstExprGlobal* global = node->func->as<Luau::AstExprGlobal>(); global && std::string_view(global->name.value) == "require")
            {
                for (Luau::AstExpr* arg : node->args)
                {
                    mLeft.insert(arg);
                }
                return true;
            }
            // `f "x"`: a call of one string with no brackets.
            for (Luau::AstExpr* arg : node->args)
            {
                if (arg->is<Luau::AstExprConstantString>() && !openedBefore(arg->location.begin))
                {
                    mBracket.insert(arg);
                }
            }
            return true;
        }

    private:
        // What Luau folds a chain of constant strings into at most
        // (kConstantFoldStringLimit).
        static constexpr size_t FOLDED_MOST = 4096;

        // Whether an expression is constant strings joined, in brackets or
        // not, each piece in order.
        static bool constantChain(Luau::AstExpr* expr, std::vector<Luau::AstExprConstantString*>& pieces)
        {
            if (Luau::AstExprGroup* group = expr->as<Luau::AstExprGroup>())
            {
                return constantChain(group->expr, pieces);
            }
            if (Luau::AstExprConstantString* piece = expr->as<Luau::AstExprConstantString>())
            {
                pieces.push_back(piece);
                return true;
            }
            const Luau::AstExprBinary* binary = expr->as<Luau::AstExprBinary>();
            return binary && binary->op == Luau::AstExprBinary::Op::Concat && constantChain(binary->left, pieces) && constantChain(binary->right, pieces);
        }

        // Whether the code before a place, past blanks, is a call's bracket
        // or a comma.
        bool openedBefore(const Luau::Position& at) const
        {
            S32    line   = 0;
            size_t offset = 0;
            while (line < static_cast<S32>(at.line) && offset < mSource.size())
            {
                const size_t next = mSource.find('\n', offset);
                if (next == std::string_view::npos)
                {
                    return false;
                }
                offset = next + 1;
                ++line;
            }
            size_t i = std::min(offset + at.column, mSource.size());
            while (i > 0 && (mSource[i - 1] == ' ' || mSource[i - 1] == '\t' || mSource[i - 1] == '\r' || mSource[i - 1] == '\n'))
            {
                --i;
            }
            return i > 0 && (mSource[i - 1] == '(' || mSource[i - 1] == ',');
        }

        std::string_view                                     mSource;
        const boost::unordered_flat_set<std::string>&         mWanted;
        size_t                                               mStart = 0;
        boost::unordered_flat_set<const Luau::AstExpr*>       mBracket;
        boost::unordered_flat_set<const Luau::AstExpr*>       mLeft;
    };

    // Whether a name stands as a word anywhere in the source.
    bool named(std::string_view source, std::string_view name)
    {
        for (size_t at = source.find(name); at != std::string_view::npos; at = source.find(name, at + 1))
        {
            const bool before = at > 0 && ALScriptLexicon::isNameByte(source[at - 1]);
            const bool after  = at + name.size() < source.size() && ALScriptLexicon::isNameByte(source[at + name.size()]);
            if (!before && !after)
            {
                return true;
            }
        }
        return false;
    }
}

namespace ALLuauSharedStart
{
    std::string quoted(std::string_view text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (byte < 0x20 || byte == 0x7F)
                    {
                        // Three digits, so that a digit after it is not
                        // read as part of it.
                        out += llformat("\\%03u", byte);
                    }
                    else
                    {
                        out += c;
                    }
            }
        }
        return out + "\"";
    }

    bool rewrite(std::string_view source, const std::string& start, const std::vector<std::string>& strings, Rewrite& out, std::string& error)
    {
        out = Rewrite();
        Luau::Allocator          allocator;
        Luau::AstNameTable       names(allocator);
        Luau::ParseOptions       options;
        const Luau::ParseResult parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
        if (!parsed.root || !parsed.errors.empty())
        {
            error = parsed.errors.empty() ? std::string("the script does not parse") : parsed.errors.front().getMessage();
            return false;
        }
        boost::unordered_flat_set<std::string> wanted;
        for (const std::string& one : strings)
        {
            if (one.compare(0, start.size(), start) == 0)
            {
                wanted.insert(one);
            }
        }
        Literals literals(source, wanted, start.size());
        parsed.root->visit(&literals);
        if (literals.found.empty())
        {
            error = "no literal of these strings is in the script's own text";
            return false;
        }
        // A name the script does not have.
        out.name = "sharedStart";
        for (int n = 2; named(source, out.name); ++n)
        {
            out.name = "sharedStart" + std::to_string(n);
        }
        // At the top, past the `--!` comments Luau reads only there.
        S32    top    = 0;
        size_t offset = 0;
        while (offset < source.size() && source.compare(offset, 3, "--!") == 0)
        {
            const size_t next = source.find('\n', offset);
            ++top;
            offset = next == std::string_view::npos ? source.size() : next + 1;
        }
        const bool ends_bare = offset >= source.size() && !source.empty() && source.back() != '\n';
        out.edits.push_back({ top, 0, top, 0,
                              std::string(ends_bare ? "\n" : "") + "local " + out.name + "\n" + out.name + " = " + ALLuauSharedStart::quoted(start) + "\n\n" });
        for (const Literals::Found& one : literals.found)
        {
            const std::string value(one.node->value.data, one.node->value.size);
            const std::string rest = value.substr(start.size());
            std::string       text = rest.empty() ? out.name : out.name + " .. " + ALLuauSharedStart::quoted(rest);
            if (one.bracketed && !rest.empty())
            {
                text = "(" + text + ")";
            }
            const Luau::Location& at = one.node->location;
            out.edits.push_back({ static_cast<S32>(at.begin.line), static_cast<S32>(at.begin.column), static_cast<S32>(at.end.line),
                                  static_cast<S32>(at.end.column), text });
            ++out.literals;
        }
        std::stable_sort(out.edits.begin(), out.edits.end(), [](const Edit& a, const Edit& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
        return true;
    }
}

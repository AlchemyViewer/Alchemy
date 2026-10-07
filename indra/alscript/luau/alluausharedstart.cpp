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

#include "alscriptfixes.h"
#include "alscriptlexicon.h"

#include "Luau/Ast.h"
#include "Luau/Lexer.h"
#include "Luau/Parser.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace
{
    // Whether an expression is constant strings joined, in brackets or
    // not, each piece in order; a literal alone is one piece.
    bool constantChain(Luau::AstExpr* expr, std::vector<Luau::AstExprConstantString*>& pieces)
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

    bool constant(Luau::AstExpr* expr)
    {
        std::vector<Luau::AstExprConstantString*> pieces;
        return constantChain(expr, pieces);
    }

    // What the literals need to know of the whole script first: the
    // strings its own types name as singletons -- `"idle" | "busy"` --
    // where a literal given for one must stay one, `string` being none of
    // them; and the one value of each local declared bare and given it once
    // after, which is a start kept once already.
    class Before final : public Luau::AstVisitor
    {
    public:
        boost::unordered_flat_set<std::string> singletons;

        std::vector<Luau::AstExpr*> keptApart() const
        {
            std::vector<Luau::AstExpr*> out;
            for (const auto& [local, given] : mGiven)
            {
                if (given.first == 1 && given.second && mBare.contains(local))
                {
                    out.push_back(given.second);
                }
            }
            return out;
        }

        bool visit(Luau::AstType*) override { return true; }
        bool visit(Luau::AstTypePack*) override { return true; }
        bool visit(Luau::AstTypeSingletonString* node) override
        {
            singletons.emplace(node->value.data, node->value.size);
            return false;
        }
        bool visit(Luau::AstStatLocal* node) override
        {
            if (node->values.size == 0)
            {
                for (Luau::AstLocal* var : node->vars)
                {
                    mBare.insert(var);
                }
            }
            return true;
        }
        bool visit(Luau::AstStatAssign* node) override
        {
            for (Luau::AstExpr* var : node->vars)
            {
                if (const Luau::AstExprLocal* local = var->as<Luau::AstExprLocal>())
                {
                    auto& [times, value] = mGiven[local->local];
                    ++times;
                    value = node->vars.size == 1 && node->values.size == 1 ? node->values.data[0] : nullptr;
                }
            }
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            // Given a value of its own already: more than once.
            if (const Luau::AstExprLocal* local = node->var->as<Luau::AstExprLocal>())
            {
                mGiven[local->local].first += 2;
            }
            return true;
        }

    private:
        boost::unordered_flat_set<const Luau::AstLocal*>                                    mBare;
        // How many times each local is assigned, and what where it is
        // assigned alone.
        boost::unordered_flat_map<const Luau::AstLocal*, std::pair<size_t, Luau::AstExpr*>> mGiven;
    };

    // Each literal of a string a load makes, which could be written as a
    // start and the rest, and whether it stands where `..` would bind
    // otherwise than the literal did: an operand of what binds tighter,
    // what is indexed or called, what is asserted a type, a call's string
    // given without brackets. Constant strings joined, which Luau folds
    // into the one string the table keeps, are one, at their first piece.
    class Literals final : public Luau::AstVisitor
    {
    public:
        struct Found
        {
            Luau::AstExprConstantString* node      = nullptr;
            // What a load makes of it: the literal's, or the pieces' joined.
            std::string                  value;
            bool                         bracketed = false;
            // Where it is the first of constant strings joined, the `..`
            // they are.
            Luau::AstExprBinary*         chain     = nullptr;
        };

        Literals(const ALScriptFixes::Lines& lines, const Before& before)
        :   mLines(lines),
            mSingletons(before.singletons)
        {
            for (Luau::AstExpr* given : before.keptApart())
            {
                mLeft.insert(given);
            }
        }

        std::vector<Found> found;

        bool visit(Luau::AstExprConstantString* node) override
        {
            std::string value(node->value.data, node->value.size);
            if (!mLeft.contains(node) && !mSingletons.contains(value))
            {
                found.push_back({ node, std::move(value), mBracket.contains(node) });
            }
            return false;
        }

        bool visit(Luau::AstExprGroup* node) override
        {
            // What is left is left in brackets as well.
            if (mLeft.contains(node))
            {
                mLeft.insert(node->expr);
            }
            return true;
        }

        bool visit(Luau::AstExprBinary* node) override
        {
            using Op = Luau::AstExprBinary::Op;
            // Constant strings joined, which Luau folds into the one string
            // the table keeps -- but not what a `..` it does not fold goes
            // on to, which it loads piece by piece: its first piece keeps
            // the start.
            std::vector<Luau::AstExprConstantString*> pieces;
            if (node->op == Op::Concat && !mUnrolled.contains(node) && constantChain(node, pieces))
            {
                std::string whole;
                for (const Luau::AstExprConstantString* piece : pieces)
                {
                    whole.append(piece->value.data, piece->value.size);
                }
                if (whole.size() <= FOLDED_MOST)
                {
                    if (!mLeft.contains(node) && !mSingletons.contains(whole))
                    {
                        found.push_back({ pieces.front(), std::move(whole), false, node });
                    }
                    return false;
                }
                // Too long for Luau to fold: joined as any `..` is.
            }
            if (node->op == Op::Concat)
            {
                unroll(node->right);
            }
            // Constants compared, which Luau decides as it compiles, and a
            // constant that `and` decides on: no load of the string, or one
            // only Luau's folding makes. Not `or`'s, which a string, always
            // true, is the value of, and loaded as it stands.
            if ((node->op == Op::CompareEq || node->op == Op::CompareNe) && constant(node->left) && constant(node->right))
            {
                return false;
            }
            if (node->op == Op::And && constant(node->left))
            {
                mLeft.insert(node->left);
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
            // `#"..."` is a length and `not "..."` false, which Luau works
            // out as it compiles: no load of the string.
            if ((node->op == Luau::AstExprUnary::Op::Len || node->op == Luau::AstExprUnary::Op::Not) && constant(node->expr))
            {
                return false;
            }
            mBracket.insert(node->expr);
            return true;
        }
        bool visit(Luau::AstExprInterpString* node) override
        {
            // A constant string filled in is written into the pattern Luau
            // makes of it, never loaded.
            for (Luau::AstExpr* expression : node->expressions)
            {
                mLeft.insert(expression);
            }
            return true;
        }
        bool visit(Luau::AstStatCompoundAssign* node) override
        {
            // `..=` joins what it is given as `..` joins what it goes on to.
            if (node->op == Luau::AstExprBinary::Op::Concat)
            {
                unroll(node->value);
            }
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

        // What a `..` Luau does not fold goes on to, down its right, which
        // it loads piece by piece (unrollConcats).
        void unroll(Luau::AstExpr* from)
        {
            for (Luau::AstExprBinary* next = from->as<Luau::AstExprBinary>(); next && next->op == Luau::AstExprBinary::Op::Concat;
                 next = next->right->as<Luau::AstExprBinary>())
            {
                mUnrolled.insert(next);
            }
        }

        // Whether the code before a place, past blanks, is a call's bracket
        // or a comma.
        bool openedBefore(const Luau::Position& at) const
        {
            const std::optional<size_t> offset = mLines.offsetOf(static_cast<S32>(at.line), static_cast<S32>(at.column));
            if (!offset)
            {
                return false;
            }
            const std::string_view source = mLines.text();
            size_t                 i      = *offset;
            while (i > 0 && (source[i - 1] == ' ' || source[i - 1] == '\t' || source[i - 1] == '\r' || source[i - 1] == '\n'))
            {
                --i;
            }
            return i > 0 && (source[i - 1] == '(' || source[i - 1] == ',');
        }

        const ALScriptFixes::Lines&                     mLines;
        const boost::unordered_flat_set<std::string>&   mSingletons;
        boost::unordered_flat_set<const Luau::AstExpr*> mBracket;
        boost::unordered_flat_set<const Luau::AstExpr*> mLeft;
        boost::unordered_flat_set<const Luau::AstExpr*> mUnrolled;
    };

    std::vector<Literals::Found> literalsOf(Luau::AstStatBlock* root, const ALScriptFixes::Lines& lines)
    {
        Before before;
        root->visit(&before);
        Literals literals(lines, before);
        root->visit(&literals);
        return std::move(literals.found);
    }

    // The edits made over a copy of the source, from the last back.
    std::string applied(std::string_view source, const std::vector<ALLuauSharedStart::Edit>& edits)
    {
        const ALScriptFixes::Lines lines(source);
        std::string                out(source);
        for (auto edit = edits.rbegin(); edit != edits.rend(); ++edit)
        {
            const std::optional<size_t> from = lines.offsetOf(edit->line, edit->column);
            const std::optional<size_t> to   = lines.offsetOf(edit->endLine, edit->endColumn);
            if (from && to && *from <= *to)
            {
                out.replace(*from, *to - *from, edit->text);
            }
        }
        return out;
    }

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

    // How long the character a byte begins is, where it begins a whole one
    // in UTF-8 -- in its shortest form, no surrogate, none past U+10FFFF;
    // nought where it does not.
    size_t characterAt(std::string_view text, size_t at)
    {
        const unsigned char lead   = static_cast<unsigned char>(text[at]);
        size_t              length = 0;
        unsigned char       low    = 0x80;
        unsigned char       high   = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            length = 2;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            length = 3;
            low    = lead == 0xE0 ? 0xA0 : 0x80;
            high   = lead == 0xED ? 0x9F : 0xBF;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            length = 4;
            low    = lead == 0xF0 ? 0x90 : 0x80;
            high   = lead == 0xF4 ? 0x8F : 0xBF;
        }
        if (length == 0 || at + length > text.size())
        {
            return 0;
        }
        for (size_t i = 1; i < length; ++i)
        {
            const unsigned char next = static_cast<unsigned char>(text[at + i]);
            if (next < (i == 1 ? low : 0x80) || next > (i == 1 ? high : 0xBF))
            {
                return 0;
            }
        }
        return length;
    }
}

namespace ALLuauSharedStart
{
    std::string quoted(std::string_view text)
    {
        std::string out = "\"";
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char          c    = text[i];
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
                    else if (byte >= 0x80)
                    {
                        // A whole character as it is; a byte of none in
                        // hex, two digits, which is all `\x` reads.
                        if (const size_t length = characterAt(text, i))
                        {
                            out.append(text.substr(i, length));
                            i += length - 1;
                        }
                        else
                        {
                            out += llformat("\\x%02X", byte);
                        }
                    }
                    else
                    {
                        out += c;
                    }
            }
        }
        return out + "\"";
    }

    boost::unordered_flat_map<std::string, size_t> written(std::string_view source)
    {
        boost::unordered_flat_map<std::string, size_t> out;
        Luau::Allocator                                allocator;
        Luau::AstNameTable                             names(allocator);
        const Luau::ParseResult                        parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator, Luau::ParseOptions());
        if (!parsed.root || !parsed.errors.empty())
        {
            return out;
        }
        const ALScriptFixes::Lines lines(source);
        for (const Literals::Found& one : literalsOf(parsed.root, lines))
        {
            const auto [at, fresh] = out.try_emplace(one.value, one.node->value.size);
            if (!fresh)
            {
                at->second = std::min(at->second, static_cast<size_t>(one.node->value.size));
            }
        }
        return out;
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
        // Each of them written out, every literal of it able to give the
        // start: of one that is not, the table keeps the string whole
        // whatever is made of the rest, which is then heavier.
        const ALScriptFixes::Lines             lines(source);
        const std::vector<Literals::Found>     found = literalsOf(parsed.root, lines);
        std::vector<const Literals::Found*>    kept;
        boost::unordered_flat_set<std::string> seen;
        for (const Literals::Found& one : found)
        {
            if (!wanted.contains(one.value))
            {
                continue;
            }
            if (one.node->value.size < start.size())
            {
                error = "the start runs past the first of the constant strings joined into one of these strings";
                return false;
            }
            seen.insert(one.value);
            kept.push_back(&one);
        }
        if (seen.empty())
        {
            error = "none of these strings is written out in the script's own text. Luau makes them as it compiles -- joining on a local "
                    "never assigned again, which keeps their start once in the source alone, or filling in an interpolated string -- or "
                    "they are in a module the script requires";
            return false;
        }
        if (seen.size() < wanted.size())
        {
            error = "not every one of these strings is written out in the script's own text, so their start would still be kept whole: "
                    "Luau makes the others as it compiles, or they are in a module the script requires";
            return false;
        }
        // A name the script does not have.
        out.name = "sharedStart";
        for (int n = 2; named(source, out.name); ++n)
        {
            out.name = "sharedStart" + std::to_string(n);
        }
        // Before the first of the script that is not a comment, where Luau
        // stops reading `--!` comments as the script's own: past all of
        // them, and whatever comments are before them. At the start of its
        // line, where nothing else is before it there.
        Luau::Lexer lexer(source.data(), source.size(), names);
        lexer.setSkipComments(true);
        const Luau::Position   first  = lexer.next().location.begin;
        const std::string_view before = lines.line(static_cast<S32>(first.line)).substr(0, first.column);
        const S32              top    = static_cast<S32>(first.line);
        const S32              column = before.find_first_not_of(" \t") == std::string_view::npos ? 0 : static_cast<S32>(first.column);
        out.edits.push_back({ top, column, top, column, "local " + out.name + "\n" + out.name + " = " + ALLuauSharedStart::quoted(start) + "\n\n" });
        for (const Literals::Found* one : kept)
        {
            const std::string_view piece(one->node->value.data, one->node->value.size);
            const std::string_view rest = piece.substr(start.size());
            std::string            text = rest.empty() ? out.name : out.name + " .. " + ALLuauSharedStart::quoted(rest);
            // Constant strings joined, the start the first of them: the
            // pieces after it in brackets, with what is left of the first,
            // which Luau folds into one string as it did the whole, rather
            // than loading each as it does what a `..` it does not fold
            // goes on to. Where the first is further in, in brackets of its
            // own, as it is.
            if (one->chain && one->chain->left == one->node)
            {
                const Luau::AstExpr* after = one->chain->right;
                const S32            line  = static_cast<S32>(after->location.end.line);
                const S32            end   = static_cast<S32>(after->location.end.column);
                if (!rest.empty())
                {
                    text = out.name + " .. (" + ALLuauSharedStart::quoted(rest);
                    out.edits.push_back({ line, end, line, end, ")" });
                }
                else if (!after->is<Luau::AstExprConstantString>() && !after->is<Luau::AstExprGroup>())
                {
                    const S32 from_line = static_cast<S32>(after->location.begin.line);
                    const S32 from      = static_cast<S32>(after->location.begin.column);
                    out.edits.push_back({ from_line, from, from_line, from, "(" });
                    out.edits.push_back({ line, end, line, end, ")" });
                }
            }
            else if (one->bracketed && !rest.empty())
            {
                text = "(" + text + ")";
            }
            const Luau::Location& at = one->node->location;
            out.edits.push_back({ static_cast<S32>(at.begin.line), static_cast<S32>(at.begin.column), static_cast<S32>(at.end.line),
                                  static_cast<S32>(at.end.column), text });
            ++out.literals;
        }
        std::stable_sort(out.edits.begin(), out.edits.end(), [](const Edit& a, const Edit& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
        // What it makes, parsed before it is offered.
        const std::string       made  = applied(source, out.edits);
        const Luau::ParseResult again = Luau::Parser::parse(made.data(), made.size(), names, allocator, options);
        if (!again.root || !again.errors.empty())
        {
            error = "what it would make does not parse: " + (again.errors.empty() ? std::string("it is not a script") : again.errors.front().getMessage());
            out   = Rewrite();
            return false;
        }
        return true;
    }
}

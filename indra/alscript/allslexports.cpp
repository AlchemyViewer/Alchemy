/**
 * @file allslexports.cpp
 * @brief What an LSL include declares for whoever includes it, read off its text.
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

#include "allslexports.h"

#include "alpreprocessor.h"

#include <algorithm>

namespace
{
    using Token = ALPreprocessor::Token;
    using Kind  = Token::Kind;

    bool isType(const std::string& word)
    {
        static const char* const TYPES[] = { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };
        return std::any_of(std::begin(TYPES), std::end(TYPES), [&word](const char* type) { return word == type; });
    }

    bool isWord(const std::string& word)
    {
        static const char* const WORDS[] = { "default", "state", "if", "else", "for", "while", "do", "jump", "return", "inline", "print" };
        return isType(word) || std::any_of(std::begin(WORDS), std::end(WORDS), [&word](const char* one) { return word == one; });
    }

    bool significant(const Token& token)
    {
        return token.kind != Kind::Space && token.kind != Kind::Newline && token.kind != Kind::Comment;
    }

    bool is(const Token* token, const char* punct)
    {
        return token && token->kind == Kind::Punct && token->text == punct;
    }
}

namespace ALLSLExports
{
    std::vector<std::string> of(std::string_view source)
    {
        const std::vector<Token> tokens = ALPreprocessor::tokenize(source, false);
        std::vector<std::string> out;
        const auto add = [&out](const std::string& name) {
            if (std::find(out.begin(), out.end(), name) == out.end())
            {
                out.push_back(name);
            }
        };
        // The declarations are what stands at the top, outside a directive:
        // the significant tokens there, each with the depth of braces it
        // stands at, and a directive's defined name noted as it passes.
        std::vector<const Token*> top;
        std::vector<S32>          depths;
        S32                       depth      = 0;
        bool                      line_start = true;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const Token& token = tokens[i];
            if (token.kind == Kind::Newline)
            {
                line_start = true;
                continue;
            }
            if (!significant(token))
            {
                continue;
            }
            if (line_start && token.kind == Kind::Punct && token.text == "#")
            {
                // A directive runs to its line's end, and past it where the
                // line ends in a backslash.
                const Token* word = nullptr;
                const Token* name = nullptr;
                size_t       j    = i + 1;
                for (; j < tokens.size(); ++j)
                {
                    const Token& next = tokens[j];
                    if (next.kind == Kind::Newline)
                    {
                        const Token* before = nullptr;
                        for (size_t k = j; k-- > i;)
                        {
                            if (tokens[k].kind != Kind::Space)
                            {
                                before = &tokens[k];
                                break;
                            }
                        }
                        if (before && before->text == "\\")
                        {
                            continue;
                        }
                        break;
                    }
                    if (!significant(next))
                    {
                        continue;
                    }
                    if (!word)
                    {
                        word = &next;
                    }
                    else if (!name)
                    {
                        name = &next;
                    }
                }
                if (word && word->text == "define" && name && name->kind == Kind::Ident)
                {
                    add(name->text);
                }
                i          = j - 1;
                line_start = false;
                continue;
            }
            line_start = false;
            if (token.kind == Kind::Punct && token.text == "{")
            {
                ++depth;
            }
            top.push_back(&token);
            depths.push_back(depth);
            if (token.kind == Kind::Punct && token.text == "}")
            {
                depth = std::max(0, depth - 1);
            }
        }
        const auto at = [&top](size_t n) -> const Token* { return n < top.size() ? top[n] : nullptr; };
        for (size_t n = 0; n < top.size(); ++n)
        {
            const Token* first = top[n];
            if (depths[n] != 0 || first->kind != Kind::Ident)
            {
                continue;
            }
            // `type name` or `name` alone, at the top.
            size_t name_at = n;
            if (isType(first->text))
            {
                if (!at(n + 1) || at(n + 1)->kind != Kind::Ident || isWord(at(n + 1)->text) || depths[n + 1] != 0)
                {
                    continue;
                }
                name_at = n + 1;
            }
            else if (isWord(first->text))
            {
                continue;
            }
            const Token* name  = at(name_at);
            const Token* after = at(name_at + 1);
            if (is(after, "("))
            {
                // A function: its parameters, then its body. A call at the
                // top, which is no declaration, has no body after it.
                S32    open = 0;
                size_t k    = name_at + 1;
                for (; k < top.size(); ++k)
                {
                    open += is(top[k], "(") ? 1 : is(top[k], ")") ? -1 : 0;
                    if (open == 0)
                    {
                        break;
                    }
                }
                if (is(at(k + 1), "{"))
                {
                    add(name->text);
                }
                n = k;
            }
            else if (name_at == n + 1 && (is(after, "=") || is(after, ";")))
            {
                add(name->text);
            }
        }
        return out;
    }
}

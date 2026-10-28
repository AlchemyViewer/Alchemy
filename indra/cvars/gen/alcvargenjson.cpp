/**
 * @file alcvargenjson.cpp
 * @brief The little JSON the generator reads and writes.
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

#include "alcvargenjson.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ALCVarGen
{
    namespace
    {
        class Parser
        {
        public:
            Parser(std::string_view text, int depth) : mText(text), mDepth(depth) {}

            bool document(Json& out, std::string& error)
            {
                if (!value(out, 0))
                {
                    error = mError;
                    return false;
                }
                space();
                if (mAt != mText.size())
                {
                    error = "text after the value at offset " + std::to_string(mAt);
                    return false;
                }
                return true;
            }

        private:
            bool fail(const std::string& what)
            {
                if (mError.empty())
                {
                    mError = what + " at offset " + std::to_string(mAt);
                }
                return false;
            }

            void space()
            {
                while (mAt < mText.size() &&
                       (mText[mAt] == ' ' || mText[mAt] == '\t' || mText[mAt] == '\n' || mText[mAt] == '\r'))
                {
                    ++mAt;
                }
            }

            bool literal(std::string_view word)
            {
                if (mText.substr(mAt, word.size()) == word)
                {
                    mAt += word.size();
                    return true;
                }
                return false;
            }

            bool value(Json& out, int level)
            {
                if (level > mDepth)
                {
                    return fail("nested too deeply");
                }
                space();
                if (mAt >= mText.size())
                {
                    return fail("a value expected");
                }
                const char c = mText[mAt];
                if (c == '{')
                {
                    return object(out, level);
                }
                if (c == '[')
                {
                    return array(out, level);
                }
                if (c == '"')
                {
                    out.kind = Json::Kind::String;
                    return string(out.text);
                }
                if (literal("true"))
                {
                    out.kind    = Json::Kind::Bool;
                    out.boolean = true;
                    return true;
                }
                if (literal("false"))
                {
                    out.kind = Json::Kind::Bool;
                    return true;
                }
                if (literal("null"))
                {
                    out.kind = Json::Kind::Null;
                    return true;
                }
                return number(out);
            }

            bool object(Json& out, int level)
            {
                out.kind = Json::Kind::Object;
                ++mAt;
                space();
                if (mAt < mText.size() && mText[mAt] == '}')
                {
                    ++mAt;
                    return true;
                }
                for (;;)
                {
                    space();
                    std::string name;
                    if (mAt >= mText.size() || mText[mAt] != '"' || !string(name))
                    {
                        return fail("a member's name expected");
                    }
                    space();
                    if (mAt >= mText.size() || mText[mAt] != ':')
                    {
                        return fail("':' expected");
                    }
                    ++mAt;
                    Json member;
                    if (!value(member, level + 1))
                    {
                        return false;
                    }
                    out.members.push_back({ std::move(name), std::move(member) });
                    space();
                    if (mAt < mText.size() && mText[mAt] == ',')
                    {
                        ++mAt;
                        continue;
                    }
                    if (mAt < mText.size() && mText[mAt] == '}')
                    {
                        ++mAt;
                        return true;
                    }
                    return fail("',' or '}' expected");
                }
            }

            bool array(Json& out, int level)
            {
                out.kind = Json::Kind::Array;
                ++mAt;
                space();
                if (mAt < mText.size() && mText[mAt] == ']')
                {
                    ++mAt;
                    return true;
                }
                for (;;)
                {
                    Json item;
                    if (!value(item, level + 1))
                    {
                        return false;
                    }
                    out.items.push_back(std::move(item));
                    space();
                    if (mAt < mText.size() && mText[mAt] == ',')
                    {
                        ++mAt;
                        continue;
                    }
                    if (mAt < mText.size() && mText[mAt] == ']')
                    {
                        ++mAt;
                        return true;
                    }
                    return fail("',' or ']' expected");
                }
            }

            static int hex(char c)
            {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f')
                {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F')
                {
                    return c - 'A' + 10;
                }
                return -1;
            }

            static void utf8(std::string& out, unsigned code)
            {
                if (code < 0x80)
                {
                    out += char(code);
                }
                else if (code < 0x800)
                {
                    out += char(0xC0 | (code >> 6));
                    out += char(0x80 | (code & 0x3F));
                }
                else if (code < 0x10000)
                {
                    out += char(0xE0 | (code >> 12));
                    out += char(0x80 | ((code >> 6) & 0x3F));
                    out += char(0x80 | (code & 0x3F));
                }
                else
                {
                    out += char(0xF0 | (code >> 18));
                    out += char(0x80 | ((code >> 12) & 0x3F));
                    out += char(0x80 | ((code >> 6) & 0x3F));
                    out += char(0x80 | (code & 0x3F));
                }
            }

            bool string(std::string& out)
            {
                ++mAt;
                while (mAt < mText.size())
                {
                    const char c = mText[mAt++];
                    if (c == '"')
                    {
                        return true;
                    }
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        return fail("a control character in a string");
                    }
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (mAt >= mText.size())
                    {
                        break;
                    }
                    const char e = mText[mAt++];
                    switch (e)
                    {
                        case '"':  out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/':  out += '/'; break;
                        case 'b':  out += '\b'; break;
                        case 'f':  out += '\f'; break;
                        case 'n':  out += '\n'; break;
                        case 'r':  out += '\r'; break;
                        case 't':  out += '\t'; break;
                        case 'u':
                        {
                            if (mAt + 4 > mText.size())
                            {
                                return fail("a short \\u escape");
                            }
                            unsigned code = 0;
                            for (int i = 0; i < 4; ++i)
                            {
                                const int digit = hex(mText[mAt++]);
                                if (digit < 0)
                                {
                                    return fail("a bad \\u escape");
                                }
                                code = (code << 4) | unsigned(digit);
                            }
                            utf8(out, code);
                            break;
                        }
                        default:
                            return fail("a bad escape");
                    }
                }
                return fail("an unterminated string");
            }

            // JSON's number and nothing looser, as the viewer's own reader takes
            // it: no plus, no leading zero, a digit on each side of a point.
            bool number(Json& out)
            {
                const size_t start = mAt;
                auto         digit = [this] { return mAt < mText.size() && mText[mAt] >= '0' && mText[mAt] <= '9'; };
                auto         digits = [this, &digit] {
                    const size_t first = mAt;
                    while (digit())
                    {
                        ++mAt;
                    }
                    return mAt > first;
                };
                if (mAt < mText.size() && mText[mAt] == '-')
                {
                    ++mAt;
                }
                if (mAt < mText.size() && mText[mAt] == '0')
                {
                    ++mAt;
                    if (digit())
                    {
                        return fail("a bad number");
                    }
                }
                else if (!digits())
                {
                    return fail(mAt == start ? "a value expected" : "a bad number");
                }
                if (mAt < mText.size() && mText[mAt] == '.')
                {
                    ++mAt;
                    if (!digits())
                    {
                        return fail("a bad number");
                    }
                }
                if (mAt < mText.size() && (mText[mAt] == 'e' || mText[mAt] == 'E'))
                {
                    ++mAt;
                    if (mAt < mText.size() && (mText[mAt] == '+' || mText[mAt] == '-'))
                    {
                        ++mAt;
                    }
                    if (!digits())
                    {
                        return fail("a bad number");
                    }
                }
                const std::string text(mText.substr(start, mAt - start));
                char*             end = nullptr;
                out.number            = std::strtod(text.c_str(), &end);
                // Past a double's range: infinite, which no default may be.
                // Its bits, as fast math may fold a test of the value.
                const bool finite =
                    (std::bit_cast<std::uint64_t>(out.number) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
                if (!end || *end != '\0' || !finite)
                {
                    return fail("a bad number");
                }
                out.kind = Json::Kind::Number;
                out.text = text;
                return true;
            }

            std::string_view mText;
            size_t           mAt = 0;
            int              mDepth;
            std::string      mError;
        };
    }

    const Json* Json::find(std::string_view name) const
    {
        for (const auto& [member, value] : members)
        {
            if (member == name)
            {
                return &value;
            }
        }
        return nullptr;
    }

    std::string_view Json::string(std::string_view name) const
    {
        const Json* value = find(name);
        return value && value->kind == Kind::String ? std::string_view(value->text) : std::string_view();
    }

    double Json::numberOr(std::string_view name, double fallback) const
    {
        const Json* value = find(name);
        return value && value->kind == Kind::Number ? value->number : fallback;
    }

    bool parseJson(std::string_view text, Json& out, std::string& error, int depth)
    {
        out = Json();
        return Parser(text, depth).document(out, error);
    }

    std::string jsonString(std::string_view text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char escape[8];
                        std::snprintf(escape, sizeof(escape), "\\u%04x", unsigned(static_cast<unsigned char>(c)));
                        out += escape;
                    }
                    else
                    {
                        out += c;
                    }
            }
        }
        out += '"';
        return out;
    }
}

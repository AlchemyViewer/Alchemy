/**
 * @file allsllibraryfold.cpp
 * @brief The LSL library, answered for constant arguments.
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

#include "allsllibraryfold.h"

#include "llmath.h"
#include "llmd5.h"
#include "llquaternion.h"
#include "v3math.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <type_traits>

namespace ALLSLPasses
{
namespace
{
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
    struct JsonField;

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
        Kind                    kind = Kind::Null;
        std::string             text;  // a number as written, a string unescaped
        std::vector<JsonValue>  items;
        std::vector<JsonField>  fields;
    };

    struct JsonField
    {
        std::string first;
        JsonValue   second;
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

    // Rotations worked out more than one way, where the VM's own way is not
    // known: LLQuaternion's of today and of 2007, in singles a step at a
    // time and in doubles. What every way comes to as singles, where they
    // all come to the same; nothing where they part.
    template <class T> struct Quat
    {
        T x, y, z, s;
    };

    std::optional<std::array<float, 4>> agreed(std::initializer_list<std::optional<Quat<double>>> ways)
    {
        std::optional<std::array<float, 4>> out;
        for (const std::optional<Quat<double>>& way : ways)
        {
            if (!way)
            {
                return std::nullopt;
            }
            const std::array<float, 4> q = { static_cast<float>(way->x), static_cast<float>(way->y), static_cast<float>(way->z), static_cast<float>(way->s) };
            for (float part : q)
            {
                if (!std::isfinite(part))
                {
                    return std::nullopt;
                }
            }
            if (out && *out != q)
            {
                return std::nullopt;
            }
            out = q;
        }
        return out;
    }

    template <class T> std::optional<Quat<double>> widened(const std::optional<Quat<T>>& q)
    {
        if (!q)
        {
            return std::nullopt;
        }
        return Quat<double>{ double(q->x), double(q->y), double(q->z), double(q->s) };
    }

    // A quaternion made of unit length: today's, always; 2007's only where
    // it is more than a part in a million away.
    template <class T> Quat<T> unitQuat(Quat<T> q, bool nearlyLeft)
    {
        const T mag = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.s * q.s);
        if (mag > T(0.0000001) && (!nearlyLeft || std::fabs(mag - T(1)) > T(0.000001)))
        {
            const T over = T(1) / mag;
            q            = { q.x * over, q.y * over, q.z * over, q.s * over };
        }
        return q;
    }

    // llRotBetween as LLQuaternion::shortestArc has it today: for vectors
    // neither parallel nor opposed.
    template <class T> std::optional<Quat<T>> arcToday(const Vector3& u, const Vector3& v)
    {
        const T ax = u.x, ay = u.y, az = u.z, bx = v.x, by = v.y, bz = v.z;
        const T ab = ax * bx + ay * by + az * bz;
        const T cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        const T cc = cx * cx + cy * cy + cz * cz;
        if (!(ab * ab + cc > T(0)) || !(cc > T(0)))
        {
            return std::nullopt;
        }
        const T sum  = std::sqrt(ab * ab + cc) + ab;
        const T over = T(1) / std::sqrt(cc + sum * sum);
        return Quat<T>{ cx * over, cy * over, cz * over, sum * over };
    }

    // And as it had it in 2007: each vector made unit, then the angle from
    // its cosine about the cross product made unit.
    template <class T> std::optional<Quat<T>> arc2007(const Vector3& u, const Vector3& v)
    {
        const auto unit = [](T& x, T& y, T& z) {
            const T mag = std::sqrt(x * x + y * y + z * z);
            if (mag > T(0.0000001))
            {
                const T over = T(1) / mag;
                x *= over;
                y *= over;
                z *= over;
            }
            return mag;
        };
        T ax = u.x, ay = u.y, az = u.z, bx = v.x, by = v.y, bz = v.z;
        if (unit(ax, ay, az) < T(0.00001) || unit(bx, by, bz) < T(0.00001))
        {
            return std::nullopt;
        }
        T       cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        const T cosine = ax * bx + ay * by + az * bz;
        if (cosine > T(1.0 - 0.00001) || cosine < T(-1.0 + 0.00001))
        {
            return std::nullopt;
        }
        const T theta = static_cast<T>(std::acos(double(cosine)));
        unit(cx, cy, cz);
        const T half = theta * T(0.5);
        const T c = std::cos(half), sn = std::sin(half);
        return unitQuat(Quat<T>{ cx * sn, cy * sn, cz * sn, c }, true);
    }

    // llAngleBetween: twice the arccosine of the rotations' dot product over
    // their lengths; and, a way apart, twice the angle of the rotation from
    // one to the other, by its arctangent.
    template <class T> std::optional<double> angleByCosine(const Quaternion& p, const Quaternion& q)
    {
        const T ax = p.x, ay = p.y, az = p.z, as = p.s, bx = q.x, by = q.y, bz = q.z, bs = q.s;
        const T aa = ax * ax + ay * ay + az * az + as * as, bb = bx * bx + by * by + bz * bz + bs * bs;
        const T aabb = std::sqrt(aa * bb);
        if (!(aabb > T(0)))
        {
            return std::nullopt;
        }
        T ab = std::fabs((ax * bx + ay * by + az * bz + as * bs) / aabb);
        if (ab > T(1))
        {
            ab = T(1);
        }
        return double(T(2) * std::acos(ab));
    }

    std::optional<double> angleByTangent(const Quaternion& p, const Quaternion& q)
    {
        const double ax = p.x, ay = p.y, az = p.z, as = p.s, bx = q.x, by = q.y, bz = q.z, bs = q.s;
        const double w  = as * bs + ax * bx + ay * by + az * bz;
        const double vx = as * bx - bs * ax - (ay * bz - az * by);
        const double vy = as * by - bs * ay - (az * bx - ax * bz);
        const double vz = as * bz - bs * az - (ax * by - ay * bx);
        const double v  = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (!(v > 0.0 || w != 0.0))
        {
            return std::nullopt;
        }
        return 2.0 * std::atan2(v, std::fabs(w));
    }

    // llAxes2Rot as LLMatrix3::quaternion has it where the trace is above
    // nought, then made unit as today or as in 2007; and, a way apart, each
    // part from its own square root, signed by the matrix.
    template <class T> std::optional<Quat<T>> axesByTrace(const Vector3& f, const Vector3& l, const Vector3& u, bool nearlyLeft)
    {
        const T m[3][3] = { { f.x, f.y, f.z }, { l.x, l.y, l.z }, { u.x, u.y, u.z } };
        const T tr      = m[0][0] + m[1][1] + m[2][2];
        if (!(tr > T(0)))
        {
            return std::nullopt;
        }
        T       root = std::sqrt(tr + T(1));
        const T w    = root * T(0.5);
        root         = T(0.5) / root;
        return unitQuat(Quat<T>{ (m[1][2] - m[2][1]) * root, (m[2][0] - m[0][2]) * root, (m[0][1] - m[1][0]) * root, w }, nearlyLeft);
    }

    std::optional<Quat<double>> axesByParts(const Vector3& f, const Vector3& l, const Vector3& u)
    {
        const double m[3][3] = { { f.x, f.y, f.z }, { l.x, l.y, l.z }, { u.x, u.y, u.z } };
        const auto   part    = [](double square) { return 0.5 * std::sqrt(std::max(0.0, square)); };
        const double w       = part(1.0 + m[0][0] + m[1][1] + m[2][2]);
        if (!(w > 0.0))
        {
            return std::nullopt;
        }
        return Quat<double>{ std::copysign(part(1.0 + m[0][0] - m[1][1] - m[2][2]), m[1][2] - m[2][1]),
                             std::copysign(part(1.0 - m[0][0] + m[1][1] - m[2][2]), m[2][0] - m[0][2]),
                             std::copysign(part(1.0 - m[0][0] - m[1][1] + m[2][2]), m[0][1] - m[1][0]), w };
    }

    // A list's strings, where every element is one: what llParseString2List
    // is given to split at.
    std::optional<std::vector<std::string>> strings(LSLListConstant* list)
    {
        std::vector<std::string> out;
        for (LSLConstant* item : elements(list))
        {
            if (item->getNodeSubType() != NODE_STRING_CONSTANT)
            {
                return std::nullopt;
            }
            out.emplace_back(static_cast<LSLStringConstant*>(item)->getValue());
        }
        return out;
    }

    // llParseString2List's and llParseStringKeepNulls' splitting: at each
    // place the separator or spacer found there, the separator dropped and
    // the spacer kept; what is between, empty or not as `nulls` says. Only
    // where which one is found cannot be in question -- at most eight of
    // each, which is as many as the VM reads, none empty and none the start
    // of another -- and where everything is ASCII.
    std::optional<std::vector<std::string>> parsed(const std::string& src, const std::vector<std::string>& separators,
                                                   const std::vector<std::string>& spacers, bool nulls)
    {
        if (src.empty() || separators.size() > 8 || spacers.size() > 8 || !ascii(src.c_str()))
        {
            return std::nullopt;
        }
        std::vector<std::pair<std::string, bool>> marks; // the mark, and whether it is kept
        for (const std::string& sep : separators)
        {
            marks.emplace_back(sep, false);
        }
        for (const std::string& spacer : spacers)
        {
            marks.emplace_back(spacer, true);
        }
        for (const auto& [mark, kept] : marks)
        {
            if (mark.empty() || !ascii(mark.c_str()))
            {
                return std::nullopt;
            }
            for (const auto& [other, also] : marks)
            {
                if (&other != &mark && other.size() >= mark.size() && other.compare(0, mark.size(), mark) == 0)
                {
                    return std::nullopt;
                }
            }
        }
        std::vector<std::string> out;
        const auto               piece = [&](const std::string& text) {
            if (nulls || !text.empty())
            {
                out.push_back(text);
            }
        };
        size_t from = 0;
        for (size_t at = 0; at < src.size();)
        {
            const auto found = std::find_if(marks.begin(), marks.end(), [&](const auto& m) { return src.compare(at, m.first.size(), m.first) == 0; });
            if (found == marks.end())
            {
                ++at;
                continue;
            }
            piece(src.substr(from, at - from));
            if (found->second)
            {
                out.push_back(found->first);
            }
            at += found->first.size();
            from = at;
        }
        piece(src.substr(from));
        return out;
    }

    // Strings llListSort orders the same by their code points as by any
    // culture's rules: letters and digits only, and the letters of all of
    // them of one case -- "B" comes before "a" by code point, after it by a
    // culture's. What letters a string has is told into `lower` and
    // `upper`; false for one that has anything else.
    bool plainSortKey(const char* s, bool& lower, bool& upper)
    {
        for (; *s; ++s)
        {
            const unsigned char ch = static_cast<unsigned char>(*s);
            if (ch >= 'a' && ch <= 'z')
            {
                lower = true;
            }
            else if (ch >= 'A' && ch <= 'Z')
            {
                upper = true;
            }
            else if (ch < '0' || ch > '9')
            {
                return false;
            }
        }
        return !(lower && upper);
    }

    // A vector's length, in singles a step at a time, as LLVector3 works it
    // out, and as a double rounded once to a single: whether the two agree,
    // and what they come to where they do. On Luau, whose answer the
    // simulator gives by a way not known, a single or a double, only where
    // the single is the double too.
    bool magnitude(const Ctx& c, float x, float y, float z, double& out)
    {
        const double once    = std::sqrt(double(x) * x + double(y) * y + double(z) * z);
        const float  xx      = x * x, yy = y * y, zz = z * z;
        const float  sum     = (xx + yy) + zz;
        const float  stepped = std::sqrt(sum);
        out                  = stepped;
        const bool agree     = std::isfinite(stepped) && static_cast<double>(static_cast<float>(once)) == static_cast<double>(stepped);
        return agree && (c.target != ALLSLOptimizer::Target::Luau || static_cast<double>(stepped) == once);
    }

    // An ll function of floats, whose answer is a single on every target,
    // folded only where two ways of working it out agree: in double and
    // rounded once, as the VMs do it, and in single precision. This host's
    // library is not the grid's -- glibc on 32-bit Linux, and .NET's under
    // Mono -- and where the answer lies so near a single's rounding that two
    // libraries could round it apart, the two ways here part too, and the VM
    // is left to say. Its sign kept: -0 is not 0 to a single's text.
    template <class Double, class Single>
    LSLConstant* agreedSingle(Ctx& c, Double inDouble, Single inSingle)
    {
        const float once = static_cast<float>(inDouble());
        const float each = inSingle();
        if (!std::isfinite(once) || once != each || std::signbit(once) != std::signbit(each))
        {
            return nullptr;
        }
        return c.single(once);
    }

    // Either zero made +0, by its bits: under /fp:fast a comparison with 0
    // may hand back the -0 it was given.
    template <class T>
    T unsignedZero(T r)
    {
        using Bits      = std::conditional_t<sizeof(T) == sizeof(uint64_t), uint64_t, uint32_t>;
        const Bits bits = std::bit_cast<Bits>(r);
        return std::bit_cast<T>(static_cast<Bits>(bits << 1) == 0 ? Bits(0) : bits);
    }

    // Whether an argument is exactly a single: what sin, cos and tan take on
    // Luau, whose SLua takes its double as it is where the others round it.
    bool exactSingle(const Args& a, size_t i)
    {
        if (i >= a.size())
        {
            return false;
        }
        if (a[i]->getNodeSubType() == NODE_FLOAT_CONSTANT)
        {
            const double v = static_cast<LSLFloatConstant*>(a[i])->getValue();
            return static_cast<double>(static_cast<float>(v)) == v;
        }
        if (a[i]->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            const int v = static_cast<LSLIntegerConstant*>(a[i])->getValue();
            return static_cast<int>(static_cast<float>(v)) == v;
        }
        return false;
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
                  return argFloat(a, 0, v) ? c.single(static_cast<float>(std::fabs(v))) : nullptr;
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
            // The functions of floats, each where its two ways agree
            // (agreedSingle), and with what SLua's own make of their edges,
            // which say they are Mono's: a pow or an atan2 of -0 is 0.
            { "llSqrt",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || v < 0.0) return nullptr;
                  return agreedSingle(c, [v] { return std::sqrt(v); }, [v] { return std::sqrt(static_cast<float>(v)); });
              } },
            { "llPow",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double b, e;
                  if (!argFloat(a, 0, b) || !argFloat(a, 1, e) || !std::isfinite(b) || !std::isfinite(e)) return nullptr;
                  return agreedSingle(c, [&] { return unsignedZero(std::pow(b, e)); },
                                      [&] { return unsignedZero(std::pow(static_cast<float>(b), static_cast<float>(e))); });
              } },
            { "llSin",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || (c.target == ALLSLOptimizer::Target::Luau && !exactSingle(a, 0))) return nullptr;
                  return agreedSingle(c, [v] { return std::sin(v); }, [v] { return std::sin(static_cast<float>(v)); });
              } },
            { "llCos",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || (c.target == ALLSLOptimizer::Target::Luau && !exactSingle(a, 0))) return nullptr;
                  return agreedSingle(c, [v] { return std::cos(v); }, [v] { return std::cos(static_cast<float>(v)); });
              } },
            { "llTan",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || (c.target == ALLSLOptimizer::Target::Luau && !exactSingle(a, 0))) return nullptr;
                  return agreedSingle(c, [v] { return std::tan(v); }, [v] { return std::tan(static_cast<float>(v)); });
              } },
            { "llAtan2",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double y, x;
                  if (!argFloat(a, 0, y) || !argFloat(a, 1, x) || !std::isfinite(y) || !std::isfinite(x)) return nullptr;
                  return agreedSingle(c, [&] { return unsignedZero(std::atan2(y, x)); },
                                      [&] { return unsignedZero(std::atan2(static_cast<float>(y), static_cast<float>(x))); });
              } },
            { "llLog",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || v <= 0.0) return nullptr;
                  return agreedSingle(c, [v] { return std::log(v); }, [v] { return std::log(static_cast<float>(v)); });
              } },
            { "llLog10",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || v <= 0.0) return nullptr;
                  return agreedSingle(c, [v] { return std::log10(v); }, [v] { return std::log10(static_cast<float>(v)); });
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
            // A vector's length as LLVector3 has it, in singles a step at a
            // time, and as a double rounded once: which the VM does is not
            // known, so folded only where the two agree (magnitude). And a
            // vector normalised by dividing each part by the length, and by
            // the product with 1 / length that LLVector3 makes of a division,
            // likewise.
            { "llVecMag",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  double  mag = 0.0;
                  return argVector(a, 0, v) && magnitude(c, v.x, v.y, v.z, mag) ? c.number(mag) : nullptr;
              } },
            { "llVecDist",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 p, q;
                  double  mag = 0.0;
                  if (!argVector(a, 0, p) || !argVector(a, 1, q)) return nullptr;
                  const float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
                  return magnitude(c, dx, dy, dz, mag) ? c.number(mag) : nullptr;
              } },
            { "llVecNorm",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 v;
                  double  length = 0.0;
                  if (!argVector(a, 0, v) || !magnitude(c, v.x, v.y, v.z, length)) return nullptr;
                  const float mag = static_cast<float>(length);
                  if (mag == 0.0f) return c.vector(0, 0, 0);
                  const float x = v.x / mag, y = v.y / mag, z = v.z / mag;
                  const float over = 1.0f / mag;
                  if (x != v.x * over || y != v.y * over || z != v.z * over) return nullptr;
                  return c.vector(x, y, z);
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
            // Three more by more than one way each (agreed): left where the
            // ways part, which an answer near a parallel, a half turn or
            // nothing does.
            { "llRotBetween",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 u, v;
                  if (!argVector(a, 0, u) || !argVector(a, 1, v)) return nullptr;
                  const auto q = agreed({ widened(arcToday<float>(u, v)), arcToday<double>(u, v), widened(arc2007<float>(u, v)), arc2007<double>(u, v) });
                  return q ? c.rotation((*q)[0], (*q)[1], (*q)[2], (*q)[3]) : nullptr;
              } },
            { "llAngleBetween",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  // Luau's answer is a double, by a way not known.
                  if (c.target == ALLSLOptimizer::Target::Luau || a.size() != 2 || a[0]->getNodeSubType() != NODE_QUATERNION_CONSTANT ||
                      a[1]->getNodeSubType() != NODE_QUATERNION_CONSTANT)
                  {
                      return nullptr;
                  }
                  const Quaternion& p = *static_cast<LSLQuaternionConstant*>(a[0])->getValue();
                  const Quaternion& q = *static_cast<LSLQuaternionConstant*>(a[1])->getValue();
                  std::optional<float> out;
                  for (const std::optional<double>& way : { angleByCosine<float>(p, q), angleByCosine<double>(p, q), angleByTangent(p, q) })
                  {
                      if (!way || !std::isfinite(static_cast<float>(*way)) || (out && *out != static_cast<float>(*way))) return nullptr;
                      out = static_cast<float>(*way);
                  }
                  return c.number(*out);
              } },
            { "llAxes2Rot",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  Vector3 f, l, u;
                  if (!argVector(a, 0, f) || !argVector(a, 1, l) || !argVector(a, 2, u)) return nullptr;
                  const auto q = agreed({ widened(axesByTrace<float>(f, l, u, false)), widened(axesByTrace<float>(f, l, u, true)),
                                          axesByTrace<double>(f, l, u, false), axesByParts(f, l, u) });
                  return q ? c.rotation((*q)[0], (*q)[1], (*q)[2], (*q)[3]) : nullptr;
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
            // A list split, as LSL splits it, where nothing could make the
            // two VMs split it otherwise (parsed): no spacers for the one
            // that keeps what is empty, whose empties beside a spacer are
            // not known.
            { "llParseString2List",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string      src;
                  LSLListConstant *seps, *spacers;
                  if (!argString(a, 0, src) || !argList(a, 1, seps) || !argList(a, 2, spacers)) return nullptr;
                  const auto sepText = strings(seps), spacerText = strings(spacers);
                  if (!sepText || !spacerText) return nullptr;
                  const auto pieces = parsed(src, *sepText, *spacerText, false);
                  if (!pieces) return nullptr;
                  std::vector<LSLConstant*> out;
                  for (const std::string& piece : *pieces)
                  {
                      LSLConstant* item = c.string(piece);
                      if (!item) return nullptr;
                      out.push_back(item);
                  }
                  return listOf(c, out);
              } },
            { "llParseStringKeepNulls",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string      src;
                  LSLListConstant *seps, *spacers;
                  if (!argString(a, 0, src) || !argList(a, 1, seps) || !argList(a, 2, spacers) || spacers->getLength() != 0) return nullptr;
                  const auto sepText = strings(seps);
                  if (!sepText) return nullptr;
                  const auto pieces = parsed(src, *sepText, {}, true);
                  if (!pieces) return nullptr;
                  std::vector<LSLConstant*> out;
                  for (const std::string& piece : *pieces)
                  {
                      LSLConstant* item = c.string(piece);
                      if (!item) return nullptr;
                      out.push_back(item);
                  }
                  return listOf(c, out);
              } },
            // Comma-separated values as strings: only where no value is
            // empty, starts or ends with a space, or holds the angle
            // brackets that keep a vector's commas in -- what the two VMs
            // are not known to treat alike.
            { "llCSV2List",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  std::string src;
                  if (!argString(a, 0, src) || src.empty() || src.find_first_of("<>") != std::string::npos) return nullptr;
                  const auto pieces = parsed(src, { "," }, {}, true);
                  if (!pieces) return nullptr;
                  std::vector<LSLConstant*> out;
                  for (const std::string& piece : *pieces)
                  {
                      if (piece.empty() || std::isspace(static_cast<unsigned char>(piece.front())) ||
                          std::isspace(static_cast<unsigned char>(piece.back())))
                      {
                          return nullptr;
                      }
                      LSLConstant* item = c.string(piece);
                      if (!item) return nullptr;
                      out.push_back(item);
                  }
                  return listOf(c, out);
              } },
            // A list sorted by every stride-th element, ascending or not:
            // only where the order cannot be the sort's to choose -- the
            // keys all of one type, integers, floats or strings a culture
            // orders as their code points do (plainSortKey), and no two the
            // same, so that a sort that is not stable sorts them alike --
            // and the stride divides the list.
            { "llListSort",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  LSLListConstant* l;
                  int              stride, ascending;
                  if (!argList(a, 0, l) || !argInt(a, 1, stride) || !argInt(a, 2, ascending) || (ascending != 0 && ascending != 1)) return nullptr;
                  const std::vector<LSLConstant*> items = elements(l);
                  if (stride < 1 || items.size() % static_cast<size_t>(stride) != 0 || items.empty()) return nullptr;
                  const LSLNodeSubType kind = items.front()->getNodeSubType();
                  if (kind != NODE_INTEGER_CONSTANT && kind != NODE_FLOAT_CONSTANT && kind != NODE_STRING_CONSTANT) return nullptr;
                  std::vector<size_t> groups;
                  bool                lower = false, upper = false;
                  for (size_t g = 0; g < items.size(); g += static_cast<size_t>(stride))
                  {
                      LSLConstant* key = items[g];
                      if (key->getNodeSubType() != kind) return nullptr;
                      if (kind == NODE_FLOAT_CONSTANT && !std::isfinite(static_cast<LSLFloatConstant*>(key)->getValue())) return nullptr;
                      if (kind == NODE_STRING_CONSTANT && !plainSortKey(static_cast<LSLStringConstant*>(key)->getValue(), lower, upper)) return nullptr;
                      groups.push_back(g);
                  }
                  const auto less = [&](size_t x, size_t y) {
                      LSLConstant* p = items[x];
                      LSLConstant* q = items[y];
                      switch (kind)
                      {
                          case NODE_INTEGER_CONSTANT:
                              return static_cast<LSLIntegerConstant*>(p)->getValue() < static_cast<LSLIntegerConstant*>(q)->getValue();
                          case NODE_FLOAT_CONSTANT:
                              return static_cast<LSLFloatConstant*>(p)->getValue() < static_cast<LSLFloatConstant*>(q)->getValue();
                          default:
                              return strcmp(static_cast<LSLStringConstant*>(p)->getValue(), static_cast<LSLStringConstant*>(q)->getValue()) < 0;
                      }
                  };
                  std::sort(groups.begin(), groups.end(), [&](size_t x, size_t y) { return ascending ? less(x, y) : less(y, x); });
                  for (size_t i = 1; i < groups.size(); ++i)
                  {
                      if (!less(groups[i - 1], groups[i]) && !less(groups[i], groups[i - 1])) return nullptr;
                  }
                  std::vector<LSLConstant*> out;
                  for (size_t g : groups)
                  {
                      out.insert(out.end(), items.begin() + static_cast<std::ptrdiff_t>(g), items.begin() + static_cast<std::ptrdiff_t>(g) + stride);
                  }
                  return listOf(c, out);
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
                          // Within 32 bits: past them, what the simulator
                          // makes of one is its 32-bit host's business, and
                          // a long is 64 bits here, and 32 on Windows.
                          const long long n = v.text.size() <= 11 ? std::strtoll(v.text.c_str(), nullptr, 10) : INT64_MAX;
                          if (n < INT32_MIN || n > INT32_MAX) return false;
                          out.push_back(c.integer(static_cast<int>(n)));
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
                  if (!argFloat(a, 0, v) || std::fabs(v) > 1.0) return nullptr;
                  return agreedSingle(c, [v] { return std::acos(v); }, [v] { return std::acos(static_cast<float>(v)); });
              } },
            { "llAsin",
              [](Ctx& c, const Args& a) -> LSLConstant* {
                  double v;
                  if (!argFloat(a, 0, v) || std::fabs(v) > 1.0) return nullptr;
                  return agreedSingle(c, [v] { return std::asin(v); }, [v] { return std::asin(static_cast<float>(v)); });
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
}

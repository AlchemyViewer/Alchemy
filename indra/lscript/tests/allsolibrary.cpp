/**
 * @file allsolibrary.cpp
 * @brief The LSL library as LL's LSO VM runs it in a test
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "allsolibrary.h"

#include "../lscript_alloc.h"
#include "llquaternion.h"
#include "llstring.h"
#include "v3math.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Where SLua implements a function (VM/src/lll.cpp and llsl.cpp, its LSL
// builtins on the Luau VM: the list accessors and slices, the math, and the
// list element casts), these answer as it does, edge cases included; the
// rest follow LSL's documentation. Indices count characters, which LSL's
// strings are made of, not bytes.

namespace ll_test_lso
{
    std::vector<std::string> gSaid;
}

namespace
{
    using Data = LLScriptLibData;
    using exec_t = decltype(LLScriptLibraryFunction::mExecFunc);

    const char* const NULL_KEY = "00000000-0000-0000-0000-000000000000";

    // --- values ---------------------------------------------------------------

    char* copied(const std::string& text)
    {
        char* out = new char[text.size() + 1];
        std::memcpy(out, text.c_str(), text.size() + 1);
        return out;
    }

    void setString(Data* retval, const std::string& text)
    {
        retval->mString = copied(text);
    }

    void setKey(Data* retval, const std::string& text)
    {
        retval->mKey = copied(text);
    }

    // A string or key argument's text: a key may come where a string is
    // asked for, and the other way.
    std::string textOf(const Data& d)
    {
        if (d.mString)
        {
            return d.mString;
        }
        if (d.mKey)
        {
            return d.mKey;
        }
        return std::string();
    }

    std::string formatted(const char* format, double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), format, value);
        return buffer;
    }

    // An element as LSL makes a list's element a string: floats, vectors and
    // rotations to six places; a negative zero kept, except where SLua's
    // llDumpList2String casts, which drops it.
    std::string elementText(const Data& d, bool neg_zero = true)
    {
        auto f = [neg_zero](F32 value) { return formatted("%f", !neg_zero && value == 0.f ? 0.f : value); };
        switch (d.mType)
        {
        case LST_INTEGER:
            return std::to_string(d.mInteger);
        case LST_FLOATINGPOINT:
            return f(d.mFP);
        case LST_STRING:
        case LST_KEY:
            return textOf(d);
        case LST_VECTOR:
            return "<" + f(d.mVec.mV[VX]) + ", " + f(d.mVec.mV[VY]) + ", " + f(d.mVec.mV[VZ]) + ">";
        case LST_QUATERNION:
            return "<" + f(d.mQuat.mQ[VX]) + ", " + f(d.mQuat.mQ[VY]) + ", " + f(d.mQuat.mQ[VZ]) + ", " + f(d.mQuat.mQ[VW]) + ">";
        default:
            return std::string();
        }
    }

    // A string as the characters LSL counts, and back.
    std::vector<llwchar> charactersOf(const std::string& text)
    {
        std::vector<llwchar> out;
        for (size_t at = 0; at < text.size();)
        {
            const LLCodepointAt cp = utf8str_decode_at(text, at);
            out.push_back(cp.cp);
            at = cp.next;
        }
        return out;
    }

    std::string textFrom(const std::vector<llwchar>& characters)
    {
        std::string out;
        for (llwchar c : characters)
        {
            utf8str_append_cp(out, c);
        }
        return out;
    }

    // LSL's float to integer: what does not fit is the least integer.
    S32 integerFrom(double f) { return std::isfinite(f) && f >= -2147483648.0 && f < 2147483648.0 ? (S32)f : S32_MIN; }

    // A list element cast as SLua's lsl_cast_list_elem casts it. A string to
    // an integer is strtoul's, hexadecimal only where it begins 0x; a key
    // casts to nothing but a string, so is 0 here.
    S32 asInteger(const Data& d)
    {
        switch (d.mType)
        {
        case LST_INTEGER:
            return d.mInteger;
        case LST_FLOATINGPOINT:
            return integerFrom(d.mFP);
        case LST_STRING:
        {
            const char* text = d.mString ? d.mString : "";
            const bool  hex  = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
            return (S32)(U32)std::strtoul(text, nullptr, hex ? 16 : 10);
        }
        default:
            return 0;
        }
    }

    F32 asFloat(const Data& d)
    {
        switch (d.mType)
        {
        case LST_INTEGER:
            return (F32)d.mInteger;
        case LST_FLOATINGPOINT:
            return d.mFP;
        case LST_STRING:
            return (F32)std::atof(d.mString ? d.mString : "");
        default:
            return 0.f;
        }
    }

    // --- lists ----------------------------------------------------------------

    std::vector<const Data*> elementsOf(const Data& list)
    {
        std::vector<const Data*> out;
        for (const Data* item = list.mListp; item; item = item->mListp)
        {
            out.push_back(item);
        }
        return out;
    }

    // The list returned: copies of these, in order, each its own.
    void setList(Data* retval, const std::vector<const Data*>& items)
    {
        Data* tail = nullptr;
        for (const Data* item : items)
        {
            Data* copy   = new Data(*item);
            copy->mListp = nullptr;
            if (tail)
            {
                tail->mListp = copy;
            }
            else
            {
                retval->mListp = copy;
            }
            tail = copy;
        }
    }

    void appendString(std::vector<Data>& made, const std::string& text)
    {
        made.emplace_back(text.c_str());
    }

    void setListOf(Data* retval, const std::vector<Data>& made)
    {
        std::vector<const Data*> items;
        for (const Data& d : made)
        {
            items.push_back(&d);
        }
        setList(retval, items);
    }

    // The indices start..end selects of n, as LSL's sublist and substring
    // functions read them: from the end where negative, and where start lies
    // past end, everything but what lies between them.
    std::vector<S32> selected(S32 n, S32 start, S32 end)
    {
        if (start < 0)
        {
            start += n;
        }
        if (end < 0)
        {
            end += n;
        }
        std::vector<S32> out;
        if (start <= end)
        {
            for (S32 i = std::max(start, 0); i <= std::min(end, n - 1); ++i)
            {
                out.push_back(i);
            }
        }
        else
        {
            for (S32 i = 0; i <= std::min(end, n - 1); ++i)
            {
                out.push_back(i);
            }
            for (S32 i = std::max(start, 0); i < n; ++i)
            {
                out.push_back(i);
            }
        }
        return out;
    }

    std::vector<S32> unselected(S32 n, S32 start, S32 end)
    {
        const std::vector<S32> gone = selected(n, start, end);
        std::vector<S32>       out;
        for (S32 i = 0; i < n; ++i)
        {
            if (std::find(gone.begin(), gone.end(), i) == gone.end())
            {
                out.push_back(i);
            }
        }
        return out;
    }

    // An index from the end where negative; past either end, none.
    const Data* at(const std::vector<const Data*>& items, S32 index)
    {
        const S32 n = (S32)items.size();
        if (index < 0)
        {
            index += n;
        }
        return index >= 0 && index < n ? items[index] : nullptr;
    }

    // --- chat -----------------------------------------------------------------

    void say(Data*, Data* args, const LLUUID&) { ll_test_lso::gSaid.push_back(textOf(args[1])); }
    void ownerSay(Data*, Data* args, const LLUUID&) { ll_test_lso::gSaid.push_back(textOf(args[0])); }
    void regionSayTo(Data*, Data* args, const LLUUID&) { ll_test_lso::gSaid.push_back(textOf(args[2])); }

    // --- math -----------------------------------------------------------------

    void abs_(Data* r, Data* a, const LLUUID&) { r->mInteger = a[0].mInteger == S32_MIN ? S32_MIN : std::abs(a[0].mInteger); }
    // As SLua's, which keep Mono's: a negative zero passes as it is.
    void fabs_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP < 0.f ? -a[0].mFP : a[0].mFP; }
    void ceil_(Data* r, Data* a, const LLUUID&) { r->mInteger = integerFrom(std::ceil((double)a[0].mFP)); }
    void floor_(Data* r, Data* a, const LLUUID&) { r->mInteger = integerFrom(std::floor((double)a[0].mFP)); }
    void round_(Data* r, Data* a, const LLUUID&) { r->mInteger = integerFrom(std::floor((double)a[0].mFP + 0.5)); }
    // Worked in double and kept as float, as SLua's are, with its answers to
    // what libm leaves open.
    F32 posZero(double f) { return f == 0.0 ? 0.f : (F32)f; }
    void sqrt_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP < 0.f ? NAN : (F32)std::sqrt((double)a[0].mFP); }
    void pow_(Data* r, Data* a, const LLUUID&)
    {
        constexpr double LARGEST_INT_DOUBLE = (1ULL << 53) - 1;
        const double     x = a[0].mFP, y = a[1].mFP;
        if (std::isnan(x) || std::isnan(y) || ((x == 1 || x == -1) && std::isinf(y)))
        {
            r->mFP = NAN;
            return;
        }
        if (x < -LARGEST_INT_DOUBLE && (y > LARGEST_INT_DOUBLE || y < -LARGEST_INT_DOUBLE))
        {
            r->mFP = y > 0 ? HUGE_VALF : 0.f;
            return;
        }
        const double result = std::pow(x, y);
        if (std::isnan(result) && x == -1.0 && (y > LARGEST_INT_DOUBLE || y < -LARGEST_INT_DOUBLE))
        {
            r->mFP = 1.f;
            return;
        }
        r->mFP = posZero(result);
    }
    void sin_(Data* r, Data* a, const LLUUID&) { r->mFP = (F32)std::sin((double)a[0].mFP); }
    void cos_(Data* r, Data* a, const LLUUID&) { r->mFP = (F32)std::cos((double)a[0].mFP); }
    void tan_(Data* r, Data* a, const LLUUID&) { r->mFP = (F32)std::tan((double)a[0].mFP); }
    void asin_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP < -1 || a[0].mFP > 1 ? NAN : (F32)std::asin((double)a[0].mFP); }
    void acos_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP < -1 || a[0].mFP > 1 ? NAN : (F32)std::acos((double)a[0].mFP); }
    void atan2_(Data* r, Data* a, const LLUUID&)
    {
        const double y = a[0].mFP, x = a[1].mFP;
        r->mFP         = std::isinf(y) && std::isinf(x) ? NAN : posZero(std::atan2(y, x));
    }
    // Nothing where it is no positive number, as SLua and Mono have it.
    void log_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP > 0.f ? (F32)std::log((double)a[0].mFP) : 0.f; }
    void log10_(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP > 0.f ? (F32)std::log10((double)a[0].mFP) : 0.f; }
    // Not random: half the bound, so a test knows what it gets.
    void frand(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mFP / 2; }
    void modPow(Data* r, Data* a, const LLUUID&)
    {
        const S64 m = a[2].mInteger;
        if (m == 0)
        {
            r->mInteger = 0;
            return;
        }
        S64 base = ((S64)a[0].mInteger % m + m) % m;
        S64 exp  = a[1].mInteger & 0xFFFF;
        S64 out  = 1 % m;
        while (exp > 0)
        {
            if (exp & 1)
            {
                out = out * base % m;
            }
            base = base * base % m;
            exp >>= 1;
        }
        r->mInteger = (S32)out;
    }

    // --- vectors and rotations -----------------------------------------------

    void vecMag(Data* r, Data* a, const LLUUID&) { r->mFP = a[0].mVec.magVec(); }
    void vecNorm(Data* r, Data* a, const LLUUID&)
    {
        r->mVec = a[0].mVec;
        r->mVec.normVec();
    }
    void vecDist(Data* r, Data* a, const LLUUID&) { r->mFP = (a[0].mVec - a[1].mVec).magVec(); }
    void rot2Euler(Data* r, Data* a, const LLUUID&)
    {
        F32 roll, pitch, yaw;
        a[0].mQuat.getEulerAngles(&roll, &pitch, &yaw);
        r->mVec.setVec(roll, pitch, yaw);
    }
    void euler2Rot(Data* r, Data* a, const LLUUID&) { r->mQuat.setQuat(a[0].mVec.mV[VX], a[0].mVec.mV[VY], a[0].mVec.mV[VZ]); }
    void rot2Fwd(Data* r, Data* a, const LLUUID&) { r->mVec = LLVector3::x_axis * a[0].mQuat; }
    void rot2Left(Data* r, Data* a, const LLUUID&) { r->mVec = LLVector3::y_axis * a[0].mQuat; }
    void rot2Up(Data* r, Data* a, const LLUUID&) { r->mVec = LLVector3::z_axis * a[0].mQuat; }
    void axisAngle2Rot(Data* r, Data* a, const LLUUID&)
    {
        LLVector3 axis = a[0].mVec;
        axis.normVec();
        r->mQuat.setQuat(a[1].mFP, axis);
    }
    void rot2Angle(Data* r, Data* a, const LLUUID&)
    {
        F32       angle;
        LLVector3 axis;
        a[0].mQuat.getAngleAxis(&angle, axis);
        r->mFP = angle;
    }
    void rot2Axis(Data* r, Data* a, const LLUUID&)
    {
        F32 angle;
        a[0].mQuat.getAngleAxis(&angle, r->mVec);
    }
    void rotBetween(Data* r, Data* a, const LLUUID&) { r->mQuat.shortestArc(a[0].mVec, a[1].mVec); }
    void angleBetween(Data* r, Data* a, const LLUUID&)
    {
        const LLQuaternion between = ~a[0].mQuat * a[1].mQuat;
        F32                angle;
        LLVector3          axis;
        between.getAngleAxis(&angle, axis);
        r->mFP = angle;
    }

    // --- strings --------------------------------------------------------------

    void stringLength(Data* r, Data* a, const LLUUID&) { r->mInteger = (S32)charactersOf(textOf(a[0])).size(); }
    void getSubString(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<llwchar> text = charactersOf(textOf(a[0]));
        std::vector<llwchar>       out;
        for (S32 i : selected((S32)text.size(), a[1].mInteger, a[2].mInteger))
        {
            out.push_back(text[i]);
        }
        setString(r, textFrom(out));
    }
    void deleteSubString(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<llwchar> text = charactersOf(textOf(a[0]));
        std::vector<llwchar>       out;
        for (S32 i : unselected((S32)text.size(), a[1].mInteger, a[2].mInteger))
        {
            out.push_back(text[i]);
        }
        setString(r, textFrom(out));
    }
    void insertString(Data* r, Data* a, const LLUUID&)
    {
        std::vector<llwchar>       text = charactersOf(textOf(a[0]));
        const std::vector<llwchar> more = charactersOf(textOf(a[2]));
        const S32                  pos  = std::clamp(a[1].mInteger, 0, (S32)text.size());
        text.insert(text.begin() + pos, more.begin(), more.end());
        setString(r, textFrom(text));
    }
    void subStringIndex(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<llwchar> text = charactersOf(textOf(a[0]));
        const std::vector<llwchar> part = charactersOf(textOf(a[1]));
        const auto                 found = std::search(text.begin(), text.end(), part.begin(), part.end());
        r->mInteger                     = found == text.end() && !part.empty() ? -1 : (S32)(found - text.begin());
    }
    void toUpper(Data* r, Data* a, const LLUUID&)
    {
        std::string text = textOf(a[0]);
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        setString(r, text);
    }
    void toLower(Data* r, Data* a, const LLUUID&)
    {
        std::string text = textOf(a[0]);
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        setString(r, text);
    }
    // STRING_TRIM_HEAD 1, STRING_TRIM_TAIL 2, STRING_TRIM both.
    void stringTrim(Data* r, Data* a, const LLUUID&)
    {
        std::string text = textOf(a[0]);
        if (a[1].mInteger & 2)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            {
                text.pop_back();
            }
        }
        if (a[1].mInteger & 1)
        {
            size_t lead = 0;
            while (lead < text.size() && std::isspace(static_cast<unsigned char>(text[lead])))
            {
                ++lead;
            }
            text.erase(0, lead);
        }
        setString(r, text);
    }
    void char_(Data* r, Data* a, const LLUUID&)
    {
        const S32 code = a[0].mInteger;
        setString(r, code > 0 ? textFrom({ (llwchar)code }) : std::string());
    }
    void ord(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<llwchar> text  = charactersOf(textOf(a[0]));
        S32                        index = a[1].mInteger;
        if (index < 0)
        {
            index += (S32)text.size();
        }
        r->mInteger = index >= 0 && index < (S32)text.size() ? (S32)text[index] : 0;
    }

    // --- lists ----------------------------------------------------------------

    void getListLength(Data* r, Data* a, const LLUUID&) { r->mInteger = (S32)elementsOf(a[0]).size(); }
    void list2Integer(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        r->mInteger   = d ? asInteger(*d) : 0;
    }
    void list2Float(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        r->mFP        = d ? asFloat(*d) : 0.f;
    }
    void list2String(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        setString(r, d ? elementText(*d) : std::string());
    }
    // A string or key only; anything else is the blank key SLua's LSL VM gives.
    void list2Key(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        setKey(r, d && (d->mType == LST_STRING || d->mType == LST_KEY) ? textOf(*d) : std::string());
    }
    // Not cast, as SLua's are not: anything but a vector is the zero vector.
    void list2Vector(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        if (d && d->mType == LST_VECTOR)
        {
            r->mVec = d->mVec;
        }
    }
    void list2Rot(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        r->mQuat      = d && d->mType == LST_QUATERNION ? d->mQuat : LLQuaternion();
    }
    void getListEntryType(Data* r, Data* a, const LLUUID&)
    {
        const Data* d = at(elementsOf(a[0]), a[1].mInteger);
        r->mInteger   = d ? (S32)d->mType : 0;
    }
    void list2List(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<const Data*> items = elementsOf(a[0]);
        std::vector<const Data*>       out;
        for (S32 i : selected((S32)items.size(), a[1].mInteger, a[2].mInteger))
        {
            out.push_back(items[i]);
        }
        setList(r, out);
    }
    void deleteSubList(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<const Data*> items = elementsOf(a[0]);
        std::vector<const Data*>       out;
        for (S32 i : unselected((S32)items.size(), a[1].mInteger, a[2].mInteger))
        {
            out.push_back(items[i]);
        }
        setList(r, out);
    }
    void listInsertList(Data* r, Data* a, const LLUUID&)
    {
        std::vector<const Data*>       items = elementsOf(a[0]);
        const std::vector<const Data*> more  = elementsOf(a[1]);
        S32                            pos   = a[2].mInteger;
        if (pos < 0)
        {
            pos += (S32)items.size();
        }
        pos = std::clamp(pos, 0, (S32)items.size());
        items.insert(items.begin() + pos, more.begin(), more.end());
        setList(r, items);
    }
    // As SLua's: the range deleted, then the replacement put in at the start
    // index, read against the list before the deleting and held to the end of
    // the one after; a start before the beginning puts in nothing.
    void listReplaceList(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<const Data*> items = elementsOf(a[0]);
        const std::vector<const Data*> with  = elementsOf(a[1]);
        std::vector<const Data*>       kept;
        for (S32 i : unselected((S32)items.size(), a[2].mInteger, a[3].mInteger))
        {
            kept.push_back(items[i]);
        }
        S32 target = a[2].mInteger;
        if (target < 0)
        {
            target += (S32)items.size();
        }
        if (!with.empty() && target >= 0)
        {
            target = std::min(target, (S32)kept.size());
            kept.insert(kept.begin() + target, with.begin(), with.end());
        }
        setList(r, kept);
    }
    void listFindList(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<const Data*> items = elementsOf(a[0]);
        const std::vector<const Data*> part  = elementsOf(a[1]);
        r->mInteger                          = -1;
        if (part.empty() || part.size() > items.size())
        {
            return;
        }
        for (size_t i = 0; i + part.size() <= items.size(); ++i)
        {
            bool same = true;
            for (size_t j = 0; j < part.size() && same; ++j)
            {
                same = items[i + j]->mType == part[j]->mType && elementText(*items[i + j]) == elementText(*part[j]);
            }
            if (same)
            {
                r->mInteger = (S32)i;
                return;
            }
        }
    }
    void list2ListStrided(Data* r, Data* a, const LLUUID&)
    {
        const std::vector<const Data*> items  = elementsOf(a[0]);
        const S32                      stride = std::max(a[3].mInteger, 1);
        std::vector<const Data*>       out;
        for (S32 i : selected((S32)items.size(), a[1].mInteger, a[2].mInteger))
        {
            if (i % stride == 0)
            {
                out.push_back(items[i]);
            }
        }
        setList(r, out);
    }
    void dumpList2String(Data* r, Data* a, const LLUUID&)
    {
        const std::string between = textOf(a[1]);
        std::string       out;
        bool              first = true;
        for (const Data* d : elementsOf(a[0]))
        {
            out += (first ? "" : between) + elementText(*d, false);
            first = false;
        }
        setString(r, out);
    }
    void list2CSV(Data* r, Data* a, const LLUUID&)
    {
        std::string out;
        bool        first = true;
        for (const Data* d : elementsOf(a[0]))
        {
            out += (first ? "" : ", ") + elementText(*d);
            first = false;
        }
        setString(r, out);
    }
    void csv2List(Data* r, Data* a, const LLUUID&)
    {
        const std::string text = textOf(a[0]);
        std::vector<Data> made;
        size_t            from = 0;
        while (true)
        {
            const size_t comma = text.find(',', from);
            std::string  part  = text.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            while (!part.empty() && part.front() == ' ')
            {
                part.erase(0, 1);
            }
            appendString(made, part);
            if (comma == std::string::npos)
            {
                break;
            }
            from = comma + 1;
        }
        setListOf(r, made);
    }
    // Split at each separator; spacers are kept as elements of their own.
    // Empty pieces are dropped, as llParseString2List does.
    void parseString2List(Data* r, Data* a, const LLUUID&)
    {
        const std::string        text = textOf(a[0]);
        std::vector<std::string> separators;
        std::vector<std::string> spacers;
        for (const Data* d : elementsOf(a[1]))
        {
            separators.push_back(elementText(*d));
        }
        for (const Data* d : elementsOf(a[2]))
        {
            spacers.push_back(elementText(*d));
        }
        std::vector<Data> made;
        std::string       piece;
        size_t            i = 0;
        while (i < text.size())
        {
            bool matched = false;
            for (int kind = 0; kind < 2 && !matched; ++kind)
            {
                for (const std::string& mark : kind == 0 ? separators : spacers)
                {
                    if (!mark.empty() && text.compare(i, mark.size(), mark) == 0)
                    {
                        if (!piece.empty())
                        {
                            appendString(made, piece);
                        }
                        piece.clear();
                        if (kind == 1)
                        {
                            appendString(made, mark);
                        }
                        i += mark.size();
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched)
            {
                piece += text[i++];
            }
        }
        if (!piece.empty())
        {
            appendString(made, piece);
        }
        setListOf(r, made);
    }
    // Through the allocator's own sort and shuffle, which take the list's
    // elements from the copy they are given.
    void listSort(Data* r, Data* a, const LLUUID&)
    {
        Data list;
        setList(&list, elementsOf(a[0]));
        r->mListp = lsa_bubble_sort(&list, a[1].mInteger, a[2].mInteger);
    }
    void listRandomize(Data* r, Data* a, const LLUUID&)
    {
        Data list;
        setList(&list, elementsOf(a[0]));
        r->mListp = lsa_randomize(&list, a[1].mInteger);
    }

    // --- the rest, as the grid would answer a script in no world -------------

    void unixTime(Data* r, Data*, const LLUUID&) { r->mInteger = 1234567890; }
    void getKey(Data* r, Data*, const LLUUID&) { setKey(r, "00000000-0000-0000-0000-0000000000ff"); }
    void getOwner(Data* r, Data*, const LLUUID&) { setKey(r, "00000000-0000-0000-0000-0000000000ee"); }
    void objectName(Data* r, Data*, const LLUUID&) { setString(r, "Object"); }

    // Nothing of each type a function can return, for every function no
    // test needs more of: a string or a key is never left unset, which the
    // VM's heap would read.
    void noneVoid(Data*, Data*, const LLUUID&) {}
    void noneInteger(Data* r, Data*, const LLUUID&) { r->mInteger = 0; }
    void noneFloat(Data* r, Data*, const LLUUID&) { r->mFP = 0.f; }
    void noneString(Data* r, Data*, const LLUUID&) { setString(r, std::string()); }
    void noneKey(Data* r, Data*, const LLUUID&) { setKey(r, NULL_KEY); }
    void noneVector(Data* r, Data*, const LLUUID&) { r->mVec.setVec(0.f, 0.f, 0.f); }
    void noneRotation(Data* r, Data*, const LLUUID&) { r->mQuat = LLQuaternion(); }
    void noneList(Data*, Data*, const LLUUID&) {}

    exec_t noneFor(const char* return_type)
    {
        switch (return_type ? return_type[0] : '\0')
        {
        case 'i': return noneInteger;
        case 'f': return noneFloat;
        case 's': return noneString;
        case 'k': return noneKey;
        case 'v': return noneVector;
        case 'q': return noneRotation;
        case 'l': return noneList;
        default: return noneVoid;
        }
    }

    const std::map<std::string, exec_t>& implemented()
    {
        static const std::map<std::string, exec_t> functions{
            { "llSay", say },
            { "llShout", say },
            { "llWhisper", say },
            { "llRegionSay", say },
            { "llOwnerSay", ownerSay },
            { "llRegionSayTo", regionSayTo },
            { "llAbs", abs_ },
            { "llFabs", fabs_ },
            { "llCeil", ceil_ },
            { "llFloor", floor_ },
            { "llRound", round_ },
            { "llSqrt", sqrt_ },
            { "llPow", pow_ },
            { "llSin", sin_ },
            { "llCos", cos_ },
            { "llTan", tan_ },
            { "llAsin", asin_ },
            { "llAcos", acos_ },
            { "llAtan2", atan2_ },
            { "llLog", log_ },
            { "llLog10", log10_ },
            { "llFrand", frand },
            { "llModPow", modPow },
            { "llVecMag", vecMag },
            { "llVecNorm", vecNorm },
            { "llVecDist", vecDist },
            { "llRot2Euler", rot2Euler },
            { "llEuler2Rot", euler2Rot },
            { "llRot2Fwd", rot2Fwd },
            { "llRot2Left", rot2Left },
            { "llRot2Up", rot2Up },
            { "llAxisAngle2Rot", axisAngle2Rot },
            { "llRot2Angle", rot2Angle },
            { "llRot2Axis", rot2Axis },
            { "llRotBetween", rotBetween },
            { "llAngleBetween", angleBetween },
            { "llStringLength", stringLength },
            { "llGetSubString", getSubString },
            { "llDeleteSubString", deleteSubString },
            { "llInsertString", insertString },
            { "llSubStringIndex", subStringIndex },
            { "llToUpper", toUpper },
            { "llToLower", toLower },
            { "llStringTrim", stringTrim },
            { "llChar", char_ },
            { "llOrd", ord },
            { "llGetListLength", getListLength },
            { "llList2Integer", list2Integer },
            { "llList2Float", list2Float },
            { "llList2String", list2String },
            { "llList2Key", list2Key },
            { "llList2Vector", list2Vector },
            { "llList2Rot", list2Rot },
            { "llGetListEntryType", getListEntryType },
            { "llList2List", list2List },
            { "llDeleteSubList", deleteSubList },
            { "llListInsertList", listInsertList },
            { "llListReplaceList", listReplaceList },
            { "llListFindList", listFindList },
            { "llList2ListStrided", list2ListStrided },
            { "llDumpList2String", dumpList2String },
            { "llList2CSV", list2CSV },
            { "llCSV2List", csv2List },
            { "llParseString2List", parseString2List },
            { "llListSort", listSort },
            { "llListRandomize", listRandomize },
            { "llGetUnixTime", unixTime },
            { "llGetKey", getKey },
            { "llGetOwner", getOwner },
            { "llGetObjectName", objectName },
        };
        return functions;
    }

    int sInstalled = 0;
}

namespace ll_test_lso
{
    TestLibrary::TestLibrary()
    {
        if (sInstalled++ > 0)
        {
            return;
        }
        for (LLScriptLibraryFunction& function : gScriptLibrary.mFunctions)
        {
            mWere.push_back(function.mExecFunc);
            if (!function.mName)
            {
                continue;
            }
            const auto found  = implemented().find(function.mName);
            function.mExecFunc = found != implemented().end() ? found->second : noneFor(function.mReturnType);
        }
    }

    TestLibrary::~TestLibrary()
    {
        if (--sInstalled > 0)
        {
            return;
        }
        for (size_t i = 0; i < mWere.size() && i < gScriptLibrary.mFunctions.size(); ++i)
        {
            gScriptLibrary.mFunctions[i].mExecFunc = mWere[i];
        }
    }

    LLScriptLibraryFunction* libraryFunction(const char* name)
    {
        for (LLScriptLibraryFunction& function : gScriptLibrary.mFunctions)
        {
            if (function.mName && !std::strcmp(function.mName, name))
            {
                return &function;
            }
        }
        return nullptr;
    }
}

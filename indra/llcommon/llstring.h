/**
 * @file llstring.h
 * @brief String utility functions and std::string class.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLSTRING_H
#define LL_LLSTRING_H

#include <boost/call_traits.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <cstdio>
#include <cwchar>                   // std::wcslen()
//#include <locale>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <map>
#include "llformat.h"
// [RLVa:KB] - Checked: RLVa-2.1.0
#include <list>
// [/RLVa:KB]

#if LL_LINUX
#include <wctype.h>
#include <wchar.h>
#endif

#include <string.h>

const char LL_UNKNOWN_CHAR = '?';
class LLSD;

class LL_COMMON_API LLStringOps
{
private:
    static long sPacificTimeOffset;
    static long sLocalTimeOffset;
    static bool sPacificDaylightTime;

    static std::map<std::string, std::string, std::less<>> datetimeToCodes;

    // Above-ASCII halves of the classification predicates below.
    static llwchar toUpperAboveAscii(llwchar elem);
    static llwchar toLowerAboveAscii(llwchar elem);
    static bool isSpaceAboveAscii(llwchar elem);
    static bool isUpperAboveAscii(llwchar elem);
    static bool isLowerAboveAscii(llwchar elem);
    static bool isPunctAboveAscii(llwchar elem);
    static bool isAlphaAboveAscii(llwchar elem);
    static bool isAlnumAboveAscii(llwchar elem);

public:
    static std::vector<std::string> sWeekDayList;
    static std::vector<std::string> sWeekDayShortList;
    static std::vector<std::string> sMonthList;
    static std::vector<std::string> sMonthShortList;
    static std::string sDayFormat;

    static std::string sAM;
    static std::string sPM;

    // Character classification. The llwchar forms answer for the whole of
    // Unicode and give the same answer on every platform; ASCII is settled
    // here, and anything above it goes out of line to llstring.cpp, where the
    // Unicode tables are reachable.
    //
    // <cwctype> is not usable for them: its wint_t is 16 bits on Windows, so a
    // codepoint above U+FFFF arrives with its top half cut off. U+2000A, a CJK
    // ideograph, becomes U+000A and classifies as a line feed.
    //
    // The char forms classify one byte, so they see UTF-8 as its bytes. Case a
    // whole std::string with LLStringUtilBase<char>::toUpper/toLower instead.

    static char toUpper(char elem) { return (char)toupper((unsigned char)elem); }
    static llwchar toUpper(llwchar elem)
    {
        return elem < 0x80 ? (llwchar)toupper((int)elem) : toUpperAboveAscii(elem);
    }

    static char toLower(char elem) { return (char)tolower((unsigned char)elem); }
    static llwchar toLower(llwchar elem)
    {
        return elem < 0x80 ? (llwchar)tolower((int)elem) : toLowerAboveAscii(elem);
    }

    static bool isSpace(char elem) { return isspace((unsigned char)elem) != 0; }
    static bool isSpace(llwchar elem)
    {
        return elem < 0x80 ? isspace((int)elem) != 0 : isSpaceAboveAscii(elem);
    }

    static bool isUpper(char elem) { return isupper((unsigned char)elem) != 0; }
    static bool isUpper(llwchar elem)
    {
        return elem < 0x80 ? isupper((int)elem) != 0 : isUpperAboveAscii(elem);
    }

    static bool isLower(char elem) { return islower((unsigned char)elem) != 0; }
    static bool isLower(llwchar elem)
    {
        return elem < 0x80 ? islower((int)elem) != 0 : isLowerAboveAscii(elem);
    }

    // Decimal digits in the ASCII sense, on purpose. Every caller goes on to
    // read the run as a number -- chat channel numbers, the numeric text
    // validators, the natural-order digit runs in compareDict -- and that only
    // works for '0' through '9'.
    static bool isDigit(char a) { return a >= '0' && a <= '9'; }
    static bool isDigit(llwchar a) { return a >= U'0' && a <= U'9'; }

    // Has no printed form and no business in a name or a line of text: the C0
    // and C1 control ranges, and the line and paragraph separators. Format
    // characters are deliberately absent -- ZWJ and the bidi marks are
    // load-bearing in the scripts that use them.
    static bool isNonprintable(llwchar a);

    static bool isPunct(char a) { return ispunct((unsigned char)a) != 0; }
    static bool isPunct(llwchar a)
    {
        return a < 0x80 ? ispunct((int)a) != 0 : isPunctAboveAscii(a);
    }

    static bool isAlpha(char a) { return isalpha((unsigned char)a) != 0; }
    static bool isAlpha(llwchar a)
    {
        return a < 0x80 ? isalpha((int)a) != 0 : isAlphaAboveAscii(a);
    }

    static bool isAlnum(char a) { return isalnum((unsigned char)a) != 0; }
    static bool isAlnum(llwchar a)
    {
        return a < 0x80 ? isalnum((int)a) != 0 : isAlnumAboveAscii(a);
    }

    // Unicode's Emoji_Presentation: 'a' renders in colour unless something
    // asks it not to. Use this to tell "unambiguously a colour emoji and
    // should never render as text" from "this could be either" — ⌚ and ⚓
    // qualify, © and ❤ do not until a VS-16 says so.
    static bool isEmoji(llwchar a);

    // Unicode's Extended_Pictographic, plus the regional indicators it leaves
    // out. This is the set UAX #51 builds sequences from, so it is what
    // wstring_find_emoji_clusters and the shape-time emoji-keeper treat as a
    // base: broader than isEmoji, because ❤ (U+2764) and ‼ (U+203C) start an
    // emoji when a VS-16 or a ZWJ follows them. Extenders are not bases and
    // are absent from it. Call this when picking emoji-styled fallbacks; call
    // isEmoji when the question is how a codepoint renders on its own.
    static bool isPictographBase(llwchar a);

    // Codepoints that extend a pictograph base into a multi-codepoint
    // cluster as a trailing modifier: VS-15 / VS-16 presentation selectors,
    // the keycap combining mark, skin-tone modifiers, and tag chars
    // (including the U+E007F CANCEL TAG terminator for subdivision flags).
    // ZWJ (U+200D) is *not* included — it joins one cluster to another
    // base codepoint and walkers handle it separately. Shared by the
    // cluster walker (wstring_find_emoji_clusters) and the shape-itemizer
    // so the two never disagree on what belongs in a cluster.
    static bool isEmojiClusterExtender(llwchar a);

    // Sort order. Root-locale Unicode collation on both forms, so a list comes
    // out the same on every platform and an accented letter sorts beside the
    // letter it decorates rather than past the end of the alphabet.
    static S32  collate(const char* a, const char* b);

    static void setupDatetimeInfo(bool pacific_daylight_time);

    static void setupWeekDaysNames(const std::string& data);
    static void setupWeekDaysShortNames(const std::string& data);
    static void setupMonthNames(const std::string& data);
    static void setupMonthShortNames(const std::string& data);
    static void setupDayFormat(const std::string& data);


    static long getPacificTimeOffset(void) { return sPacificTimeOffset;}
    static long getLocalTimeOffset(void) { return sLocalTimeOffset;}
    // Is the Pacific time zone (aka server time zone)
    // currently in daylight savings time?
    static bool getPacificDaylightTime(void) { return sPacificDaylightTime;}

    static std::string getDatetimeCode (std::string_view key);

    // Express a value like 1234567 as "1.23M"
    static std::string getReadableNumber(F64 num);
};

/**
 * @brief Return a string constructed from in without crashing if the
 * pointer is NULL.
 */
LL_COMMON_API std::string ll_safe_string(const char* in);
LL_COMMON_API std::string ll_safe_string(const char* in, S32 maxlen);


// Allowing assignments from non-strings into format_map_t is apparently
// *really* error-prone, so subclass std::string with just basic c'tors.
class LLFormatMapString
{
public:
    LLFormatMapString() = default;
    LLFormatMapString(const char* s) : mString(ll_safe_string(s)) {};
    LLFormatMapString(const std::string& s) : mString(s) {};
    LLFormatMapString(std::string_view s) : mString(s) {};
    operator const std::string&() const { return mString; }
    const std::string& operator()() const { return mString; }
    bool operator<(const LLFormatMapString& rhs) const { return mString < rhs.mString; }
    std::size_t length() const { return mString.length(); }

private:
    std::string mString;
};

// Lets a format map be probed with a view. Every lookup used to build an
// LLFormatMapString first, so asking whether a token was present allocated a
// copy of it -- and the bracketed retry allocated a second.
struct LLFormatMapStringLess
{
    using is_transparent = void;
    bool operator()(const LLFormatMapString& a, const LLFormatMapString& b) const { return a() < b(); }
    bool operator()(const LLFormatMapString& a, std::string_view b) const { return a() < b; }
    bool operator()(std::string_view a, const LLFormatMapString& b) const { return a < b(); }
    // A string literal converts just as well to LLFormatMapString as it does
    // to a view, so without these the two overloads above are ambiguous for
    // every caller that looks a token up by literal. Matching const char*
    // exactly settles it, and keeps a null pointer behaving as the empty
    // string the way LLFormatMapString's own constructor does.
    bool operator()(const LLFormatMapString& a, const char* b) const { return a() < std::string_view(b ? b : ""); }
    bool operator()(const char* a, const LLFormatMapString& b) const { return std::string_view(a ? a : "") < b(); }
};

template <class T>
class LLStringUtilBase
{
private:
    static std::string sLocale;

public:
    typedef std::basic_string<T> string_type;
    typedef std::basic_string_view<T> string_view_type;
    typedef typename string_type::size_type size_type;

public:
    /////////////////////////////////////////////////////////////////////////////////////////
    // Static Utility functions that operate on std::strings

    static const string_type null;

    typedef std::map<LLFormatMapString, LLFormatMapString, LLFormatMapStringLess> format_map_t;
    /// considers any sequence of delims as a single field separator
    LL_COMMON_API static void getTokens(const string_type& instr,
                                        std::vector<string_type >& tokens,
                                        const string_type& delims);
    /// like simple scan overload, but returns scanned vector
    static std::vector<string_type> getTokens(const string_type& instr,
                                              const string_type& delims);
    /// add support for keep_delims and quotes (either could be empty string)
    static void getTokens(const string_type& instr,
                          std::vector<string_type>& tokens,
                          const string_type& drop_delims,
                          const string_type& keep_delims,
                          const string_type& quotes=string_type());
    /// like keep_delims-and-quotes overload, but returns scanned vector
    static std::vector<string_type> getTokens(const string_type& instr,
                                              const string_type& drop_delims,
                                              const string_type& keep_delims,
                                              const string_type& quotes=string_type());
    /// add support for escapes (could be empty string)
    static void getTokens(const string_type& instr,
                          std::vector<string_type>& tokens,
                          const string_type& drop_delims,
                          const string_type& keep_delims,
                          const string_type& quotes,
                          const string_type& escapes);
    /// like escapes overload, but returns scanned vector
    static std::vector<string_type> getTokens(const string_type& instr,
                                              const string_type& drop_delims,
                                              const string_type& keep_delims,
                                              const string_type& quotes,
                                              const string_type& escapes);

    LL_COMMON_API static void formatNumber(string_type& numStr, string_view_type decimals);
    LL_COMMON_API static bool formatDatetime(string_type& replacement, string_view_type token, string_view_type param, S32 secFromEpoch);
    LL_COMMON_API static S32 format(string_type& s, const format_map_t& substitutions);
    LL_COMMON_API static S32 format(string_type& s, const LLSD& substitutions);
    LL_COMMON_API static bool simpleReplacement(string_type& replacement, string_view_type token, const format_map_t& substitutions);
    LL_COMMON_API static bool simpleReplacement(string_type& replacement, string_view_type token, const LLSD& substitutions);
    LL_COMMON_API static void setLocale (std::string inLocale);
    LL_COMMON_API static std::string getLocale (void);

    static bool isValidIndex(const string_type& string, size_type i)
    {
        return !string.empty() && (0 <= i) && (i <= string.size());
    }

    static bool contains(const string_type& string, T c, size_type i=0)
    {
        return string.find(c, i) != string_type::npos;
    }

    static void trimHead(string_type& string);
    static void trimTail(string_type& string);
    static void trimTail(string_type& string, const string_type& tokens);
    static void trim(string_type& string)   { trimHead(string); trimTail(string); }

    static void trimHead(string_view_type& string);
    static void trimTail(string_view_type& string);
    static void trim(string_view_type& string)   { trimHead(string); trimTail(string); }

    // Cuts at `count` units of string_type, which for the char instantiation
    // means bytes: it will split a multi-byte character in half. Use
    // utf8str_truncate() for UTF-8 held in a std::string -- it takes the same
    // byte bound and backs off to a character boundary. This one is right when
    // the count already came from a find() or from a walk over the text.
    static void truncate(string_type& string, size_type count);

    static void toUpper(string_type& string);
    static void toLower(string_type& string);

    // True if this is the head of s.
    static bool isHead( const string_type& string, const T* s );

    /**
     * @brief Returns true if string starts with substr
     *
     * If etither string or substr are empty, this method returns false.
     */
    static bool startsWith(
        string_view_type string,
        string_view_type substr);

    /**
     * @brief Returns true if string ends in substr
     *
     * If etither string or substr are empty, this method returns false.
     */
    static bool endsWith(
        string_view_type string,
        string_view_type substr);

    /**
     * get environment string value with proper Unicode handling
     * (key is always UTF-8)
     * detect absence by return value == dflt
     */
    static string_type getenv(const std::string& key, const string_type& dflt="");
    /**
     * get optional environment string value with proper Unicode handling
     * (key is always UTF-8)
     * detect absence by (! return value)
     */
    static std::optional<string_type> getoptenv(const std::string& key);

    static void addCRLF(string_type& string);
    static void removeCRLF(string_type& string);
    static void removeWindowsCR(string_type& string);

    static void replaceTabsWithSpaces( string_type& string, size_type spaces_per_tab );
    static void replaceNonstandardASCII( string_type& string, T replacement );
    static void replaceChar( string_type& string, T target, T replacement );
    static void replaceString( string_type& string, string_view_type target, string_view_type replacement );
    static string_type capitalize(const string_type& str);
    static void capitalize(string_type& str);

    static bool containsNonprintable(string_view_type string);
    static void stripNonprintable(string_type& string);

    /**
     * Double-quote an argument string if needed, unless it's already
     * double-quoted. Decide whether it's needed based on the presence of any
     * character in @a triggers (default space or double-quote). If we quote
     * it, escape any embedded double-quote with the @a escape string (default
     * backslash).
     *
     * Passing triggers="" means always quote, unless it's already double-quoted.
     */
    static string_type quote(const string_type& str,
                             const string_type& triggers=" \"",
                             const string_type& escape="\\");

    /**
     * @brief Unsafe way to make ascii characters. You should probably
     * only call this when interacting with the host operating system.
     * The 1 byte std::string does not work correctly.
     */
    static void _makeASCII(string_type& string);

    // Conversion to other data types
    static bool convertToBOOL(string_view_type string, bool& value);
    static bool convertToU8(string_view_type string, U8& value);
    static bool convertToS8(string_view_type string, S8& value);
    static bool convertToS16(string_view_type string, S16& value);
    static bool convertToU16(string_view_type string, U16& value);
    static bool convertToU32(string_view_type string, U32& value);
    static bool convertToS32(string_view_type string, S32& value);
    static bool convertToF32(string_view_type string, F32& value);
    static bool convertToF64(string_view_type string, F64& value);

    /////////////////////////////////////////////////////////////////////////////////////////
    // Utility functions for working with char*'s and strings

    // Like strcmp but also handles empty strings. Uses
    // current locale.
    static S32      compareStrings(const T* lhs, const T* rhs);
    static S32      compareStrings(const string_type& lhs, const string_type& rhs);

    // case insensitive version of above. Uses current locale on
    // Win32, and falls back to a non-locale aware comparison on
    // Linux.
    static S32      compareInsensitive(const T* lhs, const T* rhs);
    static S32      compareInsensitive(string_view_type lhs, string_view_type rhs);

    // Same bytes, give or take the case of an ASCII letter. This is the
    // question to ask about a token whose spelling a protocol or a file format
    // fixes -- a wire header, a keyword, a stream tag, a URL scheme -- where
    // matching has to mean the same characters and nothing else.
    // compareInsensitive() asks a collator, which is right for text a person
    // reads and wrong here: it folds away distinctions a reader is meant to
    // overlook, so a fullwidth or soft-hyphenated spelling the format never
    // allowed would be accepted as the real one.
    static bool     isEqualInsensitiveASCII(string_view_type lhs, string_view_type rhs);

    // Sort order for lists people read: Unicode collation with digit runs
    // compared as numbers, so item2 comes before item10 and an accented letter
    // sorts beside the letter it decorates. Case is a tertiary difference, so
    // "abc" and "ABC" differ but sort next to each other, lowercase first.
    // Root locale, so a list looks the same to everyone.
    static S32      compareDict(string_view_type a, string_view_type b);

    // The same, with case ignored entirely. Accents still separate.
    static S32      compareDictInsensitive(string_view_type a, string_view_type b);

    // Puts compareDict() in a form appropriate for LL container classes to use for sorting.
    static bool     precedesDict( string_view_type a, string_view_type b );

    // A replacement for strncpy.
    // If the dst buffer is dst_size bytes long or more, ensures that dst is null terminated and holds
    // up to dst_size-1 characters of src.
    static void     copy(T* dst, const T* src, size_type dst_size);

    // Copies src into dst at a given offset.
    static void     copyInto(string_type& dst, string_view_type src, size_type offset);


#ifdef _DEBUG
    LL_COMMON_API static void       testHarness();
#endif

private:
    LL_COMMON_API static size_type getSubstitution(const string_type& instr, size_type& start, std::vector<string_type >& tokens);
};

template<class T> const std::basic_string<T> LLStringUtilBase<T>::null;
template<class T> std::string LLStringUtilBase<T>::sLocale;

typedef LLStringUtilBase<char> LLStringUtil;

//@ Use this where we want to disallow input in the form of "foo"
//  This is used to catch places where english text is embedded in the code
//  instead of in a translatable XUI file.
class LLStringExplicit : public std::string
{
public:
    explicit LLStringExplicit(const char* s) : std::string(s) {}
    LLStringExplicit(const std::string& s) : std::string(s) {}
    LLStringExplicit(const std::string& s, size_type pos, size_type n = std::string::npos) : std::string(s, pos, n) {}
};

struct LLDictionaryLess
{
public:
    bool operator()(const std::string& a, const std::string& b) const
    {
        return (LLStringUtil::precedesDict(a, b));
    }
};


/**
 * Simple support functions
 */

/**
 * @brief chop off the trailing characters in a string.
 *
 * This function works on bytes rather than glyphs, so this will
 * incorrectly truncate non-single byte strings.
 * Use utf8str_truncate() for utf8 strings
 * @return a copy of in string minus the trailing count bytes.
 */
inline std::string chop_tail_copy(
    const std::string& in,
    std::string::size_type count)
{
    return std::string(in, 0, in.length() - count);
}

/**
 * @brief This translates a nybble stored as a hex value from 0-f back
 * to a nybble in the low order bits of the return byte.
 */
LL_COMMON_API bool is_char_hex(char hex);
LL_COMMON_API U8 hex_as_nybble(char hex);

/**
 * @brief read the contents of a file into a string.
 *
 * Since this function has no concept of character encoding, most
 * anything you do with this method ill-advised. Please avoid.
 * @param str [out] The string which will have.
 * @param filename The full name of the file to read.
 * @return Returns true on success. If false, str is unmodified.
 */
LL_COMMON_API bool _read_file_into_string(std::string& str, const std::string& filename);

/**
 * Unicode support
 */

/// generic conversion aliases
template<typename TO, typename FROM, typename Enable=void>
struct ll_convert_impl
{
    // Don't even provide a generic implementation. We specialize for every
    // combination we do support.
    TO operator()(const FROM& in) const;
};

// Use a function template to get the nice ll_convert<TO>(from_value) API.
template<typename TO, typename FROM>
TO ll_convert(const FROM& in)
{
    return ll_convert_impl<TO, FROM>()(in);
}

// degenerate case
template<typename T>
struct ll_convert_impl<T, T>
{
    T operator()(const T& in) const { return in; }
};

// simple construction from char*
template<typename T>
struct ll_convert_impl<T, const typename T::value_type*>
{
    T operator()(const typename T::value_type* in) const { return { in }; }
};

// specialize ll_convert_impl<TO, FROM> to return EXPR
#define ll_convert_alias(TO, FROM, EXPR)                    \
template<>                                                  \
struct ll_convert_impl<TO, FROM>                            \
{                                                           \
    /* param_type optimally passes both char* and string */ \
    TO operator()(typename boost::call_traits<FROM>::param_type in) const { return EXPR; } \
}

// If all we're doing is copying characters, pass this to ll_convert_alias as
// EXPR. Since it expands into the 'return EXPR' slot in the ll_convert_impl
// specialization above, it implies TO{ in.begin(), in.end() }.
#define LL_CONVERT_COPY_CHARS { in.begin(), in.end() }

// Generic name for strlen() / wcslen() - the default implementation should
// (!) work with U16 and llwchar, but we don't intend to engage it.
template <typename CHARTYPE>
size_t ll_convert_length(const CHARTYPE* zstr)
{
    const CHARTYPE* zp;
    // classic C string scan
    for (zp = zstr; *zp; ++zp)
        ;
    return (zp - zstr);
}

// specialize where we have a library function; may use intrinsic operations
template <>
inline size_t ll_convert_length<wchar_t>(const wchar_t* zstr) { return std::wcslen(zstr); }
template <>
inline size_t ll_convert_length<char>   (const char*    zstr) { return std::strlen(zstr); }

// ll_convert_forms() is short for a bunch of boilerplate. It defines
// longname(const char*, len), longname(const char*), longname(const string&)
// and longname(const string&, len) so calls written pre-ll_convert() will
// work. Most of these overloads will be unified once we turn on C++17 and can
// use std::string_view.
// It also uses aliasmacro to ensure that both ll_convert<OUTSTR>(const char*)
// and ll_convert<OUTSTR>(const string&) will work.
#define ll_convert_forms(aliasmacro, OUTSTR, INSTR, longname)           \
LL_COMMON_API OUTSTR longname(const INSTR::value_type* in, size_t len); \
inline auto longname(const INSTR& in, size_t len)                       \
{                                                                       \
    return longname(in.c_str(), len);                                   \
}                                                                       \
inline auto longname(const INSTR::value_type* in)                       \
{                                                                       \
    return longname(in, ll_convert_length(in));                         \
}                                                                       \
inline auto longname(const INSTR& in)                                   \
{                                                                       \
    return longname(in.c_str(), in.length());                           \
}                                                                       \
/* string param */                                                      \
aliasmacro(OUTSTR, INSTR, longname(in));                                \
/* char* param */                                                       \
aliasmacro(OUTSTR, const INSTR::value_type*, longname(in))

// Make the incoming string a utf8 string. Replaces any unknown glyph
// with the UNKNOWN_CHARACTER. Once any unknown glyph is found, the rest
// of the data may not be recovered.
LL_COMMON_API std::string rawstr_to_utf8(std::string_view raw);

//
// We should never use UTF16 except when communicating with Win32!
// https://docs.microsoft.com/en-us/cpp/cpp/char-wchar-t-char16-t-char32-t
// nat 2018-12-14: I consider the whole llutf16string thing a mistake, because
// the Windows APIs we want to call are all defined in terms of wchar_t*
// (or worse, LPCTSTR).
// https://docs.microsoft.com/en-us/windows/desktop/winprog/windows-data-types
typedef std::u16string llutf16string;

LL_COMMON_API std::ptrdiff_t wchar_to_utf8chars(llwchar inchar, char* outchars);

ll_convert_forms(ll_convert_alias, std::string, llutf16string, utf16str_to_utf8str);

// Convert to/from u8string
ll_convert_forms(ll_convert_alias, std::string, std::u8string, u8str_to_str);
ll_convert_forms(ll_convert_alias, std::u8string, std::string, str_to_u8str);

// Length in bytes of this wide char in a UTF8 string
LL_COMMON_API S32 wchar_utf8_length(const llwchar wc);

LL_COMMON_API std::string wchar_utf8_preview(const llwchar wc);

LL_COMMON_API std::string utf8str_tolower(std::string_view utf8str);

// Offsets and lengths that describe the text count BYTES; only the UTF-16
// length counts code units. The Win32 IME speaks UTF-16 and the editors speak
// UTF-8, so every offset crossing that boundary goes through one of these.
LL_COMMON_API S32 utf8str_utf16_length(std::string_view utf8str, S32 byte_offset, S32 byte_len);

// Bytes of the longest substring starting at byte_offset whose UTF-16 form does
// not exceed utf16_length code units. *unaligned reports that the limit fell
// inside a character -- a surrogate half, which an IME can ask for -- in which
// case the count stops short of it.
LL_COMMON_API S32 utf8str_length_from_utf16_length(std::string_view utf8str, S32 byte_offset,
                                                   S32 utf16_length, bool *unaligned = nullptr);

/**
 * @brief Properly truncate a utf8 string to a maximum byte count.
 *
 * The returned string may be less than max_len if the truncation
 * happens in the middle of a glyph. If max_len is longer than the
 * string passed in, the return value == utf8str.
 * @param utf8str A valid utf8 string to truncate.
 * @param max_len The maximum number of bytes in the return value.
 * @return Returns a valid utf8 string with byte count <= max_len.
 */
LL_COMMON_API std::string utf8str_truncate(std::string_view utf8str, const S32 max_len);

// [RLVa:KB] - Checked: RLVa-2.1.0
LL_COMMON_API std::string utf8str_substr(std::string_view utf8str, const S32 index, const S32 max_len);
LL_COMMON_API void utf8str_split(std::list<std::string>& split_list, std::string_view utf8str, size_t maxlen, char split_token);
// [/RLVa:KB]

LL_COMMON_API std::string utf8str_trim(std::string_view utf8str);

LL_COMMON_API S32 utf8str_compare_insensitive(
    const std::string& lhs,
    const std::string& rhs);

/**
* @brief Properly truncate a utf8 string to a maximum character count.
*
* If symbol_len is longer than the string passed in, the return
* value == utf8str.
* @param utf8str A valid utf8 string to truncate.
* @param symbol_len The maximum number of symbols in the return value.
* @return Returns a valid utf8 string with symbol count <= max_len.
*/
LL_COMMON_API std::string utf8str_symbol_truncate(std::string_view utf8str, const S32 symbol_len);

/**
 * @brief Replace all occurences of target_char with replace_char
 *
 * @param utf8str A utf8 string to process.
 * @param target_char The wchar to be replaced
 * @param replace_char The wchar which is written on replace
 */
LL_COMMON_API std::string utf8str_substChar(
    std::string_view utf8str,
    const llwchar target_char,
    const llwchar replace_char);

LL_COMMON_API std::string utf8str_makeASCII(std::string_view utf8str);

// Hack - used for evil notecards.
LL_COMMON_API std::string mbcsstring_makeASCII(std::string_view str);

LL_COMMON_API std::string utf8str_removeCRLF(std::string_view utf8str);

LL_COMMON_API std::string utf8str_showBytesUTF8(std::string_view utf8str);


LL_COMMON_API bool utf8str_remove_emojis(std::string& utf8str);

// Half-open byte ranges of multi-codepoint emoji clusters, in ascending order.
using EmojiClusterList = std::vector<std::pair<size_t, size_t>>;

// Locate contiguous byte ranges [begin, end) of utf8str that form a
// multi-code-point emoji cluster — ZWJ families, VS15/VS16 presentation
// selectors, skin-tone modifiers, regional indicator flag pairs, keycap
// sequences (digit/#/* + FE0F + 20E3), and tag sequences (e.g., subdivision
// flags). Isolated emoji that render correctly through the 1:1
// FT_Get_Char_Index path are intentionally skipped.
//
// This answers "which spans are one emoji", which is a different question from
// "where are the grapheme boundaries" — the latter is UAX #29 and lives in
// utf8str_step_grapheme_* / utf8str_grapheme_align_* below. Do not substitute
// one for the other: ALFontShaping::shape_all_sub_runs drives face selection
// off these ranges, so feeding it grapheme clusters would route accented Latin
// and Hangul onto the emoji face.
LL_COMMON_API EmojiClusterList
utf8str_find_emoji_clusters(std::string_view utf8str);

// Cost note: the single-argument emoji helpers rebuild the cluster list
// internally on every call (an O(N) scan). Fine for one-off lookups; expensive
// in tight loops over the same text. Callers making several queries on the same
// string should call utf8str_find_emoji_clusters once and feed the result into
// the two-argument overload, amortising the scan across the batch.

// One decoded codepoint and the byte position just past it. Off the end of the
// text, `cp` is 0 and `next` is the position handed in — read that as "nothing
// here" rather than testing the length separately. Malformed input decodes to
// U+FFFD over a single byte, so `next` always advances and a walk driven off
// this terminates on any input at all.
struct LLCodepointAt
{
    llwchar cp   = 0;
    size_t  next = 0;
};
LL_COMMON_API LLCodepointAt utf8str_decode_at(std::string_view utf8str, size_t byte_pos);

// The inverse: one codepoint as the bytes that encode it. What a caller holding
// a single character — a keystroke, a map key — needs to reach an API that takes
// bytes. Anything unencodable becomes U+FFFD, so the result is always
// well-formed and always non-empty.
LL_COMMON_API void utf8str_append_cp(std::string& out, llwchar cp);
LL_COMMON_API std::string utf8str_from_cp(llwchar cp);

// How many characters the text holds -- what .size() answers once it is UTF-32
// and what a limit expressed in characters has to be compared against. Malformed
// bytes count one each, so this never disagrees with a walk over the same text.
LL_COMMON_API size_t utf8str_codepoint_count(std::string_view utf8str);

// The inverse: where the character at `index` begins, in bytes. An index past
// the end gives the end, as a clamped range wants, so the pair round-trips.
// One walk, no index map -- reach for ALUtf8View only when the same text is
// indexed repeatedly.
LL_COMMON_API size_t utf8str_offset_from_codepoint_index(std::string_view utf8str, size_t index);

// Whether the bytes are well-formed UTF-8: shortest forms, no surrogate halves,
// nothing past U+10FFFF. Worth asking before utf8str_sanitize where the answer
// is nearly always yes and the caller already holds the text -- sanitize has to
// return a string, and copying one per line of chat to learn nothing changed is
// a cost the check does not have.
LL_COMMON_API bool utf8str_is_valid(std::string_view utf8str);

// Valid UTF-8, with any malformed sequence replaced by the unknown character.
// Text arriving from outside the viewer -- the system clipboard is the one that
// reaches a document today -- carries no guarantee of validity, and now that the
// document is UTF-8 there is no conversion on the way in to check it. Returns
// the input unchanged when it is already valid, which is the case that matters.
LL_COMMON_API std::string utf8str_sanitize(std::string_view utf8str);

// Every position below is a byte offset into the caller's own string, and so
// is every answer. ICU reads UTF-8 where it lies, so none of these convert or
// build an index map.

// Cursor stepping over grapheme clusters: move to the nearest cluster boundary
// strictly after / before `byte_pos`, so the caret never splits a ZWJ family,
// flag pair, keycap, tag subdivision, Hangul syllable, Indic conjunct or a base
// and its combining marks. Clamped to [0, size].
//
// These implement UAX #29 in full via ICU, and are unrelated to the emoji
// cluster list above — that answers "is this an emoji", which UAX #29 does not.
//
// Cost is one pass over the line containing `byte_pos`.
LL_COMMON_API size_t utf8str_step_grapheme_forward(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API size_t utf8str_step_grapheme_backward(std::string_view utf8str, size_t byte_pos);

// Snap `byte_pos` onto a grapheme cluster boundary when it sits strictly inside
// a cluster. The backward variant snaps to the cluster's start, the forward
// variant to its end; a position already on a boundary is returned unchanged.
// Intended for places that compute a position through some other rule — word
// walks, pixel hit-testing — and need to nudge onto the nearest safe edge in a
// chosen direction without the step that utf8str_step_grapheme_* applies.
//
// Callers asking "what emoji does byte_pos belong to?" want
// utf8str_emoji_range_at, which is inclusive of the leading boundary and aware
// of single-codepoint pictographs.
LL_COMMON_API size_t utf8str_grapheme_align_backward(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API size_t utf8str_grapheme_align_forward(std::string_view utf8str, size_t byte_pos);

// Word stepping in the sense a text cursor means it: land on the start of a
// word rather than in the gap before it, so ctrl+arrow steps over whitespace
// runs instead of stopping in them. Neither direction crosses a newline; a
// position already at a line's edge is returned unchanged.
//
// UAX #29 word boundaries, which see apostrophes inside a word, numbers with
// separators, and scripts that do not space their words -- none of which an
// alphanumeric-or-underscore test can. Word boundaries always fall on grapheme
// boundaries, so no separate cluster snap is needed afterwards.
LL_COMMON_API size_t utf8str_step_word_forward(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API size_t utf8str_step_word_backward(std::string_view utf8str, size_t byte_pos);

// Word movement as a caret means it, which is the pair above plus one thing:
// those stay inside the line the offset sits on, so a caret already at a line
// edge has nowhere to go and does not move at all. Crossing the break when the
// walker stalls carries ctrl+left and ctrl+right into the neighbouring line
// without skipping a word at an ordinary caret position. Both text widgets
// want exactly this, so it lives here rather than once in each of them.
LL_COMMON_API size_t utf8str_caret_word_forward(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API size_t utf8str_caret_word_backward(std::string_view utf8str, size_t byte_pos);

// Which word is at `byte_pos`, and where the next one starts -- the other
// question callers ask of word segmentation, as against "where do I move to"
// above.
//
// Ranges are half-open and taken from UAX #29, so a contraction is one word and
// a script that does not space its words still segments. Whitespace and
// punctuation are segments too; a segment counts as a word only when it holds
// an alphanumeric, and utf8str_word_range_at returns an empty range at
// `byte_pos` when the position is not inside one. utf8str_next_word_range
// crosses lines and returns an empty range at the end of the text.
LL_COMMON_API std::pair<size_t, size_t> utf8str_word_range_at(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API std::pair<size_t, size_t> utf8str_next_word_range(std::string_view utf8str, size_t byte_pos);

// Positions in `utf8str` where UAX #14 permits a line to end, written to `out`
// in ascending order and expressed as where the next line would begin. The
// string's own end is always one of them; 0 never is. `out` is cleared first.
//
// The caller owns the buffer so a wrapping loop can keep one around rather than
// allocate per line -- this is measurement work that runs per frame.
//
// UAX #14 is what knows that a line may not begin with closing punctuation, that
// a non-breaking space is glue, and where CJK may be split; none of that is
// visible to a test for spaces plus a hand-written ideograph range.
LL_COMMON_API void utf8str_line_break_opportunities(std::string_view utf8str, std::vector<size_t>& out);

// Bytes of `utf8str` whose cased UTF-8 encoding fills exactly `cased_bytes`
// bytes. This maps an offset produced against a cased copy of some text back
// onto the text itself, without either the copy or a stored index map: the
// original is walked applying the same conversion, and the cased byte lengths
// are accumulated as it goes.
//
// Casing UTF-8 is not length-preserving -- sharp s uppercases to two bytes'
// worth more -- so any offset taken from a cased search key needs this before it
// can index the text that key was built from.
LL_COMMON_API size_t utf8str_bytes_from_cased_bytes(std::string_view utf8str,
                                                    size_t cased_bytes, bool to_upper);

// True when every byte is below 0x80, so the string is its own codepoint
// sequence and case conversion cannot move an offset within it. Worth asking
// before the walks above, which are only needed when it is false.
inline bool utf8str_is_ascii(std::string_view utf8str)
{
    for (unsigned char c : utf8str)
    {
        if (c & 0x80)
            return false;
    }
    return true;
}

// Return the half-open byte range of the emoji cluster (or single pictograph
// codepoint) that contains `byte_pos`. Any byte inside a cluster reports that
// whole cluster, character start or not -- a hit test lands on a pixel.
// Outside a cluster the answer is an empty range with first == second, which
// is what a non-pictograph, a byte inside a character, and a position out of
// bounds all give. Used by tooltip lookup to recover the full cluster from a
// hit-test position. Symmetric in spirit to
// utf8str_grapheme_align_*, but inclusive of cluster boundaries and aware of
// single pictographs (BMP and astral) that utf8str_find_emoji_clusters skips.
// Single-codepoint detection uses LLStringOps::isPictographBase, so
// © / ® / ☦ / ⚓ / ❤ all qualify; bare extenders (ZWJ, VS-15/16, keycap
// combiner, skin-tone modifiers, tag chars) do not.
LL_COMMON_API std::pair<size_t, size_t>
utf8str_emoji_range_at(std::string_view utf8str, size_t byte_pos);
LL_COMMON_API std::pair<size_t, size_t>
utf8str_emoji_range_at(std::string_view utf8str, size_t byte_pos,
                       const EmojiClusterList& clusters);

#if LL_WINDOWS
/* @name Windows string helpers
 */
//@{

/**
 * @brief Convert a wide string to/from std::string
 *
 * This replaces the unsafe W2A macro from ATL.
 */
// Avoid requiring this header to #include the Windows header file declaring
// our actual default code_page by delegating this function to our .cpp file.
LL_COMMON_API unsigned int ll_wstring_default_code_page();

// This is like ll_convert_forms(), with the added complexity of a code page
// parameter that may or may not be passed.
#define ll_convert_cp_forms(aliasmacro, OUTSTR, INSTR, longname)    \
/* declare the only nontrivial implementation (in .cpp file) */     \
LL_COMMON_API OUTSTR longname(                                      \
    const INSTR::value_type* in,                                    \
    size_t len,                                                     \
    unsigned int code_page=ll_wstring_default_code_page());         \
/* if passed only a char pointer, scan for nul terminator */        \
inline auto longname(const INSTR::value_type* in)                   \
{                                                                   \
    return longname(in, ll_convert_length(in));                     \
}                                                                   \
/* if passed string and length, extract its char pointer */         \
inline auto longname(                                               \
    const INSTR& in,                                                \
    size_t len,                                                     \
    unsigned int code_page=ll_wstring_default_code_page())          \
{                                                                   \
    return longname(in.c_str(), len, code_page);                    \
}                                                                   \
/* if passed only a string object, no scan, pass known length */    \
inline auto longname(const INSTR& in)                               \
{                                                                   \
    return longname(in.c_str(), in.length());                       \
}                                                                   \
aliasmacro(OUTSTR, INSTR, longname(in));                            \
aliasmacro(OUTSTR, const INSTR::value_type*, longname(in))

ll_convert_cp_forms(ll_convert_alias, std::string, std::wstring, ll_convert_wide_to_string);
ll_convert_cp_forms(ll_convert_alias, std::wstring, std::string, ll_convert_string_to_wide);

/**
 * Converts incoming string into utf8 string
 *
 */
LL_COMMON_API std::string ll_convert_string_to_utf8_string(const std::string& in);

/// Get Windows message string for passed GetLastError() code.
// Microsoft says DWORD is a typedef for unsigned long
// https://docs.microsoft.com/en-us/windows/desktop/winprog/windows-data-types
// so rather than drag windows.h into everybody's include space...
//
// Ordering matters for the DLL build: forward-declare the primary template,
// then declare the exported std::wstring specialization (defined in
// llstring.cpp), then define the primary in terms of that specialization. The
// specialization must be declared BEFORE the primary's body references it,
// else an implicit instantiation is assumed and clashes with the
// specialization's dll linkage (C2375 "different linkage"). The primary itself
// carries no LL_COMMON_API: it is defined inline and instantiated per-consumer,
// so dllexport/dllimport on it is both illegal and unnecessary.
template<typename STRING>
STRING windows_message(unsigned long error);

/// There's only one real implementation
template<>
LL_COMMON_API std::wstring windows_message<std::wstring>(unsigned long error);

// the general case is just a conversion from the sole implementation
template<typename STRING>
STRING windows_message(unsigned long error)
{
    return ll_convert<STRING>(windows_message<std::wstring>(error));
}

/// Get Windows message string, implicitly calling GetLastError()
LL_COMMON_API unsigned long windows_get_last_error();

template<typename STRING>
STRING windows_message() { return windows_message<STRING>(windows_get_last_error()); }

//@}

LL_COMMON_API std::optional<std::wstring> llstring_getoptenv(const std::string& key);

LL_COMMON_API S32 wide_wstring_length(const std::wstring& utf16str, const S32 utf16_len);

#else // ! LL_WINDOWS

LL_COMMON_API std::optional<std::string>  llstring_getoptenv(const std::string& key);

#endif // ! LL_WINDOWS

/**
 * Many of the 'strip' and 'replace' methods of LLStringUtilBase need
 * specialization to work with the signed char type.
 * Sadly, it is not possible (AFAIK) to specialize a single method of
 * a template class.
 * That stuff should go here.
 */
namespace LLStringFn
{
    /**
     * @brief Replace all non-printable characters with replacement in
     * string.
     * NOTE - this will zap non-ascii
     *
     * @param [in,out] string the to modify. out value is the string
     * with zero non-printable characters.
     * @param The replacement character. use LL_UNKNOWN_CHAR if unsure.
     */
    LL_COMMON_API void replace_nonprintable_in_ascii(
        std::basic_string<char>& string,
        char replacement);


    /**
     * @brief Replace all non-printable characters and pipe characters
     * with replacement in a string.
     * NOTE - this will zap non-ascii
     *
     * @param [in,out] the string to modify. out value is the string
     * with zero non-printable characters and zero pipe characters.
     * @param The replacement character. use LL_UNKNOWN_CHAR if unsure.
     */
    LL_COMMON_API void replace_nonprintable_and_pipe_in_ascii(std::basic_string<char>& str,
                                       char replacement);


    /**
     * @brief Remove all characters that are not allowed in XML 1.0.
     * Returns a copy of the string with those characters removed.
     * Works with US ASCII and UTF-8 encoded strings.  JC
     */
    LL_COMMON_API std::string strip_invalid_xml(const std::string& input);


    /**
     * @brief Replace all characters that are not allowed in XML 1.0
     * with corresponding literals: [ < > & ] => [ &lt; &gt; &amp; ]
     */
    LL_COMMON_API std::string xml_encode(const std::string& input, bool for_attribute = false);


    /**
     * @brief Replace some of XML literals that are defined in XML 1.0
     * with corresponding characters: [ &lt; &gt; &amp; ] => [ < > & ]
     */
    LL_COMMON_API std::string xml_decode(const std::string& input, bool for_attribute = false);


    /**
     * @brief Replace all control characters (0 <= c < 0x20) with replacement in
     * string.   This is safe for utf-8
     *
     * @param [in,out] string the to modify. out value is the string
     * with zero non-printable characters.
     * @param The replacement character. use LL_UNKNOWN_CHAR if unsure.
     */
    LL_COMMON_API void replace_ascii_controlchars(
        std::basic_string<char>& string,
        char replacement);
}

////////////////////////////////////////////////////////////
// NOTE: LLStringUtil::format, getTokens, and support functions moved to llstring.cpp.
// Calling these for anything other than LLStringUtil will produce link errors.

////////////////////////////////////////////////////////////

// static
template <class T>
std::vector<typename LLStringUtilBase<T>::string_type>
LLStringUtilBase<T>::getTokens(const string_type& instr, const string_type& delims)
{
    std::vector<string_type> tokens;
    getTokens(instr, tokens, delims);
    return tokens;
}

// static
template <class T>
std::vector<typename LLStringUtilBase<T>::string_type>
LLStringUtilBase<T>::getTokens(const string_type& instr,
                               const string_type& drop_delims,
                               const string_type& keep_delims,
                               const string_type& quotes)
{
    std::vector<string_type> tokens;
    getTokens(instr, tokens, drop_delims, keep_delims, quotes);
    return tokens;
}

// static
template <class T>
std::vector<typename LLStringUtilBase<T>::string_type>
LLStringUtilBase<T>::getTokens(const string_type& instr,
                               const string_type& drop_delims,
                               const string_type& keep_delims,
                               const string_type& quotes,
                               const string_type& escapes)
{
    std::vector<string_type> tokens;
    getTokens(instr, tokens, drop_delims, keep_delims, quotes, escapes);
    return tokens;
}

namespace LLStringUtilBaseImpl
{

/**
 * Input string scanner helper for getTokens(), or really any other
 * character-parsing routine that may have to deal with escape characters.
 * This implementation defines the concept (also an interface, should you
 * choose to implement the concept by subclassing) and provides trivial
 * implementations for a string @em without escape processing.
 */
template <class T>
struct InString
{
    typedef std::basic_string<T> string_type;
    typedef typename string_type::const_iterator const_iterator;

    InString(const_iterator b, const_iterator e):
        mIter(b),
        mEnd(e)
    {}
    virtual ~InString() {}

    bool done() const { return mIter == mEnd; }
    /// Is the current character (*mIter) escaped? This implementation can
    /// answer trivially because it doesn't support escapes.
    virtual bool escaped() const { return false; }
    /// Obtain the current character and advance @c mIter.
    virtual T next() { return *mIter++; }
    /// Does the current character match specified character?
    virtual bool is(T ch) const { return (! done()) && *mIter == ch; }
    /// Is the current character any one of the specified characters?
    virtual bool oneof(const string_type& delims) const
    {
        return (! done()) && LLStringUtilBase<T>::contains(delims, *mIter);
    }

    /**
     * Scan forward from @from until either @a delim or end. This is primarily
     * useful for processing quoted substrings.
     *
     * If we do see @a delim, append everything from @from until (excluding)
     * @a delim to @a into, advance @c mIter to skip @a delim, and return @c
     * true.
     *
     * If we do not see @a delim, do not alter @a into or @c mIter and return
     * @c false. Do not pass GO, do not collect $200.
     *
     * @note The @c false case described above implements normal getTokens()
     * treatment of an unmatched open quote: treat the quote character as if
     * escaped, that is, simply collect it as part of the current token. Other
     * plausible behaviors directly affect the way getTokens() deals with an
     * unmatched quote: e.g. throwing an exception to treat it as an error, or
     * assuming a close quote beyond end of string (in which case return @c
     * true).
     */
    virtual bool collect_until(string_type& into, const_iterator from, T delim)
    {
        const_iterator found = std::find(from, mEnd, delim);
        // If we didn't find delim, change nothing, just tell caller.
        if (found == mEnd)
            return false;
        // Found delim! Append everything between from and found.
        into.append(from, found);
        // advance past delim in input
        mIter = found + 1;
        return true;
    }

    const_iterator mIter, mEnd;
};

/// InString subclass that handles escape characters
template <class T>
class InEscString: public InString<T>
{
public:
    typedef InString<T> super;
    typedef typename super::string_type string_type;
    typedef typename super::const_iterator const_iterator;
    using super::done;
    using super::mIter;
    using super::mEnd;

    InEscString(const_iterator b, const_iterator e, const string_type& escapes):
        super(b, e),
        mEscapes(escapes)
    {
        // Even though we've already initialized 'mIter' via our base-class
        // constructor, set it again to check for initial escape char.
        setiter(b);
    }

    /// This implementation uses the answer cached by setiter().
    virtual bool escaped() const { return mIsEsc; }
    virtual T next()
    {
        // If we're looking at the escape character of an escape sequence,
        // skip that character. This is the one time we can modify 'mIter'
        // without using setiter: for this one case we DO NOT CARE if the
        // escaped character is itself an escape.
        if (mIsEsc)
            ++mIter;
        // If we were looking at an escape character, this is the escaped
        // character; otherwise it's just the next character.
        T result(*mIter);
        // Advance mIter, checking for escape sequence.
        setiter(mIter + 1);
        return result;
    }

    virtual bool is(T ch) const
    {
        // Like base-class is(), except that an escaped character matches
        // nothing.
        return (! done()) && (! mIsEsc) && *mIter == ch;
    }

    virtual bool oneof(const string_type& delims) const
    {
        // Like base-class oneof(), except that an escaped character matches
        // nothing.
        return (! done()) && (! mIsEsc) && LLStringUtilBase<T>::contains(delims, *mIter);
    }

    virtual bool collect_until(string_type& into, const_iterator from, T delim)
    {
        // Deal with escapes in the characters we collect; that is, an escaped
        // character must become just that character without the preceding
        // escape. Collect characters in a separate string rather than
        // directly appending to 'into' in case we do not find delim, in which
        // case we're supposed to leave 'into' unmodified.
        string_type collected;
        // For scanning purposes, we're going to work directly with 'mIter'.
        // Save its current value in case we fail to see delim.
        const_iterator save_iter(mIter);
        // Okay, set 'mIter', checking for escape.
        setiter(from);
        while (! done())
        {
            // If we see an unescaped delim, stop and report success.
            if ((! mIsEsc) && *mIter == delim)
            {
                // Append collected chars to 'into'.
                into.append(collected);
                // Don't forget to advance 'mIter' past delim.
                setiter(mIter + 1);
                return true;
            }
            // We're not at end, and either we're not looking at delim or it's
            // escaped. Collect this character and keep going.
            collected.push_back(next());
        }
        // Here we hit 'mEnd' without ever seeing delim. Restore mIter and tell
        // caller.
        setiter(save_iter);
        return false;
    }

private:
    void setiter(const_iterator i)
    {
        mIter = i;

        // Every time we change 'mIter', set 'mIsEsc' to be able to repetitively
        // answer escaped() without having to rescan 'mEscapes'. mIsEsc caches
        // contains(mEscapes, *mIter).

        // We're looking at an escaped char if we're not already at end (that
        // is, *mIter is even meaningful); if *mIter is in fact one of the
        // specified escape characters; and if there's one more character
        // following it. That is, if an escape character is the very last
        // character of the input string, it loses its special meaning.
        mIsEsc = (! done()) &&
                LLStringUtilBase<T>::contains(mEscapes, *mIter) &&
                (mIter+1) != mEnd;
    }

    const string_type mEscapes;
    bool mIsEsc;
};

/// getTokens() implementation based on InString concept
template <typename INSTRING, typename string_type>
void getTokens(INSTRING& instr, std::vector<string_type>& tokens,
               const string_type& drop_delims, const string_type& keep_delims,
               const string_type& quotes)
{
    // There are times when we want to match either drop_delims or
    // keep_delims. Concatenate them up front to speed things up.
    string_type all_delims(drop_delims + keep_delims);
    // no tokens yet
    tokens.clear();

    // try for another token
    while (! instr.done())
    {
        // scan past any drop_delims
        while (instr.oneof(drop_delims))
        {
            // skip this drop_delim
            instr.next();
            // but if that was the end of the string, done
            if (instr.done())
                return;
        }
        // found the start of another token: make a slot for it
        tokens.push_back(string_type());
        if (instr.oneof(keep_delims))
        {
            // *iter is a keep_delim, a token of exactly 1 character. Append
            // that character to the new token and proceed.
            tokens.back().push_back(instr.next());
            continue;
        }
        // Here we have a non-delimiter token, which might consist of a mix of
        // quoted and unquoted parts. Use bash rules for quoting: you can
        // embed a quoted substring in the midst of an unquoted token (e.g.
        // ~/"sub dir"/myfile.txt); you can ram two quoted substrings together
        // to make a single token (e.g. 'He said, "'"Don't."'"'). We diverge
        // from bash in that bash considers an unmatched quote an error. Our
        // param signature doesn't allow for errors, so just pretend it's not
        // a quote and embed it.
        // At this level, keep scanning until we hit the next delimiter of
        // either type (drop_delims or keep_delims).
        while (! instr.oneof(all_delims))
        {
            // If we're looking at an open quote, search forward for
            // a close quote, collecting characters along the way.
            if (instr.oneof(quotes) &&
                instr.collect_until(tokens.back(), instr.mIter+1, *instr.mIter))
            {
                // collect_until is cleverly designed to do exactly what we
                // need here. No further action needed if it returns true.
            }
            else
            {
                // Either *iter isn't a quote, or there's no matching close
                // quote: in other words, just an ordinary char. Append it to
                // current token.
                tokens.back().push_back(instr.next());
            }
            // having scanned that segment of this token, if we've reached the
            // end of the string, we're done
            if (instr.done())
                return;
        }
    }
}

} // namespace LLStringUtilBaseImpl

// static
template <class T>
void LLStringUtilBase<T>::getTokens(const string_type& string, std::vector<string_type>& tokens,
                                    const string_type& drop_delims, const string_type& keep_delims,
                                    const string_type& quotes)
{
    // Because this overload doesn't support escapes, use simple InString to
    // manage input range.
    LLStringUtilBaseImpl::InString<T> instring(string.begin(), string.end());
    LLStringUtilBaseImpl::getTokens(instring, tokens, drop_delims, keep_delims, quotes);
}

// static
template <class T>
void LLStringUtilBase<T>::getTokens(const string_type& string, std::vector<string_type>& tokens,
                                    const string_type& drop_delims, const string_type& keep_delims,
                                    const string_type& quotes, const string_type& escapes)
{
    // This overload must deal with escapes. Delegate that to InEscString
    // (unless there ARE no escapes).
    std::unique_ptr< LLStringUtilBaseImpl::InString<T> > instrp;
    if (escapes.empty())
        instrp = std::make_unique<LLStringUtilBaseImpl::InString<T>>(string.begin(), string.end());
    else
        instrp = std::make_unique<LLStringUtilBaseImpl::InEscString<T>>(string.begin(), string.end(), escapes);
    LLStringUtilBaseImpl::getTokens(*instrp, tokens, drop_delims, keep_delims, quotes);
}

// static
template<class T>
S32 LLStringUtilBase<T>::compareStrings(const T* lhs, const T* rhs)
{
    S32 result;
    if( lhs == rhs )
    {
        result = 0;
    }
    else
    if ( !lhs || !lhs[0] )
    {
        result = ((!rhs || !rhs[0]) ? 0 : 1);
    }
    else
    if ( !rhs || !rhs[0])
    {
        result = -1;
    }
    else
    {
        result = LLStringOps::collate(lhs, rhs);
    }
    return result;
}

//static
template<class T>
S32 LLStringUtilBase<T>::compareStrings(const string_type& lhs, const string_type& rhs)
{
    return LLStringOps::collate(lhs.c_str(), rhs.c_str());
}

//static
template<class T>
bool LLStringUtilBase<T>::isEqualInsensitiveASCII(string_view_type lhs, string_view_type rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_type i = 0; i < lhs.size(); ++i)
    {
        T a = lhs[i];
        T b = rhs[i];
        // Only the twenty-six. Anything else, including every byte of a
        // multi-byte character, has to match exactly.
        if (a >= 'A' && a <= 'Z') { a = (T)(a - 'A' + 'a'); }
        if (b >= 'A' && b <= 'Z') { b = (T)(b - 'A' + 'a'); }
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

// Case-insensitive comparison is Unicode collation with the level that carries
// case switched off -- one call, no copies. Uppercasing both sides and then
// comparing, which is what this used to do, is not the same thing: it folds
// sharp s onto SS and so calls two different words equal, and it allocates and
// cases both sides on every comparison in a sort.
template<> LL_COMMON_API S32 LLStringUtilBase<char>::compareInsensitive(const char* lhs, const char* rhs);
template<> LL_COMMON_API S32 LLStringUtilBase<char>::compareInsensitive(std::string_view lhs, std::string_view rhs);

// Collation is ICU's, so these are defined in llstring.cpp where it is
// reachable.
template<> LL_COMMON_API S32 LLStringUtilBase<char>::compareDict(std::string_view a, std::string_view b);
template<> LL_COMMON_API S32 LLStringUtilBase<char>::compareDictInsensitive(std::string_view a, std::string_view b);

// Puts compareDict() in a form appropriate for LL container classes to use for sorting.
// static
template<class T>
bool LLStringUtilBase<T>::precedesDict( string_view_type a, string_view_type b )
{
    if( a.size() && b.size() )
    {
        return (LLStringUtilBase<T>::compareDict(a, b) < 0);
    }
    else
    {
        return (!b.empty());
    }
}

//static
template<class T>
void LLStringUtilBase<T>::toUpper(string_type& string)
{
    if( !string.empty() )
    {
        std::transform(
            string.begin(),
            string.end(),
            string.begin(),
            (T(*)(T)) &LLStringOps::toUpper);
    }
}

//static
template<class T>
void LLStringUtilBase<T>::toLower(string_type& string)
{
    if( !string.empty() )
    {
        std::transform(
            string.begin(),
            string.end(),
            string.begin(),
            (T(*)(T)) &LLStringOps::toLower);
    }
}

// The narrow forms case UTF-8 in place of running tolower/toupper over each
// byte, which left every non-ASCII character alone. Pure-ASCII input is
// unaffected; anything else now cases, and may change length doing so -- see
// utf8str_bytes_from_cased_bytes for callers holding an offset across it.
template<> LL_COMMON_API void LLStringUtilBase<char>::toUpper(std::string& string);
template<> LL_COMMON_API void LLStringUtilBase<char>::toLower(std::string& string);

// The narrow trims decode, so they answer for the whole of Unicode: a
// no-break space pasted in from a web page, an ideographic space either side
// of CJK, and a narrow no-break space are whitespace here, where a byte-wise
// isspace() saw none of them and left them in the string. ASCII settles
// inline and only a lead byte pays for the decode. Same rule as utf8str_trim,
// which shares the implementation.
template<> LL_COMMON_API void LLStringUtilBase<char>::trimHead(std::string& string);
template<> LL_COMMON_API void LLStringUtilBase<char>::trimTail(std::string& string);
template<> LL_COMMON_API void LLStringUtilBase<char>::trimHead(std::string_view& string);
template<> LL_COMMON_API void LLStringUtilBase<char>::trimTail(std::string_view& string);

//static
template<class T>
void LLStringUtilBase<T>::trimHead(string_type& string)
{
    if( !string.empty() )
    {
        size_type i = 0;
        while( i < string.length() && LLStringOps::isSpace( string[i] ) )
        {
            i++;
        }
        string.erase(0, i);
    }
}

//static
template<class T>
void LLStringUtilBase<T>::trimTail(string_type& string)
{
    if(!string.empty())
    {
        size_type len = string.length();
        size_type i = len;
        while( i > 0 && LLStringOps::isSpace( string[i-1] ) )
        {
            --i;
        }

        string.erase( i, len - i );
    }
}

template<class T>
void LLStringUtilBase<T>::trimTail(string_type& string, const string_type& tokens)
{
    if(!string.empty())
    {
        size_type len = string.length();
        size_type i = len;
        while( i > 0 && (tokens.find_first_of(string[i-1]) != string_type::npos) )
        {
            --i;
        }

        string.erase( i, len - i );
    }
}

// static
template<class T>
void LLStringUtilBase<T>::trimHead(string_view_type& string)
{
    if (!string.empty())
    {
        size_type i = 0;
        while (i < string.length() && LLStringOps::isSpace(string[i]))
        {
            i++;
        }
        string = string.substr(i);
    }
}

// static
template<class T>
void LLStringUtilBase<T>::trimTail(string_view_type& string)
{
    if (string.size())
    {
        size_type len = string.length();
        size_type i   = len;
        while (i > 0 && LLStringOps::isSpace(string[i - 1]))
        {
            i--;
        }

        string = string.substr(0, i);
    }
}


// Replace line feeds with carriage return-line feed pairs.
//static
template<class T>
void LLStringUtilBase<T>::addCRLF(string_type& string)
{
    const T LF = 10;
    const T CR = 13;

    // Count the number of line feeds
    size_type count = 0;
    size_type len = string.size();
    size_type i;
    for( i = 0; i < len; i++ )
    {
        if( string[i] == LF )
        {
            count++;
        }
    }

    // Insert a carriage return before each line feed
    if( count )
    {
        size_type size = len + count;
        T *t = new T[size];
        size_type j = 0;
        for( i = 0; i < len; ++i )
        {
            if( string[i] == LF )
            {
                t[j] = CR;
                ++j;
            }
            t[j] = string[i];
            ++j;
        }

        string.assign(t, size);
        delete[] t;
    }
}

// Remove all carriage returns
//static
template<class T>
void LLStringUtilBase<T>::removeCRLF(string_type& string)
{
    const T CR = 13;

    size_type cr_count = 0;
    size_type len = string.size();
    size_type i;
    for( i = 0; i < len - cr_count; i++ )
    {
        if( string[i+cr_count] == CR )
        {
            cr_count++;
        }

        string[i] = string[i+cr_count];
    }
    string.erase(i, cr_count);
}

//static
template<class T>
void LLStringUtilBase<T>::removeWindowsCR(string_type& string)
{
    if (string.empty())
    {
        return;
    }
    const T LF = 10;
    const T CR = 13;

    size_type cr_count = 0;
    size_type len = string.size();
    size_type i;
    for( i = 0; i < len - cr_count - 1; i++ )
    {
        if( string[i+cr_count] == CR && string[i+cr_count+1] == LF)
        {
            cr_count++;
        }

        string[i] = string[i+cr_count];
    }
    string.erase(i, cr_count);
}

//static
template<class T>
void LLStringUtilBase<T>::replaceChar( string_type& string, T target, T replacement )
{
    size_type found_pos = 0;
    while( (found_pos = string.find(target, found_pos)) != string_type::npos )
    {
        string[found_pos] = replacement;
        found_pos++; // avoid infinite defeat if target == replacement
    }
}

//static
template<class T>
void LLStringUtilBase<T>::replaceString( string_type& string, string_view_type target, string_view_type replacement )
{
    // Either view is allowed to point into `string` itself, and both are read
    // after a replace that can have moved the buffer they point into -- target
    // on every turn of the loop below, not only the first. Copy in that one
    // case; the pointer comparison costs nothing on the ordinary path, where
    // the arguments are literals or unrelated strings.
    const T* const begin = string.data();
    const T* const end   = begin + string.size();
    const auto views_the_subject = [begin, end](string_view_type v)
    {
        return v.data() >= begin && v.data() <= end;
    };
    if (views_the_subject(target) || views_the_subject(replacement))
    {
        const string_type owned_target(target);
        const string_type owned_replacement(replacement);
        replaceString(string,
                      string_view_type(owned_target),
                      string_view_type(owned_replacement));
        return;
    }

    size_type found_pos = 0;
    while( (found_pos = string.find(target, found_pos)) != string_type::npos )
    {
        string.replace( found_pos, target.length(), replacement );
        found_pos += replacement.length(); // avoid infinite defeat if replacement contains target
    }
}

//static
template<class T>
void LLStringUtilBase<T>::replaceNonstandardASCII( string_type& string, T replacement )
{
    const char LF = 10;
    const S8 MIN = 32;
//  const S8 MAX = 127;

    size_type len = string.size();
    for( size_type i = 0; i < len; i++ )
    {
        // No need to test MAX < mText[i] because we treat mText[i] as a signed char,
        // which has a max value of 127.
        if( ( S8(string[i]) < MIN ) && (string[i] != LF) )
        {
            string[i] = replacement;
        }
    }
}

//static
template<class T>
void LLStringUtilBase<T>::replaceTabsWithSpaces( string_type& str, size_type spaces_per_tab )
{
    const T TAB = '\t';
    const T SPACE = ' ';

    string_type out_str;
    // Replace tabs with spaces
    for (size_type i = 0; i < str.length(); i++)
    {
        if (str[i] == TAB)
        {
            for (size_type j = 0; j < spaces_per_tab; j++)
                out_str += SPACE;
        }
        else
        {
            out_str += str[i];
        }
    }
    str = out_str;
}

//static
template<class T>
std::basic_string<T> LLStringUtilBase<T>::capitalize(const string_type& str)
{
    string_type result(str);
    capitalize(result);
    return result;
}

//static
template<class T>
void LLStringUtilBase<T>::capitalize(string_type& str)
{
    if (str.size())
    {
        auto last = str[0] = LLStringOps::toUpper(str[0]);
        for (U32 i = 1; i < str.size(); ++i)
        {
            last = (last == ' ' || last == '-' || last == '_') ? str[i] = LLStringOps::toUpper(str[i]) : str[i];
        }
    }
}

//static
template<class T>
bool LLStringUtilBase<T>::containsNonprintable(string_view_type string)
{
    for (const T c : string)
    {
        if (LLStringOps::isNonprintable(c))
        {
            return true;
        }
    }
    return false;
}

//static
template<class T>
void LLStringUtilBase<T>::stripNonprintable(string_type& string)
{
    string.erase(std::remove_if(string.begin(), string.end(),
                                [](T c) { return LLStringOps::isNonprintable(c); }),
                 string.end());
}

// The narrow forms read UTF-8 rather than bytes. A byte below 0x20 is always a
// C0 control and so was safe to test on its own, but a control above the ASCII
// range is two bytes and neither of them looks like one. Defined in
// llstring.cpp, where the Unicode tables are.
template<> LL_COMMON_API bool LLStringUtilBase<char>::containsNonprintable(std::string_view string);
template<> LL_COMMON_API void LLStringUtilBase<char>::stripNonprintable(std::string& string);

// Likewise: uppercasing one byte can only ever reach ASCII.
// Parsing is std::from_chars for the integers and fast_float::from_chars for
// the reals, both defined in llstring.cpp so neither header has to reach the
// whole viewer. Only char is ever asked for; the stream extraction these
// replace could not have compiled for llwchar either.
template<> LL_COMMON_API bool LLStringUtilBase<char>::convertToU32(std::string_view string, U32& value);
template<> LL_COMMON_API bool LLStringUtilBase<char>::convertToS32(std::string_view string, S32& value);
template<> LL_COMMON_API bool LLStringUtilBase<char>::convertToF64(std::string_view string, F64& value);

template<> LL_COMMON_API void LLStringUtilBase<char>::capitalize(std::string& str);

// *TODO: reimplement in terms of algorithm
template<class T>
std::basic_string<T> LLStringUtilBase<T>::quote(const string_type& str,
                                                const string_type& triggers,
                                                const string_type& escape)
{
    size_type len(str.length());
    // If the string is already quoted, assume user knows what s/he's doing.
    if (len >= 2 && str[0] == '"' && str[len-1] == '"')
    {
        return str;
    }

    // Not already quoted: do we need to? triggers.empty() is a special case
    // meaning "always quote."
    if ((! triggers.empty()) && str.find_first_of(triggers) == string_type::npos)
    {
        // no trigger characters, don't bother quoting
        return str;
    }

    // For whatever reason, we must quote this string.
    string_type result;
    result.push_back('"');
    for (typename string_type::const_iterator ci(str.begin()), cend(str.end()); ci != cend; ++ci)
    {
        if (*ci == '"')
        {
            result.append(escape);
        }
        result.push_back(*ci);
    }
    result.push_back('"');
    return result;
}

template<class T>
void LLStringUtilBase<T>::_makeASCII(string_type& string)
{
    // Replace non-ASCII chars with LL_UNKNOWN_CHAR
    for (size_type i = 0; i < string.length(); i++)
    {
        if (string[i] > 0x7f)
        {
            string[i] = LL_UNKNOWN_CHAR;
        }
    }
}

// static
template<class T>
void LLStringUtilBase<T>::copy( T* dst, const T* src, size_type dst_size )
{
    if( dst_size > 0 )
    {
        size_type min_len = 0;
        if( src )
        {
            min_len = llmin( dst_size - 1, strlen( src ) );  /* Flawfinder: ignore */
            memcpy(dst, src, min_len * sizeof(T));      /* Flawfinder: ignore */
        }
        dst[min_len] = '\0';
    }
}

// static
template<class T>
void LLStringUtilBase<T>::copyInto(string_type& dst, string_view_type src, size_type offset)
{
    // src is allowed to view dst. Appending a string to itself is defined, but
    // appending from a view of it is not, and the insert path below assigns dst
    // out from under src before reading it. Taking src by value used to make
    // this safe at no stated cost; a view does not.
    const T* const begin = dst.data();
    if (src.data() >= begin && src.data() <= begin + dst.size())
    {
        const string_type owned(src);
        copyInto(dst, string_view_type(owned), offset);
        return;
    }

    if ( offset == dst.length() )
    {
        // special case - append to end of string and avoid expensive
        // (when strings are large) string manipulations
        dst += src;
    }
    else
    {
        string_type tail = dst.substr(offset);

        dst = dst.substr(0, offset);
        dst += src;
        dst += tail;
    };
}

// True if this is the head of s.
//static
template<class T>
bool LLStringUtilBase<T>::isHead( const string_type& string, const T* s )
{
    if( string.empty() )
    {
        // Early exit
        return false;
    }
    else
    {
        return (strncmp( s, string.c_str(), string.size() ) == 0);
    }
}

// static
template<class T>
bool LLStringUtilBase<T>::startsWith(
    string_view_type string,
    string_view_type substr)
{
    if(string.empty() || (substr.empty())) return false;
    if (substr.length() > string.length()) return false;
    if (0 == string.compare(0, substr.length(), substr)) return true;
    return false;
}

// static
template<class T>
bool LLStringUtilBase<T>::endsWith(
    string_view_type string,
    string_view_type substr)
{
    if(string.empty() || (substr.empty())) return false;
    size_t sub_len = substr.length();
    size_t str_len = string.length();
    if (sub_len > str_len) return false;
    if (0 == string.compare(str_len - sub_len, sub_len, substr)) return true;
    return false;
}

// static
template<class T>
auto LLStringUtilBase<T>::getoptenv(const std::string& key) -> std::optional<string_type>
{
    auto found(llstring_getoptenv(key));
    if (found)
    {
        // return populated std::optional
        return { ll_convert<string_type>(*found) };
    }
    else
    {
        // empty std::optional
        return {};
    }
}

// static
template<class T>
auto LLStringUtilBase<T>::getenv(const std::string& key, const string_type& dflt) -> string_type
{
    auto found(getoptenv(key));
    if (found)
    {
        return *found;
    }
    else
    {
        return dflt;
    }
}

template<class T>
bool LLStringUtilBase<T>::convertToBOOL(string_view_type string, bool& value)
{
    if( string.empty() )
    {
        return false;
    }

    trim(string);
    if(
        (string == "1") ||
        (string == "T") ||
        (string == "t") ||
        (string == "TRUE") ||
        (string == "true") ||
        (string == "True") )
    {
        value = true;
        return true;
    }
    else
    if(
        (string == "0") ||
        (string == "F") ||
        (string == "f") ||
        (string == "FALSE") ||
        (string == "false") ||
        (string == "False") )
    {
        value = false;
        return true;
    }

    return false;
}

template<class T>
bool LLStringUtilBase<T>::convertToU8(string_view_type string, U8& value)
{
    S32 value32 = 0;
    bool success = convertToS32(string, value32);
    if( success && (U8_MIN <= value32) && (value32 <= U8_MAX) )
    {
        value = (U8) value32;
        return true;
    }
    return false;
}

template<class T>
bool LLStringUtilBase<T>::convertToS8(string_view_type string, S8& value)
{
    S32 value32 = 0;
    bool success = convertToS32(string, value32);
    if( success && (S8_MIN <= value32) && (value32 <= S8_MAX) )
    {
        value = (S8) value32;
        return true;
    }
    return false;
}

template<class T>
bool LLStringUtilBase<T>::convertToS16(string_view_type string, S16& value)
{
    S32 value32 = 0;
    bool success = convertToS32(string, value32);
    if( success && (S16_MIN <= value32) && (value32 <= S16_MAX) )
    {
        value = (S16) value32;
        return true;
    }
    return false;
}

template<class T>
bool LLStringUtilBase<T>::convertToU16(string_view_type string, U16& value)
{
    S32 value32 = 0;
    bool success = convertToS32(string, value32);
    if( success && (U16_MIN <= value32) && (value32 <= U16_MAX) )
    {
        value = (U16) value32;
        return true;
    }
    return false;
}

template<class T>
bool LLStringUtilBase<T>::convertToF32(string_view_type string, F32& value)
{
    F64 value64 = 0.0;
    bool success = convertToF64(string, value64);
    if( success && (-F32_MAX <= value64) && (value64 <= F32_MAX) )
    {
        value = (F32) value64;
        return true;
    }
    return false;
}

template<class T>
void LLStringUtilBase<T>::truncate(string_type& string, size_type count)
{
    size_type cur_size = string.size();
    string.resize(count < cur_size ? count : cur_size);
}

// The good thing about *declaration* macros, vs. usage macros, is that now
// we're done with them: we don't need them to bleed into the consuming source
// file.
#undef ll_convert_alias
#undef LL_CONVERT_COPY_CHARS
#undef ll_convert_forms
#undef ll_convert_cp_forms

#endif  // LL_STRING_H

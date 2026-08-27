/**
 * @file llsdserialize_xml.cpp
 * @brief XML parsers and formatters for LLSD
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "linden_common.h"
#include "llsdserialize_xml.h"

#include <charconv>
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <simdutf.h>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>


/**
 * LLSDXMLFormatter
 */
LLSDXMLFormatter::LLSDXMLFormatter(bool boolAlpha, const std::string& realFormat,
                                   EFormatterOptions options):
    LLSDFormatter(boolAlpha, realFormat, options)
{
}

// virtual
LLSDXMLFormatter::~LLSDXMLFormatter()
{
}

// virtual
S32 LLSDXMLFormatter::format(const LLSD& data, std::ostream& ostr,
                             EFormatterOptions options) const
{
    // A stream that has already failed cannot record anything we write, and
    // the caller has no way to tell an empty document from a lost one.
    if (ostr.rdstate() & (std::ios_base::badbit | std::ios_base::failbit))
    {
        LL_WARNS() << "LLSDXMLFormatter::format: Stream already in error state" << LL_ENDL;
        return -1;
    }

    S32 rv = 0;
    try
    {
        // The Sink flushes from its destructor, so it has to be destroyed
        // inside the try — a throw out of that flush would be a throw out of
        // a destructor.
        Sink sink(ostr);
        std::string post;
        if (options & LLSDFormatter::OPTIONS_PRETTY)
        {
            post = "\n";
        }
        sink.put("<llsd>");
        sink.put(post);
        rv = format_impl(data, sink, options, 1);
        sink.put("</llsd>\n");
    }
    catch (const std::bad_alloc&)
    {
        // we might be saving something massive, don't error or crash
        LL_WARNS() << "LLSDXMLFormatter::format: Memory allocation failed during formatting" << LL_ENDL;
        return -1;
    }
    catch (const std::exception& e)
    {
        LL_WARNS() << "LLSDXMLFormatter::format: Standard exception: " << e.what() << LL_ENDL;
        return -1;
    }
    catch (...)
    {
        LL_WARNS() << "LLSDXMLFormatter::format: Unknown exception during formatting" << LL_ENDL;
        return -1;
    }

    // The Sink writes in blocks and does not check the stream as it goes, so
    // a mid-document I/O failure only shows up in the stream's state here.
    if (ostr.rdstate() & (std::ios_base::badbit | std::ios_base::failbit))
    {
        LL_WARNS() << "LLSDXMLFormatter::format: Stream I/O failed"
                   << " - Stream state: good=" << ostr.good()
                   << " eof=" << ostr.eof()
                   << " fail=" << ostr.fail()
                   << " bad=" << ostr.bad() << LL_ENDL;
        return -1;
    }

    return rv;
}

// virtual
S32 LLSDXMLFormatter::format_impl(const LLSD& data, std::ostream& ostr,
                                  EFormatterOptions options, U32 level) const
{
    Sink sink(ostr);
    return format_impl(data, sink, options, level);
}

S32 LLSDXMLFormatter::format_impl(const LLSD& data, Sink& sink,
                                  EFormatterOptions options, U32 level) const
{
    S32 format_count = 1;
    std::string pre;
    std::string post;

    if (options & LLSDFormatter::OPTIONS_PRETTY)
    {
        pre.assign(4 * (size_t)level, ' ');
        post = "\n";
    }

    switch(data.type())
    {
    case LLSD::TypeMap:
        if(0 == data.size())
        {
            sink.put(pre); sink.put("<map />"); sink.put(post);
        }
        else
        {
            sink.put(pre); sink.put("<map>"); sink.put(post);
            LLSD::map_const_iterator iter = data.beginMap();
            LLSD::map_const_iterator end = data.endMap();
            for(; iter != end; ++iter)
            {
                sink.put(pre); sink.put("<key>");
                escapeStringTo(sink.buffer(), (*iter).first);
                sink.checkFlush();
                sink.put("</key>"); sink.put(post);
                format_count += format_impl((*iter).second, sink, options, level + 1);
            }
            sink.put(pre); sink.put("</map>"); sink.put(post);
        }
        break;

    case LLSD::TypeArray:
        if(0 == data.size())
        {
            sink.put(pre); sink.put("<array />"); sink.put(post);
        }
        else
        {
            sink.put(pre); sink.put("<array>"); sink.put(post);
            LLSD::array_const_iterator iter = data.beginArray();
            LLSD::array_const_iterator end = data.endArray();
            for(; iter != end; ++iter)
            {
                format_count += format_impl(*iter, sink, options, level + 1);
            }
            sink.put(pre); sink.put("</array>"); sink.put(post);
        }
        break;

    case LLSD::TypeUndefined:
        sink.put(pre); sink.put("<undef />"); sink.put(post);
        break;

    case LLSD::TypeBoolean:
        sink.put(pre); sink.put("<boolean>");
        if(mBoolAlpha ||
           (sink.streamFlags() & std::ios::boolalpha)
           )
        {
            if (data.asBoolean()) sink.put("true"); else sink.put("false");
        }
        else
        {
            sink.put(data.asBoolean() ? '1' : '0');
        }
        sink.put("</boolean>"); sink.put(post);
        break;

    case LLSD::TypeInteger:
        sink.put(pre); sink.put("<integer>");
        sink.putInteger(data.asInteger());
        sink.put("</integer>"); sink.put(post);
        break;

    case LLSD::TypeReal:
    {
        sink.put(pre); sink.put("<real>");
        if(mRealFormat.empty())
        {
            sink.putReal(data.asReal());
        }
        else
        {
            formatReal(data.asReal(), sink.buffer());
            sink.checkFlush();
        }
        sink.put("</real>"); sink.put(post);
        break;
    }

    case LLSD::TypeUUID:
        if(data.asUUID().isNull())
        {
            sink.put(pre); sink.put("<uuid />"); sink.put(post);
        }
        else
        {
            sink.put(pre); sink.put("<uuid>");
            sink.putUUID(data.asUUID());
            sink.put("</uuid>"); sink.put(post);
        }
        break;

    case LLSD::TypeString:
        if(data.asStringRef().empty())
        {
            sink.put(pre); sink.put("<string />"); sink.put(post);
        }
        else
        {
            sink.put(pre); sink.put("<string>");
            escapeStringTo(sink.buffer(), data.asStringRef());
            sink.checkFlush();
            sink.put("</string>"); sink.put(post);
        }
        break;

    case LLSD::TypeDate:
        sink.put(pre); sink.put("<date>");
        sink.put(data.asDate().asString());
        sink.put("</date>"); sink.put(post);
        break;

    case LLSD::TypeURI:
        sink.put(pre); sink.put("<uri>");
        escapeStringTo(sink.buffer(), data.asString());
        sink.checkFlush();
        sink.put("</uri>"); sink.put(post);
        break;

    case LLSD::TypeBinary:
    {
        const LLSD::Binary& buffer = data.asBinary();
        if(buffer.empty())
        {
            sink.put(pre); sink.put("<binary />"); sink.put(post);
        }
        else
        {
            sink.put(pre); sink.put("<binary encoding=\"base64\">");
            std::string& out = sink.buffer();
            const size_t at = out.size();
            out.resize(at + simdutf::base64_length_from_binary(buffer.size()));
            simdutf::binary_to_base64((const char*)buffer.data(), buffer.size(), out.data() + at);
            sink.checkFlush();
            sink.put("</binary>"); sink.put(post);
        }
        break;
    }
    default:
        // *NOTE: This should never happen.
        sink.put(pre); sink.put("<undef />"); sink.put(post);
        break;
    }
    return format_count;
}

// static
std::string LLSDXMLFormatter::escapeString(const std::string& in)
{
    std::string out;
    escapeStringTo(out, in);
    return out;
}

// static
void LLSDXMLFormatter::escapeStringTo(std::string& out, const std::string& in)
{
    // Append unescaped runs in bulk; only the five XML special characters
    // need an entity.
    out.reserve(out.size() + in.size());
    const char* start = in.data();
    const char* end = start + in.size();
    const char* run = start;
    for(const char* p = start; p < end; ++p)
    {
        const char* entity;
        switch(*p)
        {
        case '<':
            entity = "&lt;";
            break;
        case '>':
            entity = "&gt;";
            break;
        case '&':
            entity = "&amp;";
            break;
        case '\'':
            entity = "&apos;";
            break;
        case '"':
            entity = "&quot;";
            break;
        default:
            continue;
        }
        out.append(run, p - run);
        out.append(entity);
        run = p + 1;
    }
    out.append(run, end - run);
}



class LLSDXMLParser::Impl
{
public:
    Impl(bool emit_errors);
    ~Impl() = default;

    S32 parse(std::istream& input, LLSD& data);

    void parsePart(const char* buf, llssize len);

    void reset();

private:
    enum Element {
        ELEMENT_LLSD,
        ELEMENT_UNDEF,
        ELEMENT_BOOL,
        ELEMENT_INTEGER,
        ELEMENT_REAL,
        ELEMENT_STRING,
        ELEMENT_UUID,
        ELEMENT_DATE,
        ELEMENT_URI,
        ELEMENT_BINARY,
        ELEMENT_MAP,
        ELEMENT_ARRAY,
        ELEMENT_KEY,
        ELEMENT_UNKNOWN
    };
    static Element readElement(const char* name, size_t len);

    S32 run(std::istream& input, LLSD& data);

    /// @name Source
    /// mBuffer only ever grows, so offsets into it stay valid for the whole
    /// parse; raw pointers and string_views do not survive a refill().
    //@{
    bool    refill();
    bool    avail(size_t bytes);
    bool    findMarker(const char* marker, size_t marker_len, size_t& found);
    void    rewindStream();
    //@}

    /// @name Scanning
    //@{
    bool    scanText();
    bool    scanMarkup();
    bool    scanName(size_t& name_off, size_t& name_len);
    bool    scanTagTail(size_t& attr_off, size_t& attr_len, bool& self_closing);
    bool    scanDoctype();
    bool    decodeEntity();
    static bool base64Encoded(const char* attrs, size_t len);
    //@}

    /// @name Content of the element currently open
    //@{
    void            appendContent(size_t off, size_t len);
    void            appendDecoded(const char* p, size_t len);
    void            materializeContent();
    void            clearContent();
    std::string_view contentView() const;
    std::string     takeContent();
    //@}

    void    startElement(Element element, size_t attr_off, size_t attr_len);
    void    endElement(Element element);
    void    startSkipping();

    bool mEmitErrors;

    std::istream*   mInput{ nullptr };
    std::string     mBuffer;
    size_t          mPos{ 0 };
    llssize         mStreamRead{ 0 };   // bytes of mBuffer that came from mInput
    bool            mCanSeek{ false };
    bool            mAtEOF{ false };

    // One entry per element still open, innermost last. The name offset lets an
    // end tag be matched without a second name lookup.
    struct OpenElement
    {
        Element  element;
        U32      name_off;
        U32      name_len;
    };
    std::vector<OpenElement> mOpen;

    LLSD mResult;
    S32 mParseCount;

    bool mInLLSDElement;            // true if we're on LLSD
    bool mSawLLSDElement;           // true if we ever entered an <llsd> element
    bool mGracefullStop;            // true if we found the </llsd

    typedef std::deque<LLSD*> LLSDRefStack;
    LLSDRefStack mStack;

    int mDepth;
    bool mSkipping;
    int mSkipThrough;

    std::string mCurrentKey;        // Current XML <tag>

    // String data between <tag> and </tag>. While it is a single unbroken run
    // of the source it is held as an offset into mBuffer and never copied;
    // entities, CDATA and runs split across a refill force it into mContent.
    size_t      mContentOff{ 0 };
    size_t      mContentLen{ 0 };
    bool        mContentInBuffer{ false };
    bool        mHasContent{ false };
    std::string mContent;
};


LLSDXMLParser::Impl::Impl(bool emit_errors)
    : mEmitErrors(emit_errors)
{
    reset();
}

inline bool is_eol(char c)
{
    return (c == '\n' || c == '\r');
}

void clear_eol(std::istream& input)
{
    char c = input.peek();
    while (input.good() && is_eol(c))
    {
        input.get(c);
        c = input.peek();
    }
}

static unsigned get_till_eol(std::istream& input, char *buf, unsigned bufsize)
{
    // Read via the streambuf: istream::get() pays a sentry per character,
    // and at EOF it used to store a bogus (char)EOF byte in the buffer.
    unsigned count = 0;
    std::streambuf* sb = input.rdbuf();
    while (count < bufsize)
    {
        int c = sb->sbumpc();
        if (c == std::istream::traits_type::eof())
        {
            input.setstate(std::ios::eofbit | std::ios::failbit);
            break;
        }
        buf[count++] = (char)c;
        if (is_eol((char)c))
            break;
    }
    return count;
}

static bool is_xml_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// XML forbids NUL and the other C0 controls apart from tab, LF and CR. They are
// rare enough to be worth a word-at-a-time filter: the arithmetic flags any
// byte below 0x20 and only those words are examined individually.
static bool has_invalid_xml_char(const char* p, size_t n)
{
    auto forbidden = [](unsigned char c)
    {
        return c < 0x20 && c != '\t' && c != '\n' && c != '\r';
    };

    size_t i = 0;
    for (; i + sizeof(U64) <= n; i += sizeof(U64))
    {
        U64 word;
        memcpy(&word, p + i, sizeof(word));
        const U64 below_0x20 = (word - 0x2020202020202020ULL) & ~word & 0x8080808080808080ULL;
        if (below_0x20)
        {
            for (size_t k = i; k < i + sizeof(U64); ++k)
            {
                if (forbidden((unsigned char)p[k])) return true;
            }
        }
    }
    for (; i < n; ++i)
    {
        if (forbidden((unsigned char)p[i])) return true;
    }
    return false;
}

// Appends the UTF-8 encoding of a code point, as a numeric character reference
// resolves to.
static void append_utf8(std::string& out, U32 cp)
{
    if (cp < 0x80)
    {
        out.push_back((char)cp);
    }
    else if (cp < 0x800)
    {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// Resolves the text between '&' and ';'. There is no DTD, so only the five
// predefined entities and character references can appear.
static bool resolve_entity(const char* p, size_t len, std::string& out)
{
    if (len == 0 || len > 10)
    {
        return false;
    }

    if (*p == '#')
    {
        U32 cp = 0;
        if (len >= 3 && (p[1] == 'x' || p[1] == 'X'))
        {
            for (size_t i = 2; i < len; ++i)
            {
                const char c = p[i];
                U32 d;
                if      (c >= '0' && c <= '9') d = (U32)(c - '0');
                else if (c >= 'a' && c <= 'f') d = (U32)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') d = (U32)(c - 'A' + 10);
                else return false;
                cp = cp * 16 + d;
            }
        }
        else if (len >= 2)
        {
            for (size_t i = 1; i < len; ++i)
            {
                if (p[i] < '0' || p[i] > '9') return false;
                cp = cp * 10 + (U32)(p[i] - '0');
            }
        }
        else
        {
            return false;
        }
        if (cp == 0 || cp > 0x10FFFF)
        {
            return false;
        }
        append_utf8(out, cp);
        return true;
    }

    if      (len == 2 && memcmp(p, "lt", 2) == 0)   out.push_back('<');
    else if (len == 2 && memcmp(p, "gt", 2) == 0)   out.push_back('>');
    else if (len == 3 && memcmp(p, "amp", 3) == 0)  out.push_back('&');
    else if (len == 4 && memcmp(p, "quot", 4) == 0) out.push_back('"');
    else if (len == 4 && memcmp(p, "apos", 4) == 0) out.push_back('\'');
    else return false;

    return true;
}

// XML Name characters. Bytes at or above 0x80 are taken on trust: the document
// is validated as UTF-8 as a whole, and the name ranges above ASCII are too
// broad to be worth a per-character table here.
static bool is_name_start_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || c == '_' || c == ':' || c >= 0x80;
}

static bool is_name_char(unsigned char c)
{
    return is_name_start_char(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

// Attributes are name="value" pairs separated by whitespace. A value may not
// contain '<', and an '&' in one must begin a reference.
static bool valid_attributes(const char* p, size_t len)
{
    const char* const end = p + len;
    std::string scratch;
    while (p < end)
    {
        while (p < end && is_xml_space(*p)) ++p;
        if (p >= end) return true;

        if (!is_name_start_char((unsigned char)*p)) return false;
        ++p;
        while (p < end && is_name_char((unsigned char)*p)) ++p;

        while (p < end && is_xml_space(*p)) ++p;
        if (p >= end || *p != '=') return false;
        ++p;
        while (p < end && is_xml_space(*p)) ++p;
        if (p >= end || (*p != '"' && *p != '\'')) return false;

        const char quote = *p++;
        const char* const value = p;
        while (p < end && *p != quote) ++p;
        if (p >= end) return false;

        for (const char* q = value; q < p; ++q)
        {
            if (*q == '<') return false;
            if (*q == '&')
            {
                const char* semi = (const char*)memchr(q, ';', (size_t)(p - q));
                if (!semi) return false;
                scratch.clear();
                if (!resolve_entity(q + 1, (size_t)(semi - q - 1), scratch)) return false;
                q = semi;
            }
        }

        ++p;
        if (p < end && !is_xml_space(*p)) return false;
    }
    return true;
}


void LLSDXMLParser::Impl::reset()
{
    mResult.clear();
    mParseCount = 0;

    mInLLSDElement = false;
    mSawLLSDElement = false;
    mDepth = 0;

    mGracefullStop = false;

    mStack.clear();
    mOpen.clear();

    mSkipping = false;
    mSkipThrough = 0;

    mCurrentKey.clear();
    clearContent();

    mInput = nullptr;
    mBuffer.clear();
    mPos = 0;
    mStreamRead = 0;
    mCanSeek = false;
    mAtEOF = false;
}


void LLSDXMLParser::Impl::startSkipping()
{
    mSkipping = true;
    mSkipThrough = mDepth;
}


// ---------------------------------------------------------------- content
void LLSDXMLParser::Impl::clearContent()
{
    mHasContent = false;
    mContentInBuffer = false;
    mContentOff = 0;
    mContentLen = 0;
    mContent.clear();
}

void LLSDXMLParser::Impl::materializeContent()
{
    if (mContentInBuffer)
    {
        mContent.assign(mBuffer, mContentOff, mContentLen);
        mContentInBuffer = false;
    }
    mHasContent = true;
}

void LLSDXMLParser::Impl::appendContent(size_t off, size_t len)
{
    // content inside skipped elements is discarded anyway; don't buffer it
    if (!len || mSkipping)
    {
        return;
    }
    if (!mHasContent)
    {
        mContentOff = off;
        mContentLen = len;
        mContentInBuffer = true;
        mHasContent = true;
        return;
    }
    if (mContentInBuffer)
    {
        if (off == mContentOff + mContentLen)
        {   // still one unbroken run of the source
            mContentLen += len;
            return;
        }
        materializeContent();
    }
    mContent.append(mBuffer, off, len);
}

void LLSDXMLParser::Impl::appendDecoded(const char* p, size_t len)
{
    if (mSkipping)
    {
        return;
    }
    materializeContent();
    mContent.append(p, len);
}

std::string_view LLSDXMLParser::Impl::contentView() const
{
    if (mContentInBuffer)
    {
        return std::string_view(mBuffer.data() + mContentOff, mContentLen);
    }
    return mContent;
}

std::string LLSDXMLParser::Impl::takeContent()
{
    if (mContentInBuffer)
    {
        return std::string(mBuffer, mContentOff, mContentLen);
    }
    return std::move(mContent);
}


// ----------------------------------------------------------------- source
bool LLSDXMLParser::Impl::refill()
{
    if (!mInput || mAtEOF)
    {
        return false;
    }

    // Growing mBuffer reallocates, so no view into it may survive this.
    if (mContentInBuffer)
    {
        materializeContent();
    }

    if (mCanSeek)
    {
        // Overshoot is fine: whatever the document does not consume is handed
        // back to the caller by rewindStream().
        static const std::streamsize BLOCK = 64 * 1024;
        const size_t old_size = mBuffer.size();
        mBuffer.resize(old_size + (size_t)BLOCK);
        mInput->read(&mBuffer[old_size], BLOCK);
        const std::streamsize got = mInput->gcount();
        mBuffer.resize(old_size + (size_t)got);
        if (got <= 0)
        {
            mAtEOF = true;
            return false;
        }
        mStreamRead += got;
        return true;
    }

    // A stream we cannot seek must not be over-read, so it is pulled a line at
    // a time and the caller is left at a line boundary, as it always was.
    char line[1024];
    const unsigned count = get_till_eol(*mInput, line, sizeof(line));
    if (!count)
    {
        mAtEOF = true;
        return false;
    }
    mBuffer.append(line, count);
    mStreamRead += count;
    return true;
}

bool LLSDXMLParser::Impl::avail(size_t bytes)
{
    while (mBuffer.size() - mPos < bytes)
    {
        if (!refill())
        {
            return false;
        }
    }
    return true;
}

bool LLSDXMLParser::Impl::findMarker(const char* marker, size_t marker_len, size_t& found)
{
    size_t from = mPos;
    for (;;)
    {
        const size_t at = mBuffer.find(marker, from, marker_len);
        if (at != std::string::npos)
        {
            found = at;
            return true;
        }
        // A marker may straddle the end of what has been read so far.
        from = (mBuffer.size() >= marker_len) ? mBuffer.size() - (marker_len - 1) : mPos;
        if (from < mPos)
        {
            from = mPos;
        }
        if (!refill())
        {
            return false;
        }
    }
}

void LLSDXMLParser::Impl::rewindStream()
{
    if (!mInput || !mCanSeek)
    {
        return;
    }
    // Bytes pulled from the stream that the document did not consume. Anything
    // handed to parsePart() never came from the stream, hence the clamp.
    llssize unconsumed = (llssize)(mBuffer.size() - mPos);
    if (unconsumed > mStreamRead)
    {
        unconsumed = mStreamRead;
    }
    if (unconsumed > 0)
    {
        mInput->clear();
        mInput->seekg(-(std::streamoff)unconsumed, std::ios_base::cur);
    }
}


// ---------------------------------------------------------------- scanning
// LLSD XML is a closed grammar: thirteen element names, no namespaces, no DTD.
// Length and first character identify every one of them uniquely, so the whole
// dispatch is a switch pair plus one memcmp of a length known at that point.
LLSDXMLParser::Impl::Element LLSDXMLParser::Impl::readElement(const char* name, size_t len)
{
    switch (len)
    {
        case 3:
            switch (name[0])
            {
                case 'k': if (memcmp(name, "key", 3) == 0) return ELEMENT_KEY;    break;
                case 'm': if (memcmp(name, "map", 3) == 0) return ELEMENT_MAP;    break;
                case 'u': if (memcmp(name, "uri", 3) == 0) return ELEMENT_URI;    break;
            }
            break;
        case 4:
            switch (name[0])
            {
                case 'r': if (memcmp(name, "real", 4) == 0) return ELEMENT_REAL;  break;
                case 'l': if (memcmp(name, "llsd", 4) == 0) return ELEMENT_LLSD;  break;
                case 'u': if (memcmp(name, "uuid", 4) == 0) return ELEMENT_UUID;  break;
                case 'd': if (memcmp(name, "date", 4) == 0) return ELEMENT_DATE;  break;
            }
            break;
        case 5:
            switch (name[0])
            {
                case 'a': if (memcmp(name, "array", 5) == 0) return ELEMENT_ARRAY; break;
                case 'u': if (memcmp(name, "undef", 5) == 0) return ELEMENT_UNDEF; break;
            }
            break;
        case 6:
            switch (name[0])
            {
                case 'b': if (memcmp(name, "binary", 6) == 0) return ELEMENT_BINARY; break;
                case 's': if (memcmp(name, "string", 6) == 0) return ELEMENT_STRING; break;
            }
            break;
        case 7:
            switch (name[0])
            {
                case 'i': if (memcmp(name, "integer", 7) == 0) return ELEMENT_INTEGER; break;
                case 'b': if (memcmp(name, "boolean", 7) == 0) return ELEMENT_BOOL;    break;
            }
            break;
    }
    return ELEMENT_UNKNOWN;
}

// static
bool LLSDXMLParser::Impl::base64Encoded(const char* attrs, size_t len)
{
    // <binary> takes one attribute. Anything other than base64 is unreadable,
    // and an absent encoding means base64 by default.
    const char* const end = attrs + len;
    for (const char* p = attrs; p < end; )
    {
        while (p < end && is_xml_space(*p)) ++p;
        const char* name = p;
        while (p < end && *p != '=' && !is_xml_space(*p)) ++p;
        const size_t name_len = (size_t)(p - name);
        while (p < end && is_xml_space(*p)) ++p;
        if (p >= end || *p != '=') break;
        ++p;
        while (p < end && is_xml_space(*p)) ++p;
        if (p >= end) break;
        const char quote = *p++;
        const char* value = p;
        while (p < end && *p != quote) ++p;
        const size_t value_len = (size_t)(p - value);
        if (p < end) ++p;

        if (name_len == 8 && memcmp(name, "encoding", 8) == 0)
        {
            return value_len == 6 && memcmp(value, "base64", 6) == 0;
        }
    }
    return true;
}

bool LLSDXMLParser::Impl::decodeEntity()
{
    // mPos is on '&'.
    size_t semi;
    if (!findMarker(";", 1, semi))
    {
        return false;
    }
    const size_t start = mPos + 1;

    std::string decoded;
    if (!resolve_entity(mBuffer.data() + start, semi - start, decoded))
    {
        return false;
    }

    appendDecoded(decoded.data(), decoded.size());
    mPos = semi + 1;
    return true;
}

// Consumes character data up to the next '<', which is left at mPos.
bool LLSDXMLParser::Impl::scanText()
{
    for (;;)
    {
        // decodeEntity() can refill, so the extent of the buffer is re-read on
        // every pass rather than hoisted.
        while (mPos < mBuffer.size())
        {
            const char* const base = mBuffer.data();
            const size_t limit = mBuffer.size();

            const void* hit = memchr(base + mPos, '<', limit - mPos);
            const size_t stop = hit ? (size_t)((const char*)hit - base) : limit;

            const void* amp = memchr(base + mPos, '&', stop - mPos);
            if (amp)
            {
                const size_t at = (size_t)((const char*)amp - base);
                appendContent(mPos, at - mPos);
                mPos = at;
                if (!decodeEntity())
                {
                    return false;
                }
                continue;
            }

            appendContent(mPos, stop - mPos);
            mPos = stop;
            if (hit)
            {
                return true;
            }
            break;
        }
        if (!refill())
        {
            return false;
        }
    }
}

bool LLSDXMLParser::Impl::scanName(size_t& name_off, size_t& name_len)
{
    name_off = mPos;
    for (;;)
    {
        while (mPos < mBuffer.size())
        {
            const unsigned char c = (unsigned char)mBuffer[mPos];
            if (is_xml_space((char)c) || c == '>' || c == '/')
            {
                name_len = mPos - name_off;
                return name_len > 0;
            }
            const bool ok = (mPos == name_off) ? is_name_start_char(c) : is_name_char(c);
            if (!ok)
            {
                return false;
            }
            ++mPos;
        }
        if (!refill())
        {
            return false;
        }
    }
}

bool LLSDXMLParser::Impl::scanTagTail(size_t& attr_off, size_t& attr_len, bool& self_closing)
{
    attr_off = mPos;
    self_closing = false;
    char quote = 0;
    for (;;)
    {
        while (mPos < mBuffer.size())
        {
            const char c = mBuffer[mPos];
            if (quote)
            {
                if (c == quote) quote = 0;
            }
            else if (c == '"' || c == '\'')
            {
                quote = c;
            }
            else if (c == '>')
            {
                size_t end = mPos;
                while (end > attr_off && is_xml_space(mBuffer[end - 1])) --end;
                if (end > attr_off && mBuffer[end - 1] == '/')
                {
                    self_closing = true;
                    --end;
                }
                attr_len = end - attr_off;
                ++mPos;
                return true;
            }
            ++mPos;
        }
        if (!refill())
        {
            return false;
        }
    }
}

bool LLSDXMLParser::Impl::scanDoctype()
{
    // mPos is just past "<!". The internal subset may contain '>', so track it.
    int in_subset = 0;
    for (;;)
    {
        while (mPos < mBuffer.size())
        {
            const char c = mBuffer[mPos++];
            if (c == '[') ++in_subset;
            else if (c == ']') { if (in_subset) --in_subset; }
            else if (c == '>' && !in_subset) return true;
        }
        if (!refill())
        {
            return false;
        }
    }
}

bool LLSDXMLParser::Impl::scanMarkup()
{
    // mPos is on '<'.
    if (!avail(2))
    {
        return false;
    }

    const char lead = mBuffer[mPos + 1];

    if (lead == '?')
    {
        size_t at;
        mPos += 2;
        if (!findMarker("?>", 2, at)) return false;
        mPos = at + 2;
        return true;
    }

    if (lead == '!')
    {
        if (avail(4) && memcmp(mBuffer.data() + mPos, "<!--", 4) == 0)
        {
            size_t at;
            mPos += 4;
            if (!findMarker("-->", 3, at)) return false;
            mPos = at + 3;
            return true;
        }
        if (avail(9) && memcmp(mBuffer.data() + mPos, "<![CDATA[", 9) == 0)
        {
            size_t at;
            mPos += 9;
            if (!findMarker("]]>", 3, at)) return false;
            appendContent(mPos, at - mPos);
            mPos = at + 3;
            return true;
        }
        mPos += 2;
        return scanDoctype();
    }

    if (lead == '/')
    {
        mPos += 2;
        size_t name_off, name_len;
        if (!scanName(name_off, name_len)) return false;

        if (mOpen.empty())
        {
            return false;
        }
        const OpenElement open = mOpen.back();
        if (open.name_len != name_len
            || memcmp(mBuffer.data() + open.name_off, mBuffer.data() + name_off, name_len) != 0)
        {   // mismatched tags are not well formed
            return false;
        }
        mOpen.pop_back();

        // Whatever follows the name must be optional space then '>'.
        for (;;)
        {
            while (mPos < mBuffer.size())
            {
                const char c = mBuffer[mPos];
                if (c == '>') { ++mPos; endElement(open.element); return true; }
                if (!is_xml_space(c)) return false;
                ++mPos;
            }
            if (!refill()) return false;
        }
    }

    ++mPos;
    size_t name_off, name_len;
    if (!scanName(name_off, name_len)) return false;

    size_t attr_off, attr_len;
    bool self_closing = false;
    if (!scanTagTail(attr_off, attr_len, self_closing)) return false;
    if (!valid_attributes(mBuffer.data() + attr_off, attr_len)) return false;

    const Element element = readElement(mBuffer.data() + name_off, name_len);

    mOpen.push_back(OpenElement{ element, (U32)name_off, (U32)name_len });
    startElement(element, attr_off, attr_len);

    if (self_closing)
    {
        mOpen.pop_back();
        endElement(element);
    }
    return true;
}


// ----------------------------------------------------------------- events
void LLSDXMLParser::Impl::startElement(Element element, size_t attr_off, size_t attr_len)
{
    ++mDepth;
    if (mSkipping)
    {
        return;
    }

    clearContent();

    switch (element)
    {
        case ELEMENT_LLSD:
            if (mInLLSDElement) { return startSkipping(); }
            mInLLSDElement = true;
            mSawLLSDElement = true;
            return;

        case ELEMENT_KEY:
            if (mStack.empty()  ||  !(mStack.back()->isMap()))
            {
                return startSkipping();
            }
            return;

        case ELEMENT_BINARY:
        {
            if (!base64Encoded(mBuffer.data() + attr_off, attr_len)) { return startSkipping(); }
            break;
        }

        default:
            // all rest are values, fall through
            ;
    }


    if (!mInLLSDElement) { return startSkipping(); }

    if (mStack.empty())
    {
        mStack.push_back(&mResult);
    }
    else if (mStack.back()->isMap())
    {
        if (mCurrentKey.empty()) { return startSkipping(); }

        LLSD& map = *mStack.back();
        LLSD& newElement = map[std::move(mCurrentKey)];
        mStack.push_back(&newElement);

        mCurrentKey.clear();
    }
    else if (mStack.back()->isArray())
    {
        LLSD& array = *mStack.back();
        array.append(LLSD());
        LLSD& newElement = array[array.size()-1];
        mStack.push_back(&newElement);
    }
    else {
        // improperly nested value in a non-structure
        return startSkipping();
    }

    ++mParseCount;
    switch (element)
    {
        case ELEMENT_MAP:
            *mStack.back() = LLSD::emptyMap();
            break;

        case ELEMENT_ARRAY:
            *mStack.back() = LLSD::emptyArray();
            break;

        default:
            // all the other values will be set in the end element handler
            ;
    }
}

void LLSDXMLParser::Impl::endElement(Element element)
{
    --mDepth;
    if (mSkipping)
    {
        if (mDepth < mSkipThrough)
        {
            mSkipping = false;
        }
        return;
    }

    switch (element)
    {
        case ELEMENT_LLSD:
            if (mInLLSDElement)
            {
                mInLLSDElement = false;
                mGracefullStop = true;
            }
            return;

        case ELEMENT_KEY:
            mCurrentKey = takeContent();
            clearContent();
            return;

        default:
            // all rest are values, fall through
            ;
    }

    if (!mInLLSDElement) { return; }

    LLSD& value = *mStack.back();
    mStack.pop_back();

    switch (element)
    {
        case ELEMENT_UNDEF:
            value.clear();
            break;

        case ELEMENT_BOOL:
        {
            const std::string_view content = contentView();
            value = (content == "true" || content == "1");
            break;
        }

        case ELEMENT_INTEGER:
            {
                const std::string_view content = contentView();
                // Leading whitespace and a leading '+' are skipped and trailing
                // text is ignored, so "  42  " reads as 42.
                const char* first = content.data();
                const char* const last = first + content.size();
                while (first < last && (*first == ' ' || (*first >= '\t' && *first <= '\r'))) ++first;
                if (first < last && *first == '+') ++first;
                S32 i;
                auto [ptr, ec] = std::from_chars(first, last, i);
                if (ec == std::errc())
                {   // the common case: a plain decimal integer
                    value = i;
                }
                else
                {
                    // This must treat "1.23" not as an error, but as a number, which is
                    // then truncated down to an integer.  Hence, this code doesn't call
                    // std::istringstream::operator>>(int&), which would not consume the
                    // ".23" portion.

                    // Utilizes implementation used internally by LLSD::ImplString::asInteger
                    value = (int)llsd::string_to_real(content);
                }
            }
            break;

        case ELEMENT_REAL:
            {
                // Utilizes implementation used internally by LLSD::ImplString::asReal
                value = llsd::string_to_real(contentView());
            }
            break;

        case ELEMENT_STRING:
            value = takeContent();
            break;

        case ELEMENT_UUID:
            value = LLUUID(std::string(contentView()));
            break;

        case ELEMENT_DATE:
            value = LLDate(std::string(contentView()));
            break;

        case ELEMENT_URI:
            value = LLURI(std::string(contentView()));
            break;

        case ELEMENT_BINARY:
        {
            // simdutf's forgiving-base64 decoder skips ASCII whitespace
            // natively (DEV-39358: python and other non-linden systems emit
            // line-wrapped base64), and binary_length_from_base64 computes
            // the exact decoded size even with whitespace present.
            // binary_length_from_base64 returns 0 for empty or
            // whitespace-only content, so <binary /> yields an empty
            // LLSD::Binary rather than leaving the value undefined.
            const std::string_view content = contentView();
            std::vector<U8> data(simdutf::binary_length_from_base64(content.data(), content.size()));
            // convert to binary and check for errors
            simdutf::result r = simdutf::base64_to_binary(content.data(), content.size(), (char*)data.data());
            if(r.error == simdutf::error_code::SUCCESS)
            {
                value = std::move(data);
            }
            break;
        }

        case ELEMENT_UNKNOWN:
            value.clear();
            break;

        default:
            // other values, map and array, have already been set
            break;
    }

    clearContent();
}


/*
    This code is time critical

    This is a sample of tag occurances of text in simstate file with ~8000 objects.
    A tag pair (<key>something</key>) counts is counted as two:

        key     - 2680178
        real    - 1818362
        integer -  906078
        string  -   53482
        undef   -   40353
        boolean -   33874
        llsd    -   16332
        uri     -      38
        date    -       1
*/
S32 LLSDXMLParser::Impl::run(std::istream& input, LLSD& data)
{
    mInput = &input;

    // A stream that can seek is read in blocks and rewound to the end of the
    // document afterwards; one that cannot is read a line at a time so that it
    // is never advanced past the document in the first place.
    std::streambuf* sb = input.rdbuf();
    mCanSeek = sb
        && sb->pubseekoff(0, std::ios_base::cur, std::ios_base::in)
           != std::streambuf::pos_type(std::streambuf::off_type(-1));

    while (!mGracefullStop)
    {
        if (!scanText())
        {
            break;
        }
        if (!scanMarkup())
        {
            break;
        }
    }

    // The document must be well-formed UTF-8 made of characters XML allows.
    // Checking the consumed span once is cheaper than testing each run, and
    // nothing has been handed to the caller yet.
    const bool valid_chars = mGracefullStop
        && !has_invalid_xml_char(mBuffer.data(), mPos)
        && simdutf::validate_utf8(mBuffer.data(), mPos);

    rewindStream();
    mInput = nullptr;
    clear_eol(input);

    if (!valid_chars || !mGracefullStop || !mSawLLSDElement)
    {
        if (mEmitErrors)
        {
            LL_INFOS() << "LLSDXMLParser: parse failure" << LL_ENDL;
        }
        data = LLSD();
        return LLSDParser::PARSE_FAILURE;
    }

    data = mResult;
    return mParseCount;
}

S32 LLSDXMLParser::Impl::parse(std::istream& input, LLSD& data)
{
    return run(input, data);
}


void LLSDXMLParser::Impl::parsePart(const char* buf, llssize len)
{
    if (buf != NULL && len > 0)
    {
        mBuffer.append(buf, (size_t)len);
    }
}





/**
 * LLSDXMLParser
 */
LLSDXMLParser::LLSDXMLParser(bool emit_errors /* = true */) : impl(* new Impl(emit_errors))
{
}

LLSDXMLParser::~LLSDXMLParser()
{
    delete &impl;
}

void LLSDXMLParser::parsePart(const char *buf, llssize len)
{
    impl.parsePart(buf, len);
}

// virtual
S32 LLSDXMLParser::doParse(std::istream& input, LLSD& data, S32 max_depth) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;

    return impl.parse(input, data);
}

//  virtual
void LLSDXMLParser::doReset()
{
    impl.reset();
}

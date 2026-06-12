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
#include <iostream>
#include <deque>

#include <simdutf.h>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

extern "C"
{
# include <expat.h>
}

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
    std::string post;
    if (options & LLSDFormatter::OPTIONS_PRETTY)
    {
        post = "\n";
    }
    ostr << "<llsd>" << post;
    S32 rv = format_impl(data, ostr, options, 1);
    ostr << "</llsd>\n";

    return rv;
}

S32 LLSDXMLFormatter::format_impl(const LLSD& data, std::ostream& ostr,
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
            ostr << pre << "<map />" << post;
        }
        else
        {
            ostr << pre << "<map>" << post;
            LLSD::map_const_iterator iter = data.beginMap();
            LLSD::map_const_iterator end = data.endMap();
            for(; iter != end; ++iter)
            {
                ostr << pre << "<key>" << escapeString((*iter).first) << "</key>" << post;
                format_count += format_impl((*iter).second, ostr, options, level + 1);
            }
            ostr << pre <<  "</map>" << post;
        }
        break;

    case LLSD::TypeArray:
        if(0 == data.size())
        {
            ostr << pre << "<array />" << post;
        }
        else
        {
            ostr << pre << "<array>" << post;
            LLSD::array_const_iterator iter = data.beginArray();
            LLSD::array_const_iterator end = data.endArray();
            for(; iter != end; ++iter)
            {
                format_count += format_impl(*iter, ostr, options, level + 1);
            }
            ostr << pre << "</array>" << post;
        }
        break;

    case LLSD::TypeUndefined:
        ostr << pre << "<undef />" << post;
        break;

    case LLSD::TypeBoolean:
        ostr << pre << "<boolean>";
        if(mBoolAlpha ||
           (ostr.flags() & std::ios::boolalpha)
           )
        {
            ostr << (data.asBoolean() ? "true" : "false");
        }
        else
        {
            ostr << (data.asBoolean() ? 1 : 0);
        }
        ostr << "</boolean>" << post;
        break;

    case LLSD::TypeInteger:
        ostr << pre << "<integer>" << data.asInteger() << "</integer>" << post;
        break;

    case LLSD::TypeReal:
    {
        ostr << pre << "<real>";
        if(mRealFormat.empty())
        {
            // shortest representation that round-trips to the same double
            char buf[32];
            char* end = std::to_chars(buf, buf + sizeof(buf), data.asReal()).ptr;
            ostr.write(buf, end - buf);
        }
        else
        {
            formatReal(data.asReal(), ostr);
        }
        ostr << "</real>" << post;
        break;
    }

    case LLSD::TypeUUID:
        if(data.asUUID().isNull()) ostr << pre << "<uuid />" << post;
        else ostr << pre << "<uuid>" << data.asUUID() << "</uuid>" << post;
        break;

    case LLSD::TypeString:
        if(data.asStringRef().empty()) ostr << pre << "<string />" << post;
        else ostr << pre << "<string>" << escapeString(data.asStringRef()) <<"</string>" << post;
        break;

    case LLSD::TypeDate:
        ostr << pre << "<date>" << data.asDate() << "</date>" << post;
        break;

    case LLSD::TypeURI:
        ostr << pre << "<uri>" << escapeString(data.asString()) << "</uri>" << post;
        break;

    case LLSD::TypeBinary:
    {
        const LLSD::Binary& buffer = data.asBinary();
        if(buffer.empty())
        {
            ostr << pre << "<binary />" << post;
        }
        else
        {
            // *FIX: memory inefficient.
            // *TODO: convert to use LLBase64
            ostr << pre << "<binary encoding=\"base64\">";
            std::string output;
            output.resize(simdutf::base64_length_from_binary(buffer.size()));
            simdutf::binary_to_base64((const char*)buffer.data(), buffer.size(), output.data());
            ostr.write(output.data(), output.size());
            ostr << "</binary>" << post;
        }
        break;
    }
    default:
        // *NOTE: This should never happen.
        ostr << pre << "<undef />" << post;
        break;
    }
    return format_count;
}

// static
std::string LLSDXMLFormatter::escapeString(const std::string& in)
{
    // Append unescaped runs in bulk; only the five XML special characters
    // need an entity.
    std::string out;
    out.reserve(in.size());
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
    return out;
}



class LLSDXMLParser::Impl
{
public:
    Impl(bool emit_errors);
    ~Impl();

    S32 parse(std::istream& input, LLSD& data);
    S32 parseLines(std::istream& input, LLSD& data);

    void parsePart(const char *buf, llssize len);

    void reset();

private:
    void startElementHandler(const XML_Char* name, const XML_Char** attributes);
    void endElementHandler(const XML_Char* name);
    void characterDataHandler(const XML_Char* data, int length);

    static void sStartElementHandler(
        void* userData, const XML_Char* name, const XML_Char** attributes);
    static void sEndElementHandler(
        void* userData, const XML_Char* name);
    static void sCharacterDataHandler(
        void* userData, const XML_Char* data, int length);

    void startSkipping();

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
    static Element readElement(const XML_Char* name);

    static const XML_Char* findAttribute(const XML_Char* name, const XML_Char** pairs);

    bool mEmitErrors;

    XML_Parser  mParser;

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
    std::string mCurrentContent;    // String data between <tag> and </tag>
};


LLSDXMLParser::Impl::Impl(bool emit_errors)
    : mEmitErrors(emit_errors)
{
    mParser = XML_ParserCreate(NULL);
    reset();
}

LLSDXMLParser::Impl::~Impl()
{
    XML_ParserFree(mParser);
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

S32 LLSDXMLParser::Impl::parse(std::istream& input, LLSD& data)
{
    XML_Status status;

    static const int BUFFER_SIZE = 1024;
    void* buffer = NULL;
    int count = 0;
    while (input.good() && !input.eof())
    {
        buffer = XML_GetBuffer(mParser, BUFFER_SIZE);

        /*
         * If we happened to end our last buffer right at the end of the llsd, but the
         * stream is still going we will get a null buffer here.  Check for mGracefullStop.
         */
        if (!buffer)
        {
            break;
        }
        count = get_till_eol(input, (char *)buffer, BUFFER_SIZE);
        if (!count)
        {
            break;
        }
        status = XML_ParseBuffer(mParser, count, false);

        if (status == XML_STATUS_ERROR)
        {
            break;
        }
    }

    // *FIX.: This code is buggy - if the stream was empty or not
    // good, there is not buffer to parse, both the call to
    // XML_ParseBuffer and the buffer manipulations are illegal
    // futhermore, it isn't clear that the expat buffer semantics are
    // preserved

    status = XML_ParseBuffer(mParser, 0, true);
    if (status == XML_STATUS_ERROR && !mGracefullStop)
    {
        if (buffer)
        {
            ((char*) buffer)[count ? count - 1 : 0] = '\0';
            if (mEmitErrors)
            {
                LL_INFOS() << "LLSDXMLParser::Impl::parse: XML_STATUS_ERROR parsing:" << (char*)buffer << LL_ENDL;
            }
        }
        else
        {
            if (mEmitErrors)
            {
                LL_INFOS() << "LLSDXMLParser::Impl::parse: XML_STATUS_ERROR, null buffer" << LL_ENDL;
            }
        }
        data = LLSD();
        return LLSDParser::PARSE_FAILURE;
    }

    clear_eol(input);
    if (!mSawLLSDElement)
    {
        // well-formed XML that never contained an <llsd> element. The old
        // code reported this by accident: reading EOF used to deposit a
        // bogus (char)EOF byte in the parse buffer, forcing an expat error.
        data = LLSD();
        return LLSDParser::PARSE_FAILURE;
    }
    data = mResult;
    return mParseCount;
}


S32 LLSDXMLParser::Impl::parseLines(std::istream& input, LLSD& data)
{
    XML_Status status = XML_STATUS_OK;

    data = LLSD();

    static const int BUFFER_SIZE = 1024;

    //static char last_buffer[ BUFFER_SIZE ];
    //std::streamsize last_num_read;

    // Must get rid of any leading \n, otherwise the stream gets into an error/eof state
    clear_eol(input);

    while( !mGracefullStop
        && input.good()
        && !input.eof())
    {
        void* buffer = XML_GetBuffer(mParser, BUFFER_SIZE);
        /*
         * If we happened to end our last buffer right at the end of the llsd, but the
         * stream is still going we will get a null buffer here.  Check for mGracefullStop.
         * -- I don't think this is actually true - zero 2008-05-09
         */
        if (!buffer)
        {
            break;
        }

        // Get one line
        input.getline((char*)buffer, BUFFER_SIZE);
        std::streamsize num_read = input.gcount();

        //memcpy( last_buffer, buffer, num_read );
        //last_num_read = num_read;

        if ( num_read > 0 )
        {
            if (!input.good() )
            {   // Clear state that's set when we run out of buffer
                input.clear();
            }

            // Re-insert with the \n that was absorbed by getline()
            char * text = (char *) buffer;
            if ( text[num_read - 1] == 0)
            {
                text[num_read - 1] = '\n';
            }
        }

        status = XML_ParseBuffer(mParser, (int)num_read, false);
        if (status == XML_STATUS_ERROR)
        {
            break;
        }
    }

    if (status != XML_STATUS_ERROR
        && !mGracefullStop)
    {   // Parse last bit
        status = XML_ParseBuffer(mParser, 0, true);
    }

    if (status == XML_STATUS_ERROR
        && !mGracefullStop)
    {
        if (mEmitErrors)
        {
        LL_INFOS() << "LLSDXMLParser::Impl::parseLines: XML_STATUS_ERROR" << LL_ENDL;
        }
        return LLSDParser::PARSE_FAILURE;
    }

    clear_eol(input);
    if (!mSawLLSDElement)
    {
        // well-formed XML that never contained an <llsd> element
        return LLSDParser::PARSE_FAILURE;
    }
    data = mResult;
    return mParseCount;
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

    mSkipping = false;

    mCurrentKey.clear();

    XML_ParserReset(mParser, "utf-8");
    XML_SetUserData(mParser, this);
    XML_SetElementHandler(mParser, sStartElementHandler, sEndElementHandler);
    XML_SetCharacterDataHandler(mParser, sCharacterDataHandler);
}


void LLSDXMLParser::Impl::startSkipping()
{
    mSkipping = true;
    mSkipThrough = mDepth;
}

const XML_Char*
LLSDXMLParser::Impl::findAttribute(const XML_Char* name, const XML_Char** pairs)
{
    while (NULL != pairs && NULL != *pairs)
    {
        if(0 == strcmp(name, *pairs))
        {
            return *(pairs + 1);
        }
        pairs += 2;
    }
    return NULL;
}

void LLSDXMLParser::Impl::parsePart(const char* buf, llssize len)
{
    if ( buf != NULL
        && len > 0 )
    {
        XML_Status status = XML_Parse(mParser, buf, (int)len, 0);
        // A short, complete document (e.g. "<llsd><map /></llsd>") may be
        // wholly contained in this first chunk. Reaching </llsd> calls
        // XML_StopParser(false), which makes XML_Parse return XML_STATUS_ERROR
        // even though the parse succeeded -- mGracefullStop distinguishes that
        // graceful stop from a real error, matching parse()/parseLines().
        if (status == XML_STATUS_ERROR && !mGracefullStop)
        {
            if (mEmitErrors)
            {
                LL_INFOS() << "Unexpected XML parsing error at start" << LL_ENDL;
            }
        }
    }
}

// Performance testing code
//#define   XML_PARSER_PERFORMANCE_TESTS

#ifdef XML_PARSER_PERFORMANCE_TESTS

extern U64 totalTime();
U64 readElementTime = 0;
U64 startElementTime = 0;
U64 endElementTime = 0;
U64 charDataTime = 0;
U64 parseTime = 0;

class XML_Timer
{
public:
    XML_Timer( U64 * sum ) : mSum( sum )
    {
        mStart = totalTime();
    }
    ~XML_Timer()
    {
        *mSum += (totalTime() - mStart);
    }

    U64 * mSum;
    U64 mStart;
};
#endif // XML_PARSER_PERFORMANCE_TESTS

void LLSDXMLParser::Impl::startElementHandler(const XML_Char* name, const XML_Char** attributes)
{
    #ifdef XML_PARSER_PERFORMANCE_TESTS
    XML_Timer timer( &startElementTime );
    #endif // XML_PARSER_PERFORMANCE_TESTS

    ++mDepth;
    if (mSkipping)
    {
        return;
    }

    Element element = readElement(name);

    mCurrentContent.clear();

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
            const XML_Char* encoding = findAttribute("encoding", attributes);
            if(encoding && strcmp("base64", encoding) != 0) { return startSkipping(); }
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

void LLSDXMLParser::Impl::endElementHandler(const XML_Char* name)
{
    #ifdef XML_PARSER_PERFORMANCE_TESTS
    XML_Timer timer( &endElementTime );
    #endif // XML_PARSER_PERFORMANCE_TESTS

    --mDepth;
    if (mSkipping)
    {
        if (mDepth < mSkipThrough)
        {
            mSkipping = false;
        }
        return;
    }

    Element element = readElement(name);

    switch (element)
    {
        case ELEMENT_LLSD:
            if (mInLLSDElement)
            {
                mInLLSDElement = false;
                mGracefullStop = true;
                XML_StopParser(mParser, false);
            }
            return;

        case ELEMENT_KEY:
            mCurrentKey = std::move(mCurrentContent); // This is safe to move as we are in the end element handler
            mCurrentContent.clear(); // Ensure mCurrentContent is empty for subsequent use
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
            value = (mCurrentContent == "true" || mCurrentContent == "1");
            break;

        case ELEMENT_INTEGER:
            {
                S32 i;
                // sscanf okay here with different locales - ints don't change for different locale settings like floats do.
                if ( sscanf(mCurrentContent.c_str(), "%d", &i ) == 1 )
                {   // See if sscanf works - it's faster
                    value = i;
                }
                else
                {
                    // This must treat "1.23" not as an error, but as a number, which is
                    // then truncated down to an integer.  Hence, this code doesn't call
                    // std::istringstream::operator>>(int&), which would not consume the
                    // ".23" portion.

                    // Utilizes implementation used internally by LLSD::ImplString::asInteger
                    value = (int)llsd::string_to_real(mCurrentContent);
                }
            }
            break;

        case ELEMENT_REAL:
            {
                // Utilizes implementation used internally by LLSD::ImplString::asReal
                value = llsd::string_to_real(mCurrentContent);

                // removed since this breaks when locale has decimal separator that isn't '.'
                // investigated changing local to something compatible each time but deemed higher
                // risk that just using LLSD.asReal() each time.
                //F64 r;
                //if ( sscanf(mCurrentContent.c_str(), "%lf", &r ) == 1 )
                //{ // See if sscanf works - it's faster
                //  value = r;
                //}
                //else
                //{
                //  value = LLSD(mCurrentContent).asReal();
                //}
            }
            break;

        case ELEMENT_STRING:
            value = std::move(mCurrentContent);  // This is safe to move as we are in the end element handler and this is cleared below
            break;

        case ELEMENT_UUID:
            value = LLUUID(mCurrentContent);
            break;

        case ELEMENT_DATE:
            value = LLDate(mCurrentContent);
            break;

        case ELEMENT_URI:
            value = LLURI(mCurrentContent);
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
            std::vector<U8> data(simdutf::binary_length_from_base64(mCurrentContent.data(), mCurrentContent.size()));
            // convert to binary and check for errors
            simdutf::result r = simdutf::base64_to_binary(mCurrentContent.data(), mCurrentContent.size(), (char*)data.data());
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

    mCurrentContent.clear();
}

void LLSDXMLParser::Impl::characterDataHandler(const XML_Char* data, int length)
{
    #ifdef XML_PARSER_PERFORMANCE_TESTS
    XML_Timer timer( &charDataTime );
    #endif  // XML_PARSER_PERFORMANCE_TESTS

    // content inside skipped elements is discarded anyway; don't buffer it
    if (!mSkipping)
    {
        mCurrentContent.append(data, length);
    }
}


void LLSDXMLParser::Impl::sStartElementHandler(
    void* userData, const XML_Char* name, const XML_Char** attributes)
{
    ((LLSDXMLParser::Impl*)userData)->startElementHandler(name, attributes);
}

void LLSDXMLParser::Impl::sEndElementHandler(
    void* userData, const XML_Char* name)
{
    ((LLSDXMLParser::Impl*)userData)->endElementHandler(name);
}

void LLSDXMLParser::Impl::sCharacterDataHandler(
    void* userData, const XML_Char* data, int length)
{
    ((LLSDXMLParser::Impl*)userData)->characterDataHandler(data, length);
}


/*
    This code is time critical

    This is a sample of tag occurances of text in simstate file with ~8000 objects.
    A tag pair (<key>something</key>) counts is counted as two:

        key     - 2680178
        real    - 1818362
        integer -  906078
        array   -  295682
        map     -  191818
        uuid    -  177903
        binary  -  175748
        string  -   53482
        undef   -   40353
        boolean -   33874
        llsd    -   16332
        uri     -      38
        date    -       1
*/
LLSDXMLParser::Impl::Element LLSDXMLParser::Impl::readElement(const XML_Char* name)
{
    #ifdef XML_PARSER_PERFORMANCE_TESTS
    XML_Timer timer( &readElementTime );
    #endif // XML_PARSER_PERFORMANCE_TESTS

    XML_Char c = *name;
    switch (c)
    {
        case 'k':
            if (strcmp(name, "key") == 0) { return ELEMENT_KEY; }
            break;
        case 'r':
            if (strcmp(name, "real") == 0) { return ELEMENT_REAL; }
            break;
        case 'i':
            if (strcmp(name, "integer") == 0) { return ELEMENT_INTEGER; }
            break;
        case 'a':
            if (strcmp(name, "array") == 0) { return ELEMENT_ARRAY; }
            break;
        case 'm':
            if (strcmp(name, "map") == 0) { return ELEMENT_MAP; }
            break;
        case 'u':
            if (strcmp(name, "uuid") == 0) { return ELEMENT_UUID; }
            if (strcmp(name, "undef") == 0) { return ELEMENT_UNDEF; }
            if (strcmp(name, "uri") == 0) { return ELEMENT_URI; }
            break;
        case 'b':
            if (strcmp(name, "binary") == 0) { return ELEMENT_BINARY; }
            if (strcmp(name, "boolean") == 0) { return ELEMENT_BOOL; }
            break;
        case 's':
            if (strcmp(name, "string") == 0) { return ELEMENT_STRING; }
            break;
        case 'l':
            if (strcmp(name, "llsd") == 0) { return ELEMENT_LLSD; }
            break;
        case 'd':
            if (strcmp(name, "date") == 0) { return ELEMENT_DATE; }
            break;
    }
    return ELEMENT_UNKNOWN;
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

    #ifdef XML_PARSER_PERFORMANCE_TESTS
    XML_Timer timer( &parseTime );
    #endif  // XML_PARSER_PERFORMANCE_TESTS

    if (mParseLines)
    {
        // Use line-based reading (faster code)
        return impl.parseLines(input, data);
    }

    return impl.parse(input, data);
}

//  virtual
void LLSDXMLParser::doReset()
{
    impl.reset();
}

/**
 * @file alxmldocument.cpp
 * @brief An XML document parsed by pugixml, keeping the source text so that a
 *        byte offset can still be reported as a line and column.
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

#include "alxmldocument.h"

#include "llfile.h"

#include <cstring>
#include <istream>

bool ALXmlDocument::loadBuffer(const char* buffer, size_t length)
{
    mSource.assign(buffer, length);
    return parse();
}

bool ALXmlDocument::loadFile(const std::string& path)
{
    std::error_code ec;
    mSource = LLFile::getContents(path, ec);
    if (ec)
    {
        mDocument.reset();
        mResult = pugi::xml_parse_result();
        mLoadError = ec.message();
        return false;
    }
    return parse();
}

bool ALXmlDocument::loadStream(std::istream& stream)
{
    constexpr std::streamsize CHUNK = 16 * 1024;
    char buffer[CHUNK];

    mSource.clear();
    while (stream.good())
    {
        stream.read(buffer, CHUNK);
        const std::streamsize count = stream.gcount();
        if (count <= 0)
        {
            break;
        }
        mSource.append(buffer, static_cast<size_t>(count));
    }
    return parse();
}

const char* ALXmlDocument::errorDescription() const
{
    return mLoadError.empty() ? mResult.description() : mLoadError.c_str();
}

bool ALXmlDocument::parse()
{
    mCounted = 0;
    mCountedLine = 1;
    mLoadError.clear();

    // encoding_utf8 rather than encoding_auto: the viewer's XML is UTF-8, or
    // the US-ASCII subset of it, and transcoding would parse a buffer other
    // than the one held here and move every offset off the text it indexes.
    mResult = mDocument.load_buffer(mSource.data(), mSource.size(), PARSE_FLAGS, pugi::encoding_utf8);
    return static_cast<bool>(mResult);
}

S32 ALXmlDocument::lineOf(ptrdiff_t offset) const
{
    if (offset <= 0)
    {
        return 1;
    }
    offset = llmin(offset, static_cast<ptrdiff_t>(mSource.size()));

    if (offset < mCounted)
    {
        mCounted = 0;
        mCountedLine = 1;
    }

    const char* const text = mSource.data();
    while (mCounted < offset)
    {
        const void* found = memchr(text + mCounted, '\n', static_cast<size_t>(offset - mCounted));
        if (!found)
        {
            break;
        }
        mCounted = static_cast<const char*>(found) - text + 1;
        ++mCountedLine;
    }
    mCounted = offset;

    return mCountedLine;
}

S32 ALXmlDocument::columnOf(ptrdiff_t offset) const
{
    if (offset <= 0)
    {
        return 0;
    }
    offset = llmin(offset, static_cast<ptrdiff_t>(mSource.size()));

    const size_t line_break = mSource.rfind('\n', static_cast<size_t>(offset) - 1);
    if (line_break == std::string::npos)
    {
        return static_cast<S32>(offset);
    }
    return static_cast<S32>(offset - static_cast<ptrdiff_t>(line_break) - 1);
}

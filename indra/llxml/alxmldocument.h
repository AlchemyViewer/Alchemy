/**
 * @file alxmldocument.h
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

#pragma once

#include "stdtypes.h"

#include <pugixml.hpp>

#include <iosfwd>
#include <string>

// pugixml reports positions as byte offsets into the text it was handed, both
// for a parse failure and for any node in the tree. This keeps that text so
// those offsets can be turned back into the line and column a message needs.
class ALXmlDocument
{
public:
    // Entity references expanded, line endings normalized and attribute
    // whitespace folded to spaces, which the XML specification asks for.
    // Whitespace-only text is kept as well, so that text on either side of a
    // child element still concatenates into a single run.
    static constexpr unsigned int PARSE_FLAGS = pugi::parse_default | pugi::parse_ws_pcdata;

    bool loadBuffer(const char* buffer, size_t length);
    bool loadFile(const std::string& path);
    bool loadStream(std::istream& stream);

    explicit operator bool() const { return mLoadError.empty() && mResult; }

    pugi::xml_document& document() { return mDocument; }
    const pugi::xml_document& document() const { return mDocument; }

    // A file that could not be read is reported here too, so that a caller
    // telling the user why it has no tree needs only the one message.
    const char* errorDescription() const;
    S32 errorLine() const { return lineOf(mResult.offset); }
    S32 errorColumn() const { return columnOf(mResult.offset); }

    // Line is one based and column is zero based, matching how the messages
    // built from them read.
    S32 lineOf(ptrdiff_t offset) const;
    S32 columnOf(ptrdiff_t offset) const;

private:
    bool parse();

    std::string             mSource;
    std::string             mLoadError;
    pugi::xml_document      mDocument;
    pugi::xml_parse_result  mResult;

    // A tree walk asks for offsets in ascending order, so counting resumes
    // where it stopped and only starts over for an offset already passed.
    mutable ptrdiff_t       mCounted{ 0 };
    mutable S32             mCountedLine{ 1 };
};

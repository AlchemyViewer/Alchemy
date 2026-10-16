/**
 * @file alxmlmergelog.cpp
 * @brief Writes a line for everything the layer merge drops, in builds that are not for download.
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

#include "alxmlmergelog.h"

namespace
{
    // The path the merge matched an element by, with a word for the root.
    std::string namePath(const LLXMLNode* node)
    {
        const std::string path = ALXmlLayerMerge::namePath(node);
        return path.empty() ? std::string("the root") : path;
    }
}

//static
void ALXmlMergeLog::install()
{
#if !LL_RELEASE_FOR_DOWNLOAD
    static ALXmlMergeLog log;
    ALXmlLayerMerge::setDefaultObserver(&log);
#endif
}

std::string ALXmlMergeLog::where(S32 layer, const LLXMLNode* node) const
{
    const std::string& path = layer >= 0 && layer < (S32)mLayers.size() ? mLayers[layer] : LLStringUtil::null;
    return path + ":" + std::to_string(node ? node->getLineNumber() : 0);
}

void ALXmlMergeLog::layerParsed(S32 layer, const std::string& path)
{
    if ((S32)mLayers.size() <= layer)
    {
        mLayers.resize(layer + 1);
    }
    mLayers[layer] = path;
}

void ALXmlMergeLog::layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line)
{
    layerParsed(layer, path);
    LL_WARNS("XUIMerge") << path << ":" << line << " did not parse (" << reason
                         << "); the layer is skipped and its file reads as the base" << LL_ENDL;
}

void ALXmlMergeLog::rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    std::string base_name;
    std::string overlay_name;
    base->getAttributeString("name", base_name);
    overlay->getAttributeString("name", overlay_name);
    LL_WARNS("XUIMerge") << where(layer, overlay) << " root is named \"" << overlay_name
                         << "\" where the base says \"" << base_name << "\"" << LL_ENDL;
}

void ALXmlMergeLog::rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    LL_WARNS("XUIMerge") << where(layer, overlay) << " root is <" << overlay->getName()->mString
                         << "> where the base says <" << base->getName()->mString << ">" << LL_ENDL;
}

void ALXmlMergeLog::childRescued(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    LL_DEBUGS("XUIMerge") << where(layer, overlay) << " " << namePath(overlay)
                          << " applied at " << namePath(base) << ", where the base moved it" << LL_ENDL;
}

void ALXmlMergeLog::childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why)
{
    const char* reason = "applied to nothing";
    switch (why)
    {
    case Miss::Unnamed:
        reason = "has no name to match by";
        break;
    case Miss::Duplicate:
        reason = "is a second translation of an element already translated";
        break;
    case Miss::Ambiguous:
        reason = "names several elements below the one it is under, so which is a guess";
        break;
    case Miss::NotBelow:
        reason = "names nothing below the element it is under";
        break;
    }
    LL_WARNS("XUIMerge") << where(layer, overlay) << " <" << overlay->getName()->mString << "> "
                         << namePath(overlay) << " " << reason << LL_ENDL;
}

// Only where the layer's element says nothing at all: one written for an
// attribute of its own said what it came to say.
void ALXmlMergeLog::textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    if (overlay->getFirstChild().isNull() && overlay->mAttributes.size() <= 1)
    {
        LL_DEBUGS("XUIMerge") << where(layer, overlay) << " " << namePath(base)
                              << " has no text here; the base's text is kept" << LL_ENDL;
    }
}

void ALXmlMergeLog::attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute)
{
    LL_WARNS("XUIMerge") << where(layer, overlay_attribute) << " " << namePath(base) << " "
                         << overlay_attribute->getName()->mString
                         << "= applied to nothing: the base element has no such attribute" << LL_ENDL;
}

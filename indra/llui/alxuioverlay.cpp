/**
 * @file alxuioverlay.cpp
 * @brief Which layer and line wrote each value of a merged XUI tree, and what applied to nothing.
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

#include "alxuioverlay.h"

#include <algorithm>

void ALXUIOverlay::clear()
{
    mLayers.clear();
    mOrigins.clear();
    mDrops.clear();
}

const std::string& ALXUIOverlay::layerPath(S32 layer) const
{
    return layer >= 0 && layer < (S32)mLayers.size() ? mLayers[layer] : LLStringUtil::null;
}

const ALXUIOverlay::Origin* ALXUIOverlay::originOf(const LLXMLNode* node) const
{
    auto it = mOrigins.find(node);
    return it == mOrigins.end() ? nullptr : &it->second;
}

// static
std::string ALXUIOverlay::namePath(const LLXMLNode* node)
{
    std::vector<std::string> names;
    for (const LLXMLNode* cur = node; cur && cur->mParent; cur = cur->mParent)
    {
        std::string name;
        if (!cur->getAttributeString("name", name) || name.empty())
        {
            if (!cur->getAttributeString("value", name) || name.empty())
            {
                name = std::string("<") + cur->getName()->mString + ">";
            }
        }
        names.push_back(name);
    }
    std::reverse(names.begin(), names.end());
    std::string path;
    for (const std::string& name : names)
    {
        if (!path.empty())
        {
            path += '/';
        }
        path += name;
    }
    return path;
}

void ALXUIOverlay::drop(S32 layer, const LLXMLNode* overlay_node, std::string what, std::string why)
{
    Drop& d = mDrops.emplace_back();
    d.layer = layer;
    d.line = overlay_node ? overlay_node->getLineNumber() : 0;
    d.path = overlay_node ? namePath(overlay_node->mIsAttribute ? overlay_node->mParent : overlay_node) : std::string();
    d.what = std::move(what);
    d.why = std::move(why);
}

void ALXUIOverlay::layerParsed(S32 layer, const std::string& path)
{
    if ((S32)mLayers.size() <= layer)
    {
        mLayers.resize(layer + 1);
    }
    mLayers[layer] = path;
}

void ALXUIOverlay::layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line)
{
    layerParsed(layer, path);
    Drop& d = mDrops.emplace_back();
    d.layer = layer;
    d.line = line;
    d.what = "the file";
    d.why = "did not parse: " + reason;
}

void ALXUIOverlay::rootRefused(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    std::string base_name;
    std::string overlay_name;
    base->getAttributeString("name", base_name);
    overlay->getAttributeString("name", overlay_name);
    drop(layer, overlay, "the whole file", "root name \"" + overlay_name + "\" is not the base's \"" + base_name + "\"");
}

void ALXUIOverlay::childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why)
{
    drop(layer, overlay, std::string("<") + overlay->getName()->mString + ">",
         why == Miss::Unnamed ? "has no name to match by"
                              : "no child of " + namePath(base_parent) + " has this name");
}

void ALXUIOverlay::textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    mOrigins[base] = Origin{ layer, overlay->getLineNumber() };
}

void ALXUIOverlay::textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay)
{
    if (overlay->getFirstChild().isNull())
    {
        drop(layer, overlay, "the text", "empty in this layer; the base's text is kept");
    }
}

void ALXUIOverlay::valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute)
{
    mOrigins[base] = Origin{ layer, overlay_attribute->getLineNumber() };
}

void ALXUIOverlay::attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute)
{
    mOrigins[base_attribute] = Origin{ layer, overlay_attribute->getLineNumber() };
}

void ALXUIOverlay::attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute)
{
    drop(layer, overlay_attribute, overlay_attribute->getName()->mString, "the base element has no such attribute");
}

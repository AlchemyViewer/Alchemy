/**
 * @file alxmlmergelog.h
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

#pragma once

#include "alxmllayermerge.h"

#include <string>
#include <vector>

// A layer that applies to nothing is a translation nobody sees, and the
// merge decides it in silence. This says so under the "XUIMerge" tag: the
// layer's file and line, the element or attribute, and why. A build in the
// base language merges no layers and so says nothing; it speaks exactly
// when a localized or skinned file is not reaching the screen.
class ALXmlMergeLog final : public ALXmlMergeObserver
{
public:
    // Installs one as the merge's default observer, for callers that pass
    // none. Does nothing in a build for download.
    static void install();

    void layerParsed(S32 layer, const std::string& path) override;
    void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) override;
    void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void childRescued(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why) override;
    void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override;

private:
    // The file of a layer, and the line an overlay node sits on in it.
    std::string where(S32 layer, const LLXMLNode* node) const;

    std::vector<std::string> mLayers;
};

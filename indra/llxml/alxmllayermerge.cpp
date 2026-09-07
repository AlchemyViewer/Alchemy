/**
 * @file alxmllayermerge.cpp
 * @brief The one merge of a localized or skinned XML layer over its base, observed.
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

#include "alxmllayermerge.h"

#include "alxmldocument.h"

namespace
{
    // The name a child is matched by: its name attribute, or its value
    // attribute when it has no name, as a combo box's items do.
    std::string matchKey(const LLXMLNode* node)
    {
        std::string key;
        if (!node->getAttributeString("name", key) || key.empty())
        {
            node->getAttributeString("value", key);
        }
        return key;
    }

    bool hasText(const LLXMLNode* node)
    {
        return node->getValue().find_first_not_of(" \t\r\n") != std::string::npos;
    }

    // Why a layer did not parse, for whoever is listening; the parse that
    // failed only logged it.
    void reportSkipped(ALXmlMergeObserver* observer, S32 layer, const std::string& path)
    {
        if (!observer)
        {
            return;
        }
        ALXmlDocument doc;
        doc.loadFile(path);
        observer->layerSkipped(layer, path, doc.errorDescription(), doc.errorLine());
    }
}

void ALXmlLayerMerge::merge(LLXMLNodePtr& base, LLXMLNodePtr& overlay, S32 layer, ALXmlMergeObserver* observer)
{
    if (base.isNull() || overlay.isNull())
    {
        LL_WARNS() << "Node invalid" << LL_ENDL;
        return;
    }

    // The text, whatever the overlay has.
    if (observer)
    {
        if (hasText(overlay))
        {
            observer->textApplied(layer, base, overlay);
        }
        else if (hasText(base))
        {
            observer->textBlanked(layer, base, overlay);
        }
    }
    base->setValue(overlay->getValue());

    // Every attribute the base has too.
    for (auto& [name, overlay_attribute] : overlay->mAttributes)
    {
        LLXMLNodePtr base_attribute;
        base->getAttribute(name, base_attribute, false);
        if (base_attribute)
        {
            base_attribute->setValue(overlay_attribute->getValue());
            if (observer)
            {
                observer->attributeApplied(layer, base_attribute, overlay_attribute);
            }
        }
        else if (observer)
        {
            observer->attributeDropped(layer, base, overlay_attribute);
        }
    }

    // Each overlay child against the base's children by name, from where
    // the last match left off, around once.
    LLXMLNodePtr child = base->getFirstChild();
    LLXMLNodePtr last_child = child;
    for (LLXMLNodePtr overlay_child = overlay->getFirstChild(); overlay_child.notNull();
         overlay_child = overlay_child->getNextSibling())
    {
        const std::string overlay_key = matchKey(overlay_child);
        bool matched = false;
        while (child.notNull())
        {
            const std::string base_key = matchKey(child);
            if (!base_key.empty() && overlay_key == base_key)
            {
                if (observer)
                {
                    observer->childMatched(layer, child, overlay_child);
                }
                merge(child, overlay_child, layer, observer);
                matched = true;
                last_child = child;
                child = child->getNextSibling();
                if (child.isNull())
                {
                    child = base->getFirstChild();
                }
                break;
            }

            child = child->getNextSibling();
            if (child.isNull())
            {
                child = base->getFirstChild();
            }
            if (child == last_child)
            {
                break;
            }
        }
        if (!matched && observer)
        {
            observer->childUnmatched(layer, base, overlay_child,
                                     overlay_key.empty() ? ALXmlMergeObserver::Miss::Unnamed
                                                         : ALXmlMergeObserver::Miss::NoSibling);
        }
    }
}

bool ALXmlLayerMerge::load(const std::vector<std::string>& paths, LLXMLNodePtr& root, ALXmlMergeObserver* observer)
{
    if (paths.empty())
    {
        return false;
    }

    const std::string& base_path = paths.front();
    if (base_path.empty())
    {
        return false;
    }

    if (!LLXMLNode::parseFile(base_path, root, nullptr))
    {
        LL_WARNS() << "Problem reading UI description file: " << base_path << " " << errno << LL_ENDL;
        reportSkipped(observer, 0, base_path);
        return false;
    }
    if (observer)
    {
        observer->layerParsed(0, base_path);
    }

    S32 layer = 0;
    for (size_t i = 1; i < paths.size(); ++i)
    {
        const std::string& layer_path = paths[i];
        ++layer;
        if (layer_path.empty() || layer_path == base_path)
        {
            // no localized version of this file, that's ok, keep looking
            continue;
        }

        LLXMLNodePtr overlay;
        if (!LLXMLNode::parseFile(layer_path, overlay, nullptr))
        {
            LL_WARNS() << "Problem reading localized UI description file: " << layer_path << LL_ENDL;
            reportSkipped(observer, layer, layer_path);
            return false;
        }
        if (observer)
        {
            observer->layerParsed(layer, layer_path);
        }

        std::string base_name;
        std::string overlay_name;
        overlay->getAttributeString("name", overlay_name);
        root->getAttributeString("name", base_name);
        if (overlay_name == base_name)
        {
            if (observer)
            {
                observer->rootMatched(layer, root, overlay);
            }
            merge(root, overlay, layer, observer);
        }
        else if (observer)
        {
            observer->rootRefused(layer, root, overlay);
        }
    }

    return true;
}

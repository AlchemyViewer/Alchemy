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

    // The text. What the overlay has in its body replaces the base's. A
    // value attribute against a base that carries its text in the body is
    // that text: the two forms say the same thing, and a translator writes
    // whichever the file had when the translation was made. Text the
    // overlay lacks in either form leaves the base's alone, since an
    // element written for its attributes or its children says nothing
    // about the text.
    static const LLStringTableEntry* value_entry = gStringTable.addStringEntry("value");
    LLXMLNodePtr overlay_value;
    overlay->getAttribute(value_entry, overlay_value, false);
    const bool text_in_body = hasText(base) && !base->mAttributes.count(value_entry);
    const bool value_as_text = !hasText(overlay) && text_in_body && overlay_value.notNull() && hasText(overlay_value);
    if (hasText(overlay))
    {
        if (observer)
        {
            observer->textApplied(layer, base, overlay);
        }
        base->setValue(overlay->getValue());
    }
    else if (value_as_text)
    {
        if (observer)
        {
            observer->valueAppliedAsText(layer, base, overlay_value);
        }
        base->setValue(overlay_value->getValue());
    }
    else if (hasText(base) && observer)
    {
        observer->textKept(layer, base, overlay);
    }

    // Every attribute the base has too, except the name: it is the key
    // the element was matched by, equal by construction below the root,
    // and at the root a rename the layer never heard of must not travel
    // into the tree, where the name is what the viewer knows it by.
    static const LLStringTableEntry* name_entry = gStringTable.addStringEntry("name");
    for (auto& [name, overlay_attribute] : overlay->mAttributes)
    {
        if (name == name_entry)
        {
            continue;
        }
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
        else if (observer && !(value_as_text && name == value_entry))
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

        // The file name binds the layer to the base. A root name or tag
        // that differs is a change the layer never heard of, a rename or a
        // widget that became another, and the children still match by
        // their own names.
        if (observer)
        {
            std::string base_name;
            std::string overlay_name;
            overlay->getAttributeString("name", overlay_name);
            root->getAttributeString("name", base_name);
            if (overlay_name != base_name)
            {
                observer->rootNameDiffers(layer, root, overlay);
            }
            if (overlay->getName() != root->getName())
            {
                observer->rootTagDiffers(layer, root, overlay);
            }
            observer->rootMatched(layer, root, overlay);
        }
        merge(root, overlay, layer, observer);
    }

    return true;
}

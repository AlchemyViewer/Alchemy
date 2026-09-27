/**
 * @file alscriptitemdrop.cpp
 * @brief An inventory item dropped on a script: its name, or its asset's key, put in as a string.
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


#include "llviewerprecompiledheaders.h"

#include "alscriptitemdrop.h"

#include "alcodeeditor.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "llinventory.h"

namespace ALScriptItemDrop
{
std::string literal(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

bool drop(ALScriptStudioDoc& doc, const ALScriptStudioServices& services, const key_of_t& key_of, S32 x, S32 y, MASK mask, bool dropping,
          EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)
{
    if (doc.notecard || !doc.editor)
    {
        return false;
    }
    switch (type)
    {
        case DAD_CALLINGCARD:
        case DAD_TEXTURE:
        case DAD_SOUND:
        case DAD_LANDMARK:
        case DAD_SCRIPT:
        case DAD_CLOTHING:
        case DAD_OBJECT:
        case DAD_NOTECARD:
        case DAD_BODYPART:
        case DAD_ANIMATION:
        case DAD_GESTURE:
        case DAD_MESH:
        case DAD_MATERIAL:
        case DAD_SETTINGS:
            break;
        default:
            return false;
    }
    const LLInventoryItem* item = static_cast<const LLInventoryItem*>(cargo);
    if (!item)
    {
        return false;
    }
    if (!doc.loaded || !doc.modifiable || doc.editor->isReadOnly())
    {
        *accept = ACCEPT_NO;
        tooltip = services.words("ScriptItemDropReadOnly");
        return true;
    }
    // Its name, or with Shift its asset's key where that may be seen.
    const bool by_key = (mask & MASK_SHIFT) != 0;
    std::string text;
    if (by_key)
    {
        const LLUUID key = key_of ? key_of(*item) : LLUUID::null;
        if (key.isNull())
        {
            *accept = ACCEPT_NO;
            tooltip = services.words("ScriptItemDropNoKey");
            return true;
        }
        text = literal(key.asString());
    }
    else
    {
        text = literal(item->getName());
    }
    *accept = ACCEPT_YES_SINGLE;
    if (!dropping)
    {
        LLStringUtil::format_map_t args;
        args["[TEXT]"] = text;
        tooltip        = services.words(by_key ? "ScriptItemDropKey" : "ScriptItemDropName", args);
        return true;
    }
    // Where it lands, the caret after it.
    doc.editor->setCaret(doc.editor->posAtLocal(x, y, true));
    doc.editor->insertText(text);
    doc.editor->setFocus(true);
    return true;
}
}

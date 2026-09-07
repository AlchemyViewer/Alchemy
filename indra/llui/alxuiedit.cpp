/**
 * @file alxuiedit.cpp
 * @brief One XUI file, edited as the bytes it is.
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

#include "alxuiedit.h"

#include "alxuicatalog.h"

#include "alxmldocument.h"

#include "llfile.h"

#include <cctype>

namespace
{
    bool isSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    // The three characters an attribute value cannot carry as itself, the
    // delimiter it is written between included.
    std::string escapeValue(std::string_view value, char quote)
    {
        std::string out;
        out.reserve(value.size());
        for (const char c : value)
        {
            if (c == '&')           { out += "&amp;"; }
            else if (c == '<')      { out += "&lt;"; }
            else if (c == quote)    { out += quote == '"' ? "&quot;" : "&apos;"; }
            else                    { out += c; }
        }
        return out;
    }

    // A rect edge says which side of the parent it is measured from by its
    // sign, so a delta that crosses zero is a jump to the other side and
    // not the move it was asked for.
    bool isAnchoredBySign(std::string_view name)
    {
        return name == "left" || name == "right" || name == "top" || name == "bottom";
    }
}

ALXUIEdit::ALXUIEdit()
:   mDoc(std::make_unique<ALXmlDocument>())
{
}

ALXUIEdit::~ALXUIEdit() = default;

bool ALXUIEdit::loadFile(const std::string& path)
{
    std::error_code ec;
    mText = LLFile::getContents(path, ec);
    if (ec)
    {
        mError = ec.message();
        return false;
    }
    mPath = path;
    mDirty = false;
    return parse();
}

bool ALXUIEdit::loadBuffer(std::string_view text)
{
    mPath.clear();
    mText.assign(text);
    mDirty = false;
    return parse();
}

bool ALXUIEdit::parse()
{
    mError.clear();
    if (!mDoc->loadBuffer(mText.data(), mText.size()))
    {
        mError = mDoc->errorDescription();
        return false;
    }
    return true;
}

pugi::xml_node ALXUIEdit::root() const
{
    return mDoc->document().document_element();
}

pugi::xml_node ALXUIEdit::resolve(const path_t& path) const
{
    return path.empty() ? root() : ALXUICatalog::resolve(root(), path);
}

bool ALXUIEdit::save()
{
    return saveAs(mPath);
}

bool ALXUIEdit::saveAs(const std::string& path)
{
    if (path.empty())
    {
        mError = "no file to write";
        return false;
    }

    // Binary, so that the line endings written are the ones held.
    llofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        mError = "could not open " + path;
        return false;
    }
    out.write(mText.data(), (std::streamsize)mText.size());
    out.close();
    if (!out.good())
    {
        mError = "could not write " + path;
        return false;
    }
    mPath = path;
    mDirty = false;
    return true;
}

// The element's attributes as they are written, walked from the tag name.
// A tag holds nothing but attributes, so the scan is the grammar itself:
// whitespace, a name, an equals sign, and a value between two quotes of
// the same kind.
bool ALXUIEdit::spanOf(pugi::xml_node node, std::string_view name, Span& value, Span& whole) const
{
    const ptrdiff_t start = node.offset_debug();
    if (start < 0)
    {
        return false;
    }

    size_t i = (size_t)start;
    while (i < mText.size() && !isSpace(mText[i]) && mText[i] != '>' && mText[i] != '/')
    {
        ++i;
    }
    while (i < mText.size())
    {
        const size_t space = i;
        while (i < mText.size() && isSpace(mText[i]))
        {
            ++i;
        }
        if (i >= mText.size() || mText[i] == '>' || mText[i] == '/')
        {
            return false;
        }
        const size_t name_at = i;
        while (i < mText.size() && !isSpace(mText[i]) && mText[i] != '=')
        {
            ++i;
        }
        const std::string_view found(mText.data() + name_at, i - name_at);
        while (i < mText.size() && isSpace(mText[i]))
        {
            ++i;
        }
        if (i >= mText.size() || mText[i] != '=')
        {
            return false;
        }
        ++i;
        while (i < mText.size() && isSpace(mText[i]))
        {
            ++i;
        }
        if (i >= mText.size() || (mText[i] != '"' && mText[i] != '\''))
        {
            return false;
        }
        const char quote = mText[i];
        const size_t value_at = ++i;
        while (i < mText.size() && mText[i] != quote)
        {
            ++i;
        }
        if (i >= mText.size())
        {
            return false;
        }
        const size_t value_end = i++;
        if (found == name)
        {
            value = { value_at, value_end - value_at };
            whole = { space, i - space };
            return true;
        }
    }
    return false;
}

// After the last attribute the element carries, spaced the way that one
// is spaced, so an attribute written onto a file of one attribute per
// line arrives on a line of its own.
bool ALXUIEdit::insertionPoint(pugi::xml_node node, size_t& offset, std::string& separator) const
{
    const ptrdiff_t start = node.offset_debug();
    if (start < 0)
    {
        return false;
    }

    size_t i = (size_t)start;
    while (i < mText.size() && !isSpace(mText[i]) && mText[i] != '>' && mText[i] != '/')
    {
        ++i;
    }
    separator = " ";
    while (i < mText.size())
    {
        const size_t space = i;
        while (i < mText.size() && isSpace(mText[i]))
        {
            ++i;
        }
        if (i >= mText.size())
        {
            return false;
        }
        if (mText[i] == '>' || mText[i] == '/')
        {
            offset = space;
            return true;
        }
        separator.assign(mText, space, i - space);
        while (i < mText.size() && !isSpace(mText[i]) && mText[i] != '=')
        {
            ++i;
        }
        while (i < mText.size() && (isSpace(mText[i]) || mText[i] == '='))
        {
            ++i;
        }
        if (i >= mText.size() || (mText[i] != '"' && mText[i] != '\''))
        {
            return false;
        }
        const char quote = mText[i];
        ++i;
        while (i < mText.size() && mText[i] != quote)
        {
            ++i;
        }
        if (i >= mText.size())
        {
            return false;
        }
        ++i;
    }
    return false;
}

void ALXUIEdit::splice(const Span& span, std::string_view text)
{
    mText.replace(span.offset, span.length, text);
    mDirty = true;
    parse();
}

bool ALXUIEdit::valueText(pugi::xml_node node, std::string_view name, std::string& out) const
{
    Span span;
    Span whole;
    if (!spanOf(node, name, span, whole))
    {
        return false;
    }
    out.assign(mText, span.offset, span.length);
    return true;
}

bool ALXUIEdit::setAttribute(const path_t& path, const std::string& name, const std::string& value)
{
    mError.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    Span span;
    Span whole;
    if (spanOf(node, name, span, whole))
    {
        const char quote = span.offset > 0 ? mText[span.offset - 1] : '"';
        splice(span, escapeValue(value, quote));
        return true;
    }

    size_t at = 0;
    std::string separator;
    if (!insertionPoint(node, at, separator))
    {
        mError = "could not find where " + name + " would go";
        return false;
    }
    splice({ at, 0 }, separator + name + "=\"" + escapeValue(value, '"') + "\"");
    return true;
}

bool ALXUIEdit::removeAttribute(const path_t& path, const std::string& name)
{
    mError.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    Span span;
    Span whole;
    if (!spanOf(node, name, span, whole))
    {
        mError = "the element does not carry " + name;
        return false;
    }
    splice(whole, std::string_view());
    return true;
}

bool ALXUIEdit::isGeometryAttribute(std::string_view name)
{
    return name == "left" || name == "right" || name == "top" || name == "bottom"
        || name == "width" || name == "height"
        || name == "left_pad" || name == "top_pad"
        || name == "left_delta" || name == "top_delta" || name == "bottom_delta";
}

bool ALXUIEdit::addDelta(pugi::xml_node node, std::string_view name, S32 delta,
                         std::vector<std::pair<std::string, S32>>& writes)
{
    const std::string attribute(name);
    const S32 was = node.attribute(attribute.c_str()).as_int();
    const S32 now = was + delta;
    if (isAnchoredBySign(name) && (was < 0) != (now < 0))
    {
        mError = attribute + " would cross the edge it is measured from, which moves the element to the other side"
                 " of its parent rather than by " + std::to_string(delta) + " pixels";
        return false;
    }
    writes.emplace_back(attribute, now);
    return true;
}

// LLView::applyXUILayout read backwards. Which attribute a move writes is
// whichever one the author used, in the order that function reads them:
// a delta is consulted before the edge it overrides, and an edge before
// the padding it makes irrelevant. In topleft layout every vertical form
// counts downwards, so a move up subtracts.
bool ALXUIEdit::translate(const path_t& path, S32 dx, S32 dy, const Anchor& now)
{
    mError.clear();
    mWritten.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    std::vector<std::pair<std::string, S32>> writes;
    if (dx != 0)
    {
        if (node.attribute("left_delta"))
        {
            if (!addDelta(node, "left_delta", dx, writes)) { return false; }
        }
        else if (node.attribute("left"))
        {
            if (!addDelta(node, "left", dx, writes)) { return false; }
            if (node.attribute("right") && !node.attribute("width")
                && !addDelta(node, "right", dx, writes))
            {
                return false;
            }
        }
        else if (node.attribute("left_pad"))
        {
            if (!addDelta(node, "left_pad", dx, writes)) { return false; }
        }
        else if (node.attribute("right"))
        {
            if (!addDelta(node, "right", dx, writes)) { return false; }
        }
        else
        {
            writes.emplace_back("left", now.left + dx);
        }
    }
    if (dy != 0)
    {
        const S32 dv = now.topLeft ? -dy : dy;
        if (node.attribute("bottom_delta"))
        {
            if (!addDelta(node, "bottom_delta", dv, writes)) { return false; }
        }
        else if (node.attribute("top"))
        {
            if (!addDelta(node, "top", dv, writes)) { return false; }
            if (node.attribute("bottom") && !node.attribute("height")
                && !addDelta(node, "bottom", dv, writes))
            {
                return false;
            }
        }
        else if (node.attribute("bottom"))
        {
            if (!addDelta(node, "bottom", dv, writes)) { return false; }
        }
        else if (now.topLeft && node.attribute("top_pad"))
        {
            if (!addDelta(node, "top_pad", dv, writes)) { return false; }
        }
        else if (now.topLeft && node.attribute("top_delta"))
        {
            if (!addDelta(node, "top_delta", dv, writes)) { return false; }
        }
        else if (now.topLeft)
        {
            writes.emplace_back("top", now.top + dv);
        }
        else
        {
            writes.emplace_back("bottom", now.bottom + dv);
        }
    }

    for (const auto& [attribute, value] : writes)
    {
        if (!setAttribute(path, attribute, std::to_string(value)))
        {
            return false;
        }
        mWritten.push_back(attribute);
    }
    return true;
}

// A resize holds the edge the element is positioned from and moves the
// other one: the size when the file names a size, the far edge when it
// names two edges.
bool ALXUIEdit::resize(const path_t& path, S32 dw, S32 dh, const Anchor& now)
{
    mError.clear();
    mWritten.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    std::vector<std::pair<std::string, S32>> writes;
    if (dw != 0)
    {
        if (node.attribute("width"))
        {
            if (!addDelta(node, "width", dw, writes)) { return false; }
        }
        else if (node.attribute("left") && node.attribute("right"))
        {
            if (!addDelta(node, "right", dw, writes)) { return false; }
        }
        else
        {
            writes.emplace_back("width", now.width + dw);
        }
    }
    if (dh != 0)
    {
        if (node.attribute("height"))
        {
            if (!addDelta(node, "height", dh, writes)) { return false; }
        }
        else if (node.attribute("top") && node.attribute("bottom"))
        {
            if (!addDelta(node, now.topLeft ? "bottom" : "top", dh, writes)) { return false; }
        }
        else
        {
            writes.emplace_back("height", now.height + dh);
        }
    }

    for (const auto& [attribute, value] : writes)
    {
        if (!setAttribute(path, attribute, std::to_string(value)))
        {
            return false;
        }
        mWritten.push_back(attribute);
    }
    return true;
}

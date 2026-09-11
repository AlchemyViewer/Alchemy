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
#include "alxuiselection.h"

#include "alxmldocument.h"

#include "llfile.h"

#include <algorithm>
#include <cctype>

namespace
{
    bool isSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    // The two characters text cannot carry as itself.
    std::string escapeText(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (const char c : text)
        {
            if (c == '&')       { out += "&amp;"; }
            else if (c == '<')  { out += "&lt;"; }
            else                { out += c; }
        }
        return out;
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

    // Every line after the first indented one step further, so a block of
    // several lines lands whole at the depth it is written into.
    std::string indented(const std::string& xml, const std::string& indent)
    {
        std::string block = xml;
        for (size_t line = block.find('\n'); line != std::string::npos;
             line = block.find('\n', line + 1 + indent.size()))
        {
            block.insert(line + 1, indent);
        }
        return block;
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
    std::string text = LLFile::getContents(path, ec);
    if (ec)
    {
        // What is held stays held: a file that cannot be read again is
        // not a reason to lose what was read the first time.
        mError = ec.message();
        return false;
    }
    mText = std::move(text);
    mPath = path;
    mSaved = mText;
    mDirty = false;
    clearHistory();
    return parse();
}

bool ALXUIEdit::loadBuffer(std::string_view text)
{
    mPath.clear();
    mText.assign(text);
    mSaved = mText;
    mDirty = false;
    clearHistory();
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

// An element of a file is addressed the way the merge addresses one: by
// name, whatever the tag is.
pugi::xml_node ALXUIEdit::resolve(const path_t& path) const
{
    return path.empty() ? root() : ALXUICatalog::resolve(root(), path, /*any_tag=*/true);
}

S32 ALXUIEdit::lineOf(pugi::xml_node node) const
{
    return mDoc && node ? mDoc->lineOf(node.offset_debug()) : 0;
}

bool ALXUIEdit::save()
{
    return saveAs(mPath);
}

bool ALXUIEdit::writeFile(const std::string& path, std::string_view text, std::string& error)
{
    if (path.empty())
    {
        error = "no file to write";
        return false;
    }

    // Binary, so that the line endings written are the ones held.
    llofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        error = "could not open " + path;
        return false;
    }
    out.write(text.data(), (std::streamsize)text.size());
    out.close();
    if (!out.good())
    {
        error = "could not write " + path;
        return false;
    }
    return true;
}

bool ALXUIEdit::saveAs(const std::string& path)
{
    if (!writeFile(path, mText, mError))
    {
        return false;
    }
    mPath = path;
    mSaved = mText;
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

// The undo stack holds whole texts, so it is capped by what they weigh
// rather than by how many there are: a file of twenty kilobytes keeps two
// hundred steps and one of two hundred keeps twenty.
static constexpr size_t UNDO_BYTES = 4u << 20;

ALXUIEdit::Step::Step(ALXUIEdit& doc)
:   mDoc(doc)
{
    if (++mDoc.mDepth == 1)
    {
        mDoc.mPending = Change();
    }
}

void ALXUIEdit::note(Did did, const path_t& path, std::string_view field)
{
    if (mDepth == 1)
    {
        mPending.did = did;
        mPending.path = path;
        mPending.after = path;
        mPending.field = field;
        // What a caller holding something built from this can write onto it
        // without building it again: one field of one element, and nothing
        // that changes which elements there are or where they sit.
        mPending.oneField = did == Did::WroteField
                         || did == Did::TookFieldOut
                         || did == Did::Renamed;
    }
}

// Where a step that writes a name leaves the element it renames. A step is
// the name and which of the siblings of that name it is, counted in the
// order the file has them, so a rename into a name a sibling already carries
// still says which of the two it means.
void ALXUIEdit::noteRename(pugi::xml_node node, const std::string& name)
{
    if (mDepth != 1 || mPending.after.empty())
    {
        return;
    }
    S32 ordinal = 0;
    for (pugi::xml_node sibling = node.parent().first_child();
         sibling && sibling != node;
         sibling = sibling.next_sibling())
    {
        if (name == sibling.attribute("name").value())
        {
            ++ordinal;
        }
    }
    mPending.after.back() = ALXUISelection::step(name, ordinal);
}

ALXUIEdit::Step::~Step()
{
    if (--mDoc.mDepth == 0)
    {
        // What the step did is settled only now: where an element landed
        // is read after the splice that put it there, and the record was
        // taken at the first splice.
        if (mDoc.mStepOpen && !mDoc.mUndoWhat.empty())
        {
            mDoc.mUndoWhat.back() = mDoc.mPending;
        }
        mDoc.mStepOpen = false;
    }
}

void ALXUIEdit::landed(size_t name_offset)
{
    if (const pugi::xml_node node = elementNamedAt(name_offset))
    {
        mPending.after = ALXUICatalog::namePath(node, /*any_tag=*/true);
    }
}

pugi::xml_node ALXUIEdit::elementNamedAt(size_t name_offset) const
{
    std::vector<pugi::xml_node> waiting{ root() };
    while (!waiting.empty())
    {
        const pugi::xml_node node = waiting.back();
        waiting.pop_back();
        if (!node || node.type() != pugi::node_element)
        {
            continue;
        }
        if ((size_t)node.offset_debug() == name_offset)
        {
            return node;
        }
        for (pugi::xml_node child : node.children())
        {
            waiting.push_back(child);
        }
    }
    return pugi::xml_node();
}

void ALXUIEdit::splice(const Span& span, std::string_view text)
{
    // The first splice of an operation is what makes it a step. One that
    // fails before it changes anything leaves no step behind.
    if (mDepth > 0 && !mStepOpen)
    {
        mUndo.push_back(mText);
        mUndoWhat.push_back(mPending);
        mRedo.clear();
        mRedoWhat.clear();
        mStepOpen = true;
        ++mTaken;

        size_t held = 0;
        for (const std::string& step : mUndo)
        {
            held += step.size();
        }
        while (mUndo.size() > 1 && held > UNDO_BYTES)
        {
            held -= mUndo.front().size();
            mUndo.erase(mUndo.begin());
            mUndoWhat.erase(mUndoWhat.begin());
        }
    }

    mText.replace(span.offset, span.length, text);
    mDirty = mText != mSaved;
    parse();
}

bool ALXUIEdit::undo()
{
    if (mUndo.empty())
    {
        return false;
    }
    // The step being put back is the one that followed the text being
    // restored, so what it did travels with it onto the other stack.
    mLastChange = mUndoWhat.back();
    // The step is taken back, so the element reads as it did before it.
    mLastPath = mLastChange.path;
    mRedo.push_back(std::move(mText));
    mRedoWhat.push_back(mLastChange);
    mUndoWhat.pop_back();
    mText = std::move(mUndo.back());
    mUndo.pop_back();
    mDirty = mText != mSaved;
    return parse();
}

bool ALXUIEdit::redo()
{
    if (mRedo.empty())
    {
        return false;
    }
    mLastChange = mRedoWhat.back();
    // The step is applied again, so the element is where the step put it.
    mLastPath = mLastChange.after;
    mUndo.push_back(std::move(mText));
    mUndoWhat.push_back(mLastChange);
    mRedoWhat.pop_back();
    mText = std::move(mRedo.back());
    mRedo.pop_back();
    mDirty = mText != mSaved;
    return parse();
}

void ALXUIEdit::clearHistory()
{
    mUndo.clear();
    mRedo.clear();
    mUndoWhat.clear();
    mRedoWhat.clear();
    mLastChange = Change();
    mLastPath.clear();
    mTaken = 0;
}

bool ALXUIEdit::fieldText(const path_t& path, std::string_view field, std::string& out) const
{
    const pugi::xml_node node = resolve(path);
    return node && valueText(node, field, out);
}

// From the '<' of a tag to the '>' that ends it. An angle bracket inside
// a quoted value is text, and passing over the values is what tells the
// two apart.
size_t ALXUIEdit::endOfTag(size_t at, bool& self_closing) const
{
    self_closing = false;
    for (size_t i = at; i < mText.size(); ++i)
    {
        const char c = mText[i];
        if (c == '"' || c == '\'')
        {
            const char quote = c;
            while (++i < mText.size() && mText[i] != quote)
            {
            }
            if (i >= mText.size())
            {
                return std::string::npos;
            }
        }
        else if (c == '>')
        {
            self_closing = i > at && mText[i - 1] == '/';
            return i + 1;
        }
    }
    return std::string::npos;
}

// The whole of an element: its open tag, everything under it and its
// close tag, plus the whitespace of the line it sits on, so that removing
// it removes the line. Comments, CDATA and processing instructions are
// passed over, since a close tag written inside one closes nothing.
bool ALXUIEdit::extentOf(pugi::xml_node node, Span& body, Span& whole) const
{
    const ptrdiff_t named = node.offset_debug();
    if (named < 1)
    {
        return false;
    }
    const size_t start = (size_t)named - 1;      // the '<'
    bool self_closing = false;
    size_t i = endOfTag(start, self_closing);
    if (i == std::string::npos)
    {
        return false;
    }

    size_t end = i;
    if (!self_closing)
    {
        const size_t content = i;
        S32 depth = 1;
        while (i < mText.size() && depth > 0)
        {
            if (mText[i] != '<')
            {
                ++i;
                continue;
            }
            if (mText.compare(i, 4, "<!--") == 0)
            {
                const size_t close = mText.find("-->", i + 4);
                i = close == std::string::npos ? mText.size() : close + 3;
            }
            else if (mText.compare(i, 9, "<![CDATA[") == 0)
            {
                const size_t close = mText.find("]]>", i + 9);
                i = close == std::string::npos ? mText.size() : close + 3;
            }
            else if (mText.compare(i, 2, "<?") == 0)
            {
                const size_t close = mText.find("?>", i + 2);
                i = close == std::string::npos ? mText.size() : close + 2;
            }
            else
            {
                const bool closing = mText.compare(i, 2, "</") == 0;
                bool child_self_closing = false;
                const size_t after = endOfTag(i, child_self_closing);
                if (after == std::string::npos)
                {
                    return false;
                }
                depth += closing ? -1 : (child_self_closing ? 0 : 1);
                i = after;
            }
        }
        if (depth > 0)
        {
            return false;
        }
        end = i;
        // The body is what sits between the tags.
        body = { content, mText.rfind('<', end - 1) - content };
    }
    else
    {
        body = { end, 0 };
    }

    // The whitespace before it belongs to it when nothing else shares the
    // line, which is how these files are written.
    size_t first = start;
    while (first > 0 && (mText[first - 1] == ' ' || mText[first - 1] == '\t'))
    {
        --first;
    }
    if (first > 0 && mText[first - 1] == '\n')
    {
        --first;
        if (first > 0 && mText[first - 1] == '\r')
        {
            --first;
        }
    }
    else
    {
        first = start;
    }
    whole = { first, end - first };
    return true;
}

// The bytes a tag that closes itself ends with, back through the
// whitespace someone wrote before them: opening the tag takes the space
// with it, so that <text name="b" /> opens as <text name="b">.
ALXUIEdit::Span ALXUIEdit::selfCloseSpan(size_t after_tag) const
{
    size_t first = after_tag - 2;        // the '/'
    while (first > 0 && (mText[first - 1] == ' ' || mText[first - 1] == '\t'))
    {
        --first;
    }
    return { first, after_tag - first };
}

// The indentation of the line an offset sits on.
std::string ALXUIEdit::indentAt(size_t offset) const
{
    size_t line = offset ? mText.rfind('\n', offset - 1) : std::string::npos;
    line = line == std::string::npos ? 0 : line + 1;
    const size_t first = mText.find_first_not_of(" \t", line);
    return std::string(mText, line, (first == std::string::npos ? line : llmin(first, offset)) - line);
}

// Where a child of this element goes: after the last one it has, at the
// end of its content otherwise, with the indentation its children carry
// and a note of whether the tag has to be opened for them.
bool ALXUIEdit::contentPoint(pugi::xml_node node, size_t& offset, size_t& length, std::string& indent, bool& opens) const
{
    const ptrdiff_t named = node.offset_debug();
    if (named < 1)
    {
        return false;
    }
    const size_t start = (size_t)named - 1;
    bool self_closing = false;
    const size_t after_tag = endOfTag(start, self_closing);
    if (after_tag == std::string::npos)
    {
        return false;
    }

    // A child is written one step further in than the element itself, in
    // whichever character the file indents with.
    const std::string own = indentAt(start);
    const std::string step = own.find('\t') != std::string::npos ? "\t" : "    ";

    opens = self_closing;
    if (self_closing)
    {
        const Span close = selfCloseSpan(after_tag);
        offset = close.offset;
        length = close.length;
        indent = own + step;
        return true;
    }
    length = 0;

    Span body;
    Span whole;
    if (!extentOf(node, body, whole))
    {
        return false;
    }

    // After the last child element, if it has one: its own line says how
    // the children of this element are indented.
    pugi::xml_node last;
    for (pugi::xml_node child : node.children())
    {
        if (child.type() == pugi::node_element)
        {
            last = child;
        }
    }
    if (last)
    {
        Span child_body;
        Span child_whole;
        if (!extentOf(last, child_body, child_whole))
        {
            return false;
        }
        offset = child_whole.offset + child_whole.length;
        indent = indentAt((size_t)last.offset_debug() - 1);
        if (indent.find_first_not_of(" \t") != std::string::npos)
        {
            indent = own + step;
        }
        return true;
    }

    // With no children, the content is the whitespace between the tags,
    // and the first child goes at the front of it: what was there stays
    // behind the new element and closes the tag on its own line, as it
    // did when there was nothing between them.
    offset = body.offset;
    indent = own + step;
    return true;
}

bool ALXUIEdit::setText(const path_t& path, const std::string& text)
{
    Step step(*this);
    mError.clear();
    note(Did::WroteText, path);
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }
    for (pugi::xml_node child : node.children())
    {
        if (child.type() == pugi::node_element)
        {
            mError = std::string("the text of ") + node.name() + " is written around its children";
            return false;
        }
    }

    Span body;
    Span whole;
    if (!extentOf(node, body, whole))
    {
        mError = "could not read the element";
        return false;
    }

    bool self_closing = false;
    const size_t after_tag = endOfTag((size_t)node.offset_debug() - 1, self_closing);
    if (self_closing)
    {
        // A tag that closes itself has nowhere to put text, so it opens.
        splice(selfCloseSpan(after_tag), ">" + escapeText(text) + "</" + std::string(node.name()) + ">");
        return true;
    }
    splice(body, escapeText(text));
    return true;
}

bool ALXUIEdit::insertElement(const path_t& parent, const std::string& xml)
{
    Step step(*this);
    mError.clear();
    note(Did::AddedElement, parent);
    pugi::xml_node node = resolve(parent);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    size_t at = 0;
    size_t length = 0;
    std::string indent;
    bool opens = false;
    if (!contentPoint(node, at, length, indent, opens))
    {
        mError = "could not read the element";
        return false;
    }

    const std::string eol = mText.find("\r\n") == std::string::npos ? "\n" : "\r\n";

    // Every line of it lands at the same depth, so an element that
    // arrives with children of its own keeps their shape and takes the
    // indentation of where it is going.
    const std::string block = indented(xml, indent);

    // Where the element's own '<' lands, so the step can say where it is.
    const size_t lead = llmin(block.size(), block.find_first_not_of(" \t\r\n"));
    if (opens)
    {
        // The parent closed itself, so it opens for its first child and
        // closes on a line of its own.
        const std::string own = indentAt((size_t)node.offset_debug() - 1);
        splice({ at, length }, ">" + eol + indent + block + eol + own + "</" + std::string(node.name()) + ">");
        landed(at + 1 + eol.size() + indent.size() + lead + 1);
        return true;
    }
    splice({ at, 0 }, eol + indent + block);
    landed(at + eol.size() + indent.size() + lead + 1);
    return true;
}

bool ALXUIEdit::removeElement(const path_t& path)
{
    Step step(*this);
    mError.clear();
    note(Did::RemovedElement, path);
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }
    Span body;
    Span whole;
    if (!extentOf(node, body, whole))
    {
        mError = "could not read the element";
        return false;
    }
    splice(whole, std::string_view());
    // What is left where it was is what held it.
    if (!path.empty())
    {
        mPending.after.assign(path.begin(), path.end() - 1);
    }
    return true;
}

// A move is the two operations, and the element carries its own text
// between them with the indentation of its old home taken off: the
// insertion puts back the one its new home asks for.
bool ALXUIEdit::liftElement(const path_t& path, std::string& xml) const
{
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        return false;
    }
    Span body;
    Span whole;
    if (!extentOf(node, body, whole))
    {
        return false;
    }

    const size_t start = (size_t)node.offset_debug() - 1;
    const std::string own = indentAt(start);
    xml.assign(mText, start, whole.offset + whole.length - start);
    if (!own.empty())
    {
        const std::string from = "\n" + own;
        for (size_t at = xml.find(from); at != std::string::npos; at = xml.find(from, at + 1))
        {
            xml.erase(at + 1, own.size());
        }
    }
    return true;
}

// A path is a chain of names, and a name that repeats among siblings is
// told apart by which of them it is. Take one away and the ones after it
// in that parent each answer to one fewer.
//
//static
bool ALXUIEdit::rename(const path_t& path, const std::string& name, path_t& moved)
{
    if (!setAttribute(path, "name", name))
    {
        return false;
    }
    // Worked out while the write was being made, when the element's siblings
    // and their names were in front of it.
    moved = mPending.after;
    return true;
}

void ALXUIEdit::afterRenaming(const path_t& renamed, const path_t& to, path_t& other)
{
    if (renamed.empty() || to.size() != renamed.size() || other.size() < renamed.size())
    {
        return;
    }
    const size_t depth = renamed.size() - 1;
    if (renamed[depth] == to[depth])
    {
        return;     // called something it is already called
    }
    for (size_t i = 0; i < depth; ++i)
    {
        if (renamed[i] != other[i])
        {
            return; // a different branch, so nothing here moved
        }
    }

    // The element itself, and so everything under it.
    if (other[depth] == renamed[depth])
    {
        other[depth] = to[depth];
        return;
    }

    std::string_view was_name;
    std::string_view now_name;
    std::string_view mine_name;
    S32 was_ordinal = 0;
    S32 now_ordinal = 0;
    S32 mine_ordinal = 0;
    ALXUISelection::splitOrdinal(renamed[depth], was_name, was_ordinal);
    ALXUISelection::splitOrdinal(to[depth], now_name, now_ordinal);
    ALXUISelection::splitOrdinal(other[depth], mine_name, mine_ordinal);

    // One fewer of the name it left behind.
    if (mine_name == was_name && mine_ordinal > was_ordinal)
    {
        other[depth] = ALXUISelection::step(mine_name, mine_ordinal - 1);
        return;
    }
    // And one more of the name it took, from where it now sits among them.
    if (mine_name == now_name && mine_ordinal >= now_ordinal)
    {
        other[depth] = ALXUISelection::step(mine_name, mine_ordinal + 1);
    }
}

void ALXUIEdit::afterRemoving(const path_t& removed, path_t& other)
{
    const size_t depth = removed.size() - 1;
    if (removed.empty() || other.size() <= depth)
    {
        return;
    }
    for (size_t i = 0; i < depth; ++i)
    {
        if (removed[i] != other[i])
        {
            return;     // a different branch, so nothing shifts
        }
    }

    std::string_view gone_name;
    std::string_view mine_name;
    S32 gone_ordinal = 0;
    S32 mine_ordinal = 0;
    ALXUISelection::splitOrdinal(removed[depth], gone_name, gone_ordinal);
    ALXUISelection::splitOrdinal(other[depth], mine_name, mine_ordinal);
    if (gone_name != mine_name || mine_ordinal <= gone_ordinal)
    {
        return;
    }
    // The ordinal a step carries is the index, counted from zero, so the
    // first of a name carries none: ALXUISelection::step writes it and
    // splitOrdinal reads it back.
    other[depth] = ALXUISelection::step(mine_name, mine_ordinal - 1);
}

// Whether one path is the other or runs through it, which is the move that
// cannot be made: the removal that starts it takes the destination with
// it, and what was lifted has nowhere to land.
static bool within(const ALXUIEdit::path_t& path, const ALXUIEdit::path_t& under)
{
    return path.size() <= under.size() && std::equal(path.begin(), path.end(), under.begin());
}

bool ALXUIEdit::moveElement(const path_t& path, const path_t& parent)
{
    Step step(*this);
    mError.clear();
    note(Did::MovedElement, path);
    if (path.empty() || within(path, parent))
    {
        mError = "an element cannot be moved into itself";
        return false;
    }
    std::string xml;
    if (!liftElement(path, xml))
    {
        mError = "could not read the element";
        return false;
    }

    path_t landing(parent);
    afterRemoving(path, landing);
    return removeElement(path) && insertElement(landing, xml);
}

bool ALXUIEdit::duplicateElement(const path_t& path)
{
    Step step(*this);
    mError.clear();
    note(Did::AddedElement, path);
    if (path.empty())
    {
        mError = "the root is not a thing to copy";
        return false;
    }
    std::string xml;
    if (!liftElement(path, xml))
    {
        mError = "could not read the element";
        return false;
    }
    return insertBeside(path, xml, false);
}

bool ALXUIEdit::insertBefore(const path_t& sibling, const std::string& xml)
{
    return insertBeside(sibling, xml, true);
}

bool ALXUIEdit::insertAfter(const path_t& sibling, const std::string& xml)
{
    return insertBeside(sibling, xml, false);
}

bool ALXUIEdit::insertBeside(const path_t& sibling, const std::string& xml, bool before)
{
    Step step(*this);
    mError.clear();
    note(Did::AddedElement, sibling);
    pugi::xml_node node = resolve(sibling);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }
    Span body;
    Span whole;
    if (!extentOf(node, body, whole))
    {
        mError = "could not read the element";
        return false;
    }

    const size_t start = (size_t)node.offset_debug() - 1;
    const std::string indent = indentAt(start);
    const std::string eol = mText.find("\r\n") == std::string::npos ? "\n" : "\r\n";

    // The whole of it lands at the sibling's depth, children and all.
    const std::string block = indented(xml, indent);

    const size_t lead = llmin(block.size(), block.find_first_not_of(" \t\r\n"));
    if (before)
    {
        splice({ start, 0 }, block + eol + indent);
        landed(start + lead + 1);
    }
    else
    {
        const size_t at = whole.offset + whole.length;
        splice({ at, 0 }, eol + indent + block);
        landed(at + eol.size() + indent.size() + lead + 1);
    }
    return true;
}

bool ALXUIEdit::moveBefore(const path_t& path, const path_t& sibling)
{
    return moveBeside(path, sibling, true);
}

bool ALXUIEdit::moveAfter(const path_t& path, const path_t& sibling)
{
    return moveBeside(path, sibling, false);
}

bool ALXUIEdit::moveBeside(const path_t& path, const path_t& sibling, bool before)
{
    Step step(*this);
    mError.clear();
    note(Did::MovedElement, path);
    if (path.empty() || within(path, sibling))
    {
        mError = "an element cannot be moved beside itself";
        return false;
    }
    std::string xml;
    if (!liftElement(path, xml))
    {
        mError = "could not read the element";
        return false;
    }

    // The sibling is named as the tree stands, and the removal comes
    // first: a name it shares with what is going answers to one fewer.
    path_t landing(sibling);
    afterRemoving(path, landing);
    return removeElement(path) && insertBeside(landing, xml, before);
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
    Step step(*this);
    mError.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    note(name == "name" ? Did::Renamed : Did::WroteField, path, name);
    if (name == "name")
    {
        noteRename(node, value);
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
    Step step(*this);
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
    note(Did::TookFieldOut, path, name);
    splice(whole, std::string_view());
    return true;
}

bool ALXUIEdit::renameAttribute(const path_t& path, const std::string& from, const std::string& to)
{
    Step step(*this);
    mError.clear();
    if (from == to)
    {
        mError = "that is the name it has";
        return false;
    }
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    Span span;
    Span whole;
    if (!spanOf(node, from, span, whole))
    {
        mError = "the element does not carry " + from;
        return false;
    }
    // Two of a name on one element is one value chosen by which the parser
    // reads last, which is not a thing to leave a file in.
    Span taken;
    Span taken_whole;
    if (spanOf(node, to, taken, taken_whole))
    {
        mError = "the element already carries " + to;
        return false;
    }

    // The name begins at the first byte of the attribute that is not the
    // whitespace separating it from the one before.
    size_t at = whole.offset;
    while (at < mText.size() && isSpace(mText[at]))
    {
        ++at;
    }
    note(Did::SpeltFieldAgain, path, to);
    splice(Span{ at, from.size() }, to);
    return true;
}

// The numbers a move, a resize or a re-author worked out, written one
// attribute each and remembered for the panel that says what was written.
bool ALXUIEdit::writeAll(const path_t& path, const std::vector<std::pair<std::string, S32>>& writes)
{
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
    Step step(*this);
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

    return writeAll(path, writes);
}

// A resize holds the edge the element is positioned from and moves the
// other one: the size when the file names a size, the far edge when it
// names two edges.
bool ALXUIEdit::resize(const path_t& path, S32 dw, S32 dh, const Anchor& now)
{
    Step step(*this);
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

    return writeAll(path, writes);
}

bool ALXUIEdit::reauthor(const path_t& path, const Anchor& want, EAuthor what)
{
    Step step(*this);
    mError.clear();
    mWritten.clear();
    pugi::xml_node node = resolve(path);
    if (!node)
    {
        mError = "no element at that path";
        return false;
    }

    // The ones that stay are written first, so an attribute the element
    // already carries keeps the place it has in the tag and the diff is
    // the numbers rather than the order.
    std::vector<std::pair<std::string, S32>> writes;
    if (what == AUTHOR_RECT)
    {
        writes.emplace_back("left", want.left);
        writes.emplace_back(want.topLeft ? "top" : "bottom", want.topLeft ? want.top : want.bottom);
    }
    writes.emplace_back("width", want.width);
    writes.emplace_back("height", want.height);

    if (!writeAll(path, writes))
    {
        return false;
    }

    // And everything else that positions it comes off. The node is stale
    // after the first splice, so the names are taken before any of them.
    std::vector<std::string> stale;
    for (pugi::xml_attribute attribute : resolve(path).attributes())
    {
        const std::string name(attribute.name());
        if (isGeometryAttribute(name)
            && std::none_of(writes.begin(), writes.end(),
                            [&name](const auto& write) { return write.first == name; }))
        {
            stale.push_back(name);
        }
    }
    for (const std::string& name : stale)
    {
        if (!removeAttribute(path, name))
        {
            return false;
        }
        mWritten.push_back(name);
    }
    return true;
}

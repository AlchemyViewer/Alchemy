/**
 * @file alxuiedit.h
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

#pragma once

#include "stdtypes.h"

#include <pugixml.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ALXmlDocument;

// A XUI file under edit. An operation finds the span of the attribute it
// changes in the file's own text and splices it, so every byte the
// operation does not name is the byte that was already there: comments,
// indentation, attribute order, entity spellings, the declaration and the
// line endings the file was written with. Re-serializing the tree cannot
// promise that, and a file under version control is read as its diff.
//
// The text is reparsed after every operation, so an element is named by
// its name path rather than held as a node.
class ALXUIEdit
{
public:
    using path_t = std::vector<std::string>;

    ALXUIEdit();
    ~ALXUIEdit();

    ALXUIEdit(const ALXUIEdit&) = delete;
    ALXUIEdit& operator=(const ALXUIEdit&) = delete;

    bool loadFile(const std::string& path);
    bool loadBuffer(std::string_view text);

    // Writes the text as it stands, byte for byte, to the file it came
    // from or to another.
    bool save();
    bool saveAs(const std::string& path);

    const std::string& text() const { return mText; }
    // The file as it stands on disk, which is what the edits are against.
    const std::string& saved() const { return mSaved; }
    const std::string& path() const { return mPath; }
    const std::string& error() const { return mError; }
    bool dirty() const { return mDirty; }

    pugi::xml_node root() const;
    pugi::xml_node resolve(const path_t& path) const;

    // The two operations. A value is written escaped and an attribute the
    // element does not carry is added after the last one it does, spaced
    // the way that one is spaced.
    bool setAttribute(const path_t& path, const std::string& name, const std::string& value);
    bool removeAttribute(const path_t& path, const std::string& name);

    // Naming an element is not writing a field of it: a path is made of
    // names, so this moves the element and everything under it as far as
    // anything holding a path is concerned. Where it moved to is answered
    // rather than left to be guessed at, and afterRenaming brings the rest
    // of a caller's paths along.
    bool rename(const path_t& path, const std::string& name, path_t& moved);

    // The element operations. Text is written into the element that holds
    // it, opening a self-closing tag when it has none; an element arrives
    // as the last child of its parent, on its own line, indented the way
    // the children already there are; a move is a removal and an
    // insertion, re-indented for where it lands; a removal takes the
    // whole of the element and the line it sat on.
    bool setText(const path_t& path, const std::string& text);
    bool insertElement(const path_t& parent, const std::string& xml);
    bool moveElement(const path_t& path, const path_t& parent);
    bool removeElement(const path_t& path);

    // Beside a sibling rather than at the end of a parent, taking that
    // sibling's own indentation: order is what a menu, a tab container and
    // a layout stack are, so where among the children is the edit.
    bool insertBefore(const path_t& sibling, const std::string& xml);
    bool insertAfter(const path_t& sibling, const std::string& xml);
    bool moveBefore(const path_t& path, const path_t& sibling);
    bool moveAfter(const path_t& path, const path_t& sibling);

    // --- undo ----------------------------------------------------------------
    // Every operation keeps the text it started from. A XUI file is small
    // -- the largest of them is a third of a megabyte, and the largest
    // anyone lays out by hand is a fifth of that -- so a step is the file
    // as it was rather than a way to walk an operation backwards, which
    // would have to be right about each of them separately.
    //
    // A step is one operation as a caller asked for it: a move is a
    // removal and an insertion, and undoing it puts back both.
    //
    // A step also says what it did, which the text it started from cannot.
    // A caller holding something built from this document can put one field
    // of one element onto what it has already; anything else it has to build
    // again, and a step that changed more than that says so by leaving this
    // empty.
    struct Change
    {
        path_t      path;               // where the element was before it
        path_t      after;              // and where the step leaves it
        std::string field;              // the attribute written
        bool        oneField = false;   // and nothing else was
    };

    bool canUndo() const { return !mUndo.empty(); }
    bool canRedo() const { return !mRedo.empty(); }
    bool undo();
    bool redo();
    void clearHistory();
    size_t undoDepth() const { return mUndo.size(); }

    // What the last undo or redo put back.
    const Change& lastChange() const { return mLastChange; }

    // And where the element it was about is to be found now: an undo leaves
    // it where it was before the step, a redo where the step put it. The two
    // differ only for a step that wrote a name, since a path is made of
    // names -- but a caller that reads the element again has to use this one
    // rather than either end of the record.
    const path_t& lastPath() const { return mLastPath; }

    // What the element at that path now writes for a field, which after an
    // undo is what was in force before the step. False where it writes none.
    bool fieldText(const path_t& path, std::string_view field, std::string& out) const;

    // An attribute's value as the file writes it, entity spellings and
    // all, which is not always what the parser read it as. False when the
    // element does not carry the attribute.
    bool valueText(pugi::xml_node node, std::string_view name, std::string& out) const;

    // What the element occupies now, in the terms the file would have to
    // use to put it there: read only when the attribute an edit needs is
    // not in the file at all, and one has to be written.
    struct Anchor
    {
        S32     left = 0;
        S32     top = 0;        // down from the parent's top, as topleft layout counts
        S32     bottom = 0;     // up from the parent's bottom
        S32     width = 0;
        S32     height = 0;
        bool    topLeft = true; // the layout the element is laid out under
    };

    // Move the element by a delta in view coordinates, x to the right and
    // y up, and resize it by a delta on each axis. The attribute that
    // moves is whichever one the author used -- the rule of
    // LLView::applyXUILayout read backwards -- and no form is ever
    // converted into another.
    bool translate(const path_t& path, S32 dx, S32 dy, const Anchor& now);
    bool resize(const path_t& path, S32 dw, S32 dh, const Anchor& now);

    // What a re-author writes: the four numbers that put an element
    // somewhere, or the two that only size it, for a parent that decides
    // where its children go and reads nothing else.
    enum EAuthor { AUTHOR_RECT, AUTHOR_SIZE };

    // The element's position said outright rather than moved. Every other
    // positioning attribute it carries comes off, because a delta is read
    // before the edge it overrides and a padding is measured from a
    // sibling this element no longer has: a form that meant one thing
    // under one parent means another under the next, and no delta carries
    // an element across that.
    bool reauthor(const path_t& path, const Anchor& want, EAuthor what = AUTHOR_RECT);

    // The attributes the last translate or resize wrote, for the panel
    // that says what an edit is about to do.
    const std::vector<std::string>& lastWritten() const { return mWritten; }

    // Which of these an element carries decides what a move writes.
    static bool isGeometryAttribute(std::string_view name);

    // A path through the tree the removal of another element leaves. A
    // step naming the same element by a later ordinal counts one fewer of
    // them once it is gone, which is what a caller holding a path across
    // a removal has to do to it.
    static void afterRemoving(const path_t& removed, path_t& other);

    // And a path a rename leaves. A name is what a path is made of, so
    // renaming an element moves it and every path that ran through it: the
    // element's own step becomes the new one, a later sibling of the name it
    // left counts one fewer of them, and a later sibling of the name it took
    // counts one more.
    static void afterRenaming(const path_t& renamed, const path_t& to, path_t& other);

    // Bytes to a file, as they are: what save writes with, and what a
    // caller holding an earlier text of its own puts back.
    static bool writeFile(const std::string& path, std::string_view text, std::string& error);

private:
    struct Span
    {
        size_t offset = 0;
        size_t length = 0;
    };

    bool parse();
    void splice(const Span& span, std::string_view text);

    // Holds the depth while one operation runs, so the operations an
    // operation is made of do not each become a step of their own.
    struct Step
    {
        explicit Step(ALXUIEdit& doc);
        ~Step();
        ALXUIEdit& mDoc;
    };
    friend struct Step;

    // The element's own text, with the indentation of where it sits taken
    // off, so it can be written in at another depth.
    bool liftElement(const path_t& path, std::string& xml) const;

    bool insertBeside(const path_t& sibling, const std::string& xml, bool before);
    bool moveBeside(const path_t& path, const path_t& sibling, bool before);

    // The whole of an element in the text, from its '<' through the '>'
    // that closes it, and with the whitespace of the line it sits on.
    bool extentOf(pugi::xml_node node, Span& body, Span& whole) const;

    // From the '<' of a tag to the '>' that ends it, with the quoted
    // values passed over, since an angle bracket inside one is text.
    size_t endOfTag(size_t at, bool& self_closing) const;

    // The indentation of the line an offset sits on.
    std::string indentAt(size_t offset) const;

    // The bytes a tag that closes itself ends with, and the whitespace
    // before them.
    Span selfCloseSpan(size_t after_tag) const;

    // Where a child of this element would go, how many bytes it replaces
    // there, the indentation its children carry, and whether the tag has
    // to be opened first.
    bool contentPoint(pugi::xml_node node, size_t& offset, size_t& length, std::string& indent, bool& opens) const;

    // The spans of one attribute of an element: the value between its
    // quotes, and the attribute with the whitespace that precedes it.
    bool spanOf(pugi::xml_node node, std::string_view name, Span& value, Span& whole) const;

    // Where an attribute the element does not carry would go, and the
    // whitespace that separates the one before it.
    bool insertionPoint(pugi::xml_node node, size_t& offset, std::string& separator) const;

    bool addDelta(pugi::xml_node node, std::string_view name, S32 delta, std::vector<std::pair<std::string, S32>>& writes);

    std::string                     mPath;
    std::string                     mText;
    std::string                     mSaved;     // as the file has it
    std::string                     mError;
    std::unique_ptr<ALXmlDocument>  mDoc;
    std::vector<std::string>        mWritten;
    // What this operation is doing, said by the operation itself before it
    // changes anything. Only the outermost one says: a move is a removal and
    // an insertion, and what a move did is neither of them.
    void note(const path_t& path, std::string_view field);

    // And where a step that writes a name leaves the element it renames.
    void noteRename(pugi::xml_node node, const std::string& name);

    std::vector<std::string>        mUndo;
    std::vector<std::string>        mRedo;
    std::vector<Change>             mUndoWhat;      // beside each step
    std::vector<Change>             mRedoWhat;
    Change                          mPending;       // of the operation in hand
    Change                          mLastChange;    // of the last one put back
    path_t                          mLastPath;      // where that leaves it
    S32                             mDepth = 0;     // operations in progress
    bool                            mStepOpen = false;
    bool                            mDirty = false;
};

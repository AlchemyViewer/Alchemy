/**
 * @file alfieldeditors.h
 * @brief The editor a field's type asks for: made, kept up to date, and committed as a file writes it.
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

#include "alcolorfield.h"
#include "alimagefield.h"
#include "alparamtype.h"
#include "llrect.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class LLPanel;
class LLUICtrl;
class LLView;

// The editor a field's type asks for: a check box for a flag, a swatch for
// a colour, a list for a set of names, a spinner for a number, a picture
// for a value that is one, a line for everything else. Made into a row, it
// commits what a file would write for it, and it is kept showing what the
// field says as that changes. The property grid holds its rows' editors
// from here.
class ALFieldEditors
{
public:
    struct Field
    {
        // As a file writes it, dots and all.
        std::string                 name;
        // What the row calls it, where that is not the name: a caller with
        // the name said once already, over the pane, says something else
        // here rather than the same word smaller. Empty says the name.
        std::string                 label;
        // What is in force, whoever wrote it.
        std::string                 value;
        // Which layer wrote it, or where else it came from: shown as it is
        // given, since only the caller knows what its layers are called.
        std::string                 source;
        // True when the file under edit is the one that wrote it, which is
        // the only case an edit changes in place rather than adds to.
        bool                        authored = false;
        ALParamType::EValue         kind = ALParamType::OTHER;
        // An enumeration's names. A field with any becomes a list.
        std::vector<std::string>    values;
        // What the list shows for each of them, where a file's word is not
        // the one a person would choose by. One per value, in order, or
        // none and the values are shown as they are.
        std::vector<std::string>    valueLabels;
        // A number's range, where the vocabulary knows one: the box refuses
        // what is outside it, and with `slider` the row is a slider over it
        // with the number beside, which is how an opacity or a blur is
        // chosen rather than typed.
        bool                        bounded = false;
        F32                         minimum = 0.f;
        F32                         maximum = 0.f;
        bool                        slider = false;
        // How a number steps and how many decimals it shows. Unset, a whole
        // number steps by one and shows none, and a real steps by a tenth
        // and shows three.
        F32                         step = 0.f;
        S32                         decimals = -1;
        // A value that is a picture as much as numbers, and gets the editor
        // that draws it: four corner radii around a rounded rectangle, an
        // offset as a dot on a pad, a direction as a handle on a dial. The
        // value is still the numbers with spaces between, as a file writes
        // them; the row is as tall as the picture.
        bool                        corners = false;
        bool                        pad = false;
        bool                        dial = false;
        // The C++ type: it decides the colour editor, and it is the rest
        // of the row's tool tip.
        std::string                 type;
        // The value is a set of names with bars between them rather than
        // one name: `values` are the flags, and these two words are what a
        // file writes for all of them and for none.
        bool                        flags = false;
        std::string                 allWord;
        std::string                 noneWord;
        // Which heading the row sits under, as an index into the names
        // given to setGroups. Out of range is the last heading.
        S32                         group = 0;
        // Declared only so a file may write it and be quiet about it: the
        // row is shown, because a file writes it, and shown as doing
        // nothing, because it does nothing.
        bool                        ignored = false;
        // Written by the file and declared by nothing.
        bool                        unknown = false;
        // A name that exists, works, and should not be used: the row is
        // shown because the file writes it, and says so. `instead` is the
        // name to write, where there is one.
        bool                        deprecated = false;
        std::string                 instead;
        // The field that shares this one's row. Left and top are a position
        // and width and height are a size: read on one line each, they are
        // the two rows anybody actually thinks in rather than four. Only the
        // first of a pair names the other, and the named one gets no row of
        // its own.
        std::string                 pairWith;
        // A value that is several numbers rather than one: a vector, a
        // rectangle, a colour written as its parts. One box each, captioned,
        // on the row the field would otherwise have had -- which is what
        // makes a rect readable as a rect instead of as four numbers in a
        // string somebody has to count.
        //
        // The captions are the caller's, because what the parts are called
        // is a fact about the value and not about how many there are: a
        // vector has an X, a Y and a Z, and a rect has a left, a top, a
        // right and a bottom.
        //
        // The value is the parts joined by a space, which is how a XUI file
        // writes one; it is read back split on spaces or commas, and it is
        // committed joined by a space again. One field, one value, whatever
        // the arity.
        std::vector<std::string>    components;
        // The parts named by a letter beside each box, which a drag on the
        // letter scrubs, rather than by a caption under it: a row one
        // line tall, its boxes as wide as a number needs.
        bool                        lettered = false;
        // Four names, in the order left, bottom, right, top: a field whose
        // value is which edges of its parent a thing is tied to, which is a
        // picture rather than four words. The row it gets is as tall as the
        // picture. Any other count is not one, and the field is edited the
        // way its type says.
        std::vector<std::string>    edges;
        // What the picture is a picture of: the element and the thing it is
        // in. Left empty it draws the rule rather than this element, which
        // is what a caller with nothing on screen to point at should do.
        LLRect                      subject;
        LLRect                      subjectParent;
        // What this field is, in the caller's own words: a sentence about the
        // thing rather than about its type. Said under the row's own tip,
        // because a name repeated from the heading above the pane is a tip
        // that tells nobody anything.
        std::string                 description;
        // Where else this value is written, one line each, as the caller
        // names them. A row with any is marked in the gutter beside its
        // label and this is what the mark says; a row with none leaves the
        // gutter clear, which is what makes a marked one worth looking at.
        std::vector<std::string>    alsoWritten;
    };

    // A field committed: its name, and the value as a file writes it.
    typedef std::function<void(const std::string& name, const std::string& text)> commit_t;
    // What another field of the same element says: a font's size and style
    // are two more attributes, edited on the font's row.
    typedef std::function<std::string(const std::string& name)> value_of_t;
    typedef std::function<bool(const std::string&, LLColor4&)> color_resolver_t;

    // What the editors are handed: how a colour's name resolves and what
    // names there are, the pictures to choose from and the tool that edits
    // one, and what the four edges of a follows field are called.
    void setColorResolver(color_resolver_t resolver) { mColorResolver = std::move(resolver); }
    void setColorChoices(ALColorField::choices_t choices) { mColorChoices = std::move(choices); }
    void setImageChoices(ALImageField::choices_t choices) { mImageChoices = std::move(choices); }
    void setImageEditor(ALImageField::edit_t editor, std::string label)
    {
        mImageEditor    = std::move(editor);
        mImageEditLabel = std::move(label);
    }
    void setEdgeTips(std::vector<std::string> tips) { mEdgeTips = std::move(tips); }

    // The editor for a field, in `box` of `row` and added to it, saying
    // `tip` and committing through `commit`.
    LLUICtrl* make(const Field& field, const LLRect& box, LLPanel* row, const std::string& tip, const commit_t& commit,
                   const value_of_t& value_of) const;
    // A box per part, captioned, all of them committing the whole value.
    void makeComponents(const Field& field, const LLRect& box, LLView* row, const commit_t& commit) const;
    // The editor under `within` showing what the field says now, unless it
    // is being used.
    static void refresh(LLView* within, const Field& field);

    // The types worth a swatch rather than a spelling.
    static bool isColorType(std::string_view type);

    // The line under each of them saying which part it is.
    static constexpr S32 CAPTION_HEIGHT = 12;

private:
    color_resolver_t            mColorResolver;
    ALColorField::choices_t     mColorChoices;
    ALImageField::choices_t     mImageChoices;
    ALImageField::edit_t        mImageEditor;
    std::string                 mImageEditLabel;
    std::vector<std::string>    mEdgeTips;
};

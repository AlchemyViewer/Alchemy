/**
 * @file alpropertygrid.h
 * @brief One row per field of a widget, with the editor its type asks for.
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

#include "alparamtype.h"
#include "llpanel.h"

class LLAccordionCtrl;
class LLAccordionCtrlTab;
class LLTextBox;

#include <string>
#include <vector>

#include <boost/signals2.hpp>

// Every field a widget answers to, with the editor its type asks for: a
// check box for a flag, a swatch for a colour, a list for a set of names, a
// spinner for a number, a line for everything else. What a field is comes
// from the schema, so the grid knows nothing about widgets and everything
// about types.
//
// Rows are shown under headings the caller names, in the order the caller
// gives, because which fields belong together is a fact about the
// vocabulary and not about the types: a grid that sorted its own rows would
// have to know that `left` and `top_pad` are the same subject. Each heading
// is an accordion tab, which is what folds it, scrolls it and draws it the
// way every other folding list in the viewer is drawn. A filter narrows the
// rows to the names that match, since a widget answers to eighty fields and
// an author is looking for one.
//
// The grid holds no view and no document. It is given a list of fields and
// says which one was committed and to what; putting that into a file is
// the caller's, and so is deciding what the file then looks like.
class ALPropertyGrid : public LLPanel
{
public:
    AL_VIEW_TYPE(ALPropertyGrid, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32>   row_height;
        Optional<S32>   label_width;
        Optional<S32>   source_width;
        Params();
    };

    struct Field
    {
        // As a file writes it, dots and all.
        std::string                 name;
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
    };

    // name and the value committed, as the file would write it.
    typedef boost::signals2::signal<void(const std::string&, const std::string&)> commit_signal_t;

    void setFields(std::vector<Field> fields);
    void clearFields();
    const std::vector<Field>& fields() const { return mFields; }

    // The headings, in the order they are shown. Which one a field sits
    // under is the field's own; a grid given none shows one unheaded list.
    // Set once, before the fields: changing them forgets which were folded.
    void setGroups(std::vector<std::string> groups);

    // Only the fields whose name carries this, ignoring case. Empty shows
    // every field, and a heading with nothing left under it goes too.
    void setFilter(const std::string& text);

    // What is said when there are no rows, which is three different things:
    // nothing is selected, the element writes nothing, or the filter matches
    // nothing. Given rather than written here, because this library has no
    // file for a translator to open.
    void setNotices(std::string nothing_selected, std::string nothing_written, std::string no_match);

    boost::signals2::connection onFieldCommit(const commit_signal_t::slot_type& cb)
    {
        return mFieldCommit.connect(cb);
    }

    // Only the fields the file writes, which is the short list an author
    // works in; off shows every field the tag answers to.
    void setAuthoredOnly(bool only);
    bool authoredOnly() const { return mAuthoredOnly; }

    // The leaves of the blocks a field carries -- `font.name`, `bg_alpha
    // _color.alpha` -- as well as the fields themselves. Off by default,
    // because a widget's own vocabulary is a page and the whole tree is
    // five, and a nested leaf the file actually writes is shown either way.
    void setNested(bool nested);
    bool nested() const { return mNested; }

    // A row is laid out to the width it was given, so a change of width is
    // a re-layout of the rows and not a stretch of them.
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALPropertyGrid(const Params& p);
    ~ALPropertyGrid() override;

private:
    // One heading and the rows under it. The tab is what folds and the
    // panel is what the rows are in, and both outlive a rebuild, so
    // choosing another widget does not unfold everything again.
    struct Section
    {
        LLAccordionCtrlTab* tab = nullptr;
        LLPanel*            rows = nullptr;
    };

    void rebuild();
    void addRow(LLPanel* host, const Field& field, S32 top, bool shaded);
    bool shows(const Field& field) const;
    // What another field of the same widget says, for an editor that is
    // for more than one attribute.
    std::string valueOf(const std::string& name) const;
    S32 countShown(S32 group) const;
    // Where the columns are, given how wide the rows have been made.
    S32 editorLeft() const;
    S32 editorWidth(S32 width) const;

    std::vector<Field>          mFields;
    std::vector<std::string>    mGroups;
    std::vector<Section>        mSections;
    LLAccordionCtrl*            mAccordion = nullptr;
    LLTextBox*                  mEmpty = nullptr;
    std::string                 mFilter;
    std::string                 mNothingSelected;
    std::string                 mNothingWritten;
    std::string                 mNoMatch;
    commit_signal_t             mFieldCommit;
    S32                         mRowHeight;
    S32                         mLabelWidth;
    S32                         mSourceWidth;
    bool                        mAuthoredOnly = false;
    bool                        mNested = false;
    bool                        mRebuilding = false;    // rebuild reshapes; that is not a resize
    // False where every row would say the same thing, which is a column
    // of one repeated word.
    bool                        mShowSource = true;
};

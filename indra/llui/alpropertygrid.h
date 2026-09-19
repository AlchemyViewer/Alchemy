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

#include "alcolorfield.h"
#include "aldeferredrebuild.h"
#include "alimagefield.h"
#include "alparamtype.h"
#include "llpanel.h"
#include "v4color.h"

#include <functional>

class ALEmptyState;
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
        // What the way back on a row is called. One letter where the rows
        // are many and the label is beside each; a word where there is one
        // row and a letter on its own is a button nobody can name.
        Optional<std::string> remove_label;
        Params();
    };

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

    // name and the value committed, as the file would write it.
    typedef boost::signals2::signal<void(const std::string&, const std::string&)> commit_signal_t;

    void setFields(std::vector<Field> fields);
    // The same rows with new values: what each field says now replaces
    // what it said, and every editor that is not being dragged or typed
    // in shows it, but no row is rebuilt -- a slider mid-drag and a
    // popover mid-pick keep the control they belong to. False when the
    // rows would differ, and then setFields is the answer.
    bool updateFields(std::vector<Field> fields);
    void clearFields();
    const std::vector<Field>& fields() const { return mFields; }

    // A field's standing changed without the fields changing hands: the
    // file writes it now, or has stopped, which is what the first commit
    // on an unwritten row does. The row is told rather than made again,
    // so the keyboard stays in whichever of its boxes it was in.
    void setAuthored(const std::string& name, bool authored, const std::string& source);

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
    // file for a translator to open -- and each is a headline saying what is
    // missing, a sentence saying how to get some, and, where one thing would
    // fix it, the button that does that thing.
    struct Notice
    {
        std::string headline;
        std::string sentence;
        std::string action;
    };
    void setNotices(Notice nothing_selected, Notice nothing_written, Notice no_match);

    // The button on whichever notice is showing, pressed.
    typedef boost::signals2::signal<void()> notice_signal_t;
    boost::signals2::connection onNoticeAction(const notice_signal_t::slot_type& cb);

    // The words a row is explained in, for the same reason: each may carry
    // [NAME], [TYPE], [SOURCE] and [INSTEAD] -- the field's own name, the
    // C++ type behind it, the layer that wrote what is in force, and the
    // name to write in place of a deprecated one. A row's label, its
    // editor and its way back all say one of these, so hovering anywhere
    // along a row answers the same question.
    struct Tips
    {
        std::string field;          // a row whose type is not worth saying
        std::string fieldTyped;     // and one where it is
        std::string ignored;        // read off every widget and thrown away
        std::string unknown;        // written by the file, declared by nothing
        std::string source;         // added where a row knows which layer wrote it
        std::string unwritten;      // added where nobody wrote it and it is in force anyway
        std::string deprecated;     // added where the name works and should not be used
        std::string deprecatedFor;  // and where there is a name to use instead
        std::string remove;         // the way back, on the rows that have one
    };
    void setTips(Tips tips);

    // The words the picture of a set of edges explains itself in: six, in
    // the order a field's `edges` are given. Set on the grid rather than on
    // each field, because every such field means the same thing by them.
    void setEdgeTips(std::vector<std::string> tips);

    // Asked first what a colour's text comes to, on every colour row, for
    // a caller whose files write colours colors.xml knows nothing about.
    typedef std::function<bool(const std::string&, LLColor4&)> color_resolver_t;
    void setColorResolver(color_resolver_t resolver);
    // The caller's own colour names, offered first by every colour row's
    // popover: a theme value, with the colour it comes to.
    void setColorChoices(ALColorField::choices_t choices);

    // The pictures a picture row's popover offers, and the tool that edits
    // one by name with what its button says. A grid given neither shows a
    // picture row as its name and its picture, with no popover to open.
    void setImageChoices(ALImageField::choices_t choices);
    void setImageEditor(ALImageField::edit_t editor, std::string label);

    boost::signals2::connection onFieldCommit(const commit_signal_t::slot_type& cb)
    {
        return mFieldCommit.connect(cb);
    }

    // The way back: this file writes this field, take it out again and let
    // whatever was in force before be in force. Only a row the file wrote is
    // offered it, because it is the only row there is anything to take out
    // of.
    typedef boost::signals2::signal<void(const std::string&)> remove_signal_t;
    boost::signals2::connection onFieldRemove(const remove_signal_t::slot_type& cb)
    {
        return mFieldRemove.connect(cb);
    }

    // The gutter mark beside a row, clicked. What is on the other side of
    // it is the caller's: which layers write this, and the offer to add one
    // -- neither of which this library knows anything about.
    // The mark beside a row, for a caller that wants to put something beside
    // it: what a gutter click opens has to know where it was clicked. Null
    // for a row with no mark, which is most of them.
    LLView* gutterFor(const std::string& name) const;

    typedef boost::signals2::signal<void(const std::string&)> gutter_signal_t;
    boost::signals2::connection onFieldGutter(const gutter_signal_t::slot_type& cb)
    {
        return mFieldGutter.connect(cb);
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

    // A section nothing is written in arrives folded, so what is written
    // is what is in front of you: right for a file of fields, most of
    // them at their defaults. A grid whose every row is a choice somebody
    // made -- a shape's layer, where nothing is a default -- turns this
    // off, and every section arrives open. Set before the groups.
    void setFoldsUnwritten(bool folds);
    bool foldsUnwritten() const { return mFoldsUnwritten; }

    // Rows in the order they were given, within each section, instead of
    // the written ones first and the rest by name: a vocabulary has an
    // order of its own, and a row that jumps to the top of its section
    // when it is first edited is a row that moves under the hand. Set
    // before the fields.
    void setKeepsOrder(bool keeps);
    bool keepsOrder() const { return mKeepsOrder; }

    // A row is laid out to the width it was given, so a change of width is
    // a re-layout of the rows and not a stretch of them.
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALPropertyGrid(const Params& p);
    ~ALPropertyGrid() override;

private:
    // The mark in the gutter beside a row.
    class Mark;

    // The panel a section's rows live in. It stacks them from its own top
    // every time it is reshaped, because an accordion tab sizes the view it
    // holds to the tab and not to the view: a row placed at an absolute
    // position inside a panel that is later made taller is a row that has
    // moved, and only a full rebuild put it back.
    class Rows;

    // One heading and the rows under it. The tab is what folds and the
    // panel is what the rows are in, and both outlive a rebuild, so
    // choosing another widget does not unfold everything again.
    struct Section
    {
        LLAccordionCtrlTab* tab = nullptr;
        Rows*               rows = nullptr;
    };

    void rebuild();
    // The fields in the order the rows take.
    void order(std::vector<Field>& fields) const;
    // Whether two fields would make the same row.
    static bool sameRow(const Field& a, const Field& b);
    // The row's ink and tips for what the field says about itself now.
    void refreshAuthored(const Field& field);
    // The editor showing what the field says now, unless it is being used.
    void refreshEditor(const Field& field);
    void addRow(Rows* host, const Field& field, const Field* partner, bool shaded);
    // The way back, at the right end of the row; shown on the rows the
    // file writes.
    void addRemove(LLPanel* row, const Field& field, const Field* partner);
    // The row a field is on: its own, or the one it shares with the field
    // that names it as a partner.
    LLPanel* rowOf(const std::string& name) const;
    const Field* fieldNamed(const std::string& name) const;
    LLUICtrl* makeEditor(const Field& field, const LLRect& box, LLPanel* row);
    // A box per part, captioned, all of them committing the whole value.
    void makeComponents(const Field& field, const LLRect& box, LLPanel* row);
    // The field this one shares its row with, where it names one that is
    // shown, in the same section, and not filtered away.
    const Field* partnerOf(const Field& field) const;
    // Whether some shown field has named this one as its partner, in
    // which case it is on that row and needs none of its own.
    bool isPartnered(const Field& field) const;
    bool shows(const Field& field) const;
    // What a row says about itself, wherever along it the pointer is.
    std::string tipFor(const Field& field) const;
    // How tall a field's row is: one row, or as tall as the picture it
    // carries instead of a value.
    S32 rowHeight(const Field& field) const;
    // What the rows of a section come to, which is not the count of them
    // times anything.
    S32 sectionHeight(S32 group) const;
    // What another field of the same widget says, for an editor that is
    // for more than one attribute.
    std::string valueOf(const std::string& name) const;
    S32 countShown(S32 group) const;
    // How wide a row may be: the accordion's width, less everything
    // between it and the row -- including the scrollbar's column, which
    // the accordion draws over its tabs rather than beside them.
    S32 rowWidth() const;
    // Where the columns are, given how wide the rows have been made.
    S32 editorLeft() const;
    S32 editorWidth(S32 width) const;

    std::vector<Field>          mFields;
    std::vector<std::string>    mGroups;
    color_resolver_t            mColorResolver;
    ALColorField::choices_t     mColorChoices;
    ALImageField::choices_t     mImageChoices;
    ALImageField::edit_t        mImageEditor;
    std::string                 mImageEditLabel;
    std::vector<Section>        mSections;
    LLAccordionCtrl*            mAccordion = nullptr;
    ALEmptyState*               mEmpty = nullptr;
    std::string                 mFilter;
    Tips                        mTips;
    std::vector<std::string>    mEdgeTips;
    Notice                      mNothingSelected;
    Notice                      mNothingWritten;
    Notice                      mNoMatch;
    commit_signal_t             mFieldCommit;
    remove_signal_t             mFieldRemove;
    gutter_signal_t             mFieldGutter;
    S32                         mRowHeight;
    S32                         mLabelWidth;
    std::string                 mRemoveLabel;
    S32                         mRemoveWidth;
    bool                        mAuthoredOnly = false;
    bool                        mNested = false;
    bool                        mFoldsUnwritten = true;
    bool                        mKeepsOrder = false;
    // A rebuild deletes every row, so one asked for from inside a row's own
    // callback -- a caller answering a commit by filling the grid again,
    // which is what XUI Studio does after any edit it cannot apply in place
    // -- is held until the callback is over, instead of deleting the editor
    // that is still on the stack mid-commit.
    ALDeferredRebuild           mRebuild;
};

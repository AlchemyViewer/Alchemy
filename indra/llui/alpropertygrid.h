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

#include <string>
#include <vector>

#include <boost/signals2.hpp>

// Every field a widget answers to, with the editor its type asks for: a
// check box for a flag, a list for a set of names, a spinner for a number,
// a line for everything else. What a field is comes from the schema, so
// the grid knows nothing about widgets and everything about types.
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
        // The C++ type, for the row's tool tip.
        std::string                 type;
    };

    // name and the value committed, as the file would write it.
    typedef boost::signals2::signal<void(const std::string&, const std::string&)> commit_signal_t;

    void setFields(std::vector<Field> fields);
    void clearFields();
    const std::vector<Field>& fields() const { return mFields; }

    boost::signals2::connection onFieldCommit(const commit_signal_t::slot_type& cb)
    {
        return mFieldCommit.connect(cb);
    }

    // Only the fields the file writes, which is the short list an author
    // works in; off shows every field the tag answers to.
    void setAuthoredOnly(bool only);
    bool authoredOnly() const { return mAuthoredOnly; }

    // How tall the rows come to, so a scroll container can be told.
    S32 contentHeight() const;

protected:
    friend class LLUICtrlFactory;
    ALPropertyGrid(const Params& p);
    ~ALPropertyGrid() override;

private:
    void rebuild();
    void addRow(const Field& field, S32 top);
    void onRowCommit(const std::string& name, const LLSD& value);

    std::vector<Field>  mFields;
    commit_signal_t     mFieldCommit;
    S32                 mRowHeight;
    S32                 mLabelWidth;
    S32                 mSourceWidth;
    bool                mAuthoredOnly = true;
};

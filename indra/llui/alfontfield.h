/**
 * @file alfontfield.h
 * @brief A font as a XUI file writes one: a name, a size and three flags.
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

#include "lluictrl.h"

#include <string>

#include <boost/signals2.hpp>

class LLFloater;
class LLFontGL;
class LLLineEditor;

// A font in XUI is three attributes -- `font`, `font.size` and
// `font.style` -- and the vocabulary of all three lives in fonts.xml,
// which is to say that an author is expected to have read another file.
// So this shows what the three come to, drawn in the font they name, and
// opens the three lists over whatever is under it.
//
// The popover is a floater of its own, closed by clicking away or by
// escape, because a field in a property grid inside a scroll container has
// no room to drop a list. Choosing is not committing: the three are picked
// against a preview and written when the popover goes, so one visit to it
// is one change to the file rather than three.
class ALFontField : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALFontField, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<S32> sample_width;
        Params();
    };

    // The name, which is what `font=` writes. The other two are their own,
    // because they are their own attributes.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    void setSize(const std::string& size);
    void setStyle(const std::string& style);
    const std::string& size() const { return mSize; }
    const std::string& style() const { return mStyle; }

    // Which of the three changed and what it now says: the part is empty
    // for the name, and otherwise `size` or `style`, which are the names a
    // file writes after the dot. Only what changed is reported.
    typedef boost::signals2::signal<void(const std::string&, const std::string&)> part_signal_t;
    boost::signals2::connection onPartCommit(const part_signal_t::slot_type& cb)
    {
        return mPartCommit.connect(cb);
    }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;

    ~ALFontField() override;

protected:
    friend class LLUICtrlFactory;
    ALFontField(const Params& p);

private:
    void openPopover();
    void closePopover();
    // What the popover settled on, written as the three attributes it is.
    void apply(const std::string& name, const std::string& size, const std::string& style);
    void onTextCommit();
    void refreshText();

    std::string             mName;
    std::string             mSize;
    std::string             mStyle;
    S32                     mSampleWidth;
    // The face the three parts name, and the parts it was found for.
    const LLFontGL*         mFont = nullptr;
    std::string             mFontOf;
    LLLineEditor*           mEditor = nullptr;
    part_signal_t           mPartCommit;
    LLHandle<LLFloater>     mPopover;
};

/**
 * @file alcolorfield.h
 * @brief A colour as a XUI file writes one: a swatch that opens the names.
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
#include "lluicolor.h"

#include <functional>
#include <string>
#include <vector>

class ALPopover;
class LLLineEditor;

// What a XUI file writes for a colour is almost always a name out of
// colors.xml, and sometimes four numbers. So this holds the text, shows
// the colour it comes to, and opens the names over whatever is under it:
// a swatch, the text beside it, and a list that filters as it is typed.
//
// The popover is a floater of its own, closed by choosing, by clicking
// away or by escape, so it is not clipped by the pane the field sits in --
// a property grid inside a scroll container has no room to drop a list.
class ALColorField : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALColorField, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<S32> swatch_width;
        Params();
    };

    // The text as the file writes it: a name, or a comma-separated list of
    // numbers. Committing gives back the same kind of text.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    // What the text comes to on screen, and whether it comes to anything:
    // a name colors.xml does not carry is shown as itself and drawn as
    // nothing, which is the lint's finding made visible.
    bool resolved(LLColor4& color) const;

    // What a text comes to without a resolver: a name the colour table
    // has, or three or four numbers. False for anything else.
    static bool textToColor(const std::string& text, LLColor4& color);

    // Asked first what a text comes to, for a caller whose files write
    // colours colors.xml knows nothing about; the names and the numbers
    // are tried after it says no. The popover asks the same.
    typedef std::function<bool(const std::string&, LLColor4&)> resolver_t;
    void setResolver(resolver_t resolver);

    // Names of the caller's own, offered in the popover before the
    // table's: a theme value written as $Name, with the colour it comes
    // to. Asked when the popover opens, since what they come to
    // changes.
    struct Choice
    {
        std::string name;
        LLColor4    color;
    };
    typedef std::function<std::vector<Choice>()> choices_t;
    void setChoices(choices_t choices);

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;

    ~ALColorField() override;

protected:
    friend class LLUICtrlFactory;
    ALColorField(const Params& p);

private:
    void openPopover();
    void closePopover();
    void chose(const std::string& name);
    void onTextCommit();

    std::string             mText;
    // What the text comes to, worked out when it changes, or when the
    // table a name is looked up in does.
    mutable LLColor4        mColor;
    mutable bool            mHasColor = false;
    S32                     mSwatchWidth;
    LLLineEditor*           mEditor = nullptr;
    LLHandle<ALPopover>     mPopover;
    resolver_t              mResolver;
    choices_t               mChoices;
    // The colour table as it was when the text was last resolved: a name
    // means something else once the table has changed.
    mutable U32             mSeenGeneration = 0;

    void resolveText() const;

    void setText(const std::string& text);
};

/**
 * @file alimagefield.h
 * @brief A field whose value is the name of a picture, shown as the picture
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
#include "lluiimage.h"

#include <functional>
#include <string>
#include <vector>

class ALPopover;
class LLLineEditor;

// What a XUI file writes for a picture is the name of a UI image, a file
// among the skin's textures. So this
// holds the name, shows the picture it comes to beside it, and opens the
// pictures over whatever is under it -- each drawn as itself under its
// name, since a picture is known by sight before it is known by name --
// with a way through to whatever tool edits it.
//
// The names offered and the tool are the caller's: this library draws a
// picture by name and knows nothing about where the names come from.
class ALImageField : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALImageField, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<S32> swatch_width;
        Params();
    };

    // The name as the file writes it. Committing gives back a name.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    // A name that is offered, and the heading it is offered under: the
    // skin's shapes under one, its textures under another.
    struct Choice
    {
        std::string name;
        std::string heading;
    };
    typedef std::function<std::vector<Choice>()> choices_t;
    // Asked when the popover opens, since what a skin draws changes.
    void setChoices(choices_t choices);

    // A tool that edits a picture by name, and what its button says.
    typedef std::function<void(const std::string&)> edit_t;
    void setEditor(edit_t editor, std::string label);

    // Whether the name comes to a picture at all.
    bool resolved() const { return mImage.notNull(); }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;

    ~ALImageField() override;

protected:
    friend class LLUICtrlFactory;
    ALImageField(const Params& p);

private:
    void openPopover();
    void closePopover();
    void chose(const std::string& name);
    void onTextCommit();
    void setName(const std::string& name);

    std::string             mName;
    LLPointer<LLUIImage>    mImage;
    S32                     mSwatchWidth;
    LLLineEditor*           mEditor = nullptr;
    LLHandle<ALPopover>     mPopover;
    choices_t               mChoices;
    edit_t                  mEdit;
    std::string             mEditLabel;
};

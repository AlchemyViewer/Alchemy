/**
 * @file alflagsfield.h
 * @brief A set of flags as a XUI file writes one: names with bars between them.
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
#include <vector>

class LLCheckBoxCtrl;

// `follows="left|top"` is four independent answers written as one word, and
// so is `font.style="BOLD|ITALIC"`. A box each says that, where a line of
// text says only that somebody has to spell it: which names exist, how they
// are joined, and which of them are in force are all one reading of a string
// otherwise.
//
// Two words stand for all of them and for none, because that is what files
// write when they can -- `follows="all"`, `font.style="NORMAL"` -- and a tool
// that spelled those out instead would rewrite half the tree the first time
// anyone touched a widget.
class ALFlagsField : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALFlagsField, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Params() {}
    };

    // The names, in the order they are shown and written, and the two words
    // that stand for every one and for none. Either word may be empty, and
    // then the list is written out instead.
    void setFlags(std::vector<std::string> names, std::string all_word, std::string none_word);

    // The value as a file writes it, read against the names given: a name
    // the list does not carry is passed over, which is what the parsers do.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

protected:
    friend class LLUICtrlFactory;
    ALFlagsField(const Params& p);

private:
    void rebuild();
    void onToggle();

    std::vector<std::string>        mNames;
    std::vector<bool>              mSet;
    std::vector<LLCheckBoxCtrl*>    mBoxes;
    std::string                     mAll;
    std::string                     mNone;
};

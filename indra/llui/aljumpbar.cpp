/**
 * @file aljumpbar.cpp
 * @brief A path, where every step of it offers the steps it could have been.
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

#include "aljumpbar.h"

#include "llbutton.h"
#include "llfontgl.h"
#include "llflyoutbutton.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALJumpBar> r("jump_bar");

namespace
{
    constexpr S32 CRUMB_PAD = 12;
    constexpr S32 CRUMB_GAP = 2;
    constexpr S32 TRAILER_GAP = 6;
    // What a crumb with somewhere to go adds for the mark that says so.
    constexpr S32 ARROW = 10;
    const char* const FOLD_LABEL = "\xe2\x80\xa6";   // a single character, not three dots
}

ALJumpBar::Params::Params()
:   trailer("trailer")
{
}

ALJumpBar::ALJumpBar(const Params& p)
:   LLPanel(p),
    mTrailerText(p.trailer),
    mRebuild([this]() { build(); })
{
}

ALJumpBar::~ALJumpBar() = default;

void ALJumpBar::setPath(std::vector<Crumb> crumbs)
{
    mCrumbs = std::move(crumbs);
    mRebuild.request();
}

void ALJumpBar::setTrailer(const std::string& text)
{
    mTrailerText = text;
    mRebuild.request();
}

S32 ALJumpBar::widthOf(const Crumb& crumb) const
{
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    return font->getWidth(crumb.label) + CRUMB_PAD
         + (crumb.alternatives.empty() ? 0 : ARROW);
}

// A path longer than the room folds from the front: the end of a path says
// more about where you are than the start does.
size_t ALJumpBar::folded() const
{
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const S32 trailer = mTrailerText.empty()
                      ? 0 : font->getWidth(mTrailerText) + TRAILER_GAP;
    const S32 fold = font->getWidth(FOLD_LABEL) + CRUMB_PAD + ARROW + CRUMB_GAP;
    const S32 room = getRect().getWidth() - trailer;

    S32 total = 0;
    for (const Crumb& crumb : mCrumbs)
    {
        total += widthOf(crumb) + CRUMB_GAP;
    }

    size_t first = 0;
    while (first + 1 < mCrumbs.size() && total + (first ? fold : 0) > room)
    {
        total -= widthOf(mCrumbs[first]) + CRUMB_GAP;
        ++first;
    }
    return first;
}

void ALJumpBar::build()
{
    for (LLView* part : mParts)
    {
        removeChild(part);
        delete part;
    }
    mParts.clear();
    if (mTrailer)
    {
        removeChild(mTrailer);
        delete mTrailer;
        mTrailer = nullptr;
    }

    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const S32 height = getRect().getHeight();
    const size_t first = folded();
    S32 x = 0;

    // A crumb that could have been something else is a button with an arrow
    // on it: press the label to go there, press the arrow to see what else.
    // That is what a flyout button is, and it was already here.
    const auto crumb = [&](size_t at, const std::string& label,
                           const std::vector<std::pair<std::string, std::string> >& others,
                           const std::string& own, const std::string& tip)
    {
        const S32 width = font->getWidth(label) + CRUMB_PAD + (others.empty() ? 0 : ARROW);
        if (others.empty())
        {
            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = "crumb_" + std::to_string(at);
            bp.label = label;
            bp.rect = LLRect(x, height, x + width, 0);
            bp.font = font;
            bp.tab_stop = false;
            bp.tool_tip = tip;
            LLButton* made = LLUICtrlFactory::create<LLButton>(bp);
            made->setClickedCallback([this, at, own](LLUICtrl*, const LLSD&)
            {
                chose(at, own);
            });
            addChild(made);
            mParts.push_back(made);
        }
        else
        {
            LLFlyoutButton::Params fp(LLUICtrlFactory::getDefaultParams<LLFlyoutButton>());
            fp.name = "crumb_" + std::to_string(at);
            fp.label = label;
            fp.rect = LLRect(x, height, x + width, 0);
            fp.tab_stop = false;
            fp.tool_tip = tip;
            fp.arrow_button_width = ARROW;
            LLFlyoutButton* made = LLUICtrlFactory::create<LLFlyoutButton>(fp);
            for (const auto& [other_label, other_value] : others)
            {
                made->add(other_label, LLSD(other_value));
            }
            // Pressing the label deselects the list and commits, so an empty
            // answer is the crumb saying itself: going there rather than
            // sideways.
            made->setLabel(label);
            made->setCommitCallback([this, at, own](LLUICtrl* ctrl, const LLSD&)
            {
                const std::string value = ctrl->getValue().asString();
                chose(at, value.empty() ? own : value);
            });
            addChild(made);
            mParts.push_back(made);
        }
        x += width + CRUMB_GAP;
    };

    // The fold is a crumb too, and what it offers is what it swallowed: still
    // a path, read down.
    if (first > 0)
    {
        std::vector<std::pair<std::string, std::string> > swallowed;
        std::string names;
        for (size_t i = 0; i < first && i < mCrumbs.size(); ++i)
        {
            swallowed.emplace_back(mCrumbs[i].label, mCrumbs[i].value);
            names += (names.empty() ? "" : " > ") + mCrumbs[i].label;
        }
        // Its tip is the path it swallowed: names, which are the file's
        // words and not this library's.
        crumb(first - 1, FOLD_LABEL, swallowed, mCrumbs[first - 1].value, names);
    }

    for (size_t i = first; i < mCrumbs.size(); ++i)
    {
        crumb(i, mCrumbs[i].label, mCrumbs[i].alternatives, mCrumbs[i].value,
              mCrumbs[i].toolTip);
    }

    if (!mTrailerText.empty())
    {
        LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        tp.name = "trailer";
        tp.rect = LLRect(x + TRAILER_GAP, height - 3, getRect().getWidth(), 0);
        tp.font = font;
        tp.initial_value = mTrailerText;
        mTrailer = LLUICtrlFactory::create<LLTextBox>(tp);
        addChild(mTrailer);
    }
}

// The value is copied first: what is chosen may be the path being replaced.
void ALJumpBar::chose(size_t at, std::string value)
{
    mRebuild.around([&]() { mChose(at, value); });
}

void ALJumpBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    const bool changed = width != getRect().getWidth();
    LLPanel::reshape(width, height, called_from_parent);
    if (changed)
    {
        // How many crumbs fit is a question about the width, so it is asked
        // again when the width changes and not otherwise.
        mRebuild.request();
    }
}

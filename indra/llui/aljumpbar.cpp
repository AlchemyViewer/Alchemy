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
    if (!mTrailerText.empty())
    {
        mTrailerParts.push_back({ mTrailerText, std::string(), std::string() });
    }
}

ALJumpBar::~ALJumpBar() = default;

void ALJumpBar::setPath(std::vector<Crumb> crumbs)
{
    mCrumbs = std::move(crumbs);
    mRebuild.request();
}

void ALJumpBar::setTrailer(const std::string& text)
{
    std::vector<TrailerPart> parts;
    if (!text.empty())
    {
        parts.push_back({ text, std::string(), std::string() });
    }
    setTrailer(std::move(parts));
}

void ALJumpBar::setTrailer(std::vector<TrailerPart> parts)
{
    // Said on every move of a caret, and the same words most of the time:
    // the bar is built again only for words that have changed.
    if (parts == mTrailerParts)
    {
        return;
    }
    // The same pieces with other words -- the caret's line and column
    // moving -- are the same text boxes with other words in them, where
    // the path still folds where it did: nothing is built again.
    bool same_pieces = mTrailer && parts.size() == mTrailerParts.size() && parts.size() == mTrailerPieces.size();
    for (size_t i = 0; same_pieces && i < parts.size(); ++i)
    {
        same_pieces = parts[i].value == mTrailerParts[i].value && parts[i].toolTip == mTrailerParts[i].toolTip;
    }
    mTrailerParts = std::move(parts);
    mTrailerText.clear();
    for (const TrailerPart& part : mTrailerParts)
    {
        mTrailerText += part.text;
    }
    if (same_pieces && folded() == mFolded)
    {
        layTrailer();
        return;
    }
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
    mTrailerPieces.clear();

    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const S32 height = getRect().getHeight();
    const size_t first = folded();
    mFolded = first;
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

    mPathEnd = x;
    if (!mTrailerText.empty())
    {
        // At the right end, as said: the room past the path is its, and
        // the words sit against its far edge, piece by piece, a piece
        // with somewhere to go pressed like a link.
        LLPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        pp.name               = "trailer";
        pp.rect               = LLRect(0, height, 1, 0);
        pp.background_visible = false;
        pp.mouse_opaque       = false;
        LLPanel* trailer      = LLUICtrlFactory::create<LLPanel>(pp);
        for (size_t i = 0; i < mTrailerParts.size(); ++i)
        {
            const TrailerPart& part = mTrailerParts[i];
            LLTextBox::Params  tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
            tp.name         = "trailer_" + std::to_string(i);
            tp.rect         = LLRect(0, height - 3, 1, 0);
            tp.font         = font;
            tp.font_valign  = LLFontGL::VCENTER;
            tp.tool_tip     = part.toolTip;
            tp.mouse_opaque = !part.value.empty();
            LLTextBox* piece = LLUICtrlFactory::create<LLTextBox>(tp);
            if (!part.value.empty())
            {
                const std::string value = part.value;
                piece->setClickedCallback([this, value](void*) { choseTrailer(value); });
            }
            trailer->addChild(piece);
            mTrailerPieces.push_back(piece);
        }
        mTrailer = trailer;
        addChild(mTrailer);
        layTrailer();
    }
}

void ALJumpBar::layTrailer()
{
    if (!mTrailer)
    {
        return;
    }
    const LLFontGL* font   = LLFontGL::getFontSansSerifSmall();
    const S32       height = getRect().getHeight();
    const S32       left   = mPathEnd + TRAILER_GAP;
    const S32       right  = llmax(left + 1, getRect().getWidth() - CRUMB_PAD);
    mTrailer->setShape(LLRect(left, height, right, 0));
    // The pieces measured one by one, as they are placed: each rounds on
    // its own, and their sum is not the whole's. Each is a pixel wider
    // than its words, for that rounding; the last one's pixel inside.
    S32 total = 0;
    for (const TrailerPart& part : mTrailerParts)
    {
        total += font->getWidth(part.text);
    }
    S32 at = llmax(0, (right - left) - total - 1);
    for (size_t i = 0; i < mTrailerPieces.size() && i < mTrailerParts.size(); ++i)
    {
        const S32 width = font->getWidth(mTrailerParts[i].text);
        mTrailerPieces[i]->setText(mTrailerParts[i].text);
        mTrailerPieces[i]->setShape(LLRect(at, height - 3, at + width + 1, 0));
        at += width;
    }
}

void ALJumpBar::choseTrailer(std::string value)
{
    mRebuild.around([&]() { mTrailerChosen(value); });
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

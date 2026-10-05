/**
 * @file aldiffbar.cpp
 * @brief The bar over a comparison: where the changes are, and the ways through them.
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

#include "aldiffbar.h"

#include "alflatbutton.h"
#include "alsaid.h"
#include "alsurface.h"

#include "llfontgl.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALDiffBar> r("diff_bar");

namespace
{
    constexpr S32 ROW     = 20;
    constexpr S32 PAD     = 2;
    constexpr S32 GAP     = 6;
    constexpr S32 SMALL_W = 22;
}

ALDiffBar::Params::Params()
{
    changeDefault(background_visible, false);
}

ALDiffBar::ALDiffBar(const Params& p)
:   LLPanel(p),
    mBgColor(LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black)),
    mInkColor(LLColor4::white)
{
    LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    tp.name          = "count";
    tp.rect          = LLRect(0, ROW, 100, 0);
    tp.h_pad         = 4;
    tp.v_pad         = 3;
    tp.text_color    = mInkColor;
    tp.font          = LLFontGL::getFontSansSerifSmall();
    tp.use_ellipses  = true;
    tp.follows.flags = FOLLOWS_NONE;
    mCount           = LLUICtrlFactory::create<LLTextBox>(tp);
    addChild(mCount);

    mPreviousButton = flat("previous", "\xE2\x86\x91", false, alSaid("DiffBarPrevious", "Previous change"));
    mNextButton     = flat("next", "\xE2\x86\x93", false, alSaid("DiffBarNext", "Next change"));
    // The ways of showing lit by the view, as it is once it has done what
    // was asked: a press only asks, and turns nothing itself.
    mFoldButton     = flat("fold", "\xE2\x8B\xAF", false, alSaid("DiffBarFold", "Fold away what is the same"));
    mIgnoreButton   = flat("ignore", "\xE2\x90\xA3", false, alSaid("DiffBarIgnore", "What to ignore: whitespace, blank lines, comments, case"));
    mInlineButton   = flat("inline", "\xE2\x96\xA4", false, alSaid("DiffBarInline", "Show the changes inline, in one text"));
    mSwapButton     = flat("swap", "\xE2\x87\x84", false, alSaid("DiffBarSwap", "Swap the sides"));
    mDoneButton     = flat("done", "\xC3\x97", false, alSaid("DiffBarDone", "Back to the text"));
    mTakeBackButton = flat("take_back", "\xE2\x86\xB6", false, alSaid("DiffBarTakeBack", "Take this change back"));
    mPreviousButton->setKey(KEY_F7, MASK_SHIFT);
    mNextButton->setKey(KEY_F7, MASK_NONE);
    mDoneButton->setKey(KEY_ESCAPE, MASK_NONE);
    mPreviousButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mPrevious(); });
    mNextButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mNext(); });
    mFoldButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mFold(); });
    mIgnoreButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { showIgnoreMenu(); });
    mInlineButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mInline(); });
    mSwapButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSwap(); });
    mDoneButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mDone(); });
    mTakeBackButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mTakeBack(); });
    mTakeBackButton->setVisible(false);

    setCount(-1, 0);
    layout();
}

ALFlatButton* ALDiffBar::flat(const std::string& name, const std::string& glyph, bool toggle, const std::string& tip)
{
    ALFlatButton::Params fp;
    fp.name            = name;
    fp.rect            = LLRect(0, ROW, SMALL_W, 0);
    fp.tool_tip        = tip;
    fp.follows.flags   = FOLLOWS_NONE;
    ALFlatButton* made = new ALFlatButton(fp, glyph, toggle, mInkColor, ALSurface::chosen(LLColor4::black, mInkColor.get()));
    addChild(made);
    return made;
}

void ALDiffBar::setFellBack(bool fell_back)
{
    if (mFellBack != fell_back)
    {
        mFellBack = fell_back;
        const S32 total = mTotal;
        mTotal          = -2;
        setCount(mCurrent, total);
    }
}

void ALDiffBar::setCount(S32 current, S32 total)
{
    if (current == mCurrent && total == mTotal)
    {
        return;
    }
    mCurrent = current;
    mTotal   = total;
    std::string said;
    if (total == 0)
    {
        said = alSaid("DiffBarNone", "No changes");
    }
    else if (current >= 0)
    {
        said = alSaid("DiffBarOf", "Change [CURRENT] of [TOTAL]", { { "[CURRENT]", std::to_string(current + 1) }, { "[TOTAL]", std::to_string(total) } });
    }
    else
    {
        said = alSaidCount("DiffBarChanges", total, "[COUNT] change", "[COUNT] changes");
    }
    if (mFellBack)
    {
        // The skin's string loses the blank before it.
        said += " " + alSaid("DiffBarByLines", "\xC2\xB7 by lines where too large to compare by structure");
    }
    mCount->setText(said);
}

std::string ALDiffBar::countSaid() const
{
    return mCount->getText();
}

void ALDiffBar::setSteps(bool previous, bool next)
{
    mPreviousButton->setEnabled(previous);
    mNextButton->setEnabled(next);
}

void ALDiffBar::setFolded(bool folded)
{
    mFoldButton->setToggleState(folded);
}

void ALDiffBar::setIgnoring(bool ignoring)
{
    mIgnoreButton->setToggleState(ignoring);
}

void ALDiffBar::showIgnoreMenu()
{
    if (!mIgnores.checked || !mIgnores.offered || !mIgnores.toggle)
    {
        return;
    }
    // The menu calls back while it is open; the bar may be gone by then.
    const LLHandle<LLPanel>                           self = getHandle();
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("DiffIgnore.Toggle", [self](LLUICtrl*, const LLSD& what) {
        if (ALDiffBar* bar = ALViewType::as<ALDiffBar>(self.get()))
        {
            bar->mIgnores.toggle(what.asString());
        }
    });
    enable.add("DiffIgnore.Checked", [self](LLUICtrl*, const LLSD& what) {
        const ALDiffBar* bar = ALViewType::as<ALDiffBar>(self.get());
        return bar && bar->mIgnores.checked(what.asString());
    });
    enable.add("DiffIgnore.Offered", [self](LLUICtrl*, const LLSD& what) {
        const ALDiffBar* bar = ALViewType::as<ALDiffBar>(self.get());
        return bar && bar->mIgnores.offered(what.asString());
    });
    if (mIgnoreMenu.make("menu_diff_ignore.xml"))
    {
        mIgnoreMenu.show(mIgnoreButton, 0, 0);
    }
}

void ALDiffBar::setInline(bool inline_view)
{
    mInlineButton->setToggleState(inline_view);
}

void ALDiffBar::setSwapped(bool swapped)
{
    mSwapButton->setToggleState(swapped);
}

void ALDiffBar::setDoneShown(bool shown)
{
    mDoneButton->setVisible(shown);
    layout();
}

void ALDiffBar::setTakeBackShown(bool shown)
{
    if (mTakeBackButton->getVisible() != shown)
    {
        mTakeBackButton->setVisible(shown);
        layout();
    }
}

void ALDiffBar::setTakeBackEnabled(bool enabled)
{
    mTakeBackButton->setEnabled(enabled);
}

void ALDiffBar::setColors(const LLColor4& background, const LLColor4& ink)
{
    mBgColor              = ALSurface::ground(background, ink);
    mInkColor             = ink;
    const LLColor4 chosen = ALSurface::chosen(background, ink);
    for (ALFlatButton* glyph : { mPreviousButton, mNextButton, mFoldButton, mIgnoreButton, mInlineButton, mSwapButton, mDoneButton, mTakeBackButton })
    {
        glyph->setInk(ink);
        glyph->setLit(chosen);
    }
    mCount->setColor(ink);
}

S32 ALDiffBar::wantedHeight()
{
    return PAD + ROW + PAD;
}

void ALDiffBar::draw()
{
    const F32    alpha = getDrawContext().mAlpha;
    const LLRect local = getLocalRect();
    gl_rect_2d(local, mBgColor.get() % alpha);
    // A line under it, where the titles start.
    gl_rect_2d(local.mLeft, local.mBottom + 1, local.mRight, local.mBottom, ALSurface::frame(mInkColor.get(), alpha));
    LLPanel::draw();
}

void ALDiffBar::layout()
{
    // The buttons at the right, in ones, twos and threes -- the steps,
    // taking a change back, the ways of showing, done -- and the count over
    // what is left at the left.
    const S32 width = getRect().getWidth();
    const S32 top   = getRect().getHeight() - PAD;
    S32       right = width - PAD;
    const auto place = [&](ALFlatButton* button) {
        button->setShape(LLRect(right - SMALL_W, top, right, top - ROW));
        right -= SMALL_W + 1;
    };
    if (mDoneButton->getVisible())
    {
        place(mDoneButton);
        right -= GAP;
    }
    place(mSwapButton);
    place(mInlineButton);
    place(mIgnoreButton);
    place(mFoldButton);
    right -= GAP;
    if (mTakeBackButton->getVisible())
    {
        place(mTakeBackButton);
        right -= GAP;
    }
    place(mNextButton);
    place(mPreviousButton);
    mCount->setShape(LLRect(PAD, top, llmax(PAD, right - GAP), top - ROW));
}

void ALDiffBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}

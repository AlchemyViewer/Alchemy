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
#include "llslider.h"
#include "llstring.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <vector>

static LLDefaultChildRegistry::Register<ALDiffBar> r("diff_bar");

namespace
{
    constexpr S32 ROW     = 20;
    constexpr S32 PAD     = 2;
    constexpr S32 GAP     = 6;
    constexpr S32 SMALL_W = 22;
    // The slider over the versions of the left.
    constexpr S32 SLIDER_W = 120;
    // The least the count is kept for: "Change 10 of 12", about.
    constexpr S32 COUNT_LEAST = 72;
    // How far the bar gives way when it is narrow (layout): all of it, the
    // count let go, the words as letters, the slider let go.
    constexpr S32 SQUEEZE_MOST = 3;

    // A word's first letter, which stands for it on a narrow bar: its first
    // character as a reader sees one, a letter with its marks or an emoji
    // with what joins it.
    std::string firstLetter(const std::string& word)
    {
        return word.substr(0, utf8str_step_grapheme_forward(word, 0));
    }
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
    mIgnoreButton   = flat("ignore", "\xE2\x90\xA3", false, alSaid("DiffBarIgnore", "What to ignore"));
    mCopyButton     = flat("copy", "\xE2\x9D\x90", false, alSaid("DiffBarCopy", "Copy this change, or the whole comparison as a unified diff"));
    mInlineButton   = flat("inline", "\xE2\x96\xA4", false, alSaid("DiffBarInline", "Show the changes inline, in one text"));
    mSwapButton     = flat("swap", "\xE2\x87\x84", false, alSaid("DiffBarSwap", "Swap the sides"));
    mDoneButton     = flat("done", "\xC3\x97", false, alSaid("DiffBarDone", "Back to the text"));
    mTakeBackButton = flat("take_back", "\xE2\x86\xB6", false, alSaid("DiffBarTakeBack", "Take this change back"));
    mPreviousButton->setKey(KEY_F7, MASK_SHIFT);
    mNextButton->setKey(KEY_F7, MASK_NONE);
    mDoneButton->setKey(KEY_ESCAPE, MASK_NONE);
    // The view takes these keys wherever the keyboard is in it
    // (ALDiffView::handleKeyHere); said on the buttons' tips.
    mFoldButton->setKey('F', MASK_ALT);
    mSwapButton->setKey('S', MASK_ALT);
    mPreviousButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mPrevious(); });
    mNextButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mNext(); });
    mFoldButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mFold(); });
    mIgnoreButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { showIgnoreMenu(); });
    mCopyButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { showCopyMenu(); });
    mInlineButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mInline(); });
    mSwapButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSwap(); });
    mDoneButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mDone(); });
    mTakeBackButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mTakeBack(); });
    mTakeBackButton->setVisible(false);
    mWords[0]     = alSaid("DiffBarTheirs", "Theirs");
    mWords[1]     = alSaid("DiffBarMine", "Mine");
    mWords[2]     = alSaid("DiffBarBoth", "Both");
    mTheirsButton = flat("take_theirs", mWords[0], false, alSaid("DiffBarTheirsTip", "Settle this conflict with the other side's lines"));
    mMineButton   = flat("keep_mine", mWords[1], false, alSaid("DiffBarMineTip", "Settle this conflict keeping your lines"));
    mBothButton   = flat("keep_both", mWords[2], false, alSaid("DiffBarBothTip", "Settle this conflict with your lines, then the other side's"));
    mTheirsButton->setKey('T', MASK_ALT);
    mMineButton->setKey('M', MASK_ALT);
    mBothButton->setKey('B', MASK_ALT);
    mTheirsButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSettle(ALTextMerge::Take::Theirs); });
    mMineButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSettle(ALTextMerge::Take::Ours); });
    mBothButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSettle(ALTextMerge::Take::OursThenTheirs); });
    for (ALFlatButton* word : { mTheirsButton, mMineButton, mBothButton })
    {
        word->setVisible(false);
    }
    mOlderButton = flat("older", "\xE2\x97\x82", false, alSaid("DiffBarOlder", "An older version on the left"));
    mNewerButton = flat("newer", "\xE2\x96\xB8", false, alSaid("DiffBarNewer", "A newer version on the left"));
    mOlderButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseVersion(mVersion - 1); });
    mNewerButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseVersion(mVersion + 1); });
    mOlderButton->setKey(',', MASK_ALT);
    mNewerButton->setKey('.', MASK_ALT);
    LLSlider::Params sp;
    sp.name          = "versions";
    sp.rect          = LLRect(0, ROW, SLIDER_W, 0);
    sp.min_value     = 0.f;
    sp.max_value     = 1.f;
    sp.increment     = 1.f;
    sp.initial_value = 0.f;
    sp.follows.flags = FOLLOWS_NONE;
    sp.tool_tip      = alSaid("DiffBarVersions", "Which version is on the left, oldest to newest");
    mVersions        = LLUICtrlFactory::create<LLSlider>(sp);
    mVersions->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseVersion(ll_round(mVersions->getValueF32())); });
    addChild(mVersions);
    for (LLView* version : std::initializer_list<LLView*>{ mOlderButton, mNewerButton, mVersions })
    {
        version->setVisible(false);
    }

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
        // Why, where it is said.
        mCount->setToolTip(fell_back ? alSaid("DiffBarByLinesTip", "A change too large to compare by structure was compared by lines") : std::string());
        refreshSaid();
    }
}

void ALDiffBar::setCount(S32 current, S32 total)
{
    if (current != mCurrent || total != mTotal)
    {
        mCurrent = current;
        mTotal   = total;
        refreshSaid();
    }
}

void ALDiffBar::refreshSaid()
{
    std::string said;
    if (mTotal == 0)
    {
        said = alSaid("DiffBarNone", "No changes");
    }
    else if (mCurrent >= 0)
    {
        said = alSaid("DiffBarOf", "Change [CURRENT] of [TOTAL]", { { "[CURRENT]", std::to_string(mCurrent + 1) }, { "[TOTAL]", std::to_string(mTotal) } });
    }
    else
    {
        said = alSaidCount("DiffBarChanges", mTotal, "[COUNT] change", "[COUNT] changes");
    }
    // The skin's strings lose the blank before them.
    if (mConflicts > 0)
    {
        said += " " + alSaidCount("DiffBarConflicts", mConflicts, "\xC2\xB7 [COUNT] conflict left", "\xC2\xB7 [COUNT] conflicts left");
    }
    else if (mConflicts == 0)
    {
        said += " " + alSaid("DiffBarConflictsNone", "\xC2\xB7 no conflicts left");
    }
    if (mFellBack)
    {
        said += " " + alSaid("DiffBarByLines", "\xC2\xB7 by lines");
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

void ALDiffBar::showCopyMenu()
{
    if (!mCopies.canCopyChange || !mCopies.copyChange || !mCopies.canCopyDiff || !mCopies.copyDiff)
    {
        return;
    }
    const LLHandle<LLPanel>                           self = getHandle();
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("DiffCopy.Do", [self](LLUICtrl*, const LLSD& what) {
        if (ALDiffBar* bar = ALViewType::as<ALDiffBar>(self.get()))
        {
            what.asString() == "change" ? bar->mCopies.copyChange() : bar->mCopies.copyDiff();
        }
    });
    enable.add("DiffCopy.Enabled", [self](LLUICtrl*, const LLSD& what) {
        const ALDiffBar* bar = ALViewType::as<ALDiffBar>(self.get());
        return bar && (what.asString() == "change" ? bar->mCopies.canCopyChange() : bar->mCopies.canCopyDiff());
    });
    if (mCopyMenu.make("menu_diff_copy.xml"))
    {
        mCopyMenu.show(mCopyButton, 0, 0);
    }
}

void ALDiffBar::setInline(bool inline_view)
{
    // What a press does, as it now is.
    mInlineButton->setToggleState(inline_view);
    mInlineButton->setToolTip(inline_view ? alSaid("DiffBarSideBySide", "Show side by side") : alSaid("DiffBarInline", "Show the changes inline, in one text"));
}

void ALDiffBar::setSwapped(bool swapped)
{
    mSwapButton->setToggleState(swapped);
}

void ALDiffBar::setDoneShown(bool shown)
{
    if (mDoneShown != shown)
    {
        mDoneShown = shown;
        layout();
    }
}

void ALDiffBar::setTakeBackShown(bool shown)
{
    if (mTakeBackShown != shown)
    {
        mTakeBackShown = shown;
        layout();
    }
}

void ALDiffBar::setTakeBackEnabled(bool enabled)
{
    mTakeBackButton->setEnabled(enabled);
}

void ALDiffBar::setMerging(bool merging)
{
    if (mMerging != merging)
    {
        mMerging = merging;
        layout();
    }
    if (!merging && mConflicts >= 0)
    {
        mConflicts = -1;
        refreshSaid();
    }
}

void ALDiffBar::setConflicts(S32 left, bool here)
{
    if (left != mConflicts)
    {
        mConflicts = left;
        refreshSaid();
    }
    for (ALFlatButton* word : { mTheirsButton, mMineButton, mBothButton })
    {
        word->setEnabled(here);
    }
}

void ALDiffBar::setVersions(S32 count, S32 current)
{
    const bool shown = count > 1;
    mVersionCount    = shown ? count : 0;
    mVersion         = shown ? llclamp(current, 0, count - 1) : 0;
    if (shown)
    {
        mVersions->setMaxValue(static_cast<F32>(count - 1));
        mVersions->setValue(static_cast<F32>(mVersion));
        mOlderButton->setEnabled(mVersion > 0);
        mNewerButton->setEnabled(mVersion < count - 1);
    }
    if (shown != mVersionsShown)
    {
        mVersionsShown = shown;
        layout();
    }
}

bool ALDiffBar::stepVersion(S32 delta)
{
    const S32 to = mVersion + delta;
    if (mVersionCount < 2 || delta == 0 || to < 0 || to >= mVersionCount)
    {
        return false;
    }
    chooseVersion(to);
    return true;
}

bool ALDiffBar::focusButton(bool first)
{
    // Left to right as they are placed.
    std::vector<ALFlatButton*> shown;
    for (ALFlatButton* button : { mPreviousButton, mNextButton, mFoldButton, mIgnoreButton, mCopyButton, mInlineButton, mSwapButton, mDoneButton,
                                  mTakeBackButton, mTheirsButton, mMineButton, mBothButton, mOlderButton, mNewerButton })
    {
        if (button->getVisible() && button->getEnabled())
        {
            shown.push_back(button);
        }
    }
    if (shown.empty())
    {
        return false;
    }
    const auto by_left = [](const ALFlatButton* a, const ALFlatButton* b) { return a->getRect().mLeft < b->getRect().mLeft; };
    ALFlatButton* to   = first ? *std::min_element(shown.begin(), shown.end(), by_left) : *std::max_element(shown.begin(), shown.end(), by_left);
    to->setFocus(true);
    return true;
}

void ALDiffBar::chooseVersion(S32 version)
{
    // The slider says where it was let go, which may be where it was.
    if (mVersionCount < 2 || version < 0 || version >= mVersionCount || version == mVersion)
    {
        mVersions->setValue(static_cast<F32>(mVersion));
        return;
    }
    setVersions(mVersionCount, version);
    mVersionChosen(version);
}

void ALDiffBar::setColors(const LLColor4& background, const LLColor4& ink)
{
    mBgColor              = ALSurface::ground(background, ink);
    mInkColor             = ink;
    const LLColor4 chosen = ALSurface::chosen(background, ink);
    for (ALFlatButton* glyph : { mPreviousButton, mNextButton, mFoldButton, mIgnoreButton, mCopyButton, mInlineButton, mSwapButton, mDoneButton,
                                 mTakeBackButton, mTheirsButton, mMineButton, mBothButton, mOlderButton, mNewerButton })
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
    // As little given way as leaves the count its least, or once it is let
    // go, as fits.
    const S32 room    = getRect().getWidth() - 2 * PAD;
    S32       squeeze = 0;
    while (squeeze < SQUEEZE_MOST && placed(squeeze, false) + (squeeze == 0 ? COUNT_LEAST + GAP : 0) > room)
    {
        ++squeeze;
    }
    placed(squeeze, true);
}

S32 ALDiffBar::placed(S32 squeeze, bool apply)
{
    // The buttons at the right, in ones, twos and threes -- the steps,
    // taking a change back, settling a conflict, the versions of the left,
    // the ways of showing, done -- and the count over what is left at the
    // left. A word's button as wide as its word, or squeezed, its letter.
    const S32 width = getRect().getWidth();
    const S32 top   = getRect().getHeight() - PAD;
    S32       right = width - PAD;
    // Shown where it is wanted and, placed, fits: none past the left edge.
    const auto place = [&](LLView* view, S32 wide, bool wanted) {
        const bool shown = wanted && (!apply || right - wide >= PAD);
        if (apply)
        {
            view->setVisible(shown);
            if (shown)
            {
                view->setShape(LLRect(right - wide, top, right, top - ROW));
            }
        }
        if (shown)
        {
            right -= wide + 1;
        }
        return shown;
    };
    if (place(mDoneButton, SMALL_W, mDoneShown))
    {
        right -= GAP;
    }
    place(mSwapButton, SMALL_W, true);
    place(mInlineButton, SMALL_W, true);
    place(mIgnoreButton, SMALL_W, true);
    place(mFoldButton, SMALL_W, true);
    place(mCopyButton, SMALL_W, true);
    right -= GAP;
    const bool letters = squeeze >= 2;
    bool       words   = false;
    for (S32 n = 2; n >= 0; --n)
    {
        ALFlatButton*     word = n == 0 ? mTheirsButton : n == 1 ? mMineButton : mBothButton;
        const std::string said = letters ? firstLetter(mWords[n]) : mWords[n];
        if (apply)
        {
            word->setGlyph(said);
        }
        words = place(word, llmax(SMALL_W, LLFontGL::getFontSansSerifSmall()->getWidth(said) + 2 * GAP), mMerging) || words;
    }
    if (words)
    {
        right -= GAP;
    }
    if (place(mTakeBackButton, SMALL_W, mTakeBackShown))
    {
        right -= GAP;
    }
    place(mNextButton, SMALL_W, true);
    place(mPreviousButton, SMALL_W, true);
    if (mVersionsShown)
    {
        right -= GAP;
    }
    place(mNewerButton, SMALL_W, mVersionsShown);
    place(mVersions, SLIDER_W, mVersionsShown && squeeze < 3);
    place(mOlderButton, SMALL_W, mVersionsShown);
    if (apply)
    {
        const bool counted = squeeze == 0 && right - GAP - PAD >= COUNT_LEAST;
        mCount->setVisible(counted);
        mCount->setShape(LLRect(PAD, top, llmax(PAD, right - GAP), top - ROW));
    }
    return width - PAD - right;
}

void ALDiffBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}

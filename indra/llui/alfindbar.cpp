/**
 * @file alfindbar.cpp
 * @brief The find and replace bar of a text view.
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

#include "alfindbar.h"

#include "llfontgl.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALFindBar> r("find_bar");

namespace
{
    constexpr S32 ROW      = 22;
    constexpr S32 PAD      = 3;
    constexpr S32 GAP      = 4;
    constexpr S32 EXPAND_W = 18;
    constexpr S32 SMALL_W  = 22;
    constexpr S32 COUNT_W  = 84;
}

// --- a flat glyph button ---------------------------------------------------------------

class ALFindBar::Flat : public LLUICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
    };

    Flat(const Params& p, std::string glyph, bool toggle, const LLUIColor& ink, const LLUIColor& lit)
    :   LLUICtrl(p),
        mGlyph(std::move(glyph)),
        mToggle(toggle),
        mInk(ink),
        mLit(lit)
    {
    }

    void setGlyph(const std::string& glyph) { mGlyph = glyph; }
    void setInk(const LLColor4& ink) { mInk = ink; }
    bool getToggleState() const { return mOn; }
    void setToggleState(bool on) { mOn = on; }

    void draw() override
    {
        const F32    alpha = getDrawContext().mAlpha;
        const LLRect local = getLocalRect();
        if (mOn)
        {
            gl_rect_2d(local, mLit.get() % alpha);
        }
        else if (mHover && getEnabled())
        {
            gl_rect_2d(local, mInk.get() % (0.12f * alpha));
        }
        LLColor4 ink = mInk.get() % alpha;
        if (!getEnabled())
        {
            ink.mV[VALPHA] *= 0.35f;
        }
        const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
        font->renderUTF8(mGlyph, 0, local.getCenterX(), local.getCenterY() - font->getLineHeight() / 2 + 1, ink, LLFontGL::HCENTER, LLFontGL::BOTTOM);
        mHover = false;
    }

    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        mHover = true;
        return true;
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (!getEnabled())
        {
            return true;
        }
        if (mToggle)
        {
            mOn = !mOn;
        }
        onCommit();
        return true;
    }

    bool handleMouseUp(S32 x, S32 y, MASK mask) override { return true; }

private:
    std::string mGlyph;
    bool        mToggle = false;
    bool        mOn     = false;
    bool        mHover  = false;
    LLUIColor   mInk;
    LLUIColor   mLit;
};

// --- the bar --------------------------------------------------------------------------

ALFindBar::Params::Params()
{
    changeDefault(background_visible, false);
}

ALFindBar::ALFindBar(const Params& p)
:   LLPanel(p),
    mBgColor(LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black)),
    mInkColor(LLColor4::white)
{
    mExpand = flat("expand", ">", false, "Replace as well");
    mExpand->setCommitCallback([this](LLUICtrl*, const LLSD&) { setReplaceShown(!mReplaceShown); });

    mFind = field("find", "Find", 3 * SMALL_W + 6);
    mFind->setKeystrokeCallback([this](LLLineEditor* editor, void*) {
        mQuery = editor->getText();
        mChanged();
    }, nullptr);
    mFind->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        // Return goes on; with shift, back.
        if (gKeyboard && (gKeyboard->currentMask(false) & MASK_SHIFT))
        {
            mPrevious();
        }
        else
        {
            mNext();
        }
    });
    mCase  = flat("match_case", "Aa", true, "Match case");
    mWord  = flat("whole_word", "ab", true, "Match whole words");
    mRegex = flat("regex", ".*", true, "Match a regular expression");
    for (Flat* toggle : { mCase, mWord, mRegex })
    {
        toggle->setCommitCallback([this](LLUICtrl*, const LLSD&) { mChanged(); });
    }

    LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    tp.name       = "count";
    tp.rect       = LLRect(0, ROW, COUNT_W, 0);
    tp.h_pad      = 4;
    tp.v_pad      = 4;
    tp.text_color = mInkColor;
    tp.font       = LLFontGL::getFontSansSerifSmall();
    mCount = LLUICtrlFactory::create<LLTextBox>(tp);
    addChild(mCount);

    mPrev        = flat("previous", "\xE2\x86\x91", false, "The one before (shift-return)");
    mNextButton  = flat("next", "\xE2\x86\x93", false, "The next (return)");
    mSelection   = flat("in_selection", "\xE2\x89\xA1", true, "In the selection only");
    mCloseButton = flat("close", "\xC3\x97", false, "Close (escape)");
    mPrev->setCommitCallback([this](LLUICtrl*, const LLSD&) { mPrevious(); });
    mNextButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mNext(); });
    mSelection->setCommitCallback([this](LLUICtrl*, const LLSD&) { mChanged(); });
    mCloseButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mClose(); });

    mReplaceField = field("replace", "Replace", SMALL_W + 6);
    mReplaceField->setCommitCallback([this](LLUICtrl*, const LLSD&) { mReplace(); });
    mPreserveCase = flat("preserve_case", "AB", true, "Keep each match's case: HELLO, Hello or hello");
    // Glyphs standing in for the icons: one replaced and on, all replaced.
    mReplaceOne   = flat("replace_one", "\xE2\x86\xA6", false, "Replace this one and find the next");
    mReplaceEvery = flat("replace_all", "\xE2\x87\x89", false, "Replace every one, as one step to undo");
    mReplaceOne->setCommitCallback([this](LLUICtrl*, const LLSD&) { mReplace(); });
    mReplaceEvery->setCommitCallback([this](LLUICtrl*, const LLSD&) { mReplaceAll(); });

    setReplaceShown(false);
    setCount(-1, 0, std::string());
    layout();
}

ALFindBar::Flat* ALFindBar::flat(const std::string& name, const std::string& glyph, bool toggle, const std::string& tip)
{
    Flat::Params fp;
    fp.name     = name;
    fp.rect     = LLRect(0, ROW, SMALL_W, 0);
    fp.tool_tip = tip;
    Flat* made  = new Flat(fp, glyph, toggle, mInkColor, LLUIColorTable::instance().getColor("CodeCompletionBorderColor", LLColor4::blue));
    addChild(made);
    return made;
}

LLLineEditor* ALFindBar::field(const std::string& name, const std::string& label, S32 pad_right)
{
    LLLineEditor::Params fp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
    fp.name                 = name;
    fp.label                = label;
    fp.rect                 = LLRect(0, ROW, 100, 0);
    fp.revert_on_esc        = false;
    fp.commit_on_focus_lost = false;
    fp.text_pad_right       = pad_right;
    fp.font                 = LLFontGL::getFontSansSerifSmall();
    LLLineEditor* made      = LLUICtrlFactory::create<LLLineEditor>(fp);
    addChild(made);
    return made;
}

void ALFindBar::setQuery(const std::string& query)
{
    mQuery = query;
    if (mFind->getText() != query)
    {
        mFind->setText(query);
    }
    mChanged();
}

void ALFindBar::setReplacement(const std::string& text)
{
    mReplaceField->setText(text);
}

std::string ALFindBar::replacement() const
{
    return mReplaceField->getText();
}

ALTextSearchOptions ALFindBar::options() const
{
    ALTextSearchOptions options;
    options.caseSensitive = mCase->getToggleState();
    options.wholeWord     = mWord->getToggleState();
    options.regex         = mRegex->getToggleState();
    options.preserveCase  = mPreserveCase->getToggleState();
    return options;
}

bool ALFindBar::inSelection() const
{
    return mSelection->getToggleState();
}

void ALFindBar::setReplaceShown(bool shown)
{
    mReplaceShown = shown && mReplaceAllowed;
    mExpand->setGlyph(mReplaceShown ? "v" : ">");
    for (LLView* part : { static_cast<LLView*>(mReplaceField), static_cast<LLView*>(mPreserveCase), static_cast<LLView*>(mReplaceOne),
                          static_cast<LLView*>(mReplaceEvery) })
    {
        part->setVisible(mReplaceShown);
    }
    layout();
}

void ALFindBar::setReplaceAllowed(bool allowed)
{
    mReplaceAllowed = allowed;
    mExpand->setVisible(allowed);
    if (!allowed && mReplaceShown)
    {
        setReplaceShown(false);
    }
}

void ALFindBar::setCount(S32 current, S32 total, const std::string& error)
{
    std::string said;
    if (!error.empty())
    {
        said = "Not a pattern";
    }
    else if (total == 0)
    {
        said = "No results";
    }
    else if (current >= 0)
    {
        said = llformat("%d of %d", current + 1, total);
    }
    else
    {
        said = llformat("%d", total);
    }
    mCount->setText(said);
    mCount->setToolTip(error);
    for (Flat* button : { mPrev, mNextButton, mReplaceOne, mReplaceEvery })
    {
        button->setEnabled(total > 0);
    }
}

void ALFindBar::setColors(const LLColor4& background, const LLColor4& ink)
{
    LLColor4 bg = background;
    for (S32 i = 0; i < 3; ++i)
    {
        bg.mV[i] = background.mV[i] + (ink.mV[i] - background.mV[i]) * 0.08f;
    }
    bg.mV[VALPHA] = 1.f;
    mBgColor      = bg;
    mInkColor     = ink;
    for (Flat* glyph : { mExpand, mCase, mWord, mRegex, mPrev, mNextButton, mSelection, mCloseButton, mPreserveCase, mReplaceOne, mReplaceEvery })
    {
        glyph->setInk(ink);
    }
    mCount->setColor(ink);
}

S32 ALFindBar::wantedHeight() const
{
    return PAD + ROW + PAD + (mReplaceShown ? ROW + PAD : 0);
}

void ALFindBar::focusQuery()
{
    mFind->setFocus(true);
    mFind->selectAll();
}

void ALFindBar::focusReplacement()
{
    if (mReplaceShown)
    {
        mReplaceField->setFocus(true);
        mReplaceField->selectAll();
    }
}

void ALFindBar::draw()
{
    const F32    alpha = getDrawContext().mAlpha;
    const LLRect local = getLocalRect();
    gl_rect_2d(local, mBgColor.get() % alpha);
    gl_rect_2d(local, mInkColor.get() % (0.25f * alpha), false);
    LLPanel::draw();
}

bool ALFindBar::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        mClose();
        return true;
    }
    if (key == KEY_F3 && (mask == MASK_NONE || mask == MASK_SHIFT))
    {
        if (mask == MASK_SHIFT)
        {
            mPrevious();
        }
        else
        {
            mNext();
        }
        return true;
    }
    return LLPanel::handleKeyHere(key, mask);
}

void ALFindBar::layout()
{
    const S32 width  = getRect().getWidth();
    const S32 height = getRect().getHeight();
    const S32 top    = height - PAD;
    mExpand->setShape(LLRect(PAD, top, PAD + EXPAND_W, PAD));

    const S32 x     = PAD + EXPAND_W + GAP;
    S32       right = width - PAD;
    for (Flat* button : { mCloseButton, mSelection, mNextButton, mPrev })
    {
        button->setShape(LLRect(right - SMALL_W, top, right, top - ROW));
        right -= SMALL_W + 1;
    }
    mCount->setShape(LLRect(right - COUNT_W, top, right, top - ROW));
    right -= COUNT_W + GAP;
    const S32 field_right = llmax(x + 40, right);
    mFind->setShape(LLRect(x, top, field_right, top - ROW));
    // The three ways of matching, inside the field's right end.
    S32 inner = field_right - 2;
    for (Flat* toggle : { mRegex, mWord, mCase })
    {
        toggle->setShape(LLRect(inner - SMALL_W, top - 2, inner, top - ROW + 2));
        inner -= SMALL_W;
    }

    if (mReplaceShown)
    {
        // As wide as the field above it; its two buttons where the count
        // sits above.
        const S32 second = top - ROW - PAD;
        mReplaceField->setShape(LLRect(x, second, field_right, second - ROW));
        mPreserveCase->setShape(LLRect(field_right - 2 - SMALL_W, second - 2, field_right - 2, second - ROW + 2));
        S32 after = field_right + GAP;
        for (Flat* button : { mReplaceOne, mReplaceEvery })
        {
            button->setShape(LLRect(after, second, after + SMALL_W, second - ROW));
            after += SMALL_W + 1;
        }
    }
}

void ALFindBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}

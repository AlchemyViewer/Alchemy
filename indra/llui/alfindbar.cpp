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

#include "alsaid.h"
#include "alsurface.h"

#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

static LLDefaultChildRegistry::Register<ALFindBar> r("find_bar");

namespace
{
    constexpr S32 ROW      = 22;
    constexpr S32 PAD      = 3;
    constexpr S32 GAP      = 4;
    constexpr S32 EXPAND_W = 18;
    constexpr S32 SMALL_W  = 22;
    constexpr S32 COUNT_W  = 84;

    // What turns a way of matching on and off from the keyboard, with a
    // letter: Command and Option on a Mac, as its editors have it, since
    // Option alone types a character there; Alt alone elsewhere, since
    // Control and Alt together is AltGr on many a Windows keyboard, which
    // types too.
#if LL_DARWIN
    constexpr MASK TOGGLE_MASK = MASK_CONTROL | MASK_ALT;
#else
    constexpr MASK TOGGLE_MASK = MASK_ALT;
#endif
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
        // Made here rather than by the factory, which is what would have
        // read the parameter: a stop for Tab, as a button is.
        setTabStop(true);
    }

    void setGlyph(const std::string& glyph) { mGlyph = glyph; }
    // The letter that presses it with TOGGLE_MASK, said after its tip.
    void setKey(KEY key) { mKey = key; }

    // Put together as it is shown rather than when it is made: a key's
    // name is the viewer's to give, in the viewer's language.
    std::string getToolTip() const override
    {
        const std::string tip = LLUICtrl::getToolTip();
        return mKey == KEY_NONE || tip.empty() ? tip : tip + " (" + LLKeyboard::stringFromAccelerator(TOGGLE_MASK, mKey) + ")";
    }
    void setInk(const LLColor4& ink) { mInk = ink; }
    void setLit(const LLColor4& lit) { mLit = lit; }
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
        if (hasFocus())
        {
            // Where the keyboard is, once Tab has brought it here.
            gl_rect_2d(local, gFocusMgr.getFocusColor() % alpha, false);
        }
        mHover = false;
    }

    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        mHover = true;
        // A button: the arrow, not the text's cursor under the bar.
        if (LLWindow* window = getWindow())
        {
            window->setCursor(UI_CURSOR_ARROW);
        }
        return true;
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        press();
        return true;
    }

    bool handleMouseUp(S32 x, S32 y, MASK mask) override { return true; }

    // Pressed from the keyboard as a button is: Space, which comes as the
    // character, once however long it is held; and Return.
    bool handleUnicodeCharHere(llwchar uni_char) override
    {
        if (uni_char == ' ')
        {
            if (!gKeyboard || !gKeyboard->getKeyRepeated(' '))
            {
                press();
            }
            return true;
        }
        return LLUICtrl::handleUnicodeCharHere(uni_char);
    }

    bool handleKeyHere(KEY key, MASK mask) override
    {
        if (key == KEY_RETURN && mask == MASK_NONE)
        {
            press();
            return true;
        }
        return LLUICtrl::handleKeyHere(key, mask);
    }

    void press()
    {
        if (!getEnabled())
        {
            return;
        }
        if (mToggle)
        {
            mOn = !mOn;
        }
        onCommit();
    }

private:
    std::string mGlyph;
    KEY         mKey    = KEY_NONE;
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
    mExpand = flat("expand", ">", false, alSaid("FindBarReplaceToo", "Replace as well"));
    mExpand->setCommitCallback([this](LLUICtrl*, const LLSD&) { setReplaceShown(!mReplaceShown); });

    mFind = field("find", alSaid("FindBarFind", "Find"), 3 * SMALL_W + 6);
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
    mCase  = flat("match_case", "Aa", true, alSaid("FindBarCase", "Match case"));
    mWord  = flat("whole_word", "ab", true, alSaid("FindBarWord", "Match whole words"));
    mRegex = flat("regex", ".*", true, alSaid("FindBarPattern", "Match a regular expression"));
    for (Flat* toggle : { mCase, mWord, mRegex })
    {
        toggle->setCommitCallback([this](LLUICtrl*, const LLSD&) { mChanged(); });
    }
    mCase->setKey('C');
    mWord->setKey('W');
    mRegex->setKey('R');

    LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    tp.name       = "count";
    tp.rect       = LLRect(0, ROW, COUNT_W, 0);
    tp.h_pad      = 4;
    tp.v_pad      = 4;
    tp.text_color = mInkColor;
    tp.font       = LLFontGL::getFontSansSerifSmall();
    mCount = LLUICtrlFactory::create<LLTextBox>(tp);
    addChild(mCount);

    mPrev        = flat("previous", "\xE2\x86\x91", false, alSaid("FindBarPrevious", "The one before (shift-return)"));
    mNextButton  = flat("next", "\xE2\x86\x93", false, alSaid("FindBarNext", "The next (return)"));
    mSelection   = flat("in_selection", "\xE2\x89\xA1", true, alSaid("FindBarInSelection", "In the selection only"));
    mCloseButton = flat("close", "\xC3\x97", false, alSaid("FindBarClose", "Close (escape)"));
    mPrev->setCommitCallback([this](LLUICtrl*, const LLSD&) { mPrevious(); });
    mNextButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mNext(); });
    mSelection->setCommitCallback([this](LLUICtrl*, const LLSD&) { mChanged(); });
    mSelection->setKey('L');
    mCloseButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mClose(); });

    mReplaceField = field("replace", alSaid("FindBarReplace", "Replace"), SMALL_W + 6);
    mReplaceField->setCommitCallback([this](LLUICtrl*, const LLSD&) { mReplace(); });
    mPreserveCase = flat("preserve_case", "AB", true, alSaid("FindBarKeepCase", "Keep each match's case: HELLO, Hello or hello"));
    // Glyphs standing in for the icons: one replaced and on, all replaced.
    mReplaceOne   = flat("replace_one", "\xE2\x86\xA6", false, alSaid("FindBarReplaceOne", "Replace this one and find the next"));
    mReplaceEvery = flat("replace_all", "\xE2\x87\x89", false, alSaid("FindBarReplaceAll", "Replace every one, as one step to undo"));
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
    // The band behind a glyph that is switched on. The view's own
    // colours, which setColors puts back whenever the theme moves; what
    // is here is only what it wears until the first of those.
    Flat* made  = new Flat(fp, glyph, toggle, mInkColor, ALSurface::chosen(LLColor4::black, mInkColor.get()));
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
    // A pattern may reach across lines, with \n in it; plain text never
    // has a line break to.
    options.acrossLines   = options.regex;
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
        said = alSaid("FindBarBadPattern", "Not a pattern");
    }
    else if (total == 0)
    {
        said = alSaid("FindBarNone", "No results");
    }
    else if (current >= 0)
    {
        said = alSaid("FindBarOf", "[CURRENT] of [TOTAL]", { { "[CURRENT]", std::to_string(current + 1) }, { "[TOTAL]", std::to_string(total) } });
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
    mBgColor  = ALSurface::ground(background, ink);
    mInkColor = ink;
    const LLColor4 chosen = ALSurface::chosen(background, ink);
    for (Flat* glyph : { mExpand, mCase, mWord, mRegex, mPrev, mNextButton, mSelection, mCloseButton, mPreserveCase, mReplaceOne, mReplaceEvery })
    {
        glyph->setInk(ink);
        glyph->setLit(chosen);
    }
    mCount->setColor(ink);
    // The fields in the view's own colours: its ground behind the text,
    // its ink for the text and the caret, and a shade between them for
    // the label and the selection.
    const LLColor4 faint = ALSurface::shade(background, ink, 0.45f);
    const LLColor4 lit   = ALSurface::shade(background, ink, 0.25f);
    for (LLLineEditor* field : { mFind, mReplaceField })
    {
        LLColor4 ground = background;
        ground.mV[VALPHA] = 1.f;
        field->setBgColor(ground);
        field->setFgColor(ink);
        field->setCursorColor(ink);
        field->setTentativeFgColor(faint);
        field->setHighlightColor(lit);
    }
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
    gl_rect_2d(local, ALSurface::frame(mInkColor.get(), alpha), false);
    LLPanel::draw();
}

bool ALFindBar::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        mClose();
        return true;
    }
    if (mask == TOGGLE_MASK)
    {
        Flat* toggle = key == 'C' ? mCase : key == 'W' ? mWord : key == 'R' ? mRegex : key == 'L' ? mSelection : nullptr;
        if (toggle)
        {
            toggle->press();
            return true;
        }
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

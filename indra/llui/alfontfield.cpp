/**
 * @file alfontfield.cpp
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

#include "linden_common.h"

#include "alfontfield.h"

#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfloater.h"
#include "llfontgl.h"
#include "llfontregistry.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "llstyle.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALFontField> r("font_field");

namespace
{
    constexpr S32 POPOVER_WIDTH = 340;
    constexpr S32 LIST_HEIGHT = 200;
    constexpr S32 ROW = 22;
    constexpr S32 PREVIEW_HEIGHT = 34;

    // Letters and figures rather than words: nothing here needs translating
    // and nothing here is a sentence, which is the point -- what is being
    // looked at is the shapes.
    const std::string SPECIMEN("Aa Bb Cc Gg 0123");

    // What files write, which is not what LLFontGL::getStringFromStyle
    // writes: that leads every spelling with NORMAL, and the shipped files
    // say `font.style="BOLD"`. Both parse the same, so the tool writes the
    // one already in the tree.
    std::string styleText(U8 style)
    {
        std::string out;
        if (style & LLFontGL::BOLD)      { out += out.empty() ? "BOLD" : "|BOLD"; }
        if (style & LLFontGL::ITALIC)    { out += out.empty() ? "ITALIC" : "|ITALIC"; }
        if (style & LLFontGL::UNDERLINE) { out += out.empty() ? "UNDERLINE" : "|UNDERLINE"; }
        return out.empty() ? "NORMAL" : out;
    }

    const LLFontGL* fontOf(const std::string& name, const std::string& size, const std::string& style)
    {
        const LLFontDescriptor desc(name.empty() ? "SansSerif" : name, size,
                                    LLFontGL::getStyleFromString(style));
        const LLFontGL* font = LLFontGL::getFont(desc);
        return font ? font : LLFontGL::getFontSansSerif();
    }

    // The names, one per row, each drawn in the font it names. A font is
    // recognised rather than read, so the list is the specimens and the
    // name beside them.
    class ALFontList final : public LLPanel
    {
    public:
        AL_VIEW_TYPE(ALFontList, LLPanel);

        typedef std::function<void(const std::string&)> chose_t;

        ALFontList(const LLPanel::Params& p, chose_t chose)
        :   LLPanel(p),
            mChose(std::move(chose))
        {
            mAll = LLFontGL::getDeclaredFontNames();
            filter(LLStringUtil::null);
        }

        void setChosen(const std::string& name) { mChosen = name; }

        void filter(const std::string& text)
        {
            std::string wanted(text);
            LLStringUtil::toLower(wanted);
            mShown.clear();
            for (const std::string& name : mAll)
            {
                std::string lower(name);
                LLStringUtil::toLower(lower);
                if (wanted.empty() || lower.find(wanted) != std::string::npos)
                {
                    mShown.push_back(&name);
                }
            }
            reshape(getRect().getWidth(), llmax<S32>(1, (S32)mShown.size()) * ROW, false);
        }

        void draw() override
        {
            LLPanel::draw();
            static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
            static const LLUIColor picked = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4::grey4);
            const LLFontGL* label_font = LLFontGL::getFontSansSerifSmall();
            for (size_t i = 0; i < mShown.size(); ++i)
            {
                const LLRect row = rectOf((S32)i);
                if (*mShown[i] == mChosen || (S32)i == mHover)
                {
                    gl_rect_2d(row, picked.get(), *mShown[i] == mChosen);
                }
                label_font->renderUTF8(*mShown[i], 0, row.mLeft + 4, row.mBottom + 5,
                                       ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
                // The specimen: the same letters on every row, so that what
                // differs between two of them is the only thing that differs.
                const LLFontGL* font = fontOf(*mShown[i], LLStringUtil::null, LLStringUtil::null);
                font->renderUTF8(SPECIMEN, 0, row.mLeft + 150, row.mBottom + 5,
                                 ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
            }
        }

        bool handleHover(S32 x, S32 y, MASK mask) override
        {
            mHover = at(x, y);
            return LLPanel::handleHover(x, y, mask);
        }

        bool handleMouseDown(S32 x, S32 y, MASK mask) override
        {
            const S32 which = at(x, y);
            if (which >= 0)
            {
                mChosen = *mShown[which];
                mChose(mChosen);
                return true;
            }
            return LLPanel::handleMouseDown(x, y, mask);
        }

    private:
        LLRect rectOf(S32 index) const
        {
            const S32 top = getRect().getHeight() - index * ROW;
            return LLRect(0, top, getRect().getWidth(), top - ROW);
        }

        S32 at(S32 x, S32 y) const
        {
            for (size_t i = 0; i < mShown.size(); ++i)
            {
                if (rectOf((S32)i).pointInRect(x, y))
                {
                    return (S32)i;
                }
            }
            return -1;
        }

        std::vector<std::string>            mAll;
        std::vector<const std::string*>     mShown;
        std::string                         mChosen;
        chose_t                             mChose;
        S32                                 mHover = -1;
    };

    // The popover. The three parts are picked against a preview and given
    // back when it closes, so a visit to it is one change: escape abandons
    // what was picked, and anything else keeps it.
    class ALFontPopover final : public LLFloater
    {
    public:
        AL_VIEW_TYPE(ALFontPopover, LLFloater);

        typedef std::function<void(const std::string&, const std::string&, const std::string&)> settled_t;

        ALFontPopover(const LLFloater::Params& p, const std::string& name, const std::string& size,
                      const std::string& style, settled_t settled)
        :   LLFloater(LLSD(), p),
            mName(name),
            mSize(size),
            mStyle(style),
            mSettled(std::move(settled))
        {
            const S32 right = POPOVER_WIDTH - 4;
            S32 top = getRect().getHeight() - 4;

            LLFilterEditor::Params fp;
            fp.name = "filter";
            fp.rect = LLRect(4, top, right, top - ROW);
            fp.label = "Filter fonts";
            mFilter = LLUICtrlFactory::create<LLFilterEditor>(fp);
            mFilter->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&)
            {
                mList->filter(ctrl->getValue().asString());
            });
            addChild(mFilter);
            top -= ROW + 4;

            LLScrollContainer::Params sp;
            sp.name = "scroll";
            sp.rect = LLRect(4, top, right, top - LIST_HEIGHT);
            LLScrollContainer* scroll = LLUICtrlFactory::create<LLScrollContainer>(sp);
            addChild(scroll);

            LLPanel::Params lp;
            lp.name = "fonts";
            lp.rect = LLRect(0, LIST_HEIGHT, right - 4 - 20, 0);
            lp.background_visible = false;
            mList = new ALFontList(lp, [this](const std::string& chosen)
            {
                mName = chosen;
                refreshPreview();
            });
            mList->setChosen(mName);
            scroll->addChild(mList);
            top -= LIST_HEIGHT + 6;

            LLTextBox::Params tp;
            tp.name = "size_label";
            tp.rect = LLRect(4, top, 44, top - ROW);
            tp.initial_value = "Size";
            tp.font_valign = LLFontGL::VCENTER;
            addChild(LLUICtrlFactory::create<LLTextBox>(tp));

            LLComboBox::Params cp;
            cp.name = "size";
            cp.rect = LLRect(46, top, 176, top - ROW);
            cp.allow_text_entry = true;
            mSizes = LLUICtrlFactory::create<LLComboBox>(cp);
            // Empty is a real answer: it means the size the font was
            // declared with, which is what most files leave it at.
            mSizes->add(LLStringUtil::null);
            for (const std::string& size_name : LLFontGL::getDeclaredSizeNames())
            {
                mSizes->add(size_name);
            }
            if (!mSize.empty())
            {
                mSizes->setValue(mSize);
            }
            mSizes->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&)
            {
                mSize = ctrl->getValue().asString();
                refreshPreview();
            });
            addChild(mSizes);
            top -= ROW + 4;

            const U8 style_now = LLFontGL::getStyleFromString(mStyle);
            static const char* labels[3] = { "Bold", "Italic", "Underline" };
            static const U8 bits[3] = { LLFontGL::BOLD, LLFontGL::ITALIC, LLFontGL::UNDERLINE };
            for (S32 i = 0; i < 3; ++i)
            {
                LLCheckBoxCtrl::Params bp;
                bp.name = labels[i];
                bp.label = labels[i];
                bp.rect = LLRect(4 + i * 100, top, 4 + i * 100 + 96, top - ROW);
                bp.initial_value = (style_now & bits[i]) != 0;
                LLCheckBoxCtrl* box = LLUICtrlFactory::create<LLCheckBoxCtrl>(bp);
                const U8 bit = bits[i];
                box->setCommitCallback([this, bit](LLUICtrl* ctrl, const LLSD&)
                {
                    U8 style = LLFontGL::getStyleFromString(mStyle);
                    style = ctrl->getValue().asBoolean() ? (style | bit) : (U8)(style & ~bit);
                    mStyle = styleText(style);
                    refreshPreview();
                });
                addChild(box);
            }
            top -= ROW + 4;

            LLTextBox::Params pp;
            pp.name = "preview";
            pp.rect = LLRect(4, top, right, top - PREVIEW_HEIGHT);
            pp.border_visible = true;
            pp.font_valign = LLFontGL::VCENTER;
            pp.h_pad = 6;
            mPreview = LLUICtrlFactory::create<LLTextBox>(pp);
            addChild(mPreview);
            refreshPreview();
        }

        // Closing settles it, unless escape said not to.
        void onClose(bool app_quitting) override
        {
            if (mKeep && mSettled)
            {
                mSettled(mName, mSize, mStyle);
            }
            LLFloater::onClose(app_quitting);
        }

        // The field is going away and this is going with it: what was
        // picked has nowhere to be written to.
        void abandon() { mKeep = false; }

        void onFocusLost() override
        {
            closeFloater();
        }

        bool handleKeyHere(KEY key, MASK mask) override
        {
            if (key == KEY_ESCAPE && mask == MASK_NONE)
            {
                mKeep = false;
                closeFloater();
                return true;
            }
            if (key == KEY_RETURN && mask == MASK_NONE)
            {
                closeFloater();
                return true;
            }
            return LLFloater::handleKeyHere(key, mask);
        }

    private:
        void refreshPreview()
        {
            mList->setChosen(mName);
            LLStyle::Params style;
            style.font = fontOf(mName, mSize, mStyle);
            mPreview->setText(SPECIMEN, style);
        }

        std::string         mName;
        std::string         mSize;
        std::string         mStyle;
        settled_t           mSettled;
        LLFilterEditor*     mFilter = nullptr;
        ALFontList*         mList = nullptr;
        LLComboBox*         mSizes = nullptr;
        LLTextBox*          mPreview = nullptr;
        bool                mKeep = true;
    };
}

ALFontField::Params::Params()
:   sample_width("sample_width", 26)
{
}

ALFontField::ALFontField(const Params& p)
:   LLUICtrl(p),
    mSampleWidth(p.sample_width)
{
    LLLineEditor::Params ep;
    ep.name = "text";
    ep.rect = LLRect(mSampleWidth + 3, getRect().getHeight(), getRect().getWidth(), 0);
    ep.follows.flags = FOLLOWS_ALL;
    ep.commit_on_focus_lost = true;
    mEditor = LLUICtrlFactory::create<LLLineEditor>(ep);
    mEditor->setCommitCallback([this](LLUICtrl*, const LLSD&) { onTextCommit(); });
    addChild(mEditor);
}

ALFontField::~ALFontField()
{
    closePopover();
}

void ALFontField::setValue(const LLSD& value)
{
    mName = value.asString();
    refreshText();
}

LLSD ALFontField::getValue() const
{
    return mName;
}

void ALFontField::setSize(const std::string& size)
{
    mSize = size;
    refreshText();
}

void ALFontField::setStyle(const std::string& style)
{
    mStyle = style;
    refreshText();
}

// The line says the name, because that is the attribute this row is for.
// What the size and the flags do is in the specimen beside it.
void ALFontField::refreshText()
{
    if (mEditor)
    {
        mEditor->setText(mName);
    }
}

void ALFontField::draw()
{
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    const LLRect sample(0, getRect().getHeight() - 2, mSampleWidth, 2);
    gl_rect_2d(sample, edge.get(), false);
    const LLFontGL* font = fontOf(mName, mSize, mStyle);
    font->renderUTF8(std::string("Aa"), 0, sample.mLeft + 3, sample.mBottom + 2,
                     ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
    LLUICtrl::draw();
}

bool ALFontField::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (x < mSampleWidth)
    {
        openPopover();
        return true;
    }
    return LLUICtrl::handleMouseDown(x, y, mask);
}

void ALFontField::onTextCommit()
{
    const std::string was = mName;
    mName = mEditor->getText();
    if (mName != was)
    {
        mPartCommit(LLStringUtil::null, mName);
    }
    onCommit();
}

// Three attributes, so up to three writes, and only for what changed: a
// file that never said `font.style` is not given one for staying NORMAL.
void ALFontField::apply(const std::string& name, const std::string& size, const std::string& style)
{
    const std::string was_name = mName;
    const std::string was_size = mSize;
    const std::string was_style = mStyle;
    mName = name;
    mSize = size;
    mStyle = style;
    refreshText();

    if (mName != was_name)  { mPartCommit(LLStringUtil::null, mName); }
    if (mSize != was_size)  { mPartCommit("size", mSize); }
    if (mStyle != was_style) { mPartCommit("style", mStyle); }
}

void ALFontField::openPopover()
{
    closePopover();

    static constexpr S32 POPOVER_HEIGHT = 4 + ROW + 4 + LIST_HEIGHT + 6 + ROW + 4 + ROW + 4 + PREVIEW_HEIGHT + 4;

    LLFloater::Params p(LLFloater::getDefaultParams());
    p.can_close = false;
    p.can_minimize = false;
    p.can_resize = false;
    p.title = LLStringUtil::null;
    p.rect = LLRect(0, POPOVER_HEIGHT, POPOVER_WIDTH, 0);

    ALFontPopover* popover = new ALFontPopover(p, mName, mSize, mStyle,
        [this](const std::string& name, const std::string& size, const std::string& style)
        {
            apply(name, size, style);
        });
    mPopover = popover->getHandle();

    // Under the field, and shoved back on screen if that would put it off.
    LLRect screen = calcScreenRect();
    LLRect where = popover->getRect();
    where.setLeftTopAndSize(screen.mLeft, screen.mBottom, where.getWidth(), where.getHeight());
    if (where.mBottom < 0)
    {
        where.translate(0, screen.getHeight() + where.getHeight());
    }
    popover->setRect(where);
    popover->openFloater();
    popover->setFocus(true);
}

// Closed from this side rather than from its own: whatever was picked is
// dropped, because this is either about to open another one or about to be
// deleted, and a popover closing itself is the path that keeps a choice.
void ALFontField::closePopover()
{
    if (LLFloater* popover = mPopover.get())
    {
        if (ALFontPopover* font_popover = popover->as<ALFontPopover>())
        {
            font_popover->abandon();
        }
        popover->closeFloater();
    }
    mPopover.markDead();
}

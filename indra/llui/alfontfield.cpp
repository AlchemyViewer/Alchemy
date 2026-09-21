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

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "alpopover.h"
#include "alstringmatch.h"
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
#include "lltrans.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <functional>

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
    const std::string SAMPLE("Aa");

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

        ALFontList(const LLPanel::Params& p, ALFontField::Families families, const std::string& current, chose_t chose)
        :   LLPanel(p),
            mChose(std::move(chose))
        {
            if (families == ALFontField::Families::Declared)
            {
                mAll = LLFontGL::getDeclaredFontNames();
                mLabels = mAll;
            }
            else
            {
                // The families marked for choosing, by the labels fonts.xml
                // gives them; and the name the field has now, should it be
                // one the list would not offer, so that it can be kept.
                const LLFontRegistry::FamilyFilter filter = families == ALFontField::Families::Monospace    ? LLFontRegistry::FamilyFilter::MONOSPACE
                                                            : families == ALFontField::Families::Proportional ? LLFontRegistry::FamilyFilter::PROPORTIONAL
                                                                                                              : LLFontRegistry::FamilyFilter::ANY;
                for (const LLFontRegistry::FamilyInfo& family : LLFontGL::getAvailableFamilies(filter))
                {
                    mAll.push_back(family.name);
                    mLabels.push_back(family.label.empty() ? family.name : family.label);
                }
                if (!current.empty() && std::find(mAll.begin(), mAll.end(), current) == mAll.end())
                {
                    mAll.insert(mAll.begin(), current);
                    mLabels.insert(mLabels.begin(), current);
                }
            }
            // Each name's face, found once: a registry lookup builds a
            // descriptor of three strings, and the list is drawn every frame.
            mFonts.reserve(mAll.size());
            for (const std::string& name : mAll)
            {
                mFonts.push_back(fontOf(name, LLStringUtil::null, LLStringUtil::null));
            }
            filter(LLStringUtil::null);
        }

        void setChosen(const std::string& name) { mChosen = name; }

        // What the row for a name says, or the name itself.
        std::string labelOf(const std::string& name) const
        {
            for (size_t i = 0; i < mAll.size(); ++i)
            {
                if (mAll[i] == name)
                {
                    return mLabels[i];
                }
            }
            return name;
        }

        void filter(const std::string& text)
        {
            mShown.clear();
            for (size_t i = 0; i < mAll.size(); ++i)
            {
                if (ALStringMatch::containsNoCase(mAll[i], text) || ALStringMatch::containsNoCase(mLabels[i], text))
                {
                    mShown.push_back(i);
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
                const std::string& name = mAll[mShown[i]];
                if (name == mChosen || (S32)i == mHover)
                {
                    gl_rect_2d(row, picked.get(), name == mChosen);
                }
                label_font->renderUTF8(mLabels[mShown[i]], 0, row.mLeft + 4, row.mBottom + 5,
                                       ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
                // The specimen: the same letters on every row, so that what
                // differs between two of them is the only thing that differs.
                mFonts[mShown[i]]->renderUTF8(SPECIMEN, 0, row.mLeft + 150, row.mBottom + 5,
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
                mChosen = mAll[mShown[which]];
                mChose(mChosen);
                return true;
            }
            return LLPanel::handleMouseDown(x, y, mask);
        }

        // A double-click is the choice and the yes together.
        bool handleDoubleClick(S32 x, S32 y, MASK mask) override
        {
            const S32 which = at(x, y);
            if (which >= 0)
            {
                mChosen = mAll[mShown[which]];
                mChose(mChosen);
                if (mSettle)
                {
                    mSettle();
                }
                return true;
            }
            return LLPanel::handleDoubleClick(x, y, mask);
        }

        void onDoubleClickSettle(std::function<void()> settle) { mSettle = std::move(settle); }

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
        std::vector<std::string>            mLabels;    // one per name, what the row says
        std::vector<const LLFontGL*>        mFonts;     // one per name, found once
        std::vector<size_t>                 mShown;     // into mAll
        std::string                         mChosen;
        chose_t                             mChose;
        std::function<void()>               mSettle;
        S32                                 mHover = -1;
    };

    // The popover. The three parts are picked against a preview and given
    // back when it closes, so a visit to it is one change: escape abandons
    // what was picked, and anything else keeps it. The ways out and the
    // placing are the popover's; this builds the three parts and holds
    // what they say.
    class ALFontPopover final : public ALPopover
    {
    public:
        AL_VIEW_TYPE(ALFontPopover, ALPopover);

        ALFontPopover(const LLFloater::Params& p, ALFontField::Families families, const std::string& name, const std::string& size,
                      const std::string& style)
        :   ALPopover(p),
            mName(name),
            mSize(size),
            mStyle(style)
        {
            const S32 right = POPOVER_WIDTH - 4;
            S32 top = getRect().getHeight() - 4;

            LLFilterEditor::Params fp;
            fp.name = "filter";
            fp.rect = LLRect(4, top, right, top - ROW);
            fp.label = LLTrans::getString("FontFieldFilter");
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
            mList = new ALFontList(lp, families, mName, [this](const std::string& chosen)
            {
                mName = chosen;
                refreshPreview();
            });
            mList->setChosen(mName);
            mList->onDoubleClickSettle([this]() { settle(); });
            scroll->addChild(mList);
            top -= LIST_HEIGHT + 6;

            LLTextBox::Params tp;
            tp.name = "size_label";
            tp.rect = LLRect(4, top, 44, top - ROW);
            tp.initial_value = LLTrans::getString("FontFieldSize");
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
            static const char* const names[3] = { "bold", "italic", "underline" };
            static const char* const labels[3] = { "FontFieldBold", "FontFieldItalic", "FontFieldUnderline" };
            static const U8 bits[3] = { LLFontGL::BOLD, LLFontGL::ITALIC, LLFontGL::UNDERLINE };
            for (S32 i = 0; i < 3; ++i)
            {
                LLCheckBoxCtrl::Params bp;
                bp.name = names[i];
                bp.label = LLTrans::getString(labels[i]);
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
            top -= PREVIEW_HEIGHT + 6;

            // The way out that keeps what was picked, and the one that
            // does not; looking away is the second.
            LLButton::Params ok;
            ok.name = "ok";
            ok.label = LLTrans::getString("FontFieldOK");
            ok.rect = LLRect(right - 80, top, right, top - ROW);
            ok.commit_callback.function = [this](LLUICtrl*, const LLSD&) { settle(); };
            addChild(LLUICtrlFactory::create<LLButton>(ok));
            LLButton::Params cancel;
            cancel.name = "cancel";
            cancel.label = LLTrans::getString("FontFieldCancel");
            cancel.rect = LLRect(right - 80 - 6 - 80, top, right - 80 - 6, top - ROW);
            cancel.commit_callback.function = [this](LLUICtrl*, const LLSD&) { escape(); };
            addChild(LLUICtrlFactory::create<LLButton>(cancel));
            refreshPreview();
        }

        // Only OK settles; escape, cancel and looking away keep what the
        // field had, so that what a person sees applied is what they said
        // yes to.
        void settle() override
        {
            mSettled = true;
            ALPopover::settle();
        }
        bool settled() const { return mSettled; }

        // What was picked, as the three attributes it is.
        const std::string& name() const { return mName; }
        const std::string& size() const { return mSize; }
        const std::string& style() const { return mStyle; }

    private:
        // The preview says what is chosen, in it: the name, the size and
        // the style as words, then the specimen.
        void refreshPreview()
        {
            mList->setChosen(mName);
            LLStyle::Params style;
            style.font = fontOf(mName, mSize, mStyle);
            std::string said = mList->labelOf(mName);
            if (!mSize.empty())
            {
                said += " " + mSize;
            }
            if (!mStyle.empty())
            {
                said += " " + mStyle;
            }
            mPreview->setText(said + "  " + SPECIMEN, style);
        }

        bool                mSettled = false;
        std::string         mName;
        std::string         mSize;
        std::string         mStyle;
        LLFilterEditor*     mFilter = nullptr;
        ALFontList*         mList = nullptr;
        LLComboBox*         mSizes = nullptr;
        LLTextBox*          mPreview = nullptr;
    };
}

ALFontField::Params::Params()
:   sample_width("sample_width", 26),
    families("families", "declared"),
    editable("editable", true)
{
}

ALFontField::ALFontField(const Params& p)
:   LLUICtrl(p),
    mSampleWidth(p.sample_width)
{
    const std::string& families = p.families();
    mFamilies = families == "selectable"   ? Families::Selectable
                : families == "monospace"    ? Families::Monospace
                : families == "proportional" ? Families::Proportional
                                             : Families::Declared;
    mEditable = p.editable;
    if (mEditable)
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
    refreshText();
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

const LLFontGL* ALFontField::font() const
{
    return mFont ? mFont : LLFontGL::getFontSansSerif();
}

// The line says the name, because that is the attribute this row is for.
// What the size and the flags do is in the specimen beside it, drawn in the
// face the three of them name -- found here, when one of them changes,
// rather than on every frame: a lookup builds a descriptor, and a name
// nobody declared would have the registry try to make a font of it, and say
// so, each time it was drawn.
void ALFontField::refreshText()
{
    if (mEditor)
    {
        mEditor->setText(mName);
    }
    mFont = fontOf(mName, mSize, mStyle);
}

std::string ALFontField::describe() const
{
    std::string said = mName;
    for (const LLFontRegistry::FamilyInfo& family : LLFontGL::getAvailableFamilies())
    {
        if (family.name == mName && !family.label.empty())
        {
            said = family.label;
            break;
        }
    }
    if (!mSize.empty())
    {
        said += " " + mSize;
    }
    if (!mStyle.empty())
    {
        said += " " + mStyle;
    }
    return said;
}

void ALFontField::draw()
{
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    const LLRect sample(0, getRect().getHeight() - 2, mSampleWidth, 2);
    gl_rect_2d(sample, edge.get(), false);
    font()->renderUTF8(SAMPLE, 0, sample.mLeft + 3, sample.mBottom + 2,
                       ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
    if (!mEditable)
    {
        // The choice, said in itself, where the name would be typed.
        const LLRect box(mSampleWidth + 3, getRect().getHeight() - 2, getRect().getWidth(), 2);
        gl_rect_2d(box, edge.get(), false);
        font()->renderUTF8(describe(), 0, box.mLeft + 6, box.mBottom + 2, ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, box.getWidth() - 8, nullptr, true);
    }
    LLUICtrl::draw();
}

bool ALFontField::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (!mEditable || x < mSampleWidth)
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
        mFont = fontOf(mName, mSize, mStyle);
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
    if (mName != was_name || mSize != was_size || mStyle != was_style)
    {
        onCommit();
    }
}

void ALFontField::openPopover()
{
    closePopover();

    static constexpr S32 POPOVER_HEIGHT = 4 + ROW + 4 + LIST_HEIGHT + 6 + ROW + 4 + ROW + 4 + PREVIEW_HEIGHT + 6 + ROW + 4;
    ALFontPopover* popover = new ALFontPopover(ALPopover::paramsFor(POPOVER_WIDTH, POPOVER_HEIGHT),
                                               mFamilies, mName, mSize, mStyle);
    mPopover = popover->getDerivedHandle<ALPopover>();
    // Told as it goes, while it still holds what was picked; only OK, or
    // a double-click on a font, is a yes -- escape, cancel and looking
    // away keep what the field had.
    popover->onClosed([this, held = popover->getDerivedHandle<ALFontPopover>()](bool)
    {
        if (ALFontPopover* said = held.get(); said && said->settled())
        {
            apply(said->name(), said->size(), said->style());
        }
        mPopover.markDead();
    });
    popover->openBeside(this);
}

// Closed from this side rather than from its own: whatever was picked is
// dropped, because this is either about to open another one or about to be
// deleted, and a popover closing itself is the path that keeps a choice.
void ALFontField::closePopover()
{
    if (ALPopover* popover = mPopover.get())
    {
        popover->escape();
    }
    mPopover.markDead();
}

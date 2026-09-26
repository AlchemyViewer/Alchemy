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

#include "alspecimenlist.h"

#include "alsurface.h"
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

    // The families a field offers, by name and by the label fonts.xml gives
    // each: every declared one, or those marked for choosing -- and the name
    // the field has now, should it be one the list would not offer, so that
    // it can be kept.
    std::vector<std::pair<std::string, std::string>> familiesFor(ALFontField::Families families, const std::string& current)
    {
        std::vector<std::pair<std::string, std::string>> named;
        if (families == ALFontField::Families::Declared)
        {
            for (const std::string& name : LLFontGL::getDeclaredFontNames())
            {
                named.emplace_back(name, name);
            }
            return named;
        }
        using Filter              = LLFontRegistry::FamilyFilter;
        const Filter       filter = families == ALFontField::Families::Monospace      ? Filter::MONOSPACE
                                    : families == ALFontField::Families::Proportional ? Filter::PROPORTIONAL
                                                                                      : Filter::ANY;
        for (const LLFontRegistry::FamilyInfo& family : LLFontGL::getAvailableFamilies(filter))
        {
            named.emplace_back(family.name, family.label.empty() ? family.name : family.label);
        }
        if (!current.empty() && std::none_of(named.begin(), named.end(), [&current](const auto& one) { return one.first == current; }))
        {
            named.insert(named.begin(), { current, current });
        }
        return named;
    }

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

            // The names, one per row, each beside the specimen drawn in the
            // font it names: a font is recognised rather than read. Chosen by
            // a click, which the preview shows; a double-click is the choice
            // and the yes together.
            ALSpecimenList::Params lp(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
            lp.name        = "fonts";
            lp.rect        = LLRect(4, top, right, top - LIST_HEIGHT);
            lp.row_height  = ROW;
            lp.label_width = 146;
            mList          = LLUICtrlFactory::create<ALSpecimenList>(lp);
            mFamilies      = familiesFor(families, mName);
            std::vector<ALSpecimenList::Specimen> specimens;
            for (const auto& [family, label] : mFamilies)
            {
                ALSpecimenList::Specimen one;
                one.label   = label;
                one.value   = family;
                one.toolTip = family;
                one.font    = fontOf(family, LLStringUtil::null, LLStringUtil::null);
                one.sample  = SPECIMEN;
                specimens.push_back(std::move(one));
            }
            mList->setSpecimens(std::move(specimens));
            mList->setChosen(mName);
            mList->onChose([this](const std::string& chosen)
            {
                mName = chosen;
                refreshPreview();
            });
            mList->onPicked([this](const std::string&) { settle(); });
            addChild(mList);
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
            std::string said = mName;
            for (const auto& [family, label] : mFamilies)
            {
                if (family == mName)
                {
                    said = label;
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
            mPreview->setText(said + "  " + SPECIMEN, style);
        }

        bool                mSettled = false;
        std::string         mName;
        std::string         mSize;
        std::string         mStyle;
        LLFilterEditor*     mFilter = nullptr;
        ALSpecimenList*     mList = nullptr;
        // The families offered, by name and by what the list calls each.
        std::vector<std::pair<std::string, std::string>> mFamilies;
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
    // Escaped: nothing picked in it is taken by a field going away.
    mPopover.close();
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
    const LLUIColor& ink = ALSurface::text();
    const LLUIColor& edge = ALSurface::well();
    const LLRect sample(0, getRect().getHeight() - 2, mSampleWidth, 2);
    gl_rect_2d(sample, edge.get(), false);
    font()->renderUTF8(SAMPLE, 0, sample.mLeft + 3, sample.mBottom + 2,
                       ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM);
    if (!mEditable)
    {
        // The choice, said in itself, where the name would be typed.
        const LLRect box(mSampleWidth + 3, getRect().getHeight() - 2, getRect().getWidth(), 2);
        gl_rect_2d(box, edge.get(), false);
        font()->renderUTF8(describe(), 0, static_cast<F32>(box.mLeft + 6), static_cast<F32>(box.mBottom + 2), ink.get(), LLFontGL::LEFT, LLFontGL::BOTTOM,
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
    static constexpr S32 POPOVER_HEIGHT = 4 + ROW + 4 + LIST_HEIGHT + 6 + ROW + 4 + ROW + 4 + PREVIEW_HEIGHT + 6 + ROW + 4;
    ALFontPopover* popover = new ALFontPopover(ALPopover::paramsFor(POPOVER_WIDTH, POPOVER_HEIGHT),
                                               mFamilies, mName, mSize, mStyle);
    // Told as it goes, while it still holds what was picked; only OK, or
    // a double-click on a font, is a yes -- escape, cancel and looking
    // away keep what the field had. Held before it opens, so the one up is
    // escaped from this side first: whatever was picked in it is dropped,
    // since a popover closing itself is the path that keeps a choice.
    mPopover.hold(popover, [this, held = popover->getDerivedHandle<ALFontPopover>()](bool)
    {
        if (ALFontPopover* said = held.get(); said && said->settled())
        {
            apply(said->name(), said->size(), said->style());
        }
    });
    popover->openBeside(this);
}

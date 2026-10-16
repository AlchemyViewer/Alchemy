/**
 * @file llscrolllistcell.cpp
 * @brief Scroll lists are composed of rows (items), each of which
 * contains columns (cells).
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llscrolllistcell.h"

#include "llcheckboxctrl.h"
#include "llfonttextcache.h"
#include "llui.h"   // LLUIImage
#include "lluictrlfactory.h"

namespace
{
    // Where left-aligned cell text starts. Named because the draw and the
    // search highlight both have to place themselves from it, and a highlight
    // sitting one pixel off the text it is behind is not something anyone
    // would look twice at.
    const S32 TEXT_LEFT_PAD = 1;
}

//static
LLScrollListCell* LLScrollListCell::create(const LLScrollListCell::Params& cell_p)
{
    LLScrollListCell* cell = NULL;

    if (cell_p.type() == "icon")
    {
        cell = new LLScrollListIcon(cell_p);
    }
    else if (cell_p.type() == "checkbox")
    {
        cell = new LLScrollListCheck(cell_p);
    }
    else if (cell_p.type() == "date")
    {
        cell = new LLScrollListDate(cell_p);
    }
    else if (cell_p.type() == "icontext")
    {
        cell = new LLScrollListIconText(cell_p);
    }
    else if (cell_p.type() == "bar")
    {
        cell = new LLScrollListBar(cell_p);
    }
    else if(cell_p.type() == "line_editor")
    {
        cell = new LLScrollListLineEditor(cell_p);
    }
    else    // default is "text"
    {
        cell = new LLScrollListText(cell_p);
    }

    if (cell_p.value.isProvided())
    {
        cell->setValue(cell_p.value);
    }

    return cell;
}


LLScrollListCell::LLScrollListCell(const LLScrollListCell::Params& p)
:   mWidth(p.width),
// [SL:KB] - Patch: Control-ScrollList | Checked: Catznip-5.2
    mColumnName(p.column),
// [/SL:KB]
    mToolTip(p.tool_tip)
{}

// virtual
const LLSD LLScrollListCell::getValue() const
{
    return LLStringUtil::null;
}


// virtual
const LLSD LLScrollListCell::getAltValue() const
{
    return LLStringUtil::null;
}


//
// LLScrollListIcon
//
LLScrollListIcon::LLScrollListIcon(const LLScrollListCell::Params& p)
:   LLScrollListCell(p),
    mIcon(LLUI::getUIImage(p.value().asString())),
    mIconSize(0),
    mColor(p.color),
    mAlignment(p.font_halign),
    mCallback(NULL),
    mUserData(NULL)
{}

LLScrollListIcon::~LLScrollListIcon()
{
}

/*virtual*/
S32     LLScrollListIcon::getHeight() const
{ return mIcon ? mIcon->getHeight() : 0; }

/*virtual*/
const LLSD      LLScrollListIcon::getValue() const
{ return mIcon.isNull() ? LLStringUtil::null : mIcon->getName(); }

void LLScrollListIcon::setValue(const LLSD& value)
{
    if (value.isUUID())
    {
        // don't use default image specified by LLUUID::null, use no image in that case
        LLUUID image_id = value.asUUID();
        mIcon = image_id.notNull() ? LLUI::getUIImageByID(image_id) : LLUIImagePtr(NULL);
    }
    else
    {
        std::string value_string = value.asString();
        if (LLUUID::validate(value_string))
        {
            setValue(LLUUID(value_string));
        }
        else if (!value_string.empty())
        {
            mIcon = LLUI::getUIImage(value.asString());
        }
        else
        {
            mIcon = NULL;
        }
    }
}

void LLScrollListIcon::setColor(const LLColor4& color)
{
    mColor = color;
}

void LLScrollListIcon::setIconSize(S32 size)
{
    mIconSize = size;
}

S32 LLScrollListIcon::getWidth() const
{
    // if no specified fix width, use width of icon
    if (LLScrollListCell::getWidth() != 0)
    {
        return LLScrollListCell::getWidth();
    }
    if (mIconSize != 0)
    {
        return mIconSize;
    }
    if (mIcon.notNull())
    {
        return mIcon->getWidth();
    }
    return 0;
}


void LLScrollListIcon::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    if (mIcon)
    {
        S32 draw_width = mIcon->getWidth();
        S32 draw_height = mIcon->getHeight();
        if (mIconSize != 0)
        {
            draw_width = mIconSize;
            draw_height = mIconSize;
        } // else will draw full icon even if cell is smaller
        switch(mAlignment)
        {
        case LLFontGL::LEFT:
            mIcon->draw(0, 0, draw_width, draw_height, mColor);
            break;
        case LLFontGL::RIGHT:
            mIcon->draw(getWidth() - draw_width, 0, draw_width, draw_height, mColor);
            break;
        case LLFontGL::HCENTER:
            mIcon->draw((getWidth() - draw_width) / 2, 0, draw_width, draw_height, mColor);
            break;
        default:
            break;
        }
    }
}

//
// LLScrollListBar
//
LLScrollListBar::LLScrollListBar(const LLScrollListCell::Params& p)
    :   LLScrollListCell(p),
    mRatio(0),
    mColor(p.color),
    mBottom(1),
    mLeftPad(1),
    mRightPad(1)
{}

LLScrollListBar::~LLScrollListBar()
{
}

/*virtual*/
S32 LLScrollListBar::getHeight() const
{
    return LLScrollListCell::getHeight();
}

/*virtual*/
const LLSD LLScrollListBar::getValue() const
{
    return LLStringUtil::null;
}

void LLScrollListBar::setValue(const LLSD& value)
{
    if (value.has("ratio"))
    {
        mRatio = (F32)value["ratio"].asReal();
    }
    if (value.has("bottom"))
    {
        mBottom = value["bottom"].asInteger();
    }
    if (value.has("left_pad"))
    {
        mLeftPad = value["left_pad"].asInteger();
    }
    if (value.has("right_pad"))
    {
        mRightPad = value["right_pad"].asInteger();
    }
}

void LLScrollListBar::setColor(const LLColor4& color)
{
    mColor = color;
}

S32 LLScrollListBar::getWidth() const
{
    return LLScrollListCell::getWidth();
}


void LLScrollListBar::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    S32 bar_width = getWidth() - mLeftPad - mRightPad;
    S32 left = (S32)(bar_width - bar_width * mRatio);
    left = llclamp(left, mLeftPad, getWidth() - mRightPad - 1);

    gl_rect_2d(left, mBottom, getWidth() - mRightPad, mBottom - 1, mColor);
}

//
// LLScrollListText
//
U32 LLScrollListText::sCount = 0;

LLScrollListText::LLScrollListText(const LLScrollListCell::Params& p)
:   LLScrollListCell(p),
    mText(p.label.isProvided() ? p.label() : p.value().asString()),
    mAltText(p.alt_value().asString()),
    mFont(p.font),
    mColor(p.color),
    mUseColor(p.color.isProvided()),
    mFontAlignment(p.font_halign),
    mVisible(p.visible),
    mHighlightCount( 0 ),
    mHighlightOffset( 0 )
{
    sCount++;

    mTextWidth = getWidth();

    // initialize rounded rect image
    if (!mRoundedRectImage)
    {
        mRoundedRectImage = LLUI::getUIImage("Rounded_Square");
    }
}

//virtual
void LLScrollListText::highlightText(S32 byte_offset, S32 num_bytes)
{
    mHighlightOffset = byte_offset;
    mHighlightCount = llmax(0, num_bytes);
}

//virtual
bool LLScrollListText::isText() const
{
    return true;
}

// virtual
const std::string &LLScrollListText::getToolTip() const
{
    // If base class has a tooltip, return that
    if (! LLScrollListCell::getToolTip().empty())
        return LLScrollListCell::getToolTip();

    // ...otherwise, return the value itself as the tooltip
    return mText.getString();
}

// virtual
bool LLScrollListText::needsToolTip() const
{
    // If base class has a tooltip, return that
    if (LLScrollListCell::needsToolTip())
        return LLScrollListCell::needsToolTip();

    // ...otherwise, show tooltips for truncated text
    return cachedWidth() > getWidth();
}

void LLScrollListText::setTextWidth(S32 value)
{
    mTextWidth = value;
}

void LLScrollListText::setWidth(S32 width)
{
    LLScrollListCell::setWidth(width);
    mTextWidth = width;
}

//virtual
bool LLScrollListText::getVisible() const
{
    return mVisible;
}

//virtual
S32 LLScrollListText::getHeight() const
{
    return mFont->getLineHeight();
}


LLScrollListText::~LLScrollListText()
{
    sCount--;
}

S32 LLScrollListText::cachedWidth(S32 offset, S32 max_bytes) const
{
    mFontBuffer.setSource(&mText, mText.getGeneration());
    return llceil(mFontBuffer.getWidthBytes(mFont, mText.getString(), offset, max_bytes, false));
}

S32 LLScrollListText::getContentWidth() const
{
    return cachedWidth();
}


void LLScrollListText::setColor(const LLColor4& color)
{
    mColor = color;
    mUseColor = true;
}

void LLScrollListText::setText(ALStringViewExplicit text)
{
    mText.assign(text);
}

void LLScrollListText::setFontStyle(const U8 font_style)
{
    LLFontDescriptor new_desc(mFont->getFontDesc());
    new_desc.setStyle(font_style);
    mFont = LLFontGL::getFont(new_desc);
}

void LLScrollListText::setAlignment(LLFontGL::HAlign align)
{
    mFontAlignment = align;
}

//virtual
void LLScrollListText::setValue(const LLSD& text)
{
    setText(text.asString());
}

//virtual
void LLScrollListText::setAltValue(const LLSD& text)
{
    mAltText = text.asString();
}

//virtual
const LLSD LLScrollListText::getValue() const
{
    return LLSD(mText.getString());
}

//virtual
const LLSD LLScrollListText::getAltValue() const
{
    return LLSD(mAltText.getString());
}


void LLScrollListText::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    LLColor4 display_color;
    if (mUseColor)
    {
        display_color = mColor;
    }
    else
    {
        display_color = color;
    }

    if (mHighlightCount > 0)
    {
        // Where the match sits: the left edge of the text, as the alignment
        // below places it, plus the width of what comes before the match.
        //
        // Both halves used to be one measurement with the pen position in the
        // begin-offset slot -- a pixel count read as a byte index. What that
        // measured was the text from a byte or two in, for as many bytes as
        // the match started at, which happens to be nothing at all when the
        // match is at the start of the cell. That is the case a type-ahead
        // search produces, and it is why this went unseen.
        const S32 prefix = cachedWidth(0, mHighlightOffset);
        S32 left = 0;
        switch(mFontAlignment)
        {
        case LLFontGL::LEFT:
            left = TEXT_LEFT_PAD + prefix;
            break;
        case LLFontGL::RIGHT:
            left = getWidth() - cachedWidth() + prefix;
            break;
        case LLFontGL::HCENTER:
            left = (getWidth() - cachedWidth()) / 2 + prefix;
            break;
        }
        LLRect highlight_rect(left - 2,
                mFont->getLineHeight() + 1,
                left + cachedWidth(mHighlightOffset, mHighlightCount) + 1,
                1);
        mRoundedRectImage->draw(highlight_rect, highlight_color);
    }

    // Try to draw the entire string
    F32 right_x;
    U32 string_bytes = (U32)mText.getString().size();
    F32 start_x = 0.f;
    switch(mFontAlignment)
    {
    case LLFontGL::LEFT:
        start_x = (F32)TEXT_LEFT_PAD;
        break;
    case LLFontGL::RIGHT:
        start_x = (F32)getWidth();
        break;
    case LLFontGL::HCENTER:
        start_x = (F32)getWidth() * 0.5f;
        break;
    }
    mFontBuffer.setSource(&mText, mText.getGeneration());
    mFontBuffer.renderBytes(mFont,
                       mText.getString(), 0,
                       start_x, 0.f,
                       display_color,
                       mFontAlignment,
                       LLFontGL::BOTTOM,
                       0,
                       LLFontGL::NO_SHADOW,
                       string_bytes,
                       getTextWidth(),
                       &right_x,
                       true);
}

//
// LLScrollListCheck
//
LLScrollListCheck::LLScrollListCheck(const LLScrollListCell::Params& p)
:   LLScrollListCell(p)
{
    LLCheckBoxCtrl::Params checkbox_p;
    checkbox_p.name("checkbox");
// [SL:KB] - Patch: Control-ScrollList | Checked: Catznip-5.2
    if (p.commit_callback.isProvided())
    {
        if (!mCommitSignal)
            mCommitSignal = new commit_signal_t();
        mCommitSignal->connect(p.commit_callback());
    }
// [/SL:KB]
    checkbox_p.rect = LLRect(0, p.width, p.width, 0);
    checkbox_p.enabled(p.enabled);
    checkbox_p.initial_value(p.value());

    mCheckBox = LLUICtrlFactory::create<LLCheckBoxCtrl>(checkbox_p);

    LLRect rect(mCheckBox->getRect());
    if (p.width)
    {
        rect.mRight = rect.mLeft + p.width;
        mCheckBox->setRect(rect);
        setWidth(p.width);
    }
    else
    {
        setWidth(rect.getWidth()); //check_box->getWidth();
    }

    mCheckBox->setColor(p.color());
}


LLScrollListCheck::~LLScrollListCheck()
{
// [SL:KB] - Patch: Control-ScrollList | Checked: Catznip-5.2
    delete mCommitSignal;
// [/SL:KB]
    delete mCheckBox;
    mCheckBox = NULL;
}

void LLScrollListCheck::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    mCheckBox->draw();
}

bool LLScrollListCheck::handleClick()
{
    if (mCheckBox->getEnabled())
    {
        mCheckBox->toggle();
    }
    // don't change selection when clicking on embedded checkbox
    return true;
}

/*virtual*/
const LLSD LLScrollListCheck::getValue() const
{
    return mCheckBox->getValue();
}

/*virtual*/
void LLScrollListCheck::setValue(const LLSD& value)
{
    mCheckBox->setValue(value);
}

/*virtual*/
void LLScrollListCheck::onCommit()
{
    mCheckBox->onCommit();
// [SL:KB] - Patch: Control-ScrollList | Checked: Catznip-5.2
    if (mCommitSignal)
        (*mCommitSignal)(this);
// [/SL:KB]
}

/*virtual*/
void LLScrollListCheck::setEnabled(bool enable)
{
    mCheckBox->setEnabled(enable);
}

//
// LLScrollListDate
//

LLScrollListDate::LLScrollListDate( const LLScrollListCell::Params& p)
:   LLScrollListText(p),
    mDate(p.value().asDate())
{}

void LLScrollListDate::setValue(const LLSD& value)
{
    mDate = value.asDate();
    LLScrollListText::setValue(mDate.asRFC1123());
}

const LLSD LLScrollListDate::getValue() const
{
    return mDate;
}

//
// LLScrollListIconText
//
LLScrollListIconText::LLScrollListIconText(const LLScrollListCell::Params& p)
    : LLScrollListText(p),
    mIcon(p.value().isUUID() ? LLUI::getUIImageByID(p.value().asUUID()) : LLUI::getUIImage(p.value().asString())),
    mPad(4)
{
    mTextWidth = getWidth() - getIconSpace();
}

S32 LLScrollListIconText::getIconSpace() const
{
    return mIcon ? (mFont->getLineHeight() + mPad) : 0;
}

S32 LLScrollListIconText::getContentWidth() const
{
    // The icon is content too, and a column sized to fit its contents was
    // coming out short by exactly the icon it was going to draw.
    return LLScrollListText::getContentWidth() + getIconSpace();
}

LLScrollListIconText::~LLScrollListIconText()
{
}

const LLSD LLScrollListIconText::getValue() const
{
    if (mIcon.isNull())
    {
        return LLStringUtil::null;
    }
    return mIcon->getName();
}

void LLScrollListIconText::setValue(const LLSD& value)
{
    if (value.isUUID())
    {
        // don't use default image specified by LLUUID::null, use no image in that case
        LLUUID image_id = value.asUUID();
        mIcon = image_id.notNull() ? LLUI::getUIImageByID(image_id) : LLUIImagePtr(NULL);
    }
    else
    {
        std::string value_string = value.asString();
        if (LLUUID::validate(value_string))
        {
            setValue(LLUUID(value_string));
        }
        else if (!value_string.empty())
        {
            mIcon = LLUI::getUIImage(value.asString());
        }
        else
        {
            mIcon = NULL;
        }
    }
    // Gaining or losing the icon changes how much room the text has, and a
    // scroll list reassigns cell values as it scrolls.
    mTextWidth = getWidth() - getIconSpace();
}

void LLScrollListIconText::setWidth(S32 width)
{
    LLScrollListCell::setWidth(width);
    mTextWidth = width - getIconSpace();
}


void LLScrollListIconText::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    LLColor4 display_color;
    if (mUseColor)
    {
        display_color = mColor;
    }
    else
    {
        display_color = color;
    }

    S32 icon_height = mFont->getLineHeight();
    S32 icon_space = getIconSpace();

    if (mHighlightCount > 0)
    {
        // The same reading as the plain text cell above, and the icon moves
        // the text rather than the match within it: the icon's width belongs
        // to where the text begins, which is what the alignment below says,
        // and not to the offset the match sits at. Right-aligned it was
        // subtracted from a left edge the icon does not move, and centred it
        // was subtracted where the draw adds it.
        const S32 prefix = cachedWidth(0, mHighlightOffset);
        S32 left = 0;
        switch (mFontAlignment)
        {
        case LLFontGL::LEFT:
            left = icon_space + TEXT_LEFT_PAD + prefix;
            break;
        case LLFontGL::RIGHT:
            left = getWidth() - cachedWidth() + prefix;
            break;
        case LLFontGL::HCENTER:
            left = (getWidth() + icon_space - cachedWidth()) / 2 + prefix;
            break;
        }
        LLRect highlight_rect(left - 2,
            mFont->getLineHeight() + 1,
            left + cachedWidth(mHighlightOffset, mHighlightCount) + 1,
            1);
        mRoundedRectImage->draw(highlight_rect, highlight_color);
    }

    // Try to draw the entire string
    F32 right_x;
    U32 string_bytes = (U32)mText.getString().size();
    F32 start_text_x = 0.f;
    S32 start_icon_x = 0;
    switch (mFontAlignment)
    {
    case LLFontGL::LEFT:
        start_text_x = (F32)(icon_space + TEXT_LEFT_PAD);
        start_icon_x = TEXT_LEFT_PAD;
        break;
    case LLFontGL::RIGHT:
        start_text_x = (F32)getWidth();
        start_icon_x = getWidth() - cachedWidth() - icon_space;
        break;
    case LLFontGL::HCENTER:
        F32 center = (F32)getWidth()* 0.5f;
        start_text_x = center + ((F32)icon_space * 0.5f);
        start_icon_x = (S32)(center - (((F32)icon_space + (F32)cachedWidth()) * 0.5f));
        break;
    }
    mFontBuffer.setSource(&mText, mText.getGeneration());
    mFontBuffer.renderBytes(
        mFont,
        mText.getString(), 0,
        start_text_x, 0.f,
        display_color,
        mFontAlignment,
        LLFontGL::BOTTOM,
        0,
        LLFontGL::NO_SHADOW,
        string_bytes,
        getTextWidth(),
        &right_x,
        true);

    if (mIcon)
    {
        mIcon->draw(start_icon_x, 0, icon_height, icon_height, mColor);
    }
}

//
// LLScrollListLineEditor
//
LLScrollListLineEditor::LLScrollListLineEditor( const LLScrollListCell::Params& p)
: LLScrollListCell(p)
{
    LLLineEditor::Params line_editor_p;
    line_editor_p.name("line_editor");
    line_editor_p.rect = LLRect(0, p.width, p.width, 0);
    line_editor_p.enabled(p.enabled);
    line_editor_p.initial_value(p.value());

    mLineEditor = LLUICtrlFactory::create<LLLineEditor>(line_editor_p);

    LLRect rect(mLineEditor->getRect());
    if (p.width())
    {
        rect.mRight = rect.mLeft + p.width();
        mLineEditor->setRect(rect);
        setWidth(p.width());
    }
    else
    {
        setWidth(rect.getWidth()); //line_editor->getWidth();
    }
}

LLScrollListLineEditor::~LLScrollListLineEditor()
{
    delete mLineEditor;
    mLineEditor = NULL;
}

void LLScrollListLineEditor::draw(const LLColor4& color, const LLColor4& highlight_color)
{
    mLineEditor->draw();
}

bool LLScrollListLineEditor::handleClick()
{
    if (mLineEditor->getEnabled())
    {
        mLineEditor->setFocus(true);
        mLineEditor->selectAll();
    }
    // return value changes selection?
    return false; //true;
}

bool LLScrollListLineEditor::handleUnicodeChar(llwchar uni_char, bool called_from_parent)
{
    return true;
}

bool LLScrollListLineEditor::handleUnicodeCharHere(llwchar uni_char )
{
    return true;
}


/**
 * @file LLAccordionCtrlTab.cpp
 * @brief Collapsible control implementation
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#include "llaccordionctrltab.h"
#include "llaccordionctrl.h"

#include "lllocalcliprect.h"
#include "llscrollbar.h"
#include "lltextbox.h"
#include "lltextutil.h"
#include "lluictrl.h"

static const std::string DD_BUTTON_NAME = "dd_button";
static const std::string DD_TEXTBOX_NAME = "dd_textbox";
static const std::string DD_HEADER_NAME = "dd_header";

static const S32 HEADER_HEIGHT = 25;
static const S32 HEADER_IMAGE_LEFT_OFFSET = 5;
static const S32 HEADER_TEXT_LEFT_OFFSET = 30;
static const F32 AUTO_OPEN_TIME = 1.f;
static const S32 VERTICAL_MULTIPLE = 16;
static const S32 PARENT_BORDER_MARGIN = 5;

// Optional header checkbox. It sits at the right end because the left is
// already spoken for by the expand arrow and the title, and because the right
// end is the one place a column of them lines up down an accordion.
static const S32 HEADER_CHECKBOX_RIGHT_PAD = 6;
// Between the checkbox and the title, which ellipses rather than run under it.
static const S32 HEADER_CHECKBOX_TEXT_GAP = 6;
// Default size, used when the XUI gives no width or height. Wide enough for
// the 13px box and the slack LLCheckBoxCtrl puts around it, and no wider,
// since a bare checkbox is what a header wants; anything with a label has to
// state its own width.
static const S32 HEADER_CHECKBOX_WIDTH = 24;
static const S32 HEADER_CHECKBOX_HEIGHT = 16;

static LLDefaultChildRegistry::Register<LLAccordionCtrlTab> t1("accordion_tab");

class LLAccordionCtrlTab::LLAccordionCtrlTabHeader : public LLUICtrl
{
public:
    friend class LLUICtrlFactory;

    struct Params : public LLInitParam::Block<Params, LLAccordionCtrlTab::Params>
    {
        Params();
    };

    LLAccordionCtrlTabHeader(const LLAccordionCtrlTabHeader::Params& p);

    virtual ~LLAccordionCtrlTabHeader();

    virtual void draw();

    virtual void reshape(S32 width, S32 height, bool called_from_parent = true);

    virtual bool postBuild();

    std::string getTitle();
    void setTitle(const std::string& title, const std::string& hl);

    void setTitleFontStyle(std::string style);

    void setTitleColor(LLUIColor);

    void setSelected(bool is_selected) { mIsSelected = is_selected; }

    // Adopt the checkbox the tab built. Kept null otherwise, and every use of
    // it in this file is guarded, so a header without one behaves exactly as
    // it did before there was such a thing.
    void setHeaderCheckBox(LLCheckBoxCtrl* checkbox);
    LLCheckBoxCtrl* getHeaderCheckBox() const { return mHeaderCheckBox; }

    virtual void onMouseEnter(S32 x, S32 y, MASK mask);
    virtual void onMouseLeave(S32 x, S32 y, MASK mask);
    virtual bool handleKey(KEY key, MASK mask, bool called_from_parent);
    virtual bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                                   EDragAndDropType cargo_type,
                                   void* cargo_data,
                                   EAcceptance* accept,
                                   std::string& tooltip_msg);

private:
    LLTextBox* mHeaderTextbox;
    LLCheckBoxCtrl* mHeaderCheckBox = nullptr;

    // Overlay images (arrows)
    LLPointer<LLUIImage> mImageCollapsed;
    LLPointer<LLUIImage> mImageExpanded;
    LLPointer<LLUIImage> mImageCollapsedPressed;
    LLPointer<LLUIImage> mImageExpandedPressed;

    // Background images
    LLPointer<LLUIImage> mImageHeader;
    LLPointer<LLUIImage> mImageHeaderOver;
    LLPointer<LLUIImage> mImageHeaderPressed;
    LLPointer<LLUIImage> mImageHeaderFocused;

    // style saved when applying it in setTitleFontStyle
    LLStyle::Params mStyleParams;

    LLUIColor mHeaderBGColor;

    bool mNeedsHighlight;
    bool mIsSelected;

    LLFrameTimer mAutoOpenTimer;
};

LLAccordionCtrlTab::LLAccordionCtrlTabHeader::Params::Params()
{
}

LLAccordionCtrlTab::LLAccordionCtrlTabHeader::LLAccordionCtrlTabHeader(
    const LLAccordionCtrlTabHeader::Params& p)
: LLUICtrl(p)
, mHeaderBGColor(p.header_bg_color())
, mNeedsHighlight(false)
, mIsSelected(false),
    mImageCollapsed(p.header_collapse_img),
    mImageCollapsedPressed(p.header_collapse_img_pressed),
    mImageExpanded(p.header_expand_img),
    mImageExpandedPressed(p.header_expand_img_pressed),
    mImageHeader(p.header_image),
    mImageHeaderOver(p.header_image_over),
    mImageHeaderPressed(p.header_image_pressed),
    mImageHeaderFocused(p.header_image_focused)
{
    LLTextBox::Params textboxParams;
    textboxParams.name(DD_TEXTBOX_NAME);
    textboxParams.initial_value(p.title());
    textboxParams.text_color(p.header_text_color());
    textboxParams.follows.flags(FOLLOWS_NONE);
    textboxParams.font( p.font() );
    textboxParams.font_shadow(LLFontGL::NO_SHADOW);
    textboxParams.use_ellipses = true;
    textboxParams.bg_visible = false;
    textboxParams.mouse_opaque = false;
    textboxParams.parse_urls = false;
    mHeaderTextbox = LLUICtrlFactory::create<LLTextBox>(textboxParams);
    addChild(mHeaderTextbox);
}

LLAccordionCtrlTab::LLAccordionCtrlTabHeader::~LLAccordionCtrlTabHeader()
{
}

bool LLAccordionCtrlTab::LLAccordionCtrlTabHeader::postBuild()
{
    return true;
}

std::string LLAccordionCtrlTab::LLAccordionCtrlTabHeader::getTitle()
{
    if (mHeaderTextbox)
    {
        return mHeaderTextbox->getText();
    }

    return LLStringUtil::null;
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::setTitle(const std::string& title, const std::string& hl)
{
    if (mHeaderTextbox)
    {
        LLTextUtil::textboxSetHighlightedVal(
            mHeaderTextbox,
            mStyleParams,
            title,
            hl);
    }
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::setTitleFontStyle(std::string style)
{
    if (mHeaderTextbox)
    {
        std::string text = mHeaderTextbox->getText();
        mStyleParams.font(mHeaderTextbox->getFont());
        mStyleParams.font.style(style);
        mHeaderTextbox->setText(text, mStyleParams);
    }
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::setTitleColor(LLUIColor color)
{
    if (mHeaderTextbox)
    {
        mHeaderTextbox->setColor(color);
    }
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::setHeaderCheckBox(LLCheckBoxCtrl* checkbox)
{
    mHeaderCheckBox = checkbox;
    addChild(checkbox);

    // Where it goes is decided in reshape, which has the header's size; this
    // just makes sure it is somewhere reasonable if reshape has already run.
    reshape(getRect().getWidth(), getRect().getHeight());
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::draw()
{
    S32 width = getRect().getWidth();
    S32 height = getRect().getHeight();

    F32 alpha = getCurrentTransparency();
    gl_rect_2d(0, 0, width - 1, height - 1, mHeaderBGColor.get() % alpha, true);

    LLAccordionCtrlTab* parent = dynamic_cast<LLAccordionCtrlTab*>(getParent());
    bool collapsible = parent && parent->getCollapsible();
    bool expanded = parent && parent->getDisplayChildren();

    // Handle overlay images, if needed
    // Only show green "focus" background image if the accordion is open,
    // because the user's mental model of focus is that it goes away after
    // the accordion is closed.
    if (getParent()->hasFocus() || mIsSelected
        /*&& !(collapsible && !expanded)*/ // WHY??
        )
    {
        mImageHeaderFocused->draw(0, 0, width, height);
    }
    else
    {
        mImageHeader->draw(0, 0, width, height);
    }

    if (mNeedsHighlight)
    {
        mImageHeaderOver->draw(0, 0, width, height);
    }

    if (collapsible)
    {
        LLPointer<LLUIImage> overlay_image;
        if (expanded)
        {
            overlay_image = mImageExpanded;
        }
        else
        {
            overlay_image = mImageCollapsed;
        }
        overlay_image->draw(HEADER_IMAGE_LEFT_OFFSET, (height - overlay_image->getHeight()) / 2);
    }

    LLUICtrl::draw();
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::reshape(S32 width, S32 height, bool called_from_parent /* = true */)
{
    // This override never chains to LLUICtrl::reshape, so follows flags do
    // nothing to header children and everything in here is positioned by
    // hand. The checkbox is placed first because the title has to stop short
    // of it; without one, text_right stays the full width and the rest of
    // this function is what it always was.
    S32 text_right = width;
    if (mHeaderCheckBox)
    {
        const LLRect& old_check_rect = mHeaderCheckBox->getRect();
        LLRect check_rect;
        check_rect.setLeftTopAndSize(width - HEADER_CHECKBOX_RIGHT_PAD - old_check_rect.getWidth(),
                                     (height + old_check_rect.getHeight()) / 2,
                                     old_check_rect.getWidth(),
                                     old_check_rect.getHeight());
        if (check_rect != old_check_rect)
        {
            // setRect rather than reshape: the size is not changing, only the
            // position, and LLCheckBoxCtrl::reshape does nothing when the
            // size is unchanged. The bounding rect is what the mouse is
            // tested against, so it has to be told the control moved.
            mHeaderCheckBox->setRect(check_rect);
            mHeaderCheckBox->updateBoundingRect();
        }

        // A header narrow enough for these to cross would otherwise hand the
        // textbox an inside-out rect.
        text_right = llmax(check_rect.mLeft - HEADER_CHECKBOX_TEXT_GAP, HEADER_TEXT_LEFT_OFFSET);
    }

    S32 header_height = mHeaderTextbox->getTextPixelHeight();
    LLRect old_header_rect = mHeaderTextbox->getRect();

    LLRect textboxRect(HEADER_TEXT_LEFT_OFFSET, (height + header_height) / 2, text_right, (height - header_height) / 2);
    if (old_header_rect.getHeight() != textboxRect.getHeight()
        || old_header_rect.mLeft != textboxRect.mLeft
        || old_header_rect.mTop != textboxRect.mTop
        || old_header_rect.getWidth() > textboxRect.getWidth() // reducing header's width
        || (old_header_rect.getWidth() < textboxRect.getWidth() && old_header_rect.getWidth() < mHeaderTextbox->getTextPixelWidth()))
    {
        // Expensive text reflow
        // Update if position or height changes
        // Update if width reduces
        // But do not update if text already fits and width increases (arguably LLTextBox::reshape should be smarter, not Accordion)
        mHeaderTextbox->reshape(textboxRect.getWidth(), textboxRect.getHeight());
        mHeaderTextbox->setRect(textboxRect);
    }

    if (mHeaderTextbox->getTextPixelWidth() > mHeaderTextbox->getRect().getWidth())
    {
        setToolTip(mHeaderTextbox->getText());
    }
    else
    {
        setToolTip(LLStringUtil::null);
    }
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::onMouseEnter(S32 x, S32 y, MASK mask)
{
    LLUICtrl::onMouseEnter(x, y, mask);
    mNeedsHighlight = true;
}

void LLAccordionCtrlTab::LLAccordionCtrlTabHeader::onMouseLeave(S32 x, S32 y, MASK mask)
{
    LLUICtrl::onMouseLeave(x, y, mask);
    mNeedsHighlight = false;
    mAutoOpenTimer.stop();
}

bool LLAccordionCtrlTab::LLAccordionCtrlTabHeader::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    if ((key == KEY_LEFT || key == KEY_RIGHT) && mask == MASK_NONE)
    {
        return getParent()->handleKey(key, mask, called_from_parent);
    }

    return LLUICtrl::handleKey(key, mask, called_from_parent);
}

bool LLAccordionCtrlTab::LLAccordionCtrlTabHeader::handleDragAndDrop(S32 x, S32 y, MASK mask,
                                                                     bool drop,
                                                                     EDragAndDropType cargo_type,
                                                                     void* cargo_data,
                                                                     EAcceptance* accept,
                                                                     std::string& tooltip_msg)
{
    LLAccordionCtrlTab* parent = dynamic_cast<LLAccordionCtrlTab*>(getParent());

    if (parent && !parent->getDisplayChildren() && parent->getCollapsible() && parent->canOpenClose())
    {
        if (mAutoOpenTimer.getStarted())
        {
            if (mAutoOpenTimer.getElapsedTimeF32() > AUTO_OPEN_TIME)
            {
                parent->changeOpenClose(false);
                mAutoOpenTimer.stop();
                return true;
            }
        }
        else
        {
            mAutoOpenTimer.start();
        }
    }

    return LLUICtrl::handleDragAndDrop(x, y, mask, drop, cargo_type,
                                       cargo_data, accept, tooltip_msg);
}

LLAccordionCtrlTab::Params::Params()
    : title("title")
    ,display_children("expanded", true)
    ,header_height("header_height", HEADER_HEIGHT),
    min_width("min_width", 0),
    min_height("min_height", 0)
    ,collapsible("collapsible", true)
    ,header_bg_color("header_bg_color")
    ,dropdown_bg_color("dropdown_bg_color")
    ,header_visible("header_visible",true)
    ,padding_left("padding_left",2)
    ,padding_right("padding_right",2)
    ,padding_top("padding_top",2)
    ,padding_bottom("padding_bottom",2)
    ,header_expand_img("header_expand_img")
    ,header_expand_img_pressed("header_expand_img_pressed")
    ,header_collapse_img("header_collapse_img")
    ,header_collapse_img_pressed("header_collapse_img_pressed")
    ,header_image("header_image")
    ,header_image_over("header_image_over")
    ,header_image_pressed("header_image_pressed")
    ,header_image_focused("header_image_focused")
    ,header_text_color("header_text_color")
    ,header_check_box("header_check_box")
    ,fit_panel("fit_panel",true)
    ,selection_enabled("selection_enabled", false)
{
    changeDefault(mouse_opaque, false);
}

LLAccordionCtrlTab::LLAccordionCtrlTab(const LLAccordionCtrlTab::Params&p)
    : LLUICtrl(p)
    ,mDisplayChildren(p.display_children)
    ,mCollapsible(p.collapsible)
    ,mExpandedHeight(0)
    ,mDropdownBGColor(p.dropdown_bg_color())
    ,mHeaderVisible(p.header_visible)
    ,mPaddingLeft(p.padding_left)
    ,mPaddingRight(p.padding_right)
    ,mPaddingTop(p.padding_top)
    ,mPaddingBottom(p.padding_bottom)
    ,mCanOpenClose(true)
    ,mFitPanel(p.fit_panel)
    ,mSelectionEnabled(p.selection_enabled)
    ,mContainerPanel(NULL)
    ,mScrollbar(NULL)
    ,mHeaderCheckBox(NULL)
{
    mStoredOpenCloseState = false;
    mWasStateStored = false;
    mSkipChangesOnNotifyParent = false;

    mDropdownBGColor = LLColor4::white;
    LLAccordionCtrlTabHeader::Params headerParams;
    headerParams.name(DD_HEADER_NAME);
    headerParams.title(p.title);
    mHeader = LLUICtrlFactory::create<LLAccordionCtrlTabHeader>(headerParams);
    addChild(mHeader, 1);

    if (p.header_check_box.isProvided())
    {
        LLCheckBoxCtrl::Params checkParams = p.header_check_box;

        // The header decides where this goes on every reshape, so keep
        // follows out of it: an anchored child would be dragged somewhere
        // else first and land back here, which is at best wasted work.
        checkParams.follows.flags(FOLLOWS_NONE);

        // A checkbox is normally sized by the XUI that places it, and this
        // one is placed by us. Default it to the bare box; anything with a
        // label is the caller's to size, and saying so is the whole reason
        // this reads the params rather than always overriding them.
        if (!checkParams.rect.width.isProvided())
        {
            checkParams.rect.width = HEADER_CHECKBOX_WIDTH;
        }
        if (!checkParams.rect.height.isProvided())
        {
            checkParams.rect.height = HEADER_CHECKBOX_HEIGHT;
        }

        // Built here rather than in the header because create() resolves
        // control_name and commit_callback against the registrar scope that
        // is open now -- the floater being read from XUI -- and the header
        // is only where it ends up living.
        mHeaderCheckBox = LLUICtrlFactory::create<LLCheckBoxCtrl>(checkParams);
        mHeader->setHeaderCheckBox(mHeaderCheckBox);
    }

    LLFocusableElement::setFocusReceivedCallback(boost::bind(&LLAccordionCtrlTab::selectOnFocusReceived, this));

    if (!p.selection_enabled)
    {
        LLFocusableElement::setFocusLostCallback(boost::bind(&LLAccordionCtrlTab::deselectOnFocusLost, this));
    }

    reshape(100, 200,false);
}

LLAccordionCtrlTab::~LLAccordionCtrlTab()
{
}

void LLAccordionCtrlTab::setDisplayChildren(bool display)
{
    mDisplayChildren = display;
    LLRect rect = getRect();

    rect.mBottom = rect.mTop - (getDisplayChildren() ? mExpandedHeight : HEADER_HEIGHT);
    setRect(rect);

    if (mContainerPanel)
    {
        mContainerPanel->setVisible(getDisplayChildren());
    }

    if (mDisplayChildren)
    {
        adjustContainerPanel();
    }
    else
    {
        if (mScrollbar)
            mScrollbar->setVisible(false);
    }
}

void LLAccordionCtrlTab::reshape(S32 width, S32 height, bool called_from_parent /* = true */)
{
    LLRect headerRect;

    headerRect.setLeftTopAndSize(0, height, width, HEADER_HEIGHT);
    if (mHeader->getRect() != headerRect)
    {
        mHeader->setRect(headerRect);
        mHeader->reshape(headerRect.getWidth(), headerRect.getHeight());
    }

    if (!mDisplayChildren)
        return;

    LLRect childRect;

    childRect.setLeftTopAndSize(
        getPaddingLeft(),
        height - getHeaderHeight() - getPaddingTop(),
        width - getPaddingLeft() - getPaddingRight(),
        height - getHeaderHeight() - getPaddingTop() - getPaddingBottom() );

    adjustContainerPanel(childRect);
}

void LLAccordionCtrlTab::changeOpenClose(bool is_open)
{
    if (is_open)
        mExpandedHeight = getRect().getHeight();

    setDisplayChildren(!is_open);
    reshape(getRect().getWidth(), getRect().getHeight(), false);
    if (mCommitSignal)
    {
        (*mCommitSignal)(this, getDisplayChildren());
    }
}

void LLAccordionCtrlTab::onVisibilityChange(bool new_visibility)
{
    LLUICtrl::onVisibilityChange(new_visibility);

    notifyParent(LLSD().with("child_visibility_change", new_visibility));
}

// virtual
void LLAccordionCtrlTab::onUpdateScrollToChild(const LLUICtrl *cntrl)
{
    if (mScrollbar && mScrollbar->getVisible())
    {
        LLRect rect;
        cntrl->localRectToOtherView(cntrl->getLocalRect(), &rect, this);

        // Translate to parent coordinatess to check if we are in visible rectangle
        rect.translate(getRect().mLeft, getRect().mBottom);

        bool needs_to_scroll = false;
        const LLRect &acc_rect = getRect();
        if (!acc_rect.contains(rect))
        {
            if (acc_rect.mTop < rect.mBottom || acc_rect.mBottom > rect.mTop)
            {
                // Content fully not in view
                needs_to_scroll = true;
            }
            else if (acc_rect.getHeight() >= rect.getHeight())
            {
                // Content can be displayed fully, but only partially in view
                needs_to_scroll = true;
            }
            else if (acc_rect.mTop <= rect.mTop || acc_rect.mBottom >= rect.mBottom)
            {
                // Intersects, but too big to be displayed fully
                S32 covered_height = acc_rect.mTop > rect.mTop ? rect.mTop - acc_rect.mBottom : acc_rect.mTop - rect.mBottom;
                constexpr F32 covered_ratio = 0.7f;
                if (covered_height < covered_ratio * acc_rect.getHeight())
                {
                    // Try to show bigger portion of the content
                    needs_to_scroll = true;
                }
            }
            // else too big and in the middle of the view as is
        }
        if (needs_to_scroll)
        {
            // for accordition's scroll, height is in pixels
            // Back to local coords and calculate position for scroller
            S32 bottom = mScrollbar->getDocPos() - rect.mBottom + getRect().mBottom;
            S32 top = mScrollbar->getDocPos() - rect.mTop + getRect().mTop;

            S32 scroll_pos = llclamp(mScrollbar->getDocPos(),
                bottom, // min vertical scroll
                top); // max vertical scroll

            mScrollbar->setDocPos(scroll_pos);
        }
    }

    LLUICtrl::onUpdateScrollToChild(cntrl);
}

// Where the header checkbox is, in this tab's own coordinates, so the two
// header-band branches below can ask whether a click or a hover is really
// meant for it. Returns false when there is no checkbox, which is the answer
// for every tab that did not ask for one.
bool LLAccordionCtrlTab::pointInHeaderCheckBox(S32 x, S32 y) const
{
    if (!mHeaderCheckBox || !mHeaderCheckBox->getVisible())
    {
        return false;
    }

    LLRect check_rect;
    mHeaderCheckBox->localRectToOtherView(mHeaderCheckBox->getLocalRect(), &check_rect, this);
    return check_rect.pointInRect(x, y);
}

bool LLAccordionCtrlTab::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // The branch below claims the whole header band and opens or closes the
    // tab, so a checkbox living in that band would never be clicked. Send
    // this one press down the normal child path instead, which reaches the
    // header and then the checkbox inside it.
    //
    // Only when it is taken, though: LLCheckBoxCtrl hit-tests against its
    // bounding rect, which is the box and label rather than the whole
    // control, so a press can land inside the rect and still be refused. Left
    // at "return", that inset would be a ring of pixels around the checkbox
    // where clicking did nothing at all; falling through means it does what
    // the rest of the header does.
    if (pointInHeaderCheckBox(x, y) && LLUICtrl::handleMouseDown(x, y, mask))
    {
        return true;
    }

    if (mCollapsible && mHeaderVisible && mCanOpenClose)
    {
        if (y >= (getRect().getHeight() - HEADER_HEIGHT))
        {
            mHeader->setFocus(true);
            changeOpenClose(getDisplayChildren());

            // Reset stored state
            mWasStateStored = false;
            return true;
        }
    }
    return LLUICtrl::handleMouseDown(x,y,mask);
}

bool LLAccordionCtrlTab::handleMouseUp(S32 x, S32 y, MASK mask)
{
    return LLUICtrl::handleMouseUp(x,y,mask);
}

boost::signals2::connection LLAccordionCtrlTab::setDropDownStateChangedCallback(commit_callback_t cb)
{
    return setCommitCallback(cb);
}

bool LLAccordionCtrlTab::addChild(LLView* child, S32 tab_group)
{
    if (DD_HEADER_NAME != child->getName())
    {
        reshape(child->getRect().getWidth() , child->getRect().getHeight() + HEADER_HEIGHT );
        mExpandedHeight = getRect().getHeight();
    }

    bool res = LLUICtrl::addChild(child, tab_group);

    if (DD_HEADER_NAME != child->getName())
    {
        if (!mCollapsible)
            setDisplayChildren(true);
        else
            setDisplayChildren(getDisplayChildren());
    }

    if (!mContainerPanel)
        mContainerPanel = findContainerView();

    return res;
}

void LLAccordionCtrlTab::setAccordionView(LLView* panel)
{
    addChild(panel, 0);
}

std::string LLAccordionCtrlTab::getTitle() const
{
    if (mHeader)
    {
        return mHeader->getTitle();
    }

    return LLStringUtil::null;
}

void LLAccordionCtrlTab::setTitle(const std::string& title, const std::string& hl)
{
    if (mHeader)
    {
        mHeader->setTitle(title, hl);
    }
}

void LLAccordionCtrlTab::setTitleFontStyle(std::string style)
{
    if (mHeader)
    {
        mHeader->setTitleFontStyle(style);
    }
}

void LLAccordionCtrlTab::setTitleColor(LLUIColor color)
{
    if (mHeader)
    {
        mHeader->setTitleColor(color);
    }
}

boost::signals2::connection LLAccordionCtrlTab::setFocusReceivedCallback(const focus_signal_t::slot_type& cb)
{
    if (mHeader)
    {
        return mHeader->setFocusReceivedCallback(cb);
    }

    return boost::signals2::connection();
}

boost::signals2::connection LLAccordionCtrlTab::setFocusLostCallback(const focus_signal_t::slot_type& cb)
{
    if (mHeader)
    {
        return mHeader->setFocusLostCallback(cb);
    }

    return boost::signals2::connection();
}

void LLAccordionCtrlTab::setSelected(bool is_selected)
{
    if (mHeader)
    {
        mHeader->setSelected(is_selected);
    }
}

LLView* LLAccordionCtrlTab::findContainerView()
{
    for (auto child : *getChildList())
    {
        if (DD_HEADER_NAME != child->getName() && child->getVisible())
            return child;
    }

    return nullptr;
}

void LLAccordionCtrlTab::selectOnFocusReceived()
{
    if (getParent()) // A parent may not be set if tabs are added dynamically.
    {
        getParent()->notifyParent(LLSD().with("action", "select_current"));
    }
}

void LLAccordionCtrlTab::deselectOnFocusLost()
{
    if (getParent()) // A parent may not be set if tabs are added dynamically.
    {
        getParent()->notifyParent(LLSD().with("action", "deselect_current"));
    }
}

S32 LLAccordionCtrlTab::getHeaderHeight()
{
    return mHeaderVisible ? HEADER_HEIGHT : 0;
}

void LLAccordionCtrlTab::setHeaderVisible(bool value)
{
    if (mHeaderVisible == value)
        return;

    mHeaderVisible = value;

    if (mHeader)
    {
        mHeader->setVisible(value);
    }

    reshape(getRect().getWidth(), getRect().getHeight(), false);
};

//virtual
bool LLAccordionCtrlTab::postBuild()
{
    if (mHeader)
    {
        mHeader->setVisible(mHeaderVisible);
    }

    static LLUICachedControl<S32> scrollbar_size("UIScrollbarSize", 0);

    LLRect scroll_rect;
    scroll_rect.setOriginAndSize(
        getRect().getWidth() - scrollbar_size,
        1,
        scrollbar_size,
        getRect().getHeight() - 1);

    mContainerPanel = findContainerView();

    if (!mFitPanel)
    {
        LLScrollbar::Params sbparams;
        sbparams.name("scrollable vertical");
        sbparams.rect(scroll_rect);
        sbparams.orientation(LLScrollbar::VERTICAL);
        sbparams.doc_size(getRect().getHeight());
        sbparams.doc_pos(0);
        sbparams.page_size(getRect().getHeight());
        sbparams.step_size(VERTICAL_MULTIPLE);
        sbparams.follows.flags(FOLLOWS_RIGHT | FOLLOWS_TOP | FOLLOWS_BOTTOM);
        sbparams.change_callback(boost::bind(&LLAccordionCtrlTab::onScrollPosChangeCallback, this, _1, _2));

        mScrollbar = LLUICtrlFactory::create<LLScrollbar>(sbparams);
        LLView::addChild(mScrollbar);
        mScrollbar->setFollowsRight();
        mScrollbar->setFollowsTop();
        mScrollbar->setFollowsBottom();

        mScrollbar->setVisible(false);
    }

    if (mContainerPanel)
    {
        mContainerPanel->setVisible(mDisplayChildren);
    }

    return LLUICtrl::postBuild();
}

bool LLAccordionCtrlTab::notifyChildren (const LLSD& info)
{
    if (info.has("action"))
    {
        const std::string& str_action = info["action"].asStringRef();
        if (str_action == "store_state")
        {
            storeOpenCloseState();
            return true;
        }

        if (str_action == "restore_state")
        {
            restoreOpenCloseState();
            return true;
        }
    }

    return LLUICtrl::notifyChildren(info);
}

S32 LLAccordionCtrlTab::notifyParent(const LLSD& info)
{
    if (info.has("action"))
    {
        const std::string& str_action = info["action"].asStringRef();
        if (str_action == "size_changes")
        {
            S32 height = info["height"];
            height = llmax(height, 10) + HEADER_HEIGHT + getPaddingTop() + getPaddingBottom();

            mExpandedHeight = height;

            if (isExpanded() && !mSkipChangesOnNotifyParent)
            {
                LLRect panel_rect = getRect();
                panel_rect.setLeftTopAndSize( panel_rect.mLeft, panel_rect.mTop, panel_rect.getWidth(), height);
                reshape(getRect().getWidth(),height);
                setRect(panel_rect);
            }

            // LLAccordionCtrl should rearrange accordion tab if one of accordions changed its size
            if (getParent()) // A parent may not be set if tabs are added dynamically.
                getParent()->notifyParent(info);
            return 1;
        }

        if (str_action == "select_prev")
        {
            showAndFocusHeader();
            return 1;
        }
    }
    else if (info.has("scrollToShowRect"))
    {
        LLAccordionCtrl* parent = dynamic_cast<LLAccordionCtrl*>(getParent());
        if (parent && parent->getFitParent())
        {
            //  EXT-8285 ('No attachments worn' text appears at the bottom of blank 'Attachments' accordion)
            //  The problem was in passing message "scrollToShowRect" IN LLAccordionCtrlTab::notifyParent
            //  FROM child LLScrollContainer TO parent LLAccordionCtrl with "it_parent" set to true.

            //  It is wrong notification for parent accordion which leads to recursive call of adjustContainerPanel
            //  As the result of recursive call of adjustContainerPanel we got LLAccordionCtrlTab
            //  that reshaped and re-sized with different rectangles.

            //  LLAccordionCtrl has own scrollContainer and LLAccordionCtrlTab has own scrollContainer
            //  both should handle own scroll container's event.
            //  So, if parent accordion "fit_parent" accordion tab should handle its scroll container events itself.

            return 1;
        }

        if (!getDisplayChildren())
        {
            // Don't pass scrolling event further if our contents are invisible (STORM-298).
            return 1;
        }
    }

    return LLUICtrl::notifyParent(info);
}

S32 LLAccordionCtrlTab::notify(const LLSD& info)
{
    if (info.has("action"))
    {
        std::string str_action = info["action"];
        if (str_action == "select_first")
        {
            showAndFocusHeader();
            return 1;
        }

        if (str_action == "select_last")
        {
            if (!getDisplayChildren())
            {
                showAndFocusHeader();
            }
            else
            {
                LLView* view = getAccordionView();
                if (view)
                {
                    view->notify(LLSD().with("action", "select_last"));
                }
            }
        }
    }

    return 0;
}

bool LLAccordionCtrlTab::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    if (!mHeader->hasFocus())
        return LLUICtrl::handleKey(key, mask, called_from_parent);

    if ((key == KEY_RETURN) && mask == MASK_NONE)
    {
        changeOpenClose(getDisplayChildren());
        return true;
    }

    if ((key == KEY_ADD || key == KEY_RIGHT) && mask == MASK_NONE)
    {
        if (!getDisplayChildren())
        {
            changeOpenClose(getDisplayChildren());
            return true;
        }
    }

    if ((key == KEY_SUBTRACT || key == KEY_LEFT) && mask == MASK_NONE)
    {
        if (getDisplayChildren())
        {
            changeOpenClose(getDisplayChildren());
            return true;
        }
    }

    if (key == KEY_DOWN && mask == MASK_NONE)
    {
        // if collapsed go to the next accordion
        if (!getDisplayChildren())
        {
            // we're processing notifyParent so let call parent directly
            getParent()->notifyParent(LLSD().with("action", "select_next"));
        }
        else
        {
            getAccordionView()->notify(LLSD().with("action", "select_first"));
        }
        return true;
    }

    if (key == KEY_UP && mask == MASK_NONE)
    {
        // go to the previous accordion

        // we're processing notifyParent so let call parent directly
        getParent()->notifyParent(LLSD().with("action", "select_prev"));
        return true;
    }

    return LLUICtrl::handleKey(key, mask, called_from_parent);
}

void LLAccordionCtrlTab::showAndFocusHeader()
{
    if (!mHeader)
    {
        return;
    }

    mHeader->setFocus(true);
    mHeader->setSelected(mSelectionEnabled);

    LLRect screen_rc;
    LLRect selected_rc = mHeader->getRect();
    localRectToScreen(selected_rc, &screen_rc);

    // This call to notifyParent() is intended to deliver "scrollToShowRect" command
    // to the parent LLAccordionCtrl so by calling it from the direct parent of this
    // accordion tab (assuming that the parent is an LLAccordionCtrl) the calls chain
    // is shortened and messages from inside the collapsed tabs are avoided.
    // See STORM-536.
    getParent()->notifyParent(LLSD().with("scrollToShowRect", screen_rc.getValue()));
}

void LLAccordionCtrlTab::storeOpenCloseState()
{
    if (mWasStateStored)
        return;
    mStoredOpenCloseState = getDisplayChildren();
    mWasStateStored = true;
}

void LLAccordionCtrlTab::restoreOpenCloseState()
{
    if (!mWasStateStored)
        return;
    if (getDisplayChildren() != mStoredOpenCloseState)
    {
        changeOpenClose(getDisplayChildren());
    }
    mWasStateStored = false;
}

void LLAccordionCtrlTab::adjustContainerPanel()
{
    S32 width = getRect().getWidth();
    S32 height = getRect().getHeight();

    LLRect child_rect;
    child_rect.setLeftTopAndSize(
        getPaddingLeft(),
        height - getHeaderHeight() - getPaddingTop(),
        width - getPaddingLeft() - getPaddingRight(),
        height - getHeaderHeight() - getPaddingTop() - getPaddingBottom() );

    adjustContainerPanel(child_rect);
}

void LLAccordionCtrlTab::adjustContainerPanel(const LLRect& child_rect)
{
    if (!mContainerPanel)
        return;

    if (!mFitPanel)
    {
        show_hide_scrollbar(child_rect);
        updateLayout(child_rect);
    }
    else if (mContainerPanel->getRect() != child_rect)
    {
        mContainerPanel->reshape(child_rect.getWidth(), child_rect.getHeight());
        mContainerPanel->setRect(child_rect);
    }
}

S32 LLAccordionCtrlTab::getChildViewHeight()
{
    if (!mContainerPanel)
        return 0;
    return mContainerPanel->getRect().getHeight();
}

void LLAccordionCtrlTab::show_hide_scrollbar(const LLRect& child_rect)
{
    if (getChildViewHeight() > child_rect.getHeight())
        showScrollbar(child_rect);
    else
        hideScrollbar(child_rect);
}

void LLAccordionCtrlTab::showScrollbar(const LLRect& child_rect)
{
    if (!mContainerPanel || !mScrollbar)
        return;
    bool was_visible = mScrollbar->getVisible();
    mScrollbar->setVisible(true);

    static LLUICachedControl<S32> scrollbar_size ("UIScrollbarSize", 0);

    ctrlSetLeftTopAndSize(mScrollbar,
        child_rect.getWidth() - scrollbar_size,
        child_rect.getHeight() - PARENT_BORDER_MARGIN,
        scrollbar_size,
        child_rect.getHeight() - PARENT_BORDER_MARGIN * 2);

    LLRect orig_rect = mContainerPanel->getRect();

    mScrollbar->setPageSize(child_rect.getHeight());
    mScrollbar->setDocParams(orig_rect.getHeight(), mScrollbar->getDocPos());

    if (was_visible)
    {
        S32 scroll_pos = llmin(mScrollbar->getDocPos(), orig_rect.getHeight() - child_rect.getHeight() - 1);
        mScrollbar->setDocPos(scroll_pos);
    }
    else // Shrink child panel
    {
        updateLayout(child_rect);
    }
}

void LLAccordionCtrlTab::hideScrollbar(const LLRect& child_rect)
{
    if (!mContainerPanel || !mScrollbar)
        return;

    if (!mScrollbar->getVisible())
        return;

    mScrollbar->setVisible(false);
    mScrollbar->setDocPos(0);

    //shrink child panel
    updateLayout(child_rect);
}

void LLAccordionCtrlTab::onScrollPosChangeCallback(S32, LLScrollbar*)
{
    LLRect child_rect;

    S32 width = getRect().getWidth();
    S32 height = getRect().getHeight();

    child_rect.setLeftTopAndSize(
        getPaddingLeft(),
        height - getHeaderHeight() - getPaddingTop(),
        width - getPaddingLeft() - getPaddingRight(),
        height - getHeaderHeight() - getPaddingTop() - getPaddingBottom() );

    updateLayout(child_rect);
}

void LLAccordionCtrlTab::drawChild(const LLRect& root_rect, LLView* child)
{
    if (child && child->getVisible() && child->getRect().isValid())
    {
        LLRect screen_rect;
        localRectToScreen(child->getRect(), &screen_rect);

        if (root_rect.overlaps(screen_rect) && sDirtyRect.overlaps(screen_rect))
        {
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            LLUI::pushMatrix();
            {
                LLUI::translate((F32)child->getRect().mLeft, (F32)child->getRect().mBottom);
                child->draw();
            }
            LLUI::popMatrix();
        }
    }
}

void LLAccordionCtrlTab::draw()
{
    if (mFitPanel)
    {
        LLUICtrl::draw();
    }
    else
    {
        LLRect root_rect(getRootView()->getRect());
        drawChild(root_rect, mHeader);
        drawChild(root_rect, mScrollbar);

        LLRect child_rect;

        S32 width = getRect().getWidth();
        S32 height = getRect().getHeight();

        child_rect.setLeftTopAndSize(
            getPaddingLeft(),
            height - getHeaderHeight() - getPaddingTop(),
            width - getPaddingLeft() - getPaddingRight(),
            height - getHeaderHeight() - getPaddingTop() - getPaddingBottom());

        LLLocalClipRect clip(child_rect);
        drawChild(root_rect,mContainerPanel);
    }
}

void LLAccordionCtrlTab::updateLayout(const LLRect& child_rect)
{
    LLView* child = getAccordionView();
    if (!mContainerPanel)
        return;

    S32 panel_top = child_rect.getHeight();
    S32 panel_width = child_rect.getWidth();

    static LLUICachedControl<S32> scrollbar_size("UIScrollbarSize", 0);
    if (mScrollbar && mScrollbar->getVisible())
    {
        panel_top += mScrollbar->getDocPos();
        panel_width -= scrollbar_size;
    }

    // Set sizes for first panels and dragbars
    LLRect panel_rect = child->getRect();
    ctrlSetLeftTopAndSize(mContainerPanel, child_rect.mLeft, panel_top, panel_width, panel_rect.getHeight());
}

void LLAccordionCtrlTab::ctrlSetLeftTopAndSize(LLView* panel, S32 left, S32 top, S32 width, S32 height)
{
    if (!panel)
        return;
    LLRect panel_rect = panel->getRect();
    panel_rect.setLeftTopAndSize(left, top, width, height);
    panel->reshape( width, height, 1);
    panel->setRect(panel_rect);
}

bool LLAccordionCtrlTab::handleToolTip(S32 x, S32 y, MASK mask)
{
    // Same reason as handleMouseDown: the header branch answers for the whole
    // band, and it answers with the title's tooltip. A header checkbox is a
    // separate control saying a separate thing, so let it speak for its own
    // rect. Its coordinates have to be converted -- the header branch below
    // passes this tab's, which is why its own children never match.
    if (pointInHeaderCheckBox(x, y))
    {
        LLRect check_rect;
        mHeaderCheckBox->localRectToOtherView(mHeaderCheckBox->getLocalRect(), &check_rect, this);
        if (mHeaderCheckBox->handleToolTip(x - check_rect.mLeft, y - check_rect.mBottom, mask))
        {
            return true;
        }
    }

    //header may be not the first child but we need to process it first
    if (y >= (getRect().getHeight() - HEADER_HEIGHT - HEADER_HEIGHT / 2))
    {
        //inside tab header
        //fix for EXT-6619
        mHeader->handleToolTip(x, y, mask);
        return true;
    }
    return LLUICtrl::handleToolTip(x, y, mask);
}

bool LLAccordionCtrlTab::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (LLUICtrl::handleScrollWheel(x, y, delta))
    {
        return true;
    }

    if (mScrollbar && mScrollbar->getVisible() && mScrollbar->handleScrollWheel(0, 0, delta))
    {
        return true;
    }

    return false;
}

bool LLAccordionCtrlTab::handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (LLUICtrl::handleScrollHWheel(x, y, delta))
    {
        return true;
    }

    if (mScrollbar && mScrollbar->getVisible() && mScrollbar->handleScrollHWheel(0, 0, delta))
    {
        return true;
    }

    return false;
}

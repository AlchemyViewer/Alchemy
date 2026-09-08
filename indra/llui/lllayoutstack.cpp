/**
 * @file lllayoutstack.cpp
 * @brief LLLayout class - dynamic stacking of UI elements
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

// Opaque view with a background and a border.  Can contain LLUICtrls.

#include "linden_common.h"

#include "lllayoutstack.h"

#include "lllocalcliprect.h"
#include "llpanel.h"
#include "llcriticaldamp.h"
#include "llframetimer.h"
#include "lliconctrl.h"
#include "boost/foreach.hpp"

static constexpr F32 MIN_FRACTIONAL_SIZE = 0.00001f;
static constexpr F32 MAX_FRACTIONAL_SIZE = 1.f;

static LLDefaultChildRegistry::Register<LLLayoutStack> register_layout_stack("layout_stack");
static LLLayoutStack::LayoutStackRegistry::Register<LLLayoutPanel> register_layout_panel("layout_panel");

//
// LLLayoutPanel
//
LLLayoutPanel::Params::Params()
:   expanded_min_dim("expanded_min_dim", 0),
    min_dim("min_dim", -1),
    max_dim("max_dim", -1),
    user_resize("user_resize", false),
    auto_resize("auto_resize", true)
{
    addSynonym(min_dim, "min_width");
    addSynonym(min_dim, "min_height");
    addSynonym(max_dim, "max_width");
    addSynonym(max_dim, "max_height");
}

LLLayoutPanel::LLLayoutPanel(const Params& p)
:   LLPanel(p),
    mAutoResize(p.auto_resize),
    mUserResize(p.user_resize),
    mExpandedMinDim(p.expanded_min_dim.isProvided() ? p.expanded_min_dim : p.min_dim),
    mMinDim(p.min_dim),
    mMaxDim(p.max_dim),
    mCollapsed(false),
    mVisibleAmt(1.f), // default to fully visible
    mCollapseAmt(0.f),
    mFractionalSize(0.f),
    mTargetDim(0),
    mIgnoreReshape(false),
    mOrientation(LLLayoutStack::HORIZONTAL),
    mResizeBar(nullptr)
{
    // panels initialized as hidden should not start out partially visible
    if (!getVisible())
    {
        mVisibleAmt = 0.f;
    }
    setMaxDim(mMaxDim);
}

void LLLayoutPanel::initFromParams(const Params& p)
{
    LLPanel::initFromParams(p);
    setFollowsNone();
}


LLLayoutPanel::~LLLayoutPanel()
{
    gFocusMgr.removeKeyboardFocusWithoutCallback(this);

    // ~LLView takes a child out of its parent, but by the time it runs this
    // is no longer a layout panel, and the stack cannot tell it from any other
    // view: the entry in its panel list would outlive the panel. Leave now,
    // while still one. A parent already tearing down has cut the link.
    if (LLLayoutStack* stackp = getParentAs<LLLayoutStack>())
    {
        stackp->removeChild(this);
    }
}

F32 LLLayoutPanel::getAutoResizeFactor() const
{
    return mVisibleAmt * (1.f - mCollapseAmt);
}

F32 LLLayoutPanel::getVisibleAmount() const
{
    return mVisibleAmt;
}

S32 LLLayoutPanel::getLayoutDim() const
{
    return ll_round((F32)((mOrientation == LLLayoutStack::HORIZONTAL)
                    ? getRect().getWidth()
                    : getRect().getHeight()));
}

S32 LLLayoutPanel::getTargetDim() const
{
    return mTargetDim;
}

void LLLayoutPanel::setTargetDim(S32 value)
{
    // Asks the stack for this size, the way a drag on the resize bar does. An
    // auto-resize panel's target is recomputed from the stack's free space
    // every pass, so what survives the ask there is its share of that space,
    // not the number. Clamped to what the panel will hold either way, so the
    // shape handed over is one a layout can settle on.
    value = llclamp(value, getRelevantMinDim(), mMaxDim);

    LLRect new_rect(getRect());
    if (mOrientation == LLLayoutStack::HORIZONTAL)
    {
        new_rect.mRight = new_rect.mLeft + value;
    }
    else
    {
        new_rect.mTop = new_rect.mBottom + value;
    }
    setShape(new_rect, true);
}

S32 LLLayoutPanel::getVisibleDim() const
{
    F32 min_dim = (F32)getRelevantMinDim();
    return ll_round(mVisibleAmt
                    * (min_dim
                        + (((F32)mTargetDim - min_dim) * (1.f - mCollapseAmt))));
}

void LLLayoutPanel::setOrientation( LLView::EOrientation orientation )
{
    mOrientation = orientation;
    S32 layout_dim = ll_round((F32)((mOrientation == LLLayoutStack::HORIZONTAL)
        ? getRect().getWidth()
        : getRect().getHeight()));

    if (!mAutoResize && mUserResize && mMinDim == -1)
    {
        setMinDim(layout_dim);
    }
    mTargetDim = llmax(layout_dim, getMinDim());
    mTargetDim = llmin(mTargetDim, mMaxDim);
}

void LLLayoutPanel::setVisible( bool visible )
{
    if (visible != getVisible())
    {
        LLLayoutStack* stackp = getParentAs<LLLayoutStack>();
        if (stackp)
        {
            stackp->mNeedsLayout = true;
        }
    }
    LLPanel::setVisible(visible);
}

void LLLayoutPanel::reshape( S32 width, S32 height, bool called_from_parent /*= true*/ )
{
    if (width == getRect().getWidth() && height == getRect().getHeight() && !LLView::sForceReshape) return;

    if (!mIgnoreReshape && !mAutoResize)
    {
        mTargetDim = (mOrientation == LLLayoutStack::HORIZONTAL) ? width : height;
        mTargetDim = llmin(mTargetDim, mMaxDim);
        LLLayoutStack* stackp = getParentAs<LLLayoutStack>();
        if (stackp)
        {
            stackp->mNeedsLayout = true;
        }
    }
    LLPanel::reshape(width, height, called_from_parent);
}

void LLLayoutPanel::handleReshape(const LLRect& new_rect, bool by_user)
{
    LLLayoutStack* stackp = getParentAs<LLLayoutStack>();
    if (stackp)
    {
        if (by_user)
        {   // tell layout stack to account for new shape

            // make sure that panels have already been auto resized
            stackp->updateLayout();
            // now apply requested size to panel
            stackp->updatePanelRect(this, new_rect);
        }
        stackp->mNeedsLayout = true;
    }
    LLPanel::handleReshape(new_rect, by_user);
}

//
// LLLayoutStack
//

LLLayoutStack::Params::Params()
:   orientation("orientation"),
    animate("animate", true),
    clip("clip", true),
    open_time_constant("open_time_constant", 0.02f),
    close_time_constant("close_time_constant", 0.03f),
    resize_bar_overlap("resize_bar_overlap", 1),
    border_size("border_size", LLUI::getInstance()->mSettingGroups["config"]->getS32("UIResizeBarHeight")),
    show_drag_handle("show_drag_handle", false),
    drag_handle_first_indent("drag_handle_first_indent", 0),
    drag_handle_second_indent("drag_handle_second_indent", 0),
    drag_handle_thickness("drag_handle_thickness", 5),
    drag_handle_shift("drag_handle_shift", 2),
    drag_handle_color("drag_handle_color", LLUIColorTable::instance().getColor("ResizebarBody"))
{
    addSynonym(border_size, "drag_handle_gap");
}

LLLayoutStack::LLLayoutStack(const LLLayoutStack::Params& p)
:   LLView(p),
    mOrientation(p.orientation),
    mPanelSpacing(p.border_size),
    mAnimatedFrame(LLFrameTimer::getFrameCount() - 1),
    mAnimate(p.animate),
    mClip(p.clip),
    mOpenTimeConstant(p.open_time_constant),
    mCloseTimeConstant(p.close_time_constant),
    mNeedsLayout(true),
    mResizeBarOverlap(p.resize_bar_overlap),
    mShowDragHandle(p.show_drag_handle),
    mDragHandleFirstIndent(p.drag_handle_first_indent),
    mDragHandleSecondIndent(p.drag_handle_second_indent),
    mDragHandleThickness(p.drag_handle_thickness),
    mDragHandleShift(p.drag_handle_shift),
    mDragHandleColor(p.drag_handle_color())
{
}

LLLayoutStack::~LLLayoutStack()
{
}

// virtual
void LLLayoutStack::draw()
{
    updateLayout();

    // always clip to stack itself
    LLLocalClipRect clip(getLocalRect());
    for (LLLayoutPanel* panelp : mPanels)
    {
        if ((!panelp->getVisible() || panelp->mCollapsed)
            && (panelp->mVisibleAmt < 0.001f || !mAnimate))
        {
            // essentially invisible
            continue;
        }
        // clip to layout rectangle, not bounding rectangle
        LLRect clip_rect = panelp->getRect();
        // scale clipping rectangle by visible amount
        if (mOrientation == HORIZONTAL)
        {
            clip_rect.mRight = clip_rect.mLeft + panelp->getVisibleDim();
        }
        else
        {
            clip_rect.mBottom = clip_rect.mTop - panelp->getVisibleDim();
        }

        {LLLocalClipRect clip(clip_rect, mClip);
            // only force drawing invisible children if visible amount is non-zero
            drawChild(panelp, 0, 0, !clip_rect.isEmpty());
        }
        if (panelp->getResizeBar()->getVisible())
        {
            drawChild(panelp->getResizeBar());
        }
    }
}

// virtual
void LLLayoutStack::deleteAllChildren()
{
    for (LLLayoutPanel* p : mPanels)
    {
        p->mResizeBar = nullptr;
    }

    mPanels.clear();
    LLView::deleteAllChildren();

    // Not really needed since nothing is left to
    // display, but for the sake of consistency
    updateFractionalSizes();
    mNeedsLayout = true;
}

// virtual
void LLLayoutStack::removeChild(LLView* view)
{
    if (LLLayoutPanel* embedded_panelp = view->as<LLLayoutPanel>())
    {
        auto it = std::find(mPanels.begin(), mPanels.end(), embedded_panelp);
        if (it != mPanels.end())
        {
            mPanels.erase(it);

            // The bar belongs to the stack, not to the panel, and a panel that
            // has left is the last thing that would ever ask for it. Deleting
            // takes it out of the child list on the way; removing it alone
            // strands it, since nothing else holds it. Cleared first, so the
            // removeChild the destructor comes back with finds nothing to do.
            if (LLResizeBar* resize_barp = embedded_panelp->mResizeBar)
            {
                embedded_panelp->mResizeBar = nullptr;
                delete resize_barp;
            }
        }
    }
    else
    {
        // A resize bar, if it is one of ours, is known by address.
        for (LLLayoutPanel* p : mPanels)
        {
            if (p->mResizeBar == view)
            {
                p->mResizeBar = nullptr;
            }
        }
    }

    LLView::removeChild(view);

    updateFractionalSizes();
    mNeedsLayout = true;
}

// virtual
bool LLLayoutStack::postBuild()
{
    updateLayout();
    return true;
}

// virtual
bool LLLayoutStack::addChild(LLView* child, S32 tab_group)
{
    // The move first. LLView::addChild is what takes the panel off whatever
    // parent it had, and a stack losing a panel is what deletes that panel's
    // resize bar and clears the pointer -- so a bar built before this point is
    // the one the old stack goes on to delete, leaving the panel on a list the
    // layout pass reads with nothing there. That reaches a stack adding a panel
    // it already holds, too: the removal that arrives from inside would take
    // the new entry back off the list.
    bool result = LLView::addChild(child, tab_group);

    if (LLLayoutPanel* panelp = child ? child->as<LLLayoutPanel>() : nullptr)
    {
        panelp->setOrientation(mOrientation);
        mPanels.push_back(panelp);
        createResizeBar(panelp);
        mNeedsLayout = true;
    }
    else if (result && !child->as<LLResizeBar>())
    {
        // A stack draws its panels and their resize bars, and nothing else --
        // it never reaches LLView::draw. Anything else added here is in the
        // tree, takes part in hit testing and is never seen.
        LL_WARNS_ONCE() << "\"" << child->getName() << "\" is not a layout panel; "
                        << getName() << " will not draw it" << LL_ENDL;
    }

    updateFractionalSizes();
    return result;
}

void LLLayoutStack::addPanel(LLLayoutPanel* panel, EAnimate animate)
{
    addChild(panel);

    // panel starts off invisible (collapsed)
    if (animate == ANIMATE)
    {
        panel->mVisibleAmt = 0.f;
        panel->setVisible(true);
    }
}

void LLLayoutStack::collapsePanel(LLPanel* panel, bool collapsed)
{
    LLLayoutPanel* panel_container = findEmbeddedPanel(panel);
    if (!panel_container) return;

    panel_container->mCollapsed = collapsed;
    mNeedsLayout = true;
}

void LLLayoutStack::updateLayout()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if (!mNeedsLayout) return;

    // How much of the view tree this pass moved. setShape on a panel reshapes
    // everything under it, and the question a capture cannot otherwise answer
    // is whether that is many views once or few views many times.
    const S32 reshapes_before = LLView::sReshapeCount;

    bool continue_animating = animatePanels();
    F32 total_visible_fraction = 0.f;
    S32 space_to_distribute = (mOrientation == HORIZONTAL)
                            ? getRect().getWidth()
                            : getRect().getHeight();

    // The gap the loop below takes out after the last panel that is showing.
    // Nothing follows that panel, so nothing needs the room -- and the panel it
    // belongs to is not the last on the list once a trailing one is hidden.
    S32 trailing_spacing = 0;

    // first, assign minimum dimensions
    for (LLLayoutPanel* panelp : mPanels)
    {
        if (panelp->mAutoResize)
        {
            panelp->mTargetDim = panelp->getRelevantMinDim();
        }
        S32 panel_spacing = ll_round((F32)mPanelSpacing * panelp->getVisibleAmount());
        space_to_distribute -= panelp->getVisibleDim() + panel_spacing;
        if (panel_spacing > 0)
        {
            trailing_spacing = panel_spacing;
        }
        total_visible_fraction += panelp->mFractionalSize * panelp->getAutoResizeFactor();
    }

    // The total is a divisor and nothing else: each panel's share is its own
    // fraction over this one, so the shares sum to the space however far the
    // total has drifted from one. A user drag rewrites fractions without
    // renormalizing, so it does drift.

    // don't need spacing after last panel
    space_to_distribute += trailing_spacing;

    S32 remaining_space = space_to_distribute;
    if (space_to_distribute > 0 && total_visible_fraction > 0.f)
    {   // give space proportionally to visible auto resize panels
        for (LLLayoutPanel* panelp : mPanels)
        {
            if (panelp->mAutoResize)
            {
                F32 fraction_to_distribute = (panelp->mFractionalSize * panelp->getAutoResizeFactor()) / (total_visible_fraction);
                S32 delta = ll_round((F32)space_to_distribute * fraction_to_distribute);
                panelp->mTargetDim += delta;
                panelp->mTargetDim = llmin(panelp->mTargetDim, panelp->mMaxDim);
                remaining_space -= delta;
            }
        }
    }

    // distribute any left over pixels to non-collapsed, visible panels
    for (LLLayoutPanel* panelp : mPanels)
    {
        if (remaining_space == 0) break;

        if (panelp->mAutoResize
            && !panelp->mCollapsed
            && panelp->getVisible())
        {
            S32 space_for_panel = remaining_space > 0 ? 1 : -1;
            panelp->mTargetDim += space_for_panel;
            panelp->mTargetDim = llmin(panelp->mTargetDim, panelp->mMaxDim);
            remaining_space -= space_for_panel;
        }
    }

    F32 cur_pos = (mOrientation == HORIZONTAL) ? 0.f : (F32)getRect().getHeight();

    for (LLLayoutPanel* panelp : mPanels)
    {
        F32 panel_dim = (F32)llmax(panelp->getExpandedMinDim(), panelp->mTargetDim);

        LLRect panel_rect;
        if (mOrientation == HORIZONTAL)
        {
            panel_rect.setLeftTopAndSize(ll_round(cur_pos),
                                        getRect().getHeight(),
                                        ll_round(panel_dim),
                                        getRect().getHeight());
        }
        else
        {
            panel_rect.setLeftTopAndSize(0,
                                        ll_round(cur_pos),
                                        getRect().getWidth(),
                                        ll_round(panel_dim));
        }

        LLRect resize_bar_rect(panel_rect);
        F32 panel_spacing = (F32)mPanelSpacing * panelp->getVisibleAmount();
        F32 panel_visible_dim = (F32)panelp->getVisibleDim();
        S32 panel_spacing_round = (S32)(ll_round(panel_spacing));

        if (mOrientation == HORIZONTAL)
        {
            cur_pos += panel_visible_dim + panel_spacing;

            if (mShowDragHandle && panel_spacing_round > mDragHandleThickness)
            {
                resize_bar_rect.mLeft = panel_rect.mRight + mDragHandleShift;
                resize_bar_rect.mRight = resize_bar_rect.mLeft + mDragHandleThickness;
            }
            else
            {
                resize_bar_rect.mLeft = panel_rect.mRight - mResizeBarOverlap;
                resize_bar_rect.mRight = panel_rect.mRight + panel_spacing_round + mResizeBarOverlap;
            }

            if (mShowDragHandle)
            {
                resize_bar_rect.mBottom += mDragHandleSecondIndent;
                resize_bar_rect.mTop -= mDragHandleFirstIndent;
            }

        }
        else //VERTICAL
        {
            cur_pos -= panel_visible_dim + panel_spacing;

            if (mShowDragHandle && panel_spacing_round > mDragHandleThickness)
            {
                resize_bar_rect.mTop = panel_rect.mBottom - mDragHandleShift;
                resize_bar_rect.mBottom = resize_bar_rect.mTop - mDragHandleThickness;
            }
            else
            {
                resize_bar_rect.mTop = panel_rect.mBottom + mResizeBarOverlap;
                resize_bar_rect.mBottom = panel_rect.mBottom - panel_spacing_round - mResizeBarOverlap;
            }

            if (mShowDragHandle)
            {
                resize_bar_rect.mLeft += mDragHandleFirstIndent;
                resize_bar_rect.mRight -= mDragHandleSecondIndent;
            }
        }

        // Put back what the panel was holding rather than the answer this pass
        // wants: a caller that told a panel to ignore reshapes said so about
        // its own work, and would find the flag off again on the far side of
        // any layout.
        bool ignore_reshape = panelp->mIgnoreReshape;
        panelp->setIgnoreReshape(true);
        panelp->setShape(panel_rect);
        panelp->setIgnoreReshape(ignore_reshape);
        panelp->mResizeBar->setShape(resize_bar_rect);
    }

    updateResizeBarLimits();

    // clear animation flag at end, since panel resizes will set it
    // and leave it set if there is any animation in progress
    mNeedsLayout = continue_animating;

    LL_PROFILE_ZONE_NUM(LLView::sReshapeCount - reshapes_before);
} // end LLLayoutStack::updateLayout

void LLLayoutStack::setPanelSpacing(S32 val)
{
    if (mPanelSpacing != val)
    {
        mPanelSpacing = val;
        mNeedsLayout = true;
    }
}

LLLayoutPanel* LLLayoutStack::findEmbeddedPanel(LLPanel* panelp) const
{
    if (!panelp) return NULL;

    for (LLLayoutPanel* p : mPanels)
    {
        if (p == panelp)
        {
            return p;
        }
    }
    return NULL;
}

LLLayoutPanel* LLLayoutStack::findEmbeddedPanelByName(std::string_view name) const
{
    LLLayoutPanel* result = NULL;

    for (LLLayoutPanel* p : mPanels)
    {
        if (p->getName() == name)
        {
            result = p;
            break;
        }
    }

    return result;
}

void LLLayoutStack::createResizeBar(LLLayoutPanel* panelp)
{
    if (!panelp->mResizeBar)
    {
        LLResizeBar::Params resize_params;
        resize_params.name("resize");
        resize_params.resizing_view(panelp);
        resize_params.min_size(panelp->getRelevantMinDim());
        resize_params.side((mOrientation == HORIZONTAL) ? LLResizeBar::RIGHT : LLResizeBar::BOTTOM);
        resize_params.snapping_enabled(false);
        LLResizeBar* resize_bar = LLUICtrlFactory::create<LLResizeBar>(resize_params);
        panelp->mResizeBar = resize_bar;

        if (mShowDragHandle)
        {
            LLPanel::Params resize_bar_bg_panel_p;
            resize_bar_bg_panel_p.name = "resize_handle_bg_panel";
            resize_bar_bg_panel_p.rect = resize_bar->getLocalRect();
            resize_bar_bg_panel_p.follows.flags = FOLLOWS_ALL;
            resize_bar_bg_panel_p.tab_stop = false;
            resize_bar_bg_panel_p.background_visible = true;
            resize_bar_bg_panel_p.bg_alpha_color = mDragHandleColor;
            resize_bar_bg_panel_p.has_border = true;
            resize_bar_bg_panel_p.border.border_thickness = 1;
            resize_bar_bg_panel_p.border.highlight_light_color = LLUIColorTable::instance().getColor("ResizebarBorderLight");
            resize_bar_bg_panel_p.border.shadow_dark_color = LLUIColorTable::instance().getColor("ResizebarBorderDark");

            LLPanel* resize_bar_bg_panel = LLUICtrlFactory::create<LLPanel>(resize_bar_bg_panel_p);

            LLIconCtrl::Params icon_p;
            icon_p.name = "resize_handle_image";
            icon_p.rect = resize_bar->getLocalRect();
            icon_p.follows.flags = FOLLOWS_ALL;
            icon_p.image = LLUI::getUIImage(mOrientation == HORIZONTAL ? "Vertical Drag Handle" : "Horizontal Drag Handle");
            resize_bar_bg_panel->addChild(LLUICtrlFactory::create<LLIconCtrl>(icon_p));

            resize_bar->addChild(resize_bar_bg_panel);
        }

        LLView::addChild(resize_bar, 0);
    }
    // bring all resize bars to the front so that they are clickable even over the panels
    // with a bit of overlap
    for (LLLayoutPanel* lp : mPanels)
    {
        sendChildToFront(lp->mResizeBar);
    }
}

// update layout stack animations, etc. once per frame
// NOTE: we use this to size world view based on animating UI, *before* we draw the UI
// we might still need to call updateLayout during UI draw phase, in case UI elements
// are resizing themselves dynamically
//static
void LLLayoutStack::updateClass()
{
    for (auto& layout : instance_snapshot())
    {
        layout.updateLayout();
    }
}

void LLLayoutStack::updateFractionalSizes()
{
    F32 total_resizable_dim = 0.f;

    for (LLLayoutPanel* panelp : mPanels)
    {
        if (panelp->mAutoResize)
        {
            total_resizable_dim += llmax(MIN_FRACTIONAL_SIZE, (F32)(panelp->getLayoutDim() - panelp->getRelevantMinDim()));
        }
    }

    for (LLLayoutPanel* panelp : mPanels)
    {
        if (panelp->mAutoResize)
        {
            F32 panel_resizable_dim = llmax(MIN_FRACTIONAL_SIZE, (F32)(panelp->getLayoutDim() - panelp->getRelevantMinDim()));
            panelp->mFractionalSize = panel_resizable_dim > 0.f
                ? llclamp(panel_resizable_dim / total_resizable_dim, MIN_FRACTIONAL_SIZE, MAX_FRACTIONAL_SIZE)
                : MIN_FRACTIONAL_SIZE;
            llassert(!llisnan(panelp->mFractionalSize));
        }
    }

    normalizeFractionalSizes();
}


void LLLayoutStack::normalizeFractionalSizes()
{
    S32 num_auto_resize_panels = 0;
    F32 total_fractional_size = 0.f;

    for (LLLayoutPanel* panelp : mPanels)
    {
        if (panelp->mAutoResize)
        {
            total_fractional_size += panelp->mFractionalSize;
            num_auto_resize_panels++;
        }
    }

    if (total_fractional_size == 0.f)
    { // equal distribution
        for (LLLayoutPanel* panelp : mPanels)
        {
            if (panelp->mAutoResize)
            {
                panelp->mFractionalSize = MAX_FRACTIONAL_SIZE / (F32)num_auto_resize_panels;
            }
        }
    }
    else
    { // renormalize
        for (LLLayoutPanel* panelp : mPanels)
        {
            if (panelp->mAutoResize)
            {
                panelp->mFractionalSize /= total_fractional_size;
            }
        }
    }
}

bool LLLayoutStack::animatePanels()
{
    bool continue_animating = false;

    // One frame's worth of motion, once a frame, for every panel that wants
    // it. The interpolant is cached per frame, so the question a later pass
    // over the same frame has to answer is whether it has already been paid --
    // asked of the stack, not of the panel, since the panels of one stack move
    // together.
    U32 frame = LLFrameTimer::getFrameCount();
    bool advance = mAnimatedFrame != frame;
    mAnimatedFrame = frame;

    bool moved = false;

    //
    // animate visibility
    //
    for (LLLayoutPanel* panelp : mPanels)
    {
        if (panelp->getVisible())
        {
            if (mAnimate && panelp->mVisibleAmt < 1.f)
            {
                if (advance)
                {
                    panelp->mVisibleAmt = lerp(panelp->mVisibleAmt, 1.f, LLSmoothInterpolation::getInterpolant(mOpenTimeConstant));
                    if (panelp->mVisibleAmt > 0.99f)
                    {
                        panelp->mVisibleAmt = 1.f;
                    }
                    moved = true;
                }

                continue_animating = true;
            }
            else
            {
                if (panelp->mVisibleAmt != 1.f)
                {
                    panelp->mVisibleAmt = 1.f;
                    moved = true;
                }
            }
        }
        else // not visible
        {
            if (mAnimate && panelp->mVisibleAmt > 0.f)
            {
                if (advance)
                {
                    panelp->mVisibleAmt = lerp(panelp->mVisibleAmt, 0.f, LLSmoothInterpolation::getInterpolant(mCloseTimeConstant));
                    if (panelp->mVisibleAmt < 0.001f)
                    {
                        panelp->mVisibleAmt = 0.f;
                    }
                    moved = true;
                }

                continue_animating = true;
            }
            else
            {
                if (panelp->mVisibleAmt != 0.f)
                {
                    panelp->mVisibleAmt = 0.f;
                    moved = true;
                }
            }
        }

        F32 collapse_state = panelp->mCollapsed ? 1.f : 0.f;
        if (panelp->mCollapseAmt != collapse_state)
        {
            if (mAnimate)
            {
                if (advance)
                {
                    panelp->mCollapseAmt = lerp(panelp->mCollapseAmt, collapse_state, LLSmoothInterpolation::getInterpolant(mCloseTimeConstant));
                    moved = true;
                }

                if (llabs(panelp->mCollapseAmt - collapse_state) < 0.001f)
                {
                    panelp->mCollapseAmt = collapse_state;
                }

                continue_animating = true;
            }
            else
            {
                panelp->mCollapseAmt = collapse_state;
                moved = true;
            }
        }
    }

    if (moved) mNeedsLayout = true;
    return continue_animating;
}

void LLLayoutStack::updatePanelRect( LLLayoutPanel* resized_panel, const LLRect& new_rect )
{
    S32 new_dim = (mOrientation == HORIZONTAL)
                    ? new_rect.getWidth()
                    : new_rect.getHeight();
    S32 delta_panel_dim = new_dim - resized_panel->getVisibleDim();
    if (delta_panel_dim == 0) return;

    F32 total_visible_fraction = 0.f;
    F32 delta_auto_resize_headroom = 0.f;
    F32 old_auto_resize_headroom = 0.f;

    LLLayoutPanel* other_resize_panel = NULL;
    LLLayoutPanel* following_panel = NULL;

    BOOST_REVERSE_FOREACH(LLLayoutPanel* panelp, mPanels) // Should replace this when C++20 reverse view adaptor becomes available...
    {
        if (panelp->mAutoResize)
        {
            old_auto_resize_headroom += (F32)(panelp->mTargetDim - panelp->getRelevantMinDim());
            if (panelp->getVisible() && !panelp->mCollapsed)
            {
                total_visible_fraction += panelp->mFractionalSize;
            }
        }

        if (panelp == resized_panel)
        {
            other_resize_panel = following_panel;
        }

        if (panelp->getVisible() && !panelp->mCollapsed)
        {
            following_panel = panelp;
        }
    }

    if (resized_panel->mAutoResize)
    {
        if (!other_resize_panel || !other_resize_panel->mAutoResize)
        {
            delta_auto_resize_headroom += delta_panel_dim;
        }
    }
    else
    {
        if (!other_resize_panel || other_resize_panel->mAutoResize)
        {
            delta_auto_resize_headroom -= delta_panel_dim;
        }
    }

    F32 fraction_given_up = 0.f;
    F32 fraction_remaining = 1.f;
    F32 new_auto_resize_headroom = old_auto_resize_headroom + delta_auto_resize_headroom;

    enum
    {
        BEFORE_RESIZED_PANEL,
        RESIZED_PANEL,
        NEXT_PANEL,
        AFTER_RESIZED_PANEL
    } which_panel = BEFORE_RESIZED_PANEL;

    for (LLLayoutPanel* panelp : mPanels)
    {
        if (!panelp->getVisible() || panelp->mCollapsed)
        {
            if (panelp->mAutoResize)
            {
                fraction_remaining -= panelp->mFractionalSize;
            }
            continue;
        }

        if (panelp == resized_panel)
        {
            which_panel = RESIZED_PANEL;
        }

        switch(which_panel)
        {
        case BEFORE_RESIZED_PANEL:
            if (panelp->mAutoResize)
            {   // freeze current size as fraction of overall auto_resize space
                F32 fractional_adjustment_factor = new_auto_resize_headroom == 0.f
                                                    ? 1.f
                                                    : old_auto_resize_headroom / new_auto_resize_headroom;
                F32 new_fractional_size = llclamp(panelp->mFractionalSize * fractional_adjustment_factor,
                                                    MIN_FRACTIONAL_SIZE,
                                                    MAX_FRACTIONAL_SIZE);
                fraction_given_up -= new_fractional_size - panelp->mFractionalSize;
                fraction_remaining -= panelp->mFractionalSize;
                panelp->mFractionalSize = new_fractional_size;
                llassert(!llisnan(panelp->mFractionalSize));
            }
            else
            {
                // leave non auto-resize panels alone
            }
            break;
        case RESIZED_PANEL:
            if (panelp->mAutoResize)
            {   // freeze new size as fraction
                F32 new_fractional_size = (new_auto_resize_headroom == 0.f)
                    ? MAX_FRACTIONAL_SIZE
                    : llclamp(total_visible_fraction * (F32)(new_dim - panelp->getRelevantMinDim()) / new_auto_resize_headroom, MIN_FRACTIONAL_SIZE, MAX_FRACTIONAL_SIZE);
                fraction_given_up -= new_fractional_size - panelp->mFractionalSize;
                fraction_remaining -= panelp->mFractionalSize;
                panelp->mFractionalSize = new_fractional_size;
                llassert(!llisnan(panelp->mFractionalSize));
            }
            else
            {   // freeze new size as original size
                panelp->mTargetDim = new_dim;
            }
            which_panel = NEXT_PANEL;
            break;
        case NEXT_PANEL:
            if (panelp->mAutoResize)
            {
                fraction_remaining -= panelp->mFractionalSize;
                if (resized_panel->mAutoResize)
                {
                    panelp->mFractionalSize = llclamp(panelp->mFractionalSize + fraction_given_up, MIN_FRACTIONAL_SIZE, MAX_FRACTIONAL_SIZE);
                    fraction_given_up = 0.f;
                }
                else
                {
                    if (new_auto_resize_headroom < 1.f)
                    {
                        new_auto_resize_headroom = 1.f;
                    }

                    F32 new_fractional_size = llclamp(total_visible_fraction * (F32)(panelp->mTargetDim - panelp->getRelevantMinDim() + delta_auto_resize_headroom)
                                                        / new_auto_resize_headroom,
                                                    MIN_FRACTIONAL_SIZE,
                                                    MAX_FRACTIONAL_SIZE);
                    fraction_given_up -= new_fractional_size - panelp->mFractionalSize;
                    panelp->mFractionalSize = new_fractional_size;
                }
            }
            else
            {
                panelp->mTargetDim -= delta_panel_dim;
            }
            which_panel = AFTER_RESIZED_PANEL;
            break;
        case AFTER_RESIZED_PANEL:
            if (panelp->mAutoResize && fraction_given_up != 0.f)
            {
                panelp->mFractionalSize = llclamp(panelp->mFractionalSize + (panelp->mFractionalSize / fraction_remaining) * fraction_given_up,
                                                MIN_FRACTIONAL_SIZE,
                                                MAX_FRACTIONAL_SIZE);
            }
            break;
        default:
            break;
        }
    }
    updateLayout();
    //normalizeFractionalSizes();
}

// virtual
void LLLayoutStack::reshape(S32 width, S32 height, bool called_from_parent)
{
    mNeedsLayout = true;
    LLView::reshape(width, height, called_from_parent);
}

void LLLayoutStack::updateResizeBarLimits()
{
    LLLayoutPanel* previous_visible_panelp{ nullptr };
    BOOST_REVERSE_FOREACH(LLLayoutPanel* visible_panelp, mPanels) // Should replace this when C++20 reverse view adaptor becomes available...
    {
        if (!visible_panelp->getVisible() || visible_panelp->mCollapsed)
        {
            visible_panelp->mResizeBar->setVisible(false);
            continue;
        }

        // toggle resize bars based on panel visibility, resizability, etc
        if (previous_visible_panelp
            && (visible_panelp->mUserResize || previous_visible_panelp->mUserResize)                // one of the pair is user resizable
            && (visible_panelp->mAutoResize || visible_panelp->mUserResize)                         // current panel is resizable
            && (previous_visible_panelp->mAutoResize || previous_visible_panelp->mUserResize))      // previous panel is resizable
        {
            visible_panelp->mResizeBar->setVisible(true);
            S32 previous_panel_headroom = previous_visible_panelp->getVisibleDim() - previous_visible_panelp->getRelevantMinDim();
            visible_panelp->mResizeBar->setResizeLimits(visible_panelp->getRelevantMinDim(),
                                                        visible_panelp->getVisibleDim() + previous_panel_headroom);
        }
        else
        {
            visible_panelp->mResizeBar->setVisible(false);
        }

        previous_visible_panelp = visible_panelp;
    }
}


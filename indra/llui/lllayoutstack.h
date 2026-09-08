/**
 * @file lllayoutstack.h
 * @author Richard Nelson
 * @brief LLLayout class - dynamic stacking of UI elements
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Reshasearch, Inc.
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

#ifndef LL_LLLAYOUTSTACK_H
#define LL_LLLAYOUTSTACK_H

#include "llpanel.h"
#include "llresizebar.h"


class LLLayoutPanel;


class LLLayoutStack final : public LLView, public LLInstanceTracker<LLLayoutStack>
{
public:
    AL_VIEW_TYPE(LLLayoutStack, LLView);

    struct LayoutStackRegistry : public LLChildRegistry<LayoutStackRegistry>
    {
        LLSINGLETON_EMPTY_CTOR(LayoutStackRegistry);
    };

    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<EOrientation> orientation;
        Optional<S32>           border_size;
        Optional<bool>          animate,
                                clip;
        Optional<F32>           open_time_constant,
                                close_time_constant;
        Optional<S32>           resize_bar_overlap;
        Optional<bool>          show_drag_handle;
        Optional<S32>           drag_handle_first_indent;
        Optional<S32>           drag_handle_second_indent;
        // A handle of this thickness, sitting this far past the panel it
        // follows, needs a gap wider than itself to sit in: border_size (which
        // drag_handle_gap also names) is that gap. In a gap it does not fit,
        // the resize bar takes the whole of it instead and these two do
        // nothing -- the handle is still drawn, filling the bar.
        Optional<S32>           drag_handle_thickness;
        Optional<S32>           drag_handle_shift;

        Optional<LLUIColor>     drag_handle_color;

        Params();
    };

    typedef LayoutStackRegistry child_registry_t;


    ~LLLayoutStack() override;

    void draw() override;
    void deleteAllChildren() override;
    void removeChild(LLView*) override;
    bool postBuild() override;
    bool addChild(LLView* child, S32 tab_group = 0) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

    typedef enum e_animate
    {
        NO_ANIMATE,
        ANIMATE
    } EAnimate;

    void addPanel(LLLayoutPanel* panel, EAnimate animate = NO_ANIMATE);
    void collapsePanel(LLPanel* panel, bool collapsed = true);
    S32 getNumPanels() const { return static_cast<S32>(mPanels.size()); }

    void updateLayout();

    S32 getPanelSpacing() const { return mPanelSpacing; }
    void setPanelSpacing(S32 val);

    // The axis the panels run along, which is the one dimension of a
    // panel that the stack reads from the file.
    EOrientation getOrientation() const { return mOrientation; }

    static void updateClass();

protected:
    LLLayoutStack(const Params&);
    friend class LLUICtrlFactory;
    friend class LLLayoutPanel;

private:
    void updateResizeBarLimits();
    bool animatePanels();
    void createResizeBar(LLLayoutPanel* panel);

    const EOrientation mOrientation;

    typedef std::vector<LLLayoutPanel*> e_panel_list_t;
    e_panel_list_t mPanels;

    LLLayoutPanel* findEmbeddedPanel(LLPanel* panelp) const;
    LLLayoutPanel* findEmbeddedPanelByName(std::string_view name) const;
    void updateFractionalSizes();
    void normalizeFractionalSizes();
    void updatePanelRect( LLLayoutPanel* param1, const LLRect& new_rect );

    S32 mPanelSpacing;

    // The frame whose worth of animation the panels have already had. Layout
    // runs several times in a frame -- from updateClass, from draw, and from
    // any panel that reshapes -- while the interpolant is computed once per
    // frame, so a second pass would move every panel twice as far.
    U32  mAnimatedFrame;
    bool mAnimate;
    bool mClip;
    F32  mOpenTimeConstant;
    F32  mCloseTimeConstant;
    bool mNeedsLayout;
    S32  mResizeBarOverlap;
    bool mShowDragHandle;
    S32  mDragHandleFirstIndent;
    S32  mDragHandleSecondIndent;
    S32  mDragHandleThickness;
    S32  mDragHandleShift;
    LLUIColor mDragHandleColor;
}; // end class LLLayoutStack


class LLLayoutPanel : public LLPanel
{
friend class LLLayoutStack;
friend class LLUICtrlFactory;
public:
    AL_VIEW_TYPE(LLLayoutPanel, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Optional<S32>           expanded_min_dim,
                                min_dim,
                                max_dim;
        Optional<bool>          user_resize,
                                auto_resize;

        Params();
    };

    ~LLLayoutPanel() override;

    // Hides LLPanel's, rather than overriding it: the parameters a layout
    // panel is built from are its own block, not a panel's.
    void initFromParams(const Params& p);

    void handleReshape(const LLRect& new_rect, bool by_user) override;

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

    void setVisible(bool visible) override;

    S32 getLayoutDim() const;
    S32 getTargetDim() const;
    void setTargetDim(S32 value);
    S32 getMinDim() const { return llmax(0, mMinDim); }
    void setMinDim(S32 value) { mMinDim = value; }

    void setMaxDim(S32 value) { mMaxDim = value < 0 ? S32_MAX : value; }

    S32 getExpandedMinDim() const { return mExpandedMinDim >= 0 ? mExpandedMinDim : getMinDim(); }
    void setExpandedMinDim(S32 value) { mExpandedMinDim = value; }

    // Never negative: -1 is how min_dim says it was never given, and a
    // negative dimension travels through the stack's space arithmetic and out
    // into a clip rect.
    S32 getRelevantMinDim() const
    {
        return mCollapsed ? getMinDim() : getExpandedMinDim();
    }

    F32 getAutoResizeFactor() const;
    F32 getVisibleAmount() const;
    S32 getVisibleDim() const;
    LLResizeBar* getResizeBar() { return mResizeBar; }

    bool isCollapsed() const { return mCollapsed;}

    bool getAutoResize() const { return mAutoResize; }
    bool getUserResize() const { return mUserResize; }

    void setOrientation(LLView::EOrientation orientation);

    void setIgnoreReshape(bool ignore) { mIgnoreReshape = ignore; }

protected:
    LLLayoutPanel(const Params& p);

    const bool  mAutoResize;
    const bool  mUserResize;

    S32     mExpandedMinDim;
    S32     mMinDim;
    S32     mMaxDim;
    bool    mCollapsed;
    F32     mVisibleAmt;
    F32     mCollapseAmt;
    F32     mFractionalSize;
    S32     mTargetDim;
    bool    mIgnoreReshape;
    LLView::EOrientation mOrientation;
    class LLResizeBar* mResizeBar;
};


#endif

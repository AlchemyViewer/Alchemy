/**
 * @file llpaneltopinfobar.h
 * @brief Coordinates and Parcel Settings information panel definition
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#ifndef LLPANELTOPINFOBAR_H_
#define LLPANELTOPINFOBAR_H_

#include "llpanel.h"
#include "llinitdestroyclass.h"
#include "alparceliconstrip.h"

class LLButton;
class LLTextBox;
class LLIconCtrl;
class LLParcelChangeObserver;
class LLViewerRegion;

class LLPanelTopInfoBar : public LLPanel, public LLSingleton<LLPanelTopInfoBar>, private LLDestroyClass<LLPanelTopInfoBar>
{
    LLSINGLETON(LLPanelTopInfoBar);
    ~LLPanelTopInfoBar();
    LOG_CLASS(LLPanelTopInfoBar);

    friend class LLDestroyClass<LLPanelTopInfoBar>;

public:
    typedef boost::signals2::signal<void ()> resize_signal_t;

    bool postBuild() override;
    void draw() override;

    /**
     * Updates location and parcel icons on login complete
     */
    void handleLoginComplete();

    /**
     * Called when the top info bar gets shown or hidden
     */
    void onVisibilityChanged(const LLSD& show);

    boost::signals2::connection setResizeCallback( const resize_signal_t::slot_type& cb );

// [RLVa:KB] - Checked: 2014-03-23 (RLVa-1.4.10)
    /**
     * Shorthand to call refreshParcelInfoText() and updateParcelIcons().
     */
    void update();
// [/RLV:KB]
private:
    class LLParcelChangeObserver;

    friend class LLParcelChangeObserver;


    /**
     * Hands the strip its icon controls. Called from the constructor.
     */
    void initParcelIcons();

    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;

    /**
     * Handles clicks on the parcel icons.
     */


    /**
     * Handles clicks on the parcel info text.
     */
    void onParcelInfoTextClicked();

    /**
     * Handles clicks on the info buttons.
     */
    void onInfoButtonClicked();

    /**
     * Called when agent changes the parcel.
     */
    void onAgentParcelChange();

    /**
     * Called when context menu item is clicked.
     */
    void onContextMenuItemClicked(const LLSD::String& userdata);

    /**
     * Called when user checks/unchecks Show Coordinates menu item.
     */
    void onNavBarShowParcelPropertiesCtrlChanged();

//  /**
//   * Shorthand to call updateParcelInfoText() and updateParcelIcons().
//   */
//  void update();

    /**
     * Updates parcel info text (mParcelInfoText).
     */
    void updateParcelInfoText();

    /**
     * Reads the parcel and writes the icons' visibility, then lays them out.
     */
    void updateParcelIcons();

    /**
     * The region's name and its maturity rating are both in the readout, and
     * both arrive on a handshake that moves nothing else this panel watches.
     */
    void onRegionInfoChanged(LLViewerRegion* regionp);

    /**
     * Lays out all parcel icons starting from right edge of the mParcelInfoText + 11px
     * (see screenshots in EXT-5808 for details).
     */
    void layoutParcelIcons();

    /**
     * Builds the readout and pushes it at the widget, but only if it would
     * differ from what is already on screen. Every path that writes the text
     * goes through here, and every one of them builds the same string: a
     * caller that left the coordinates out used to be corrected by the next
     * frame's unconditional rebuild, and there is no longer one to correct it.
     */
    void refreshParcelInfoText();

    /**
     * Sets new value to the mParcelInfoText and updates the size of the top bar.
     */
    void setParcelInfoText(const std::string& new_text);

    /**
     *  Implementation of LLDestroyClass<T>
     */
    static void destroyClass()
    {
        if (LLPanelTopInfoBar::instanceExists())
        {
            LLPanelTopInfoBar::getInstance()->setEnabled(false);
        }
    }

    LLButton*               mInfoBtn;
    LLTextBox*              mParcelInfoText;
    ALParcelIconStrip       mParcelIcons;
    LLParcelChangeObserver* mParcelChangedObserver;

    boost::signals2::connection mParcelPropsCtrlConnection;
    boost::signals2::connection mShowCoordsCtrlConnection;
    boost::signals2::connection mParcelMgrConnection;
    boost::signals2::connection mRegionInfoConnection;
    boost::signals2::connection mHealthConnection;

    // The rounded position baked into the text on screen. draw() compares the
    // agent's current one against this to decide whether the readout can have
    // moved; S32_MIN cannot be a real coordinate, so the first draw always
    // builds.
    S32 mDisplayPosX = S32_MIN;
    S32 mDisplayPosY = S32_MIN;
    S32 mDisplayPosZ = S32_MIN;

    // Reused rather than declared per call, so a readout that rebuilds twice a
    // second stops asking the allocator for the same string every time.
    std::string mLocationScratch;

    resize_signal_t mResizeSignal;
};

#endif /* LLPANELTOPINFOBAR_H_ */

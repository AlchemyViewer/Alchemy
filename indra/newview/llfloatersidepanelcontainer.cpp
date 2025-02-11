/**
 * @file llfloatersidepanelcontainer.cpp
 * @brief LLFloaterSidePanelContainer class definition
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "llnotificationsutil.h"
#include "llpaneleditwearable.h"

// newview includes
#include "llsidetraypanelcontainer.h"
#include "lltransientfloatermgr.h"
#include "llpaneloutfitedit.h"
#include "llsidepanelappearance.h"

// [RLVa:KB] - Checked: 2012-02-07 (RLVa-1.4.5) | Added: RLVa-1.4.5
LLFloaterSidePanelContainer::validate_signal_t LLFloaterSidePanelContainer::mValidateSignal;
// [/RLVa:KB]

LLFloaterSidePanelContainer::LLFloaterSidePanelContainer(const LLSD& key, const Params& params)
:   LLFloater(key, params)
{
    // Prevent transient floaters (e.g. IM windows) from hiding
    // when this floater is clicked.
    LLTransientFloaterMgr::getInstance()->addControlView(LLTransientFloaterMgr::GLOBAL, this);
}

LLFloaterSidePanelContainer::~LLFloaterSidePanelContainer()
{
    LLTransientFloaterMgr::getInstance()->removeControlView(LLTransientFloaterMgr::GLOBAL, this);
}

bool LLFloaterSidePanelContainer::postBuild()
{
    mMainPanel = getChild<LLPanel>(sMainPanelName);
    return true;
}

void LLFloaterSidePanelContainer::onOpen(const LLSD& key)
{
    mMainPanel->onOpen(key);
}

void LLFloaterSidePanelContainer::closeFloater(bool app_quitting)
{
    if(getInstanceName() == "appearance")
    {
        LLPanelOutfitEdit* panel_outfit_edit =
            LLFloaterSidePanelContainer::findPanel<LLPanelOutfitEdit>("appearance", "panel_outfit_edit");
        if (panel_outfit_edit)
        {
            LLFloater *parent = gFloaterView->getParentFloater(panel_outfit_edit);
        if (parent == this )
            {
                LLSidepanelAppearance* panel_appearance = dynamic_cast<LLSidepanelAppearance*>(mMainPanel);
            if (panel_appearance)
                {
                    LLPanelEditWearable *edit_wearable_ptr = panel_appearance->getWearable();
                    if (edit_wearable_ptr)
                    {
                        edit_wearable_ptr->onClose();
                    }
                if (!app_quitting)
                    {
                        panel_appearance->showOutfitsInventoryPanel();
                    }
                    }
                }
            }
        }
    }

    LLFloater::closeFloater(app_quitting);

    if (getInstanceName() == "inventory" && !getKey().isUndefined())
    {
        destroy();
    }
}

void LLFloaterSidePanelContainer::onClickCloseBtn(bool app_quitting)
{
    if (!app_quitting && getInstanceName() == "appearance")
    {
        LLPanelOutfitEdit* panel_outfit_edit = findChild<LLPanelOutfitEdit>("panel_outfit_edit");
        if (panel_outfit_edit)
        {
            LLFloater* parent = gFloaterView->getParentFloater(panel_outfit_edit);
            if (parent == this)
            {
                LLSidepanelAppearance* panel_appearance = dynamic_cast<LLSidepanelAppearance*>(getPanel("appearance"));
                if (panel_appearance)
                {
                    LLPanelEditWearable* edit_wearable_ptr = panel_appearance->getWearable();
                    if (edit_wearable_ptr && edit_wearable_ptr->getVisible() && edit_wearable_ptr->isDirty())
                    {
                        LLNotificationsUtil::add("UsavedWearableChanges", LLSD(), LLSD(), [this](const LLSD& notification, const LLSD& response)
                        {
                            onCloseMsgCallback(notification, response);
                        });
                        return;
                    }
                }
            }
        }
    }

    closeFloater();
}

void LLFloaterSidePanelContainer::onCloseMsgCallback(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (0 == option)
    {
        closeFloater();
    }
}

LLFloater* LLFloaterSidePanelContainer::getTopmostInventoryFloater()
{
    LLFloater* topmost_floater = NULL;
    S32 z_min = S32_MAX;

    LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
    for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end(); ++iter)
    {
        LLFloater* inventory_floater = (*iter);

        if (inventory_floater && inventory_floater->getVisible())
        {
            S32 z_order = gFloaterView->getZOrder(inventory_floater);
            if (z_order < z_min)
            {
                z_min = z_order;
                topmost_floater = inventory_floater;
            }
        }
    }
    return topmost_floater;
}

LLPanel* LLFloaterSidePanelContainer::openChildPanel(std::string_view panel_name, const LLSD& params)
{
    LLView* view = findChildView(panel_name, true);
    if (!view)
        return NULL;

    if (!getVisible())
    {
        openFloater();
    }
    else if (!hasFocus())
    {
        setFocus(true);
    }

    LLPanel* panel = NULL;

    LLSideTrayPanelContainer* container = dynamic_cast<LLSideTrayPanelContainer*>(view->getParent());
    if (container)
    {
        container->openPanel(std::string(panel_name), params);
        panel = container->getCurrentPanel();
    }
    else if ((panel = dynamic_cast<LLPanel*>(view)) != NULL)
    {
        panel->onOpen(params);
    }

    return panel;
}

// [RLVa:KB] - Checked: 2012-02-07 (RLVa-1.4.5) | Added: RLVa-1.4.5
bool LLFloaterSidePanelContainer::canShowPanel(std::string_view floater_name, const LLSD& key)
{
    return mValidateSignal(floater_name, sMainPanelName, key);
}

bool LLFloaterSidePanelContainer::canShowPanel(std::string_view floater_name, std::string_view panel_name, const LLSD& key)
{
    return mValidateSignal(floater_name, panel_name, key);
}
// [/RLVa:KB]

void LLFloaterSidePanelContainer::showPanel(std::string_view floater_name, const LLSD& key)
{
    LLFloaterSidePanelContainer* floaterp = LLFloaterReg::getTypedInstance<LLFloaterSidePanelContainer>(floater_name);
//  if (floaterp)
// [RLVa:KB] - Checked: 2013-04-16 (RLVa-1.4.8)
    if ( (floaterp) && ((floaterp->getVisible()) || (LLFloaterReg::canShowInstance(floater_name, key))) && (canShowPanel(floater_name, key)) )
// [/RLVa:KB]
    {
        floaterp->openChildPanel(sMainPanelName, key);
    }
}

void LLFloaterSidePanelContainer::showPanel(std::string_view floater_name, std::string_view panel_name, const LLSD& key)
{
// [SL:KB] - Patch: World-Derender | Checked: Catznip-3.2
    // Hack in case we forget a reference somewhere
    if ( (!panel_name.empty()) && ("panel_people" == panel_name) && (key.has("people_panel_tab_name")) && ("blocked_panel" == key["people_panel_tab_name"].asString()) )
    {
#ifndef LL_RELEASE_FOR_DOWNLOAD
        LL_ERRS() << "Request to open the blocked floater through the sidepanel!" << LL_ENDL;
#endif // LL_RELEASE_FOR_DOWNLOAD
        LLFloaterReg::showInstance("blocked", key);
        return;
    }
// [/SL:KB]

    LLFloaterSidePanelContainer* floaterp = LLFloaterReg::getTypedInstance<LLFloaterSidePanelContainer>(floater_name);
//  if (floaterp)
// [RLVa:KB] - Checked: 2013-04-16 (RLVa-1.4.8)
    if ( (floaterp) && ((floaterp->getVisible()) || (LLFloaterReg::canShowInstance(floater_name, key))) && (canShowPanel(floater_name, panel_name, key)) )
// [/RLVa:KB]
    {
        floaterp->openChildPanel(panel_name, key);
    }
}

LLPanel* LLFloaterSidePanelContainer::getPanel(std::string_view floater_name, std::string_view panel_name)
{
    LLFloaterSidePanelContainer* floaterp = LLFloaterReg::getTypedInstance<LLFloaterSidePanelContainer>(floater_name);
    if (floaterp)
    {
        if (panel_name == sMainPanelName)
        {
            return floaterp->mMainPanel;
        }
        else
        {
            return floaterp->findChild<LLPanel>(panel_name, true);
        }
    }

    return NULL;
}

LLPanel* LLFloaterSidePanelContainer::findPanel(std::string_view floater_name, std::string_view panel_name)
{
    LLFloaterSidePanelContainer* floaterp = LLFloaterReg::findTypedInstance<LLFloaterSidePanelContainer>(floater_name);
    if (floaterp)
    {
        if (panel_name == sMainPanelName)
        {
            return floaterp->mMainPanel;
        }
        else
        {
            return floaterp->findChild<LLPanel>(panel_name, true);
        }
    }

    return NULL;
}

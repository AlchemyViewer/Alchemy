/**
 * @file alfloaterao.cpp
 * @brief Animation overrider controls
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2011, Zi Ree @ Second Life
 * Copyright (C) 2016, Cinder <cinder@sdf.org>
 * Copyright (C) 2020, Rye Mutt <rye@alchemyviewer.org>
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

#include "alfloaterao.h"
#include "alaoengine.h"
#include "alaoset.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "llspinctrl.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"

ALFloaterAO::ALFloaterAO(const LLSD& key) : LLTransientDockableFloater(nullptr, true, key)
, LLEventTimer(10.f)
, mSetList()
, mSelectedSet(nullptr)
, mSelectedState(nullptr)
, mReloadCoverPanel(nullptr)
, mMainInterfacePanel(nullptr)
, mSetSelector(nullptr)
, mActivateSetButton(nullptr)
, mAddButton(nullptr)
, mRemoveButton(nullptr)
, mDefaultCheckBox(nullptr)
, mOverrideSitsCheckBox(nullptr)
, mSmartCheckBox(nullptr)
, mDisableMouselookCheckBox(nullptr)
, mStateSelector(nullptr)
, mAnimationList(nullptr)
, mCurrentBoldItem(nullptr)
, mMoveUpButton(nullptr)
, mMoveDownButton(nullptr)
, mTrashButton(nullptr)
, mCycleCheckBox(nullptr)
, mRandomizeCheckBox(nullptr)
, mCycleTimeTextLabel(nullptr)
, mCycleTimeSpinner(nullptr)
, mReloadButton(nullptr)
, mPreviousButton(nullptr)
, mNextButton(nullptr)
, mCanDragAndDrop(false)
, mImportRunning(false)
{
	mEventTimer.stop();
	
	mCommitCallbackRegistrar.add("AO.Reload", boost::bind(&ALFloaterAO::onClickReload, this));
	mCommitCallbackRegistrar.add("AO.ActivateSet", boost::bind(&ALFloaterAO::onClickActivate, this));
	mCommitCallbackRegistrar.add("AO.AddSet", boost::bind(&ALFloaterAO::onClickAdd, this));
	mCommitCallbackRegistrar.add("AO.RemoveSet", boost::bind(&ALFloaterAO::onClickRemove, this));
	mCommitCallbackRegistrar.add("AO.SelectSet", boost::bind(&ALFloaterAO::onSelectSet, this, _2));
	mCommitCallbackRegistrar.add("AO.SelectState", boost::bind(&ALFloaterAO::onSelectState, this));
	mCommitCallbackRegistrar.add("AO.NextAnim", boost::bind(&ALFloaterAO::onClickNext, this));
	mCommitCallbackRegistrar.add("AO.PrevAnim", boost::bind(&ALFloaterAO::onClickPrevious, this));
	mCommitCallbackRegistrar.add("AO.SetCycle", boost::bind(&ALFloaterAO::onCheckCycle, this));
	mCommitCallbackRegistrar.add("AO.SetCycleTime", boost::bind(&ALFloaterAO::onChangeCycleTime, this));
	mCommitCallbackRegistrar.add("AO.SetDefault", boost::bind(&ALFloaterAO::onCheckDefault, this));
	mCommitCallbackRegistrar.add("AO.SetRandomize", boost::bind(&ALFloaterAO::onCheckRandomize, this));
	mCommitCallbackRegistrar.add("AO.SetSitOverride", boost::bind(&ALFloaterAO::onCheckOverrideSits, this));
	mCommitCallbackRegistrar.add("AO.SetSmart", boost::bind(&ALFloaterAO::onCheckSmart, this));
	mCommitCallbackRegistrar.add("AO.DisableStandsML", boost::bind(&ALFloaterAO::onCheckDisableStands, this));
	mCommitCallbackRegistrar.add("AO.SelectAnim", boost::bind(&ALFloaterAO::onChangeAnimationSelection, this));
	mCommitCallbackRegistrar.add("AO.RemoveAnim", boost::bind(&ALFloaterAO::onClickTrash, this));
	mCommitCallbackRegistrar.add("AO.MoveAnimUp", boost::bind(&ALFloaterAO::onClickMoveUp, this));
	mCommitCallbackRegistrar.add("AO.MoveAnimDown", boost::bind(&ALFloaterAO::onClickMoveDown, this));
	
	//mEnableCallbackRegistrar.add("AO.EnableSet", boost::bind());
	//mEnableCallbackRegistrar.add("AO.EnableState", boost::bind());
}

ALFloaterAO::~ALFloaterAO()
{
	if (mReloadCallback.connected())
		mReloadCallback.disconnect();
	if (mAnimationChangedCallback.connected())
		mAnimationChangedCallback.disconnect();
}

void ALFloaterAO::reloading(const bool reload)
{
	if (reload)
		mEventTimer.start();
	else
		mEventTimer.stop();
	
	mReloadCoverPanel->setVisible(reload);
	enableSetControls(!reload);
	enableStateControls(!reload);
}

BOOL ALFloaterAO::tick()
{
	// reloading took too long, probably missed the signal, so we hide the reload cover
	LL_WARNS("AOEngine") << "AO reloading timeout." << LL_ENDL;
	updateList();
	return FALSE;
}

void ALFloaterAO::updateSetParameters()
{
	mOverrideSitsCheckBox->setValue(mSelectedSet->getSitOverride());
	mSmartCheckBox->setValue(mSelectedSet->getSmart());
	mDisableMouselookCheckBox->setValue(mSelectedSet->getMouselookDisable());
	BOOL isDefault = (mSelectedSet == ALAOEngine::instance().getDefaultSet()) ? TRUE : FALSE;
	mDefaultCheckBox->setValue(isDefault);
	mDefaultCheckBox->setEnabled(!isDefault);
	updateSmart();
}

void ALFloaterAO::updateAnimationList()
{
	S32 currentStateSelected = mStateSelector->getCurrentIndex();
	
	mStateSelector->removeall();
	onChangeAnimationSelection();
	
	if (!mSelectedSet)
	{
		mStateSelector->setEnabled(FALSE);
		mStateSelector->add(getString("ao_no_animations_loaded"));
		return;
	}
	
	for (auto const& stateName : mSelectedSet->mStateNames)
    {
        ALAOSet::AOState* state = mSelectedSet->getStateByName(stateName);
		mStateSelector->add(stateName, state, ADD_BOTTOM, TRUE);
	}
	
	enableStateControls(TRUE);
	
	if (currentStateSelected == -1)
	{
		mStateSelector->selectFirstItem();
	}
	else
	{
		mStateSelector->selectNthItem(currentStateSelected);
	}
	
	onSelectState();
}

void ALFloaterAO::updateList()
{
	mReloadButton->setEnabled(TRUE);
	mImportRunning = false;
	
	std::string currentSetName = mSetSelector->getSelectedItemLabel();
	if (currentSetName.empty())
	{
		currentSetName = ALAOEngine::instance().getCurrentSetName();
	}
	
	mSetList = ALAOEngine::instance().getSetList();
	mSetSelector->removeall();
	mSetSelector->clear();
	
	mAnimationList->deleteAllItems();
	mCurrentBoldItem = nullptr;
	reloading(false);
	
	if (mSetList.empty())
	{
		LL_DEBUGS("AOEngine") << "empty set list" << LL_ENDL;
		mSetSelector->add(getString("ao_no_sets_loaded"));
		mSetSelector->selectNthItem(0);
		enableSetControls(false);
		return;
	}
	
	for (U32 index = 0; index < mSetList.size(); ++index)
	{
		std::string setName = mSetList[index]->getName();
		mSetSelector->add(setName, &mSetList[index], ADD_BOTTOM, TRUE);
		if (setName.compare(currentSetName) == 0)
		{
			mSelectedSet = ALAOEngine::instance().selectSetByName(currentSetName);
			mSetSelector->selectNthItem(index);
			updateSetParameters();
			updateAnimationList();
		}
	}
	enableSetControls(true);
}

BOOL ALFloaterAO::postBuild()
{
	LLPanel* panel = getChild<LLPanel>("animation_overrider_outer_panel");
	mMainInterfacePanel = panel->getChild<LLPanel>("animation_overrider_panel");
	mReloadCoverPanel = panel->getChild<LLPanel>("ao_reload_cover");
	
	mSetSelector = mMainInterfacePanel->getChild<LLComboBox>("ao_set_selection_combo");
	mActivateSetButton = mMainInterfacePanel->getChild<LLButton>("ao_activate");
	mAddButton = mMainInterfacePanel->getChild<LLButton>("ao_add");
	mRemoveButton = mMainInterfacePanel->getChild<LLButton>("ao_remove");
	mDefaultCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_default");
	mOverrideSitsCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_sit_override");
	mSmartCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_smart");
	mDisableMouselookCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_disable_stands_in_mouselook");
	
	mStateSelector = mMainInterfacePanel->getChild<LLComboBox>("ao_state_selection_combo");
	mAnimationList = mMainInterfacePanel->getChild<LLScrollListCtrl>("ao_state_animation_list");
	mMoveUpButton = mMainInterfacePanel->getChild<LLButton>("ao_move_up");
	mMoveDownButton = mMainInterfacePanel->getChild<LLButton>("ao_move_down");
	mTrashButton = mMainInterfacePanel->getChild<LLButton>("ao_trash");
	mCycleCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_cycle");
	mRandomizeCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_randomize");
	mCycleTimeTextLabel = mMainInterfacePanel->getChild<LLTextBox>("ao_cycle_time_seconds_label");
	mCycleTimeSpinner = mMainInterfacePanel->getChild<LLSpinCtrl>("ao_cycle_time");
	
	mReloadButton = mMainInterfacePanel->getChild<LLButton>("ao_reload");
	mPreviousButton = mMainInterfacePanel->getChild<LLButton>("ao_previous");
	mNextButton = mMainInterfacePanel->getChild<LLButton>("ao_next");
	
	mAnimationList->setCommitOnSelectionChange(TRUE);
	
	updateSmart();
	
	mReloadCallback = ALAOEngine::instance().setReloadCallback(boost::bind(&ALFloaterAO::updateList, this));
	mAnimationChangedCallback = ALAOEngine::instance().setAnimationChangedCallback(boost::bind(&ALFloaterAO::onAnimationChanged, this, _1));
	
	onChangeAnimationSelection();
	mMainInterfacePanel->setVisible(TRUE);
	reloading(true);
	
	updateList();
	
	return LLDockableFloater::postBuild();
}

// static
ALFloaterAO* ALFloaterAO::getInstance()
{
	return LLFloaterReg::getTypedInstance<ALFloaterAO>("ao");
}

void ALFloaterAO::enableSetControls(const bool enable)
{
	mSetSelector->setEnabled(enable);
	mActivateSetButton->setEnabled(enable);
	mRemoveButton->setEnabled(enable);
	mDefaultCheckBox->setEnabled(enable);
	mOverrideSitsCheckBox->setEnabled(enable);
	mDisableMouselookCheckBox->setEnabled(enable);
	
	if (!enable)
	{
		enableStateControls(enable);
	}
}

void ALFloaterAO::enableStateControls(const bool enable)
{
	mStateSelector->setEnabled(enable);
	mAnimationList->setEnabled(enable);
	mCycleCheckBox->setEnabled(enable);
	if (enable)
	{
		updateCycleParameters();
	}
	else
	{
		mRandomizeCheckBox->setEnabled(enable);
		mCycleTimeTextLabel->setEnabled(enable);
		mCycleTimeSpinner->setEnabled(enable);
	}
	mPreviousButton->setEnabled(enable);
	mNextButton->setEnabled(enable);
	mCanDragAndDrop = enable;
}

void ALFloaterAO::onSelectSet(const LLSD& userdata)
{
	ALAOSet* set = ALAOEngine::instance().getSetByName(userdata.asString());
	if (!set)
	{
		onRenameSet();
		return;
	}
	
	mSelectedSet = set;
	
	updateSetParameters();
	updateAnimationList();
}

void ALFloaterAO::onRenameSet()
{
	if (!mSelectedSet)
	{
		LL_WARNS("AOEngine") << "Rename AO set without set selected." << LL_ENDL;
		return;
	}
	
	std::string name = mSetSelector->getSimple();
	LLStringUtil::trim(name);
	
	LLUIString new_set_name = name;
	
	if (!name.empty())
	{
		if (
			LLTextValidate::validateASCIIPrintableNoPipe(new_set_name.getWString()) &&	// only allow ASCII
			name.find_first_of(":|") == std::string::npos)								// don't allow : or |
		{
			if (ALAOEngine::instance().renameSet(mSelectedSet, name))
			{
				reloading(true);
				return;
			}
		}
		else
		{
			LLNotificationsUtil::add("RenameAOMustBeASCII", LLSD().with("AO_SET_NAME", name));
		}
	}
	mSetSelector->setSimple(mSelectedSet->getName());
}

void ALFloaterAO::onClickActivate()
{
	LL_DEBUGS("AOEngine") << "Set activated: " << mSetSelector->getSelectedItemLabel() << LL_ENDL;
	ALAOEngine::instance().selectSet(mSelectedSet);
}

LLScrollListItem* ALFloaterAO::addAnimation(const std::string& name)
{
	LLSD row;
	row["columns"][0]["column"] = "icon";
	row["columns"][0]["type"] = "icon";
	row["columns"][0]["value"] = "Inv_Animation";
	
	row["columns"][1]["column"] = "animation_name";
	row["columns"][1]["type"] = "text";
	row["columns"][1]["value"] = name;
	
	return mAnimationList->addElement(row);
}

void ALFloaterAO::onSelectState()
{
	mAnimationList->deleteAllItems();
	mCurrentBoldItem = nullptr;
	mAnimationList->setCommentText(getString("ao_no_animations_loaded"));
	mAnimationList->setEnabled(FALSE);
	
	onChangeAnimationSelection();
	
	if (!mSelectedSet) return;
	
	mSelectedState = mSelectedSet->getStateByName(mStateSelector->getSelectedItemLabel());
	if (!mSelectedState) return;
	
	mSelectedState = static_cast<ALAOSet::AOState*>(mStateSelector->getCurrentUserdata());
	if (!mSelectedState->mAnimations.empty())
	{
		for (auto& ao_animation : mSelectedState->mAnimations)
        {
			LLScrollListItem* item = addAnimation(ao_animation.mName);
			if (item)
			{
				item->setUserdata(&ao_animation.mInventoryUUID);
			}
		}
		
		mAnimationList->setCommentText(LLStringUtil::null);
		mAnimationList->setEnabled(TRUE);
	}
	
	mCycleCheckBox->setValue(mSelectedState->mCycle);
	mRandomizeCheckBox->setValue(mSelectedState->mRandom);
	mCycleTimeSpinner->setValue(mSelectedState->mCycleTime);
	
	updateCycleParameters();
}

void ALFloaterAO::onClickReload()
{
	reloading(true);
	
	mSelectedSet = nullptr;
	mSelectedState = nullptr;
	
	ALAOEngine::instance().reload(false);
	updateList();
}

void ALFloaterAO::onClickAdd()
{
	LLNotificationsUtil::add("NewAOSet", LLSD(), LLSD(),
							 boost::bind(&ALFloaterAO::newSetCallback, this, _1, _2));
}

BOOL ALFloaterAO::newSetCallback(const LLSD& notification, const LLSD& response)
{
	std::string new_name = response["message"].asString();
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	
	LLStringUtil::trim(new_name);
	
	LLUIString new_set_name = new_name;
	
	if (new_name.empty())
	{
		return FALSE;
	}
	else if (!LLTextValidate::validateASCIIPrintableNoPipe(new_set_name.getWString())		// only allow ASCII
			 || new_name.find_first_of(":|") != std::string::npos)							// don't allow : or |
	{
		LLNotificationsUtil::add("NewAOCantContainNonASCII", LLSD().with("AO_SET_NAME", new_name));
		return FALSE;
	}
	
	if (option == 0)
	{
		if (ALAOEngine::instance().addSet(new_name).notNull())
		{
			reloading(true);
			return TRUE;
		}
	}
	return FALSE;
}

void ALFloaterAO::onClickRemove()
{
	if (!mSelectedSet) return;
	
	LLNotificationsUtil::add("RemoveAOSet", LLSD().with("AO_SET_NAME", mSelectedSet->getName()),
							 LLSD(), boost::bind(&ALFloaterAO::removeSetCallback, this, _1, _2));
}

BOOL ALFloaterAO::removeSetCallback(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	
	if (option == 0)
	{
		if (ALAOEngine::instance().removeSet(mSelectedSet))
		{
			reloading(true);
			// to prevent snapping back to deleted set
			mSetSelector->removeall();
			// visually indicate there are no items left
			mSetSelector->clear();
			mAnimationList->deleteAllItems();
			mCurrentBoldItem = nullptr;
			return TRUE;
		}
	}
	return FALSE;
}

void ALFloaterAO::onCheckDefault()
{
	if (mSelectedSet)
	{
		ALAOEngine::instance().setDefaultSet(mSelectedSet);
	}
}

void ALFloaterAO::onCheckOverrideSits()
{
	if (mSelectedSet)
	{
		ALAOEngine::instance().setOverrideSits(mSelectedSet, mOverrideSitsCheckBox->getValue().asBoolean());
	}
	updateSmart();
}

void ALFloaterAO::updateSmart()
{
	mSmartCheckBox->setEnabled(mOverrideSitsCheckBox->getValue());
}

void ALFloaterAO::onCheckSmart()
{
	if (mSelectedSet)
	{
		ALAOEngine::instance().setSmart(mSelectedSet, mSmartCheckBox->getValue().asBoolean());
	}
}

void ALFloaterAO::onCheckDisableStands()
{
	if (mSelectedSet)
	{
		ALAOEngine::instance().setDisableStands(mSelectedSet, mDisableMouselookCheckBox->getValue().asBoolean());
	}
}

void ALFloaterAO::onChangeAnimationSelection()
{
	std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
	LL_DEBUGS("AOEngine") << "Selection count: " << list.size() << LL_ENDL;
	
	BOOL resortEnable = FALSE;
	BOOL trashEnable = FALSE;
	
	// Linden Lab bug: scroll lists still select the first item when you click on them, even when they are disabled.
	// The control does not memorize it's enabled/disabled state, so mAnimationList->mEnabled() doesn't seem to work.
	// So we need to safeguard against it.
	if (!mCanDragAndDrop)
	{
		mAnimationList->deselectAllItems();
		LL_DEBUGS("AOEngine") << "Selection count now: " << list.size() << LL_ENDL;
	}
	else if (!list.empty())
	{
		if (list.size() == 1)
		{
			resortEnable = TRUE;
		}
		trashEnable = TRUE;
	}
	
	mMoveDownButton->setEnabled(resortEnable);
	mMoveUpButton->setEnabled(resortEnable);
	mTrashButton->setEnabled(trashEnable);
}

void ALFloaterAO::onClickMoveUp()
{
	if (!mSelectedState)
	{
		return;
	}
	
	std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
	if (list.size() != 1)
	{
		return;
	}
	
	S32 currentIndex = mAnimationList->getFirstSelectedIndex();
	if (currentIndex == -1)
	{
		return;
	}
	
	if (ALAOEngine::instance().swapWithPrevious(mSelectedState, currentIndex))
	{
		mAnimationList->swapWithPrevious(currentIndex);
	}
}

void ALFloaterAO::onClickMoveDown()
{
	if (!mSelectedState)
	{
		return;
	}
	
	std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
	if (list.size() != 1) return;
	
	S32 currentIndex = mAnimationList->getFirstSelectedIndex();
	if (currentIndex >= (mAnimationList->getItemCount() - 1))
	{
		return;
	}
	
	if (ALAOEngine::instance().swapWithNext(mSelectedState, currentIndex))
	{
		mAnimationList->swapWithNext(currentIndex);
	}
}

void ALFloaterAO::onClickTrash()
{
	if (!mSelectedState) return;
	
	std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
	if (list.empty()) return;

	for (auto iter = list.crbegin(), iter_end = list.crend(); iter != iter_end; ++iter)
	{
		ALAOEngine::instance().removeAnimation(mSelectedSet, mSelectedState, mAnimationList->getItemIndex(*iter));
	}
	
	mAnimationList->deleteSelectedItems();
	mCurrentBoldItem = nullptr;
}

void ALFloaterAO::updateCycleParameters()
{
	BOOL cycle = mCycleCheckBox->getValue().asBoolean();
	mRandomizeCheckBox->setEnabled(cycle);
	mCycleTimeTextLabel->setEnabled(cycle);
	mCycleTimeSpinner->setEnabled(cycle);
}

void ALFloaterAO::onCheckCycle()
{
	if (mSelectedState)
	{
		bool cycle = mCycleCheckBox->getValue().asBoolean();
		ALAOEngine::instance().setCycle(mSelectedState, cycle);
		updateCycleParameters();
	}
}

void ALFloaterAO::onCheckRandomize()
{
	if (mSelectedState)
	{
		ALAOEngine::instance().setRandomize(mSelectedState, mRandomizeCheckBox->getValue().asBoolean());
	}
}

void ALFloaterAO::onChangeCycleTime()
{
	if (mSelectedState)
	{
		ALAOEngine::instance().setCycleTime(mSelectedState, mCycleTimeSpinner->getValueF32());
	}
}

void ALFloaterAO::onClickPrevious()
{
	ALAOEngine::instance().cycle(ALAOEngine::CyclePrevious);
}

void ALFloaterAO::onClickNext()
{
	ALAOEngine::instance().cycle(ALAOEngine::CycleNext);
}

void ALFloaterAO::onAnimationChanged(const LLUUID& animation)
{
	LL_DEBUGS("AOEngine") << "Received animation change to " << animation << LL_ENDL;
	
	if (mCurrentBoldItem)
	{
		LLScrollListText* column = static_cast<LLScrollListText*>(mCurrentBoldItem->getColumn(1));
		column->setFontStyle(LLFontGL::NORMAL);
		
		mCurrentBoldItem = nullptr;
	}
	
	if (animation.isNull()) return;
	
	std::vector<LLScrollListItem*> item_list = mAnimationList->getAllData();
	for (LLScrollListItem* item : item_list)
	{
		LLUUID* id = static_cast<LLUUID*>(item->getUserdata());
		
		if (id == &animation)
		{
			mCurrentBoldItem = item;
			
			LLScrollListText* column = static_cast<LLScrollListText*>(mCurrentBoldItem->getColumn(1));
			column->setFontStyle(LLFontGL::BOLD);
			
			return;
		}
	}
}

// virtual
BOOL ALFloaterAO::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType type, void* data,
								  EAcceptance* accept, std::string& tooltipMsg)
{	
	LLInventoryItem* item = (LLInventoryItem*)data;
	
	if (type == DAD_NOTECARD)
	{
		if (mImportRunning)
		{
			*accept = ACCEPT_NO;
			return TRUE;
		}
		*accept = ACCEPT_YES_SINGLE;
		if (item && drop)
		{
			if (ALAOEngine::instance().importNotecard(item))
			{
				reloading(true);
				mReloadButton->setEnabled(FALSE);
				mImportRunning = true;
			}
		}
	}
	else if (type == DAD_ANIMATION)
	{
		if (!drop && (!mSelectedSet || !mSelectedState || !mCanDragAndDrop))
		{
			*accept = ACCEPT_NO;
			return TRUE;
		}
		*accept = ACCEPT_YES_MULTI;
		if (item && drop)
		{
			if (ALAOEngine::instance().addAnimation(mSelectedSet, mSelectedState, item))
			{
				addAnimation(item->getName());
				
				// TODO: this would be the right thing to do, but it blocks multi drop
				// before final release this must be resolved
				reloading(true);
			}
		}
	}
	else
	{
		*accept = ACCEPT_NO;
	}
	
	return TRUE;
}

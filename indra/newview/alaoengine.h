/**
 * @file alaoengine.h
 * @brief The core Animation Overrider engine
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Zi Ree @ Second Life
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
 * $/LicenseInfo$
 */

#ifndef AL_AOENGINE_H
#define AL_AOENGINE_H

#include <boost/signals2.hpp>

#include "alaoset.h"

#include "llassettype.h"
#include "lleventtimer.h"

#include "llextendedstatus.h"
#include "llsingleton.h"

class ALAOTimerCollection final : public LLEventTimer
{
public:
	ALAOTimerCollection();
	~ALAOTimerCollection() = default;

	BOOL tick() override;

	void setInventoryTimer(const bool enable);
	void setSettingsTimer(const bool enable);
	void setReloadTimer(const bool enable);
	void setImportTimer(const bool enable);

protected:
	void updateTimers();

	bool mInventoryTimer;
	bool mSettingsTimer;
	bool mReloadTimer;
	bool mImportTimer;
};

// ----------------------------------------------------

class ALAOSitCancelTimer final : public LLEventTimer
{
public:
	ALAOSitCancelTimer();
	~ALAOSitCancelTimer() = default;

	void oneShot();
	void stop();

	BOOL tick() override;

protected:
	S32 mTickCount;
};

// ----------------------------------------------------

class LLInventoryItem;
class LLVFS;

class ALAOEngine final : public LLSingleton<ALAOEngine>
{
	LLSINGLETON(ALAOEngine);
	~ALAOEngine();

public:
	typedef enum e_cycle_mode
	{
		CycleAny,
		CycleNext,
		CyclePrevious
	} eCycleMode;

	void enable(const bool enable);
	const LLUUID override(const LLUUID& motion, const bool start);
	void tick();
	void update();
	void reload(const bool reload);
	void reloadStateAnimations(ALAOSet::AOState* state);
	void clear(const bool clear);

	const LLUUID& getAOFolder() const;

	LLUUID addSet(const std::string& name, bool reload = true);
	bool removeSet(ALAOSet* set);

	bool addAnimation(const ALAOSet* set, ALAOSet::AOState* state,
					  const LLInventoryItem* item, bool reload = true);
	bool removeAnimation(const ALAOSet* set, ALAOSet::AOState* state, S32 index);
	void checkSitCancel();
	void checkBelowWater(const bool under);

	bool importNotecard(const LLInventoryItem* item);
	void processImport(const bool process);

	bool swapWithPrevious(ALAOSet::AOState* state, S32 index);
	bool swapWithNext(ALAOSet::AOState* state, S32 index);

	void cycleTimeout(const ALAOSet* set);
	void cycle(eCycleMode cycleMode);

	void inMouselook(const bool in_mouselook);
	void selectSet(ALAOSet* set);
	ALAOSet* selectSetByName(const std::string& name);
	ALAOSet* getSetByName(const std::string& name) const;

	// callback from LLAppViewer
	static void onLoginComplete();

	const std::vector<ALAOSet*> getSetList() const;
	const std::string& getCurrentSetName() const;
	const ALAOSet* getDefaultSet() const;
	bool renameSet(ALAOSet* set, const std::string& name);

	void setDefaultSet(ALAOSet* set);
	void setOverrideSits(ALAOSet* set, const bool override_sits);
	void setSmart(ALAOSet* set, const bool smart);
	void setDisableStands(ALAOSet* set, const bool disable);
	void setCycle(ALAOSet::AOState* set, const bool cycle);
	void setRandomize(ALAOSet::AOState* state, const bool randomize);
	void setCycleTime(ALAOSet::AOState* state, const F32 time);

	void saveSettings();

	typedef boost::signals2::signal<void ()> updated_signal_t;
	boost::signals2::connection setReloadCallback(const updated_signal_t::slot_type& cb)
	{
		return mUpdatedSignal.connect(cb);
	};

	typedef boost::signals2::signal<void (const LLUUID&)> animation_changed_signal_t;
	boost::signals2::connection setAnimationChangedCallback(const animation_changed_signal_t::slot_type& cb)
	{
		return mAnimationChangedSignal.connect(cb);
	};
	
	typedef boost::signals2::signal<void (const std::string&)> set_changed_signal_t;
	boost::signals2::connection setSetChangedCallback(const set_changed_signal_t::slot_type& cb)
	{
		return mSetChangedSignal.connect(cb);
	}

protected:
	void init();

	void setLastMotion(const LLUUID& motion);
	void setLastOverriddenMotion(const LLUUID& motion);
	void setStateCycleTimer(const ALAOSet::AOState* state);

	void stopAllStandVariants();
	void stopAllSitVariants();

	bool foreignAnimations(const LLUUID& seat);
	const LLUUID& mapSwimming(const LLUUID& motion) const;

	void updateSortOrder(ALAOSet::AOState* state);
	void saveSet(const ALAOSet* set);
	void saveState(const ALAOSet::AOState* state);

	bool createAnimationLink(const ALAOSet* set, ALAOSet::AOState* state, const LLInventoryItem* item);
	bool findForeignItems(const LLUUID& uuid) const;
	void purgeFolder(const LLUUID& uuid) const;

	void onRegionChange();

	void onToggleAOControl();
	static void onNotecardLoadComplete(LLVFS* vfs, const LLUUID& assetUUID, LLAssetType::EType type,
												void* userdata, S32 status, LLExtStat extStatus);
	void parseNotecard(std::unique_ptr<char[]>&& buffer);

	updated_signal_t mUpdatedSignal;
	animation_changed_signal_t mAnimationChangedSignal;
	set_changed_signal_t mSetChangedSignal;

	ALAOTimerCollection mTimerCollection;
	ALAOSitCancelTimer mSitCancelTimer;

	bool mEnabled;
	bool mInMouselook;
	bool mUnderWater;

	LLUUID mAOFolder;
	LLUUID mLastMotion;
	LLUUID mLastOverriddenMotion;

	std::vector<ALAOSet*> mSets;
	std::vector<ALAOSet*> mOldSets;
	ALAOSet* mCurrentSet;
	ALAOSet* mDefaultSet;
	
	ALAOSet* mImportSet;
	std::vector<ALAOSet*> mOldImportSets;
	LLUUID mImportCategory;
	S32 mImportRetryCount;

	boost::signals2::connection mRegionChangeConnection;
};

extern const std::string ROOT_AO_FOLDER;

#endif // LL_AOENGINE_H

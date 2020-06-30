/**
 * @file alaoset.h
 * @brief Implementation of an Animation Overrider set of animations
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Zi Ree @ Second Life
 * Copyright (C) 2016, Cinder Roxley <cinder@sdf.org>
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

#ifndef AL_AOSET_H
#define AL_AOSET_H

#include <utility>
#include "lleventtimer.h"

class ALAOSet : public LLEventTimer
{
public:
	ALAOSet(const LLUUID& inventoryID);
	~ALAOSet();

	// keep number and order in sync with list of names in the constructor
	enum
	{
		Start = 0,		// convenience, so we don't have to know the name of the first state
		Standing = 0,
		Walking,
		Running,
		Sitting,
		SittingOnGround,
		Crouching,
		CrouchWalking,
		Landing,
		SoftLanding,
		StandingUp,
		Falling,
		FlyingDown,
		FlyingUp,
		Flying,
		FlyingSlow,
		Hovering,
		Jumping,
		PreJumping,
		TurningRight,
		TurningLeft,
		Typing,
		Floating,
		SwimmingForward,
		SwimmingUp,
		SwimmingDown,
		AOSTATES_MAX
	};

	struct AOAnimation
	{
		AOAnimation(): mSortOrder( 0 ) {}
		AOAnimation(std::string name, const LLUUID& asset_id, const LLUUID& inv_id, S32 sort_order)
			: mName(std::move(name))
			, mAssetUUID(asset_id)
			, mInventoryUUID(inv_id)
			, mSortOrder(sort_order)
		{}
		std::string mName;
		LLUUID mAssetUUID;
		LLUUID mInventoryUUID;
		S32 mSortOrder;
	};

	struct AOState
	{
		std::string mName;
		std::vector<std::string> mAlternateNames;
		LLUUID mRemapID;
		bool mCycle;
		bool mRandom;
		S32 mCycleTime;
		std::vector<AOAnimation> mAnimations;
		U32 mCurrentAnimation;
		LLUUID mCurrentAnimationID;
		LLUUID mInventoryUUID;
		bool mDirty;
	};

	// getters and setters
	const LLUUID& getInventoryUUID() const { return mInventoryID; }
	void setInventoryUUID(const LLUUID& inv_id) { mInventoryID = inv_id; }

	const std::string& getName() const { return mName; }
	void setName(const std::string& name) { mName = name; }

	bool getSitOverride() const { return mSitOverride; }
	void setSitOverride(const bool sit_override) { mSitOverride = sit_override; }

	bool getSmart() const { return mSmart; }
	void setSmart(const bool smart) { mSmart = smart; }

	bool getMouselookDisable() const { return mMouselookDisable; }
	void setMouselookDisable(const bool disable) { mMouselookDisable = disable; }

	bool getComplete() const { return mComplete; }
	void setComplete(const bool complete) { mComplete = complete; }

	const LLUUID& getMotion() const { return mCurrentMotion; }
	void setMotion(const LLUUID& motion) { mCurrentMotion = motion; }

	bool getDirty() const { return mDirty; }
	void setDirty(const bool dirty) { mDirty = dirty; }

	AOState* getState(S32 name);
	AOState* getStateByName(const std::string& name);
	AOState* getStateByRemapID(const LLUUID& id);
	const LLUUID& getAnimationForState(AOState* state) const;

	void startTimer(F32 timeout);
	void stopTimer();
	virtual BOOL tick() override;

	std::vector<std::string> mStateNames;

private:
	LLUUID mInventoryID;

	std::string mName;
	bool mSitOverride;
	bool mSmart;
	bool mMouselookDisable;
	bool mComplete;
	LLUUID mCurrentMotion;
	bool mDirty;

	std::array<AOState, AOSTATES_MAX> mStates;
};

#endif // LL_AOSET_H

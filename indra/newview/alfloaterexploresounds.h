/** 
 * @file alfloaterexploresounds.h
 */

#ifndef AL_ALFLOATEREXPLORESOUNDS_H
#define AL_ALFLOATEREXPLORESOUNDS_H

#include "llfloater.h"
#include "lleventtimer.h"
#include "llaudioengine.h"
#include "llavatarnamecache.h"

class LLCheckBoxCtrl;
class LLScrollListCtrl;

class ALFloaterExploreSounds final
: public LLFloater, public LLEventTimer
{
public:
	ALFloaterExploreSounds(const LLSD& key);
	BOOL postBuild();

	BOOL tick();

	LLSoundHistoryItem getItem(const LLUUID& itemID);

private:
	virtual ~ALFloaterExploreSounds();
	void handlePlayLocally();
	void handleLookAt();
	void handleStop();
	void handleStopLocally();
	void handleSelection();
	void blacklistSound();

	LLScrollListCtrl*	mHistoryScroller;
	LLCheckBoxCtrl*		mCollisionSounds;
	LLCheckBoxCtrl*		mRepeatedAssets;
	LLCheckBoxCtrl*		mAvatarSounds;
	LLCheckBoxCtrl*		mObjectSounds;
	LLCheckBoxCtrl*		mPaused;

	std::list<LLSoundHistoryItem> mLastHistory;

	uuid_vec_t mLocalPlayingAudioSourceIDs;

	typedef std::map<LLUUID, boost::signals2::connection> blacklist_avatar_name_cache_connection_map_t;
	blacklist_avatar_name_cache_connection_map_t mBlacklistAvatarNameCacheConnections;

	void onBlacklistAvatarNameCacheCallback(const LLUUID& av_id, const LLAvatarName& av_name, const LLUUID& asset_id, const std::string& region_name);
};

#endif

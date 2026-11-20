/**
 * @file llvocache.h
 * @brief Cache of objects on the viewer.
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#ifndef LL_LLVOCACHE_H
#define LL_LLVOCACHE_H

#include "lluuid.h"
#include "lldatapacker.h"
#include "lldir.h"
#include "llvieweroctree.h"
#include "llgltfmaterial.h"

#include <boost/unordered_map.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <functional>
#include <initializer_list>
#include <vector>

//---------------------------------------------------------------------------
// Cache entries
class LLCamera;

// The overrides a region last sent for one object: every face it lists, and none for the faces it leaves out. An entry
// with no sides says the region removed the object's overrides; it lives for the session and is never written.
class LLGLTFOverrideCacheEntry
{
public:
    static const std::string VERSION_LABEL;
    static const int VERSION;
    // How long the entry waits for the region to answer an edit the viewer sent for the object, showing the edit
    // meanwhile. The answer replaces the entry.
    static constexpr F64 HOLD_SECONDS = 10.0;

    bool fromLLSD(const LLSD& data);
    LLSD toLLSD() const;
    // Build mGLTFMaterial from mSides if it has not been built. Loading the
    // cache leaves the materials unbuilt, since most cached objects are never
    // created; the first apply to an object builds them.
    void materialize();

    // The override a face takes from the entry: the region's for it, or none. Built by materialize().
    LLGLTFMaterial* overrideFor(S32 side) const;
    // Whether a face holding `held` shows `wanted`: the same override by value, or none on both.
    static bool sameOverride(const LLGLTFMaterial* held, const LLGLTFMaterial* wanted);

    void hold(F64 now) { mHeldUntil = now + HOLD_SECONDS; }
    bool isHeld(F64 now) const { return now < mHeldUntil; }

    // Whether the entry is for the object `id`: one naming no object is taken to be.
    bool isFor(const LLUUID& id) const { return mObjectId.isNull() || mObjectId == id; }

    LLUUID mObjectId;
    U32    mLocalId = 0;
    boost::unordered_map<S32, LLSD> mSides; //override LLSD per side
    boost::unordered_map<S32, LLPointer<LLGLTFMaterial> > mGLTFMaterial; //GLTF material per side, empty until materialize()
    U64 mRegionHandle = 0;
    F64 mHeldUntil = 0.0;
};

class LLVOCacheEntry
:   public LLViewerOctreeEntryData
{
public:
    enum
    {
        //low 16-bit state
        INACTIVE = 0x00000000,     //not visible
        IN_QUEUE = 0x00000001,     //in visible queue, object to be created
        WAITING  = 0x00000002,     //object creation request sent
        ACTIVE   = 0x00000004,      //object created, and in rendering pipeline.

        //high 16-bit state
        IN_VO_TREE = 0x00010000,    //the entry is in the object cache tree.

        LOW_BITS  = 0x0000ffff,
        HIGH_BITS = 0xffff0000
    };

    struct CompareVOCacheEntry
    {
        bool operator()(const LLVOCacheEntry* const& lhs, const LLVOCacheEntry* const& rhs) const
        {
            F32 lpa = lhs->getSceneContribution();
            F32 rpa = rhs->getSceneContribution();

            //larger pixel area first
            if(lpa > rpa)
            {
                return true;
            }
            else if(lpa < rpa)
            {
                return false;
            }
            else
            {
                return lhs < rhs;
            }
        }
    };

protected:
    ~LLVOCacheEntry();
public:
    LLVOCacheEntry(U32 local_id, U32 crc, LLDataPackerBinaryBuffer &dp);
    // Decode one entry from a cache file held in memory. On success the cursor
    // is left at the next entry; on failure the entry has a zero local id and
    // the cursor is untouched.
    LLVOCacheEntry(const U8*& cursor, const U8* end);
    LLVOCacheEntry();

    void updateEntry(U32 crc, LLDataPackerBinaryBuffer &dp);

    void clearState(U32 state) {mState &= ~state;}
    bool hasState(U32 state)   {return mState & state;}
    void setState(U32 state);
    bool isState(U32 state)    {return (mState & LOW_BITS) == state;}
    U32  getState() const      {return mState & LOW_BITS;}

    bool isAnyVisible(const LLVector4a& camera_origin, const LLVector4a& local_camera_origin, F32 dist_threshold);

    U32 getLocalID() const          { return mLocalID; }
    U32 getCRC() const              { return mCRC; }
    S32 getHitCount() const         { return mHitCount; }
    S32 getCRCChangeCount() const   { return mCRCChangeCount; }

    void calcSceneContribution(const LLVector4a& camera_origin, bool needs_update, U32 last_update, F32 dist_threshold);
    void setSceneContribution(F32 scene_contrib) {mSceneContrib = scene_contrib;}
    F32 getSceneContribution() const             { return mSceneContrib;}

    void dump() const;
    S32 writeToBuffer(U8 *data_buffer) const;
    LLDataPackerBinaryBuffer *getDP() const;
    // Whether this entry is the object of this UUID, which its data opens with.
    bool isObject(const LLUUID& id) const;
    // The UUID the entry's data opens with, its object's; null where the data is too short to hold one.
    LLUUID getObjectID() const;
    void recordHit();

    /*virtual*/ void setOctreeEntry(LLViewerOctreeEntry* entry);

    void setParentID(U32 id);
    U32  getParentID() const {return mParentID;}
    bool isChild() const {return mParentID > 0;}

    // Where an entry goes once a full update or a probe has decoded it.
    enum class EChildPlace
    {
        // Not a child: its data names no parent.
        ROOT,
        // Queued to be made, its parent made: from its entry, where it has a
        // valid one, and the parent's drawable made (ACTIVE).
        MADE,
        // On its parent's entry, which is not made yet, though it may be
        // queued or waiting for its drawable, to be made as the parent's
        // drawable is (LLDrawable::init).
        WITH_PARENT,
        // Among the region's orphans, until its parent comes.
        ORPHAN
    };
    // This entry takes parent_id, which its data names, 0 for none, as its
    // parent, the caller having taken it off the entry of the one it named
    // before, and says where it goes. parent is the parent's valid entry, null
    // where it has none, and parent_made, where it has none, whether an object
    // is made under the parent's local ID. A child goes where it would whether
    // or not it named the parent before: one an update or a probe makes valid
    // again names the parent it named, and may be waiting nowhere.
    EChildPlace takeParent(U32 parent_id, const LLVOCacheEntry* parent, bool parent_made);

    void addChild(LLVOCacheEntry* entry);
    void removeChild(LLVOCacheEntry* entry);
    void removeAllChildren();
    LLVOCacheEntry* getChild(); //remove the first child, and return it.
    S32  getNumOfChildren() const { return static_cast<S32>(mChildrenList.size()); }

    void setBoundingInfo(const LLVector3& pos, const LLVector3& scale); //called from processing object update message
    void updateParentBoundingInfo();
    void saveBoundingSphere();

    void setValid(bool valid = true) {mValid = valid;}
    bool isValid() const {return mValid;}

    // The update made from the entry's bytes was refused, cut short (LLViewerObject::processUpdateMessage), and the
    // object asked for whole again. The first refusal at a CRC sets the entry's CRC apart from the region's, so that
    // the update the region sends replaces the bytes rather than being taken for them; a second, of the bytes the
    // region sent, leaves the entry refused at that CRC (isRefused) until its object changes, rather than asked for
    // and refused again for as long as it is in view.
    void refuseUpdate();
    bool isRefused(U32 crc) const { return mRefusals > 1 && crc == mRefusedCRC; }
    // Whether an update made from the entry was refused since it was last killed. Refused and not yet sent again, the
    // entry is invalid, and holds its children for the object the region's update makes (LLViewerRegion::findOrphans).
    bool wasRefused() const { return mRefusals > 0; }
    // Whether a kill of the entry's local ID reaches it: a valid entry, or one invalid only for a refused update.
    bool isKillable() const { return mValid || wasRefused(); }
    // A kill lets the refused update go with the object it was of.
    void forgetRefusal() { mRefusals = 0; }

    void setUpdateFlags(U32 flags) {mUpdateFlags = flags;}
    U32  getUpdateFlags() const    {return mUpdateFlags;}

    static void updateDebugSettings();
    // How far the scene-load radii are pulled in for the memory in use: 1 for
    // not at all, 0 at the tightest. All figures in megabytes: the process's
    // allocation, installed RAM, the heap cap, and the two bound settings. A
    // setting that is explicit is the bound; one at its default follows RAM.
    static F32  memoryAdjustFactor(F32 allocated_MB, F32 physical_MB, F32 heap_cap_MB,
                                   F32 low_setting_MB, F32 high_setting_MB,
                                   bool low_is_explicit = false, bool high_is_explicit = false);

    // What isAnyVisible answers, with no octree to ask. The frame numbers are the
    // octree's own counter; the distance is squared, from the camera to the entry's
    // bounding centre, and the threshold is the radius of the sphere behind the
    // camera. An entry stays in memory while its group was recently visible, while
    // its frame window has not run out, or while it sits inside that sphere.
    struct VisibilityFacts
    {
        bool mHasGroup             = false;
        bool mGroupRecentlyVisible = false;
        bool mGroupOccluded        = false;
        S32  mGroupAnyVisibleFrame = 0;
        S32  mEntryVisibleFrame    = 0;
        S32  mCurrentFrame         = 0;
        U32  mMinFrameRange        = 0;
        bool mIsChild              = false;
        F32  mDistanceSquared      = 0.f;
        F32  mRadius               = 0.f;
        F32  mDistThreshold        = 0.f;
    };
    static bool staysInMemory(const VisibilityFacts& facts);

    // The radius outside which an entry may be evicted, from the radius that
    // loads one, the SceneLoadMinRadius setting and the draw distance. Wider
    // than the load radius on purpose: sRearFarRadius does both jobs otherwise,
    // and loading and unloading at the same distance is what paged objects in
    // and out when the memory bounds pinned that radius to two metres. Never
    // narrower than the setting, and never wider than the draw distance.
    static F32  evictRadius(F32 rear_far_radius, F32 min_radius, F32 draw_radius);

    static F32  getSquaredPixelThreshold(bool is_front);

private:
    void updateParentBoundingInfo(const LLVOCacheEntry* child);

public:
    // Keyed by local id, looked up once per cached-object probe the sim sends
    // after a handshake. A node map keeps each entry's slot at a stable address
    // across rehash, which callers holding the raw entry pointer rely on.
    typedef boost::unordered_node_map<U32, LLPointer<LLVOCacheEntry>> vocache_entry_map_t;
    typedef std::set<LLVOCacheEntry*>                      vocache_entry_set_t;
    typedef std::set<LLVOCacheEntry*, CompareVOCacheEntry> vocache_entry_priority_list_t;

    typedef boost::unordered_map<U32, LLGLTFOverrideCacheEntry>  vocache_gltf_overrides_map_t;

    // Lets every entry of a region's map go, leaving nothing that points at one: each leaves the cache's tree
    // through `leave_tree` and the octree entry it may share with a drawable, and is marked invalid, and `sets`,
    // which hold bare pointers to the map's entries, are emptied. The map's references go to `keep`.
    static void releaseAll(vocache_entry_map_t& entries, std::initializer_list<vocache_entry_set_t*> sets,
                           const std::function<void(LLVOCacheEntry*)>& leave_tree, std::vector<LLPointer<LLVOCacheEntry>>& keep);

    // What a kill of local_id takes from a region's cache: its entry, through `kill_entry`, where it has one a kill
    // reaches (isKillable), and its glTF overrides whether or not it has, since an object given the local ID later
    // would take them. True where it took any overrides.
    static bool killLocalID(U32 local_id, vocache_entry_map_t& entries, vocache_gltf_overrides_map_t& overrides,
                            const std::function<void(LLVOCacheEntry*)>& kill_entry);

    // What a kill of an entry takes of its object's glTF overrides: none where the object stays in the world, its entry
    // killed so that it is made from the region's update, since the region sends them only with the object's full
    // update; all where the object is gone. True where it took any.
    static bool killEntryOverrides(U32 local_id, bool object_stays, vocache_gltf_overrides_map_t& overrides);

    // The region sent an entry for local_id holding object_id's data: overrides kept under the local ID for another
    // object are dropped. True where it dropped any.
    static bool dropOverridesOfAnother(U32 local_id, const LLUUID& object_id, vocache_gltf_overrides_map_t& overrides);

    // Takes this entry off the entry of the parent it names in `entries`, where that entry holds its children: a
    // valid one, or one invalid for a refused update (wasRefused), which keeps them for the object the region's update
    // makes. Returns that entry, null where there is none, this entry then naming its parent still: a killed entry
    // holds no children.
    LLVOCacheEntry* leaveParent(const vocache_entry_map_t& entries);

// [SL:KB] - Patch: World-Derender | Checked: 2014-08-10 (Catznip-3.7)
    vocache_entry_set_t::const_iterator getChildrenBegin() const { return mChildrenList.begin(); }
    vocache_entry_set_t::const_iterator getChildrenEnd() const { return mChildrenList.end(); }
// [/SL:KB]

    S32                         mLastCameraUpdated;
protected:
    U32                         mLocalID;
    U32                         mParentID;
    U32                         mCRC;
    U32                         mUpdateFlags; //receive from sim
    S32                         mHitCount;
    S32                         mCRCChangeCount;    // in memory only, for dumpCache
    U32                         mRefusedCRC = 0;    // the CRC refuseUpdate last refused at, in memory only
    U8                          mRefusals   = 0;    // and how often, to 2
    mutable LLDataPackerBinaryBuffer    mDP;
    U8                          *mBuffer;

    F32                         mSceneContrib; //projected scene contributuion of this object.
    U32                         mState; //high 16 bits reserved for special use.
    vocache_entry_set_t         mChildrenList; //children entries in a linked set.

    bool                        mValid; //if set, this entry is valid, otherwise it is invalid and will be removed.

    LLVector4a                  mBSphereCenter; //bounding sphere center
    F32                         mBSphereRadius; //bounding sphere radius

public:
    static U32                  sMinFrameRange;
    static F32                  sNearRadius;
    static F32                  sRearFarRadius;
    static F32                  sEvictFarRadius;
    // The last factor updateDebugSettings derived the radii from: 1 with memory
    // to spare, 0 at the tightest. Kept so readers can report the policy the
    // radii came from without recomputing it.
    static F32                  sMemoryAdjustFactor;
    static F32                  sFrontPixelThreshold;
    static F32                  sRearPixelThreshold;
};

class LLVOCacheGroup : public LLOcclusionCullingGroup
{
public:
    LLVOCacheGroup(OctreeNode* node, LLViewerOctreePartition* part) : LLOcclusionCullingGroup(node, part){}

    //virtual
    void handleChildAddition(const OctreeNode* parent, OctreeNode* child);

protected:
    virtual ~LLVOCacheGroup();
};

class LLVOCachePartition : public LLViewerOctreePartition
{
public:
    LLVOCachePartition(LLViewerRegion* regionp);
    virtual ~LLVOCachePartition();

    bool addEntry(LLViewerOctreeEntry* entry);
    void removeEntry(LLViewerOctreeEntry* entry);
    /*virtual*/ S32 cull(LLCamera &camera, bool do_occlusion);
    void addOccluders(LLViewerOctreeGroup* gp);
    void resetOccluders();
    void processOccluders(LLCamera* camera);
    void removeOccluder(LLVOCacheGroup* group);

    void setCullHistory(bool has_new_object);

    bool isFrontCull() const {return mFrontCull;}

    // Whether the front cull runs on `frame` while the partition is quiet and the camera rests. Each block of
    // max(period / 2, 4) frames runs it on the one frame `idle_hash` picks, and moves `idle_hash` on at its first frame,
    // so the culls fall one more than a block apart, and two fall together once the hash wraps.
    static bool quietFrontCullDue(U32 frame, U32 period, U32& idle_hash);

    // What the front cull knows of a group in view whose objects are large enough to make, with occlusion on. The
    // frames are the octree's counter: the group was last in view to the world camera at mGroupSeenFrame, and the
    // partition's front cull before this one ran at mLastFrontCull, 0 for none.
    struct GroupCullFacts
    {
        S32  mCurrentFrame   = 0;
        S32  mLastFrontCull  = 0;
        S32  mGroupSeenFrame = 0;
        U32  mPeriod         = 0;     // the partition's update period, mLODPeriod
        bool mNeedsUpdate    = false; // the group's frame to be asked again (LLOcclusionCullingGroup::needsUpdate)
        bool mHoldsCamera    = false; // its box holds the camera, so no query will ask it (doOcclusion)
        bool mAnswerAwaited  = false; // a query is out, or one was asked this frame
        bool mOccluded       = false; // its last answer found it hidden
        bool mRetestDue      = false; // no query is out and its last is a period old (isOcclusionRetestDue)
    };
    struct GroupCullAction
    {
        bool mAsk     = false; // ask an occlusion query (addOccluders)
        bool mVisible = false; // add it to the region's visible groups, whose cached objects are then made
    };
    // A group is asked when it is new to the view (not seen at the last front cull), on its needsUpdate() frame, and
    // while it is hidden and due a retest: a retest the query pool refused, its target held, leaves a group seen and
    // still hidden, and while the camera rests half the groups' needsUpdate() frames never fall on a front cull. It
    // is added, and its objects made, where it was seen at the last front cull too, is not hidden and awaits no answer,
    // and at once where its box holds the camera. The last front cull counts only within a period of this one.
    static GroupCullAction cullGroup(const GroupCullFacts& facts);

    // Whether the front cull passes over a group its last answer found hidden, on a frame that is not its needsUpdate()
    // frame: until its retest is due, unless its box holds the camera, which can step into a box a query found hidden
    // from outside.
    static bool culledAsHidden(bool retest_due, bool holds_camera) { return !retest_due && !holds_camera; }

private:
    void selectBackObjects(LLCamera &camera, F32 projection_area_cutoff, bool use_occlusion); //select objects behind camera.

public:
    static bool sNeedsOcclusionCheck;

private:
    bool  mFrontCull; //the view frustum cull if set, otherwise is back sphere cull.
    U32   mCullHistory;
    U32   mCulledTime[LLViewerCamera::NUM_CAMERAS];
    std::set<LLVOCacheGroup*> mOccludedGroups;

    S32   mBackSlectionEnabled; //enable to select back objects if > 0.
    U32   mIdleHash;
    U32   mLastFrontCull; // the frame of the last front cull, 0 before the first
};

//
//Note: LLVOCache is not thread-safe
//
class LLVOCache : public LLParamSingleton<LLVOCache>
{
    LLSINGLETON(LLVOCache, bool read_only);
    ~LLVOCache() ;

private:
    struct HeaderEntryInfo
    {
        HeaderEntryInfo() : mHandle(0), mIndex(0), mTime(0) {}
        U64 mHandle;
        S32 mIndex;
        U32 mTime ;
    };

    struct HeaderMetaInfo
    {
        HeaderMetaInfo() : mVersion(0), mAddressSize(0) {}

        U32 mVersion;
        U32 mAddressSize;
    };

    struct header_entry_less
    {
        bool operator()(const HeaderEntryInfo* lhs, const HeaderEntryInfo* rhs) const
        {
            if(lhs->mTime == rhs->mTime)
            {
                return lhs < rhs ;
            }

            return lhs->mTime < rhs->mTime ; // older entry in front of queue (set)
        }
    };
    typedef std::set<HeaderEntryInfo*, header_entry_less> header_entry_queue_t;
    typedef std::map<U64, HeaderEntryInfo*> handle_entry_map_t;

public:
    // We need this init to be separate from constructor, since we might construct cache, purge it, then init.
    void initCache(ELLPath location, U32 size, U32 cache_version);
    void removeCache(ELLPath location, bool started = false) ;

    bool readFromCache(U64 handle, const LLUUID& id, LLVOCacheEntry::vocache_entry_map_t& cache_entry_map) ;
    // Neither read touches the region or the object list; each reports whether
    // the file was usable and leaves removal to the caller.
    bool readGenericExtrasFromCache(U64 handle, const LLUUID& id, LLVOCacheEntry::vocache_gltf_overrides_map_t& cache_extras_entry_map, const LLVOCacheEntry::vocache_entry_map_t& cache_entry_map);

    void writeToCache(U64 handle, const LLUUID& id, const LLVOCacheEntry::vocache_entry_map_t& cache_entry_map, bool dirty_cache, bool removal_enabled);
    // Writes an override only beside the entry writeToCache writes for its object, given the same map and removal.
    void writeGenericExtrasToCache(U64 handle, const LLUUID& id, const LLVOCacheEntry::vocache_gltf_overrides_map_t& cache_extras_entry_map,
                                   const LLVOCacheEntry::vocache_entry_map_t& cache_entry_map, bool dirty_cache, bool removal_enabled);
    void removeEntry(U64 handle) ;
    // Removes a region's entry as removeEntry does, once removeRequestedEntries runs: the removal lets go of the
    // region's objects in memory, one of which an object update asking for it is read from.
    void requestRemoval(U64 handle);
    bool isRemovalRequested(U64 handle) const;
    void removeRequestedEntries();
    void removeGenericExtrasForHandle(U64 handle);

    U32 getCacheEntries() { return mNumEntries; }
    U32 getCacheEntriesMax() { return mCacheSize; }

private:
    void setDirNames(ELLPath location);
    // determine the cache filename for the region from the region handle
    std::filesystem::path getObjectCacheFilename(U64 handle);
    std::filesystem::path getObjectCacheExtrasFilename(U64 handle);
    void removeFromCache(HeaderEntryInfo* entry);
    void readCacheHeader();
    void writeCacheHeader();
    void clearCacheInMemory();
    void removeCache() ;
    void removeEntry(HeaderEntryInfo* entry) ;
    void purgeEntries(U32 size);
    bool updateEntry(const HeaderEntryInfo* entry);
    S32 firstFreeSlot() const;

private:
    bool                 mEnabled;
    bool                 mInitialized ;
    bool                 mReadOnly ;
    HeaderMetaInfo       mMetaInfo;
    U32                  mCacheSize;
    U32                  mNumEntries;
    std::filesystem::path          mHeaderFilePath;
    std::filesystem::path          mObjectCacheDirPath;
    header_entry_queue_t mHeaderEntryQueue;
    handle_entry_map_t   mHandleEntryMap;
    std::vector<U64>     mRequestedRemovals;
};

#endif

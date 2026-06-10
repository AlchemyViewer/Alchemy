/**
 * @file alobjectproperties.h
 * @brief Shared per-object metric record for the Scene Explorer, Inspect and detail views
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */
#ifndef AL_OBJECTPROPERTIES_H
#define AL_OBJECTPROPERTIES_H

#include <deque>

#include <boost/signals2.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "llsingleton.h"
#include "lluuid.h"
#include "v3math.h"
#include "v3dmath.h"

class LLMessageSystem;
class LLViewerObject;

// ============================================================================
// ALObjectProperties - a flat, copyable snapshot of an object's properties.
//
// Local (geometry / flag / cost) fields are filled synchronously from a live
// LLViewerObject via fillFromObject(). Server fields (name/owner/creator/desc/
// dates) arrive asynchronously and are merged by the owning view.
// ============================================================================

namespace ALObjectProperties
{
    // Geometry kind, used to pick the row icon.
    enum EGeom : U8
    {
        GEOM_PRIM = 0,
        GEOM_MESH,
        GEOM_SCULPT,
        GEOM_AVATAR,
        GEOM_UNKNOWN
    };

    // Feature flags. Independent of EGeom (an object can be e.g. a scripted,
    // glowing, light-emitting mesh). Used by the Scene Explorer filter.
    enum EFlag : U32
    {
        FLAG_NONE             = 0,
        FLAG_SCRIPTED         = 1 << 0,
        FLAG_PHYSICS          = 1 << 1,
        FLAG_PHANTOM          = 1 << 2,
        FLAG_TEMPORARY        = 1 << 3,
        FLAG_FLEXIBLE         = 1 << 4,
        FLAG_LIGHT            = 1 << 5,
        FLAG_SPOTLIGHT        = 1 << 6,   // light with a projector cone
        FLAG_PROJECTOR        = 1 << 7,   // has a projection texture
        FLAG_GLOW             = 1 << 8,
        FLAG_FULLBRIGHT       = 1 << 9,
        FLAG_MEDIA            = 1 << 10,
        FLAG_PARTICLES        = 1 << 11,
        FLAG_REFLECTION_PROBE = 1 << 12,
        FLAG_ANIMATED         = 1 << 13,  // animated (animesh) object
        FLAG_ATTACHMENT       = 1 << 14,
        FLAG_PBR_MATERIAL     = 1 << 15,  // has a GLTF render material on any face
        FLAG_ALPHA            = 1 << 16,  // any face is alpha-blended / transparent
        FLAG_AVATAR           = 1 << 17
    };

    struct Record
    {
        // Identity
        LLUUID      mId;
        U32         mLocalId        = 0;
        U64         mRegionHandle   = 0;

        // Spatial (local, live)
        LLVector3   mPosRegion;
        LLVector3d  mPosGlobal;
        LLVector3   mScale;
        F32         mDistance       = 0.f;

        // Structure / geometry (local, live)
        EGeom       mGeom           = GEOM_UNKNOWN;
        U32         mFlags          = FLAG_NONE;
        S32         mPrimCount      = 1;   // linkset prim count for a root
        S32         mNumFaces       = 0;
        U32         mNumVertices    = 0;
        U32         mNumTriangles   = 0;

        // Cost (local cached; populates over time)
        F32         mLandImpact     = 0.f; // linkset cost for a root object
        F32         mObjectCost     = 0.f;
        F32         mPhysicsCost    = 0.f;
        F32         mRenderCost     = 0.f; // "ARC"
        F32         mStreamingCost  = 0.f;

        // Server props (async; merged by the view)
        bool        mPropsValid     = false;
        std::string mName;
        std::string mDescription;
        LLUUID      mOwnerId;
        LLUUID      mGroupId;
        LLUUID      mCreatorId;
        bool        mGroupOwned     = false;
        time_t      mCreationDate   = 0;

        bool hasFlag(EFlag f) const { return (mFlags & f) != 0; }
    };

    // Fill the local (non-server) fields of @rec from a live object. The
    // identity and server fields are left untouched so a cached record keeps
    // its async data across refreshes.
    void fillFromObject(Record& rec, LLViewerObject* obj);

    // Convenience: build a fresh record (identity + local fields) for @obj.
    Record fromObject(LLViewerObject* obj);

    // A compact human-readable flag list for a row suffix / tooltip, e.g.
    // "scripted, light, glow". Empty when no notable flags set.
    std::string flagsToString(U32 flags);

    // UI image name for a record's geometry kind.
    const std::string& iconName(const Record& rec);
}

// ============================================================================
// ALObjectPropertiesCache
//
// Session-lifetime cache of server-provided object properties (name, owner,
// group, creator, description, creation date), keyed by object UUID. Populated
// by the ObjectProperties / ObjectPropertiesFamily message handlers, which are
// registered in llstartup.cpp ahead of LLSelectMgr's and forward to it so all
// existing selection behaviour is preserved. Consumers (Scene Explorer, detail
// pane, ...) read from here and subscribe for change notifications instead of
// having to be in a selection.
//
// The cache deliberately PERSISTS across region crossings: entries are keyed
// by globally-unique object UUID, so crossing a border doesn't stale them —
// and wiping would make every border hop re-probe the whole region. Growth is
// bounded by oldest-first eviction instead (insertion order roughly equals
// region visit order, so stale regions age out first).
// ============================================================================
class ALObjectPropertiesCache : public LLSingleton<ALObjectPropertiesCache>
{
    LLSINGLETON(ALObjectPropertiesCache);
    ~ALObjectPropertiesCache();
public:
    struct ServerProps
    {
        std::string mName;
        std::string mDescription;
        LLUUID      mOwnerId;
        LLUUID      mGroupId;
        LLUUID      mCreatorId;
        bool        mGroupOwned   = false;
        time_t      mCreationDate = 0;
        bool        mHasFullData  = false; // creator/date came from full ObjectProperties
    };

    // Message handlers (registered in llstartup.cpp). Each caches the relevant
    // fields, notifies listeners, then forwards to the matching LLSelectMgr
    // handler so normal selection / inspect / build behaviour is unaffected.
    static void processObjectProperties(LLMessageSystem* msg, void** user_data);
    static void processObjectPropertiesFamily(LLMessageSystem* msg, void** user_data);

    // Returns the cached entry for @id, or nullptr if nothing received yet.
    const ServerProps* get(const LLUUID& id) const;

    typedef boost::signals2::signal<void(const LLUUID&)> change_signal_t;
    boost::signals2::connection setChangeCallback(const change_signal_t::slot_type& cb)
    {
        return mChangeSignal.connect(cb);
    }

    // In-flight request tracking. The Scene Explorer marks each object id it
    // probes via bulk select/deselect; the id is retired when the full
    // ObjectProperties reply lands (or explicitly, when the requester decides
    // the reply was lost and re-requests). LLSelectMgr consults this to avoid
    // warning about reply blocks that legitimately match no select node —
    // including replies that arrive after the requesting floater has closed.
    void markPending(const LLUUID& id) { mPendingRequests.insert(id); }
    void clearPending(const LLUUID& id) { mPendingRequests.erase(id); }
    bool isPending(const LLUUID& id) const { return mPendingRequests.count(id) != 0; }
    static bool isExpectedReply(const LLUUID& id)
    {
        return instanceExists() && instance().isPending(id);
    }

    // Drop everything (explicit refresh only — see the class comment for why
    // region crossings must NOT clear).
    void clear()
    {
        mCache.clear();
        mCacheOrder.clear();
        mPendingRequests.clear();
    }

private:
    // Bounded growth: once past the cap, evict oldest insertions first. The
    // cap comfortably exceeds a maximal (~60k object) region so a single
    // region never churns against it.
    static constexpr size_t MAX_CACHE_ENTRIES = 100000;
    ServerProps& lookupOrCreate(const LLUUID& id);
    void trimCache();

    boost::unordered_map<LLUUID, ServerProps> mCache;
    std::deque<LLUUID> mCacheOrder;     // insertion order, for eviction
    boost::unordered_set<LLUUID> mPendingRequests;
    change_signal_t mChangeSignal;
};

#endif // AL_OBJECTPROPERTIES_H

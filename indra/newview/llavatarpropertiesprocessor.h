/**
 * @file llavatarpropertiesprocessor.h
 * @brief LLAvatarPropertiesProcessor class description
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

#ifndef LL_LLAVATARPROPERTIESPROCESSOR_H
#define LL_LLAVATARPROPERTIESPROCESSOR_H

#include "lluuid.h"
#include "llsingleton.h"
#include "v3dmath.h"    // LLVector3d
#include <list>
#include <map>

// For Flags in AvatarPropertiesReply
constexpr U32 AVATAR_ALLOW_PUBLISH  = 0x1 << 0; // whether profile is externally visible or not
constexpr U32 AVATAR_MATURE_PUBLISH = 0x1 << 1; // profile is "mature"
constexpr U32 AVATAR_IDENTIFIED     = 0x1 << 2; // whether avatar has provided payment info
constexpr U32 AVATAR_TRANSACTED     = 0x1 << 3; // whether avatar has actively used payment info
constexpr U32 AVATAR_ONLINE         = 0x1 << 4; // the online status of this avatar, if known.
constexpr U32 AVATAR_AGEVERIFIED    = 0x1 << 5; // whether avatar has been age-verified

/*
*TODO Vadim: This needs some refactoring:
- Remove EAvatarProcessorType in favor of separate observers, derived from a common parent (to get rid of void*).
*/

class LLMessageSystem;

enum EAvatarProcessorType
{
    APT_PROPERTIES_LEGACY, // APT_PROPERTIES via udp request (Truncates data!!!)
    APT_PROPERTIES,        // APT_PROPERTIES via http request
    APT_NOTES,
    APT_GROUPS,
    APT_PICKS,
    APT_PICK_INFO,
    APT_TEXTURES,
    APT_CLASSIFIEDS,
    APT_CLASSIFIED_INFO,
    APT_INTERESTS_INFO  // legacy support. don't be lazy and keep this. :O)
};

// legacy data is supposed to match AvatarPropertiesReply,
// but it is obsolete, fields like about_text will truncate
// data, if you need them, use AgenProfile cap.
// Todo: remove it once once icon ids get moved elsewhere,
// since AgentProfile is too large for bulk icon requests
struct LLAvatarLegacyData
{
    LLUUID      agent_id;
    LLUUID      avatar_id; //target id
    LLUUID      image_id;
    LLUUID      fl_image_id;
    LLUUID      partner_id;
    std::string about_text;
    std::string fl_about_text;
    LLDate      born_on;
    std::string profile_url;
    U8          caption_index;
    std::string caption_text;
    std::string customer_type;
    U32         flags;
};

// Don't belete my sexy InterestsData.
struct LLLegacyInterestsData
{
    LLUUID      agent_id;
    LLUUID      avatar_id;  // target id
    U32         want_to_mask;
    std::string want_to_text;
    U32         skills_mask;
    std::string skills_text;
    std::string languages_text;
};

struct LLAvatarData
{
    LLUUID      agent_id;
    LLUUID      avatar_id; //target id
    LLUUID      image_id;
    LLUUID      fl_image_id;
    LLUUID      partner_id;
    std::string about_text;
    std::string fl_about_text;
    LLDate      born_on;
    std::string profile_url;
    U8          caption_index;
    std::string caption_text;
    std::string customer_type;
    U32         flags;
    bool        hide_age;
    std::string notes;

    struct LLGroupData;
    typedef std::list<LLGroupData> group_list_t;
    group_list_t group_list;

    typedef std::pair<LLUUID, std::string> pick_data_t;
    typedef std::list< pick_data_t> picks_list_t;
    picks_list_t picks_list;
    BOOL        allow_publish;
    LLAvatarData() = default;
    LLAvatarData(const LLAvatarLegacyData& legacy_data)
    {
        agent_id = legacy_data.agent_id;
        avatar_id = legacy_data.avatar_id;
        image_id = legacy_data.image_id;
        fl_image_id = legacy_data.fl_image_id;
        partner_id = legacy_data.partner_id;
        about_text = legacy_data.about_text;
        fl_about_text = legacy_data.fl_about_text;
        born_on = legacy_data.born_on;
        profile_url = legacy_data.profile_url;
        caption_index = legacy_data.caption_index;
        caption_text = legacy_data.caption_text;
        customer_type = legacy_data.customer_type;
        flags = legacy_data.flags;
    }
};

struct LLAvatarData::LLGroupData
{
    U64 group_powers;
    BOOL accept_notices;
    std::string group_title;
    LLUUID group_id;
    std::string group_name;
    LLUUID group_insignia_id;
};

struct LLPickData
{
    LLUUID agent_id;
    LLUUID pick_id;
    LLUUID creator_id;
    BOOL top_pick;
    LLUUID parcel_id;
    std::string name;
    std::string desc;
    LLUUID snapshot_id;
    LLVector3d pos_global;
    S32 sort_order;
    BOOL enabled;

    //used only in read requests
    std::string user_name;
    std::string original_name;
    std::string sim_name;

    //used only in write (update) requests
    LLUUID session_id;
};

struct LLAvatarPicks
{
    LLUUID agent_id;
    LLUUID target_id; //target id

    typedef std::pair<LLUUID,std::string> pick_data_t;
    typedef std::list< pick_data_t> picks_list_t;
    picks_list_t picks_list;
};

struct LLAvatarNotes
{
    LLUUID agent_id;
    LLUUID target_id; //target id
    std::string notes;
};

struct LLAvatarGroups
{
    LLUUID agent_id;
    LLUUID avatar_id; //target id
    BOOL list_in_profile;

    struct LLGroupData;
    typedef std::list<LLGroupData> group_list_t;

    group_list_t group_list;

    struct LLGroupData
    {
        U64 group_powers;
        BOOL accept_notices;
        std::string group_title;
        LLUUID group_id;
        std::string group_name;
        LLUUID group_insignia_id;
    };
};

struct LLAvatarClassifieds
{
    LLUUID agent_id;
    LLUUID target_id;

    struct classified_data;
    typedef std::list<classified_data> classifieds_list_t;

    classifieds_list_t classifieds_list;

    struct classified_data
    {
        LLUUID classified_id;
        std::string name;
    };
};

struct LLAvatarClassifiedInfo
{
    LLUUID agent_id;
    LLUUID classified_id;
    LLUUID creator_id;
    U32 creation_date;
    U32 expiration_date;
    U32 category;
    std::string name;
    std::string description;
    LLUUID parcel_id;
    U32 parent_estate;
    LLUUID snapshot_id;
    std::string sim_name;
    LLVector3d pos_global;
    std::string parcel_name;
    U8 flags;
    S32 price_for_listing;
};

class LLAvatarPropertiesObserver
{
public:
    virtual      ~LLAvatarPropertiesObserver() = default;
    virtual void processProperties(void* data, EAvatarProcessorType type) = 0;
};

class LLAvatarPropertiesProcessor
    : public LLSingleton<LLAvatarPropertiesProcessor>
{
    LLSINGLETON(LLAvatarPropertiesProcessor);
    virtual ~LLAvatarPropertiesProcessor();

public:
    void addObserver(const LLUUID& avatar_id, LLAvatarPropertiesObserver* observer);

    void removeObserver(const LLUUID& avatar_id, LLAvatarPropertiesObserver* observer);

    // Request various types of avatar data.  Duplicate requests will be
    // suppressed while waiting for a response from the network.
    void sendAvatarPropertiesRequest(const LLUUID& avatar_id);
    void sendAvatarLegacyPropertiesRequest(const LLUUID& avatar_id);
    void sendAvatarLegacyPicksRequest(const LLUUID& avatar_id);
    void sendAvatarLegacyNotesRequest(const LLUUID& avatar_id);
    void sendAvatarLegacyGroupsRequest(const LLUUID& avatar_id);
    void sendAvatarTexturesRequest(const LLUUID& avatar_id);
    void sendAvatarClassifiedsRequest(const LLUUID& avatar_id);

    // Duplicate pick info requests are not suppressed.
    static void sendPickInfoRequest(const LLUUID& creator_id, const LLUUID& pick_id);

    static void sendClassifiedInfoRequest(const LLUUID& classified_id);

    static void sendPickInfoUpdate(const LLPickData* new_pick);

    static void sendClassifiedInfoUpdate(const LLAvatarClassifiedInfo* c_data);

    static void sendInterestsInfoUpdate(const LLLegacyInterestsData* interests_data);

    static void sendFriendRights(const LLUUID& avatar_id, S32 rights);

    static void sendPickDelete(const LLUUID& pick_id);

    static void sendClassifiedDelete(const LLUUID& classified_id);

    bool isHideAgeSupportedByServer() const { return mIsHideAgeSupportedByServer; }

    // Returns translated, human-readable string for account type, such
    // as "Resident" or "Linden Employee".  Used for profiles, inspectors.
    static std::string accountType(const LLAvatarData* avatar_data);

    // Returns translated, human-readable string for payment info, such
    // as "Payment Info on File" or "Payment Info Used".
    // Used for profiles, inspectors.
    static std::string paymentInfo(const LLAvatarData* avatar_data);

    static bool hasPaymentInfoOnFile(const LLAvatarData* avatar_data);

    static void requestAvatarPropertiesCoro(std::string cap_url, LLUUID avatar_id, EAvatarProcessorType type);

    // Processing of UDP variant of properties, truncates certain fields!
    static void processAvatarLegacyPropertiesReply(LLMessageSystem* msg, void**);

    static void processAvatarInterestsReply(LLMessageSystem* msg, void**);

    static void processAvatarClassifiedsReply(LLMessageSystem* msg, void**);

    static void processClassifiedInfoReply(LLMessageSystem* msg, void**);

    static void processAvatarGroupsReply(LLMessageSystem* msg, void**);

    static void processAvatarNotesReply(LLMessageSystem* msg, void**);

    static void processAvatarPicksReply(LLMessageSystem* msg, void**);

    static void processPickInfoReply(LLMessageSystem* msg, void**);

protected:

    void sendRequest(const LLUUID& avatar_id, EAvatarProcessorType type, const std::string &method);
    void sendGenericRequest(const LLUUID& avatar_id, EAvatarProcessorType type, const std::string &method);
    void sendAvatarPropertiesRequestMessage(const LLUUID& avatar_id);
    void initAgentProfileCapRequest(const LLUUID& avatar_id, const std::string& cap_url, EAvatarProcessorType type);

    void notifyObservers(const LLUUID& id,void* data, EAvatarProcessorType type) const;

    // Is there a pending, not timed out, request for this avatar's data?
    // Use this to suppress duplicate requests for data when a request is
    // pending.
    bool isPendingRequest(const LLUUID& avatar_id, EAvatarProcessorType type);

    // Call this when a request has been sent
    void addPendingRequest(const LLUUID& avatar_id, EAvatarProcessorType type);

    // Call this when the reply to the request is received
    void removePendingRequest(const LLUUID& avatar_id, EAvatarProcessorType type);

    typedef void* (*processor_method_t)(LLMessageSystem*);

protected:

    typedef std::multimap<LLUUID, LLAvatarPropertiesObserver*> observer_multimap_t;

    observer_multimap_t mObservers;

    // Keep track of pending requests for data by avatar id and type.
    // Maintain a timestamp for each request so a request that receives no reply
    // does not block future requests forever.
    // Map avatar_id+request_type -> U32 timestamp in seconds
    typedef std::map< std::pair<LLUUID, EAvatarProcessorType>, U32> timestamp_map_t;
    timestamp_map_t mRequestTimestamps;

    // Is returned by isHideAgeSupportedByServer()
    bool mIsHideAgeSupportedByServer { false };
};

#endif  // LL_LLAVATARPROPERTIESPROCESSOR_H

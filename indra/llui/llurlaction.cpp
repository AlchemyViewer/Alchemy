/**
 * @file llurlaction.cpp
 * @author Martin Reddy
 * @brief A set of actions that can performed on Urls
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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
#include "linden_common.h"

#include "llurlaction.h"
#include "llview.h"
#include "llwindow.h"
#include "llurlregistry.h"
#include "v3dmath.h"


// global state for the callback functions
LLUrlAction::url_callback_t         LLUrlAction::sOpenURLCallback;
LLUrlAction::url_callback_t         LLUrlAction::sOpenURLInternalCallback;
LLUrlAction::url_callback_t         LLUrlAction::sOpenURLExternalCallback;
LLUrlAction::execute_url_callback_t LLUrlAction::sExecuteSLURLCallback;
LLUrlAction::id_query_t             LLUrlAction::sIsFriendCallback;
LLUrlAction::named_query_t          LLUrlAction::sIsObjectBlockedCallback;
LLUrlAction::id_query_t             LLUrlAction::sIsObjectReachableCallback;


void LLUrlAction::setOpenURLCallback(url_callback_t cb)
{
    sOpenURLCallback = cb;
}

void LLUrlAction::setOpenURLInternalCallback(url_callback_t cb)
{
    sOpenURLInternalCallback = cb;
}

void LLUrlAction::setOpenURLExternalCallback(url_callback_t cb)
{
    sOpenURLExternalCallback = cb;
}

void LLUrlAction::setExecuteSLURLCallback(execute_url_callback_t cb)
{
    sExecuteSLURLCallback = cb;
}

void LLUrlAction::setIsFriendCallback(id_query_t cb)
{
    sIsFriendCallback = std::move(cb);
}

void LLUrlAction::setIsObjectBlockedCallback(named_query_t cb)
{
    sIsObjectBlockedCallback = std::move(cb);
}

void LLUrlAction::setIsObjectReachableCallback(id_query_t cb)
{
    sIsObjectReachableCallback = std::move(cb);
}

std::optional<bool> LLUrlAction::isFriend(const std::string& url)
{
    if (!sIsFriendCallback)
    {
        return std::nullopt;
    }
    return sIsFriendCallback(LLUUID(getUserID(url)));
}

std::optional<bool> LLUrlAction::isObjectBlocked(const std::string& url)
{
    if (!sIsObjectBlockedCallback)
    {
        return std::nullopt;
    }
    return sIsObjectBlockedCallback(LLUUID(getObjectId(url)), getObjectName(url));
}

std::optional<bool> LLUrlAction::isObjectReachable(const std::string& url)
{
    if (!sIsObjectReachableCallback)
    {
        return std::nullopt;
    }
    return sIsObjectReachableCallback(LLUUID(getObjectId(url)));
}

void LLUrlAction::adjustMenu(LLView* menu, const std::string& url, bool friends, bool blocked, bool reachable)
{
    if (!menu)
    {
        return;
    }
    if (friends)
    {
        LLView* add    = menu->findChild<LLView>("add_friend");
        LLView* remove = menu->findChild<LLView>("remove_friend");
        if (add && remove)
        {
            if (const std::optional<bool> is_friend = isFriend(url))
            {
                add->setEnabled(!*is_friend);
                remove->setEnabled(*is_friend);
            }
        }
    }
    if (blocked)
    {
        LLView* block   = menu->findChild<LLView>("block_object");
        LLView* unblock = menu->findChild<LLView>("unblock_object");
        if (block && unblock)
        {
            if (const std::optional<bool> is_blocked = isObjectBlocked(url))
            {
                block->setVisible(!*is_blocked);
                unblock->setVisible(*is_blocked);
            }
        }
    }
    if (reachable)
    {
        if (LLView* zoom = menu->findChild<LLView>("zoom_in"))
        {
            if (const std::optional<bool> is_reachable = isObjectReachable(url))
            {
                zoom->setEnabled(*is_reachable);
            }
        }
    }
}

void LLUrlAction::openURL(std::string url)
{
    if (sOpenURLCallback)
    {
        sOpenURLCallback(url);
    }
}

void LLUrlAction::openURLInternal(std::string url)
{
    if (sOpenURLInternalCallback)
    {
        sOpenURLInternalCallback(url);
    }
}

void LLUrlAction::openURLExternal(std::string url)
{
    if (sOpenURLExternalCallback)
    {
        sOpenURLExternalCallback(url);
    }
}

bool LLUrlAction::executeSLURL(std::string url, bool trusted_content)
{
    if (sExecuteSLURLCallback)
    {
        return sExecuteSLURLCallback(url, trusted_content);
    }
    return false;
}

void LLUrlAction::clickAction(std::string url, bool trusted_content)
{
    // Try to handle as SLURL first, then http Url
    if ( (sExecuteSLURLCallback) && !sExecuteSLURLCallback(url, trusted_content) )
    {
        if (sOpenURLCallback)
        {
            sOpenURLCallback(url);
        }
    }
}

void LLUrlAction::teleportToLocation(std::string url)
{
    LLUrlMatch match;
    if (LLUrlRegistry::instance().findUrl(url, match))
    {
        if (! match.getLocation().empty())
        {
            executeSLURL("secondlife:///app/teleport/" + match.getLocation());
        }
    }
}

void LLUrlAction::zoomInObject(std::string url)
{
    LLUrlMatch match;
    std::string object_id = getObjectId(url);
    if (LLUUID::validate(object_id) && LLUrlRegistry::instance().findUrl(url, match))
    {
        executeSLURL("secondlife:///app/object/" + object_id + "/zoomin/" + match.getLocation());
    }
}

void LLUrlAction::showLocationOnMap(std::string url)
{
    LLUrlMatch match;
    if (LLUrlRegistry::instance().findUrl(url, match))
    {
        if (! match.getLocation().empty())
        {
            executeSLURL("secondlife:///app/worldmap/" + match.getLocation());
        }
    }
}

void LLUrlAction::showParcelOnMap(std::string url)
{
    LLSD path_array = LLURI(url).pathArray();
    auto path_parts = path_array.size();

    if (path_parts < 3) // no parcel id
    {
        LL_WARNS() << "Global coordinates are missing in url: [" << url << "]" << LL_ENDL;
        return;
    }

    LLVector3d parcel_pos = LLUrlEntryParcel::getParcelPos(LLUUID(LLURI::unescape(path_array[2])));
    std::ostringstream pos;
    pos << parcel_pos.mdV[VX] << '/' << parcel_pos.mdV[VY] << '/' << parcel_pos.mdV[VZ];
    executeSLURL("secondlife:///app/worldmap_global/" + pos.str());
}

void LLUrlAction::copyURLToClipboard(std::string url)
{
    LLView::getWindow()->copyTextToClipboard(url);
}

void LLUrlAction::copyUUIDToClipboard(std::string url)
{
    std::string id_str = getUserID(url);
    if (LLUUID::validate(id_str))
    {
        LLView::getWindow()->copyTextToClipboard(id_str);
    }
}

void LLUrlAction::copyLabelToClipboard(std::string url)
{
    LLUrlMatch match;
    if (LLUrlRegistry::instance().findUrl(url, match))
    {
        LLView::getWindow()->copyTextToClipboard(match.getLabel());
    }
}

std::string LLUrlAction::getURLLabel(std::string url)
{
    LLUrlMatch match;
    if (LLUrlRegistry::instance().findUrl(url, match))
    {
       return match.getLabel();
    }
    return "";
}

void LLUrlAction::showProfile(std::string url)
{
    // Get id from 'secondlife:///app/{cmd}/{id}/{action}'
    // and show its profile
    LLURI uri(url);
    LLSD path_array = uri.pathArray();
    if (path_array.size() == 4)
    {
        std::string id_str = path_array.get(2).asString();
        if (LLUUID::validate(id_str))
        {
            std::string cmd_str = path_array.get(1).asString();
            executeSLURL("secondlife:///app/" + cmd_str + "/" + id_str + "/about");
        }
    }
}

std::string LLUrlAction::getUserID(std::string url)
{
    LLURI uri(url);
    LLSD path_array = uri.pathArray();
    std::string id_str;
    if (path_array.size() == 4)
    {
        id_str = path_array.get(2).asString();
    }
    return id_str;
}

std::string LLUrlAction::getObjectId(std::string url)
{
    LLURI uri(url);
    LLSD path_array = uri.pathArray();
    std::string id_str;
    if (path_array.size() >= 3)
    {
        id_str = path_array.get(2).asString();
    }
    return id_str;
}

std::string LLUrlAction::getObjectName(std::string url)
{
    LLURI uri(url);
    LLSD query_map = uri.queryMap();
    std::string name;
    if (query_map.has("name"))
    {
        name = query_map["name"].asString();
    }
    return name;
}

void LLUrlAction::sendIM(std::string url)
{
    std::string id_str = getUserID(url);
    if (LLUUID::validate(id_str))
    {
        executeSLURL("secondlife:///app/agent/" + id_str + "/im");
    }
}

void LLUrlAction::addFriend(std::string url)
{
    std::string id_str = getUserID(url);
    if (LLUUID::validate(id_str))
    {
        executeSLURL("secondlife:///app/agent/" + id_str + "/requestfriend");
    }
}

void LLUrlAction::removeFriend(std::string url)
{
    std::string id_str = getUserID(url);
    if (LLUUID::validate(id_str))
    {
        executeSLURL("secondlife:///app/agent/" + id_str + "/removefriend");
    }
}

void LLUrlAction::reportAbuse(std::string url)
{
    std::string id_str = getUserID(url);
    if (LLUUID::validate(id_str))
    {
        executeSLURL("secondlife:///app/agent/" + id_str + "/reportAbuse");
    }
}

void LLUrlAction::reportAbuseObj(std::string url)
{
    std::string object_id = getObjectId(url);
    if (LLUUID::validate(object_id))
    {
        executeSLURL("secondlife:///app/object/" + object_id + "/reportAbuse");
    }
}

void LLUrlAction::blockObject(std::string url)
{
    std::string object_id = getObjectId(url);
    std::string object_name = getObjectName(url);
    if (LLUUID::validate(object_id))
    {
        executeSLURL("secondlife:///app/object/" + object_id + "/block/" + LLURI::escape(object_name));
    }
}

void LLUrlAction::unblockObject(std::string url)
{
    std::string object_id = getObjectId(url);
    std::string object_name = getObjectName(url);
    if (LLUUID::validate(object_id))
    {
        executeSLURL("secondlife:///app/object/" + object_id + "/unblock/" + LLURI::escape(object_name));
    }
}

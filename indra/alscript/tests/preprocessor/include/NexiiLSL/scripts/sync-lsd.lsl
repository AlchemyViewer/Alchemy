/*
    Synchronised Linkset Data
    
    This script synchronises specific linkset data keys across the region that match a prefix, per the config.
    
    On initialisation, rezzing or changing region it does a full reset/synchronisation with the full
    tracked dataset (per scopes) of the network and then simply listens for changes as they propagate across the region.
    
    Each object can be configured to only sync specific prefixes to optimise bandwidth and processing.
    
    If you want to persist data across reboots (e.g. if syncing data across temp attach / intermittent objects),
    make sure to establish a permanent server object that holds data.
*/

// SETTINGS BELOW CAN BE CUSTOMISED PER PROJECT ////////////////////////////////////////////////////

/// Security
// We use RSA signatures to verify secure communications
string privateKey = "-----REPLACE WITH RSA PRIVATE KEY-----";
string publicKey = "-----REPLACE WITH RSA PUBLIC KEY-----";
// We use a protected linkset data key to prevent spoofing due to extracted scripts
// You only need this when using this script in no-mod attachments, e.g. experience temp attach
string protectedName = "SYNC_LSD_PROTECTED";
string protectedKey = "-----REPLACE WITH SECRET-----";

/// Scope prefixes of linkset data keys to synchronise
list scopes = [
    "Test_",
    "Foo_",
    "Bar_"
];
// Corresponding listener channels for each scope above
list scopeChannels = [
    204505,
    204506,
    204507
];

// SETTINGS ABOVE CAN BE CUSTOMISED PER PROJECT ////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////

// Link Message Numbers
#define MESSAGE_SYNC_BOOT 1
#define MESSAGE_SYNC_READY 2

// Event Types Enumeration
#define EVENT_TYPE 63
#define EVENT_PING 1
#define EVENT_PONG 2
#define EVENT_SYNC_REQUEST 3
#define EVENT_SYNC_RESPONSE 4
#define EVENT_SYNC_COMPLETE 5
#define EVENT_DATA 6
#define EVENT_MESSAGE 7
// Event Bit flags
#define EVENT_FRAGMENT 64
#define EVENT_FINAL_FRAGMENT 128


list candidates = [/* key object, string scope */];
list pending = [/* integer action, string name */];
list verified = [/* key object */];
list buffer = [/* string name, string value */];

////////////////////////////////////////////////////////////////////////////////////////////////////

integer inScope(string name) {
    integer iterator = llGetListLength(scopes);
    while(iterator --> 0)
    {
        string prefix = llList2String(scopes, iterator);
        if(llSubStringIndex(name, prefix) == 0) return TRUE;
    }
    return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// From https://wiki.secondlife.com/wiki/LlStringToBase64
integer getStringBytes(string msg) {
    return (llStringLength((string)llParseString2List(llStringToBase64(msg), ["="], [])) * 3) >> 2;
}

sendData(key target, integer channel, integer eventType, string name, string value) {
    integer nameLength = llStringLength(name);
    integer available = 1023 - (3 + nameLength);
    integer total = getStringBytes(value);
    integer isASCII = (total == llStringLength(value)); // Prefer llStringLength as its fastest
    
    // Fragment the update across multiple messages
    if(total > available)
    {
        eventType = eventType | EVENT_FRAGMENT;
        while(value)
        {
            string fragment = "";
            integer width = 0;
            while(total > 0)
            {
                string part = llGetSubString(value, 0, 250);
                integer bytes;
                if(isASCII) bytes = llStringLength(part);
                else bytes = getStringBytes(part);
                
                if(width + bytes <= available)
                {
                    value = llDeleteSubString(value, 0, 250);
                    width += bytes;
                    total -= bytes;
                    fragment += part;
                }
                else jump breakFragment;
            }
            @breakFragment;
            
            if(value == "") eventType = eventType | EVENT_FINAL_FRAGMENT;
            
            string message = llChar(eventType) + llChar(LINKSETDATA_UPDATE + 1) + llChar(nameLength) + name + fragment;
            if(target) llRegionSayTo(target, channel, message);
            else llRegionSay(channel, message);
        }
    }
    
    // Direct message
    else
    {
        string message = llChar(eventType) + llChar(LINKSETDATA_UPDATE + 1) + llChar(nameLength) + name + value;
        if(target) llRegionSayTo(target, channel, message);
        else llRegionSay(channel, message);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialise by pinging all and get latest dataset from network
default
{
    state_entry()
    {
        if(llLinksetDataReadProtected(protectedName, protectedKey) == "")
        {
            llDetachFromAvatar();
            llDie();
            return;
        }
        
        llMessageLinked(LINK_SET, MESSAGE_SYNC_BOOT, "", "");
        
        // Reset scoped data, setup listeners and ping everyone
        integer iterator = llGetListLength(scopes);
        string signature = llSignRSA(privateKey, (string)llGetKey() + " " + llGetDate(), "sha512");
        integer signatureLength = llStringLength(signature);
        while(iterator --> 0)
        {
            string prefix = llList2String(scopes, iterator);
            llLinksetDataDeleteFound("^" + prefix, "");
            integer channel = llList2Integer(scopeChannels, iterator);
            llListen(channel, "", "", "");
            llRegionSay(channel, llChar(EVENT_PING) + llChar(signatureLength) + signature + prefix);
        }
        
        // Timeout from pongs
        llSetTimerEvent(0.5);
    }
    
    listen(integer channel, string name, key identifier, string message)
    {
        integer header = llOrd(message, 0);
        integer eventType = header & EVENT_TYPE;
        integer eventFlags = header & ~EVENT_TYPE;
        
        if(eventType == EVENT_PONG)
        {
            // Parse packet and verify RSA signature
            integer signatureLength = llOrd(message, 1);
            string signature = llGetSubString(message, 2, 1 + signatureLength);
            string expected = (string)identifier + " " + llGetDate();
            if(!llVerifyRSA(publicKey, expected, signature, "sha512")) return;
            
            // Don't add to verified list yet, check scope first
            
            // Refresh timeout
            llSetTimerEvent(FALSE);
            llSetTimerEvent(0.5);
            
            // Add to candidates if scope matches
            integer isVerified = (llListFindList(verified, [identifier]) != -1);
            list peerScope = llParseString2List(llGetSubString(message, 2 + signatureLength, -1), [llChar(1)], []);
            integer peerIterator = llGetListLength(peerScope);
            while(peerIterator --> 0)
            {
                string peerPrefix = llList2String(peerScope, peerIterator);
                
                // Check if each peer prefix matches any of our scopes
                integer ownIterator = llGetListLength(scopes);
                while(ownIterator --> 0)
                {
                    string ownPrefix = llList2String(scopes, ownIterator);
                    
                    // Phase 1: Check if peer prefix is wider, shorter or exact match
                    // Is peer prefix wider?
                    if(llSubStringIndex(peerPrefix, ownPrefix) != 0)
                    {
                        // Is peer prefix shorter?
                        if(llSubStringIndex(ownPrefix, peerPrefix) != 0)
                        {
                            // Peer prefix doesnt match our scope to track, so skip it
                            jump nextPeerPrefix;
                        }
                    }
                    
                    if(!isVerified)
                    {
                        // Mark as verified
                        integer iterator = llGetListLength(verified);
                        while(iterator --> 0)
                        {
                            if(llKey2Name(llList2Key(verified, iterator)) == "")
                                verified = llDeleteSubList(verified, iterator, iterator);
                        }
                        verified += identifier;
                        isVerified = TRUE;
                    }
                    
                    // Phase 2: Compare with candidates; Check if already exact match or is shorter, otherwise delete any wider prefixes
                    integer candidatesIterator = llGetListLength(candidates);
                    while(candidatesIterator > 0)
                    {
                        candidatesIterator -= 2;
                        string candidatePrefix = llList2String(candidates, candidatesIterator + 1);
                        
                        if(peerPrefix == candidatePrefix)
                            jump nextPeerPrefix; // Already have this candidate;
                        
                        // Is candidate shorter than peer?
                        else if(llSubStringIndex(peerPrefix, candidatePrefix) == 0)
                            jump nextPeerPrefix; // Skip adding peer as candidate as we already have a better match
                        
                        // Is candidate wider than peer?
                        else if(llSubStringIndex(candidatePrefix, peerPrefix) == 0)
                            // Delete candidate as peer is a better match
                            candidates = llDeleteSubList(candidates, candidatesIterator, candidatesIterator + 1);
                    }
                    
                    // Phase 3: Add peer as candidate
                    candidates += [identifier, peerPrefix];
                    
                    @nextPeerPrefix;
                }
                @nextPeer;
            }
        }
        
        else if(llListFindList(verified, [identifier]) == -1) return;
        
        
        // Sync event
        if(eventType == EVENT_SYNC_RESPONSE)
        {
            // integer action = llOrd(message, 1) - 1;
            integer nameLength = llOrd(message, 2);
            string name = llGetSubString(message, 3, 2 + nameLength);
            string value = llGetSubString(message, 3 + nameLength, -1);
            
            if(!inScope(name)) return;
            
            if(eventFlags & EVENT_FRAGMENT)
            {
                integer pointer = llListFindStrided(buffer, [name], 0, -1, 2);
                if(pointer != -1) value = llList2String(buffer, pointer + 1) + value;
                
                if(eventFlags & EVENT_FINAL_FRAGMENT)
                {
                    buffer = llDeleteSubList(buffer, pointer, pointer + 1);
                    llLinksetDataWrite(name, value);
                }
                else if(pointer == -1) buffer += [name, value];
                else buffer = llListReplaceList(buffer, [name, value], pointer, pointer + 1);
            }
            else llLinksetDataWrite(name, value);
        }
        
        else if(eventType == EVENT_SYNC_COMPLETE)
        {
            // If no more candidates, move to sync state
            if(candidates); else state sync;
            
            // Pull out the next candidate to sync
            key peer = llList2Key(candidates, 0);
            list peerScopes = [llList2String(candidates, 1)];
            candidates = llDeleteSubList(candidates, 0, 1);
            
            // Merge other scopes for same peer
            while(llList2Key(candidates, 0) == peer)
            {
                peerScopes += llList2String(candidates, 1);
                candidates = llDeleteSubList(candidates, 0, 1);
            }
            
            llRegionSayTo(peer, llList2Integer(scopeChannels, llListFindList(scopes, [llList2String(peerScopes, 0)])),
                llChar(EVENT_SYNC_REQUEST) + llDumpList2String(peerScopes, llChar(1))
            );
        }
    }
    
    timer()
    {
        llSetTimerEvent(FALSE);
        
        // If no candidates, move directly to sync state
        if(candidates); else state sync;
        
        // Sort candidates by key
        candidates = llListSort(candidates, 2, TRUE);
        
        // Pull out a candidate
        key peer = llList2Key(candidates, 0);
        list peerScopes = [llList2String(candidates, 1)];
        candidates = llDeleteSubList(candidates, 0, 1);
        
        // Merge other scopes for same peer
        while(llList2Key(candidates, 0) == peer)
        {
            peerScopes += llList2String(candidates, 1);
            candidates = llDeleteSubList(candidates, 0, 1);
        }
        
        llRegionSayTo(peer, llList2Integer(scopeChannels, llListFindList(scopes, [llList2String(peerScopes, 0)])),
            llChar(EVENT_SYNC_REQUEST) + llDumpList2String(peerScopes, llChar(1))
        );
    }
    
    state_exit()
    {
        llMessageLinked(LINK_SET, MESSAGE_SYNC_READY, "", "");
    }
}




////////////////////////////////////////////////////////////////////////////////////////////////////
// Idle away, synchronising data as needed
state sync
{
    on_rez(integer param) { llResetScript(); }
    changed(integer change) { if(change & CHANGED_REGION) llResetScript(); }
    state_entry()
    {
        integer iterator = llGetListLength(scopes);
        while(iterator --> 0) llListen(llList2Integer(scopeChannels, iterator), "", "", "");
    }
    
    listen(integer channel, string name, key identifier, string message)
    {
        integer header = llOrd(message, 0);
        integer eventType = header & EVENT_TYPE;
        integer eventFlags = header & ~EVENT_TYPE;
        
        if(eventType == EVENT_PING)
        {
            // Parse packet and verify RSA signature
            integer signatureLength = llOrd(message, 1);
            string signature = llGetSubString(message, 2, 1 + signatureLength);
            string expected = (string)identifier + " " + llGetDate();
            if(!llVerifyRSA(publicKey, expected, signature, "sha512")) return;
            
            // Check if peer prefix matches any of our scopes
            integer hasMatch = FALSE;
            list matchedScopes;
            string peerPrefix = llGetSubString(message, 2 + signatureLength, -1);
            integer iterator = llGetListLength(scopes);
            while(iterator --> 0)
            {
                string ownPrefix = llList2String(scopes, iterator);
                if(llSubStringIndex(peerPrefix, ownPrefix) == 0) matchedScopes += ownPrefix;
                else if(llSubStringIndex(ownPrefix, peerPrefix) == 0) matchedScopes += ownPrefix;
            }
            if(matchedScopes); else return;
            
            // Mark as verified
            iterator = llGetListLength(verified);
            while(iterator --> 0)
            {
                if(llKey2Name(llList2Key(verified, iterator)) == "")
                    verified = llDeleteSubList(verified, iterator, iterator);
            }
            if(llListFindList(verified, [identifier]) == -1) verified += identifier;
            
            // Respond with pong
            signature = llSignRSA(privateKey, (string)llGetKey() + " " + llGetDate(), "sha512");
            signatureLength = llStringLength(signature);
            llRegionSayTo(identifier, channel,
                llChar(EVENT_PONG) +
                llChar(signatureLength) +
                signature +
                llDumpList2String(matchedScopes, llChar(1))
            );
        }
        
        else if(llListFindList(verified, [identifier]) == -1) return;
        
        
        // Remote linkset data event
        if(eventType == EVENT_DATA)
        {
            integer action = llOrd(message, 1) - 1;
            
            if(action == LINKSETDATA_UPDATE)
            {
                integer nameLength = llOrd(message, 2);
                string name = llGetSubString(message, 3, 2 + nameLength);
                
                if(!inScope(name)) return;
                
                string value = llGetSubString(message, 3 + nameLength, -1);
                
                if(eventFlags & EVENT_FRAGMENT)
                {
                    integer pointer = llListFindStrided(buffer, [name], 0, -1, 2);
                    if(pointer != -1) value = llList2String(buffer, pointer + 1) + value;
                    
                    if(eventFlags & EVENT_FINAL_FRAGMENT)
                    {
                        buffer = llDeleteSubList(buffer, pointer, pointer + 1);
                        if(llLinksetDataWrite(name, value) == LINKSETDATA_OK) pending += [action, name];
                    }
                    else if(pointer == -1) buffer += [name, value];
                    else buffer = llListReplaceList(buffer, [name, value], pointer, pointer + 1);
                }
                else
                {
                    if(llLinksetDataWrite(name, value) == LINKSETDATA_OK) pending += [action, name];
                }
            }
            
            else if(action == LINKSETDATA_DELETE)
            {
                string name = llGetSubString(message, 2, -1);
                if(!inScope(name)) return;
                if(llLinksetDataDelete(name) == LINKSETDATA_OK) pending += [action, name];
            }
            
            else if(action == LINKSETDATA_MULTIDELETE)
            {
                integer prefixLength = llOrd(message, 2);
                string prefix = llGetSubString(message, 3, 2 + prefixLength);
                // if(!inScope(prefix)) return; -- This is complex as the source may be wider or narrower than our scopes but still have relevant data, hmm...
                // For now we cheat a bit because delete found can tell us how many were deleted
                // There is however danger for overlap between local and synced data if they have the same names but weren't intended to be synced
                
                // Escape regex special characters and prepare bucket as a regex pattern
                string bucket = llGetSubString(message, 3 + prefixLength, -1);
                bucket = llReplaceSubString(bucket, ".", "\\.", 0);
                bucket = llReplaceSubString(bucket, "^", "\\^", 0);
                bucket = llReplaceSubString(bucket, "$", "\\$", 0);
                bucket = llReplaceSubString(bucket, "*", "\\*", 0);
                bucket = llReplaceSubString(bucket, "+", "\\+", 0);
                bucket = llReplaceSubString(bucket, "?", "\\?", 0);
                bucket = llReplaceSubString(bucket, "(", "\\(", 0);
                bucket = llReplaceSubString(bucket, ")", "\\)", 0);
                bucket = llReplaceSubString(bucket, "{", "\\{", 0);
                bucket = llReplaceSubString(bucket, "}", "\\}", 0);
                bucket = llReplaceSubString(bucket, "|", "\\|", 0);
                bucket = "^" + prefix + "(" + llReplaceSubString(bucket, llChar(1), "|", 0) + ")$";
                integer deleted = llList2Integer(llLinksetDataDeleteFound(bucket, ""), 0);
                if(deleted) pending += [action, ""];
            }
            
            else if(action == LINKSETDATA_RESET)
            {
                string prefix = llGetSubString(message, 2, -1);
                if(!inScope(prefix)) return;
                // llLinksetDataReset(); -- Takes out all data even locally scoped, so we convert it to a multi-delete instead
                integer deleted = llList2Integer(llLinksetDataDeleteFound("^" + prefix, ""), 0);
                if(deleted) pending += [LINKSETDATA_MULTIDELETE, ""];
            }
        }
        
        // TODO: Would this be desirable? Would have to specify a common channel though. The idea would be to piggyback on the secure comms to send messages across
        // // Remote link message event
        // else if(eventType == EVENT_MESSAGE)
        // {
        //     integer textLength = llOrd(message, 1);
        //     string text = llGetSubString(message, 2, 1 + textLength);
        //     string text2 = llGetSubString(message, 2 + textLength, -1);
        //     llMessageLinked(LINK_SET, MESSAGE_SYNC, text, text2);
        // }
        
        // Full synchronisation with peer
        else if(eventType == EVENT_SYNC_REQUEST)
        {
            list requestScopes;
            integer requestIterator;
            integer requestTotal;
            if(llStringLength(message) > 1)
            {
                requestScopes = llParseString2List(llGetSubString(message, 1, -1), [llChar(1)], []);
                requestTotal = llGetListLength(requestScopes);
            }
            
            integer sent = 0;
            integer iterator = llGetListLength(scopes);
            while(iterator --> 0)
            {
                string pattern = "^" + llList2String(scopes, iterator);
                list names = llLinksetDataFindKeys(pattern, 0, FALSE); // TODO: Change to paginate the results
                integer nameIterator = llGetListLength(names);
                while(nameIterator --> 0)
                {
                    string name = llList2String(names, nameIterator);
                    if(requestTotal)
                    {
                        for(requestIterator = 0; requestIterator < requestTotal; ++requestIterator)
                        {
                            string requestPrefix = llList2String(requestScopes, requestIterator);
                            if(llSubStringIndex(name, requestPrefix) == 0) jump requestMatch;
                        }
                        jump requestSkip;
                    }
                    @requestMatch;
                    
                    string value = llLinksetDataRead(name);
                    sendData(identifier, channel, EVENT_SYNC_RESPONSE, name, value);
                    if((++sent % 32) == 0) llSleep(4/45.); // Avoid flooding
                    
                    @requestSkip;
                }
            }
            
            llRegionSayTo(identifier, channel, llChar(EVENT_SYNC_COMPLETE));
        }
    }
    
    linkset_data(integer action, string name, string value)
    {
        // Skip if this was expected and a change by this script from an external source
        if(pending)
        {
            integer pendingAction = llList2Integer(pending, 0);
            string pendingName = llList2String(pending, 1);
            if(action == pendingAction)
            {
                if((action == LINKSETDATA_UPDATE || action == LINKSETDATA_DELETE) && name == pendingName)
                    { pending = llDeleteSubList(pending, 0, 1); return; }
                
                else // MULTIDELETE or RESET
                    { pending = llDeleteSubList(pending, 0, 1); return; }
            }
        }
        
        if(action == LINKSETDATA_UPDATE)
        {
            integer iterator = llGetListLength(scopes);
            while(iterator --> 0)
            {
                string prefix = llList2String(scopes, iterator);
                if(llSubStringIndex(name, prefix) == 0)
                {
                    integer channel = llList2Integer(scopeChannels, iterator);
                    return sendData(NULL_KEY, channel, EVENT_DATA, name, value);
                }
            }
        }
        
        else if(action == LINKSETDATA_DELETE)
        {
            integer iterator = llGetListLength(scopes);
            while(iterator --> 0)
            {
                string prefix = llList2String(scopes, iterator);
                if(llSubStringIndex(name, prefix) == 0)
                {
                    integer channel = llList2Integer(scopeChannels, iterator);
                    llRegionSay(channel, llChar(EVENT_DATA) + llChar(action + 1) + name);
                    return;
                }
            }
        }
        
        else if(action == LINKSETDATA_RESET)
        {
            integer iterator = llGetListLength(scopes);
            while(iterator --> 0)
            {
                string prefix = llList2String(scopes, iterator);
                integer channel = llList2Integer(scopeChannels, iterator);
                llRegionSay(channel, llChar(EVENT_DATA) + llChar(action + 1) + prefix);
            }
        }
        
        else if(action == LINKSETDATA_MULTIDELETE)
        {
            list deleted = llParseString2List(name, [","], []);
            list known;
            integer total = llGetListLength(scopes);
            integer iterator = total;
            while(iterator --> 0) known += "";
            
            do {
                name = llList2String(deleted, 0);
                deleted = llDeleteSubList(deleted, 0, 0);
                
                iterator = total;
                while(iterator --> 0)
                {
                    string prefix = llList2String(scopes, iterator);
                    if(llSubStringIndex(name, prefix) == 0)
                    {
                        name = llDeleteSubString(name, 0, llStringLength(prefix) - 1);
                        
                        string bucket = llList2String(known, iterator);
                        if(bucket) bucket += llChar(1) + name; else bucket += name;
                        known = llListReplaceList(known, [bucket], iterator, iterator);
                    }
                }
            } while(deleted);
            
            iterator = total;
            while(iterator --> 0)
            {
                string bucket = llList2String(known, iterator);
                if(bucket)
                {
                    string prefix = llList2String(scopes, iterator);
                    integer channel = llList2Integer(scopeChannels, iterator);
                    llRegionSay(channel, llChar(EVENT_DATA) + llChar(action + 1) + llChar(llStringLength(prefix)) + prefix + bucket);
                    // TODO: Check if we need to fragment the bucket across multiple messages
                }
            }
        }
    }
}

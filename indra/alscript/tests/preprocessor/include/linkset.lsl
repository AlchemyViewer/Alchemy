////////////////////////////////////////////////////////////////////////////////////////////
/// Linkset scanning functions
/// These functions return linkset numbers

// Returns a list of linkset numbers from a prim name
list LinksByName(string needle) {
    list needles;
    integer link = 1;
    integer prims = llGetNumberOfPrims();
    for(; link <= prims; ++link) if(llGetLinkName(link) == needle) needles += link;
    return needles;
}

// Returns a linkset number based on the name
integer LinkByName(string needle) {
    integer prims = llGetNumberOfPrims() + 1;
    while(--prims) if(llGetLinkName(prims) == needle) return prims;
    return FALSE;
}

// Converts a list of prim names to a list of linkset numbers
list LinksetList(list needles) {
    integer prims = llGetNumberOfPrims() + 1;
    while(--prims) {
        integer pointer = llListFindList(needles, [llGetLinkName(prims)]);
        if(~pointer) needles = llListReplaceList(needles, [prims], pointer, pointer);
    }
    return needles;
}

// Roll your own loop boilerplate
#define LinksetScan(conditions) \
    integer link = llGetNumberOfPrims();\
    do {\
        string linkName = llGetLinkName(link);\
        conditions\
    } while(--link > 1);

/* Usage:
LinksetScan(
    if(linkName == "Foot") Foot = link;
    else if(linkName == "Leg") Leg = link;
    else if(linkName == "Torso") Torso = link;
    else if(linkName == "Head") Head = link;
);
*/




////////////////////////////////////////////////////////////////////////////////////////////
/// Remote Object Linkset scanning variants
/// These functions return keys instead of linkset numbers

// Returns a list of linkset keys from a prim name
list ObjectLinksByName(key object, string needle) {
    list needles;
    integer link = 1;
    integer prims = llGetObjectPrimCount(object);
    for(; link <= prims; ++link)
    {
        key linkKey = llGetObjectLinkKey(object, link);
        string linkName = llKey2Name(linkKey);
        if(linkName == needle) needles += linkKey;
    }
    return needles;
}

// Returns a linkset key based on the name
key ObjectLinkByName(key object, string needle) {
    integer prims = llGetObjectPrimCount(object) + 1;
    while(--prims)
    {
        key linkKey = llGetObjectLinkKey(object, prims);
        string linkName = llKey2Name(linkKey);
        if(linkName == needle) return linkKey;
    }
    return NULL_KEY;
}

// Converts a list of prim names to a list of linkset keys
list ObjectLinksetList(key object, list needles) {
    integer prims = llGetObjectPrimCount(object) + 1;
    while(--prims) {
        key linkKey = llGetObjectLinkKey(object, prims);
        string linkName = llKey2Name(linkKey);
        integer pointer = llListFindList(needles, [linkName]);
        if(~pointer) needles = llListReplaceList(needles, [linkKey], pointer, pointer);
    }
    return needles;
}

// Roll your own loop boilerplate
#define ObjectLinksetScan(object, conditions) \
    integer link = llGetObjectPrimCount(object);\
    do {\
        key linkKey = llGetObjectLinkKey(object, link);\
        string linkName = llKey2Name(linkKey);\
        conditions\
    } while(--link > 1);

/* Usage:
ObjectLinksetScan(object,
    if(linkName == "Turret") ...
    else if(linkName == "Barrel") ...
);
*/


// Scans an object for a list of sitting avatars
list ObjectLinksetSittingAvatars(object) {
    list details = llGetObjectDetails(object, [OBJECT_PRIM_COUNT, OBJECT_SIT_COUNT]);
    integer prims = llList2Integer(details, 0);
    integer sitters = llList2Integer(details, 1);
    
    list agents;
    integer index = prims + 1;
    integer count = prims + sitters;
    for(; index <= count; ++index) agents += llGetObjectLinkKey(object, index);
    
    return agents;
}




////////////////////////////////////////////////////////////////////////////////////////////
/// "Linkset Resources" use Linkset Data to store/cache an array of linkset numbers in a compact format
/// The usecase is typically for reusable prims/meshes in a linkset such as in a HUD or user interface
/// For example for dynamically rendering markers, text or for data visualisation

// Sets up a Linkset Resource on the key kv, finding prims in the linkset whose name exactly matches pattern to put into the resource
LinksetResourceSetup(string kv, string pattern) {
    string links;
    integer iterator = llGetNumberOfPrims();
    while(iterator --> 0)
    {
        string name = llGetLinkName(1 + iterator);
        if(name == pattern) links = llChar(iterator) + links;
    }
    llLinksetDataWrite(kv, links);
}

// Pulls out a linkset number from the resource
integer LinksetResourceReserve(string kv) {
    string links = llLinksetDataRead(kv);
    if(links == "") return FALSE;
    llLinksetDataWrite(kv, llDeleteSubString(links, 0, 0));
    return 1 + llOrd(links, 0);
}

// Releases a linkset number back into the resource
LinksetResourceRelease(string kv, integer link) {
    llLinksetDataWrite(kv, llChar(link - 1) + llLinksetDataRead(kv));
}

// Resets the prim params for a Linkset Resource, e.g. to reset a bunch of reusable prims to a default state out of sight
LinksetResourceReset(string kv, list reset) {
    list params;
    string links = llLinksetDataRead(kv);
    integer iterator = llStringLength(links);
    while(iterator --> 0)
    {
        params += [PRIM_LINK_TARGET, 1 + llOrd(links, iterator)] + reset;
        if(llGetFreeMemory() < 1500) { llSetLinkPrimitiveParamsFast(0, params); params = []; }
    }
    if(params) llSetLinkPrimitiveParamsFast(0, params);
}

integer LinksetResourceLookup(string kv, integer index) {
    string links = llLinksetDataRead(kv);
    if(links) return 1 + llOrd(links, index);
    else return FALSE;
}



// These two functions build upon linkset resources at a higher level, so that you can trade linkset numbers between resources
// For example to have one linkset resource of unused reusable prims, and another for reusable prims you are currently using to render in a UI

// Takes out a linkset number from the "Pool" resource and puts it into the "Used" resource
// You can flip the kv's to do the reverse
integer LinksetResourceUse(string kvPool, string kvUsed) {
    string links = llLinksetDataRead(kvPool);
    if(links == "") return FALSE;
    llLinksetDataWrite(kvPool, llDeleteSubString(links, 0, 0));
    integer link = 1 + llOrd(links, 0);
    llLinksetDataWrite(kvUsed, llChar(link - 1) + llLinksetDataRead(kvUsed));
    return link;
}

// Frees up all the linkset numbers by moving all from the "Used" resource and puts them back into "Pool" resource
LinksetResourceFreeAll(string kvPool, string kvUsed) {
    llLinksetDataWrite(kvPool, llLinksetDataRead(kvUsed) + llLinksetDataRead(kvPool));
    llLinksetDataWrite(kvUsed, "");
}

// TODO: A way to move a specific linkset number from a "Used" resource into the "Pool" resource in one function
// LinksetResourceFree(string kvPool, string kvUsed, integer link) {...}

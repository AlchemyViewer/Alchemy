// Implementation of a public Remote Linkset Data storage system, similar in practice to llLinksetDataRead but on a remote object
// Gotcha is that this works entirely through prim params (names, descriptions and/or hover text to store data)
// This does mean that this it is mostly suitable for a server object to hold the remote linkset data

// PRIM_NAME -- 63 bytes, restricted UTF-8 range, 32-126 except 124 ("|")
// PRIM_DESC -- 127 bytes, restricted UTF-8 range, 32-126 except 124 ("|")
// PRIM_TEXT -- 254 bytes, full UTF-8 range -- best for storing the raw values chunked across multiple prims
// -- Warning: Any PRIM_TEXT with unicode in range 0x1-0x1F via llChar corrupts the object from being rezzed from inventory
// Storage capacity is dependent on the number of prims in the linkset
// Values are divided by 254 bytes per child prim's PRIM_TEXT, but it is not guaranteed to utilise capacity fully
// as data is reserved in 254 byte chunks for each `name:value` pair, so e.g. if you have a linkset of 61 prims for example (where 1 prim is the root):
// - 60x`name:value` pairs if value is <= 254 bytes, or
// - 30x`name:value` pairs if values are up to 254*2 bytes.

string RemoteLinksetDataRead(key object, string name)
{
    if(object == llGetKey() || object == NULL_KEY) return llLinksetDataRead("Remote." + name);
    
    // TODO: Lookup table on root prim?
    integer link = 1;
    @remoteReadSearch;
    key prim = llGetObjectLinkKey(object, ++link);
    if(prim == NULL_KEY) return "";
    string primName = llKey2Name(prim);
    if(primName != name) jump remoteReadSearch;
    
    string value;
    @remoteReadLoop;
    value += (string)llGetObjectDetails(prim, [OBJECT_TEXT]);
    prim = llGetObjectLinkKey(object, ++link);
    primName = llKey2Name(prim);
    if(primName == name) jump remoteReadLoop;
    
    return value;
}

integer RemoteLinksetDataWrite(key object, string name, string value)
{
    if(object != llGetKey() && object != NULL_KEY) return 6; // Currently only allow writing from own object to keep the implementation straightforward
    
    // TODO: Check many prims we have left available for writing (any prims free at all + chunk/prim for full value)
    
    // We mirror the data locally for rebuilds
    integer result = llLinksetDataWrite("Remote." + name, value);
    if(result) return result;
    
    RebuildRemoteLinksetData();
    
    return LINKSETDATA_OK;
}



// INTERNAL -- Rebuild entire remote dataset, this is not efficient but it keeps the implementation robust, avoiding defragmentation issues
RebuildRemoteLinksetData()
{
    list params;
    
    list names = llLinksetDataFindKeys("^Remote\\.", 0, 0);
    integer link = 2;
    #ifdef RLSD_LINK_START
    link = RLSD_LINK_START;
    #endif
    integer index;
    integer count = llGetListLength(names);
    vector lastColor;
    for(; index < count; ++index)
    {
        string name = llList2String(names, index);
        string value = llLinksetDataRead(name);
        name = llDeleteSubString(name, 0, 6);
        
        vector color = (vector)llLinksetDataRead("RemoteDebug." + name);
        while(color == ZERO_VECTOR || color * lastColor > 0.8) llLinksetDataWrite("RemoteDebug." + name, string(color = llVecNorm(<llFrand(1), llFrand(1), llFrand(1)>)));
        lastColor = color;
        
        integer chunks = llCeil(llStringLength(value) / 254.);
        while(chunks --> 0)
        {
            string chunk = llGetSubString(value, 0, 253);
            value = llDeleteSubString(value, 0, 253);
            params += [
                PRIM_LINK_TARGET, link++,
                PRIM_NAME, name,
                PRIM_TEXT, chunk, <0, 0, 0>, 0
                
                #ifdef RLSD_DISPLAY
                , PRIM_COLOR, 0, color, 1
                #endif
            ];
        }
    }
    
    // TODO: Lookup table on root prim?
    
    llSetLinkPrimitiveParamsFast(LINK_ALL_CHILDREN, [
        PRIM_NAME, "-",
        PRIM_TEXT, "", <0, 0, 0>, 0
        
        #ifdef RLSD_DISPLAY
        , PRIM_COLOR, 0, <0, 0, 0>, 1
        #endif
    ] + params);
}

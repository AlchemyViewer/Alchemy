// NexiiLSL's app-uri.lsl (include/NexiiLSL/LICENSE), each of its macros
// called: a type's name as a cast inside a body, and a call's argument
// that is itself a call.












default
{
    touch_start(integer count)
    {
        key toucher = llDetectedKey(0);
        llOwnerSay("secondlife:///app/agent/" + ((string)(toucher)) + "/inspect");
        llOwnerSay("secondlife:///app/agent/" + ((string)(toucher)) + "/about");
        llOwnerSay("secondlife:///app/region/" + llEscapeURL(llGetRegionName()));
        llOwnerSay("secondlife:///app/objectim/" + ((string)(llGetKey())) + "?name=" + llEscapeURL(llKey2Name(llGetKey())) + "&owner=" + (string)llGetOwnerKey(llGetKey()));
        llOwnerSay("secondlife:///app/objectim/" + ((string)(llGetKey())) + "?name=" + llEscapeURL(llGetObjectName()) + "&owner=" + ((string)(llGetOwner())));
        llOwnerSay("secondlife:///app/group/" + ((string)(llList2Key(llGetObjectDetails(llGetKey(), [OBJECT_GROUP]), 0))) + "/inspect");
        llOwnerSay("secondlife:///app/group/" + ((string)(NULL_KEY)) + "/about");
        llOwnerSay("secondlife:///app/experience/" + ((string)(NULL_KEY)) + "/profile");
        llOwnerSay("secondlife:///app/chat/" + ((string)(-42)) + "/" + llEscapeURL("hello there"));
        llOwnerSay("[" + "https://wiki.secondlife.com" + " " + "the wiki" + "]");
    }
}

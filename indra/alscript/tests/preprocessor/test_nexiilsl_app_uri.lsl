// NexiiLSL's app-uri.lsl (include/NexiiLSL/LICENSE), each of its macros
// called: a type's name as a cast inside a body, and a call's argument
// that is itself a call.
#include "NexiiLSL/app-uri.lsl"

default
{
    touch_start(integer count)
    {
        key toucher = llDetectedKey(0);
        llOwnerSay(agentURI(toucher));
        llOwnerSay(agentAboutURI(toucher));
        llOwnerSay(regionURI(llGetRegionName()));
        llOwnerSay(objectURI(llGetKey()));
        llOwnerSay(fullObjectURI(llGetKey(), llGetObjectName(), llGetOwner()));
        llOwnerSay(groupURI(llList2Key(llGetObjectDetails(llGetKey(), [OBJECT_GROUP]), 0)));
        llOwnerSay(groupAboutURI(NULL_KEY));
        llOwnerSay(experienceURI(NULL_KEY));
        llOwnerSay(chatURI(-42, "hello there"));
        llOwnerSay(wikiLink("https://wiki.secondlife.com", "the wiki"));
    }
}

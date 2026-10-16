// NexiiLSL's utilities.lsl (include/NexiiLSL/LICENSE), its macros called
// where a condition goes.
#include "NexiiLSL/utilities.lsl"

default
{
    collision_start(integer count)
    {
        if(isSameOwner(llDetectedKey(0))) llOwnerSay("mine, the " + (string)count + nth(count));
        if(notSameOwner(llDetectedKey(0))) llOwnerSay((string)stringBytes(llDetectedName(0)));
    }
}

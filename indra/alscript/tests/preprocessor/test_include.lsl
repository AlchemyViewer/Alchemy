// Includes come from the object, inventory or disk; the resolver decides.
#include "common.lsl"
#include "config.lsl"
#include "common.lsl"

string here = __SHORTFILE__;

default
{
    state_entry()
    {
        llSay(LISTEN_CHANNEL, GREETING + " " + tag() + " " + (string)COMMON_VERSION + " " + here);
    }
}

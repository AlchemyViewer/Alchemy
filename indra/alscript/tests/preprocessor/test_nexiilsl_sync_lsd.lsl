// NexiiLSL's scripts/sync-lsd.lsl (include/NexiiLSL/LICENSE): a whole
// script, its numbers as macros; the one it does not use itself is used
// in a state after it.
#include "NexiiLSL/scripts/sync-lsd.lsl"

state events
{
    state_entry()
    {
        llOwnerSay((string)EVENT_MESSAGE);
    }
}

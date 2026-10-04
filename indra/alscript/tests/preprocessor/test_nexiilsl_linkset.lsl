// NexiiLSL's linkset.lsl as the repository has it now (include/NexiiLSL/
// LICENSE; include/linkset.lsl is an older copy), used as its comments
// show: statements for an argument, over several lines of their own.
#include "NexiiLSL/linkset.lsl"

integer Foot;
integer Leg;

default
{
    state_entry()
    {
        {
            LinksetScan(
                if(linkName == "Foot") Foot = link;
                else if(linkName == "Leg") Leg = link;
            );
        }
        {
            key object = llGetKey();
            ObjectLinksetScan(object,
                if(linkName == "Turret") llOwnerSay("turret " + (string)linkKey);
                else if(linkName == "Barrel") llOwnerSay("barrel " + (string)linkKey);
            );
        }
    }
}

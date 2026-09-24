// Nexii's linkset library, as NexiiLSL has it (include/linkset.LICENSE),
// used as its comments show: macros whose bodies run over several lines
// by backslashes, called with statements for an argument, over several
// lines of their own. Each scan declares its own link, so each has a
// block of its own.
#include "linkset.lsl"

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

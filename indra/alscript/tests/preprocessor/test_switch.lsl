// preprocessor: switches
integer state_of(integer n)
{
    switch (n)
    {
        case 0:
            return 10;
        case 1:
        case 2:
        {
            integer m = n * 2;
            switch (m)
            {
                case 4: return 40;
                default: break;
            }
            return m;
        }
        default:
            llOwnerSay("other");
            break;
    }
    return -1;
}

default
{
    touch_start(integer total)
    {
        integer owner = llDetectedKey(0) == llGetOwner();
        switch (owner)
        {
            case TRUE: llSay(0, "owner"); break;
            case FALSE: llSay(0, "visitor");
        }
    }
}

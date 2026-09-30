default
{
    state_entry()
    {
        if (1) { // $[E20012]
            jump x; // will jump to wrong label $[E20016]
            @x; // declared but never used $[E20009]
        }
        if (0) { // $[E20013]
            jump x; // this is fine
            @x; // same label name used multiple times $[E20017]
            llOwnerSay("Oh god");
        }
    }
}

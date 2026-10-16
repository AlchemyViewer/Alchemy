// Conditional compilation, with the switches scripts set near the top.












string mode = "new";











string extra = "none";



string never = "";






default
{
    state_entry()
    {
        llOwnerSay("mode " + mode + " " + extra);
    }
}

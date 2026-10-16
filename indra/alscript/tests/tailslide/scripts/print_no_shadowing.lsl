default
{
    state_entry()
    {
        integer print = 1; // not valid, "print" is actually a keyword in the grammar!
        llOwnerSay((string)print);
    }
}

// Object-like and function-like macros, as scripts written for Firestorm use them.












integer counter = 0;
vector origin = <0, 0, 0.5>;

default
{
    state_entry()
    {
        llSay(42, "hello " + "CHANNEL");
        llOwnerSay("[" + "test_macros.lsl" + ":" + (string)22 + "] " + "started" + " " + llList2CSV([counter, origin]));
        llSay(0, "first " + (string)(counter)); llSay(0, "second " + (string)(counter));
        if (1) llListen(42, "", llGetOwner(), "");
        integer biggest = ((counter) > (10) ? (counter) : (10));
        string s = "SAY(not here)"; // SAY(nor here)
        /* SAY(nor here either) */
    }
}

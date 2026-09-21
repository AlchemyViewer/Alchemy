// Includes come from the object, inventory or disk; the resolver decides.


// Shared helpers, included wherever they are needed and read once.

string tag() { return "common.lsl" + " " + "NOT_IN_WORLD"; }





string here = "test_include.lsl";

default
{
    state_entry()
    {
        llSay(7, "hi from " + "test_include.lsl" + " " + tag() + " " + (string)2 + " " + here);
    }
}

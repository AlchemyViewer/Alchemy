// LL's compiler takes a list variable in a list literal, which the VM makes
// a list holding a list, refused as nesting when it reaches the heap. The
// inner list it read was dropped unfreed; it is freed now.
//
// expect fault: Lists may not contain lists

default
{
    state_entry()
    {
        list inner = [1, "two"];
        list outer = [0, inner, 3];
        llSay(0, (string)llGetListLength(outer));
    }
}

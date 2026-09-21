// lso
list items = [1, 2, 3];
default
{
    state_entry()
    {
        list copy = [llList2Integer(items, 0), "two"];
        integer n = llGetListLength(copy) + llGetListLength(items);
        float f = 0.1 + 0.2;
        llSay(0, (string)n + (string)f);
    }
}

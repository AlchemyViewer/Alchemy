// A float past an integer's range cast to an integer, which C++ leaves
// undefined, is the least integer, as the grid's x86 made it.
//
// expect said: -2147483648 -2147483648 2147483520 -2147483648 -2147483648 -2

default
{
    state_entry()
    {
        float big = 3.0e38;
        llSay(0, (string)((integer)big) + " " + (string)((integer)(-big)) + " " + (string)((integer)2147483520.0) + " " +
                 (string)((integer)-2147483648.0) + " " + (string)((integer)2147483648.0) + " " + (string)((integer)-2.9));
    }
}

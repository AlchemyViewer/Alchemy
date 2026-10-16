// LL's compiler, laying out a global list literal for LSO, makes no entry
// for a global it refuses there, an unset one, and pointed the entries
// after it at that null entry's next, an offset from nothing. The error
// returns every recursion after it before any is built there, and the
// pointer is no longer taken either way; this holds the refusal.
//
// expect refused: Unitialized variables can't be included in lists

integer x;
list l = [x, 1];

default
{
    state_entry()
    {
        llSay(0, (string)l);
    }
}

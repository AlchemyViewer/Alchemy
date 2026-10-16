// LL's compiler, laying out a global list literal for LSO, made no entry
// for a global it refused there, an unset one, and then built the entries
// after it from where that entry's next pointer would have been, at an
// offset from nothing. The refusal stands, and the rest are left unbuilt.
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

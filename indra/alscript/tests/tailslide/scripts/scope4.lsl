string foo = "foobar";

integer llOwnerSay = 1; // globals may not shadow builtin functions $[E10026] $[E20009]

default
{
    state_entry()
    {
        integer llOwnerSay = 1; // $[E20009] $[E20001] allowed, but unused.
        // this should fail
        integer foo; // $[E20001] shadow
        {
            llOwnerSay(foo); // $[E10011] type mismatch
        }
        if (0) // $[E20013]
            llOwnerSay(foo); // $[E10011] type mismatch
    }

    touch_start(integer x)
    {
        {
            llOwnerSay(foo);
        }
        if (0) // $[E20013]
            llOwnerSay(foo);
        // this is fine
        integer foo; // $[E20009] unused $[E20001] shadow
    }
}

state llSay { // globals may not shadow builtin functions $[E10026] $[E20009]
    state_entry(){}
}

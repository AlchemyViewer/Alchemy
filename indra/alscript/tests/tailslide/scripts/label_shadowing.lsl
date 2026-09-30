string foo = "bar";

default
{
    state_entry()
    {
        jump after;
        do
            @foo; // $[E20009] $[E20001]
        while(1);
        @after;
        llOwnerSay(foo);
    }
}

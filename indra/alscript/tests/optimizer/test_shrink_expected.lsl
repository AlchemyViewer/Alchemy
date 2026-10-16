integer b;
c(string e)
{
    llSay(0, "Hello, world: " + e);
}

default
{
    state_entry()
    {
        c("started");
    }

    touch_start(integer f)
    {
        integer d;
        for (d = 0; d < f; ++d)
        {
            ++b;
            c((string)b);
        }
        @g;
        if (b < 101)
            jump g;
        state a;
    }
}
state a
{
    state_entry()
    {
        c("counting");
        state default;
    }
}

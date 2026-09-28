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
        for (d = 0; d < f; d = d + 1)
        {
            b = b + 1;
            c((string)b);
        }
        @g;
        if (b > 100)
            jump h;
        jump g;
        @h;
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

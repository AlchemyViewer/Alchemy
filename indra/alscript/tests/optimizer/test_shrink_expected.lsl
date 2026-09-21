integer a;
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
        integer b;
        for (b = 0; b < f; b = b + 1)
        {
            a = a + 1;
            c((string)a);
        }
        @g;
        if (a > 100)
            jump h;
        jump g;
        @h;
        state d;
    }
}
state d
{
    state_entry()
    {
        c("counting");
        state default;
    }
}

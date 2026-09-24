integer a;
c(string d)
{
    llSay(0, "Hello, world: " + d);
}

default
{
    state_entry()
    {
        c("started");
    }

    touch_start(integer e)
    {
        integer b;
        for (b = 0; b < e; b = b + 1)
        {
            a = a + 1;
            c((string)a);
        }
        @f;
        if (a > 100)
            jump g;
        jump f;
        @g;
        state h;
    }
}
state h
{
    state_entry()
    {
        c("counting");
        state default;
    }
}

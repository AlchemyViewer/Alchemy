// shrinknames addstrings
integer total_touches;
string greeting = "Hello, " + "world";

announce(string what)
{
    llSay(0, greeting + ": " + what);
}

default
{
    state_entry()
    {
        announce("started");
    }

    touch_start(integer how_many)
    {
        integer index;
        for (index = 0; index < how_many; index = index + 1)
        {
            total_touches = total_touches + 1;
            announce((string)total_touches);
        }
        @again;
        if (total_touches > 100) jump done;
        jump again;
        @done;
        state counting;
    }
}

state counting
{
    state_entry()
    {
        announce("counting");
        state default;
    }
}

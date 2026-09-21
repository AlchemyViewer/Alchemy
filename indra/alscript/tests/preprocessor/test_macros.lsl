// Object-like and function-like macros, as scripts written for Firestorm use them.
#define CHANNEL 42
#define OWNER_ONLY 1
#define SAY(text) llSay(CHANNEL, text)
#define DEBUG(fmt, ...) llOwnerSay("[" + __SHORTFILE__ + ":" + (string)__LINE__ + "] " + fmt + " " + llList2CSV([__VA_ARGS__]))
#define STR(x) #x
#define JOIN(a, b) a##b
#define VEC(x, y, z) <x, y, z>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LONG_BODY(a) \
    llSay(0, "first " + (string)(a)); \
    llSay(0, "second " + (string)(a))

integer JOIN(count, er) = 0;
vector origin = VEC(0, 0, 0.5);

default
{
    state_entry()
    {
        SAY("hello " + STR(CHANNEL));
        DEBUG("started", counter, origin);
        LONG_BODY(counter);
        if (OWNER_ONLY) llListen(CHANNEL, "", llGetOwner(), "");
        integer biggest = MAX(counter, 10);
        string s = "SAY(not here)"; // SAY(nor here)
        /* SAY(nor here either) */
    }
}

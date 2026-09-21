// Conditional compilation, with the switches scripts set near the top.
#define DEBUG 1
#define VERSION 3
#define FEATURE_A
#undef FEATURE_B

#if DEBUG
#define LOG(x) llOwnerSay(x)
#else
#define LOG(x)
#endif

#if VERSION >= 3 && defined(FEATURE_A)
string mode = "new";
#elif VERSION == 2
string mode = "old";
#else
string mode = "ancient";
#endif

#ifdef FEATURE_B
string extra = "b";
#endif

#ifndef FEATURE_B
string extra = "none";
#endif

#if (VERSION * 2 + 1) % 7 == 0 || !defined FEATURE_A
string never = "";
#else
#if DEBUG == 1
string nested = "yes";
#endif
#endif

default
{
    state_entry()
    {
        LOG("mode " + mode + " " + extra);
    }
}

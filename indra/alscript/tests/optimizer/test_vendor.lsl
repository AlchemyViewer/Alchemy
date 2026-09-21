// A small vendor, as scripts are written: constants at the top, helpers, a menu.
integer CHANNEL = -12345;
float PRICE = 10.0;
string NAME = "Widget";
integer DEBUG = 0;
list BUTTONS = ["Buy", "Info", "Close"];
integer listener;
integer unused_global;

debug(string text)
{
    if (DEBUG) llOwnerSay("[debug] " + text);
}

string describe()
{
    return NAME + " for L$" + (string)((integer)PRICE);
}

unused_helper(integer x)
{
    llSay(0, (string)x);
}

default
{
    state_entry()
    {
        integer i = 0;
        integer count = llGetListLength(BUTTONS);
        float half = PRICE / 2.0;
        llSetText(describe(), <1.0, 1.0, 1.0>, 1.0);
        debug("ready with " + (string)count + " buttons, half price " + (string)half);
        while (i < count)
        {
            debug(llList2String(BUTTONS, i));
            i = i + 1;
        }
    }

    touch_start(integer total)
    {
        integer unused_local = 5;
        listener = llListen(CHANNEL, "", llDetectedKey(0), "");
        llDialog(llDetectedKey(0), describe(), BUTTONS, CHANNEL);
        if (total != 0) llSetTimerEvent(30.0 * 1);
    }

    listen(integer channel, string name, key id, string message)
    {
        if (message == "Buy")
        {
            llSay(0, "Thanks!");
            return;
            llSay(0, "unreachable");
        }
        else if (message == "Info")
            llSay(0, describe());
        llListenRemove(listener);
        llSetTimerEvent(0.0);
    }

    timer()
    {
        llListenRemove(listener);
        llSetTimerEvent(0);
    }
}

state never
{
    state_entry()
    {
        llSay(0, "nobody comes here");
    }
}
